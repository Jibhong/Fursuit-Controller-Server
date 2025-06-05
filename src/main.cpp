#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// #include "BLECallbacks.h"

#include <Adafruit_NeoPixel.h>

#include <ArduinoJson.h>

typedef struct fan_settings {
	const std::string uuid;
	const int pin1;
	const int pin2;
	int speed;
} FanSettings;

typedef struct led_settings {
	const std::string uuid;
	const int pin;
	const int stripLength;
	Adafruit_NeoPixel& strip; // Adafruit_NeoPixel instance for controlling the LEDs

	std::vector<std::vector<uint32_t>> animation;
	float brightness; //brightness multiplier (0.0-3.0)
	int animationDelay; //delay between animation frames in milliseconds

	std::vector<uint32_t> lastAnimation;
	std::vector<uint32_t> nextAnimation;
	unsigned int lastFrameIndex;

} LedSettings;

// UUIDs
const std::string FAN_SERVICE_UUID = "87a728cc-b26b-4f5e-aab7-25b6c82a1434";
std::array<FanSettings, 5> FAN_CHARACTERISTIC_UUID = {{
	{"6f5a70c8-b114-4c50-ab97-f33a6253d482", 16, 25},
	{"aadd3fa8-d8be-4d8f-bd36-b2d9df0ca8a8", 17, 26},
	{"ba96a026-921f-48b9-a6e1-0e496f14264f", 5 , 27},
	{"292842c4-795c-4e2c-b1b4-47a733d41a94", 18, 14},
	{"d4eebdd0-e824-4754-81f8-f04b1820b57c", 19, 12}
}};

Adafruit_NeoPixel ledStrip1(18, 22, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel ledStrip2(18, 23, NEO_GRB + NEO_KHZ800);

const std::string LED_SERVICE_UUID = "56956ce8-6d0d-4919-8016-8ba7ea56b350";
std::array<LedSettings, 2> LED_CHARACTERISTIC_UUID = {{
	{"5952eae5-f5da-4be1-adad-795a663c3aec", 22, 18, ledStrip1, {{0xFF0000, 0x00FF00, 0x0000FF},{0xFFFF00, 0xFF00FF, 0x00FFFF}}, 1.0, 300},
	{"f0e2c120-01f6-4fc6-982d-de2c45b1623d", 23, 18, ledStrip2, {{0xFF0000, 0x00FF00, 0x0000FF},{0xFFFF00, 0xFF00FF, 0x00FFFF}}, 1.0, 300}
}};

BLEServer* pServer = nullptr;

BLEService* pFanService = nullptr;
BLECharacteristic* pFanCharacteristics[5];

BLEService* pLedService = nullptr;
BLECharacteristic* pLedCharacteristics[9];

// Handles Fan characteristic write events
class FanCharacteristicCallback : public BLECharacteristicCallbacks {
    std::string uuid;

public:
    explicit FanCharacteristicCallback(const std::string& uuid_) : uuid(uuid_) {}

    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string inputString = pCharacteristic->getValue();

        Serial.print("Fan ");
        Serial.print(uuid.c_str());
        Serial.print(": ");

        for (char c : inputString) {
            Serial.print(c);
        }
        Serial.println();
    }
};

// Handles LED characteristic write events
class LedCharacteristicCallback : public BLECharacteristicCallbacks {
    std::string uuid;

public:
    explicit LedCharacteristicCallback(const std::string& uuid_) : uuid(uuid_) {}

    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string inputString = pCharacteristic->getValue();

        Serial.print("LED ");
        Serial.print(uuid.c_str());
        Serial.print(": ");

        for (char c : inputString) {
            Serial.print(c);
        }
        Serial.println();

		int uuidIndex = -1; // Assuming handle starts from 1
		for(int i = 0; i < LED_CHARACTERISTIC_UUID.size(); ++i) {
			if (LED_CHARACTERISTIC_UUID[i].uuid != uuid) continue;
			uuidIndex = i;
			break;
		}
		if(uuidIndex == -1) {
			Serial.println("ERROR: LED Characteristic not found");
			return;
		}
		Serial.println("LED Characteristic Index: " + String(uuidIndex));

		// {animation:{{0xFF0000, 0x00FF00, 0x0000FF},{0xFFFF00, 0xFF00FF, 0x00FFFF}}, brightness: 1.0, delay: 300};
		// Parse input as JSON and update LED settings
		JsonDocument inputJson;
		DeserializationError error = deserializeJson(inputJson, inputString);
		if (error) {
			Serial.print("JSON parse failed: ");
			Serial.println(error.c_str());
			return;
		}

