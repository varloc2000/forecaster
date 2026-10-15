# TRD: Web UI Initial Configuration

Ref task: `../tasks/02-web-ui-initial-configuration.md`

Rev 2 — switched from hand-rolled AP+mDNS+form to **WiFiManager** (tzapu) captive-portal library. Rev 1 (custom implementation) superseded; kept no history here, git has it.

Rev 3 — tzapu's `WiFiManager` is **not usable alongside `ESPAsyncWebServer`**: it internally includes `ESP8266WebServer.h`, whose global `HTTP_GET`/`HTTP_OPTIONS`/etc. enum collides symbol-for-symbol with `ESPAsyncWebServer`'s own global enum of the same names — hard compile error (`redeclared` / `previous declaration`), not a runtime issue. This was missed in rev 2 and only surfaced when the sketch was actually compiled. Switched to **ESPAsyncWiFiManager** (by alanswx) instead — built directly on `ESPAsyncWebServer` + `DNSServer`, no `ESP8266WebServer` dependency, no clash. See §2/§4 for the updated design.

## 1. Goal

Device without saved WiFi/API config must let user configure it via its own AP + web form, persist config, then reboot into normal flow. (Factory Reset — originally in scope — removed per §8.)

## 2. Library dependency

Add **WiFiManager** by tzapu (install via Arduino Library Manager: "WiFiManager"). It brings its own `ESP8266WebServer` + `DNSServer` internally — these run only during the CONFIGURE portal, on a separate blocking sync server instance, never concurrently with our `AsyncWebServer` (`webServer`) used for normal operation. Mutually exclusive lifetimes, same reasoning as before: CONFIGURE phase and normal phase never run in the same boot.

No `ESP8266mDNS` needed. WiFiManager's captive portal `DNSServer` answers every DNS query (wildcard) with the AP's IP while the portal is open — `http://forecaster.local/` (and literally any other hostname) resolves to the portal automatically during that window. Dropped from design.

## 3. Mode: CONFIGURE

Keep `MODE_CONFIGURE` in `DeviceMode` enum — used only to drive LED effect (`effectConfigure()`, purple/magenta breathe, same pattern as `effectConnecting`/`effectError`) and to report state via `/state` JSON. WiFiManager itself owns the actual connect-vs-portal decision; our enum just mirrors it for LEDs/UI.

## 4. Boot / connect flow

Replace old manual boot branch (`loadConfig()` → CONFIGURE or CONNECTING) with WiFiManager non-blocking flow.

**Lifetime hazard, must design around it:** with `setConfigPortalBlocking(false)`, `wm` keeps running across `loop()` iterations long after `setup()` returns — it holds raw pointers into every `WiFiManagerParameter` passed to `addParameter()`. If those parameters are `setup()`-local stack variables, they're destroyed the moment `setup()` returns; `wm.process()` then dereferences dangling pointers on the next tick → exception/watchdog reset. `WiFiManager wm` itself has the same problem if declared local to `setup()`.

Fix: `wm` and every `WiFiManagerParameter` passed to it must outlive `setup()`. Declare `wm` as a file-scope global; heap-allocate the parameter objects inside `setup()` (via `new`, intentionally never `delete`d — they must live for the whole process lifetime, and `setup()` runs once) and hold them via global pointers:

```cpp
WiFiManager wm;                                     // global — must outlive setup()
WiFiManagerParameter* apiKeyParam;                  // heap-allocated in setup(), lives forever
WiFiManagerParameter* linkHtml;
bool wifiManagerDone = false;

void setup() {
    ...
    loadApiKeyConfig();                             // reads /config.json → runtimeOwmApiKey (falls back to hardcoded default)

    apiKeyParam = new WiFiManagerParameter("apiKey", "openweathermap.org API key", runtimeOwmApiKey.c_str(), 40, "type='password'");
    linkHtml = new WiFiManagerParameter("<a href='https://openweathermap.org/' target='_blank'>Get / verify API key</a>");
    wm.addParameter(apiKeyParam);
    wm.addParameter(linkHtml);
    wm.setTitle("Forecaster Light Configuration");
    wm.setSaveConfigCallback(onWifiManagerSaved);   // fires after portal saves new WiFi + params
    wm.setConfigPortalBlocking(false);              // non-blocking so LED loop keeps running (~60fps)
    wm.setConfigPortalTimeout(300);                 // 5 min; on timeout wm returns, we ESP.restart() to retry
    wm.autoConnect("forecaster_wifi", "Forec@ster"); // tries saved creds first, opens portal only if needed
}

void loop() {
    if (!wifiManagerDone) {
        wm.process();
        currentMode = wm.getConfigPortalActive() ? MODE_CONFIGURE : MODE_CONNECTING;
        (currentMode == MODE_CONFIGURE) ? effectConfigure() : effectConnecting();

        if (WiFi.status() == WL_CONNECTED) {
            wifiManagerDone = true;
            setupWebServer();
            setMode(MODE_AMBIENT);
            if (syncWeather()) setMode(MODE_FORECAST);
        } else if (!wm.getConfigPortalActive() && millis() - wmStartMs > WIFI_TIMEOUT_MS) {
            ESP.restart(); // portal timed out with no save, or connect attempt stalled — retry from scratch
        }
        delay(16);
        return;
    }
    // existing switch(currentMode) loop body unchanged from here down
    ...
}
```

