// Caleb Aragones, Kwang Hak Lee, Daniel Crooks
// EGR 425 - Project 1 and 2

#include <M5Unified.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "EGR425_Phase1_weather_bitmap_images.h"
#include "WiFi.h"
#include <WiFiUdp.h>
#include <NTPClient.h>

// Project 2
#include "../include/I2C_RW.h"
#include <Wire.h>

////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////
///// Project 1
String urlOpenWeather = "https://api.openweathermap.org/data/2.5/weather?";
String apiKey = "d4fbab132209f8288d5ee07e27bfa1d2";

// WiFi variables
String wifiNetworkName = "CBU-LANCERS";
String wifiPassword = "L@ncerN@tion";

// Time variables
unsigned long lastTime = 0;
unsigned long timerDelay = 2500;

// LCD variables
int sWidth;
int sHeight;
bool wasTouched = false;
bool touchHandled = false;

// Weather/zip variables
String strWeatherIcon;
String strWeatherDesc;
String cityName;
String unit = "F";
double tempNow;
double tempNowC;
double tempMin;
double tempMax;
bool isFahrenheit = true;

int zipcode = 91016;
int zipcodeArray[5] = {9, 1, 0, 1, 6};

// Enum
enum State { WEATHER, ZIP, SENSOR };
static State displayState = WEATHER;

// Misc
bool tp = false;

// NTP objects
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -8 * 3600, 60000);
String lastSyncTime = "Not synced";

// Sensor-related variables
float lastTempC = -1000;     // Initial value to force first draw
float lastHumidity = -1000;  // Initial value to force first draw
unsigned long lastSensorUpdate = 0;
const unsigned long sensorUpdateInterval = 5000;  // 5 seconds

///// Project 2
int const I2C_FREQ = 400000;

#define SHT_I2C_ADDR 0x44
// int const SHT_SDA_PIN = 32;
// int const SHT_SCL_PIN = 33;

#define VCNL_I2C_ADDR 0x60
// int const VCNL_SDA_PIN = 21;
// int const VCNL_SCL_PIN = 22;

int const SDA_PIN = 32;
int const SCL_PIN = 33;

#define VCNL_REG_PROX_DATA 0x08
#define VCNL_REG_ALS_DATA 0x09
#define VCNL_REG_WHITE_DATA 0x0A
#define VCNL_REG_PS_CONFIG 0x03
#define VCNL_REG_ALS_CONFIG 0x00
#define VCNL_REG_WHITE_CONFIG 0x04

////////////////////////////////////////////////////////////////////
// Method headers
////////////////////////////////////////////////////////////////////
///// Project 1
String httpGETRequest(const char* serverName);
void drawWeatherImage(String iconId, int resizeMult, int xOffset, int yOffset, int iconWidth, int iconHeight);
void fetchWeatherDetails();
void drawWeatherDisplay();
void drawZipcodeSelectScreen();
void checkButtonPress();
void getZipcode();
void drawSensorDisplay();

///// Project 2
void readSHT();
void readVCNL();

////////////////////////////////////////////////////////////////////
void setup() {
    M5.begin();
    Serial.begin(115200);

    sWidth = M5.Display.width();
    sHeight = M5.Display.height();

    WiFi.begin(wifiNetworkName.c_str(), wifiPassword.c_str());
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }

    timeClient.begin();
    timeClient.update();

    // Project 2

    // Initialize I2C for VCNL and SHT sensor
    I2C_RW::initI2C(VCNL_I2C_ADDR, I2C_FREQ, SDA_PIN, SCL_PIN);
    I2C_RW::writeReg8Addr16DataWithProof(VCNL_REG_PS_CONFIG, 2, 0x0800, " to enable proximity sensor", true);
    I2C_RW::writeReg8Addr16DataWithProof(VCNL_REG_ALS_CONFIG, 2, 0x0000, " to enable ambient light sensor", true);
    I2C_RW::writeReg8Addr16DataWithProof(VCNL_REG_WHITE_CONFIG, 2, 0x0000, " to enable raw white light sensor", true);

    // Initialize I2C for SHT sensor
    I2C_RW::writeReg8Addr16Data(0xFD, 0x0000, "to start measurement", true);

	displayState = WEATHER;  // Still start with weather
}

