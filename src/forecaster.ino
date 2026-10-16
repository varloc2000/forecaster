// forecaster.ino — ESP12F weather LED display (4x4 Matrix Driver Architecture)
// Requires: FastLED, ESP8266WiFi, ESP8266HTTPClient, ArduinoJson (v6), ESPAsyncWebServer, LittleFS, WiFiManager (tzapu)

#include <Wire.h>
#include <FastLED.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include "wifi_setup.h" // tzapu/WiFiManager kept in its own .cpp — see wifi_setup.h for why

// ── Hardware constants ────────────────────────────────────────────────────────
static const uint8_t PIN_LED_DATA = 5;
static const uint8_t PIN_BUTTON   = 4;
static const uint8_t MATRIX_WIDTH  = 4;
static const uint8_t MATRIX_HEIGHT = 4;
static const uint16_t LED_COUNT   = MATRIX_WIDTH * MATRIX_HEIGHT; // 16 LEDs

// ── Network / API constants ───────────────────────────────────────────────────
static const char* CONFIG_AP_SSID       = "forecaster_wifi";
static const char* CONFIG_AP_PASSWORD   = "Forec@ster";
static const char* OWM_API_KEY_DEFAULT  = "";
static const char* CONFIG_FILE_PATH     = "/config.json";
static const uint32_t CONFIG_PORTAL_TIMEOUT_S = 300;

static const char* WEATHER_LOCATION_LAT   = "54.5189";
static const char* WEATHER_LOCATION_LON   = "18.5319";
static const char* WEATHER_LANGUAGE       = "en";
static const uint8_t WEATHER_MAX_FORECASTS  = 1;
static const bool    WEATHER_IS_METRIC      = true;

static const uint32_t WIFI_TIMEOUT_MS       = 15000;
static const uint32_t WEATHER_INTERVAL_MS   = 10UL * 60UL * 1000UL;
static const uint8_t  WEATHER_MAX_RETRIES   = 3;
static const uint32_t FETCH_RETRY_DELAY_MS = 3000;

// ── Wind speed thresholds (m/s) ───────────────────────────────────────────────
static const float WIND_WINDY_MS  = 7.0f;
static const float WIND_RAINY_MS  = 10.0f;
static const float WIND_STORMY_MS = 15.0f;

// ── Enums ─────────────────────────────────────────────────────────────────────
enum DeviceMode {
    MODE_CONFIGURE,
    MODE_CONNECTING,
    MODE_AMBIENT,
    MODE_LIGHT,
    MODE_FORECAST,
    MODE_TESTING,
    MODE_ERROR
};

enum WeatherCondition {
    WC_CLEAR,
    WC_CLEAR_CLOUDY,
    WC_CLEAR_CLOUDY_WINDY,
    WC_CLOUDY,
    WC_CLOUDY_WINDY,
    WC_RAIN,
    WC_RAIN_WINDY,
    WC_STORMY
};

enum RainLevel {
    RAIN_LIGHT,
    RAIN_MODERATE,
    RAIN_HEAVY
};

// ── Structs ───────────────────────────────────────────────────────────────────
struct WeatherData {
    int   weatherId;
    float temperature;
    float windSpeed;
    int   windDeg;
    float rain3h;
    bool  valid;
};

// ── Global state ──────────────────────────────────────────────────────────────
CRGB            leds[LED_COUNT];
CRGB            logicalCanvas[MATRIX_WIDTH][MATRIX_HEIGHT];
DeviceMode      currentMode       = MODE_CONFIGURE;
WeatherData     weatherData       = {};
WeatherCondition weatherCond      = WC_CLEAR;
RainLevel       rainLevel         = RAIN_LIGHT;
uint8_t         fetchFailCount    = 0;
uint32_t        lastWeatherFetch  = 0;
uint32_t        connectingStartMs = 0;

// WiFiManager captive-portal config flow
String runtimeOwmApiKey = OWM_API_KEY_DEFAULT;
bool   wifiManagerDone  = false;