WiFi credentials themselves are **not** stored in our own file — WiFiManager relies on the ESP8266 SDK's own flash-cached STA credentials (`WiFi.begin` persistence), same mechanism the chip always used. Our `/config.json` only needs to hold the OWM API key.

`onWifiManagerSaved()` callback: read `apiKeyParam->getValue()`, write `/config.json` (`{"owmApiKey": "..."}`), then set the restart-pending flag (§7) — do not call `ESP.restart()` synchronously inside the callback; let WiFiManager finish responding to the portal's browser POST first.

## 5. Storage (`/config.json`, LittleFS, runtime-written)

```json
{ "owmApiKey": "11f0d20175f478a5b78664b124859ad9" }
```
- `loadApiKeyConfig()` — read+parse; on missing/invalid, `runtimeOwmApiKey` = hardcoded default constant (still kept as fallback literal in sketch).
- `saveApiKeyConfig(key)` — serialize+write.
- `clearApiKeyConfig()` — `LittleFS.remove("/config.json")`.

`runtimeOwmApiKey` becomes a mutable `String`, replacing the old hardcoded `OWM_API_KEY` at the call site in `fetchWeather()`.

## 6. Configuration page — architecture exception

WiFiManager renders the portal HTML itself, server-side, from PROGMEM strings inside the library, served by its own internal `ESP8266WebServer` — **not** from LittleFS `/data`, and not JSON. This is a deliberate, scoped exception to the project's "firmware serves JSON only / HTML lives in `/data`" rule, limited strictly to the CONFIGURE-mode captive portal. Normal operation (`index.html`, `app.js`, `/state`, `/mode`) is untouched and still JSON-only, served by `AsyncWebServer` as before.

Trade-off accepted for using an established library instead of hand-rolled AP/DNS/form code (less code to maintain, well-tested captive-portal behavior — auto-popup "sign in to network" prompts on phones — at the cost of losing full control over exact page markup).

Known divergence from task's literal field wording: WiFiManager's built-in portal already provides WiFi SSID (with network scan/picker) + WiFi Password fields, labeled "SSID"/"Password" by default rather than task's "Home WIFI"/"WIFI Password". Relabeling requires injecting custom head CSS/JS (`wm.setCustomHeadElement`) to rewrite label text at render time. Flagged as open decision — see §9.

No separate `configure.html` file needed in `/data`. Drop that file from scope entirely.

## 7. Restart-after-response pattern

The WiFiManager save callback must let the portal's HTTP response flush before rebooting.
```cpp
bool restartPending = false;
uint32_t restartAtMs = 0;
```
Set `restartPending=true; restartAtMs = millis()+500;` in `onWifiManagerSaved()`; checked once per `loop()` tick: `if (restartPending && millis() >= restartAtMs) ESP.restart();`.

## 8. Factory Reset — removed from scope

Factory Reset (button, confirmation dialog, `/factory-reset` route, `clearApiKeyConfig()`) has been dropped entirely per explicit decision. No user-facing way to clear WiFi creds or the API key from the normal web UI. If WiFi/API config ever needs clearing, that's a manual action outside app scope (e.g. reflashing, or a future task if reinstated).

## 9. Files touched

- `forecaster.ino`: add `WiFiManager` include/instance, `effectConfigure()`, `loadApiKeyConfig()`/`saveApiKeyConfig()`, `onWifiManagerSaved()` callback, restart-pending flag, reworked `setup()`/`loop()` boot flow, `runtimeOwmApiKey` replacing hardcoded `OWM_API_KEY` at usage site.
- `data/index.html` / `data/main.css` / `data/app.js`: unchanged by this task (no Factory Reset UI).
- No new `/data/configure.html` (superseded by §6).
- LittleFS image (`fs.bin`) reflash not required for this task's firmware-only changes, but rebuild if `/data` contents changed for other reasons.

## 10. Edge cases / open questions

- **No in-app way to clear WiFi creds.** WiFiManager's SDK-stored STA credentials persist across reboots and across `/config.json` deletion. Without Factory Reset, the only way back into CONFIGURE mode once WiFi is saved is erasing flash/creds outside the app (e.g. `esptool erase_flash`, or reflashing). Accepted trade-off per decision above.
- **Label wording** ("Home WIFI"/"WIFI Password" vs WiFiManager defaults "SSID"/"Password") — decide: accept default wording, or add `setCustomHeadElement` CSS/JS relabel hack. Recommend accepting default wording unless user pushes back — lower risk, less fragile than DOM-rewriting injected JS against a library-owned page.
  Decision: accept default wording, no need to relabel.
- **Portal timeout** (300s chosen) — after timeout with no save, sketch restarts and retries `autoConnect()` from scratch (opens portal again). Confirm 5 min is acceptable, or make it longer/indefinite.
  Decision: timeout is ok.
