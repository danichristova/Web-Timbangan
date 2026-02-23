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
#define WEIGHT_ADDR 20

// ================= VAR =================
float calFactor = 1.0;
float weight = 0;
float lastWeight = 0;
float lastSavedWeight = 0;

unsigned long lastSend = 0;

// ================= FILTER =================
float filterWeight(float newWeight) {

  float diff = newWeight - lastWeight;

  if (abs(diff) < 0.5) return lastWeight;

  lastWeight = lastWeight * 0.7 + newWeight * 0.3;

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

  // ================= EEPROM =================
  EEPROM.begin(EEPROM_SIZE);

  EEPROM.get(CAL_ADDR, calFactor);
  EEPROM.get(WEIGHT_ADDR, lastSavedWeight);

  if (isnan(calFactor) || calFactor == 0) {
    calFactor = 1.0;
  }

  if (isnan(lastSavedWeight) || isinf(lastSavedWeight)) {
    lastSavedWeight = 0;
  }

  Serial.print("CalFactor: ");
  Serial.println(calFactor);

  Serial.print("Last Weight: ");
  Serial.println(lastSavedWeight);

  // ================= WIFI =================
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("WiFi Connected");

  // ================= FIREBASE =================
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("Firebase Connected");

  // Kirim data lama dulu
  Firebase.RTDB.setFloat(&fbdo, "scale/weight", lastSavedWeight);
  Firebase.RTDB.setFloat(&fbdo, "scale/cal_factor", calFactor);
  Firebase.RTDB.setString(&fbdo, "scale/status", "Booting");

  sendLog("BOOT OK");

  // ================= HX711 =================
  LoadCell.begin();
  LoadCell.start(2000, true);

  LoadCell.setCalFactor(calFactor);

  delay(2000);

  LoadCell.tare();

  Serial.println("HX711 Ready");
}

// ================= LOOP =================
void loop() {

  if (!LoadCell.update()) return;

  weight = LoadCell.getData();
  weight = filterWeight(weight);

  // Cegah error
  if (isnan(weight) || isinf(weight)) {
    weight = lastWeight;
  }

  if (abs(weight) < 1) {
    weight = 0;
  }

  Serial.print("Weight: ");
  Serial.println(weight);

  // ================= FIREBASE UPDATE =================
  if (Firebase.ready() && millis() - lastSend > 1000) {

    Firebase.RTDB.setFloat(&fbdo, "scale/weight", weight);

    int bottles = weight / 600;
    Firebase.RTDB.setInt(&fbdo, "scale/bottles", bottles);

    Firebase.RTDB.setString(&fbdo, "scale/status", "Running");

    // Simpan EEPROM
    EEPROM.put(WEIGHT_ADDR, weight);
    EEPROM.commit();

    lastSend = millis();
  }

  // ================= TARE WEB =================
  if (Firebase.RTDB.getBool(&fbdo, "scale/tare")) {

    if (fbdo.boolData()) {

      LoadCell.tare();

      Firebase.RTDB.setBool(&fbdo, "scale/tare", false);

      sendLog("TARE WEB");
    }
  }

  // ================= KALIBRASI WEB =================
  if (Firebase.RTDB.getFloat(&fbdo, "command/kalibrasi")) {

    float known = fbdo.floatData();

    if (known > 0) {

      Serial.println("CALIB START");

      LoadCell.refreshDataSet();

      float newCal = LoadCell.getNewCalibration(known);

      LoadCell.setCalFactor(newCal);

      calFactor = newCal;

      EEPROM.put(CAL_ADDR, calFactor);
      EEPROM.commit();

      Firebase.RTDB.setFloat(&fbdo, "scale/cal_factor", calFactor);
      Firebase.RTDB.setFloat(&fbdo, "command/kalibrasi", 0);

      sendLog("CALIB DONE");

      Serial.print("New Cal: ");
      Serial.println(calFactor);
    }
  }

  delay(50);
}