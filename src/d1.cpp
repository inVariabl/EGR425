#include <M5Core2.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Forward declarations
void processGuess(int x, int y);
void sendGameState();

BLEServer* pServer = NULL;
BLECharacteristic* pGuessCharacteristic = NULL;
BLECharacteristic* pGameStateCharacteristic = NULL;
bool deviceConnected = false;

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
  }
};

class GuessCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    std::string value = pCharacteristic->getValue();

    if (value.length() >= 2) {
      int x = value[0];
      int y = value[1];
      processGuess(x, y);
      sendGameState(); // Update D2 with the new game state
    }
  }
};

void setup() {
  M5.begin();
  BLEDevice::init("D1 Device");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService("1234");

  pGuessCharacteristic = pService->createCharacteristic(
                         "abcd",
                         BLECharacteristic::PROPERTY_WRITE
                       );

  pGuessCharacteristic->setCallbacks(new GuessCharacteristicCallbacks());

  pGameStateCharacteristic = pService->createCharacteristic(
                              "efgh",
                              BLECharacteristic::PROPERTY_NOTIFY
                            );
  pGameStateCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  pServer->getAdvertising()->start();
}

void loop() {
  // Your main loop logic here if needed
}
