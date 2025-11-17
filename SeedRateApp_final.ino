/*
  Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
  Ported to Arduino ESP32 by Evandro Copercini
  updated by chegewara and MoThunderz
*/
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLEServer* pServer = NULL;
BLECharacteristic* pCharGrains = NULL;
BLECharacteristic* pCharControl = NULL;
BLECharacteristic* pCharReset = NULL;
BLEDescriptor* pDescr_Speed = NULL;
BLEDescriptor* pDescr_Grains = NULL;
BLE2902* pBLE2902_Grains;

bool deviceConnected = false;
bool oldDeviceConnected = false;
bool countingEnabled = false;

volatile unsigned long pulseCountSpeed = 0;
unsigned long pulseCountGrains = 0;
unsigned long lastTime = 0;
float graudu_skaits = 0;

// Grain filtering
bool lastGrainState = false;       // false = nav grauda, true = grauds
unsigned long lastGrainTime = 0;   // last detected grain
const unsigned long grainDebounce = 5; // ms

// thresholds
const int highThreshold = 60; // ADC value above which sensor considers "no grain"
const int lowThreshold = 20;  // ADC value below which sensor considers "grain present"


#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_UUID_SPEED "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR_UUID_GRAINS "e71920a0-f827-4f50-b1e5-386cfb0d3fbb"
#define CHAR_UUID_CONTROL "d2a36f2c-1234-4b0a-9e24-abcdef123456"
#define CHAR_UUID_RESET "a1b2c3d4-5678-90ab-cdef-1234567890ab"

#define GRAIN_SENSOR_PIN 21

// Data reset
void resetData() {
  pulseCountGrains = 0;
  pulseCountSpeed = 0;
  graudu_skaits = 0;
  lastTime = millis();
  Serial.println("Data reset completed");
}

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) { deviceConnected = true; }
  void onDisconnect(BLEServer* pServer) { deviceConnected = false; }
};

class ControlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    countingEnabled = (value.length() > 0 && value[0] == '1');
    Serial.println(countingEnabled ? "Counting ENABLED" : "Counting DISABLED");
  }
};

class ResetCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0 && value[0] == '1') {
      resetData();
      Serial.println("RESET from phone");
    }
  }
};

void setup() {
  Serial.begin(9600);
  pinMode(GRAIN_SENSOR_PIN, INPUT);

  // BLE setup
  BLEDevice::init("ESP32");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService* pService = pServer->createService(SERVICE_UUID);


  // Reset characteristic
  pCharReset = pService->createCharacteristic(
    CHAR_UUID_RESET,
    BLECharacteristic::PROPERTY_WRITE);
  pCharReset->setCallbacks(new ResetCallbacks());

  // Control characteristic
  pCharControl = pService->createCharacteristic(
    CHAR_UUID_CONTROL,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ);
  pCharControl->setCallbacks(new ControlCallbacks());
  pCharControl->setValue("0");  // initially disabled

  // Grains characteristic
  pCharGrains = pService->createCharacteristic(
    CHAR_UUID_GRAINS,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
  pDescr_Grains = new BLEDescriptor((uint16_t)0x2901);
  pDescr_Grains->setValue("Total grains");
  pCharGrains->addDescriptor(pDescr_Grains);
  pBLE2902_Grains = new BLE2902();
  pBLE2902_Grains->setNotifications(true);
  pCharGrains->addDescriptor(pBLE2902_Grains);

  pService->start();
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("Waiting for a client connection to notify...");
}

void loop() {
  if (countingEnabled) {
    int val = analogRead(GRAIN_SENSOR_PIN);

    unsigned long now = millis();
    if (!lastGrainState && val < lowThreshold && (now - lastGrainTime > grainDebounce)) {
      lastGrainState = true;
      lastGrainTime = now;
      pulseCountGrains++;
      Serial.print("Graudu skaits: ");
      Serial.println(pulseCountGrains);
    } else if (lastGrainState && val > highThreshold) {
      lastGrainState = false;
    }

    // BLE notification
    if (deviceConnected && pBLE2902_Grains->getNotifications()) {
      pCharGrains->setValue(String(pulseCountGrains).c_str());
      pCharGrains->notify();
    }
  }

  // BLE connection management
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
}
