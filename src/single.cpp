#include <M5Unified.h>
#include "Adafruit_seesaw.h"

// Seesaw Gamepad setup
Adafruit_seesaw ss;
#define SEESAW_ADDR 0x50 // I2C address from reference code
#define JOYSTICK_X_PIN 14  // Analog X-axis
#define JOYSTICK_Y_PIN 15  // Analog Y-axis
#define BUTTON_A_PIN 5     // Digital pin for Button A
uint32_t button_mask = (1UL << BUTTON_A_PIN); // Button mask for configuration
// Joystick thresholds
const int JOYSTICK_DEADZONE = 100; // Deadzone to avoid jitter
const int JOYSTICK_MAX = 1023;     // Max analog value from Seesaw

// Grid settings
const int GRID_SIZE = 8;
const int CELL_SIZE = 25; // Pixels per cell
char grid[GRID_SIZE][GRID_SIZE]; // Player's view
char hiddenGrid[GRID_SIZE][GRID_SIZE]; // Computer's ship locations

// Ship sizes
int ships[] = {4, 3, 2}; // Carrier (4), Battleship (3), Destroyer (2)
const int NUM_SHIPS = 3;

// Cursor position
int cursorX = 0;
int cursorY = 0;
int lastCursorX = -1; // Track previous cursor position
int lastCursorY = -1;
int hits = 0;
int totalHitsNeeded = 9; // Sum of ship lengths (4 + 3 + 2)
bool gameOver = false;
bool gridDrawn = false; // Flag to draw grid only once

// Function declarations
void drawInitialGrid();
void updateCell(int x, int y); // Update a single cell
void updateCursor();           // Update cursor position
void placeShips();
void processGuess();
bool canPlaceShip(int x, int y, int len, int dir);

void setup() {
  M5.begin(); // M5Unified initialization with default I2C settings
  M5.Lcd.setTextSize(2);
  M5.Lcd.fillScreen(BLACK);

  // Start Serial for debugging
  Serial.begin(115200);
  while (!Serial) delay(10); // Wait for Serial to initialize
  Serial.println("Starting Battleship with Seesaw Gamepad...");

  // Initialize Seesaw Gamepad
  Serial.print("Attempting to initialize Seesaw at address 0x");
  Serial.println(SEESAW_ADDR, HEX);
  if (!ss.begin(SEESAW_ADDR)) {
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.setTextColor(RED);
    M5.Lcd.print("Seesaw not found!");
    Serial.println("ERROR: Seesaw initialization failed!");
    Serial.println("Check wiring (SDA: 21, SCL: 22), I2C address, and power.");
    while (1) {
      delay(1000);
      Serial.println("Retrying Seesaw initialization...");
      if (ss.begin(SEESAW_ADDR)) break; // Retry until success
    }
  }

  // Check firmware version
  uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
  Serial.print("Seesaw firmware version: ");
  Serial.println(version);
  if (version != 5743) {
    Serial.println("Wrong firmware loaded! Expected 5743.");
    M5.Lcd.setCursor(0, 20);
    M5.Lcd.setTextColor(RED);
    M5.Lcd.print("Wrong firmware!");
    while (1) delay(10);
  }
  Serial.println("Seesaw initialized successfully! Found Product 5743");

  // Configure Seesaw pins
  ss.pinModeBulk(button_mask, INPUT_PULLUP);
  ss.setGPIOInterrupts(button_mask, 1);

  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.print("Seesaw OK");

  // Initialize grids
  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      grid[i][j] = ' ';
      hiddenGrid[i][j] = ' ';
    }
  }

  placeShips();     // Place ships randomly
  drawInitialGrid(); // Draw the grid once
  updateCursor();   // Draw initial cursor
}

