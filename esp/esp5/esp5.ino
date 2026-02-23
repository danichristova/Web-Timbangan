#include <HX711_ADC.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>

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

// ================= VAR =================
float calFactor = 30.3;
float lastWeight = 0;

unsigned long warmupTime = 0;
unsigned long calibrationStartTime = 0;
unsigned long tareStabilizeTime = 0;
int state = 0;  // 0=Booting, 1=Siap Tare, 2=Tare Stabilizing, 3=Siap Kalibrasi, 4=Kalibrasi Loading, 5=Stabil

// ================= MOVING AVERAGE BUFFER =================
#define AVG_BUFFER_SIZE 10
float avgBuffer[AVG_BUFFER_SIZE];
int avgIndex = 0;
bool avgBufferFull = false;

float getMovingAverage(float newValue) {
  avgBuffer[avgIndex] = newValue;
  avgIndex = (avgIndex + 1) % AVG_BUFFER_SIZE;
  
  if (avgIndex == 0) avgBufferFull = true;
  
  float sum = 0;
  int count = avgBufferFull ? AVG_BUFFER_SIZE : avgIndex;
  
  for (int i = 0; i < count; i++) {
    sum += avgBuffer[i];
  }
  
  return count > 0 ? sum / count : newValue;
}

// ================= UPDATE TIMER =================
unsigned long updateTimer = 0;

// ================= FILTER =================
float filterWeight(float newWeight) {
  // Exponential moving average dengan alpha yang lebih baik untuk stabilitas
  lastWeight = lastWeight * 0.95 + newWeight * 0.05;
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
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi Connected");

  // ================= OTA =================
  ArduinoOTA.setHostname("ESP-TA01");
  ArduinoOTA.setPassword("ta01");

  ArduinoOTA.begin();

  Serial.println("OTA Ready");
  Serial.println(WiFi.localIP());

  // ================= FIREBASE =================
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("Firebase Ready");

  // Kirim IP ke Firebase
  Firebase.RTDB.setString(&fbdo, "scale/ip", WiFi.localIP().toString());

  // ================= EEPROM =================
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(CAL_ADDR, calFactor);

  if (isnan(calFactor) || calFactor == 0) {
    calFactor = 21.18;
  }

  Serial.print("CalFactor: ");
  Serial.println(calFactor);

  // ================= HX711 =================
  LoadCell.begin();
  LoadCell.start(2000);
  LoadCell.setCalFactor(calFactor);

  Firebase.RTDB.setString(&fbdo, "scale/status", "Booting...");
  Firebase.RTDB.setFloat(&fbdo, "scale/weight", 0);

  // Setup output pin 21
  pinMode(21, OUTPUT);
  digitalWrite(21, LOW);

  warmupTime = millis();

  Serial.println("BOOT DONE");
}

