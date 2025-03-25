#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "Adafruit_seesaw.h"

// BLE Setup
#define SERVICE_UUID "60d3a4dc-1951-4791-8c42-198c2180cf2b"
#define CHARACTERISTIC_UUID "734d991d-ce8a-42c9-a83d-2508cf2940e1"
static String BLE_BROADCAST_NAME = "Dustie's M5 Server"; // P1’s name
static String BLE_CLIENT_NAME = "Dustie's M5 Client";   // P2’s name

BLEServer *pServer;
BLEService *pService;
BLECharacteristic *pCharacteristic; // Server-side for P1
BLEClient *pClient;                 // Client-side for P2
BLERemoteCharacteristic *pRemoteCharacteristic; // Client-side for P2
bool deviceConnected = false;
bool isPlayer1 = true; // Set to false for Player 2
bool opponentReady = false;
bool localReady = false;

// Seesaw Gamepad setup
Adafruit_seesaw ss;
#define SEESAW_ADDR 0x50
#define JOYSTICK_X_PIN 14
#define JOYSTICK_Y_PIN 15
#define BUTTON_A_PIN 5
#define BUTTON_B_PIN 6
uint32_t button_mask = (1UL << BUTTON_A_PIN) | (1UL << BUTTON_B_PIN);

// Grid settings
const int GRID_SIZE = 8;
const int CELL_SIZE = 25;
char grid[GRID_SIZE][GRID_SIZE];
char hiddenGrid[GRID_SIZE][GRID_SIZE];

// Ship sizes
int ships[] = {4, 3, 2};
const int NUM_SHIPS = 3;

struct Ship {
  int x;
  int y;
  int length;
  bool horizontal;
};

Ship placedShips[NUM_SHIPS];

// Cursor position
int cursorX = 0;
int cursorY = 0;
int lastCursorX = -1;
int lastCursorY = -1;
int hits = 0;
int totalHitsNeeded = 9;
bool gameOver = false;
bool gridDrawn = false;

// Function declarations
void drawInitialGrid();
void updateCell(int x, int y);
void updateCursor();
bool canPlaceShip(int x, int y, int len, int dir);
void sendMove(int x, int y, char result);
void placeShipsScreen();
void waitForOpponentScreen();
char checkHit(int x, int y);
void drawScreenTextWithBackground(String text, int backgroundColor);

// BLE Server Callbacks (P1)
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("BLE: Device connected successfully");
    drawScreenTextWithBackground("Connected!", TFT_GREEN);
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    opponentReady = false;
    localReady = false;
    Serial.println("BLE: Device disconnected");
    pServer->startAdvertising(); // Restart advertising
    Serial.println("BLE: Restarted advertising");
    drawScreenTextWithBackground("Disconnected! Waiting...", TFT_RED);
  }
};

// BLE Characteristic Callbacks (P1)
class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value == "READY") {
      opponentReady = true;
      Serial.println("BLE: Received READY from opponent");
    } else if (value.substr(0, 6) == "GUESS:") {
      int x = value[6] - '0';
      int y = value[8] - '0';
      char result = checkHit(x, y);
      hiddenGrid[y][x] = result;
      String response = String(x) + "," + String(y) + "," + (result == 'X' ? 'H' : 'O');
      pCharacteristic->setValue(response.c_str());
      pCharacteristic->notify();
      Serial.println("BLE: Received guess: " + String(x) + "," + String(y) + " Result: " + (result == 'X' ? 'H' : 'O'));
    } else if (value.length() >= 5) {
      int x = value[0] - '0';
      int y = value[2] - '0';
      char result = value[4];
      grid[y][x] = result;
      if (result == 'H') {
        hits++;
        if (hits == totalHitsNeeded) gameOver = true;
      }
      updateCell(x, y);
      Serial.print("BLE: Shot confirmed at: ("); Serial.print(x); Serial.print(", "); Serial.print(y);
      Serial.print(") - "); Serial.println(result == 'H' ? "Hit" : "Miss");
    }
  }
};

