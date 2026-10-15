# TRD: Cleanup / UI Improvements

Ref task: `../tasks/03-cleanup-ui-improvements.md`

## 1. Goal

Remove two buttons from the mode UI (`CONNECTING`, `TESTING`) without removing the underlying mode functionality, add an info modal (about + reconfigure + demo-all-modes + close), drop dead `configureClass` JSON field, and surface wind direction (arrow + compass letter) in the weather display.

## 2. Button removal

`data/state.fragment.html`:
- Remove `CONNECTING` button. Server-side rule stays untouched (`/mode` still accepts `set=CONNECTING` only when `currentMode == MODE_ERROR` — `forecaster.ino:245`); there is simply no UI control for it anymore. Not otherwise reachable from the web UI (ERROR mode has no other buttons either), so reconnect-after-error becomes a device-side-only recovery until it self-corrects or is manually rebooted. Accepted — task explicitly says "can't imagine how it can be useful."
- Remove `TESTING` button. `MODE_TESTING` stays fully functional (`handleTesting()`, `handleModePost()` branch for `val == "TESTING"` unchanged) — now triggered only from the info modal's "Demo all weather light modes" action (§3). Exit path: user clicks `AMBIENT` or `FORECAST` (still-visible buttons) to leave demo — no dead end.
- `AMBIENT` and `FORECAST` buttons unchanged.

`forecaster.ino` `buildStateJson()` (line ~205): drop the `testingClass`/`connectingClass` unused-by-new-UI... **decision:** keep emitting `testingClass`/`connectingClass` in JSON (harmless, low risk, no template references them anymore) — only remove `configureClass` per requirement 4, since that one was explicitly flagged as dead. Rationale: minimize firmware churn; template simply won't consume the other two.

## 3. Info modal

### Placement
Modal markup + trigger button live in `data/index.html` directly (static, outside `#state`), **not** in a templated fragment — `app.js:42` does `el.outerHTML = html` on `#state` every 5s poll and after every `/mode` POST, which would destroy modal DOM/state if it lived inside `#state`. Modal is pure static client-side UI, not driven by `/state` JSON, so this doesn't violate the "firmware serves JSON only" rule (§ Architecture in `CLAUDE.md`) — no firmware-rendered HTML involved.

### Markup (`data/index.html`)
```html
<button id="info-btn" type="button" aria-label="About">i</button>

<div id="info-modal" class="modal-overlay" hidden>
  <div class="modal-box">
    <h2>Forecaster v1.0</h2>
    <p>Author: Tim Cherniavski<br>varloc2000@gmail.com</p>
    <button type="button" id="reconfigure-btn">Reconfigure</button>
    <button type="button" id="demo-btn">Demo all weather light modes</button>
    <button type="button" id="modal-close-btn">Close</button>
  </div>
</div>
```

### CSS (`data/main.css`)
- `#info-btn`: `position: fixed; top: max(0.75rem, env(safe-area-inset-top)); right: max(0.75rem, env(safe-area-inset-right)); width: 2rem; height: 2rem; border-radius: 50%;` centered "i" glyph — `position: fixed` (not `absolute`) so it stays put regardless of body scroll/flex layout, and reads the same safe-area inset the body already applies (`main.css:14`) so it doesn't clip under a notch.
- `.modal-overlay`: fixed, full-viewport, `display:flex; align-items:center; justify-content:center; background: rgba(0,0,0,0.6)`; toggled via `[hidden]` attribute, not a class — simplest, no extra JS state to track.
- `.modal-box`: dark card matching existing palette (`#1c1c2e` bg, `#333` border), buttons reuse existing `button` style already in `main.css:20`.

