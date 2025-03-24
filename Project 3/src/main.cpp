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
bool isPlayer1 = true; // Set to false for Device 2 (Player 2)

// Seesaw Gamepad setup
Adafruit_seesaw ss;
#define SEESAW_ADDR 0x50
#define JOYSTICK_X_PIN 14
#define JOYSTICK_Y_PIN 15
#define BUTTON_A_PIN 5
uint32_t button_mask = (1UL << BUTTON_A_PIN);

// Grid settings
const int GRID_SIZE = 8;
const int CELL_SIZE = 25;
char grid[GRID_SIZE][GRID_SIZE];
char hiddenGrid[GRID_SIZE][GRID_SIZE];

// Ship sizes
int ships[] = {4, 3, 2};
const int NUM_SHIPS = 3;

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
void placeShips();
void processGuess();
bool canPlaceShip(int x, int y, int len, int dir);
void sendMove(int x, int y, char result);
void onReceive(BLECharacteristic* pCharacteristic);
void handleTouch();

// BLE Server Callbacks
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("Device connected");
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("Device disconnected");
    pServer->startAdvertising(); // Restart advertising
  }
};

// BLE Characteristic Callbacks
class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    onReceive(pCharacteristic);
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

  // Initialize BLE
  BLEDevice::init(isPlayer1 ? "M5Core2_Player1" : "M5Core2_Player2");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
                    );
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();
  Serial.println("BLE initialized. Waiting for connection...");

  // Initialize grids
  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      grid[i][j] = ' ';
      hiddenGrid[i][j] = ' ';
    }
  }

  placeShips();
  drawInitialGrid();
  updateCursor();
}

void loop() {
  M5.update();

  if (!gameOver && deviceConnected) {
    // Read joystick values
    int16_t joyX = 1023 - ss.analogRead(JOYSTICK_X_PIN);
    int16_t joyY = 1023 - ss.analogRead(JOYSTICK_Y_PIN);
    uint32_t buttons = ss.digitalReadBulk(button_mask);
    bool buttonA = !(buttons & (1UL << BUTTON_A_PIN));

    // Move cursor
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
      delay(200);
    }

    // Confirm guess with button A
    if (buttonA) {
      processGuess();
      updateCell(cursorX, cursorY);
      sendMove(cursorX, cursorY, grid[cursorY][cursorX]);
      delay(200);
    }
  }

  // Handle touch input
  handleTouch();
}

// Draw the initial grid
void drawInitialGrid() {
  M5.Lcd.fillScreen(BLACK);
  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      int x = j * CELL_SIZE;
      int y = i * CELL_SIZE;
      M5.Lcd.drawRect(x, y, CELL_SIZE, CELL_SIZE, WHITE);
      if (grid[i][j] == 'X') {
        M5.Lcd.fillRect(x + 5, y + 5, CELL_SIZE - 10, CELL_SIZE - 10, RED);
      } else if (grid[i][j] == 'O') {
        M5.Lcd.fillCircle(x + CELL_SIZE / 2, y + CELL_SIZE / 2, CELL_SIZE / 4, BLUE);
      }
    }
  }
  gridDrawn = true;
}

// Update a single cell
void updateCell(int x, int y) {
  int pixelX = x * CELL_SIZE;
  int pixelY = y * CELL_SIZE;
  M5.Lcd.fillRect(pixelX + 1, pixelY + 1, CELL_SIZE - 2, CELL_SIZE - 2, BLACK);
  M5.Lcd.drawRect(pixelX, pixelY, CELL_SIZE, CELL_SIZE, WHITE);
  if (grid[y][x] == 'X') {
    M5.Lcd.fillRect(pixelX + 5, pixelY + 5, CELL_SIZE - 10, CELL_SIZE - 10, RED);
  } else if (grid[y][x] == 'O') {
    M5.Lcd.fillCircle(pixelX + CELL_SIZE / 2, pixelY + CELL_SIZE / 2, CELL_SIZE / 4, BLUE);
  }
  if (gameOver) {
    M5.Lcd.setCursor(10, GRID_SIZE * CELL_SIZE + 10);
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.print(isPlayer1 ? "You Win!" : "You Lose!");
  }
}

// Update cursor position
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

// Place ships randomly
void placeShips() {
  for (int s = 0; s < NUM_SHIPS; s++) {
    int len = ships[s];
    bool placed = false;
    while (!placed) {
      int dir = random(2);
      int x = random(GRID_SIZE);
      int y = random(GRID_SIZE);
      if (canPlaceShip(x, y, len, dir)) {
        for (int i = 0; i < len; i++) {
          if (dir == 0) hiddenGrid[y][x + i] = 'S';
          else hiddenGrid[y + i][x] = 'S';
        }
        placed = true;
      }
    }
  }
}

// Check if a ship can be placed
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

// Process a guess
void processGuess() {
  if (grid[cursorY][cursorX] != ' ') return;
  if (hiddenGrid[cursorY][cursorX] == 'S') {
    grid[cursorY][cursorX] = 'X';
    hits++;
    if (hits == totalHitsNeeded) gameOver = true;
  } else {
    grid[cursorY][cursorX] = 'O';
  }
}

// Send move data over BLE
void sendMove(int x, int y, char result) {
  String moveData = String(x) + "," + String(y) + "," + String(result);
  pCharacteristic->setValue(moveData.c_str());
  pCharacteristic->notify();
}

// Handle received BLE data
void onReceive(BLECharacteristic* pCharacteristic) {
  std::string value = pCharacteristic->getValue();
  int x = value[0] - '0';
  int y = value[2] - '0';
  char result = value[4];
  grid[y][x] = result;
  updateCell(x, y);
}

// Handle touch input
void handleTouch() {
  if (M5.Touch.getCount() > 0) {
    auto touchPoint = M5.Touch.getDetail(0);
    int newCursorX = touchPoint.x / CELL_SIZE;
    int newCursorY = touchPoint.y / CELL_SIZE;

    if (newCursorX != cursorX || newCursorY != cursorY) {
      cursorX = newCursorX;
      cursorY = newCursorY;
      updateCursor();
    }
  }
}
