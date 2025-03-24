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
bool opponentReady = false;

// Seesaw Gamepad setup
Adafruit_seesaw ss;
#define SEESAW_ADDR 0x50
#define JOYSTICK_X_PIN 14
#define JOYSTICK_Y_PIN 15
#define BUTTON_A_PIN 5
#define BUTTON_B_PIN 6  // Add Button B pin - adjust this number based on your hardware
uint32_t button_mask = (1UL << BUTTON_A_PIN) | (1UL << BUTTON_B_PIN);  // Update mask for both buttons

// Grid settings
const int GRID_SIZE = 8;
const int CELL_SIZE = 25;
char grid[GRID_SIZE][GRID_SIZE];
char hiddenGrid[GRID_SIZE][GRID_SIZE];

// Ship sizes
int ships[] = {4, 3, 2};
const int NUM_SHIPS = 3;

struct Ship {
  int x;          // Starting x position
  int y;          // Starting y position
  int length;     // Ship length
  bool horizontal;// Orientation
};

Ship placedShips[NUM_SHIPS];  // Array to store all ship positions

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
//void processGuess();
bool canPlaceShip(int x, int y, int len, int dir);
void sendMove(int x, int y, char result);
void onReceive(BLECharacteristic* pCharacteristic);
void handleTouch();
void showPairingScreen();
void placeShipsScreen();
void waitForOpponentScreen();

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

// check if a coordinate hits a ship
char checkHit(int x, int y) {
  for (int i = 0; i < NUM_SHIPS; i++) {
    Ship ship = placedShips[i];
    if (ship.horizontal) {
      if (y == ship.y && x >= ship.x && x < ship.x + ship.length) {
        return 'X';  // Hit
      }
    } else {
      if (x == ship.x && y >= ship.y && y < ship.y + ship.length) {
        return 'X';  // Hit
      }
    }
  }
  return 'O';  // Miss
}

class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value == "READY") {
      opponentReady = true;
      Serial.println("Received READY from opponent");
    } else if (value.substr(0, 6) == "GUESS:") {
      // Handle guess request from opponent
      int x = value[6] - '0';
      int y = value[8] - '0';
      char result = checkHit(x, y);
      hiddenGrid[y][x] = result;  // Update our hidden grid with hit/miss
      String response = String(x) + "," + String(y) + "," + (result == 'X' ? 'H' : 'O');
      pCharacteristic->setValue(response.c_str());
      pCharacteristic->notify();
      Serial.println("Received guess: " + String(x) + "," + String(y) + " Result: " + (result == 'X' ? 'H' : 'O'));
    } else {
      // Existing move handling (opponent's confirmed hit/miss)
      int x = value[0] - '0';
      int y = value[2] - '0';
      char result = value[4];
      grid[y][x] = result;  // Update with 'H' or 'O'
      if (result == 'H') {
        hits++;
        if (hits == totalHitsNeeded) gameOver = true;
      }
      updateCell(x, y);
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

  // // Initialize BLE (uncommented for full functionality)
  // BLEDevice::init(isPlayer1 ? "M5Core2_Player1" : "M5Core2_Player2");
  // pServer = BLEDevice::createServer();
  // pServer->setCallbacks(new MyServerCallbacks());
  // pService = pServer->createService(SERVICE_UUID);
  // pCharacteristic = pService->createCharacteristic(
  //                     CHARACTERISTIC_UUID,
  //                     NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
  //                   );
  // pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
  // pService->start();
  // BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  // pAdvertising->addServiceUUID(SERVICE_UUID);
  // pAdvertising->start();
  // Serial.println("BLE initialized. Waiting for connection...");

  // showPairingScreen();

  //opponentReady = false;
  opponentReady = true;

  // Initialize grids
  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      grid[i][j] = ' ';
      hiddenGrid[i][j] = ' ';
    }
  }

  //placeShipsScreen();

  drawInitialGrid();
  updateCursor();
}

