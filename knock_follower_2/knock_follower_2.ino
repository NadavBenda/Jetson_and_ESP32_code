#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

// I2C
const int SDA_PIN = 8;
const int SCL_PIN = 9;
const byte MPU_ADDR = 0x68;

// LCD
hd44780_I2Cexp lcd;

// Outputs
const int LED_PINS[4] = {4, 5, 6, 7};
const int BUZZER_PIN = 10;

// Knock sensing
long baseline = 0;

const long NOISE_FLOOR      = 1500;
const long KNOCK_THRESHOLD  = 3500;
const long MAX_KNOCK        = 15000;

// UI timing
const unsigned long HOLD_MS = 100;

// Knock recording
const unsigned long REFRACTORY_MS   = 120;
const unsigned long END_SILENCE_MS  = 1500;
const unsigned long KNOCK_END_MS    = 40;

const int MAX_KNOCKS = 20;

unsigned long knocks[MAX_KNOCKS];     // knock times relative to first knock
long knockStrengths[MAX_KNOCKS];      // average strength of each knock
int knockCount = 0;

bool recording = false;
unsigned long firstKnockTime = 0;
unsigned long lastKnockTime = 0;

// Knock averaging
bool knockActive = false;
unsigned long knockEndCandidateTime = 0;
long knockSum = 0;
int knockSamples = 0;

// LED hold
int displayedLeds = 0;
unsigned long holdUntil = 0;

// LCD refresh
unsigned long lastLcdUpdate = 0;
const unsigned long LCD_UPDATE_MS = 150;


// ---------- IMU ----------
void writeReg(byte reg, byte value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

int16_t read16(byte reg) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);

  Wire.requestFrom(MPU_ADDR, (byte)2);

  byte hi = Wire.read();
  byte lo = Wire.read();

  return (int16_t)((hi << 8) | lo);
}

long readMag() {
  int16_t ax = read16(0x3B);
  int16_t ay = read16(0x3D);
  int16_t az = read16(0x3F);

  long axl = ax;
  long ayl = ay;
  long azl = az;

  return sqrt(axl * axl + ayl * ayl + azl * azl);
}


// ---------- Helpers ----------
int strengthToLeds(long strength) {
  strength = constrain(strength, KNOCK_THRESHOLD, MAX_KNOCK);
  int n = map(strength, KNOCK_THRESHOLD, MAX_KNOCK, 1, 4);
  return constrain(n, 1, 4);
}


// ---------- Outputs ----------
void setLeds(int n) {
  for (int i = 0; i < 4; i++) {
    digitalWrite(LED_PINS[i], i < n ? HIGH : LOW);
  }
}

void beep(int freq, int ms) {
  tone(BUZZER_PIN, freq);
  delay(ms);
  noTone(BUZZER_PIN);
}

void showReady() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("READY");
  lcd.setCursor(0, 1);
  lcd.print("L:0 K:0");
}

void showRecording(int count, long strength) {
  int leds = strengthToLeds(strength);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("REC Knocks:");
  lcd.print(count);

  lcd.setCursor(0, 1);
  lcd.print("L:");
  lcd.print(leds);
  lcd.print(" K:");
  lcd.print(strength);
}

void showReplay(int count) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("REPLAY");
  lcd.setCursor(0, 1);
  lcd.print("Knocks: ");
  lcd.print(count);
}


// ---------- Replay ----------
void replayKnocks() {
  if (knockCount == 0) return;

  Serial.println("REPLAY");
  showReplay(knockCount);

  delay(300);

  for (int i = 0; i < knockCount; i++) {
    if (i > 0) {
      unsigned long gap = knocks[i] - knocks[i - 1];
      delay(gap);
    }

    long strength = constrain(knockStrengths[i], KNOCK_THRESHOLD, MAX_KNOCK);

    int freq = map(strength, KNOCK_THRESHOLD, MAX_KNOCK, 1200, 2200);
    int dur  = map(strength, KNOCK_THRESHOLD, MAX_KNOCK, 40, 130);

    int replayLeds = strengthToLeds(strength);

    setLeds(replayLeds);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("REPLAY ");
    lcd.print(i + 1);
    lcd.print("/");
    lcd.print(knockCount);

    lcd.setCursor(0, 1);
    lcd.print("L:");
    lcd.print(replayLeds);
    lcd.print(" K:");
    lcd.print(strength);

    beep(freq, dur);

    setLeds(0);
  }

  knockCount = 0;
  recording = false;
  displayedLeds = 0;
  setLeds(0);
  showReady();
}


// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(1000);

  for (int i = 0; i < 4; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }

  pinMode(BUZZER_PIN, OUTPUT);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  int lcdStatus = lcd.begin(16, 2);
  if (lcdStatus) {
    Serial.print("LCD init failed, status=");
    Serial.println(lcdStatus);
  } else {
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Knock Recorder");
    lcd.setCursor(0, 1);
    lcd.print("Calibrating...");
  }

  // Wake up IMU
  writeReg(0x6B, 0x00);
  delay(100);

  // Baseline calibration
  long sum = 0;
  const int N = 200;

  for (int i = 0; i < N; i++) {
    sum += readMag();
    delay(5);
  }

  baseline = sum / N;

  Serial.print("baseline:");
  Serial.println(baseline);

  beep(1500, 80);
  showReady();
}


// ---------- Main loop ----------
void loop() {
  unsigned long now = millis();

  long mag = readMag();
  long knock = abs(mag - baseline);

  // ----- LED bar live -----
  int newLedCount = 0;

  if (knock > NOISE_FLOOR) {
    newLedCount = map(knock, NOISE_FLOOR, MAX_KNOCK, 1, 4);
    newLedCount = constrain(newLedCount, 1, 4);
  }

  if (newLedCount > displayedLeds) {
    displayedLeds = newLedCount;
    holdUntil = now + HOLD_MS;
  }

  if (now > holdUntil) {
    displayedLeds = newLedCount;
  }

  setLeds(displayedLeds);


  // ----- Knock detection with average strength -----
  bool aboveThreshold = knock > KNOCK_THRESHOLD;

  // Start new knock
  if (aboveThreshold && !knockActive && now - lastKnockTime > REFRACTORY_MS) {
    knockActive = true;
    knockSum = 0;
    knockSamples = 0;

    if (!recording) {
      recording = true;
      firstKnockTime = now;
      knockCount = 0;
      Serial.println("REC_START");

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("RECORDING...");
      lcd.setCursor(0, 1);
      lcd.print("L:0 K:0");
    }

    knockEndCandidateTime = now;
  }

  // During knock
  if (knockActive) {
    if (aboveThreshold) {
      knockSum += knock;
      knockSamples++;
      knockEndCandidateTime = now;
    }

    // End knock after short silence
    if (!aboveThreshold && now - knockEndCandidateTime > KNOCK_END_MS) {
      long avgStrength = knockSamples > 0 ? knockSum / knockSamples : KNOCK_THRESHOLD;

      if (knockCount < MAX_KNOCKS) {
        knocks[knockCount] = knockEndCandidateTime - firstKnockTime;
        knockStrengths[knockCount] = avgStrength;
        knockCount++;
      }

      lastKnockTime = now;
      knockActive = false;

      Serial.print("KNOCK ");
      Serial.print(knockCount);
      Serial.print(" avg=");
      Serial.println(avgStrength);

      showRecording(knockCount, avgStrength);
    }
  }


  // ----- End recording after silence -----
  if (recording && knockCount > 0 && !knockActive && now - lastKnockTime > END_SILENCE_MS) {
    replayKnocks();
  }


  // ----- LCD live update when idle -----
  if (!recording && !knockActive && now - lastLcdUpdate > LCD_UPDATE_MS) {
    lastLcdUpdate = now;

    lcd.setCursor(0, 0);
    lcd.print("READY           ");

    lcd.setCursor(0, 1);
    lcd.print("L:");
    lcd.print(displayedLeds);
    lcd.print(" K:");
    lcd.print(knock);
    lcd.print("      ");
  }


  // ----- Serial Plotter -----
  Serial.print("min:");
  Serial.print(0);
  Serial.print(",max:");
  Serial.print(30000);

  Serial.print(",knock:");
  Serial.print(knock);
  Serial.print(",leds:");
  Serial.println(displayedLeds);

  delay(10);
}