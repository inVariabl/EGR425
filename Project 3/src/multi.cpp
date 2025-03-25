#include <M5Unified.h>
#include <NimBLEDevice.h>
#include "Adafruit_seesaw.h"

// BLE Setup
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer *pServer;
BLEService *pService;
BLECharacteristic *pCharacteristic;
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
#define BUTTON_B_PIN 6  // Pin 6 = Button X
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

// BLE Server Callbacks
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("BLE: Device connected successfully");
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    opponentReady = false;
    localReady = false;
    Serial.println("BLE: Device disconnected");
    pServer->startAdvertising();
    Serial.println("BLE: Restarted advertising after disconnect");
  }
};

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
    } else {
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

void setup() {
  M5.begin();
  M5.Lcd.setTextSize(2);
  M5.Lcd.fillScreen(BLACK);

  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("Starting Battleship with BLE...");

  // Initialize Seesaw Gamepad
  if (!ss.begin(SEESAW_ADDR)) {
    Serial.println("ERROR: Seesaw initialization failed!");
    while (1) delay(1000);
  }
  ss.pinModeBulk(button_mask, INPUT_PULLUP);
  ss.setGPIOInterrupts(button_mask, 1);
  Serial.println("Seesaw Gamepad initialized");

  // Initialize BLE with debug
  Serial.println("BLE: Initializing device...");
  BLEDevice::init(isPlayer1 ? "M5Core2_Player1" : "M5Core2_Player2");
  Serial.println("BLE: Device initialized as " + String(isPlayer1 ? "M5Core2_Player1" : "M5Core2_Player2"));

  Serial.println("BLE: Creating server...");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  Serial.println("BLE: Server created");

  Serial.println("BLE: Creating service with UUID " + String(SERVICE_UUID));
  pService = pServer->createService(SERVICE_UUID);

  Serial.println("BLE: Creating characteristic with UUID " + String(CHARACTERISTIC_UUID));
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
                    );
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
  Serial.println("BLE: Characteristic created");

  Serial.println("BLE: Starting service...");
  pService->start();
  Serial.println("BLE: Service started");

  Serial.println("BLE: Starting advertising...");
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();
  Serial.println("BLE: Advertising started. Waiting for connection...");

  // Wait for connection with debug
  while (!deviceConnected) {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(BLUE);
    M5.Lcd.setCursor(10, M5.Lcd.height() / 2 - 8);
    M5.Lcd.print("Waiting for opponent...");
    delay(100);
    Serial.println("BLE: Still waiting for connection...");
  }

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.setCursor(10, M5.Lcd.height() / 2 - 8);
  M5.Lcd.print("Connected!");
  delay(2000);

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
      delay(150);
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
        Serial.print("Shot sent at: ("); Serial.print(cursorX); Serial.print(", "); Serial.print(cursorY); Serial.println(")");
        delay(200);
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
      if (y == ship.y && x >= ship.x && x < ship.x + ship.length) {
        return 'X';
      }
    } else {
      if (x == ship.x && y >= ship.y && y < ship.y + ship.length) {
        return 'X';
      }
    }
  }
  return 'O';
}

void sendMove(int x, int y, char result) {
  String guessData = "GUESS:" + String(x) + "," + String(y);
  pCharacteristic->setValue(guessData.c_str());
  pCharacteristic->notify();
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
    M5.Lcd.printf("Place ship %d (size %d) - A to confirm, X to rotate",
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
    bool buttonX = !(buttons & (1UL << BUTTON_B_PIN));

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

    static bool lastButtonX = false;
    if (buttonX && !lastButtonX) {
      horizontal = !horizontal;
      if (horizontal && shipX + ships[currentShip] > GRID_SIZE) {
        shipX = GRID_SIZE - ships[currentShip];
      }
      if (!horizontal && shipY + ships[currentShip] > GRID_SIZE) {
        shipY = GRID_SIZE - ships[currentShip];
      }
      drawInitialGrid();
      delay(200);
    }
    lastButtonX = buttonX;

    if (buttonA && canPlaceShip(shipX, shipY, ships[currentShip], horizontal ? 0 : 1)) {
      for (int i = 0; i < ships[currentShip]; i++) {
        if (horizontal) {
          hiddenGrid[shipY][shipX + i] = 'S';
          M5.Lcd.fillRect((shipX + i) * CELL_SIZE + 5, shipY * CELL_SIZE + 5,
                         CELL_SIZE - 10, CELL_SIZE - 10, RED);
        } else {
          hiddenGrid[shipY + i][shipX] = 'S';
          M5.Lcd.fillRect(shipX * CELL_SIZE + 5, (shipY + i) * CELL_SIZE + 5,
                         CELL_SIZE - 10, CELL_SIZE - 10, RED);
        }
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
    pCharacteristic->setValue("READY");
    pCharacteristic->notify();
    Serial.println("Sent READY to opponent");
    localReady = true;
  }
}

void waitForOpponentScreen() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE);

  const char* waitingText = "Waiting for opponent...";
  int textWidth = strlen(waitingText) * 12;
  int textHeight = 16;
  int x = (M5.Lcd.width() - textWidth) / 2;
  int y = (M5.Lcd.height() - textHeight) / 2;

  M5.Lcd.setCursor(x, y);
  M5.Lcd.print(waitingText);

  while (!(localReady && opponentReady)) {
    delay(100);
  }

  M5.Lcd.fillScreen(BLACK);
}
