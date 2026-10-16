# Product Requirements — Forecaster

## Project

Arduino sketch (`forecaster.ino`) — weather forecasting device.
Components:
- ESP12F chip
- WS2812B addressable LED matrix, 4×4 (16 LEDs), driven as logical 2D canvas
- Tact switch button

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
`WiFiManager` (tzapu) used for captive-portal WiFi config — isolated in its own `.cpp`, see Architecture note in [CLAUDE.md](CLAUDE.md).

Weather / location constants (in sketch as `static const`):
```cpp
static const char* WEATHER_LOCATION_LAT  = "54.5189";
static const char* WEATHER_LOCATION_LON  = "18.5319";
static const char* WEATHER_LANGUAGE      = "en";
static const uint8_t WEATHER_MAX_FORECASTS = 1;
static const bool    WEATHER_IS_METRIC   = true;
```

OpenWeatherMap forecast URL uses lat/lon (`/data/2.5/forecast?lat=…&lon=…`), not city ID.

OWM API key: `OWM_API_KEY_DEFAULT` hardcoded fallback, overridable via WiFiManager captive-portal custom field and persisted to `/config.json` (`runtimeOwmApiKey`).

## Device functions

## Web Interface

### Stack
- `ESP8266AsyncWebServer` + `ESPAsyncTCP` — non-blocking HTTP server on port 80
- `LittleFS` — serves static files from `/data` folder
- `htmx` — bundled as `/htmx.js` (no CDN); handles `POST /mode` from mode buttons
- `app.js` — fetches JSON, renders HTML templates client-side, polls every 5s

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
Placeholders use `{{key}}` syntax. Button `*Class` values are derived in `app.js` from `mode`; weather placeholders come from the nested `weather` object.

**state.fragment.html**
| Placeholder | Source |
|---|---|
| `{{mode}}` | `mode` |
| `{{weatherSection}}` | Rendered weather fragment or empty (built client-side) |
| `{{lightClass}}` / `{{ambientClass}}` / `{{forecastClass}}` | `"active"` if `mode` matches that button, else `""` |

**weather.fragment.html** (only when `weather` is present)
| Placeholder | Source |
|---|---|
| `{{condition}}` | `weather.condition` |
| `{{temperature}}` | `weather.temperature` (°C, one decimal) |
| `{{wind}}` | `weather.wind` (m/s, one decimal) |
| `{{windDeg}}` | `weather.windDeg` |
| `{{windDir}}` | `weather.windDir` (N/NE/E/.../NW) |
| `{{rainSuffix}}` | `" · "` + `weather.rain`, or empty if `rain` is omitted |

### Server endpoints (JSON only — no HTML generated in firmware)
| Method | Path | Body | Response |
|---|---|---|---|
| GET | `/` | — | `index.html` from LittleFS |
| GET | `/state` | — | `application/json` — current device state |
| POST | `/mode` | `set=AMBIENT` (form field via htmx) | `application/json` — state after mode change |
| POST | `/reconfigure` | — | clears WiFi credentials (`wifiSetupResetCredentials()`), restarts device after 500ms, returns `{}` |

Example `GET /state` / `POST /mode` response (FORECAST with weather):
```json
{
  "mode": "FORECAST",
  "weather": {
    "condition": "Clear",
    "temperature": 12.5,
    "wind": 3.2,
    "windDeg": 180,
    "windDir": "S",
    "rain": "Light rain"
  }
}
```

`weather` is present whenever `weatherData.valid == true`, regardless of current mode. When there is no rain, `rain` is omitted. With no valid weather the object is only `{ "mode": "AMBIENT" }` (pretty-printed). Button CSS classes are not in JSON — `app.js` derives them from `mode`.

Firmware functions: `buildStateJson()`, `handleModePost()` — no `buildStateFragment()` or server-side HTML.

### Mode switch rules (server-side, via `POST /mode`)
- `CONNECTING` → allowed only if currently ERROR (reconnect attempt; calls `WiFi.reconnect()`)
- `AMBIENT` → always allowed
- `LIGHT` → always allowed
- `FORECAST` → allowed only if `weatherData.valid == true`
- `TESTING` → always allowed (resets testing cycle timer)
- `CONFIGURE` / `ERROR` → never settable via web

### Installable app (PWA)
`index.html` includes iOS/Android meta tags; `manifest.webmanifest` defines standalone display and icons.

- **iOS (Safari):** Share → Add to Home Screen
- **Android (Chrome):** Menu → Install app / Add to Home screen

Requires icon files in `/data` (`favicon.png`, `apple-touch-icon.png`). On local HTTP (`192.168.x.x`), iOS Add to Home Screen works; Android full install prompt may require HTTPS.

### index.html behavior
- Displays current device mode as active/highlighted button
- Shows buttons for: CONNECTING, AMBIENT, FORECAST, TESTING (CONFIGURE/ERROR buttons omitted — fault/setup states only)
- When in FORECAST with valid weather, shows condition, temperature, wind, wind direction, rain level
- Polls `GET /state` every 5s via `app.js` (not htmx polling)