void loop() {
  M5.update(); // Update M5 state (optional)

  if (!gameOver) {
    // Read joystick values (inverted as in reference code)
    int16_t joyX = 1023 - ss.analogRead(JOYSTICK_X_PIN); // X-axis
    int16_t joyY = 1023 - ss.analogRead(JOYSTICK_Y_PIN); // Y-axis

    // Read button state (active low)
    uint32_t buttons = ss.digitalReadBulk(button_mask);
    bool buttonA = !(buttons & (1UL << BUTTON_A_PIN));

    // Print debug info to Serial
    Serial.print("Joystick X: ");
    Serial.print(joyX);
    Serial.print(", Y: ");
    Serial.print(joyY);
    Serial.print(", Button A: ");
    Serial.println(buttonA);

    // Move cursor with joystick
    int newCursorX = cursorX;
    int newCursorY = cursorY;

    if (joyX > JOYSTICK_MAX / 2 + JOYSTICK_DEADZONE) { // Right
      newCursorX = (cursorX + 1) % GRID_SIZE;
    } else if (joyX < JOYSTICK_MAX / 2 - JOYSTICK_DEADZONE) { // Left
      newCursorX = (cursorX - 1 + GRID_SIZE) % GRID_SIZE;
    }

    if (joyY < JOYSTICK_MAX / 2 - JOYSTICK_DEADZONE) { // Down
      newCursorY = (cursorY + 1) % GRID_SIZE;
    } else if (joyY > JOYSTICK_MAX / 2 + JOYSTICK_DEADZONE) { // Up
      newCursorY = (cursorY - 1 + GRID_SIZE) % GRID_SIZE;
    }

    // Update cursor if position changed
    if (newCursorX != cursorX || newCursorY != cursorY) {
      cursorX = newCursorX;
      cursorY = newCursorY;
      updateCursor();
      delay(200); // Debounce
    }

    // Confirm guess with button A
    if (buttonA) { // Button A pressed
      processGuess();
      updateCell(cursorX, cursorY); // Update only the guessed cell
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

  // Draw game over message if applicable (initially not shown)
  if (gameOver) {
    M5.Lcd.setCursor(10, GRID_SIZE * CELL_SIZE + 10);
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.print("You Win!");
  }

  gridDrawn = true;
}

// Update a single cell’s appearance
void updateCell(int x, int y) {
  int pixelX = x * CELL_SIZE;
  int pixelY = y * CELL_SIZE;

  // Clear the cell by drawing a black rectangle (erase previous content)
  M5.Lcd.fillRect(pixelX + 1, pixelY + 1, CELL_SIZE - 2, CELL_SIZE - 2, BLACK);

  // Redraw the cell border
  M5.Lcd.drawRect(pixelX, pixelY, CELL_SIZE, CELL_SIZE, WHITE);

  // Draw the cell content based on grid state
  if (grid[y][x] == 'X') {
    M5.Lcd.fillRect(pixelX + 5, pixelY + 5, CELL_SIZE - 10, CELL_SIZE - 10, RED); // Hit
  } else if (grid[y][x] == 'O') {
    M5.Lcd.fillCircle(pixelX + CELL_SIZE / 2, pixelY + CELL_SIZE / 2, CELL_SIZE / 4, BLUE); // Miss
  }

  // If game is over, draw the win message
  if (gameOver && gridDrawn) {
    M5.Lcd.fillRect(10, GRID_SIZE * CELL_SIZE + 10, 200, 20, BLACK); // Clear previous text
    M5.Lcd.setCursor(10, GRID_SIZE * CELL_SIZE + 10);
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.print("You Win!");
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

void placeShips() {
  for (int s = 0; s < NUM_SHIPS; s++) {
    int len = ships[s];
    bool placed = false;

    while (!placed) {
      int dir = random(2); // 0 = horizontal, 1 = vertical
      int x = random(GRID_SIZE);
      int y = random(GRID_SIZE);

      if (canPlaceShip(x, y, len, dir)) {
        for (int i = 0; i < len; i++) {
          if (dir == 0) hiddenGrid[y][x + i] = 'S'; // Horizontal
          else hiddenGrid[y + i][x] = 'S'; // Vertical
        }
        placed = true;
      }
    }
  }
}

bool canPlaceShip(int x, int y, int len, int dir) {
  if (dir == 0) { // Horizontal
    if (x + len > GRID_SIZE) return false;
    for (int i = 0; i < len; i++) {
      if (hiddenGrid[y][x + i] != ' ') return false;
    }
  } else { // Vertical
    if (y + len > GRID_SIZE) return false;
    for (int i = 0; i < len; i++) {
      if (hiddenGrid[y + i][x] != ' ') return false;
    }
  }
  return true;
}

void processGuess() {
  if (grid[cursorY][cursorX] != ' ') return; // Already guessed

  if (hiddenGrid[cursorY][cursorX] == 'S') {
    grid[cursorY][cursorX] = 'X'; // Hit
    hits++;
    if (hits == totalHitsNeeded) gameOver = true;
  } else {
    grid[cursorY][cursorX] = 'O'; // Miss
  }
}
