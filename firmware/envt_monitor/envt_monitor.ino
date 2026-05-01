#include <WiFi.h>
#include <FirebaseESP32.h>
#include "DHT.h"

// --- HARDWARE CONFIGURATION ---

// SENSORS
#define PIN_DHT 33      
#define PIN_MQ135 34    // Analog Input
#define PIN_MQ2 35      // Analog Input
#define PIN_VIB 25      // Vibration (Digital Input)
#define PIN_PIR 12      // Motion (Digital Input)

// ACTUATORS (Active Low Relays: LOW = ON)
#define RELAY_FAN 14    // Cooling Fan
#define RELAY_PUMP 26   // Sprinkler Pump
#define RELAY_AUX 27    // Exhaust Fan / Alarm (Assigned to D27)

// CONSTANTS
#define DHTTYPE DHT22
const int GAS_LIMIT = 1200;       // Gas threshold
const float TEMP_COOL_LIMIT = 28.0; // Fan turns on
const float TEMP_FIRE_LIMIT = 45.0; // Fire logic temp

// WIFI & FIREBASE
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
#define DB_URL "environment-monitoring-s-1885c-default-rtdb.asia-southeast1.firebasedatabase.app"
#define DB_SECRET "YOUR_FIREBASE_SECRET"

DHT dht(PIN_DHT, DHTTYPE);
FirebaseData fbdo;
FirebaseConfig config;
FirebaseAuth auth;

void setup() {
  Serial.begin(115200);
  
  // Init Sensors
  dht.begin();
  analogReadResolution(12); // ESP32 default is 12-bit (0-4095)
  pinMode(PIN_VIB, INPUT);
  pinMode(PIN_PIR, INPUT);
  
  // Init Relays (Start OFF = HIGH)
  pinMode(RELAY_FAN, OUTPUT);
  pinMode(RELAY_PUMP, OUTPUT);
  pinMode(RELAY_AUX, OUTPUT);
  digitalWrite(RELAY_FAN, HIGH);
  digitalWrite(RELAY_PUMP, HIGH);
  digitalWrite(RELAY_AUX, HIGH);

  // Connect Network
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println(" Connected!");

  // Connect Database
  config.host = DB_URL;
  config.signer.tokens.legacy_token = DB_SECRET;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  // 1. Read Data
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int mq135 = analogRead(PIN_MQ135);
  int mq2 = analogRead(PIN_MQ2);
  int vib = digitalRead(PIN_VIB);
  int pir = digitalRead(PIN_PIR);

  if (isnan(h) || isnan(t)) { Serial.println("DHT Error"); return; }

  // 2. Logic Evaluation
  bool isHot = (t > TEMP_COOL_LIMIT);
  // Fire = High Temp AND Smoke
  bool isFire = (t > TEMP_FIRE_LIMIT && mq2 > (GAS_LIMIT / 2)); 
  // Hazard = Gas Leak OR Fire OR Vibration
  bool isHazard = (mq135 > GAS_LIMIT || mq2 > GAS_LIMIT || isFire || vib == HIGH);

  // 3. Hardware Control (Active Low Logic)
  digitalWrite(RELAY_FAN, isHot ? LOW : HIGH);      // Fan Logic
  digitalWrite(RELAY_PUMP, isFire ? LOW : HIGH);    // Sprinkler Logic
  digitalWrite(RELAY_AUX, isHazard ? LOW : HIGH);   // General Alarm Logic

  // 4. Send to Firebase
  FirebaseJson status;
  status.set("Temp", t);
  status.set("Hum", h);
  status.set("MQ-2", mq2);
  status.set("MQ-135", mq135);
  status.set("Vibration", vib);
  status.set("PIR", pir);
  Firebase.updateNode(fbdo, "/Device-1/Status", status);

  FirebaseJson alert;
  alert.set("IsAlert", isHazard);
  alert.set("Cooling fan", isHot ? "ON" : "OFF");
  alert.set("sprinkler", isFire ? "ON" : "OFF");
  alert.set("ExFan", isHazard ? "ON" : "OFF"); // Aux relay acts as Exhaust/Alarm
  Firebase.updateNode(fbdo, "/Device-1/Alert", alert);

  // Debug
  Serial.printf("T:%.1f | VIB:%d | PIR:%d | Hazard:%d\n", t, vib, pir, isHazard);
  
  delay(1000);
}