// ============================================================
//        ESP32 Differential Relay Protection System
// ============================================================

#include <LiquidCrystal.h>

// ---- Pin Definitions (adjust to match your wiring) ----
#define CS1_PIN       39     // Current Sensor 1 → ADC1 (IO34)
#define CS2_PIN       36     // Current Sensor 2 → ADC1 (IO35)
#define RELAY_PIN     25     // Relay driver (IN3 via optocoupler)
#define LED_NORMAL    27     // Green LED - Normal operation
#define LED_FAULT     26     // Red LED   - Fault condition

// LCD pin mapping from your 16-pin header schematic
// RS, EN, D4, D5, D6, D7
LiquidCrystal lcd(15, 4, 16, 17, 18, 19);  // Adjust to your actual wiring

// ---- Calibration ----
#define VREF              3.3f        // ESP32 ADC reference voltage
#define ADC_RESOLUTION    4095.0f     // 12-bit ADC
#define SENSITIVITY       0.185f      // ACS712-5A: 185mV/A 
#define OFFSET_VOLTAGE    1.65f       // Midpoint of sensor output (VCC/2)
#define DIFF_THRESHOLD    1.5f        // Amps — trip relay if |I1 - I2| > this
#define NUM_SAMPLES       100          // Samples per reading for averaging
#define TURNS_RATIO       1           // Truns ratio of the transformer 

// ---- Voltage divider correction ----
// R10=10kΩ, R12=6.8kΩ → Vout = Vin × 6.8/(10+6.8) = Vin × 0.4048

#define DIVIDER_RATIO     (10.0f + 6.8f) / 6.8f   // ≈ 2.47 — scale back up

// ---- State ----

bool faultTripped = false;

// ============================================================
//  Read and average ADC, convert to current in Amps
// ============================================================

float readCurrent1(int pin) {
  long sum = 0;

  for (int i = 0; i < NUM_SAMPLES; i++) {

    float adc = analogRead(pin);

    float voltage = (adc / ADC_RESOLUTION) * VREF;
    voltage *= DIVIDER_RATIO;

    float current = (voltage - OFFSET_VOLTAGE)/ SENSITIVITY;

    sum += current * current;   // square

    delayMicroseconds(200);
  }

  float mean = sum / (float)NUM_SAMPLES;

  float rmsCurrent = sqrt(mean);

  return rmsCurrent;
}

float readCurrent2(int pin) {
  long sum = 0;

  for (int i = 0; i < NUM_SAMPLES; i++) {

    float adc = analogRead(pin);

    float voltage = (adc / ADC_RESOLUTION) * VREF;
    voltage *= DIVIDER_RATIO;

    float current = (voltage - OFFSET_VOLTAGE) / SENSITIVITY;

    sum += current * current;   // square

    delayMicroseconds(200);
  }

  float mean = sum / (float)NUM_SAMPLES;

  float rmsCurrent = sqrt(mean);

  return rmsCurrent;
}

// ============================================================
//  Trip the relay and show fault
// ============================================================
void tripRelay(float I1, float I2) {
  faultTripped = true;

  digitalWrite(RELAY_PIN, LOW);    // De-energize relay (trip)
  digitalWrite(LED_NORMAL, LOW);   // Normal LED OFF
  digitalWrite(LED_FAULT,  HIGH);  // Fault LED ON

  Serial.println("!!! FAULT DETECTED — RELAY TRIPPED !!!");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("!! FAULT TRIPPED");
  lcd.setCursor(0, 1);
  lcd.print("dI=");
  lcd.print(abs(I1 - I2), 3);
  lcd.print("A");
}

// ============================================================
//  Reset to normal state
// ============================================================
void resetNormal() {
  faultTripped = false;

  digitalWrite(RELAY_PIN, HIGH);   // Energize relay (normal = closed)
  digitalWrite(LED_NORMAL, HIGH);  // Normal LED ON
  digitalWrite(LED_FAULT,  LOW);   // Fault LED OFF

  Serial.println("System NORMAL — Relay Energized");
}

// ============================================================
//  Update LCD with live readings
// ============================================================
void updateLCD(float I1, float I2) {
  float diff = abs(I1 - I2);

  lcd.clear();

  // Line 1: Both currents
  lcd.setCursor(0, 0);
  lcd.print("I1:");
  lcd.print(I1, 2);
  lcd.print("A ");
  lcd.setCursor(0, 1);
  lcd.print("I2:");
  lcd.print(I2, 2);
  lcd.print("A");

  // // Line 2: Differential + status
  // lcd.setCursor(0, 1);
  // lcd.print("dI:");
  // lcd.print(diff, 3);
  // lcd.print("A ");
  // lcd.print(faultTripped ? "FAULT" : "NORMAL");
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("=================================");
  Serial.println("  Differential Relay Protection  ");
  Serial.println("=================================");

  // Pin modes
  pinMode(RELAY_PIN,  OUTPUT);
  pinMode(LED_NORMAL, OUTPUT);
  pinMode(LED_FAULT,  OUTPUT);

  // ADC config
  analogReadResolution(12);         // 12-bit (0–4095)
  analogSetAttenuation(ADC_11db);   // Full 0–3.3V range

  // LCD
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Diff. Relay Sys.");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(2000);

  // Start in normal state
  resetNormal();
}

// ============================================================
//  MAIN LOOP
// ============================================================
void loop() {
  float I1   = readCurrent1(CS1_PIN);
  float I2   = readCurrent2(CS2_PIN);
  float diff = abs(I1 - I2);

  // Serial monitor output
  Serial.print("I1: "); Serial.print(I1, 2); Serial.print(" A  |  ");
  Serial.print("I2: "); Serial.print(I2, 2); Serial.print(" A  |  ");
  Serial.print("ΔI: "); Serial.print(diff, 3); Serial.print(" A  |  ");
  Serial.println(faultTripped ? "Status: FAULT" : "Status: NORMAL");

  // ---- Differential Protection Logic ----
  if (!faultTripped) {
    if (diff > DIFF_THRESHOLD) {
      tripRelay(I1, I2);           // Fault detected → trip
    } else {
      digitalWrite(LED_NORMAL, HIGH);
      digitalWrite(LED_FAULT,  LOW);
      updateLCD(I1, I2);
    }
  } else {
    // Show fault on LCD until manually reset
    // To auto-reset: check if fault clears, then call resetNormal()
    updateLCD(I1, I2);

    // Optional auto-reset: uncomment below if you want self-clearing
    // if (diff < DIFF_THRESHOLD) {
    //   delay(2000);  // Confirm fault has cleared
    //   resetNormal();
    // }
  }

  delay(500);  // Update every 500ms
}