#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_PN532.h>

#include "config.h"

// ─── Server ──────────────────────────────────────────────────────────────────

AsyncWebServer server(80);

// ─── PN532 NFC reader (I2C — Adrian rev) ─────────────────────────────────────
// I2C constructor uses the default Wire pins (SDA=21, SCL=22 on ESP32).

Adafruit_PN532 nfc(PIN_NFC_IRQ, PIN_NFC_RESET);

static bool          nfcReady       = false;
static String        nfcUid         = "";
static bool          nfcTagPresent  = false;
static unsigned long nfcLastSeenMs  = 0;
static unsigned long nfcLastPollMs  = 0;

// ─── Machine state ───────────────────────────────────────────────────────────

enum class MachineState { IDLE, POURING };

static MachineState  machineState = MachineState::IDLE;
static int           lastPourMl   = 0;
static unsigned long pourEndMs    = 0;

// ─── Glass detection (debounced — Adrian rev) ────────────────────────────────

static unsigned long lastDistanceReadMs = 0;
static uint8_t       glassHitCount      = 0;
static bool          glassPresent       = false;

// ─── HC-SR04 helpers ─────────────────────────────────────────────────────────

float readDistanceCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long duration = pulseIn(PIN_ECHO, HIGH, 30000);  // 30ms timeout
  if (duration == 0) return -1.0f;                  // -1 = no echo
  return duration * 0.0343f / 2.0f;
}

void updateGlassDetection() {
  if (millis() - lastDistanceReadMs < DISTANCE_INTERVAL_MS) return;
  lastDistanceReadMs = millis();

  float distance = readDistanceCm();

  if (distance >= 0 && distance < GLASS_THRESHOLD_CM) {
    if (glassHitCount < GLASS_DEBOUNCE_HITS) glassHitCount++;
    if (glassHitCount >= GLASS_DEBOUNCE_HITS && !glassPresent) {
      glassPresent = true;
      Serial.println("[glass] detected (debounced)");
    }
  } else {
    glassHitCount = 0;
    if (glassPresent) {
      glassPresent = false;
      Serial.println("[glass] removed");
    }
  }
}

// ─── PN532 helpers ───────────────────────────────────────────────────────────

void initNfc() {
  Serial.println("[nfc] Initialising PN532 (I2C, SDA=21 SCL=22)");
  delay(1000);  // give the PN532 time to boot after power-on

  nfc.begin();

  uint32_t versiondata = 0;
  for (int attempt = 1; attempt <= 5; attempt++) {
    versiondata = nfc.getFirmwareVersion();
    if (versiondata) break;
    Serial.printf("[nfc] Attempt %d/5 — no response, retrying...\n", attempt);
    delay(500);
  }

  if (!versiondata) {
    Serial.println("[nfc] PN532 not found after 5 attempts — NFC disabled");
    Serial.println("[nfc] Check: DIP SEL0=ON SEL1=OFF, wiring SDA→21 SCL→22 + pull-ups");
    nfcReady = false;
    return;
  }
  Serial.printf("[nfc] Found PN532  firmware v%d.%d\n",
                (int)((versiondata >> 16) & 0xFF),
                (int)((versiondata >> 8) & 0xFF));
  nfc.SAMConfig();
  nfcReady = true;
}

void pollNfc() {
  if (!nfcReady) return;

  uint8_t uid[7];
  uint8_t uidLen = 0;

  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 100)) {
    String hex = "";
    for (uint8_t i = 0; i < uidLen; i++) {
      if (uid[i] < 0x10) hex += "0";
      hex += String(uid[i], HEX);
    }
    hex.toUpperCase();

    if (hex != nfcUid) {
      Serial.printf("[nfc] Tag detected: %s (%d bytes)\n", hex.c_str(), uidLen);
    }
    nfcUid        = hex;
    nfcTagPresent = true;
    nfcLastSeenMs = millis();
  } else {
    if (nfcTagPresent && (millis() - nfcLastSeenMs > NFC_TAG_LINGER_MS)) {
      Serial.println("[nfc] Tag cleared (timeout)");
      nfcUid        = "";
      nfcTagPresent = false;
    }
  }
}