// Deferred restart (let async HTTP response flush before ESP.restart())
bool     restartPending    = false;
bool     wifiResetPending  = false;
uint32_t restartAtMs       = 0;

// Testing mode variables
uint32_t        testingModeTimer  = 0;
uint8_t         testingStep       = 0;

static bool ledYellow[LED_COUNT];
static bool ledCloud[LED_COUNT];
static bool ledBlue[LED_COUNT];

static AsyncWebServer webServer(80);

// ─────────────────────────────────────────────────────────────────────────────
// Helpers & Matrix Mapping
// ─────────────────────────────────────────────────────────────────────────────

uint16_t XY(uint8_t x, uint8_t y) {
    uint16_t index;
    if (x % 2 == 0) {
        index = (x * MATRIX_HEIGHT) + y;
    } else {
        index = (x * MATRIX_HEIGHT) + (MATRIX_HEIGHT - 1 - y);
    }
    return index;
}

const char* modeToString(DeviceMode m) {
    switch (m) {
        case MODE_CONFIGURE:  return "CONFIGURE";
        case MODE_CONNECTING: return "CONNECTING";
        case MODE_AMBIENT:    return "AMBIENT";
        case MODE_LIGHT:      return "LIGHT";
        case MODE_FORECAST:   return "FORECAST";
        case MODE_TESTING:    return "TESTING";
        case MODE_ERROR:      return "ERROR";
    }
    return "UNKNOWN";
}

const char* windDirLabel(int deg) {
    static const char* DIRS[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int idx = ((deg % 360) + 360) % 360;
    idx = ((idx + 22) / 45) % 8;
    return DIRS[idx];
}

const char* conditionToString(WeatherCondition c) {
    switch (c) {
        case WC_CLEAR:              return "Clear";
        case WC_CLEAR_CLOUDY:       return "Partly Cloudy";
        case WC_CLEAR_CLOUDY_WINDY: return "Partly Cloudy, Windy";
        case WC_CLOUDY:             return "Cloudy";
        case WC_CLOUDY_WINDY:       return "Cloudy, Windy";
        case WC_RAIN:               return "Rain";
        case WC_RAIN_WINDY:         return "Rain, Windy";
        case WC_STORMY:             return "Storm";
    }
    return "Unknown";
}

// ── API key config persistence (LittleFS /config.json) ──────────────────────

void loadApiKeyConfig() {
    File f = LittleFS.open(CONFIG_FILE_PATH, "r");
    if (!f) {
        runtimeOwmApiKey = OWM_API_KEY_DEFAULT;
        return;
    }
    DynamicJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    const char* key = doc["owmApiKey"];
    if (err || key == nullptr || strlen(key) == 0) {
        runtimeOwmApiKey = OWM_API_KEY_DEFAULT;
        return;
    }
    runtimeOwmApiKey = key;
}

void saveApiKeyConfig(const String& apiKey) {
    File f = LittleFS.open(CONFIG_FILE_PATH, "w");
    if (!f) {
        Serial.println("[Config] Failed to open /config.json for write");
        return;
    }
    DynamicJsonDocument doc(256);
    doc["owmApiKey"] = apiKey;
    serializeJson(doc, f);
    f.close();
}

void clearApiKeyConfig() {
    LittleFS.remove(CONFIG_FILE_PATH);
}

// ── Web API (JSON only) ──────────────────────────────────────────────────────

static const char* rainLevelLabel() {
    if (rainLevel == RAIN_HEAVY)    return "Heavy rain";
    if (rainLevel == RAIN_MODERATE) return "Moderate rain";
    return "Light rain";
}

static bool weatherHasRain() {
    return weatherCond == WC_RAIN || weatherCond == WC_RAIN_WINDY || weatherCond == WC_STORMY;
}

static String buildStateJson() {
    DynamicJsonDocument doc(512);
    char tempBuf[8];
    char windBuf[8];
    doc["mode"] = modeToString(currentMode);

    if (weatherData.valid) {
        dtostrf(weatherData.temperature, 0, 1, tempBuf);
        dtostrf(weatherData.windSpeed, 0, 1, windBuf);
        JsonObject weather = doc.createNestedObject("weather");
        weather["condition"] = conditionToString(weatherCond);
        weather["temperature"] = serialized(tempBuf);
        weather["wind"] = serialized(windBuf);
        weather["windDeg"] = weatherData.windDeg;
        weather["windDir"] = windDirLabel(weatherData.windDeg);
        if (weatherHasRain()) {
            weather["rain"] = rainLevelLabel();
        }
    }

    String json;
    serializeJsonPretty(doc, json);
    return json;
}

static void handleModePost(AsyncWebServerRequest* req) {
    if (req->hasParam("set", true)) {
        String val = req->getParam("set", true)->value();
        if (val == "CONNECTING" && currentMode == MODE_ERROR) {
            Serial.println("[Web] Reconnect requested");
            WiFi.reconnect();
            connectingStartMs = millis(); // Reset timeout timer
            setMode(MODE_CONNECTING);
        } else if (val == "AMBIENT") {
            setMode(MODE_AMBIENT);
        } else if (val == "LIGHT") {
            setMode(MODE_LIGHT);
        } else if (val == "FORECAST" && weatherData.valid) {
            setMode(MODE_FORECAST);
        } else if (val == "TESTING") {
            setMode(MODE_TESTING);
            testingModeTimer = millis();
        }
    }
    req->send(200, "application/json", buildStateJson());
}

void setupWebServer() {
    if (!LittleFS.begin()) {
        Serial.println("[FS] LittleFS mount failed — web UI unavailable");
        return;
    }
    webServer.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    webServer.on("/state", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", buildStateJson());
    });
    webServer.on("/mode", HTTP_POST, [](AsyncWebServerRequest* req) {
        handleModePost(req);
    });
    webServer.on("/reconfigure", HTTP_POST, [](AsyncWebServerRequest* req) {
        Serial.println("[Web] Reconfigure requested — credentials will be cleared then restart");
        req->send(200, "application/json", "{}");
        wifiResetPending = true;
        restartPending = true;
        restartAtMs = millis() + 500;
    });

    webServer.begin();
    Serial.println("[Web] Server started");
}

