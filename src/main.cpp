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
	const int pwmChannel;
	int speed;
} FanSettings;

typedef struct animation_container{
	std::vector<std::pair<float,float>> keyFrame; // pairs of (p1,p2) for the animation
	std::vector<uint32_t> color; // colors for the animation, stored as hex values (0xRRGGBB)
} AnimationContainer;

typedef struct led_settings {
	const std::string uuid;
	const int pin;
	const int stripLength;
	Adafruit_NeoPixel& strip; // Adafruit_NeoPixel instance for controlling the LEDs

	std::vector<AnimationContainer> animation;
	float brightness; //brightness multiplier (0.0-3.0)
	int animationDuration; //total duration of the animation in milliseconds
	float animationDelay; //delay between animation frames in milliseconds

	std::vector<uint32_t> nowFrame;
	unsigned int microFrameDelay;

} LedSettings;

// UUIDs
const std::string FAN_SERVICE_UUID = "87a728cc-b26b-4f5e-aab7-25b6c82a1434";
std::array<FanSettings, 5> FAN_CHARACTERISTIC_UUID = {{
	{"6f5a70c8-b114-4c50-ab97-f33a6253d482", 16, 25, 0, 255},
	{"aadd3fa8-d8be-4d8f-bd36-b2d9df0ca8a8", 17, 26, 1, 255},
	{"ba96a026-921f-48b9-a6e1-0e496f14264f", 5 , 27, 2, 255},
	{"292842c4-795c-4e2c-b1b4-47a733d41a94", 18, 14, 3, 255},
	{"d4eebdd0-e824-4754-81f8-f04b1820b57c", 19, 12, 4, 255}
}};