// ─── Pump control via relay ──────────────────────────────────────────────────

void pumpOn() {
  digitalWrite(PIN_PUMP, HIGH);
  Serial.println("[pump] ON");
}

void pumpOff() {
  digitalWrite(PIN_PUMP, LOW);
  Serial.println("[pump] OFF");
}

// ─── Pour logic ──────────────────────────────────────────────────────────────

void startPour(int ml) {
  unsigned long durationMs = (unsigned long)((ml / FLOW_RATE_ML_PER_S) * 1000.0f);
  pourEndMs    = millis() + durationMs;
  lastPourMl   = ml;
  machineState = MachineState::POURING;
  pumpOn();
  Serial.printf("[dispense] Starting %dml pour, duration %lums\n", ml, durationMs);
}

void stopPour() {
  pumpOff();
  machineState = MachineState::IDLE;
  Serial.println("[dispense] Pour complete");
}

// ─── Route handlers ──────────────────────────────────────────────────────────

void handleStatus(AsyncWebServerRequest *request) {
  JsonDocument doc;

  doc["state"]         = (machineState == MachineState::POURING) ? "pouring" : "idle";
  doc["glass_present"] = glassPresent;
  doc["uptime"]        = millis() / 1000;
  doc["last_pour_ml"]  = lastPourMl;

  doc["nfc_uid"]        = nfcUid;
  doc["nfc_tag_present"]= nfcTagPresent;
  doc["nfc_ready"]      = nfcReady;

  String body;
  serializeJson(doc, body);
  request->send(200, "application/json", body);
}

void handleDispense(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                    size_t index, size_t total) {
  if (machineState == MachineState::POURING) {
    request->send(409, "application/json", "{\"error\":\"Already pouring\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, data, len);
  if (err) {
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  int amountMl = doc["amount_ml"] | 0;
  if (amountMl <= 0 || amountMl > MAX_DISPENSE_ML) {
    request->send(400, "application/json", "{\"error\":\"Invalid amount\"}");
    return;
  }

  if (!glassPresent) {
    request->send(409, "application/json", "{\"error\":\"No glass present\"}");
    return;
  }

  startPour(amountMl);
  request->send(200, "application/json", "{\"ok\":true}");
}

// ─── WiFi ────────────────────────────────────────────────────────────────────

void connectWifi() {
  Serial.printf("[wifi] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[wifi] Connected — IP: %s\n", WiFi.localIP().toString().c_str());
}

// ─── Setup / loop ────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);

  // Boot-safe relay init: drive LOW *before* setting OUTPUT to prevent a brief
  // pour at power-on (Adrian rev).
  digitalWrite(PIN_PUMP, LOW);
  pinMode(PIN_PUMP, OUTPUT);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);

  initNfc();

  connectWifi();

  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mdns] Hostname: http://%s.local\n", MDNS_HOSTNAME);
  } else {
    Serial.println("[mdns] Failed to start mDNS");
  }

  server.on("/status", HTTP_GET, handleStatus);
  server.on(
      "/dispense", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      nullptr,
      handleDispense
  );

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "application/json", "{\"error\":\"Not found\"}");
  });

  server.begin();
  Serial.println("[server] HTTP server started");
}

void loop() {
  // Non-blocking pour timer
  if (machineState == MachineState::POURING && millis() >= pourEndMs) {
    stopPour();
  }

  // Debounced glass detection (every DISTANCE_INTERVAL_MS)
  updateGlassDetection();

  // NFC poll (every NFC_POLL_INTERVAL_MS)
  if (millis() - nfcLastPollMs >= NFC_POLL_INTERVAL_MS) {
    nfcLastPollMs = millis();
    pollNfc();
  }
}
