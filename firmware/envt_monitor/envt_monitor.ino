#include <WiFi.h>
#include <FirebaseESP32.h>
#include "DHT.h"

// --- 1. CONFIGURATION ---
// REPLACE WITH YOUR WIFI DETAILS
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// YOUR EXACT CREDENTIALS
#define API_KEY "secret"
#define DATABASE_URL "secret firebase url"

// --- 2. HARDWARE PINS ---
#define PIN_DHT 33      
#define PIN_MQ135 34    
#define PIN_MQ2 35      
#define PIN_VIB 25      
#define PIN_PIR 12      

// RELAYS (Active Low: LOW = ON)
#define RELAY_FAN 14     // Cooling Fan
#define RELAY_PUMP 26    // Sprinkler
#define RELAY_EXHAUST 27 // Exhaust Fan

#define DHTTYPE DHT22
const int GAS_LIMIT = 1200;       
const float TEMP_COOL_LIMIT = 28.0; 
const float TEMP_FIRE_LIMIT = 45.0; 

// --- 3. OBJECTS ---
DHT dht(PIN_DHT, DHTTYPE);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long lastCycle = 0;

void setup() {
  Serial.begin(115200);
  
  dht.begin();
  pinMode(PIN_VIB, INPUT);
  pinMode(PIN_PIR, INPUT);
  pinMode(RELAY_FAN, OUTPUT);
  pinMode(RELAY_PUMP, OUTPUT);
  pinMode(RELAY_EXHAUST, OUTPUT);
  
  // Default OFF
  digitalWrite(RELAY_FAN, HIGH);
  digitalWrite(RELAY_PUMP, HIGH);
  digitalWrite(RELAY_EXHAUST, HIGH);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { Serial.print("."); delay(300); }
  Serial.println("\nConnected!");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  if (millis() - lastCycle > 1000) {
    
    // 1. READ SENSORS
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    int mq135 = analogRead(PIN_MQ135);
    int mq2 = analogRead(PIN_MQ2);
    int vib = digitalRead(PIN_VIB);
    int pir = digitalRead(PIN_PIR);
    if (isnan(h) || isnan(t)) t = 0;

    // 2. READ DASHBOARD CONTROLS (Manual Override)
    bool cmd_ExFan = false;
    bool cmd_Cooling = false;
    bool cmd_Sprinkler = false;
    bool cmd_Security = false;

    // We check the specific "Controls" path created by the dashboard switches
    if (Firebase.getString(fbdo, "/Device-1/Controls/ExFan")) 
      cmd_ExFan = (fbdo.stringData() == "ON");
    if (Firebase.getString(fbdo, "/Device-1/Controls/Cooling")) 
      cmd_Cooling = (fbdo.stringData() == "ON");
    if (Firebase.getString(fbdo, "/Device-1/Controls/Sprinkler")) 
      cmd_Sprinkler = (fbdo.stringData() == "ON");
    if (Firebase.getString(fbdo, "/Device-1/Controls/SecurityMode")) 
      cmd_Security = (fbdo.stringData() == "ARMED");

    // 3. SAFETY LOGIC (Overrides Manual OFF)
    bool need_Exhaust = (mq135 > GAS_LIMIT || mq2 > GAS_LIMIT);
    bool need_Cooling = (t > TEMP_COOL_LIMIT);
    bool need_Sprinkler = (t > TEMP_FIRE_LIMIT && mq2 > (GAS_LIMIT / 2));

    // 4. THEFT LOGIC
    bool theft_Alert = (cmd_Security && pir == HIGH);

    // 5. DECIDE RELAY STATE (Manual ON OR Safety ON)
    bool state_ExFan = cmd_ExFan || need_Exhaust;
    bool state_Cooling = cmd_Cooling || need_Cooling;
    bool state_Sprinkler = cmd_Sprinkler || need_Sprinkler;

    // 6. ACTUATE
    digitalWrite(RELAY_EXHAUST, state_ExFan ? LOW : HIGH);
    digitalWrite(RELAY_FAN, state_Cooling ? LOW : HIGH);
    digitalWrite(RELAY_PUMP, state_Sprinkler ? LOW : HIGH);

    // 7. SYNC WITH FIREBASE
    Firebase.setFloat(fbdo, "/Device-1/Status/Temp", t);
    Firebase.setFloat(fbdo, "/Device-1/Status/Hum", h);
    Firebase.setInt(fbdo, "/Device-1/Status/MQ-135", mq135);
    Firebase.setInt(fbdo, "/Device-1/Status/MQ-2", mq2);
    Firebase.setInt(fbdo, "/Device-1/Status/Vibration", vib);
    Firebase.setInt(fbdo, "/Device-1/Status/PIR", pir);
    
    // Update Alerts
    Firebase.setBool(fbdo, "/Device-1/Alert/Theft", theft_Alert);
    
    // Feedback: If Safety forced a fan ON, update the database so the dashboard switch flips ON too
    if (need_Exhaust && !cmd_ExFan) Firebase.setString(fbdo, "/Device-1/Controls/ExFan", "ON");
    if (need_Cooling && !cmd_Cooling) Firebase.setString(fbdo, "/Device-1/Controls/Cooling", "ON");
    if (need_Sprinkler && !cmd_Sprinkler) Firebase.setString(fbdo, "/Device-1/Controls/Sprinkler", "ON");

    lastCycle = millis();
  }
}