Adafruit_NeoPixel ledStrip1(18, 22, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel ledStrip2(18, 23, NEO_GRB + NEO_KHZ800);

const std::string LED_SERVICE_UUID = "56956ce8-6d0d-4919-8016-8ba7ea56b350";
std::array<LedSettings, 2> LED_CHARACTERISTIC_UUID = {{
	{"5952eae5-f5da-4be1-adad-795a663c3aec", 22, 18, ledStrip1, {{{{0.0,0.0},{0.25,0.75},{0.0,1.0}},{0xFF0000, 0x00FF00, 0x0000FF}}}, 0.2, 300, 1000},
	{"f0e2c120-01f6-4fc6-982d-de2c45b1623d", 23, 18, ledStrip2, {{{{0.0,0.1},{0.9,1.0}},{0x005050}}}, 0.2, 300, 1000}
}};

BLEServer* pServer = nullptr;

BLEService* pFanService = nullptr;
BLECharacteristic* pFanCharacteristics[5];

BLEService* pLedService = nullptr;
BLECharacteristic* pLedCharacteristics[9];

// PWM settings
const int fanPwmFrequency = 25000;        // 25 kHz
const int fanPwmresolution = 8;     // 8-bit resolution (0–255)

// Handles Fan characteristic write events
class FanCharacteristicCallback : public BLECharacteristicCallbacks {
    std::string uuid;

public:
    explicit FanCharacteristicCallback(const std::string& uuid_) : uuid(uuid_) {}

    void onWrite(BLECharacteristic* pCharacteristic) override {
		std::string inputString = pCharacteristic->getValue();
		if(inputString.empty())return;

		int inputSpeed = static_cast<int>(inputString[0]);

		Serial.print("Fan ");
		Serial.print(uuid.c_str());
		Serial.print(": ");
		Serial.println(inputSpeed);
		int fanIndex = 0;
		for(fanIndex;fanIndex<FAN_CHARACTERISTIC_UUID.size();++fanIndex){
			if(FAN_CHARACTERISTIC_UUID[fanIndex].uuid!=uuid) continue;
			FAN_CHARACTERISTIC_UUID[fanIndex].speed = std::min(255, std::max(0, inputSpeed));
			ledcWrite(FAN_CHARACTERISTIC_UUID[fanIndex].pwmChannel, FAN_CHARACTERISTIC_UUID[fanIndex].speed); 
			break;
		}
		
	}
};

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

uint32_t linearInterpolationColor(uint32_t color1, uint32_t color2, double t) {
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

uint32_t gammaInterpolationColor(uint32_t colorA, uint32_t colorB, double t) {
	const double gamma = 1.2; // try 1.2 or 1.0 if needed

	auto toLinear = [gamma](int c) -> double {
		return pow(c / 255.0, gamma);
	};

	auto toSRGB = [gamma](double c) -> int {
		return int(pow(std::max(0.0, std::min(1.0, c)), 1.0 / gamma) * 255.0 + 0.5);
	};

	int rA = (colorA >> 16) & 0xFF;
	int gA = (colorA >> 8) & 0xFF;
	int bA = colorA & 0xFF;

	int rB = (colorB >> 16) & 0xFF;
	int gB = (colorB >> 8) & 0xFF;
	int bB = colorB & 0xFF;

	double r = toLinear(rA) * (1.0 - t) + toLinear(rB) * t;
	double g = toLinear(gA) * (1.0 - t) + toLinear(gB) * t;
	double b = toLinear(bA) * (1.0 - t) + toLinear(bB) * t;

	return (toSRGB(r) << 16) | (toSRGB(g) << 8) | toSRGB(b);
}

uint32_t addRGB(uint32_t colorA, uint32_t colorB) {
	uint8_t rA = (colorA >> 16) & 0xFF;
	uint8_t gA = (colorA >> 8) & 0xFF;
	uint8_t bA = colorA & 0xFF;

	uint8_t rB = (colorB >> 16) & 0xFF;
	uint8_t gB = (colorB >> 8) & 0xFF;
	uint8_t bB = colorB & 0xFF;

	uint8_t r = std::max(rA , rB);
	uint8_t g = std::max(gA , gB);
	uint8_t b = std::max(bA , bB);

	return (r << 16) | (g << 8) | b;
}

void generateFrame(LedSettings &led, float t) {
	led.nowFrame.clear(); // Clear the nowFrame vector
	led.nowFrame.resize(led.stripLength, 0x000000); // Initialize the frame with black color
	for(AnimationContainer &toFill:led.animation){
		int nowIndex = (float)(toFill.keyFrame.size()-1) * t;
		// Serial.println(String("Now Index: ") + String(nowIndex) + " t: " + String(t) + " keyFrame size: " + String(toFill.keyFrame.size()));
		float nowPercent = t*(float)(toFill.keyFrame.size()-1) - nowIndex;
		float p1 = lerp(toFill.keyFrame[nowIndex].first, toFill.keyFrame[nowIndex+1].first, nowPercent) * (float)led.stripLength;
		float p2 = lerp(toFill.keyFrame[nowIndex].second, toFill.keyFrame[nowIndex+1].second, nowPercent) * (float)led.stripLength;
		//Fill color between p1 and p2
		float segmentSize = (float)abs(p2-p1) / (float)toFill.color.size();
		for(int ledIndex=ceil(p1); ledIndex<=floor(p2); ++ledIndex) {
			// Serial.println(String("LED Index: ") + String(ledIndex) + " p1: " + String(p1) + " p2: " + String(p2) + " segmentSize: " + String(segmentSize));
			int ledColorIndex = ((float)ledIndex-p1) / segmentSize;
			float ledColorPercent = ( ((float)ledIndex-p1) / segmentSize) - ledColorIndex;
			led.nowFrame[ledIndex] = gammaInterpolationColor(toFill.color[ledColorIndex], toFill.color[min(ledColorIndex+1,(int)toFill.color.size()-1)], ledColorPercent);
			// led.nowFrame[ledIndex] 	= 0x00FF00;
		}
		//Fill edge of p1 and p2
		for(int ledIndex=ceil(p2); ledIndex<=floor(p2+segmentSize); ++ledIndex) {
			int realIndex = ledIndex%led.stripLength;
			float ledColorPercent = ( ((float)ledIndex-p2) / segmentSize);
			led.nowFrame[realIndex] = addRGB(led.nowFrame[realIndex], gammaInterpolationColor(toFill.color[toFill.color.size()-1], 0x000000, ledColorPercent));
		}
		for(int ledIndex=floor(p1); ledIndex>=floor(p1-segmentSize); --ledIndex) {
			int realIndex = (ledIndex+led.stripLength)%led.stripLength;
			float ledColorPercent = ( ((float)ledIndex-p1+segmentSize) / segmentSize);
			led.nowFrame[realIndex] = addRGB(led.nowFrame[realIndex], gammaInterpolationColor(0x000000, toFill.color[0], ledColorPercent));
		}
	}
}

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

		// {animation:[channel:0, keyFrame:[[0.0,1.0],[0,5,1.0],[0.0,0.5]]]}
		// {animation:[channel:0, color:["ff0000", "00ff00", "0000ff"]]}
		// Parse animation array (channel is inside animation object)
		if (inputJson["animation"].is<JsonObject>()) {
			JsonObject animationObj = inputJson["animation"].as<JsonObject>();
			int channelIndex = 0;
			if (!animationObj["channel"].is<int>()) channelIndex = animationObj["channel"].as<int>();
			if (animationObj["keyFrame"].is<JsonArray>()) {
				JsonArray keyFrameInput = animationObj["keyFrame"].as<JsonArray>();
				std::vector<std::pair<float, float>> &ledColor = LED_CHARACTERISTIC_UUID[uuidIndex].animation[channelIndex].keyFrame;
				ledColor.clear();
				for (JsonArray kf : keyFrameInput) {
					if (kf.size() != 2) continue;
					float first = kf[0].as<float>();
					float second = kf[1].as<float>();
					ledColor.emplace_back(first, second);
				}
			}
			if (animationObj["color"].is<JsonArray>()) {
				JsonArray colorInput = animationObj["color"].as<JsonArray>();
				std::vector<uint32_t> &ledColor = LED_CHARACTERISTIC_UUID[uuidIndex].animation[channelIndex].color;
				ledColor.clear();
					for (JsonVariant v : colorInput) {
						std::string colorStr = v.as<const char*>();
						// Convert hex string to uint32_t
						uint32_t color = (uint32_t)strtol(colorStr.c_str(), nullptr, 16);
						ledColor.push_back(color);
						Serial.print(ledColor.back(), HEX);
						Serial.print(" ");
					}
					Serial.println();
			}
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
	BLEDevice::setMTU(517);
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

	// Initalize FAN
	for (int i = 0; i < FAN_CHARACTERISTIC_UUID.size(); i++) {
        ledcSetup(FAN_CHARACTERISTIC_UUID[i].pwmChannel, fanPwmFrequency, fanPwmresolution);              // Configure PWM
        ledcAttachPin(FAN_CHARACTERISTIC_UUID[i].pin1, FAN_CHARACTERISTIC_UUID[i].pwmChannel);  // Attach pin to channel
        ledcWrite(FAN_CHARACTERISTIC_UUID[i].pwmChannel, FAN_CHARACTERISTIC_UUID[i].speed);                           // 100% duty cycle
    }

	// Initialize LED
	// for (int i = 0; i < LED_CHARACTERISTIC_UUID.size(); i++) {
	for (LedSettings &led : LED_CHARACTERISTIC_UUID) {

		// LED_CHARACTERISTIC_UUID[i].lastFrameIndex=0;
		// fillStripWithPoint(LED_CHARACTERISTIC_UUID[i],LED_CHARACTERISTIC_UUID[i].lastAnimation,LED_CHARACTERISTIC_UUID[i].lastFrameIndex);
		// fillStripWithPoint(LED_CHARACTERISTIC_UUID[i],LED_CHARACTERISTIC_UUID[i].nextAnimation,(LED_CHARACTERISTIC_UUID[i].lastFrameIndex + 1) % (LED_CHARACTERISTIC_UUID[i].animation.size()));
		// generateFrame(LED_CHARACTERISTIC_UUID[i],3);
	}

}

void loop() {
    unsigned long now = millis();
    for (LedSettings &led : LED_CHARACTERISTIC_UUID) {
		// generateFrame(led, 0.5);
		generateFrame(led, ( ((float)(now)) / ((float)(led.animationDuration+led.animationDelay)) ) - ( (int)(now/(led.animationDuration+led.animationDelay)) ) );
		for (int i = 0; i < led.stripLength && i < led.nowFrame.size(); ++i) {
			uint32_t color = led.nowFrame[i];
			// Extract RGB components
			uint8_t r = (color >> 16) & 0xFF;
			uint8_t g = (color >> 8) & 0xFF;
			uint8_t b = color & 0xFF;
			float brightness = std::min(led.brightness, 1.0f);
			r = static_cast<uint8_t>(r * brightness);
			g = static_cast<uint8_t>(g * brightness);
			b = static_cast<uint8_t>(b * brightness);
			led.strip.setPixelColor(i, r, g, b);
		}
        led.strip.show();
    }
}