///////////////////////////////////////////////////////////////
void loop() {
    M5.update();

    // Existing button A handling
    if (M5.BtnA.wasPressed()) {
        isFahrenheit = !isFahrenheit;
        if (displayState == WEATHER || displayState == SENSOR) {
            fetchWeatherDetails();
            delay(10);
            displayState == WEATHER ? drawWeatherDisplay() : drawSensorDisplay();
        }
    }

    // Existing touch handling for ZIP screen
    if (M5.Touch.getDetail().wasPressed() && displayState == ZIP) {
        checkButtonPress();
    }

    // Modify button B to cycle through all three screens
    if (M5.BtnB.wasPressed()) {
        if (displayState == WEATHER) {
            drawZipcodeSelectScreen();
            displayState = ZIP;
            timerDelay = 1000;
            tp = false;
        } else if (displayState == ZIP) {
            getZipcode();
            fetchWeatherDetails();
            delay(10);
            drawWeatherDisplay();
            displayState = WEATHER;
            timerDelay = 2500;
        } else if (displayState == SENSOR) {
            fetchWeatherDetails();
            delay(10);
            drawWeatherDisplay();
            displayState = WEATHER;
            timerDelay = 2500;
        }
    }

    // Add button C to switch to sensor screen
    if (M5.BtnC.wasPressed()) {
        drawSensorDisplay();
        displayState = SENSOR;
        timerDelay = 5000;  // 5-second updates for sensor screen
    }

    if ((millis() - lastTime) > timerDelay) {
        if (WiFi.status() == WL_CONNECTED) {
            if (displayState == WEATHER) {
                fetchWeatherDetails();
                drawWeatherDisplay();
                readSHT();
                delay(10);
                readVCNL();
            } else if (displayState == SENSOR) {
                readSHT();
                drawSensorDisplay();
            }
        }
        lastTime = millis();
    }
}

/////////////////////////////////////////////////////////////////
void fetchWeatherDetails() {
    //Update the NTP client to get current time (added code)
    timeClient.update();

    // Convert 24-hr time to 12-hr with AM/PM
    int hour = timeClient.getHours();
    int minute = timeClient.getMinutes();
    int second = timeClient.getSeconds();

    int hour12 = hour % 12;
    if (hour12 == 0) hour12 = 12;

    String ampm = (hour >= 12) ? "PM" : "AM";

    char buffer[12]; // Enough for "HH:MM:SSAM"
    sprintf(buffer, "%02d:%02d:%02d%s", hour12, minute, second, ampm.c_str());
    lastSyncTime = String(buffer);

    timeClient.update();
    String serverURL = urlOpenWeather + "zip=" + zipcode + ",us&units=imperial&appid=" + apiKey;
    String response = httpGETRequest(serverURL.c_str());

    const size_t jsonCapacity = 768 + 250;
    DynamicJsonDocument objResponse(jsonCapacity);
    DeserializationError error = deserializeJson(objResponse, response);
    if (error) return;

    JsonArray arrWeather = objResponse["weather"];
    JsonObject objWeather0 = arrWeather[0];
    strWeatherDesc = objWeather0["main"].as<String>();
    strWeatherIcon = objWeather0["icon"].as<String>();
    cityName = objResponse["name"].as<String>();
    JsonObject objMain = objResponse["main"];
    tempNow = objMain["temp"];
    tempMin = objMain["temp_min"];
    tempMax = objMain["temp_max"];
}