		// Parse animation array (hex color strings)
		if (inputJson["animation"].is<JsonArray>()) {
			LED_CHARACTERISTIC_UUID[uuidIndex].animation.clear();
			for (JsonVariant frameVar : inputJson["animation"].as<JsonArray>()) {
				std::vector<uint32_t> frame;
				if (frameVar.is<JsonArray>()) {
					for (JsonVariant colorVar : frameVar.as<JsonArray>()) {
						if (colorVar.is<const char*>()) {
							std::string colorStr = colorVar.as<const char*>();
							if (colorStr.size() == 6) {
								uint32_t color = (uint32_t)strtoul(colorStr.c_str(), nullptr, 16);
								frame.push_back(color);
							}
						}
					}
				}
				if (!frame.empty()) {
					LED_CHARACTERISTIC_UUID[uuidIndex].animation.push_back(frame);
				}
			}
			Serial.print("LED Animation: ");
			for (const auto& frame : LED_CHARACTERISTIC_UUID[uuidIndex].animation) {
				Serial.print("[");
				for (const auto& color : frame) {
					Serial.print("#");
					char buf[7];
					snprintf(buf, sizeof(buf), "%06X", color);
					Serial.print(buf);
					Serial.print(" ");
				}
				Serial.print("] ");
			}
			Serial.println();
		}

		// Parse brightness
		if (inputJson["brightness"].is<float>()) {
			LED_CHARACTERISTIC_UUID[uuidIndex].brightness = inputJson["brightness"].as<float>();
			Serial.println("LED Brightness: " + String(LED_CHARACTERISTIC_UUID[uuidIndex].brightness));
		}

		// Parse delay
		if (inputJson["delay"].is<int>()) {
			LED_CHARACTERISTIC_UUID[uuidIndex].animationDelay = inputJson["delay"].as<int>();
			Serial.println("LED Animation Delay: " + String(LED_CHARACTERISTIC_UUID[uuidIndex].animationDelay));
		}
    }
};

// Handles server connection events
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        Serial.println("Device connected");
    }

    void onDisconnect(BLEServer* pServer) override {
        Serial.println("Device disconnected, restarting advertising");
        BLEDevice::startAdvertising(); // Restart advertising
    }
};

uint32_t lerpColor(uint32_t color1, uint32_t color2, double t) {
    uint8_t r1 = (color1 >> 16) & 0xFF;
    uint8_t g1 = (color1 >> 8) & 0xFF;
    uint8_t b1 = color1 & 0xFF;

    uint8_t r2 = (color2 >> 16) & 0xFF;
    uint8_t g2 = (color2 >> 8) & 0xFF;
    uint8_t b2 = color2 & 0xFF;

    uint8_t r = static_cast<uint8_t>(r1 + t * (r2 - r1));
    uint8_t g = static_cast<uint8_t>(g1 + t * (g2 - g1));
    uint8_t b = static_cast<uint8_t>(b1 + t * (b2 - b1));

    return (r << 16) | (g << 8) | b;
}

void initFirstFrame() {
	for (LedSettings& ledAnimation : LED_CHARACTERISTIC_UUID) {
		ledAnimation.lastAnimation.resize(ledAnimation.stripLength);
		ledAnimation.nextAnimation.resize(ledAnimation.stripLength);
		ledAnimation.lastFrameIndex = 0;
		const double segmentSize = double(ledAnimation.stripLength)/double(ledAnimation.animation.size());
		int nowIndex = 0;
		int nextIndex = segmentSize;
		for(int i=0;i<ledAnimation.stripLength; ++i) {
			if(i >= segmentSize*nextIndex)
			nowIndex = nextIndex;
			nextIndex = nowIndex + 1;
			if(nextIndex > ledAnimation.animation.size()) nextIndex = 0;
			double nowPercent = double(double(i)-(double(nowIndex)*segmentSize))/segmentSize;
			Serial.print("now: ");
			Serial.print(ledAnimation.animation[0][nowIndex], HEX);
			Serial.print(", next: ");
			Serial.print(ledAnimation.animation[0][nextIndex], HEX);
			Serial.print(", percent: ");
			Serial.print(nowPercent, 4);
			Serial.print("\n");
			ledAnimation.lastAnimation[i] = lerpColor(ledAnimation.animation[0][nowIndex], ledAnimation.animation[0][nextIndex], nowPercent);
			Serial.print(" ");
		}
		Serial.println();
	}
}