// BLE Client Callbacks (P2)
class MyClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient* pClient) {
    deviceConnected = true;
    Serial.println("BLE: Connected to server");
    drawScreenTextWithBackground("Connected!", TFT_GREEN);
  }

  void onDisconnect(BLEClient* pClient) {
    deviceConnected = false;
    opponentReady = false;
    localReady = false;
    Serial.println("BLE: Disconnected from server");
    drawScreenTextWithBackground("Disconnected! Reconnecting...", TFT_RED);
  }
};

// Notification Callback for P2 (static function)
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  String value = String((char*)pData, length);
  if (value == "READY") {
    opponentReady = true;
    Serial.println("BLE: Received READY from server");
  } else if (value.length() >= 5) {
    int x = value[0] - '0';
    int y = value[2] - '0';
    char result = value[4];
    grid[y][x] = result;
    if (result == 'H') {
      hits++;
      if (hits == totalHitsNeeded) gameOver = true;
    }
    updateCell(x, y);
    Serial.print("BLE: Shot confirmed at: ("); Serial.print(x); Serial.print(", "); Serial.print(y);
    Serial.print(") - "); Serial.println(result == 'H' ? "Hit" : "Miss");
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Lcd.setTextSize(2);

  // Initialize Seesaw Gamepad
  if (!ss.begin(SEESAW_ADDR)) {
    Serial.println("ERROR: Seesaw initialization failed!");
    while (1) delay(1000);
  }
  ss.pinModeBulk(button_mask, INPUT_PULLUP);
  ss.setGPIOInterrupts(button_mask, 1);
  Serial.println("Seesaw Gamepad initialized");

  if (isPlayer1) {
    // Player 1: Server Setup
    Serial.println("BLE: Initializing as server...");
    BLEDevice::init(BLE_BROADCAST_NAME.c_str());
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_READ |
                        BLECharacteristic::PROPERTY_WRITE |
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
    pCharacteristic->addDescriptor(new BLE2902()); // Enable notifications
    pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
    pCharacteristic->setValue("Ready");
    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    BLEDevice::startAdvertising();
    Serial.println("BLE: Broadcasting as " + BLE_BROADCAST_NAME);
    drawScreenTextWithBackground("Broadcasting as:\n" + BLE_BROADCAST_NAME, TFT_BLUE);
  } else {
    // Player 2: Client Setup
    Serial.println("BLE: Initializing as client...");
    BLEDevice::init(BLE_CLIENT_NAME.c_str());
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallbacks());

    BLEScan* pScan = BLEDevice::getScan();
    pScan->setActiveScan(true);
    pScan->start(10, false);
    BLEScanResults results = pScan->getResults();
    BLEAdvertisedDevice* targetDevice = nullptr;
    for (int i = 0; i < results.getCount(); i++) {
      BLEAdvertisedDevice device = results.getDevice(i);
      if (device.getName() == BLE_BROADCAST_NAME.c_str()) {
        targetDevice = new BLEAdvertisedDevice(device);
        break;
      }
    }

    if (targetDevice) {
      if (pClient->connect(targetDevice)) {
        BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
        if (pRemoteService) {
          pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
          if (pRemoteCharacteristic) {
            if (pRemoteCharacteristic->canNotify()) {
              pRemoteCharacteristic->registerForNotify(notifyCallback);
              Serial.println("BLE: Registered for notifications");
            }
          }
        }
      } else {
        Serial.println("BLE: Failed to connect to server");
      }
      delete targetDevice;
    } else {
      Serial.println("BLE: Server not found");
    }
  }

  // Wait for connection
  while (!deviceConnected) {
    drawScreenTextWithBackground("Waiting for opponent...", TFT_CYAN);
    delay(100);
  }

  delay(2000); // Show "Connected!" briefly

  // Initialize grids
  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      grid[i][j] = ' ';
      hiddenGrid[i][j] = ' ';
    }
  }

  placeShipsScreen();
  waitForOpponentScreen();
  drawInitialGrid();
  updateCursor();
}

