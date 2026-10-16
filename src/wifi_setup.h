// wifi_setup.h — tzapu/WiFiManager wrapper, isolated in its own translation unit.
// MUST NOT be included together with anything that pulls ESPAsyncWebServer.h in the
// same .cpp: WiFiManager.h drags in ESP8266WebServer.h, whose `enum HTTPMethod`
// (HTTP_GET, HTTP_POST, ...) collides with ESPAsyncWebServer's own HTTP_GET/HTTP_POST
// enumerators at global scope. Keeping WiFiManager usage confined to wifi_setup.cpp
// (which never includes ESPAsyncWebServer.h) avoids the redefinition error.
#pragma once
#include <Arduino.h>

enum WifiSetupStatus {
    WIFI_SETUP_PORTAL_ACTIVE, // AP config portal open, waiting for user
    WIFI_SETUP_CONNECTING,    // trying to join saved/new credentials
    WIFI_SETUP_CONNECTED
};

// Starts WiFiManager (non-blocking config portal) and the OWM API key custom field.
void wifiSetupBegin(const char* apSsid, const char* apPassword,
                     uint32_t portalTimeoutS, uint32_t stallTimeoutMs,
                     const String& defaultApiKey);

// Call every loop() iteration while WiFi isn't connected yet.
// Restarts the device internally if stalled without connecting or opening a portal.
WifiSetupStatus wifiSetupProcess();

// Current value of the API key field (default or user-edited).
String wifiSetupGetApiKey();

// Returns true once, right after the portal's Save button persisted new credentials.
bool wifiSetupConsumeReconfigured();

// Clears saved WiFi STA credentials (SDK flash + WiFiManager) so the next
// autoConnect() opens the config portal. Call from loop() only — not from an
// AsyncWebServer handler (resetSettings() delay()s and erases flash).
void wifiSetupResetCredentials();