### Device supported modes:
- CONFIGURE
- CONNECTING
- AMBIENT
- LIGHT
- FORECAST
- TESTING
- ERROR

Defined via `enum DeviceMode`.

## Boot / mode flow

On power-on, device starts in **CONFIGURE** mode and calls `wifiSetupBegin()` (WiFiManager captive portal), with a custom field for the OWM API key:

- If no saved WiFi credentials, or saved credentials fail, WiFiManager opens a captive-portal AP:
    - SSID: `CONFIG_AP_SSID` = `"forecaster_wifi"`, password `CONFIG_AP_PASSWORD` = `"Forec@ster"`
    - Portal timeout: `CONFIG_PORTAL_TIMEOUT_S` = 300s
    - LED effect during portal: `effectConfigure()` — same visual as `effectConnecting()` (solid blue, breathe)
    - API key entered in portal is persisted to `/config.json` (LittleFS) via `saveApiKeyConfig()`, device restarts after save (500ms deferred, so async HTTP response can flush)
- Once WiFiManager reports connected (`WIFI_SETUP_CONNECTED`), device starts web server (`setupWebServer()`), switches to **AMBIENT**, attempts `syncWeather()`, and if it succeeds switches to **FORECAST**.
- If WiFi connect attempt (post-portal, in `handleConnecting()` reconnect path) times out after `WIFI_TIMEOUT_MS` (15s), switch to **ERROR**.

API key loaded on boot from `/config.json` via `loadApiKeyConfig()`; falls back to hardcoded `OWM_API_KEY_DEFAULT` if file missing/invalid.

### CONNECTING mode
Reached only via web-triggered reconnect from ERROR (`POST /mode set=CONNECTING`), not on fresh boot (boot goes CONFIGURE → WiFiManager → AMBIENT/ERROR directly).
- LED: `effectConnecting()` — solid blue, breathe
- On `WiFi.status() == WL_CONNECTED`: start web server, switch to AMBIENT, attempt `syncWeather()`, switch to FORECAST on success
- On timeout (`WIFI_TIMEOUT_MS` = 15s): switch to ERROR

### AMBIENT mode
- LED: `effectAmbient()` — rainbow hues drifting across matrix, hue offset driven by `millis()/60`
- Weather re-fetch managed by `manageWeatherUpdates()` (runs in every mode except CONNECTING/ERROR): fetch interval is `WEATHER_INTERVAL_MS` (10 min) normally, or `FETCH_RETRY_DELAY_MS` (3s) after a failure
- On successful fetch, `applyWeather()` classifies condition and switches to FORECAST is NOT automatic from AMBIENT loop — initial AMBIENT→FORECAST transition only happens right after a mode entry (boot/connecting handlers); subsequent re-fetches from FORECAST mode just update condition/effect in place (see FORECAST mode)
- After `WEATHER_MAX_RETRIES` (3) consecutive fetch failures, switch to **ERROR**

### LIGHT mode
- LED: `effectLight()` — solid white (full brightness), diagnostic/utility mode, web-triggered only

### FORECAST mode
- Weather condition classified from fetched data (`classifyWeather()`) → dedicated `effect*()` function runs every loop (~60fps, non-blocking) via `handleForecast()`
- Each effect reflects real weather visually: clear sun = yellow shimmer, clouds = white/off shimmer, rain = blue cells replacing off cells, storm = rain + yellow splashes
- Each light effect is a separate function (see LED Light Effects section)
- `manageWeatherUpdates()` keeps re-fetching every `WEATHER_INTERVAL_MS` (10 min) while in FORECAST (and other non-CONNECTING/ERROR modes); on success, `applyWeather()` updates `weatherCond`/`rainLevel` in place (mode stays FORECAST); after `WEATHER_MAX_RETRIES` consecutive failures, switches to **ERROR** (not AMBIENT)
- Supported weather conditions and their triggers:
    - CLEAR           - OWM id 800-801, wind ≤ 7 m/s
    - CLEAR_CLOUDY    - OWM id 802-803, wind ≤ 7 m/s
    - CLEAR_CLOUDY_WINDY - OWM id 802-803, wind > 7 m/s
    - CLOUDY          - OWM id 804 or 701/721/741/711/731/751/761/762, wind ≤ 7 m/s
    - CLOUDY_WINDY    - OWM id 804 or 711/731/751/761/762, wind > 7 m/s
    - RAIN            - OWM id 300-699, wind ≤ 10 m/s
    - RAIN_WINDY      - OWM id 300-699, wind > 10 m/s
    - STORMY          - OWM id 200-299 or 771/781, OR wind > 15 m/s
- Rain level (`RainLevel` enum, drives blue-cell proportion in RAIN/RAIN_WINDY/STORMY):
    - Light  - OWM id 300-321 or 500-501: ~30% of off-cells turn blue
    - Moderate - OWM id 502, 520-521: ~60% of off-cells turn blue
    - Heavy  - OWM id 503-504, 522, 531, or any STORMY classification: 100% of off-cells turn blue