void loop() {
  M5.update();

  // Reconnect logic for P2
  if (!deviceConnected && !isPlayer1 && pClient) {
    BLEScan* pScan = BLEDevice::getScan();
    pScan->start(5, false);
    BLEScanResults results = pScan->getResults();
    BLEAdvertisedDevice* targetDevice = nullptr;
    for (int i = 0; i < results.getCount(); i++) {
      BLEAdvertisedDevice device = results.getDevice(i);
      if (device.getName() == BLE_BROADCAST_NAME.c_str()) {
        targetDevice = new BLEAdvertisedDevice(device);
        break;
      }
    }
    if (targetDevice && pClient->connect(targetDevice)) {
      BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
      if (pRemoteService) {
        pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
        if (pRemoteCharacteristic && pRemoteCharacteristic->canNotify()) {
          pRemoteCharacteristic->registerForNotify(notifyCallback);
          Serial.println("BLE: Reconnected and registered for notifications");
          deviceConnected = true;
        }
      }
      delete targetDevice;
    }
  }

  if (!gameOver && deviceConnected && opponentReady && localReady) {
    int16_t joyX = 1023 - ss.analogRead(JOYSTICK_X_PIN);
    int16_t joyY = 1023 - ss.analogRead(JOYSTICK_Y_PIN);
    uint32_t buttons = ss.digitalReadBulk(button_mask);
    bool buttonA = !(buttons & (1UL << BUTTON_A_PIN));

    int newCursorX = cursorX;
    int newCursorY = cursorY;
    if (joyX > 512 + 100) newCursorX = (cursorX + 1) % GRID_SIZE;
    else if (joyX < 512 - 100) newCursorX = (cursorX - 1 + GRID_SIZE) % GRID_SIZE;
    if (joyY < 512 - 100) newCursorY = (cursorY + 1) % GRID_SIZE;
    else if (joyY > 512 + 100) newCursorY = (cursorY - 1 + GRID_SIZE) % GRID_SIZE;

    if (newCursorX != cursorX || newCursorY != cursorY) {
      cursorX = newCursorX;
      cursorY = newCursorY;
      updateCursor();
      delay(150); // Debounce joystick
    }

    static bool wasTouching = false;
    bool isTouching = M5.Touch.getCount() > 0;
    if (isTouching) {
      auto touchPoint = M5.Touch.getDetail(0);
      int touchX = constrain(touchPoint.x / CELL_SIZE, 0, GRID_SIZE - 1);
      int touchY = constrain(touchPoint.y / CELL_SIZE, 0, GRID_SIZE - 1);
      if (touchX != cursorX || touchY != cursorY) {
        cursorX = touchX;
        cursorY = touchY;
        updateCursor();
      }
    }

    static bool lastButtonA = false;
    if ((buttonA && !lastButtonA) || (!isTouching && wasTouching)) {
      if (grid[cursorY][cursorX] == ' ') {
        grid[cursorY][cursorX] = 'X';
        updateCell(cursorX, cursorY);
        sendMove(cursorX, cursorY, 'X');
        Serial.print("BLE: Shot sent at: ("); Serial.print(cursorX); Serial.print(", "); Serial.print(cursorY); Serial.println(")");
      }
    }
    lastButtonA = buttonA;
    wasTouching = isTouching;
  }
}

void drawInitialGrid() {
  M5.Lcd.fillScreen(BLACK);
  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      int x = j * CELL_SIZE;
      int y = i * CELL_SIZE;
      M5.Lcd.drawRect(x, y, CELL_SIZE, CELL_SIZE, WHITE);
      if (grid[i][j] == 'X') {
        M5.Lcd.drawLine(x + 5, y + 5, x + CELL_SIZE - 5, y + CELL_SIZE - 5, GREEN);
        M5.Lcd.drawLine(x + CELL_SIZE - 5, y + 5, x + 5, y + CELL_SIZE - 5, GREEN);
      } else if (grid[i][j] == 'O') {
        M5.Lcd.fillCircle(x + CELL_SIZE / 2, y + CELL_SIZE / 2, CELL_SIZE / 4, WHITE);
      } else if (grid[i][j] == 'H') {
        M5.Lcd.fillRect(x + 5, y + 5, CELL_SIZE - 10, CELL_SIZE - 10, RED);
      }
    }
  }
  gridDrawn = true;
}