// ─────────────────────────────────────────────────────────────────────────────
// Driver Layer (Smooth Horizontal Sub-Pixel Scrolling & Composing)
// ─────────────────────────────────────────────────────────────────────────────

void renderDisplayDriver(bool windy = false) {
    // Smooth horizontal scroll position over time (faster if windy)
    uint32_t loopDuration = windy ? 2000UL : 6000UL;
    float scrollPos = (millis() % loopDuration) * ((float)MATRIX_WIDTH / (float)loopDuration); 

    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
            uint16_t physicalIndex = XY(x, y);

            float fx = x - scrollPos;
            while (fx < 0) fx += MATRIX_WIDTH;
            while (fx >= MATRIX_WIDTH) fx -= MATRIX_WIDTH;

            int x1 = (int)fx;
            int x2 = (x1 + 1) % MATRIX_WIDTH;
            float frac = fx - x1;

            CRGB color1 = logicalCanvas[x1][y];
            CRGB color2 = logicalCanvas[x2][y];
            
            CRGB blendedColor;
            blendedColor.r = (uint8_t)((1.0f - frac) * color1.r + frac * color2.r);
            blendedColor.g = (uint8_t)((1.0f - frac) * color1.g + frac * color2.g);
            blendedColor.b = (uint8_t)((1.0f - frac) * color1.b + frac * color2.b);

            leds[physicalIndex] = blendedColor;
        }
    }
    FastLED.show();
}

// ─────────────────────────────────────────────────────────────────────────────
// LED Math & Initializers
// ─────────────────────────────────────────────────────────────────────────────

