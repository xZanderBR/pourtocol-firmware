#pragma once

// ─── WiFi ────────────────────────────────────────────────────────────────────
#define WIFI_SSID "TeaOn63rd"
#define WIFI_PASSWORD "Shaker123"

// mDNS hostname — device will be reachable at http://esp32.local
// Must match ESP32_URL in backend/config.py (http://esp32.local)
#define MDNS_HOSTNAME "esp32"

// ─── Hardware pins (Adrian rev) ──────────────────────────────────────────────
// HC-SR04 ultrasonic sensor (glass detection)
#define PIN_TRIG 5           // Trigger pin (output)
#define PIN_ECHO 18          // Echo pin (input) — needs 5V→3.3V level shifter!

// Pump relay (active HIGH)
#define PIN_PUMP 15

// PN532 NFC reader — I2C mode (set DIP: SEL0=ON, SEL1=OFF)
// SDA = GPIO21, SCL = GPIO22 (ESP32 default Wire pins).
// IRQ/RESET unused on the breakout when running over I2C.
#define PIN_NFC_IRQ   (-1)
#define PIN_NFC_RESET (-1)

// ─── NFC behaviour ───────────────────────────────────────────────────────────
// How long (ms) a scanned tag UID stays "active" before clearing.
// Gives the user time to press Dispense on the frontend after tapping.
#define NFC_TAG_LINGER_MS 10000

// How often (ms) the firmware polls the PN532 for a tag
#define NFC_POLL_INTERVAL_MS 100

// ─── Glass detection (debounced) ─────────────────────────────────────────────
// HC-SR04 distance threshold — anything closer than this counts as "cup detected".
// Calibrate to your enclosure.
#define GLASS_THRESHOLD_CM 6.0f

// Number of consecutive in-range readings required to flag glass present.
// Filters out spurious echoes / single bad reads.
#define GLASS_DEBOUNCE_HITS 2

// How often (ms) the firmware reads the HC-SR04
#define DISTANCE_INTERVAL_MS 150

// ─── Behaviour ───────────────────────────────────────────────────────────────
// Maximum dispense volume accepted (should match backend settings)
#define MAX_DISPENSE_ML 60

// Flow rate used to convert ml → pump-on duration (calibrate after assembly).
// e.g. 10 ml/s means 30ml takes 3000ms.
#define FLOW_RATE_ML_PER_S 10.0f