// ================= LOOP =================
void loop() {

  ArduinoOTA.handle();
  LoadCell.update();

  float weight = LoadCell.getData();

  if (isnan(weight)) weight = 0;

  weight = filterWeight(weight);

  // ================= UPDATE SETIAP 1 DETIK =================
  if (millis() - updateTimer > 1000) {
    updateTimer = millis();
  }

  // ================= FILTER & MOVING AVERAGE =================
  weight = filterWeight(weight);
  weight = getMovingAverage(weight);

  // ================= STATE =================

  // STATE 0: WARMUP 60 DETIK
  if (state == 0) {

    int sisa = 60 - (millis() - warmupTime) / 1000;
    Firebase.RTDB.setString(&fbdo, "scale/status", "Booting " + String(sisa) + "s");

    if (millis() - warmupTime > 60000) {
      sendLog("BOOT DONE");
      state = 1;
    }
  }

  // STATE 1: READY FOR TARE
  else if (state == 1) {

    Firebase.RTDB.setString(&fbdo, "scale/status", "Siap Tare");

    // TARE dari WEB
    if (Firebase.RTDB.getInt(&fbdo, "command/tare")) {

      if (fbdo.intData() == 1) {

        LoadCell.refreshDataSet();
        LoadCell.tare();
        
        // Clear moving average buffer setelah tare
        avgIndex = 0;
        avgBufferFull = false;
        for (int i = 0; i < AVG_BUFFER_SIZE; i++) {
          avgBuffer[i] = 0;
        }
        
        tareStabilizeTime = millis();
        Firebase.RTDB.setInt(&fbdo, "command/tare", 0);

        sendLog("TARE STABILIZING...");
        state = 2;  // Go to stabilizing state
      }
    }
  }

  // STATE 2: TARE STABILIZATION (3 DETIK)
  else if (state == 2) {

    int sisa = 3 - (millis() - tareStabilizeTime) / 1000;
    Firebase.RTDB.setString(&fbdo, "scale/status", "Tare Stabilizing " + String(sisa) + "s");

    if (millis() - tareStabilizeTime > 3000) {
      sendLog("TARE DONE");
      state = 3;  // Go to calibration ready state
    }
  }

  // STATE 3: READY FOR CALIBRATION
  else if (state == 3) {

    Firebase.RTDB.setString(&fbdo, "scale/status", "Siap Kalibrasi");

    // KALIBRASI dari WEB
    if (Firebase.RTDB.getFloat(&fbdo, "command/kalibrasi")) {

      float known = fbdo.floatData();

      if (known > 0) {

        calibrationStartTime = millis();
        state = 4;

        sendLog("Kalibrasi dimulai...");
      }
    }
  }

  // STATE 4: CALIBRATION LOADING (20 DETIK)
  else if (state == 4) {

    int sisa = 20 - (millis() - calibrationStartTime) / 1000;
    Firebase.RTDB.setString(&fbdo, "scale/status", "Kalibrasi " + String(sisa) + "s");

    if (millis() - calibrationStartTime > 20000) {

      float known = 0;
      Firebase.RTDB.getFloat(&fbdo, "command/kalibrasi");
      known = fbdo.floatData();

      if (known > 0) {

        LoadCell.refreshDataSet();

        float newCal = LoadCell.getNewCalibration(known);

        LoadCell.setCalFactor(newCal);

        EEPROM.put(CAL_ADDR, newCal);
        EEPROM.commit();

        Firebase.RTDB.setFloat(&fbdo, "scale/cal_factor", newCal);
        Firebase.RTDB.setFloat(&fbdo, "command/kalibrasi", 0);

        sendLog("KALIBRASI SELESAI");
        state = 5;
      }
    }
  }

  // STATE 5: RUNNING STABLE
  else if (state == 5) {

    Firebase.RTDB.setString(&fbdo, "scale/status", "Stabil");

    // TARE dari WEB
    if (Firebase.RTDB.getInt(&fbdo, "command/tare")) {

      if (fbdo.intData() == 1) {

        LoadCell.tare();
        Firebase.RTDB.setInt(&fbdo, "command/tare", 0);

        sendLog("TARE DONE");
      }
    }

    // KALIBRASI ulang
    if (Firebase.RTDB.getFloat(&fbdo, "command/kalibrasi")) {

      float known = fbdo.floatData();

      if (known > 0) {

        calibrationStartTime = millis();
        state = 4;

        sendLog("Kalibrasi ulang dimulai...");
      }
    }
  }

  // ================= KIRIM DATA SETIAP 1 DETIK =================
  if (Firebase.ready() && millis() - updateTimer > 1000) {

    Firebase.RTDB.setFloat(&fbdo, "scale/weight", weight);
    // Baca command untuk pin 21 (1 = HIGH / 0 = LOW)
    if (Firebase.RTDB.getInt(&fbdo, "command/pin21")) {
      int v = fbdo.intData();
      if (v == 1) {
        digitalWrite(21, HIGH);
      } else if (v == 0) {
        digitalWrite(21, LOW);
      }
    }

    updateTimer = millis();
  }

  delay(50);
}