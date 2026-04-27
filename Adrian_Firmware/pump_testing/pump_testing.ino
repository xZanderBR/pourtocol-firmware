/**************************************************************************/
/*!
    Pourtocol — sensor-triggered pump test

    When the HC-SR04 ultrasonic sensor detects an object (cup) within
    GLASS_THRESHOLD_CM for GLASS_DEBOUNCE_HITS consecutive readings,
    the pump activates for POUR_DURATION_MS, then shuts off.

    The cup must be removed (sensor reading goes out of range) before
    another pour can be triggered. This prevents continuous pouring
    while the cup sits there.
*/
/**************************************************************************/

// --- Pin assignments ---
#define TRIG_PIN  5
#define ECHO_PIN  18
#define RELAY_PIN 15

// --- Relay logic (high-level trigger) ---
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

// --- Sensor timing & detection ---
const unsigned long DISTANCE_INTERVAL  = 200;    // sensor reading every 200ms
const unsigned long HEARTBEAT_INTERVAL = 5000;   // heartbeat every 5s

const float    GLASS_THRESHOLD_CM  = 6.0;       // anything closer = "cup detected"
const uint8_t  GLASS_DEBOUNCE_HITS = 3;          // need 3 consecutive in-range readings

// --- Pour timing ---
const unsigned long POUR_DURATION_MS = 2000;     // pump runs for 2 seconds

// --- State ---
unsigned long lastDistanceRead = 0;
unsigned long lastHeartbeat    = 0;
unsigned long pourStartTime    = 0;

uint8_t glassHitCount = 0;
bool    glassPresent  = false;
bool    pumpRunning   = false;
bool    pourCompleted = false;   // true after a pour for the current cup; resets when cup leaves

// --- Helpers ---
float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return -1;
  return duration * 0.0343 / 2.0;
}

void startPour() {
  Serial.println("[PUMP] ON — starting pour");
  digitalWrite(RELAY_PIN, RELAY_ON);
  pumpRunning   = true;
  pourStartTime = millis();
}

void stopPour() {
  Serial.println("[PUMP] OFF — pour complete");
  digitalWrite(RELAY_PIN, RELAY_OFF);
  pumpRunning   = false;
  pourCompleted = true;   // mark this cup as already poured
}

// --- Setup ---
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  // Set relay OFF before configuring as output (prevents brief pour at boot)
  digitalWrite(RELAY_PIN, RELAY_OFF);
  pinMode(RELAY_PIN, OUTPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  Serial.println("Pourtocol — sensor-triggered pump test");
  Serial.println("Place a cup within range to trigger a pour.");
}

// --- Main loop ---
void loop() {
  unsigned long now = millis();

  // --- Heartbeat ---
  if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    lastHeartbeat = now;
    Serial.print("[HB] glass=");
    Serial.print(glassPresent ? "yes" : "no");
    Serial.print(" | pump=");
    Serial.print(pumpRunning ? "ON" : "off");
    Serial.print(" | poured=");
    Serial.println(pourCompleted ? "yes" : "no");
  }

  // --- Pump shutoff timer ---
  if (pumpRunning && (now - pourStartTime >= POUR_DURATION_MS)) {
    stopPour();
  }

  // --- Sensor read ---
  if (now - lastDistanceRead >= DISTANCE_INTERVAL) {
    lastDistanceRead = now;
    float distance = readDistanceCm();

    if (distance >= 0) {
      Serial.print("[DIST] "); Serial.print(distance, 1); Serial.println(" cm");

      if (distance < GLASS_THRESHOLD_CM) {
        // Object in range
        if (glassHitCount < GLASS_DEBOUNCE_HITS) glassHitCount++;
        if (glassHitCount >= GLASS_DEBOUNCE_HITS && !glassPresent) {
          glassPresent = true;
          Serial.println("[GLASS] detected (debounced)");

          // Trigger pour only if we haven't poured for this cup yet
          if (!pourCompleted && !pumpRunning) {
            startPour();
          }
        }
      } else {
        // Out of range
        glassHitCount = 0;
        if (glassPresent) {
          glassPresent  = false;
          pourCompleted = false;   // cup is gone, ready for next cup
          Serial.println("[GLASS] removed — ready for next cup");
        }
      }
    } else {
      // No echo (sensor saw nothing within max range)
      glassHitCount = 0;
      if (glassPresent) {
        glassPresent  = false;
        pourCompleted = false;
        Serial.println("[GLASS] removed (no echo) — ready for next cup");
      }
    }
  }
}