/////////////////////////////////////////////////////////////////
void drawWeatherDisplay() {
    //////////////////////////////////////////////////////////////////
    // Draw background - light blue if day time and navy blue of night
    //////////////////////////////////////////////////////////////////
    uint16_t primaryTextColor;
    uint16_t backgroundColor;
    uint16_t syncTextColor = TFT_DARKGREY;

    if (strWeatherIcon.indexOf("d") >= 0) {
        backgroundColor = TFT_CYAN;
        primaryTextColor = TFT_DARKGREY;
    } else {
        backgroundColor = TFT_NAVY;
        primaryTextColor = TFT_WHITE;
    }
    M5.Lcd.fillScreen(backgroundColor);

    //////////////////////////////////////////////////////////////////
    // Draw the weather icon on the right side of the screen
    //////////////////////////////////////////////////////////////////
    int iconSize = 100; // Size of the weather icon
    int iconX = M5.Lcd.width() - iconSize - 20; // Position icon on the right with some padding
    int iconY = 20; // Position icon at the top with some padding
    drawWeatherImage(strWeatherIcon, 3, iconX, iconY, iconSize, iconSize);

    //////////////////////////////////////////////////////////////////
    // Draw the temperatures and city name
    //////////////////////////////////////////////////////////////////
    int pad = 20; // Increased padding for better spacing
    int textX = pad;
    int textY = pad;

    tempNow = isFahrenheit ? tempNow : (tempNow - 32) * 5 / 9;
    tempMin = isFahrenheit ? tempMin : (tempMin - 32) * 5 / 9;
    tempMax = isFahrenheit ? tempMax : (tempMax - 32) * 5 / 9;
    unit = isFahrenheit ? "F" : "C";

    // Draw "LO" temperature
    M5.Display.setCursor(textX, textY);
    M5.Display.setTextColor(TFT_RED);
    M5.Display.setTextSize(2);
    M5.Display.printf("HI: %.0f%s\n", tempMax, unit);

    // Draw current temperature
    textY += 30; // Adjust vertical spacing
    M5.Display.setCursor(textX, textY);
    M5.Display.setTextColor(primaryTextColor);
    M5.Display.setTextSize(6); // Larger font size for current temperature
    M5.Display.printf("%.0f%s\n", tempNow, unit);

    // Draw "HI" temperature
    textY += 70; // Adjust vertical spacing
    M5.Display.setCursor(textX, textY);
    M5.Display.setTextColor(TFT_BLUE);
    M5.Display.setTextSize(2);
    M5.Display.printf("LO: %.0f%s\n", tempMin, unit);

    // Draw city name
    textY += 30; // Adjust vertical spacing
    M5.Display.setCursor(textX, textY);
    M5.Display.setTextColor(primaryTextColor);
    M5.Display.setTextSize(2);
    M5.Display.setTextWrap(true); // Enable text wrapping for long city names
    M5.Display.printf("%s\n", cityName.c_str());

    //    Draw last sync time (added code)
    //    Example: "Last Sync: 05:23:45PM"
    //    We'll put it near the bottom of the screen
    int timestampY = sHeight - 30;  // 30 px from bottom
    M5.Lcd.setCursor(pad, timestampY);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(syncTextColor);
    M5.Lcd.printf("Last Sync: %s", lastSyncTime.c_str());
}

void drawSensorDisplay() {
    // Read current sensor values
    Wire.beginTransmission(SHT_I2C_ADDR);
    Wire.write(0xFD);  // Measure T & RH with high precision
    Wire.endTransmission();
    delay(10);

    float t_degC = 0;
    float rh_pRH = 0;

    Wire.requestFrom(SHT_I2C_ADDR, 6);
    if (Wire.available() == 6) {
        uint8_t rx_bytes[6];
        Wire.readBytes(rx_bytes, 6);

        int16_t t_ticks = (rx_bytes[0] << 8) | rx_bytes[1];
        t_degC = -45 + 175.0 * t_ticks / 65535.0;

        int16_t rh_ticks = (rx_bytes[3] << 8) | rx_bytes[4];
        rh_pRH = -6 + 125.0 * rh_ticks / 65535.0;
        rh_pRH = constrain(rh_pRH, 0.0, 100.0);
    }

    // Only redraw if values changed significantly
    if (abs(t_degC - lastTempC) < 0.1 && abs(rh_pRH - lastHumidity) < 0.1) {
        return;
    }

    lastTempC = t_degC;
    lastHumidity = rh_pRH;

    // Update time
    timeClient.update();
    int hour = timeClient.getHours();
    int hour12 = hour % 12;
    if (hour12 == 0) hour12 = 12;
    String ampm = (hour >= 12) ? "PM" : "AM";
    char buffer[12];
    sprintf(buffer, "%02d:%02d:%02d%s", hour12, timeClient.getMinutes(), timeClient.getSeconds(), ampm.c_str());
    lastSyncTime = String(buffer);

    // Draw display
    M5.Lcd.fillScreen(TFT_WHITE);

    int pad = 20;
    int textX = pad;
    int textY = pad;

    // Header
    M5.Display.setCursor(textX, textY);
    M5.Display.setTextColor(TFT_RED);
    M5.Display.setTextSize(2);
    M5.Display.println("Live Sensor Readings");

    // Temperature
    textY += 40;
    float displayTemp = isFahrenheit ? (t_degC * 9/5) + 32 : t_degC;
    M5.Display.setCursor(textX, textY);
    M5.Display.setTextColor(TFT_DARKGREY);
    M5.Display.setTextSize(6);
    M5.Display.printf("%.1f%s\n", displayTemp, unit);

    // Humidity
    textY += 80;
    M5.Display.setCursor(textX, textY);
    M5.Display.setTextSize(4);
    M5.Display.printf("%.1f%%\n", rh_pRH);

    // Labels
    textY = pad + 40;
    M5.Display.setCursor(textX + 120, textY);
    M5.Display.setTextSize(2);
    M5.Display.print("Temperature");

    textY += 80;
    M5.Display.setCursor(textX + 120, textY);
    M5.Display.print("Humidity");

    // Timestamp
    textY = sHeight - 30;
    M5.Display.setCursor(pad, textY);
    M5.Display.setTextColor(TFT_DARKGREY);
    M5.Display.setTextSize(2);
    M5.Display.printf("Last Sync: %s", lastSyncTime.c_str());
}