void setup() {
	// BLEDevice::deinit(true); // Force clear GATT table on device
	// delay(100);
	Serial.begin(9600);

	// Initialize NeoPixel strips
	ledStrip1.begin();
	ledStrip1.show(); // Initialize all pixels to 'off'
	ledStrip2.begin();
	ledStrip2.show(); // Initialize all pixels to 'off'

	// Initialize BLE device with a device name
	// BLEDevice::setMTU(517);
	BLEDevice::init("MyFanDevice");

	// Create BLE Server
	pServer = BLEDevice::createServer();

	// Callbacks for BLE Server
	pServer->setCallbacks(new MyServerCallbacks());

	// Create BLE Service
	pFanService = pServer->createService(FAN_SERVICE_UUID);
	pLedService = pServer->createService(LED_SERVICE_UUID);

	// Create Characteristics inside the Service
	for (int i=0; i<FAN_CHARACTERISTIC_UUID.size(); ++i) {
		Serial.println(("Creating Fan Characteristic: " + FAN_CHARACTERISTIC_UUID[i].uuid).c_str());
		pFanCharacteristics[i] = pFanService->createCharacteristic(
			FAN_CHARACTERISTIC_UUID[i].uuid,
			BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
		);
		
		// BLEDescriptor* userDesc = new BLEDescriptor(BLEUUID((uint16_t)0x2901));
		// userDesc->setValue(FAN_CHARACTERISTIC_UUID[i].description);
		// pFanCharacteristics[i]->addDescriptor(userDesc);

		// pFanCharacteristics[i]->addDescriptor(new BLE2902());
		pFanCharacteristics[i]->setValue("0");
		pFanCharacteristics[i]->setCallbacks(new FanCharacteristicCallback(FAN_CHARACTERISTIC_UUID[i].uuid));
	}

	for (int i = 0; i < LED_CHARACTERISTIC_UUID.size(); ++i) {
		Serial.println(("Creating LED Characteristic: " + LED_CHARACTERISTIC_UUID[i].uuid).c_str());
		pLedCharacteristics[i] = pLedService->createCharacteristic(
			LED_CHARACTERISTIC_UUID[i].uuid,
			BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
		);

		// BLEDescriptor* userDesc = new BLEDescriptor(BLEUUID((uint16_t)0x2901));
		// userDesc->setValue("LED Characteristic");
		// pLedCharacteristics[i]->addDescriptor(userDesc);

		// pLedCharacteristics[i]->addDescriptor(new BLE2902());
		pLedCharacteristics[i]->setValue("0");
		pLedCharacteristics[i]->setCallbacks(new LedCharacteristicCallback(LED_CHARACTERISTIC_UUID[i].uuid));
		// You may want to set callbacks here if needed, e.g.:
		// pLedCharacteristics[i]->setCallbacks(new LedCharacteristicCallback(LED_CHARACTERISTIC_UUID[i]));
	}

	// Start the service
	pFanService->start();
	pLedService->start();

	// Start advertising
	BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
	pAdvertising->addServiceUUID(FAN_SERVICE_UUID);
	pAdvertising->addServiceUUID(LED_SERVICE_UUID);

	pAdvertising->start();

	Serial.println("BLE Service started, waiting for clients to connect...");

	initFirstFrame();
}

void loop() {
	for (LedSettings& ledAnimation : LED_CHARACTERISTIC_UUID) {
		// unsigned int thisFrameIndex =(millis() / ledAnimation.animationDelay) % ledAnimation.animation.size();
		// double frameStep = (double(millis()) / double(ledAnimation.animationDelay)) - (millis() / ledAnimation.animationDelay);
		// std::vector<uint32_t>& thisFrame = ledAnimation.animation[thisFrameIndex];
		// Adafruit_NeoPixel& strip = ledAnimation.strip;
		// Serial.println(frameStep+thisFrameIndex);

		for (int i = 0; i < ledAnimation.stripLength; ++i) {
			uint32_t color = ledAnimation.lastAnimation[i];
			ledAnimation.strip.setPixelColor(i, color);
		}
		ledAnimation.strip.show();
	}
}