### TESTING mode
- Web-triggered diagnostic mode; cycles through weather effects every 30s regardless of actual weather (`handleTesting()`), stepping `testingStep` 0..4 (CLEAR → CLEAR_CLOUDY → CLEAR_CLOUDY_WINDY → CLOUDY → CLOUDY_WINDY; cases 5-7 for RAIN/RAIN_WINDY/STORMY exist in switch but step cycles mod 5 so unreachable currently)

### ERROR mode
- Reached from: WiFi connect timeout, or `WEATHER_MAX_RETRIES` consecutive weather-fetch failures
- LED: `effectError()` — solid red, 2s breathe cycle
- Exit only via web `POST /mode set=CONNECTING`, which calls `WiFi.reconnect()` and switches to CONNECTING

## LED Light Effects

Each weather condition maps to a dedicated `effect*()` function. All effects run non-blocking (no delay inside), called every loop iteration (~60fps). Weather effects paint a `logicalCanvas[MATRIX_WIDTH][MATRIX_HEIGHT]` which `renderDisplayDriver()` scrolls/blends and maps onto the physical 4×4 WS2812B matrix via `XY()` (serpentine column layout). Mode effects (connecting/ambient/light/error) paint `leds[]` directly.

### Helper: `breathe(period_ms, minVal, maxVal, phaseMs)`
Sinusoidal brightness oscillator. `phaseMs` offsets per-LED phase for shimmer without sync.

### Helper: `renderDisplayDriver(windy)`
Smooth horizontal sub-pixel scroll of `logicalCanvas` onto `leds[]`, with per-pixel linear blend between adjacent columns. Scroll period 6000ms normally, 2000ms when `windy`.

### Mode effects
| Mode | Function | Description |
|---|---|---|
| CONFIGURE | `effectConfigure()` | Solid blue, 4s breathe cycle (same as CONNECTING) |
| CONNECTING | `effectConnecting()` | Solid blue, 4s breathe cycle |
| AMBIENT | `effectAmbient()` | Rainbow hues drifting across matrix, hue offset driven by `millis()/60` |
| LIGHT | `effectLight()` | Solid white, full brightness |
| ERROR | `effectError()` | Solid red, 2s breathe cycle |

### Weather effects (FORECAST / TESTING modes)
| Condition | Function | Visual |
|---|---|---|
| CLEAR | `effectClear()` | Golden-yellow (HSV hue ~38), per-cell shimmer with staggered phase, 2.5s breathe cycle |
| CLEAR_CLOUDY | `effectClearCloudy()` | Per-cell randomly assigned yellow (`ledYellow[]`) or off, 3s staggered shimmer |
| CLEAR_CLOUDY_WINDY | `effectClearCloudyWindy()` | Same as CLEAR_CLOUDY but 1.5s shimmer cycle + windy scroll, to convey wind movement |
| CLOUDY | `effectCloudy()` | Cells where `ledCloud[]` true show white shimmer (3.5s breathe), rest off |
| CLOUDY_WINDY | `effectCloudyWindy()` | Same as CLOUDY but 1.6s breathe cycle + windy scroll |
| RAIN | `effectRain()` | Base is CLOUDY; non-cloud cells where `ledBlue[]` true show blue shimmer (proportional to rain level) |
| RAIN_WINDY | `effectRainWindy()` | Base is CLOUDY_WINDY; non-cloud cells where `ledBlue[]` true show blue shimmer — same rain-level proportion as RAIN |
| STORMY | `effectStormy()` | All non-cloud cells blue (heavy rain), cloud cells white; random yellow splash (`CRGB(255,220,0)`) on 1–3 physical LEDs for 40-150ms, every 0.5–2s |

LED role assignment: `initLedRoles()` randomizes `ledYellow[]` (2/3 chance) and `ledCloud[]` (1/2 chance) per cell on boot and whenever weather changes (`applyWeather()`). `initRainBlue()` randomizes `ledBlue[]` on non-cloud cells based on current rain level's blue-chance percentage.

### Weather classification thresholds
Evaluated top-to-bottom in `classifyWeather()`, first match wins:

- OWM id 200-299 OR id 771 OR id 781 OR wind > 15 m/s → STORMY (forces rain level HEAVY)
- OWM id 300-699 → RAIN_WINDY if wind > 10 m/s, else RAIN
- OWM id 804 OR id 711/731/751/761/762 → CLOUDY_WINDY if wind > 7 m/s, else CLOUDY
- OWM id 701/721/741 (mist/haze/fog) → CLOUDY (calm; no wind split)
- OWM id 802-803 → CLEAR_CLOUDY_WINDY if wind > 7 m/s, else CLEAR_CLOUDY
- else (OWM id 800-801 etc.) → CLEAR

Rain level computed separately, before the id/wind branch above (can be overridden to HEAVY by STORMY match):
- Heavy (id 503-504, 522, 531): 100% of off-cells blue
- Moderate (id 502, 520-521): ~60% of off-cells blue
- else: Light — ~30% of off-cells blue
