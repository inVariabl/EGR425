#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// Grid settings
const int GRID_SIZE = 8;
const int CELL_SIZE = 25; // Pixels per cell
char grid[GRID_SIZE][GRID_SIZE]; // Player's view

// Cursor position
int cursorX = 0;
int cursorY = 0;
int lastCursorX = -1; // Track previous cursor position
int lastCursorY = -1;
bool gameOver = false;
bool gridDrawn = false; // Flag to draw grid only once

// BLE Setup
BLEClient* pClient;
BLERemoteCharacteristic* pRemoteGameStateCharacteristic;
BLERemoteCharacteristic* pRemoteGuessCharacteristic;
bool deviceConnected = false;

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define GAME_STATE_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define GUESS_CHARACTERISTIC_UUID "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"

// BLE Client Callbacks
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pClient) {
    deviceConnected = true;
    Serial.println("Connected to D1!");
  }

  void onDisconnect(BLEClient* pClient) {
    deviceConnected = false;
    Serial.println("Disconnected from D1!");
  }
};

// BLE Characteristic Callback for Game State Updates
class GameStateCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onNotify(BLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
      // Parse the game state JSON (simplified for this example)
      // Example JSON: {"grid":[[...]], "cursorX":0, "cursorY":0}
      // Update the grid and cursor position
      updateGrid(value);
      updateCursor();
    }
  }
};

// Function declarations
void drawInitialGrid();
void updateCell(int x, int y);
void updateCursor();
void sendGuess(int x, int y);
void updateGrid(const std::string& gameState);
void setupBLE();

void setup() {
  M5.begin(); // M5Unified initialization with default I2C settings
  M5.Lcd.setTextSize(2);
  M5.Lcd.fillScreen(BLACK);

  // Start Serial for debugging
  Serial.begin(115200);
  while (!Serial) delay(10); // Wait for Serial to initialize
  Serial.println("Starting Battleship Client...");

  // Initialize grids
  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      grid[i][j] = ' ';
    }
  }

  drawInitialGrid(); // Draw the grid once
  updateCursor();   // Draw initial cursor

  // Initialize BLE
  setupBLE();
}

void loop() {
  M5.update(); // Update M5 state

  if (!gameOver) {
    // Handle touch input
    if (M5.Touch.ispressed()) {
      int touchX = M5.Touch.getX();
      int touchY = M5.Touch.getY();
      cursorX = touchX / CELL_SIZE;
      cursorY = touchY / CELL_SIZE;
      updateCursor();
    }

    // Confirm guess with button A
    if (M5.BtnA.wasPressed()) {
      sendGuess(cursorX, cursorY);
      delay(200); // Debounce
    }
  }
}

// Draw the initial grid once
void drawInitialGrid() {
  M5.Lcd.fillScreen(BLACK);

  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      int x = j * CELL_SIZE;
      int y = i * CELL_SIZE;
      M5.Lcd.drawRect(x, y, CELL_SIZE, CELL_SIZE, WHITE);

      if (grid[i][j] == 'X') {
        M5.Lcd.fillRect(x + 5, y + 5, CELL_SIZE - 10, CELL_SIZE - 10, RED); // Hit
      } else if (grid[i][j] == 'O') {
        M5.Lcd.fillCircle(x + CELL_SIZE / 2, y + CELL_SIZE / 2, CELL_SIZE / 4, BLUE); // Miss
      }
    }
  }

  gridDrawn = true;
}

// Update a single cell’s appearance
void updateCell(int x, int y) {
  int pixelX = x * CELL_SIZE;
  int pixelY = y * CELL_SIZE;

  // Clear the cell by drawing a black rectangle
  M5.Lcd.fillRect(pixelX + 1, pixelY + 1, CELL_SIZE - 2, CELL_SIZE - 2, BLACK);

  // Redraw the cell border
  M5.Lcd.drawRect(pixelX, pixelY, CELL_SIZE, CELL_SIZE, WHITE);

  // Draw the cell content based on grid state
  if (grid[y][x] == 'X') {
    M5.Lcd.fillRect(pixelX + 5, pixelY + 5, CELL_SIZE - 10, CELL_SIZE - 10, RED); // Hit
  } else if (grid[y][x] == 'O') {
    M5.Lcd.fillCircle(pixelX + CELL_SIZE / 2, pixelY + CELL_SIZE / 2, CELL_SIZE / 4, BLUE); // Miss
  }

  // If game is over, draw the lose message
  if (gameOver && gridDrawn) {
    M5.Lcd.fillRect(10, GRID_SIZE * CELL_SIZE + 10, 200, 20, BLACK); // Clear previous text
    M5.Lcd.setCursor(10, GRID_SIZE * CELL_SIZE + 10);
    M5.Lcd.setTextColor(RED);
    M5.Lcd.print("You Lose!");
  }
}

// Update cursor by erasing old position and drawing new position
void updateCursor() {
  // Erase the old cursor (if it exists)
  if (lastCursorX >= 0 && lastCursorY >= 0) {
    int oldX = lastCursorX * CELL_SIZE;
    int oldY = lastCursorY * CELL_SIZE;
    M5.Lcd.drawRect(oldX, oldY, CELL_SIZE, CELL_SIZE, WHITE); // Revert to grid line color
    updateCell(lastCursorX, lastCursorY); // Restore cell content
  }

  // Draw the new cursor
  int newX = cursorX * CELL_SIZE;
  int newY = cursorY * CELL_SIZE;
  M5.Lcd.drawRect(newX, newY, CELL_SIZE, CELL_SIZE, GREEN);

  // Update last known position
  lastCursorX = cursorX;
  lastCursorY = cursorY;
}

// Send a guess to D1 via BLE
void sendGuess(int x, int y) {
  if (deviceConnected) {
    String guess = String(x) + String(y); // Example: "34" for x=3, y=4
    pRemoteGuessCharacteristic->writeValue(guess.c_str());
    Serial.println("Guess sent: " + guess);
  }
}

// Update the grid based on the game state received from D1
void updateGrid(const std::string& gameState) {
  // Parse the game state JSON (simplified for this example)
  // Example JSON: {"grid":[[...]], "cursorX":0, "cursorY":0}
  // Update the grid and cursor position
  // For simplicity, assume the grid is updated directly
  // In a real implementation, you would parse the JSON and update the grid array
  // Example: grid[i][j] = parsedGrid[i][j];
}

// Initialize BLE
void setupBLE() {
  BLEDevice::init("BattleshipClient");
  pClient = BLEDevice::createClient();
  pClient->setCallbacks(new MyClientCallback());

  // Scan for D1's BLE service
  BLEScan* pScan = BLEDevice::getScan();
  pScan->setActiveScan(true);
  BLEScanResults results = pScan->start(5); // Scan for 5 seconds

  // Connect to D1
  for (int i = 0; i < results.getCount(); i++) {
    BLEAdvertisedDevice device = results.getDevice(i);
    if (device.getName() == "BattleshipHost") {
      pClient->connect(&device);
      break;
    }
  }

  if (pClient->isConnected()) {
    BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
    if (pRemoteService != nullptr) {
      pRemoteGameStateCharacteristic = pRemoteService->getCharacteristic(GAME_STATE_CHARACTERISTIC_UUID);
      if (pRemoteGameStateCharacteristic != nullptr) {
        pRemoteGameStateCharacteristic->registerForNotify(new GameStateCharacteristicCallbacks());
      }

      pRemoteGuessCharacteristic = pRemoteService->getCharacteristic(GUESS_CHARACTERISTIC_UUID);
    }
  }

  Serial.println("BLE initialized and connected to D1!");
}
