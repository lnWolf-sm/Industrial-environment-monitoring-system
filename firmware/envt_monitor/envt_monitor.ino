#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <AM2302-Sensor.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>
#include <time.h>

// --- FreeRTOS Includes ---
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ============================================================
// 1. CONFIGURATION
// ============================================================
#define WIFI_SSID       "19-20"
#define WIFI_PASSWORD   "90599934"
#define API_KEY         "AIzaSyDO3vqJGU-DfvdbNxN9wYj_7yzVQMtOAcY"
#define DATABASE_URL    "environment-monitoring-s-1885c-default-rtdb.asia-southeast1.firebasedatabase.app"

// ============================================================
// 2. HARDWARE PINS
// ============================================================
#define PIN_DHT   33
#define PIN_MQ135 34
#define PIN_MQ2   35
#define PIN_VIB   23
#define PIN_PIR   18

#define RELAY_FAN     16  
#define RELAY_PUMP    17  
#define RELAY_EXHAUST 19 
#define ONBOARD_LED   2   // Restored Wi-Fi Status LED

#define LCD_ADDR 0x27
#define LCD_COLS 16
#define LCD_ROWS 2
#define SDA_PIN  21
#define SCL_PIN  22

// ============================================================
// 3. CONSTANTS & AI VARIABLES
// ============================================================
const int   GAS_LIMIT       = 1200;    
const float TEMP_FIRE_LIMIT = 45.0; // Left here so nothing else breaks, but no longer used for the sprinkler   

const unsigned long CONTROL_INTERVAL  = 2000;  // Read Firebase/Actuate every 2s
const unsigned long FIREBASE_INTERVAL = 5000;  // Sync Status every 5s

double running_avg_temp = 25.0;        // Base for AI calculation
double dynamic_cool_limit = 28.0;      // Dynamic threshold (starts at 28.0 default)

// ============================================================
// 4. OBJECTS & RTOS HANDLES
// ============================================================
AM2302::AM2302_Sensor am2302{PIN_DHT};
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

SemaphoreHandle_t i2cMutex;
SemaphoreHandle_t dataMutex;
TaskHandle_t TaskCore0Handle;

// ============================================================
// 5. SHARED GLOBALS (Protected by dataMutex)
// ============================================================
float g_temp = 0, g_hum = 0;
int   g_mq135 = 0, g_mq2 = 0, g_vib = 0, g_pir = 0;

bool g_lcd_theftAlert    = false;
bool g_lcd_needSprinkler = false;
bool g_lcd_needExhaust   = false;
bool g_lcd_cmdSecurity   = false;
bool g_lcd_finalCooling  = false;

bool firebaseReady = false;
bool signupOK      = false;
unsigned long lastControlTime  = 0;
unsigned long lastFirebaseTime = 0;