void updateCell(int x, int y) {
  int pixelX = x * CELL_SIZE;
  int pixelY = y * CELL_SIZE;
  M5.Lcd.fillRect(pixelX + 1, pixelY + 1, CELL_SIZE - 2, CELL_SIZE - 2, BLACK);
  M5.Lcd.drawRect(pixelX, pixelY, CELL_SIZE, CELL_SIZE, WHITE);

  if (grid[y][x] == 'X') {
    M5.Lcd.drawLine(pixelX + 5, pixelY + 5, pixelX + CELL_SIZE - 5, pixelY + CELL_SIZE - 5, GREEN);
    M5.Lcd.drawLine(pixelX + CELL_SIZE - 5, pixelY + 5, pixelX + 5, pixelY + CELL_SIZE - 5, GREEN);
  } else if (grid[y][x] == 'O') {
    M5.Lcd.fillCircle(pixelX + CELL_SIZE / 2, pixelY + CELL_SIZE / 2, CELL_SIZE / 4, WHITE);
  } else if (grid[y][x] == 'H') {
    M5.Lcd.fillRect(pixelX + 5, pixelY + 5, CELL_SIZE - 10, CELL_SIZE - 10, RED);
  }

  if (gameOver) {
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, GRID_SIZE * CELL_SIZE + 10);
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.print(isPlayer1 ? "You Win!" : "You Lose!");
  }
}

void updateCursor() {
  if (lastCursorX >= 0 && lastCursorY >= 0) {
    int oldX = lastCursorX * CELL_SIZE;
    int oldY = lastCursorY * CELL_SIZE;
    M5.Lcd.drawRect(oldX, oldY, CELL_SIZE, CELL_SIZE, WHITE);
    updateCell(lastCursorX, lastCursorY);
  }
  int newX = cursorX * CELL_SIZE;
  int newY = cursorY * CELL_SIZE;
  M5.Lcd.drawRect(newX, newY, CELL_SIZE, CELL_SIZE, GREEN);
  lastCursorX = cursorX;
  lastCursorY = cursorY;
}

bool canPlaceShip(int x, int y, int len, int dir) {
  if (dir == 0) {
    if (x + len > GRID_SIZE) return false;
    for (int i = 0; i < len; i++) {
      if (hiddenGrid[y][x + i] != ' ') return false;
    }
  } else {
    if (y + len > GRID_SIZE) return false;
    for (int i = 0; i < len; i++) {
      if (hiddenGrid[y + i][x] != ' ') return false;
    }
  }
  return true;
}

char checkHit(int x, int y) {
  for (int i = 0; i < NUM_SHIPS; i++) {
    Ship ship = placedShips[i];
    if (ship.horizontal) {
      if (y == ship.y && x >= ship.x && x < ship.x + ship.length) return 'X';
    } else {
      if (x == ship.x && y >= ship.y && y < ship.y + ship.length) return 'X';
    }
  }
  return 'O';
}

void sendMove(int x, int y, char result) {
  String guessData = "GUESS:" + String(x) + "," + String(y);
  if (isPlayer1 && pCharacteristic) {
    pCharacteristic->setValue(guessData.c_str());
    pCharacteristic->notify();
    Serial.println("BLE: Sent guess: " + guessData);
  } else if (pRemoteCharacteristic) {
    pRemoteCharacteristic->writeValue(guessData.c_str());
    Serial.println("BLE: Sent guess: " + guessData);
  }
}