// Returns a brightness value (0-255) based on continuous time
uint8_t breathe(uint32_t period_ms, uint8_t minVal = 20, uint8_t maxVal = 255, uint32_t phaseMs = 0) {
    
    // 1. Calculate progress within the current breathing cycle (restarts every period_ms)
    uint32_t currentTick = (millis() + phaseMs) % period_ms;
    
    // 2. Convert progress to a percentage from 0.0 (start of cycle) to 1.0 (end of cycle)
    float t = (float)currentTick / (float)period_ms;
    
    // 3. Smooth out the percentage into a wave curve so it fades gently instead of changing abruptly
    // (s goes smoothly from 0.0 at the dimmest point to 1.0 at the brightest point)
    float s = (sin(t * 2.0f * PI - PI / 2.0f) + 1.0f) / 2.0f;
    
    // 4. Map that smooth 0.0–1.0 wave to your chosen minVal and maxVal range
    return (uint8_t)(minVal + s * (maxVal - minVal));
}

void initLedRoles() {
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        ledYellow[i] = (random(3) != 0); 
        ledCloud[i]  = (random(2) == 0); 
    }
}

void initRainBlue() {
    uint8_t blueChance = (rainLevel == RAIN_HEAVY) ? 100
                       : (rainLevel == RAIN_MODERATE) ? 60
                       : 30;
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        ledBlue[i] = !ledCloud[i] && ((uint8_t)random(100) < blueChance);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// LED Effects (Logical Canvas Painters)
// ─────────────────────────────────────────────────────────────────────────────

void effectConnecting() {
    uint8_t b = breathe(4000, 100, 220);
    fill_solid(leds, LED_COUNT, CHSV(240, 110, b));
    FastLED.show();
}

void effectConfigure() {
    uint8_t b = breathe(4000, 100, 220);
    fill_solid(leds, LED_COUNT, CHSV(240, 110, b));
    FastLED.show();
}

void effectError() {
    uint8_t b = breathe(2000, 20, 220);
    fill_solid(leds, LED_COUNT, CHSV(220, 210, b));
    FastLED.show();
}

void effectAmbient() {
    uint8_t initialHue = millis() / 60;
    uint8_t deltaHue = 255 / LED_COUNT; 
    
    fill_rainbow(leds, LED_COUNT, initialHue, deltaHue);
    FastLED.show();
}

void effectLight() {
    fill_solid(leds, LED_COUNT, CHSV(255, 255, 255));
    FastLED.show();
}

void effectClear() {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
            uint8_t i = (x * MATRIX_HEIGHT) + y;
            uint8_t b = breathe(2500, 170, 255, i * 200UL);
            logicalCanvas[x][y] = CHSV(38 + (i % 4), 200, b);
        }
    }
    renderDisplayDriver(false);
}

void effectClearCloudy() {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
            uint8_t i = (x * MATRIX_HEIGHT) + y;
            uint8_t b = breathe(3000, 150, 255, i * 220UL);
            if (ledYellow[i]) {
                logicalCanvas[x][y] = CHSV(38, 200, b);
            } else {
                logicalCanvas[x][y] = CRGB::Black;
            }
        }
    }
    renderDisplayDriver(false);
}

void effectClearCloudyWindy() {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
            uint8_t i = (x * MATRIX_HEIGHT) + y;
            uint8_t b = breathe(1500, 150, 255, i * 140UL);
            if (ledYellow[i]) {
                logicalCanvas[x][y] = CHSV(38, 200, b);
            } else {
                logicalCanvas[x][y] = CRGB::Black;
            }
        }
    }
    renderDisplayDriver(true);
}

void effectCloudy() {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
            uint8_t i = (x * MATRIX_HEIGHT) + y;
            if (ledCloud[i]) {
                uint8_t b = breathe(3500, 100, 210, i * 280UL);
                logicalCanvas[x][y] = CHSV(0, 0, b);
            } else {
                logicalCanvas[x][y] = CRGB::Black;
            }
        }
    }
    renderDisplayDriver(false);
}

void effectCloudyWindy() {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
            uint8_t i = (x * MATRIX_HEIGHT) + y;
            if (ledCloud[i]) {
                uint8_t b = breathe(1600, 100, 210, i * 170UL);
                logicalCanvas[x][y] = CHSV(0, 0, b);
            } else {
                logicalCanvas[x][y] = CRGB::Black;
            }
        }
    }
    renderDisplayDriver(true);
}

