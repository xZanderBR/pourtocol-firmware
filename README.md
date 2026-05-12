# Pourtocol Firmware

ESP32 firmware for [Pourtocol](https://github.com/xZanderBR/Pourtocol) — a networked drink dispenser. Handles pump control, ultrasonic glass detection, and NFC-based user identification, exposing a small HTTP API consumed by the Flask coordinator.

## Hardware

| Component | Purpose | Pins |
| :--- | :--- | :--- |
| **ESP32 DevKit** | Main controller | — |
| **5V relay module** | Switches submersible pump (active HIGH) | `GPIO15` |
| **HC-SR04** | Ultrasonic distance sensor for glass detection | `TRIG=GPIO5`, `ECHO=GPIO18` |
| **PN532 (I2C)** | NFC reader for user tap-to-identify | `SDA=GPIO21`, `SCL=GPIO22` |

> The HC-SR04 echo pin runs at 5V. Use a level shifter (or resistor divider) to step down to 3.3V before driving the ESP32 input.

> Set PN532 DIP switches to `SEL0=ON`, `SEL1=OFF` for I2C mode.

## HTTP API

The firmware runs an async HTTP server on port 80 and advertises itself via mDNS at `http://esp32.local`.

### `GET /status`
Returns the current machine state:

```json
{
  "state": "idle",          // "idle" | "pouring"
  "glass_present": true,
  "uptime": 1234,           // seconds since boot
  "last_pour_ml": 30,
  "nfc_uid": "04A1B2C3...", // empty when no tag is present
  "nfc_tag_present": true,
  "nfc_ready": true
}
```

### `POST /dispense`
Starts a pour. Rejected if a pour is already in progress, the amount is out of range, or no glass is detected.

```json
// Request
{ "amount_ml": 30, "request_id": "req_1234567890" }

// Responses
200 { "ok": true }
400 { "error": "Invalid amount" }
409 { "error": "Already pouring" }
409 { "error": "No glass present" }
```

## Behaviour

- **Non-blocking pour** — pump duration is computed from `FLOW_RATE_ML_PER_S` and tracked via `millis()` so the HTTP server stays responsive during dispensing.
- **Debounced glass detection** — requires `GLASS_DEBOUNCE_HITS` consecutive in-range readings to filter spurious ultrasonic echoes.
- **NFC tag linger** — a scanned tag's UID stays "active" for `NFC_TAG_LINGER_MS` so the frontend has time to pick it up after a tap.
- **Boot-safe relay** — the pump pin is driven `LOW` before being set to `OUTPUT`, preventing a brief pour on power-on.
- **WiFi modem sleep disabled** — keeps the radio responsive (no multi-second latency from DTIM beacon intervals).
- **Status cache (server-side)** — concurrent `/status` polls are coalesced upstream in the Flask layer; this firmware can be polled aggressively without harm.

## Build & Flash

The sketch lives in `pourtocol/` and is buildable from both **PlatformIO** and the **Arduino IDE**.

### 1. Configure
```bash
cp pourtocol/config.h.example pourtocol/config.h
# Edit pourtocol/config.h and set WIFI_SSID / WIFI_PASSWORD.
```
The real `config.h` is gitignored. Never commit credentials.

### 2. PlatformIO (recommended)
```bash
pio run                 # build
pio run --target upload # flash
pio device monitor      # serial monitor @ 115200 baud
```

### 3. Arduino IDE
Open `pourtocol/pourtocol.ino`. Install the libraries listed under "Dependencies" via Library Manager, select your ESP32 board, then Upload.

## Dependencies

| Library | Version | Purpose |
| :--- | :--- | :--- |
| `mathieucarbou/ESP Async WebServer` | `^3.0.6` | Non-blocking HTTP server |
| `bblanchon/ArduinoJson` | `^7.0.0` | JSON parsing / serialization |
| `adafruit/Adafruit PN532` | `^1.3.4` | PN532 NFC reader driver |

PlatformIO resolves these automatically from `platformio.ini`.

## Configuration Reference

All tunables live in `pourtocol/config.h`:

| Define | Default | Description |
| :--- | :--- | :--- |
| `MDNS_HOSTNAME` | `"esp32"` | Device advertised at `http://<hostname>.local` |
| `GLASS_THRESHOLD_CM` | `6.0f` | Distance below which a glass is "present" |
| `GLASS_DEBOUNCE_HITS` | `2` | Consecutive in-range reads required |
| `DISTANCE_INTERVAL_MS` | `150` | HC-SR04 sample period |
| `NFC_POLL_INTERVAL_MS` | `100` | PN532 polling period |
| `NFC_TAG_LINGER_MS` | `10000` | How long a UID stays active after the tag leaves |
| `MAX_DISPENSE_ML` | `60` | Hard upper bound on pour size |
| `FLOW_RATE_ML_PER_S` | `10.0f` | Pump flow rate — calibrate after assembly |

## Project Layout

```text
Pourtocol-Firmware/
├── pourtocol/
│   ├── pourtocol.ino       # Main sketch — WiFi, HTTP server, state machine
│   ├── config.h.example    # Template (committed)
│   └── config.h            # Local credentials (gitignored)
├── Adrian_Firmware/        # Hardware bring-up sketches (pump, PN532)
├── platformio.ini          # PlatformIO build config
└── README.md
```
