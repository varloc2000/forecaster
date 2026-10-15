## Project

Arduino sketch (`forecaster.ino`) — weather forecasting device.
Consist of components: 
ESP12F chip
WS2812B addressable led strip with 15 leds
Tact switch button

Custom made breakout board to programm ESP12F chip have following pinout:
    - ESP VCC - connected to LM1117 3.3 voltage regulator output through HER208 diode to lower voltage to stable 2.9 v
    - ESP GND - directly connected to ground
    - ESP GPIO15 - pulled down through 10K resistor
    - ESP EN - pulled up through 10K resistor
    - ESP GPIO0 - pulled up through 10K resistor then button pulls it down to GND - button called FLASH button
    - ESP RST - pulled up through 10K resistor then button pulls it down to GND - button called RESET button
    - ESP TXD - connected to CP2102 programmer RXD
    - ESP RXD - connected to CP2102 programmer TXD

## Libraries and predefined variables

Use libraries:
```cpp
#include <Wire.h>
#include <FastLED.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
```

Weather / location constants (in sketch as `static const`):
```cpp
static const char* WEATHER_LOCATION_LAT  = "54.5189";
static const char* WEATHER_LOCATION_LON  = "18.5319";
static const char* WEATHER_LANGUAGE      = "en";
static const uint8_t WEATHER_MAX_FORECASTS = 1;
static const bool    WEATHER_IS_METRIC   = true;
```

OpenWeatherMap forecast URL uses lat/lon (`/data/2.5/forecast?lat=…&lon=…`), not city ID.

## Architecture

Mostly single-file Arduino sketch — device logic lives in `forecaster.ino`. No build system beyond Arduino IDE.

Exception: `wifi_setup.h`/`wifi_setup.cpp` isolate tzapu/WiFiManager in their own translation unit. `WiFiManager.h` pulls in `ESP8266WebServer.h`, whose `enum HTTPMethod` (HTTP_GET, HTTP_POST, …) collides with `ESPAsyncWebServer`'s own HTTP_GET/HTTP_POST enumerators at global scope if both headers land in the same `.cpp`. `forecaster.ino` never includes `WiFiManager.h` — it only calls the primitive-typed functions declared in `wifi_setup.h`.

Web UI is split: **firmware serves JSON only**; **HTML/JS/templates live in LittleFS `/data`**; **browser renders** templates from JSON.

Clear separations on function  
Do not put all code into loop() or setup()  
All hardcoded parameters as SSID passwords api keys pins are defined as static const or enum or struct.

## Device functions

## Web Interface

### Stack
- `ESP8266AsyncWebServer` + `ESPAsyncTCP` — non-blocking HTTP server on port 80
- `LittleFS` — serves static files from `/data` folder
- `htmx` — bundled as `/htmx.js` (no CDN); handles `POST /mode` from mode buttons
- `app.js` — fetches JSON, renders HTML templates client-side, polls every 5s

### Required libraries (install via Arduino Library Manager)
- ESP8266AsyncWebServer
- ESPAsyncTCP
- ArduinoJson (v6)
- FastLED
- (LittleFS is bundled with ESP8266 Arduino core)

### /data folder structure
```
/data
├── index.html              — page shell, styles, PWA meta tags
├── app.js                  — JSON → template renderer, polling, htmx re-process
├── htmx.js                 — htmx library (local copy)
├── manifest.webmanifest    — PWA manifest (Android / installable app)
├── state.fragment.html     — #state component template
├── weather.fragment.html   — weather block (injected when showWeather)
├── favicon.png             — 192×192 PNG (PWA / tab icon)
├── apple-touch-icon.png    — 180×180 PNG (iOS home screen)
└── favicon.ico             — legacy favicon
```

### UI flow
1. Browser loads `index.html` — shows `#state` with "Loading…"
2. `app.js` fetches `state.fragment.html` and `weather.fragment.html` once
3. `GET /state` returns JSON → `renderState()` fills `{{placeholders}}` and replaces `#state`
4. Mode buttons use `hx-post="/mode"` with `hx-swap="none"` (htmx does not swap HTML)
5. On successful `POST /mode`, `htmx:afterRequest` parses JSON response and re-renders
6. `setInterval(refreshState, 5000)` polls `GET /state` for background mode changes

### HTML templates (LittleFS)
Placeholders use `{{key}}` syntax; keys match JSON field names from `/state`.

**state.fragment.html**
| Placeholder | Source |
|---|---|
| `{{mode}}` | Current mode name |
| `{{weatherSection}}` | Rendered weather fragment or empty (built client-side) |
| `{{connectingClass}}` / `{{ambientClass}}` / `{{forecastClass}}` | `"active"` or `""` |

**weather.fragment.html** (only when `showWeather: true`)
| Placeholder | Source |
|---|---|
| `{{condition}}` | Human-readable weather condition |
| `{{temperature}}` | °C, one decimal |
| `{{wind}}` | m/s, one decimal |
| `{{rainSuffix}}` | `" · Light rain"` etc., or empty |