void effectRain() {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
            uint8_t i = (x * MATRIX_HEIGHT) + y;
            if (ledCloud[i]) {
                uint8_t b = breathe(3500, 100, 210, i * 280UL);
                logicalCanvas[x][y] = CHSV(0, 0, b);
            } else if (ledBlue[i]) {
                uint8_t b = breathe(3500, 70, 180, i * 280UL);
                logicalCanvas[x][y] = CHSV(160, 230, b);
            } else {
                logicalCanvas[x][y] = CRGB::Black;
            }
        }
    }
    renderDisplayDriver(false);
}

void effectRainWindy() {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
            uint8_t i = (x * MATRIX_HEIGHT) + y;
            if (ledCloud[i]) {
                uint8_t b = breathe(1600, 100, 210, i * 170UL);
                logicalCanvas[x][y] = CHSV(0, 0, b);
            } else if (ledBlue[i]) {
                uint8_t b = breathe(1600, 70, 180, i * 170UL);
                logicalCanvas[x][y] = CHSV(160, 230, b);
            } else {
                logicalCanvas[x][y] = CRGB::Black;
            }
        }
    }
    renderDisplayDriver(true);
}

void effectStormy() {
    static uint32_t nextSplash  = 0;
    static uint32_t splashEnd   = 0;
    static uint8_t  splashStart = 0;
    static uint8_t  splashLen   = 0;
    static bool     splashing   = false;

    uint32_t t = millis();

    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
            uint8_t i = (x * MATRIX_HEIGHT) + y;
            if (ledCloud[i]) {
                uint8_t b = breathe(1600, 100, 210, i * 170UL);
                logicalCanvas[x][y] = CHSV(0, 0, b);
            } else {
                uint8_t b = breathe(1600, 70, 180, i * 170UL);
                logicalCanvas[x][y] = CHSV(160, 230, b);
            }
        }
    }

    if (!splashing && t >= nextSplash) {
        splashing   = true;
        splashStart = random8(0, LED_COUNT - 2);
        splashLen   = random8(1, 4);
        splashEnd   = t + random(40, 150);
        nextSplash  = t + random(500, 2000);
    }

    renderDisplayDriver(true);

    if (splashing) {
        if (t < splashEnd) {
            for (uint8_t i = splashStart; i < splashStart + splashLen && i < LED_COUNT; i++) {
                leds[i] = CRGB(255, 220, 0);
            }
            FastLED.show();
        } else {
            splashing = false;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Weather fetch & classify
// ─────────────────────────────────────────────────────────────────────────────

WeatherCondition classifyWeather(const WeatherData& d) {
    int   id   = d.weatherId;
    float wind = d.windSpeed;

    if ((id >= 503 && id <= 504) || id == 522 || id == 531) rainLevel = RAIN_HEAVY;
    else if (id == 502 || (id >= 520 && id <= 521))         rainLevel = RAIN_MODERATE;
    else                                                    rainLevel = RAIN_LIGHT;

    if ((id >= 200 && id < 300) || id == 771 || id == 781 || wind > WIND_STORMY_MS) {
        rainLevel = RAIN_HEAVY;
        return WC_STORMY;
    }
    if (id >= 300 && id < 700) {
        return (wind > WIND_RAINY_MS) ? WC_RAIN_WINDY : WC_RAIN;
    }
    if (id == 804 || id == 711 || id == 731 || id == 751 || id == 761 || id == 762) {
        return (wind > WIND_WINDY_MS) ? WC_CLOUDY_WINDY : WC_CLOUDY;
    }
    if (id == 701 || id == 721 || id == 741) {
        return WC_CLOUDY;
    }
    if (id >= 802 && id <= 803) {
        return (wind > WIND_WINDY_MS) ? WC_CLEAR_CLOUDY_WINDY : WC_CLEAR_CLOUDY;
    }
    return WC_CLEAR;
}

void applyWeather() {
    weatherCond = classifyWeather(weatherData);
    initLedRoles();
    initRainBlue();
}

bool fetchWeather() {
    WiFiClient client;
    HTTPClient http;

    String url = String("http://api.openweathermap.org/data/2.5/forecast") +
                 "?lat="   + WEATHER_LOCATION_LAT +
                 "&lon="   + WEATHER_LOCATION_LON +
                 "&cnt="   + WEATHER_MAX_FORECASTS +
                 "&lang="  + WEATHER_LANGUAGE +
                 "&units=" + (WEATHER_IS_METRIC ? "metric" : "imperial") +
                 "&appid=" + runtimeOwmApiKey;

    Serial.print("[Weather] Free heap before fetch: ");
    Serial.println(ESP.getFreeHeap());
    Serial.print("[Weather] GET ");
    Serial.println(url);

    http.begin(client, url);
    int code = http.GET();
    Serial.print("[Weather] HTTP response code: ");
    Serial.println(code);

    if (code != HTTP_CODE_OK) {
        Serial.println("[Weather] Fetch failed — non-200 response");
        http.end();
        return false;
    }

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        Serial.print("[Weather] JSON parse error: ");
        Serial.println(err.c_str());
        return false;
    }

    JsonObject entry = doc["list"][0];
    weatherData.weatherId   = entry["weather"][0]["id"].as<int>();
    weatherData.temperature = entry["main"]["temp"].as<float>();
    weatherData.windSpeed   = entry["wind"]["speed"].as<float>();
    weatherData.windDeg     = entry["wind"]["deg"].as<int>();
    weatherData.rain3h      = entry["rain"]["3h"] | 0.0f;
    weatherData.valid       = true;

    Serial.printf("[Weather] Parsed OK — id=%d temp=%.1f wind=%.1f rain3h=%.1f\n",
        weatherData.weatherId, weatherData.temperature,
        weatherData.windSpeed, weatherData.rain3h);
    Serial.print("[Weather] Free heap after fetch: ");
    Serial.println(ESP.getFreeHeap());

    return true;
}

bool syncWeather() {
    Serial.println("[Weather] syncWeather() start");
    if (fetchWeather()) {
        lastWeatherFetch = millis();
        fetchFailCount = 0;
        applyWeather();
        Serial.println("[Weather] syncWeather() success — weather applied");
        return true;
    } else {
        lastWeatherFetch = millis();
        fetchFailCount++;
        Serial.printf("[Weather] syncWeather() failed — fail count now %d/%d\n",
            fetchFailCount, WEATHER_MAX_RETRIES);
        return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Mode Handlers
// ─────────────────────────────────────────────────────────────────────────────

void setMode(DeviceMode newMode) {
    if (currentMode != newMode) {
        Serial.print("[System] Mode changed: ");
        Serial.print(modeToString(currentMode));
        Serial.print(" -> ");
        Serial.println(modeToString(newMode));
        currentMode = newMode;
    }
}

void manageWeatherUpdates() {
    if (currentMode == MODE_CONNECTING || currentMode == MODE_ERROR) return;

    uint32_t interval = (fetchFailCount > 0) ? FETCH_RETRY_DELAY_MS : WEATHER_INTERVAL_MS;

    if (millis() - lastWeatherFetch >= interval) {
        yield(); 
        if (!syncWeather() && fetchFailCount >= WEATHER_MAX_RETRIES) {
            Serial.println("[Weather] Max retries reached. Switching to ERROR mode.");
            setMode(MODE_ERROR);
        }
        yield();
    }
}

void handleConnecting() {
    effectConnecting();
    yield();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("[WiFi] Connected! Web UI IP Address: ");
        Serial.println(WiFi.localIP());
        
        setupWebServer();
        setMode(MODE_AMBIENT);
        if (syncWeather()) {
            setMode(MODE_FORECAST);
        }
    } 
    else if (millis() - connectingStartMs > WIFI_TIMEOUT_MS) {
        Serial.println("[WiFi] Connection timeout. Switching to ERROR mode.");
        setMode(MODE_ERROR);
    }
}

void handleTesting() {
    if (millis() - testingModeTimer > 30000UL) {
        testingModeTimer = millis();
        testingStep = (testingStep + 1) % 5;
    }

    switch (testingStep) {
        case 0: effectClear();          break;
        case 1: effectClearCloudy();    break;
        case 2: effectClearCloudyWindy(); break;
        case 3: effectCloudy();         break;
        case 4: effectCloudyWindy();    break;
        case 5: effectRain();           break;
        case 6: effectRainWindy();      break;
        case 7: effectStormy();         break;
    }
}

void handleForecast() {
    switch (weatherCond) {
        case WC_CLEAR:              effectClear();          break;
        case WC_CLEAR_CLOUDY:       effectClearCloudy();    break;
        case WC_CLEAR_CLOUDY_WINDY: effectClearCloudyWindy(); break;
        case WC_CLOUDY:             effectCloudy();         break;
        case WC_CLOUDY_WINDY:       effectCloudyWindy();    break;
        case WC_RAIN:               effectRain();           break;
        case WC_RAIN_WINDY:         effectRainWindy();      break;
        case WC_STORMY:             effectStormy();         break;
    }
}

void testLeds() {
    const CRGB colors[] = { CRGB::Red, CRGB::Green, CRGB::Blue };
    FastLED.clear();
    FastLED.show();
    
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        for (uint8_t c = 0; c < 3; c++) {
            leds[i] = colors[c];
            FastLED.show();
            delay(50);
            leds[i] = CRGB::Black;
            FastLED.show();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// setup / loop
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(100);

    FastLED.addLeds<WS2812B, PIN_LED_DATA, GRB>(leds, LED_COUNT);
    FastLED.setBrightness(180);
    FastLED.clear();
    FastLED.show();

    testLeds();
    initLedRoles();

    testingModeTimer = millis();
    testingStep = 0;

    if (!LittleFS.begin()) {
        Serial.println("[FS] LittleFS mount failed");
    }
    loadApiKeyConfig();

    currentMode = MODE_CONFIGURE;
    wifiSetupBegin(CONFIG_AP_SSID, CONFIG_AP_PASSWORD, CONFIG_PORTAL_TIMEOUT_S,
                   WIFI_TIMEOUT_MS, runtimeOwmApiKey);
}

void loop() {
    if (restartPending && millis() >= restartAtMs) {
        if (wifiResetPending) {
            wifiSetupResetCredentials();
            wifiResetPending = false;
        }
        ESP.restart();
    }

    if (!wifiManagerDone) {
        WifiSetupStatus st = wifiSetupProcess();
        currentMode = (st == WIFI_SETUP_PORTAL_ACTIVE) ? MODE_CONFIGURE : MODE_CONNECTING;
        (currentMode == MODE_CONFIGURE) ? effectConfigure() : effectConnecting();

        if (st == WIFI_SETUP_CONNECTED) {
            wifiManagerDone = true;
            if (wifiSetupConsumeReconfigured()) {
                Serial.println("[Config] WiFiManager portal saved — persisting API key");
                runtimeOwmApiKey = wifiSetupGetApiKey();
                saveApiKeyConfig(runtimeOwmApiKey);
                restartPending = true;
                restartAtMs = millis() + 500;
            }
            Serial.print("[WiFi] Connected! Web UI IP Address: ");
            Serial.println(WiFi.localIP());
            setupWebServer();
            setMode(MODE_AMBIENT);
            if (syncWeather()) {
                setMode(MODE_FORECAST);
            }
        }
        delay(16);
        return;
    }

    switch (currentMode) {
        case MODE_CONFIGURE:  effectConfigure();  break; // unreachable post-boot, kept for completeness
        case MODE_CONNECTING: handleConnecting(); break;
        case MODE_AMBIENT:    effectAmbient();    break;
        case MODE_LIGHT:      effectLight();      break;
        case MODE_FORECAST:   handleForecast();   break;
        case MODE_TESTING:    handleTesting();    break;
        case MODE_ERROR:      effectError();      break;
    }

    manageWeatherUpdates();
    delay(16); // ~60 fps
}