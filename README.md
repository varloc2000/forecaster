# Forecaster

A 4×4 LED weather lamp. It paints the sky — sun, cloud, rain, wind, storm — or works as a plain lamp, a rainbow, or a demo of those looks.

Hardware is an ESP12F plus a WS2812B matrix. Flash it with [TECH.md](TECH.md). This page is **full technical documentation with project requirements**.

## Modes

One mode, one picture. Weather showed as live animation in **Forecast** mode.
Other modes control the light effect independent from weather conditions.

| Mode | What you see |
|---|---|
| **Light** | Steady white. A lamp, and a way to see every LED is on. |
| **Ambient** | Slow rainbow. Ignores the sky; still downloads weather in the background. |
| **Forecast** | Live sky. Only after a successful fetch. |
| **Demo** (in **i**) | Weather pictures, 30 s each: clear → partly cloudy → windy sun gaps → cloudy → windy cloudy, then loop. Rain and storm are Forecast-only. |

## Getting Started or Reconfigure:

- **Blue pulse light** — on first boot (Also after **Reconfigure**). Join `forecaster_wifi` and pick your home network. Connecting looks the same: waiting on Wi‑Fi.
- **Red pulse light** — Wi‑Fi gone, or weather fetch failed three times. Fix the network and retry, or power-cycle.

## Forecast animations

Viasual effects on LED matrix try to simulate sky look depending on weather — light **shimmers** (cells breathe out of phase) and **scrolls** sideways to visualise winds.

| Sky | Lights |
|---|---|
| **Clear** | Warm gold, every cell. |
| **Partly cloudy** | Gold with random dark gaps (clouds). If Windy: faster sideways movement and pulse. |
| **Cloudy** | Soft white patches, rest off. If Windy: same, but quicker. |
| **Rain** | Cloudy effect, plus blue in the gaps — ~30% / ~60% / all, for light / moderate / heavy. If Windy: fast sideways movement and pulse. |
| **Storm** | Heavy rain, windy motion, plus short yellow flashes (1–3 LEDs) every 0.5–2 s. |

## How the sky is chosen

The lamp asks OpenWeatherMap every ten minutes (default: Gdynia / Gdańsk Bay). It picks a picture from the forecast, then may speed it up if the wind is high:

| What’s going on outside | What you see |
|---|---|
| Thunder, a squall, a tornado, or wind over 15 m/s | **Storm** (always drawn as heavy rain) |
| Rain or drizzle | **Rain** — windy look if wind is over 10 m/s |
| Overcast, or dusty / smoky air | **Cloudy** — windy look if wind is over 7 m/s |
| Fog, mist, or haze | **Cloudy**, always calm (no windy look) |
| Broken or scattered cloud | **Partly cloudy** — windy look if wind is over 7 m/s |
| Mostly sun | **Clear** |

A bright but very windy day can still look like a storm: the lamp cares about how it *feels*, not only the icon name. How much blue is in rain comes from how hard it is raining; storm is always full blue.

---

Exact ids, pinout, and HTTP: [PRD.md](PRD.md). Build: [TECH.md](TECH.md).