### Server endpoints (JSON only — no HTML generated in firmware)
| Method | Path | Body | Response |
|---|---|---|---|
| GET | `/` | — | `index.html` from LittleFS |
| GET | `/state` | — | `application/json` — current device state |
| POST | `/mode` | `set=AMBIENT` (form field via htmx) | `application/json` — state after mode change |

Example `GET /state` / `POST /mode` response (FORECAST with weather):
```json
{
  "mode": "FORECAST",
  "connectingClass": "",
  "ambientClass": "",
  "forecastClass": "active",
  "showWeather": true,
  "condition": "Clear",
  "temperature": "12.5",
  "wind": "3.2",
  "rainSuffix": " · Light rain"
}
```

When not in FORECAST or weather invalid, weather fields are omitted and `showWeather` is `false`.

Firmware functions: `buildStateJson()`, `handleModePost()` — no `buildStateFragment()` or server-side HTML.

### Mode switch rules (server-side)
- `CONNECTING` → allowed only if currently ERROR (reconnect attempt)
- `AMBIENT` → always allowed
- `FORECAST` → allowed only if `weatherData.valid == true`
- `ERROR` → never settable via web

### Installable app (PWA)
`index.html` includes iOS/Android meta tags; `manifest.webmanifest` defines standalone display and icons.

- **iOS (Safari):** Share → Add to Home Screen
- **Android (Chrome):** Menu → Install app / Add to Home screen

Requires icon files in `/data` (`favicon.png`, `apple-touch-icon.png`). On local HTTP (`192.168.x.x`), iOS Add to Home Screen works; Android full install prompt may require HTTPS.

### index.html behavior
- Displays current device mode as active/highlighted button
- Shows buttons for: CONNECTING, AMBIENT, FORECAST (ERROR button omitted — ERROR is fault state only)
- When in FORECAST with valid weather, shows condition, temperature, wind, rain level
- Polls `GET /state` every 5s via `app.js` (not htmx polling)

### LittleFS filesystem — build and flash guide

**Step 1 — Install mklittlefs**
Download binary for your OS: https://github.com/earlephilhower/mklittlefs/releases

**Step 2 — Build fs.bin from /data**
```bash
/mnt/c/Users/varloc2000/AppData/Local/Arduino15/packages/esp8266/tools/mklittlefs/3.1.0-gcc10.3-e5f9fec/mklittlefs.exe -c data -p 256 -b 8192 -s 1024000 fs.bin
# or add alias to bashrc and use it
# alias mklittlefs_win="/mnt/c/Users/varloc2000/AppData/Local/Arduino15/packages/esp8266/tools/mklittlefs/3.1.0-gcc10.3-e5f9fec/mklittlefs.exe"

mklittlefs_win -c data -p 256 -b 8192 -s 1024000 fs.bin
```
Note: Flags: `-c` source dir, `-p` page size 256, `-b` block size 8192, `-s` size 1MB (adjust to match your flash layout).

**Step 3 — Find flash layout offset**
For ESP12F with 4MB flash (default Arduino board `Generic ESP8266 Module`, Flash Size `2MB (FS:1MB ...)`):
- Sketch: `0x000000`
- LittleFS: `0x100000`

Verify offset matches your board setting in Arduino IDE → Tools → Flash Size.

**Step 4 — Attach windows USB port to WSL**
```bash
# 3.1 Check windows ports 
usbipd list
# Look for CP2102 USB to UART Bridge Controller and use number. Example: 2-11

# 3.2 bind and attach
usbipd bind --busid 2-11
usbipd attach -a --wsl --busid 2-11

# 3.3 inside WSL distro check ports attached
ls /dev/serial/by-id/*
# Example: /dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0

# 3.4 Write flash
esptool --port /dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0 --baud 115200 write_flash 0x100000 fs.bin

# 3.5 EXTRA Read and debug filesystem content
esptool --port /dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0 --baud 115200 read_flash 0x100000 1024000 /tmp/readback.bin
mklittlefs -l -p 256 -b 8192 -s 1024000 /tmp/readback.bin
```

### Serial monitor

From WSL:
```bash
PORT="/dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0"
python3 -m serial.tools.miniterm $PORT 115200

# OR using tio (install in wsl)
tio -b 115200 /dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0
```

### Device supported modes:
- CONNECTING
- AMBIENT
- FORECAST
- ERROR

Note: use Enum to define modes

Once turned ON device should be in CONNECTING mode which should do:
    - search for wifi connection SSID "multimedia_okrzei20" and hardcoded password in sketch.
    - LED strip is glowing with blue color with slowly modulated brightness up and down like led indication of bluetooth connection.
    - Once wifi succesfully connected mode switches to AMBIENT 
    - If wifi connection failed switch to ERROR mode

In AMBIENT mode:
    - led strip glowing with rainbow colors that a slowly floating and changing
    - connect to OpenWeatherMap API (`http://api.openweathermap.org/data/2.5/forecast`) using hardcoded API key; location from `WEATHER_LOCATION_LAT` / `WEATHER_LOCATION_LON`; use other `WEATHER_*` constants
    - save forecast info such as temperature, sky clearness, precipitation
    - Once have forecast data switch to FORECAST mode
    - If fetching forecast failed for 3 times switch back to ERROR mode