### Behavior (`data/app.js`)
Plain event listeners, no htmx needed for open/close (htmx stays scoped to `/mode` POSTs per existing convention):
- `#info-btn` click → un-hide `#info-modal`.
- `#modal-close-btn` click → hide `#info-modal`.
- `#demo-btn` click → `fetch("/mode", {method:"POST", headers:{...}, body:"set=TESTING"})`, then hide modal, then `renderState()` the JSON response (same pattern already used for htmx `/mode` responses — reuse `renderState`).
- `#reconfigure-btn` click → `if (!confirm("Device will switch to setup mode. Reconnect your phone/PC to the 'forecaster_wifi' network to reconfigure.")) return;` then `fetch("/reconfigure", {method:"POST"})`, hide modal. No response body needed (device restarts into portal — page will lose connection, that's expected).
  - Native `confirm()` chosen over a second custom modal — task literally says "show dialog", lowest-risk match, no extra markup/state.

## 4. New endpoint: `POST /reconfigure`

Distinct from `/mode` — it must reset WiFi credentials and restart, not just switch `currentMode`. Reuses the existing "respond then delayed restart" pattern already used after the WiFiManager portal save (`forecaster.ino:776-777`, `restartPending`/`restartAtMs`).

`wifi_setup.h` / `wifi_setup.cpp` (kept isolated per the existing `ESPAsyncWebServer`/`WiFiManager` translation-unit split — see file header comment):
```cpp
// wifi_setup.h — add:
void wifiSetupResetCredentials(); // wm.resetSettings() — clears saved STA creds only

// wifi_setup.cpp — add:
void wifiSetupResetCredentials() {
    wm.resetSettings();
}
```

`forecaster.ino` `setupWebServer()` — add:
```cpp
webServer.on("/reconfigure", HTTP_POST, [](AsyncWebServerRequest* req) {
    Serial.println("[Web] Reconfigure requested — clearing WiFi credentials");
    wifiSetupResetCredentials();
    req->send(200, "application/json", "{}");
    restartPending = true;
    restartAtMs = millis() + 500;
});
```
On reboot, `wifiSetupBegin()` runs at `setup()` (line 756) as normal; with credentials cleared, `wm.autoConnect()` finds nothing saved and opens the config portal immediately → `currentMode` becomes `MODE_CONFIGURE` via the existing boot-time branch (`forecaster.ino:765-768`). No new mode-transition logic needed — this is the same path a factory-fresh device takes.

The OWM API key in `/config.json` is **not** cleared — task only asks to reset WiFi credentials, and the key is unrelated to network identity. `clearApiKeyConfig()` stays unused by this feature (still exists for other callers, if any — confirmed only currently unused/orphaned; leaving as-is, out of scope to remove).

## 5. Wind direction

### Firmware (`forecaster.ino`)
`WeatherData` struct (line ~72) — add field:
```cpp
struct WeatherData {
    int   weatherId;
    float temperature;
    float windSpeed;
    int   windDeg;      // new — meteorological degrees, 0-359, 0/360 = North
    float rain3h;
    bool  valid;
};
```
Parse alongside existing fields (line ~604):
```cpp
weatherData.windDeg = entry["wind"]["deg"].as<int>();
```
(OWM `/data/2.5/forecast` nests direction under the same per-entry `wind` object as `speed` — doc calls the field `wind_deg`, actual JSON path is `wind.deg`, consistent with the existing `windSpeed` parse one line above.)

Compass-letter lookup — new helper, same file, near `conditionToString()`:
```cpp
const char* windDirLabel(int deg) {
    static const char* DIRS[8] = {"N","NE","E","SE","S","SW","W","NW"};
    int idx = ((deg % 360) + 360) % 360;       // normalize
    idx = ((idx + 22) / 45) % 8;               // 45° buckets, centered on each compass point
    return DIRS[idx];
}
```
Computed server-side, matching the existing pattern where firmware already classifies (`classifyWeather()`, `rainLevelLabel()`) rather than pushing raw enums to the browser.

`buildStateJson()` (line ~226, inside the `showWeather` block) — add two fields:
```cpp
json += F("\",\"windDeg\":\"");
json += String(weatherData.windDeg);
json += F("\",\"windDir\":\"");
json += windDirLabel(weatherData.windDeg);
json += F("\"");
```

### Frontend (`data/weather.fragment.html`)
```html
<div class="weather-details">
  <span class="wind-arrow" style="display:inline-block; transform: rotate({{windDeg}}deg);">&#8593;</span>
  {{windDir}} · {{wind}} m/s{{rainSuffix}}
</div>
```
Template regex in `app.js:10` (`/\{\{(\w+)\}\}/g`) handles the new placeholders with no changes — plain string substitution, values are firmware-controlled (numeric degree, fixed 2-letter/1-letter enum strings), no injection risk.

## 6. Files touched

- `forecaster.ino`: `WeatherData.windDeg` field, wind-deg parse, `windDirLabel()` helper, `buildStateJson()` new fields + `configureClass` removal, `/reconfigure` route in `setupWebServer()`.
- `wifi_setup.h` / `wifi_setup.cpp`: add `wifiSetupResetCredentials()`.
- `data/state.fragment.html`: remove `CONNECTING`/`TESTING` buttons.
- `data/index.html`: add info button + modal markup.
- `data/main.css`: `#info-btn`, `.modal-overlay`, `.modal-box` styles.
- `data/app.js`: modal open/close/demo/reconfigure listeners.
- `data/weather.fragment.html`: wind arrow + direction letter.
- LittleFS image (`fs.bin`) must be rebuilt and reflashed after `/data` changes (per `CLAUDE.md` build/flash guide) — firmware-only changes need a normal sketch reflash.

## 7. Edge cases / open questions

- **Known pre-existing bug, out of scope:** `handleTesting()` (`forecaster.ino:687`) cycles `testingStep = (testingStep + 1) % 5`, but the `switch` below it has cases `0`-`7` — cases `5`,`6`,`7` (`effectRain`, `effectRainWindy`, `effectStormy`) are unreachable, demo only ever cycles the first 5 effects. Not touched by this task; flagging since the modal now becomes the only way to trigger a demo. Fix separately if wanted.
- **ERROR-mode reconnect** now has no UI trigger at all (button removed, no other UI path invokes `set=CONNECTING`). Device still self-recovers via its own retry/timeout logic elsewhere; confirmed acceptable per task wording.
- **Compass rounding:** `windDirLabel()` buckets are 45° wide centered on each point (e.g. `N` = 338°-22°). Standard convention, no alternative needed unless you want finer resolution (16-point compass) — task explicitly lists 8 labels, so 8 buckets it is.
