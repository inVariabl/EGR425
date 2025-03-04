#include <M5Unified.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "EGR425_Phase1_weather_bitmap_images.h"
#include "WiFi.h"
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Wire.h>
#include "Adafruit_VCNL4040.h"

////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////
String urlOpenWeather = "https://api.openweathermap.org/data/2.5/weather?";
String apiKey = "d4fbab132209f8288d5ee07e27bfa1d2";

String wifiNetworkName = "CBU-LANCERS";
String wifiPassword = "L@ncerN@tion";

unsigned long lastTime = 0;
unsigned long timerDelay = 2500;

int sWidth;
int sHeight;
bool wasTouched = false;
bool touchHandled = false;

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

Adafruit_VCNL4040 vcnl4040 = Adafruit_VCNL4040();
const uint16_t PROXIMITY_THRESHOLD = 100; // Adjust this value based on testing
bool displayOn = true;

// === Added for Light Sensor ===
const uint16_t MIN_LIGHT = 0;        // Minimum ambient light value
const uint16_t MAX_LIGHT = 1000;     // Maximum ambient light value (adjust based on testing)
const uint8_t MIN_BRIGHTNESS = 10;   // Minimum LCD brightness (0-255)
const uint8_t MAX_BRIGHTNESS = 255;  // Maximum LCD brightness (0-255)

enum State { WEATHER, ZIP };
static State displayState = WEATHER;

bool tp = false;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -8 * 3600, 60000);
String lastSyncTime = "Not synced";

////////////////////////////////////////////////////////////////////
// Method headers
////////////////////////////////////////////////////////////////////
String httpGETRequest(const char* serverName);
void drawWeatherImage(String iconId, int resizeMult, int xOffset, int yOffset, int iconWidth, int iconHeight);
void fetchWeatherDetails();
void drawWeatherDisplay();
void drawZipcodeSelectScreen();
void checkButtonPress();
void getZipcode();
void checkProximity();
void adjustBrightness();

////////////////////////////////////////////////////////////////////
void setup() {
    M5.begin();

    sWidth = M5.Display.width();
    sHeight = M5.Display.height();

    Wire.begin();
    if (!vcnl4040.begin()) {
        while (1); // Halt if sensor not found
    }
    vcnl4040.setProximityLEDCurrent(VCNL4040_LED_CURRENT_100MA);
    vcnl4040.setProximityLEDDutyCycle(VCNL4040_LED_DUTY_1_40);
    

    WiFi.begin(wifiNetworkName.c_str(), wifiPassword.c_str());
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }

    timeClient.begin();
    timeClient.update();
    
    // === Added for Light Sensor ===
    M5.Display.setBrightness(MAX_BRIGHTNESS);  // Initial brightness setting
}

///////////////////////////////////////////////////////////////
void loop() {
    M5.update();
    
    checkProximity();
    // === Added for Light Sensor ===
    adjustBrightness();  // Adjust LCD brightness based on ambient light
    
    if (M5.BtnA.wasPressed() && displayOn) {  // Only process if display is on
        isFahrenheit = !isFahrenheit;
        fetchWeatherDetails();
        delay(10);
        drawWeatherDisplay();
    }

    if (M5.Touch.getDetail().wasPressed() && displayState == ZIP && displayOn) {
        checkButtonPress();
    }

    if (M5.BtnB.wasPressed() && displayOn) {
        if (displayState == WEATHER) {
            drawZipcodeSelectScreen();
            displayState = ZIP;
            timerDelay = 1000;
            tp = false;
        } else {
            getZipcode();
            fetchWeatherDetails();
            delay(10);
            drawWeatherDisplay();
            displayState = WEATHER;
            timerDelay = 2500;
        }
    }

    if ((millis() - lastTime) > timerDelay && displayOn) {
        if (WiFi.status() == WL_CONNECTED) {
            if (displayState == WEATHER) {
                fetchWeatherDetails();
                drawWeatherDisplay();
            }
        }
        lastTime = millis();
    }
}

void checkProximity() {
    uint16_t proximity = vcnl4040.getProximity();
    
    if (proximity > PROXIMITY_THRESHOLD && displayOn) {
        // Too close - turn off display
        M5.Display.sleep();
        displayOn = false;
    } 
    else if (proximity <= PROXIMITY_THRESHOLD && !displayOn) {
        // Far enough - turn on display
        M5.Display.wakeup();
        displayOn = true;
        // Redraw the screen based on current state
        if (displayState == WEATHER) {
            drawWeatherDisplay();
        } else {
            drawZipcodeSelectScreen();
        }
    }
}

// === Added for Light Sensor ===
void adjustBrightness() {
    if (!displayOn) return;  // Don't adjust brightness if display is off
    
    uint16_t light = vcnl4040.getLux();  // Get ambient light value in lux
    
    // Map light value to brightness range
    light = constrain(light, MIN_LIGHT, MAX_LIGHT);  // Keep within bounds
    uint8_t brightness = map(light, MIN_LIGHT, MAX_LIGHT, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
    
    M5.Display.setBrightness(brightness);  // Set LCD brightness
}

/////////////////////////////////////////////////////////////////
void fetchWeatherDetails() {
    // Update the NTP client to get current time
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

    String serverURL = urlOpenWeather + "zip=" + zipcode + ",us&units=imperial&appid=" + apiKey;
    String response = httpGETRequest(serverURL.c_str());

    // === Updated for ArduinoJson 7.x ===
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) return;

    JsonArray arrWeather = doc["weather"];
    JsonObject objWeather0 = arrWeather[0];
    strWeatherDesc = objWeather0["main"].as<String>();
    strWeatherIcon = objWeather0["icon"].as<String>();
    cityName = doc["name"].as<String>();
    JsonObject objMain = doc["main"];
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

    // Draw last sync time
    int timestampY = sHeight - 30;  // 30 px from bottom
    M5.Lcd.setCursor(pad, timestampY);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(syncTextColor);
    M5.Lcd.printf("Last Sync: %s", lastSyncTime.c_str());
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

//////////////////////////////////////////////////////////////////////////////////
// For more documentation see the following links:
// https://github.com/m5stack/m5-docs/blob/master/docs/en/api/
// https://docs.m5stack.com/en/api/core2/lcd_api
//////////////////////////////////////////////////////////////////////////////////