In FORECAST mode:
    - Weather condition classified from fetched data → dedicated `effect*()` function runs every loop (~60fps, non-blocking).
    - Each effect reflects real weather visually: clear sun = yellow shimmer, clouds = white/off shimmer, rain = blue LEDs replacing off slots, storm = rain + yellow splashes.
    - Each light effect is a separate function (see LED Light Effects section).
    - Supported weather conditions and their triggers:
        - CLEAR           - OWM id 800-801, wind ≤ 7 m/s
        - CLEAR_CLOUDY    - OWM id 802-803, wind ≤ 7 m/s
        - CLEAR_CLOUDY_WINDY - OWM id 802-803, wind > 7 m/s
        - CLOUDY          - OWM id 804, wind ≤ 7 m/s
        - CLOUDY_WINDY    - OWM id 804, wind > 7 m/s
        - RAIN            - OWM id 300-699, wind ≤ 10 m/s
        - RAIN_WINDY      - OWM id 300-699, wind > 10 m/s
        - STORMY          - OWM id 200-299 OR wind > 15 m/s
    - Rain level (for RAIN / RAIN_WINDY blue LED count):
        - Light  - OWM id 300-321 or 500-501: ~30% of off-slots turn blue
        - Moderate - OWM id 502, 520-521: ~60% of off-slots turn blue
        - Heavy  - OWM id 503-504, 522, 531: 100% of off-slots turn blue
    - Every 10 minutes: re-fetch weather without switching to AMBIENT, update condition and effect.
        - If re-fetch fails → switch to AMBIENT mode.

In ERROR mode:
    - Print to serial all helpful information about what was the error
    - LED strip is glowing with red color with slowly modulated brightness up and down.

## LED Light Effects

Each weather condition maps to a dedicated `effect*()` function. All effects run non-blocking (no delay inside), called every loop iteration (~60fps).

### Helper: `breathe(period_ms, minVal, maxVal, phaseMs)`
Sinusoidal brightness oscillator. `phaseMs` offsets per-LED phase for shimmer without sync.

### Mode effects
| Mode | Function | Description |
|---|---|---|
| CONNECTING | `effectConnecting()` | Solid blue, 2s breathe cycle |
| AMBIENT | `effectAmbient()` | Rainbow hues drifting across strip, hue offset driven by `millis()/60` |
| ERROR | `effectError()` | Solid red, 2s breathe cycle |

### Weather effects (FORECAST mode)
| Condition | Function | Visual |
|---|---|---|
| CLEAR | `effectClear()` | Golden-yellow (HSV hue ~38), per-LED shimmer with staggered phase, slow breathe cycle |
| CLEAR_CLOUDY | `effectClearCloudy()` | Per-LED randomly assigned yellow or white, slow staggered shimmer; yellow = primary, white = secondary |
| CLEAR_CLOUDY_WINDY | `effectClearCloudyWindy()` | Same as CLEAR_CLOUDY but faster shimmer cycle to convey wind movement |
| CLOUDY | `effectCloudy()` | Strip split ~50% white shimmer / 50% off, slow staggered breathe per LED |
| CLOUDY_WINDY | `effectCloudyWindy()` | Same as CLOUDY but faster breathe cycle |
| RAIN | `effectRain()` | Base is CLOUDY; off-slot LEDs randomly replaced with blue — count proportional to rain level (light ~30%, moderate ~60%, heavy 100%) |
| RAIN_WINDY | `effectRainWindy()` | Base is CLOUDY_WINDY; off-slot LEDs randomly replaced with blue — same rain-level proportion as RAIN |
| STORMY | `effectStormy()` | Base is RAIN_WINDY (heavy rain, all off-slots blue); random yellow-white quick splash on 1–3 LEDs every 0.5–2s |

### Weather classification thresholds
Evaluated top-to-bottom, first match wins.

- OWM id 200-299 OR id 771 OR id 781 OR wind > 15 m/s → STORMY
- OWM id 300-699, wind > 10 m/s → RAIN_WINDY
- OWM id 300-699, wind ≤ 10 m/s → RAIN
- OWM id 804 OR id 711/731/751/761/762, wind > 7 m/s → CLOUDY_WINDY
- OWM id 804 OR id 711/731/751/761/762, wind ≤ 7 m/s → CLOUDY
- OWM id 701/721/741 (mist/haze/fog) → CLOUDY (calm; no wind split)
- OWM id 802-803, wind > 7 m/s → CLEAR_CLOUDY_WINDY
- OWM id 802-803, wind ≤ 7 m/s → CLEAR_CLOUDY
- OWM id 800-801 → CLEAR
- Rain level (blue LED proportion in RAIN/RAIN_WINDY):
  - Light (id 300-321, 500-501): ~30% of off-slots blue
  - Moderate (id 502, 520-521): ~60% of off-slots blue
  - Heavy (id 503-504, 522, 531): 100% of off-slots blue


## Problems and solutions
Check @.specs/tasks/ folder that contains collection of files describing tasks or problems and Solutions that worked.