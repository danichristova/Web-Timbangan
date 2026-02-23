#include <HX711_ADC.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <EEPROM.h>

// ================= WIFI =================
#define WIFI_SSID "Dani"
#define WIFI_PASSWORD "rumahdani"

// ================= FIREBASE =================
#define API_KEY "AIzaSyAUH7n1tvEFw_ns46Wv6as_sldSPG_EQyE"
#define DATABASE_URL "https://timbangan-test-1-default-rtdb.asia-southeast1.firebasedatabase.app"

#define USER_EMAIL "esp@gmail.com"
#define USER_PASSWORD "esp12345"

// ================= HX711 =================
#define DOUT 22
#define SCK 23
HX711_ADC LoadCell(DOUT, SCK);

// ================= FIREBASE =================
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ================= EEPROM =================
#define EEPROM_SIZE 64
#define CAL_ADDR 0

// ================= AVERAGE 5 DETIK =================
unsigned long avgStartTime = 0;
float avgTotal = 0;
int avgCount = 0;
float avgWeight = 0;
bool avgReady = false;

// ================= VAR =================
float lastWeight = 0;
float calFactor = 21.18;

unsigned long lastSend = 0;
unsigned long warmupTime = 0;
unsigned long detectTime = 0;

int state = 0;

bool baseSet = false;
float baseWeight = 0;

// ================= AVERAGE =================
float readStableWeight() {

  float total = 0;
  int samples = 15;

  for (int i = 0; i < samples; i++) {
    while (!LoadCell.update())
      ;
    total += LoadCell.getData();
  }

  return total / samples;
}

float read5SecondAverage() {

  while (!LoadCell.update())
    ;

  float w = LoadCell.getData();

  if (isnan(w) || isinf(w)) return avgWeight;

  avgTotal += w;
  avgCount++;

  // Mulai timer
  if (avgStartTime == 0) {
    avgStartTime = millis();
  }

  // Jika sudah 5 detik
  if (millis() - avgStartTime >= 5000) {

    if (avgCount > 0) {
      avgWeight = avgTotal / avgCount;
    }

    Serial.print("AVG 5 DETIK: ");
    Serial.println(avgWeight);

    avgTotal = 0;
    avgCount = 0;
    avgStartTime = millis();

    avgReady = true;
  }

  return avgWeight;
}

// ================= FILTER =================
float filterWeight(float newWeight) {

  float diff = newWeight - lastWeight;

  if (abs(diff) < 0.5) return lastWeight;

  lastWeight = lastWeight * 0.85 + newWeight * 0.15;

  return lastWeight;
}

// ================= LOG =================
void sendLog(String text) {
  Firebase.RTDB.setString(&fbdo, "scale/log", text);
}

// ================= SETUP =================
void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println("BOOTING...");

  // WIFI
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("WiFi Connected");

  // FIREBASE
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("Firebase Ready");

  // EEPROM
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(CAL_ADDR, calFactor);

  Serial.print("CalFactor: ");
  Serial.println(calFactor);

  // HX711
  LoadCell.begin();
  LoadCell.start(2000, true);

  // Pakai calFactor default
  LoadCell.setCalFactor(calFactor);

  // Reset Firebase
  Firebase.RTDB.setFloat(&fbdo, "scale/weight", 0);
  Firebase.RTDB.setString(&fbdo, "scale/status", "Booting...");
  Firebase.RTDB.setFloat(&fbdo, "command/kalibrasi", 0);

  warmupTime = millis();
  state = 0;

  Serial.println("BOOT DONE");
}

// ================= LOOP =================
void loop() {

  if (!LoadCell.update()) return;

  float weight = read5SecondAverage();
  weight = filterWeight(weight);

  // Cegah NaN
  if (isnan(weight) || isinf(weight)) weight = 0;

  if (abs(weight) < 1) weight = 0;

  Serial.print("Weight: ");
  Serial.println(weight);

  // Kirim tiap 5 detik
  if (Firebase.ready() && avgReady) {

    Firebase.RTDB.setFloat(&fbdo, "scale/weight", weight);

    avgReady = false;
    lastSend = millis();
  }

  // ================= STATE MACHINE =================

  // WARMUP
if (state == 0) {

  int sisa = 30 - (millis() - warmupTime) / 1000;

  Firebase.RTDB.setString(&fbdo, "scale/status", "Warmup " + String(sisa) + " detik");

  if (millis() - warmupTime > 30000) {

    Serial.println("START TARE");

    LoadCell.tareNoDelay();
    sendLog("TARE START");

    state = 1;
  }
}

  // TARE
  else if (state == 1) {

    Firebase.RTDB.setString(&fbdo, "scale/status", "Tare...");

    if (LoadCell.getTareStatus()) {

      sendLog("TARE DONE");
      state = 2;
    }
  }

  // WAIT OBJECT
  else if (state == 2) {

    Firebase.RTDB.setString(&fbdo, "scale/status", "Letakkan benda");

    if (!baseSet) {
      baseWeight = weight;
      baseSet = true;
    }

    if (weight - baseWeight > 10 || weight - baseWeight < 10) {

      sendLog("OBJECT DETECTED");
      detectTime = millis();
      state = 3;
    }
  }

  // WAIT STABLE
  else if (state == 3) {

    Firebase.RTDB.setString(&fbdo, "scale/status", "Menunggu stabil");

    if (millis() - detectTime > 20000) {

      sendLog("READY CALIB");
      state = 4;
    }
  }

  // CALIBRATION
  else if (state == 4) {

    Firebase.RTDB.setString(&fbdo, "scale/status", "Masukkan berat di web");

    if (Firebase.RTDB.getFloat(&fbdo, "command/kalibrasi")) {

      float known = fbdo.floatData();

      if (known > 0) {

        LoadCell.refreshDataSet();

        float newCal = LoadCell.getNewCalibration(known);

        LoadCell.setCalFactor(newCal);

        EEPROM.put(CAL_ADDR, newCal);
        EEPROM.commit();

        Firebase.RTDB.setFloat(&fbdo, "scale/cal_factor", newCal);
        Firebase.RTDB.setFloat(&fbdo, "command/kalibrasi", 0);

        sendLog("CALIB DONE");

        state = 5;
      }
    }
  }

  // NORMAL
  else if (state == 5) {

    Firebase.RTDB.setString(&fbdo, "scale/status", "Running");

    if (Firebase.RTDB.getBool(&fbdo, "scale/tare")) {

      if (fbdo.boolData()) {

        LoadCell.tareNoDelay();
        Firebase.RTDB.setBool(&fbdo, "scale/tare", false);

        sendLog("MANUAL TARE");
      }
    }
  }

  delay(50);
}