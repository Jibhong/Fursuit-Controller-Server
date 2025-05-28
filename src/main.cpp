#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// UUIDs
const std::string FAN_SERVICE_UUID = "87a728cc-b26b-4f5e-aab7-25b6c82a1434";
const std::array<std::string, 5> FAN_CHARACTERISTIC_UUID = {
	"6f5a70c8-b114-4c50-ab97-f33a6253d482",
	"aadd3fa8-d8be-4d8f-bd36-b2d9df0ca8a8",
	"ba96a026-921f-48b9-a6e1-0e496f14264f",
	"292842c4-795c-4e2c-b1b4-47a733d41a94",
	"d4eebdd0-e824-4754-81f8-f04b1820b57c"
};

const std::string LED_SERVICE_UUID;
const std::array<std::string, 1> LED_CHARACTERISTIC_UUID = {
};

BLEServer* pServer = nullptr;

BLEService* pFanService = nullptr;
BLECharacteristic* pFanCharacteristics[5];

BLEService* pLedService = nullptr;
BLECharacteristic* pLedCharacteristics[1];

void setup() {
	Serial.begin(9600);

	// Initialize BLE device with a device name
	BLEDevice::init("MyFanDevice");

	// Create BLE Server
	pServer = BLEDevice::createServer();

	// Create BLE Service
	pFanService = pServer->createService(FAN_SERVICE_UUID);
	pLedService = pServer->createService(LED_SERVICE_UUID);

	// Create Characteristics inside the Service
	for (int i=0; i<FAN_CHARACTERISTIC_UUID.size(); ++i) {
		pFanCharacteristics[i] = pFanService->createCharacteristic(
			FAN_CHARACTERISTIC_UUID[i],
			BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
		);
		pFanCharacteristics[i]->addDescriptor(new BLE2902());
		pFanCharacteristics[i]->setValue("0");
	}

	for (int i=0; i<LED_CHARACTERISTIC_UUID.size(); ++i) {
		pLedCharacteristics[i] = pLedService->createCharacteristic(
			LED_CHARACTERISTIC_UUID[i],
			BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
		);
		pLedCharacteristics[i]->addDescriptor(new BLE2902());
		pLedCharacteristics[i]->setValue("0");
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
}

void loop() {

}
