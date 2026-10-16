# Build and Flash Guide — Forecaster (PlatformIO)

Config: `platformio.ini` (env `esp12f`, espressif8266, littlefs, lib_deps for FastLED/ArduinoJson/ESPAsyncWebServer/WiFiManager).

## Step 1 — Create venv and install PlatformIO

```bash
python3 -m venv forecaster
source forecaster/bin/activate
pip install -r requirements.txt
```

## Step 2 — Build filesystem image (from /data)

```bash
pio run -t buildfs
```

## Step 3 — Build firmware

```bash
pio run
```

## Step 4 — Forward USB port into WSL

```bash
# On Windows: list ports
usbipd list
# Look for CP2102 USB to UART Bridge Controller, note busid (e.g. 2-11)

# Bind and attach
usbipd bind --busid 2-11
usbipd attach -a --wsl --busid 2-11

# Inside WSL: confirm port attached
ls /dev/serial/by-id/*
# Example: /dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0
```

Must match `upload_port` in `platformio.ini`.

## Step 5 — Flash (inside WSL)

```bash
source forecaster/bin/activate

# Flash filesystem
pio run -t uploadfs

# Flash firmware
pio run -t upload
```

## Serial monitor

```bash
source forecaster/bin/activate
pio device monitor
```