void checkButtonPress() {
    if (M5.Touch.getCount() == 0) return;

    auto t = M5.Touch.getDetail(0);

    int startX = 40;
    int startY = 100;

    for (int i = 0; i < 5; i++) {
        int xPos = startX + i * 50;

        // Check if "+" button is pressed
        if (t.x > xPos && t.x < xPos + 40 && t.y > startY - 40 && t.y < startY - 10) {
            zipcodeArray[i] = (zipcodeArray[i] + 1) % 10;
            drawZipcodeSelectScreen();
        }

        // Check if "-" button is pressed
        if (t.x > xPos && t.x < xPos + 40 && t.y > startY + 60 && t.y < startY + 90) {
            zipcodeArray[i] = (zipcodeArray[i] - 1 + 10) % 10;
            drawZipcodeSelectScreen();
        }
    }
    getZipcode();
}

/////////////////////////////////////////////////////////////////
void drawZipcodeSelectScreen() {
    int padding = 10;
    int zipcodeX = 40;
    int zipcodeY = 100;
    int headerX = 30;
    int headerY = 10;

    M5.Display.fillScreen(TFT_WHITE);

    // Draw the header
    M5.Display.setCursor(headerX, headerY);
    M5.Display.setTextColor(TFT_RED);
    M5.Display.setTextSize(3);
    M5.Display.print("Enter Zip Code");

    // Draw the zipcode
    M5.Display.setCursor(zipcodeX, zipcodeY);
    M5.Display.setTextColor(TFT_DARKGREY);
    M5.Display.setTextSize(2);

    for (int i = 0; i < 5; i++) {
        int boxX = zipcodeX + i * 50;

        // Draw "+" button
        M5.Display.setTextColor(TFT_DARKGREY);
        M5.Display.fillRect(boxX, zipcodeY - 40, 40, 30, TFT_GREEN);
        M5.Display.setCursor(boxX + 15, zipcodeY - 30);
        M5.Display.print("+");

        // Draw ZIP digit
        M5.Display.fillRect(boxX, zipcodeY, 40, 50, TFT_WHITE);
        M5.Display.setTextColor(TFT_BLACK);
        M5.Display.setCursor(boxX + 15, zipcodeY + 15);
        M5.Display.print(zipcodeArray[i]);

        // Draw "-" button
        M5.Display.setTextColor(TFT_DARKGREY);
        M5.Display.fillRect(boxX, zipcodeY + 60, 40, 30, TFT_RED);
        M5.Display.setCursor(boxX + 15, zipcodeY + 70);
        M5.Display.print("-");
    }

    // Draw the buttons
    // initButtons();
}

void getZipcode() {
    zipcode = 0;
    for (int i = 0; i < 5; i++) {
        zipcode = zipcode * 10 + zipcodeArray[i];
    }
}

/////////////////////////////////////////////////////////////////
String httpGETRequest(const char* serverURL) {
    HTTPClient http;
    http.begin(serverURL);
    int httpResponseCode = http.GET();
    String response = http.getString();
    http.end();
    return response;
}