void placeShipsScreen() {
  M5.Lcd.fillScreen(BLACK);
  drawInitialGrid();

  int currentShip = 0;
  int shipX = 0, shipY = 0;
  bool horizontal = true;

  while (currentShip < NUM_SHIPS) {
    M5.update();

    for (int y = 0; y < GRID_SIZE; y++) {
      for (int x = 0; x < GRID_SIZE; x++) {
        if (hiddenGrid[y][x] == 'S') {
          M5.Lcd.fillRect(x * CELL_SIZE + 5, y * CELL_SIZE + 5,
                          CELL_SIZE - 10, CELL_SIZE - 10, RED);
        }
      }
    }

    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(10, GRID_SIZE * CELL_SIZE + 10);
    M5.Lcd.printf("Place ship %d (size %d) - A to confirm, B to rotate",
                  currentShip + 1, ships[currentShip]);

    bool canPlace = canPlaceShip(shipX, shipY, ships[currentShip], horizontal ? 0 : 1);
    for (int i = 0; i < ships[currentShip]; i++) {
      int previewX = horizontal ? shipX + i : shipX;
      int previewY = horizontal ? shipY : shipY + i;
      if (previewX < GRID_SIZE && previewY < GRID_SIZE && canPlace) {
        M5.Lcd.drawRect(previewX * CELL_SIZE, previewY * CELL_SIZE,
                        CELL_SIZE, CELL_SIZE, YELLOW);
      }
    }

    int16_t joyX = 1023 - ss.analogRead(JOYSTICK_X_PIN);
    int16_t joyY = 1023 - ss.analogRead(JOYSTICK_Y_PIN);
    uint32_t buttons = ss.digitalReadBulk(button_mask);
    bool buttonA = !(buttons & (1UL << BUTTON_A_PIN));
    bool buttonB = !(buttons & (1UL << BUTTON_B_PIN));

    int newShipX = shipX;
    int newShipY = shipY;
    if (joyX > 512 + 100) newShipX = min(shipX + 1, GRID_SIZE - (horizontal ? ships[currentShip] : 1));
    else if (joyX < 512 - 100) newShipX = max(shipX - 1, 0);
    if (joyY < 512 - 100) newShipY = min(shipY + 1, GRID_SIZE - (horizontal ? 1 : ships[currentShip]));
    else if (joyY > 512 + 100) newShipY = max(shipY - 1, 0);

    if (newShipX != shipX || newShipY != shipY) {
      shipX = newShipX;
      shipY = newShipY;
      drawInitialGrid();
    }

    if (M5.Touch.getCount() > 0) {
      auto touch = M5.Touch.getDetail(0);
      shipX = constrain(touch.x / CELL_SIZE, 0, GRID_SIZE - (horizontal ? ships[currentShip] : 1));
      shipY = constrain(touch.y / CELL_SIZE, 0, GRID_SIZE - (horizontal ? 1 : ships[currentShip]));
      drawInitialGrid();
    }

    static bool lastButtonB = false;
    if (buttonB && !lastButtonB) {
      horizontal = !horizontal;
      if (horizontal && shipX + ships[currentShip] > GRID_SIZE) shipX = GRID_SIZE - ships[currentShip];
      if (!horizontal && shipY + ships[currentShip] > GRID_SIZE) shipY = GRID_SIZE - ships[currentShip];
      drawInitialGrid();
      delay(200);
    }
    lastButtonB = buttonB;

    if (buttonA && canPlaceShip(shipX, shipY, ships[currentShip], horizontal ? 0 : 1)) {
      for (int i = 0; i < ships[currentShip]; i++) {
        if (horizontal) hiddenGrid[shipY][shipX + i] = 'S';
        else hiddenGrid[shipY + i][shipX] = 'S';
      }
      placedShips[currentShip].x = shipX;
      placedShips[currentShip].y = shipY;
      placedShips[currentShip].length = ships[currentShip];
      placedShips[currentShip].horizontal = horizontal;

      currentShip++;
      shipX = 0;
      shipY = 0;
      horizontal = true;
      drawInitialGrid();
      delay(200);
    }

    delay(100);
  }

  M5.Lcd.fillRect(0, GRID_SIZE * CELL_SIZE, M5.Lcd.width(), M5.Lcd.height() - GRID_SIZE * CELL_SIZE, BLACK);

  if (deviceConnected) {
    if (isPlayer1 && pCharacteristic) {
      pCharacteristic->setValue("READY");
      pCharacteristic->notify();
      Serial.println("BLE: Sent READY to opponent");
    } else if (pRemoteCharacteristic) {
      pRemoteCharacteristic->writeValue("READY");
      Serial.println("BLE: Sent READY to opponent");
    }
    localReady = true;
  }
}

void waitForOpponentScreen() {
  drawScreenTextWithBackground("Waiting for opponent...", TFT_CYAN);
  while (!(localReady && opponentReady)) {
    delay(100);
  }
  M5.Lcd.fillScreen(BLACK);
}

void drawScreenTextWithBackground(String text, int backgroundColor) {
  M5.Lcd.fillScreen(backgroundColor);
  M5.Lcd.setCursor(10, M5.Lcd.height() / 2 - 8);
  M5.Lcd.println(text);
}
