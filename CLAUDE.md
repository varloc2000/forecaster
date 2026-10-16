## Project

Arduino sketch (`forecaster.ino`) — weather forecasting device (ESP12F, WS2812B 15-led strip, tact button).

Full product behavior, modes, web interface, LED effects: see [PRD.md](PRD.md)
Build / flash / serial monitor steps: see [TECH.md](TECH.md)

## Architecture

Mostly single-file Arduino sketch — device logic lives in `forecaster.ino`. No build system beyond Arduino IDE.

Exception: `wifi_setup.h`/`wifi_setup.cpp` isolate tzapu/WiFiManager in their own translation unit. `WiFiManager.h` pulls in `ESP8266WebServer.h`, whose `enum HTTPMethod` (HTTP_GET, HTTP_POST, …) collides with `ESPAsyncWebServer`'s own HTTP_GET/HTTP_POST enumerators at global scope if both headers land in the same `.cpp`. `forecaster.ino` never includes `WiFiManager.h` — it only calls the primitive-typed functions declared in `wifi_setup.h`.

Web UI is split: **firmware serves JSON only**; **HTML/JS/templates live in LittleFS `/data`**; **browser renders** templates from JSON.

## Coding rules

- Clear separation of functions. Do not put all code into `loop()` or `setup()`.
- All hardcoded parameters (SSID, passwords, API keys, pins) defined as `static const`, `enum`, or `struct`.

## Problems and solutions

Check @.specs/tasks/ folder that contains collection of files describing tasks or problems and Solutions that worked.