// ============================================================
// AI THRESHOLD MATH FUNCTIONS
// ============================================================
int get_day_of_year(int year, int month, int day) {
    static const int month_days[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (month < 1 || month > 12 || day < 1 || day > 31) return -1;
    int is_leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;
    int doy = 0;
    for (int i = 0; i < month - 1; i++) doy += month_days[i];
    if (is_leap && month > 2) doy += 1;
    if (is_leap && month == 2 && day == 29) doy += 1;
    doy += day;
    return doy;
}

double calculate_threshold(int year, int month, int day, double temp, double humidity, double avg_temp) {
    int doy = get_day_of_year(year, month, day);
    if (doy < 0) return -1.0; 
    double sin_day = sin(2.0 * M_PI * doy / 365.25);
    double cos_day = cos(2.0 * M_PI * doy / 365.25);
    return (-0.0014 * temp) + (-0.0349 * humidity) + (1.0018 * avg_temp) + (-0.0131 * sin_day) + (0.0021 * cos_day) + 3.8528;
}

// ============================================================
// HELPER FUNCTIONS
// ============================================================
void setRelay(int pin, bool active) {
  digitalWrite(pin, active ? LOW : HIGH);
}

void lcdPrint(int col, int row, const String &msg) {
  if (xSemaphoreTake(i2cMutex, portMAX_DELAY)) {
    lcd.setCursor(col, row);
    String padded = msg;
    while ((int)padded.length() < LCD_COLS - col) padded += ' ';
    lcd.print(padded.substring(0, LCD_COLS - col));
    xSemaphoreGive(i2cMutex);
  }
}

// ============================================================
// TASK: CORE 0 - Sensors & Display Only
// ============================================================
void TaskSensorsLCD(void *pvParameters) {
  for (;;) {
    auto am2302_status = am2302.read();
    float t = am2302.get_Temperature();
    float h = am2302.get_Humidity();
    int mq135 = analogRead(PIN_MQ135);
    int mq2   = analogRead(PIN_MQ2);
    int vib   = digitalRead(PIN_VIB);
    int pir   = digitalRead(PIN_PIR);

    bool loc_theft = false, loc_sprink = false, loc_exh = false;
    bool loc_sec = false, loc_cool = false;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
      if (am2302_status == AM2302::AM2302_READ_OK) {
        if (!isnan(t)) g_temp = t;
        if (!isnan(h)) g_hum  = h;
      }
      g_mq135 = mq135; g_mq2 = mq2;
      g_vib = vib;     g_pir = pir;

      loc_theft  = g_lcd_theftAlert;
      loc_sprink = g_lcd_needSprinkler;
      loc_exh    = g_lcd_needExhaust;
      loc_sec    = g_lcd_cmdSecurity;
      loc_cool   = g_lcd_finalCooling;
      xSemaphoreGive(dataMutex); 
    }

    if (loc_theft) {
      lcdPrint(0, 0, "!! SECURITY !!");
      lcdPrint(0, 1, "INTRUDER ALERT!");
    } else if (loc_sprink) {
      lcdPrint(0, 0, "!! DANGER !!");
      lcdPrint(0, 1, "FIRE DETECTED!");
    } else if (loc_exh) {
      lcdPrint(0, 0, "!! DANGER !!");
      lcdPrint(0, 1, "GAS DETECTED!");
    } else {
      String row0 = "T:" + String(g_temp, 1) + "C H:" + String(g_hum, 1) + "%";
      lcdPrint(0, 0, row0);

      String row1;
      if      (loc_sec)  row1 = "System ARMED";
      else if (loc_cool) row1 = "Cooling Active";
      else               row1 = "Air:" + String(g_mq135) + " raw";
      lcdPrint(0, 1, row1);
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// ============================================================
// SETUP - Runs on Core 1
// ============================================================
void setup() {
  Serial.begin(115200);

  i2cMutex  = xSemaphoreCreateMutex();
  dataMutex = xSemaphoreCreateMutex();

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcdPrint(0, 0, "System Init...");
  lcdPrint(0, 1, "");

  am2302.begin();
  pinMode(PIN_VIB, INPUT);
  pinMode(PIN_PIR, INPUT);
  pinMode(RELAY_FAN, OUTPUT);
  pinMode(RELAY_PUMP, OUTPUT);
  pinMode(RELAY_EXHAUST, OUTPUT);
  pinMode(ONBOARD_LED, OUTPUT); 

  setRelay(RELAY_FAN, false);
  setRelay(RELAY_PUMP, false);
  setRelay(RELAY_EXHAUST, false);
  digitalWrite(ONBOARD_LED, LOW);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lcdPrint(0, 1, "WiFi Connecting.");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.println("\nWiFi Connected: " + WiFi.localIP().toString());
  lcdPrint(0, 0, "WiFi Connected!");
  digitalWrite(ONBOARD_LED, HIGH); 
  delay(1000);

  // Init Time for AI Math (GMT+6 for Bangladesh)
  configTime(6 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  if (Firebase.signUp(&config, &auth, "", "")) {
    signupOK = true;
  } else {
    config.signer.test_mode = true;
    signupOK = true;
  }
  config.token_status_callback = tokenStatusCallback;
  fbdo.setBSSLBufferSize(4096, 1024);
  fbdo.setResponseSize(4096);
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  lcdPrint(0, 0, "Firebase Ready");
  lcdPrint(0, 1, "");
  delay(1000);

  xTaskCreatePinnedToCore(
    TaskSensorsLCD, "TaskCore0", 10000, NULL, 1, &TaskCore0Handle, 0
  );
}

// ============================================================
// MAIN LOOP - CORE 1 - Network, Logic, Actuators
// ============================================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(ONBOARD_LED, LOW); 
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) delay(300);
    if(WiFi.status() == WL_CONNECTED) digitalWrite(ONBOARD_LED, HIGH);
  }

  firebaseReady = (Firebase.ready() && signupOK);

  // --- CYCLE 1: CONTROL LOGIC & MATH (Every 2 seconds) ---
  if (millis() - lastControlTime >= CONTROL_INTERVAL) {
    lastControlTime = millis();

    float loc_t, loc_h;
    int loc_mq135, loc_mq2, loc_vib, loc_pir;
    bool cmd_ExFan = false, cmd_Cooling = false, cmd_Sprinkler = false, cmd_Security = false;

    if (firebaseReady) {
      if (Firebase.RTDB.getString(&fbdo, "/Device-1/Controls/ExFan")) cmd_ExFan = (fbdo.stringData() == "ON");
      if (Firebase.RTDB.getString(&fbdo, "/Device-1/Controls/Cooling")) cmd_Cooling = (fbdo.stringData() == "ON");
      if (Firebase.RTDB.getString(&fbdo, "/Device-1/Controls/Sprinkler")) cmd_Sprinkler = (fbdo.stringData() == "ON");
      if (Firebase.RTDB.getString(&fbdo, "/Device-1/Controls/SecurityMode")) cmd_Security = (fbdo.stringData() == "ARMED");
    }

    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
      loc_t = g_temp; loc_h = g_hum;
      loc_mq135 = g_mq135; loc_mq2 = g_mq2;
      loc_vib = g_vib; loc_pir = g_pir;
      xSemaphoreGive(dataMutex);
    }

    // AI THRESHOLD CALCULATION 
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        int year = timeinfo.tm_year + 1900;
        int month = timeinfo.tm_mon + 1;
        int day = timeinfo.tm_mday;
        
        running_avg_temp = (running_avg_temp * 0.95) + (loc_t * 0.05);
        double base_calc_thresh = calculate_threshold(year, month, day, loc_t, loc_h, running_avg_temp);
        
        if (base_calc_thresh > 0) {
            dynamic_cool_limit = base_calc_thresh + 16.0; 
        }
    }

    // ==========================================
    // SAFETY & SECURITY LOGIC (UPDATED)
    // ==========================================
    bool need_Exhaust  = (loc_mq135 > GAS_LIMIT || loc_mq2 > GAS_LIMIT);
    bool need_Cooling  = (loc_t > dynamic_cool_limit); 
    
    // --> Sprinkler now uses the AI Dynamic Threshold <--
    bool need_Sprinkler = (loc_t > dynamic_cool_limit && loc_mq2 > (GAS_LIMIT / 2)); 
    
    bool theft_Alert   = (cmd_Security && (loc_pir == HIGH || loc_vib == HIGH));

    bool final_ExFan    = cmd_ExFan || need_Exhaust;
    bool final_Cooling  = cmd_Cooling || need_Cooling;
    bool final_Sprinkler = cmd_Sprinkler || need_Sprinkler;

    setRelay(RELAY_EXHAUST, final_ExFan);
    setRelay(RELAY_FAN, final_Cooling);
    setRelay(RELAY_PUMP, final_Sprinkler);

    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
      g_lcd_theftAlert    = theft_Alert;
      g_lcd_needSprinkler = final_Sprinkler; 
      g_lcd_needExhaust   = need_Exhaust;
      g_lcd_cmdSecurity   = cmd_Security;
      g_lcd_finalCooling  = final_Cooling;
      xSemaphoreGive(dataMutex);
    }

    if (firebaseReady) {
      Firebase.RTDB.setString(&fbdo, "/Device-1/Controls/ExFan", final_ExFan ? "ON" : "OFF");
      Firebase.RTDB.setString(&fbdo, "/Device-1/Controls/Cooling", final_Cooling ? "ON" : "OFF");
      Firebase.RTDB.setString(&fbdo, "/Device-1/Controls/Sprinkler", final_Sprinkler ? "ON" : "OFF");
      Firebase.RTDB.setBool(&fbdo, "/Device-1/Alert/Theft", theft_Alert);
    }
  }

  // --- CYCLE 2: FIREBASE HEARTBEAT & STATUS (Every 5 seconds) ---
  if (firebaseReady && millis() - lastFirebaseTime >= FIREBASE_INTERVAL) {
    lastFirebaseTime = millis();

    float u_t, u_h; int u_mq135, u_mq2, u_vib, u_pir;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
      u_t = g_temp; u_h = g_hum;
      u_mq135 = g_mq135; u_mq2 = g_mq2;
      u_vib = g_vib; u_pir = g_pir;
      xSemaphoreGive(dataMutex);
    }

    FirebaseJson json;
    json.set("Uptime", millis() / 1000); 
    json.set("Temp", u_t);
    json.set("Hum", u_h);
    json.set("DynThreshold", dynamic_cool_limit); 
    json.set("MQ-135", u_mq135);
    json.set("MQ-2", u_mq2);
    json.set("Vibration", u_vib);
    json.set("PIR", u_pir);
    
    Firebase.RTDB.updateNode(&fbdo, "/Device-1/Status", &json);
  }

  vTaskDelay(pdMS_TO_TICKS(10));
}