void loop() {
  M5.update();

  // Simplified for testing cursor movement - remove conditions for now
  // if (!gameOver && deviceConnected && opponentReady) {
    // Handle joystick input
    int16_t joyX = 1023 - ss.analogRead(JOYSTICK_X_PIN);
    int16_t joyY = 1023 - ss.analogRead(JOYSTICK_Y_PIN);
    uint32_t buttons = ss.digitalReadBulk(button_mask);
    bool buttonA = !(buttons & (1UL << BUTTON_A_PIN));

    // Move cursor with joystick
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
      delay(150);  // Small debounce for joystick movement
    }

    // Handle touchscreen input for cursor movement
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

    // Confirm shot with Button A or touch release (for testing)
    static bool lastButtonA = false;
    if ((buttonA && !lastButtonA) || (!isTouching && wasTouching)) {
      if (grid[cursorY][cursorX] == ' ') {  // Only process if square is empty
        grid[cursorY][cursorX] = 'X';  // Mark as unconfirmed shot
        updateCell(cursorX, cursorY);
        if (deviceConnected) sendMove(cursorX, cursorY, 'X');  // Send guess to opponent
        delay(200);  // Debounce for button/touch
      }
    }
    lastButtonA = buttonA;  // Update last state for edge detection
    wasTouching = isTouching;  // Update touch state
  // }
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

  if (grid[y][x] == 'X') {  // Unconfirmed shot
    M5.Lcd.drawLine(pixelX + 5, pixelY + 5, pixelX + CELL_SIZE - 5, pixelY + CELL_SIZE - 5, GREEN);
    M5.Lcd.drawLine(pixelX + CELL_SIZE - 5, pixelY + 5, pixelX + 5, pixelY + CELL_SIZE - 5, GREEN);
  } else if (grid[y][x] == 'O') {  // Confirmed miss
    M5.Lcd.fillCircle(pixelX + CELL_SIZE / 2, pixelY + CELL_SIZE / 2, CELL_SIZE / 4, WHITE);
  } else if (grid[y][x] == 'H') {  // Confirmed hit
    M5.Lcd.fillRect(pixelX + 5, pixelY + 5, CELL_SIZE - 10, CELL_SIZE - 10, RED);
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

// No longer needed
// void processGuess() {
//   if (grid[cursorY][cursorX] != ' ') return;
//   grid[cursorY][cursorX] = '?';  // Temporary marker for guess awaiting confirmation
// }

void sendMove(int x, int y, char result) {
  // Send guess request to opponent
  String guessData = "GUESS:" + String(x) + "," + String(y);
  pCharacteristic->setValue(guessData.c_str());
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

void handleTouch() {
  if (M5.Touch.getCount() > 0) {
    auto touchPoint = M5.Touch.getDetail(0);
    int newCursorX = constrain(touchPoint.x / CELL_SIZE, 0, GRID_SIZE - 1);
    int newCursorY = constrain(touchPoint.y / CELL_SIZE, 0, GRID_SIZE - 1);

    if (newCursorX != cursorX || newCursorY != cursorY) {
      cursorX = newCursorX;
      cursorY = newCursorY;
      updateCursor();
      //processGuess();
      updateCell(cursorX, cursorY);
      sendMove(cursorX, cursorY, grid[cursorY][cursorX]);
    }
  }
}

// Display BLE pairing screen and wait for connection with animated dots
void showPairingScreen() {
  bool wasConnected = false;
  M5.Lcd.fillScreen(BLACK);

  const char* baseText = "Searching";
  int dotCount = 0;
  unsigned long lastUpdate = millis();
  const int animationDelay = 500;  // Update every 500ms

  while (!deviceConnected) {
    // Update animation periodically
    if (millis() - lastUpdate >= animationDelay) {
      M5.Lcd.fillScreen(BLACK);  // Clear previous text
      M5.Lcd.setTextSize(2);
      M5.Lcd.setTextColor(BLUE);

      // Build string with varying number of dots (0 to 3)
      char displayText[13];  // "Searching" (9) + up to 3 dots + null terminator
      strcpy(displayText, baseText);
      for (int i = 0; i < dotCount; i++) {
        displayText[9 + i] = '.';
      }
      displayText[9 + dotCount] = '\0';

      // Center the text
      int textWidth = strlen(displayText) * 12;  // 6 pixels per char * textSize
      int textHeight = 16;  // 8 pixels * textSize
      int x = (M5.Lcd.width() - textWidth) / 2;
      int y = (M5.Lcd.height() - textHeight) / 2;

      M5.Lcd.setCursor(x, y);
      M5.Lcd.print(displayText);

      // Update dot count (cycle 0-3)
      dotCount = (dotCount + 1) % 4;
      lastUpdate = millis();
    }

    // Check connection status
    if (deviceConnected && !wasConnected) {
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setTextColor(GREEN);
      const char* connected = "Connected!";
      int textWidth = strlen(connected) * 12;
      int textHeight = 16;
      int x = (M5.Lcd.width() - textWidth) / 2;
      int y = (M5.Lcd.height() - textHeight) / 2;
      M5.Lcd.setCursor(x, y);
      M5.Lcd.print(connected);
      delay(2000);  // Show connected message for 2 seconds
      wasConnected = true;
    }

    delay(10);  // Small delay to prevent excessive CPU usage
  }
}

void placeShipsScreen() {
  M5.Lcd.fillScreen(BLACK);
  drawInitialGrid();  // Draw empty grid

  int currentShip = 0;
  int shipX = 0, shipY = 0;  // Starting position
  bool horizontal = true;    // Ship orientation (true = horizontal, false = vertical)

  while (currentShip < NUM_SHIPS) {
    M5.update();

    // Draw already placed ships in red
    for (int y = 0; y < GRID_SIZE; y++) {
      for (int x = 0; x < GRID_SIZE; x++) {
        if (hiddenGrid[y][x] == 'S') {
          M5.Lcd.fillRect(x * CELL_SIZE + 5, y * CELL_SIZE + 5,
                         CELL_SIZE - 10, CELL_SIZE - 10, RED);
        }
      }
    }

    // Draw current ship placement preview
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(10, GRID_SIZE * CELL_SIZE + 10);
    M5.Lcd.printf("Place ship %d (size %d) - A to confirm, B to rotate",
                  currentShip + 1, ships[currentShip]);

    // Preview current ship placement in yellow
    for (int i = 0; i < ships[currentShip]; i++) {
      int previewX = horizontal ? shipX + i : shipX;
      int previewY = horizontal ? shipY : shipY + i;
      if (previewX < GRID_SIZE && previewY < GRID_SIZE &&
          canPlaceShip(shipX, shipY, ships[currentShip], horizontal ? 0 : 1)) {
        M5.Lcd.drawRect(previewX * CELL_SIZE, previewY * CELL_SIZE,
                       CELL_SIZE, CELL_SIZE, YELLOW);
      }
    }

    // Handle joystick input
    int16_t joyX = 1023 - ss.analogRead(JOYSTICK_X_PIN);
    int16_t joyY = 1023 - ss.analogRead(JOYSTICK_Y_PIN);
    uint32_t buttons = ss.digitalReadBulk(button_mask);
    bool buttonA = !(buttons & (1UL << BUTTON_A_PIN));
    bool buttonB = !(buttons & (1UL << BUTTON_B_PIN));

    // Move ship position
    int newShipX = shipX;
    int newShipY = shipY;
    if (joyX > 512 + 100) newShipX = min(shipX + 1, GRID_SIZE - (horizontal ? ships[currentShip] : 1));
    else if (joyX < 512 - 100) newShipX = max(shipX - 1, 0);
    if (joyY < 512 - 100) newShipY = min(shipY + 1, GRID_SIZE - (horizontal ? 1 : ships[currentShip]));
    else if (joyY > 512 + 100) newShipY = max(shipY - 1, 0);

    if (newShipX != shipX || newShipY != shipY) {
      shipX = newShipX;
      shipY = newShipY;
      drawInitialGrid();  // Redraw grid to clear previous preview
    }

    // Handle touch input
    if (M5.Touch.getCount() > 0) {
      auto touch = M5.Touch.getDetail(0);
      shipX = constrain(touch.x / CELL_SIZE, 0, GRID_SIZE - (horizontal ? ships[currentShip] : 1));
      shipY = constrain(touch.y / CELL_SIZE, 0, GRID_SIZE - (horizontal ? 1 : ships[currentShip]));
      drawInitialGrid();
    }

    // Rotate ship with Button B
    static bool lastButtonB = false;
    if (buttonB && !lastButtonB) {  // Detect rising edge
      horizontal = !horizontal;
      if (horizontal && shipX + ships[currentShip] > GRID_SIZE) {
        shipX = GRID_SIZE - ships[currentShip];
      }
      if (!horizontal && shipY + ships[currentShip] > GRID_SIZE) {
        shipY = GRID_SIZE - ships[currentShip];
      }
      drawInitialGrid();
    }
    lastButtonB = buttonB;

    // Confirm placement with Button A
    if (buttonA && canPlaceShip(shipX, shipY, ships[currentShip], horizontal ? 0 : 1)) {
      // Place ship in hiddenGrid and mark with red
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
      // Store ship position
      placedShips[currentShip].x = shipX;
      placedShips[currentShip].y = shipY;
      placedShips[currentShip].length = ships[currentShip];
      placedShips[currentShip].horizontal = horizontal;

      currentShip++;
      shipX = 0;
      shipY = 0;
      horizontal = true;
      delay(200);  // Small delay to show placement
    }

    delay(100);  // Debounce
  }

  // Clear instructions
  M5.Lcd.fillRect(0, GRID_SIZE * CELL_SIZE, M5.Lcd.width(), M5.Lcd.height() - GRID_SIZE * CELL_SIZE, BLACK);

  // Send READY message to opponent
  if (deviceConnected) {
    pCharacteristic->setValue("READY");
    pCharacteristic->notify();
    Serial.println("Sent READY to opponent");
  }

  // Show waiting screen until opponent is ready
  waitForOpponentScreen();
}

// Waiting screen for opponent's ready signal
void waitForOpponentScreen() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE);

  const char* waitingText = "Awaiting Opponent...";
  int textWidth = strlen(waitingText) * 12;  // 6 pixels per char * textSize
  int textHeight = 16;  // 8 pixels * textSize
  int x = (M5.Lcd.width() - textWidth) / 2;
  int y = (M5.Lcd.height() - textHeight) / 2;

  M5.Lcd.setCursor(x, y);
  M5.Lcd.print(waitingText);

  while (!opponentReady) {
    delay(100);  // Wait for opponent to send READY
  }

  // Clear screen before game starts
  M5.Lcd.fillScreen(BLACK);
}