/////////////////////////////////////////////////////////////////
// This method takes in an image icon string (from API) and a
// resize multiple and draws the corresponding image (bitmap byte
// arrays found in EGR425_Phase1_weather_bitmap_images.h) to scale (for
// example, if resizeMult==2, will draw the image as 200x200 instead
// of the native 100x100 pixels) on the right-hand side of the
// screen (centered vertically).
/////////////////////////////////////////////////////////////////
void drawWeatherImage(String iconId, int resizeMult, int xOffset, int yOffset, int iconWidth, int iconHeight) {
    // Get the corresponding byte array
    const uint16_t * weatherBitmap = getWeatherBitmap(iconId);

    // Iterate through each pixel of the imgSqDim x imgSqDim (100 x 100) array
    for (int y = 0; y < imgSqDim; y++) {
        for (int x = 0; x < imgSqDim; x++) {
            // Compute the linear index in the array and get pixel value
            int pixNum = (y * imgSqDim) + x;
            uint16_t pixel = weatherBitmap[pixNum];

            // If the pixel is black, do NOT draw (treat it as transparent);
            // otherwise, draw the value
            if (pixel != 0) {
                // 16-bit RBG565 values give the high 5 pixels to red, the middle
                // 6 pixels to green and the low 5 pixels to blue as described
                // here: http://www.barth-dev.de/online/rgb565-color-picker/
                byte red = (pixel >> 11) & 0b0000000000011111;
                red = red << 3;
                byte green = (pixel >> 5) & 0b0000000000111111;
                green = green << 2;
                byte blue = pixel & 0b0000000000011111;
                blue = blue << 3;

                // Scale image; for example, if resizeMult == 2, draw a 2x2
                // filled square for each original pixel
                for (int i = 0; i < resizeMult; i++) {
                    for (int j = 0; j < resizeMult; j++) {
                        int xDraw = x * resizeMult + i + xOffset;
                        int yDraw = y * resizeMult + j + yOffset;
                        M5.Lcd.drawPixel(xDraw, yDraw, M5.Lcd.color565(red, green, blue));
                    }
                }
            }
        }
    }
}

void readVCNL() {
    int prox = I2C_RW::readReg8Addr16Data(VCNL_REG_PROX_DATA, 2, "to read proximity data", false);
    Serial.printf("Proximity: %d\n", prox);

    int als = I2C_RW::readReg8Addr16Data(VCNL_REG_ALS_DATA, 2, "to read ambient light data", false) * 0.1;
    Serial.printf("Ambient Light: %d\n", als);

    int rwl = I2C_RW::readReg8Addr16Data(VCNL_REG_WHITE_DATA, 2, "to read white light data", false) * 0.1;
    Serial.printf("White Light: %d\n\n", rwl);
}

void readSHT() {
    Wire.beginTransmission(SHT_I2C_ADDR);
    Wire.write(0xFD);  // Measure T & RH with high precision
    Wire.endTransmission();

    delay(10);

  // Read data from sensor
    Wire.requestFrom(SHT_I2C_ADDR, 6);
    if (Wire.available() == 6) {
    uint8_t rx_bytes[6];
    Wire.readBytes(rx_bytes, 6);

    // Calculate temperature
    int16_t t_ticks = (rx_bytes[0] << 8) | rx_bytes[1];
    float t_degC = -45 + 175.0 * t_ticks / 65535.0;

    // Calculate humidity
    int16_t rh_ticks = (rx_bytes[3] << 8) | rx_bytes[4];
    float rh_pRH = -6 + 125.0 * rh_ticks / 65535.0;
    rh_pRH = constrain(rh_pRH, 0.0, 100.0);

    // Print data to serial monitor
    Serial.print("Temperature: ");
    Serial.print(t_degC);
    Serial.println("°C");
    Serial.print("Humidity: ");
    Serial.print(rh_pRH);
    Serial.println("%");

    } else {
    Serial.println("Error reading data from SHT40!");
    }
}

//////////////////////////////////////////////////////////////////////////////////
// For more documentation see the following links:
// https://github.com/m5stack/m5-docs/blob/master/docs/en/api/
// https://docs.m5stack.com/en/api/core2/lcd_api
//////////////////////////////////////////////////////////////////////////////////
