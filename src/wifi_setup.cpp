// wifi_setup.cpp — see wifi_setup.h for why this stays in its own translation unit.
#include "wifi_setup.h"
#include <ESP8266WiFi.h>
#include <WiFiManager.h> // tzapu/WiFiManager

static WiFiManager           wm;
static WiFiManagerParameter* apiKeyParam     = nullptr;
static WiFiManagerParameter* apiKeyLinkHtml  = nullptr;
static volatile bool         saveFlag        = false;
static uint32_t              startMs         = 0;
static uint32_t              stallTimeoutMs  = 0;

static void onSaved() {
    saveFlag = true;
}

void wifiSetupBegin(const char* apSsid, const char* apPassword,
                     uint32_t portalTimeoutS, uint32_t stallTimeoutMsIn,
                     const String& defaultApiKey) {
    stallTimeoutMs = stallTimeoutMsIn;

    apiKeyParam = new WiFiManagerParameter("apiKey", "openweathermap.org API key",
                                            defaultApiKey.c_str(), 40, "type='password'");
    apiKeyLinkHtml = new WiFiManagerParameter(
        "<a href='https://openweathermap.org/' target='_blank'>Get / verify API key</a>");
    wm.addParameter(apiKeyParam);
    wm.addParameter(apiKeyLinkHtml);

    wm.setTitle("Forecaster Light Configuration");
    wm.setSaveConfigCallback(onSaved);
    wm.setConfigPortalBlocking(false);
    wm.setConfigPortalTimeout(portalTimeoutS);

    startMs = millis();
    wm.autoConnect(apSsid, apPassword);
}

WifiSetupStatus wifiSetupProcess() {
    wm.process();

    if (WiFi.status() == WL_CONNECTED) {
        return WIFI_SETUP_CONNECTED;
    }
    if (wm.getConfigPortalActive()) {
        return WIFI_SETUP_PORTAL_ACTIVE;
    }
    if (millis() - startMs > stallTimeoutMs) {
        Serial.println("[WiFiManager] Stalled without connecting or opening portal — restarting to retry");
        ESP.restart();
    }
    return WIFI_SETUP_CONNECTING;
}

String wifiSetupGetApiKey() {
    return apiKeyParam ? String(apiKeyParam->getValue()) : String();
}

bool wifiSetupConsumeReconfigured() {
    if (!saveFlag) return false;
    saveFlag = false;
    return true;
}

void wifiSetupResetCredentials() {
    // Must run from loop(), never from an AsyncWebServer callback:
    // resetSettings() delay()s and writes the SDK config sector.
    Serial.println("[WiFi] Erasing saved STA credentials");
    wm.resetSettings();
    WiFi.persistent(true);
    WiFi.disconnect(true, true);
    WiFi.persistent(false);
    ESP.eraseConfig();
}
