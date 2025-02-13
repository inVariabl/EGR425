#include <M5Core2.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "EGR425_Phase1_weather_bitmap_images.h"
#include "WiFi.h"

////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////
// TODO 3: Register for openweather account and get API key
String urlOpenWeather = "https://api.openweathermap.org/data/2.5/weather?";
String apiKey ="d4fbab132209f8288d5ee07e27bfa1d2";

// TODO 1: WiFi variables
String wifiNetworkName = "CBU-LANCERS";
String wifiPassword = "L@ncerN@tion";

// Time variables
unsigned long lastTime = 0;
unsigned long timerDelay = 5000;  // 5000; 5 minutes (300,000ms) or 5 seconds (5,000ms)

// LCD variables
int sWidth;
int sHeight;

// Weather/zip variables
String strWeatherIcon;
String strWeatherDesc;
String cityName;
double tempNow;
double tempMin;
double tempMax;

////////////////////////////////////////////////////////////////////
// Method header declarations
////////////////////////////////////////////////////////////////////
String httpGETRequest(const char* serverName);
void drawWeatherImage(String iconId, int resizeMult, int xOffset, int yOffset, int iconWidth, int iconHeight);
void fetchWeatherDetails();
void drawWeatherDisplay();

///////////////////////////////////////////////////////////////
// Put your setup code here, to run once
///////////////////////////////////////////////////////////////
void setup() {
    // Initialize the device
    M5.begin();

    // Set screen orientation and get height/width
    sWidth = M5.Lcd.width();
    sHeight = M5.Lcd.height();

    // TODO 2: Connect to WiFi
    WiFi.begin(wifiNetworkName.c_str(), wifiPassword.c_str());
    Serial.printf("Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.print("\n\nConnected to WiFi network with IP address: ");
    Serial.println(WiFi.localIP());
}

///////////////////////////////////////////////////////////////
// Put your main code here, to run repeatedly
///////////////////////////////////////////////////////////////
void loop() {

    // Only execute every so often
    if ((millis() - lastTime) > timerDelay) {
        if (WiFi.status() == WL_CONNECTED) {

            fetchWeatherDetails();
            drawWeatherDisplay();

        } else {
            Serial.println("WiFi Disconnected");
        }

        // Update the last time to NOW
        lastTime = millis();
    }
}

/////////////////////////////////////////////////////////////////
// This method fetches the weather details from the OpenWeather
// API and saves them into the fields defined above
/////////////////////////////////////////////////////////////////
void fetchWeatherDetails() {
    //////////////////////////////////////////////////////////////////
    // Hardcode the specific city,state,country into the query
    // Examples: https://api.openweathermap.org/data/2.5/weather?q=riverside,ca,usa&units=imperial&appid=YOUR_API_KEY
    //////////////////////////////////////////////////////////////////
    String serverURL = urlOpenWeather + "q=monrovia,ca,usa&units=imperial&appid=" + apiKey;
    //Serial.println(serverURL); // Debug print

    //////////////////////////////////////////////////////////////////
    // Make GET request and store reponse
    //////////////////////////////////////////////////////////////////
    String response = httpGETRequest(serverURL.c_str());
    //Serial.print(response); // Debug print

    //////////////////////////////////////////////////////////////////
    // Import ArduinoJSON Library and then use arduinojson.org/v6/assistant to
    // compute the proper capacity (this is a weird library thing) and initialize
    // the json object
    //////////////////////////////////////////////////////////////////
    const size_t jsonCapacity = 768+250;
    DynamicJsonDocument objResponse(jsonCapacity);

    //////////////////////////////////////////////////////////////////
    // Deserialize the JSON document and test if parsing succeeded
    //////////////////////////////////////////////////////////////////
    DeserializationError error = deserializeJson(objResponse, response);
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }
    //serializeJsonPretty(objResponse, Serial); // Debug print

    //////////////////////////////////////////////////////////////////
    // Parse Response to get the weather description and icon
    //////////////////////////////////////////////////////////////////
    JsonArray arrWeather = objResponse["weather"];
    JsonObject objWeather0 = arrWeather[0];
    String desc = objWeather0["main"];
    String icon = objWeather0["icon"];
    String city = objResponse["name"];

    // ArduionJson library will not let us save directly to these
    // variables in the 3 lines above for unknown reason
    strWeatherDesc = desc;
    strWeatherIcon = icon;
    cityName = city;

    // Parse response to get the temperatures
    JsonObject objMain = objResponse["main"];
    tempNow = objMain["temp"];
    tempMin = objMain["temp_min"];
    tempMax = objMain["temp_max"];
    Serial.printf("NOW: %.1f F and %s\tMIN: %.1f F\tMax: %.1f F\n", tempNow, strWeatherDesc, tempMin, tempMax);
}

/////////////////////////////////////////////////////////////////
// Update the display based on the weather variables defined
// at the top of the screen.
/////////////////////////////////////////////////////////////////
void drawWeatherDisplay() {
    //////////////////////////////////////////////////////////////////
    // Draw background - light blue if day time and navy blue of night
    //////////////////////////////////////////////////////////////////
    uint16_t primaryTextColor;
    uint16_t backgroundColor;
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

    // Draw "LO" temperature
    M5.Lcd.setCursor(textX, textY);
    M5.Lcd.setTextColor(TFT_BLUE);
    M5.Lcd.setTextSize(2);
    M5.Lcd.printf("LO: %0.fF\n", tempMin);

    // Draw current temperature
    textY += 30; // Adjust vertical spacing
    M5.Lcd.setCursor(textX, textY);
    M5.Lcd.setTextColor(primaryTextColor);
    M5.Lcd.setTextSize(6); // Larger font size for current temperature
    M5.Lcd.printf("%0.fF\n", tempNow);

    // Draw "HI" temperature
    textY += 70; // Adjust vertical spacing
    M5.Lcd.setCursor(textX, textY);
    M5.Lcd.setTextColor(TFT_RED);
    M5.Lcd.setTextSize(2);
    M5.Lcd.printf("HI: %0.fF\n", tempMax);

    // Draw city name
    textY += 30; // Adjust vertical spacing
    M5.Lcd.setCursor(textX, textY);
    M5.Lcd.setTextColor(primaryTextColor);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextWrap(true); // Enable text wrapping for long city names
    M5.Lcd.printf("%s\n", cityName.c_str());
}

/////////////////////////////////////////////////////////////////
// This method takes in a URL and makes a GET request to the
// URL, returning the response.
/////////////////////////////////////////////////////////////////
String httpGETRequest(const char* serverURL) {

    // Initialize client
    HTTPClient http;
    http.begin(serverURL);

    // Send HTTP GET request and obtain response
    int httpResponseCode = http.GET();
    String response = http.getString();

    // Check if got an error
    if (httpResponseCode > 0)
        Serial.printf("HTTP Response code: %d\n", httpResponseCode);
    else {
        Serial.printf("HTTP Response ERROR code: %d\n", httpResponseCode);
        Serial.printf("Server Response: %s\n", response);
    }

    // Free resources and return response
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
