// ============================================================
//  Smart Garden v1.0 – Online Firmware (Robust REST API)
//  Fully integrated with Firebase Realtime Database
//
//  Library dependencies:
//    - Firebase ESP32 Client (by Mobizt) -> Digunakan hanya untuk FirebaseJson
//    - DHT sensor library (by Adafruit)
//    - LiquidCrystal I2C (by Frank de Brabander)
// ============================================================

// ════════════════════════════════════════════════════════════
//  SECTION 1 – CONFIG
// ════════════════════════════════════════════════════════════

// ── WiFi Credentials ─────────────────────────────────────────
#define WIFI_SSID        "Affan"
#define WIFI_PASSWORD    "12345678"

// ── Firebase Credentials ─────────────────────────────────────
#define FIREBASE_API_KEY "AIzaSyDS36vzvbje14ZLAyn3jWmCuaFqCr7Szwk"
#define FIREBASE_URL     "smartgardenaffan-default-rtdb.asia-southeast1.firebasedatabase.app"

// ── Pin mapping ──────────────────────────────────────────────
#define DHT_PIN          4
#define DHT_TYPE         DHT22
#define SOIL_AO_PIN      34    // ADC1 – GPIO 34
#define RELAY_PIN        23    // Active-LOW relay (LOW = ON)

// ── LCD I2C ──────────────────────────────────────────────────
#define LCD_ADDR         0x27
#define LCD_COLS         16
#define LCD_ROWS         2

// ── Soil calibration ─────────────────────────────────────────
#define SOIL_RAW_DRY     3200  // ADC ketika kering  → 0%
#define SOIL_RAW_WET      900  // ADC ketika basah   → 100%

// ── Timing (ms) ──────────────────────────────────────────────
#define SENSOR_INTERVAL    2000UL
#define LCD_PAGE_INTERVAL  3000UL
#define PUMP_SAFETY_MS     30000UL
#define FIREBASE_INTERVAL  8000UL

// ── Default thresholds ───────────────────────────────────────
#define DEFAULT_SOIL_MIN   30
#define DEFAULT_SOIL_MAX   60
#define DEFAULT_MODE       "AUTO"

// ════════════════════════════════════════════════════════════
//  SECTION 2 – INCLUDES
// ════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Firebase_ESP_Client.h>

// ════════════════════════════════════════════════════════════
//  SECTION 3 – OBJECTS
// ════════════════════════════════════════════════════════════

DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// Menggunakan objek global untuk menghindari memory fragmentasi dan TLS Handshake overhead
WiFiClientSecure net_client;

// ════════════════════════════════════════════════════════════
//  SECTION 4 – SYSTEM STATE
// ════════════════════════════════════════════════════════════

struct SensorState {
  float temperature = 0.0f;
  float humidity    = 0.0f;
  int   soil        = 0;
  bool  healthy     = true;
} sensor;

struct ControlState {
  String mode        = DEFAULT_MODE;
  bool   pumpCommand = false;
} control;

struct Settings {
  int soilMin = DEFAULT_SOIL_MIN;
  int soilMax = DEFAULT_SOIL_MAX;
} settings;

struct StatusState {
  bool pumpState    = false;
  bool wifiConnected = false;
  bool online        = false;
} status;

unsigned long pumpOnSince = 0;
bool          safetyTripped = false;

// Timers
unsigned long lastSensorRead = 0;
unsigned long lastLcdSwitch  = 0;
unsigned long lastFirebaseIO = 0;
int           lcdPage        = 0;

// ════════════════════════════════════════════════════════════
//  SECTION 5 – PUMP CONTROL
// ════════════════════════════════════════════════════════════

void relayON() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Nyala untuk relay Active-LOW
}

void relayOFF() {
  pinMode(RELAY_PIN, INPUT); // Trik: Jadikan INPUT agar relay Active-LOW "keras kepala" 5V bisa mati total
}

void setPump(bool on) {
  if (status.pumpState == on) return;

  status.pumpState = on;
  if (on) {
    relayON();
    if (pumpOnSince == 0) pumpOnSince = millis(); 
    Serial.println("[PUMP] Perubahan State: NYALA (OUTPUT HIGH)");
  } else {
    relayOFF();
    pumpOnSince = 0; 
    Serial.println("[PUMP] Perubahan State: MATI (OUTPUT LOW)");
  }
}

// ════════════════════════════════════════════════════════════
//  SECTION 6 – SENSOR
// ════════════════════════════════════════════════════════════

int rawToSoilPercent(int raw) {
  int pct = map(raw, SOIL_RAW_DRY, SOIL_RAW_WET, 0, 100);
  return constrain(pct, 0, 100);
}

void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t) && !isnan(h)) {
    sensor.temperature = t;
    sensor.humidity    = h;
    sensor.healthy     = true;
  } else {
    sensor.healthy = false;
    Serial.println("[SENSOR] DHT22 Gagal dibaca – menjaga nilai terakhir");
  }

  int raw = analogRead(SOIL_AO_PIN);
  sensor.soil = rawToSoilPercent(raw);

  Serial.printf("[SENSOR] T=%.1f°C H=%.1f%% Soil=%d%% Healthy=%s\n",
    sensor.temperature, sensor.humidity, sensor.soil,
    sensor.healthy ? "YA" : "TIDAK");
}

// ════════════════════════════════════════════════════════════
//  SECTION 7 – PUMP LOGIC
// ════════════════════════════════════════════════════════════

void checkSafetyTimeout() {
  if (status.pumpState && pumpOnSince > 0) {
    if (millis() - pumpOnSince >= PUMP_SAFETY_MS) {
      Serial.println("[SAFETY] Pompa hidup > 30 detik – dimatikan paksa");
      safetyTripped = true;
      setPump(false);
    }
  }
}

void evaluateAutoMode() {
  if (sensor.soil < settings.soilMin) {
    if (!status.pumpState)
      Serial.printf("[AUTO] Soil %d%% < soilMin %d → Pompa NYALA\n", sensor.soil, settings.soilMin);
    setPump(true);
  } else if (sensor.soil > settings.soilMax) {
    if (status.pumpState)
      Serial.printf("[AUTO] Soil %d%% > soilMax %d → Pompa MATI\n", sensor.soil, settings.soilMax);
    setPump(false);
  }
}

void evaluateManualMode() {
  setPump(control.pumpCommand);
}

void evaluatePumpLogic() {
  if (safetyTripped) {
    safetyTripped = false;
    return;
  }

  if (control.mode == "AUTO") {
    evaluateAutoMode();
  } else {
    evaluateManualMode();
  }
}

// ════════════════════════════════════════════════════════════
//  SECTION 8 – MODE TRANSITION
// ════════════════════════════════════════════════════════════

void onModeChanged(const String& oldMode, const String& newMode) {
  Serial.printf("[MODE] %s → %s\n", oldMode.c_str(), newMode.c_str());
  if (newMode == "AUTO") {
    evaluateAutoMode();
  }
}

// ════════════════════════════════════════════════════════════
//  SECTION 9 – LCD
// ════════════════════════════════════════════════════════════

void updateLCD() {
  lcd.clear();
  if (lcdPage == 0) {
    lcd.setCursor(0, 0);
    lcd.printf("T:%.1fC H:%.0f%%", sensor.temperature, sensor.humidity);
    lcd.setCursor(0, 1);
    lcd.printf("Soil:%d%% %s", sensor.soil, status.online ? "ON" : "OF");
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Mode:");
    lcd.print(control.mode);
    lcd.setCursor(0, 1);
    lcd.print("Pump:");
    lcd.print(status.pumpState ? "ON" : "OFF");
  }
}

// ════════════════════════════════════════════════════════════
//  SECTION 10 – FIREBASE IMPLEMENTATION (ROBUST)
// ════════════════════════════════════════════════════════════

// Fungsi helper REST API dengan mekanisme Retry dan Exponential Backoff
String firebaseREST(const String& method, const String& path, const String& payload = "") {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[REST] Request dibatalkan: WiFi tidak terhubung");
    return "";
  }

  HTTPClient http;
  String url = String("https://") + FIREBASE_URL + path;
  http.setReuse(true); // Meminta koneksi agar tetap terbuka
  http.setTimeout(8000); // Timeout 8 detik agar tidak block lama

  int maxRetries = 3;
  int retryDelay = 1000; // Mulai dengan jeda 1 detik
  
  for (int attempt = 1; attempt <= maxRetries; attempt++) {

    if (!http.begin(net_client, url)) {
      Serial.println("[REST] Gagal inisialisasi HTTPClient");
      return "";
    }

    http.addHeader("Content-Type", "application/json");

    int httpCode = 0;
    if (method == "GET") {
      httpCode = http.GET();
    } else if (method == "PATCH") {
      httpCode = http.PATCH(payload);
    }

    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        http.end();
        return response; // BERHASIL
      } else {
        Serial.printf("[REST] Attempt %d Gagal HTTP Code: %d (%s)\n", attempt, httpCode, http.errorToString(httpCode).c_str());
      }
    } else {
      Serial.printf("[REST] Attempt %d Koneksi Error: %s\n", attempt, http.errorToString(httpCode).c_str());
      // Dump SSL Error jika tersedia (Berguna untuk melihat apakah sertifikat / TLS bermasalah)
      char errorBuf[100];
      if (net_client.lastError(errorBuf, 100) < 0) {
         Serial.printf("[SSL_ERROR] %s\n", errorBuf);
      }
    }
    
    http.end(); // Tutup sebelum retry
    
    // Jika masih ada sisa attempt, tunggu dengan exponential backoff
    if (attempt < maxRetries) {
      Serial.printf("[REST] Retrying in %d ms...\n", retryDelay);
      delay(retryDelay);
      retryDelay *= 2; // Exponential backoff (1s, 2s, 4s)
    }
  }

  Serial.println("[REST] Semua percobaan request gagal.");
  return "";
}

void initFirebase() {
  lcd.clear();
  lcd.print("WiFi Connecting");
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WIFI] Connecting to WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    status.wifiConnected = true;
    Serial.println("\n[WIFI] Terhubung! IP: " + WiFi.localIP().toString());
    lcd.clear();
    lcd.print("WiFi Connected");
    delay(1000);
  } else {
    status.wifiConnected = false;
    Serial.println("\n[WIFI] Gagal terhubung ke WiFi, menjalankan mode lokal");
    lcd.clear();
    lcd.print("WiFi Failed");
    delay(1000);
  }

  // Setup Global SSL Client
  net_client.setInsecure(); // Bypass Verifikasi Sertifikat
  net_client.setTimeout(8); // Timeout 8 detik pada level TCP/SSL

  Serial.printf("[SYSTEM] Memori RAM bebas (Heap): %d bytes\n", ESP.getFreeHeap());
  Serial.println("[FIREBASE] Menggunakan Mode REST API (Robust Version)");
}

void uploadToFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;

  FirebaseJson json;
  
  // Set sensors data
  json.set("sensor/temperature", sensor.temperature);
  json.set("sensor/humidity", sensor.humidity);
  json.set("sensor/soil", sensor.soil);

  // Set statuses data
  json.set("status/pumpState", status.pumpState);
  json.set("status/wifiConnected", status.wifiConnected);
  json.set("status/sensorHealthy", sensor.healthy);
  json.set("status/online", true);
  json.set("status/mode", control.mode);
  json.set("status/lastUpdate", (int)(millis() / 1000));

  String payload;
  json.toString(payload, false);

  String res = firebaseREST("PATCH", "/.json", payload);
  if (res.length() > 0) {
    status.online = true;
    Serial.println("[FIREBASE] Sukses upload data sensor & status");
  } else {
    status.online = false;
    Serial.println("[FIREBASE] Gagal upload");
  }
}

void readFirebaseControl() {
  if (WiFi.status() != WL_CONNECTED) return;

  // Baca seluruh data root dalam satu request untuk menghindari banyak socket terbuka
  String res = firebaseREST("GET", "/.json");
  if (res.length() > 0) {
    FirebaseJson json;
    json.setJsonData(res);
    FirebaseJsonData data;
    
    if (json.get(data, "control/mode") && data.success) {
      String newMode = data.stringValue;
      newMode.toUpperCase();
      if (newMode == "AUTO" || newMode == "MANUAL") {
        String oldMode = control.mode;
        if (oldMode != newMode) {
          control.mode = newMode;
          onModeChanged(oldMode, newMode);
        }
      }
    }

    if (json.get(data, "control/pumpCommand") && data.success) {
      control.pumpCommand = data.boolValue;
    }

    if (json.get(data, "setting/soilMin") && data.success) {
      settings.soilMin = data.intValue;
    }

    if (json.get(data, "setting/soilMax") && data.success) {
      settings.soilMax = data.intValue;
    }
    status.online = true; // Data berhasil ditarik = sistem masih terhubung
  } else {
    status.online = false;
    Serial.println("[FIREBASE] Gagal baca data dari Firebase");
  }
}

void handleWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    status.wifiConnected = false;
    status.online = false;
    
    static unsigned long lastReconnectAttempt = 0;
    unsigned long now = millis();
    if (now - lastReconnectAttempt >= 10000) {
      lastReconnectAttempt = now;
      Serial.println("[WIFI] Reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      Serial.printf("[SYSTEM] WDT Reset dalam Handle WiFi. Free Heap: %d bytes\n", ESP.getFreeHeap());
    }
  } else {
    status.wifiConnected = true;
  }
}

// ════════════════════════════════════════════════════════════
//  SECTION 11 – SERIAL COMMAND
// ════════════════════════════════════════════════════════════

String serialBuffer = "";

void parseSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      serialBuffer.trim();
      serialBuffer.toLowerCase();

      if (serialBuffer.startsWith("mode ")) {
        String newMode = serialBuffer.substring(5);
        newMode.trim();
        if (newMode == "auto" || newMode == "manual") {
          newMode.toUpperCase();
          String oldMode = control.mode;
          if (oldMode != newMode) {
            control.mode = newMode;
            onModeChanged(oldMode, newMode);
          }
          Serial.printf("[CMD] Mode set to %s\n", control.mode.c_str());
        }

      } else if (serialBuffer == "pump on") {
        control.pumpCommand = true;
        Serial.println("[CMD] pumpCommand = true");

      } else if (serialBuffer == "pump off") {
        control.pumpCommand = false;
        Serial.println("[CMD] pumpCommand = false");

      } else if (serialBuffer.startsWith("soil ")) {
        int val = serialBuffer.substring(5).toInt();
        sensor.soil = constrain(val, 0, 100);
        Serial.printf("[CMD] soil overridden to %d%%\n", sensor.soil);

      } else if (serialBuffer == "status") {
        Serial.println("─────────────────────────");
        Serial.printf("  Mode       : %s\n",   control.mode.c_str());
        Serial.printf("  pumpCommand: %s\n",   control.pumpCommand ? "true" : "false");
        Serial.printf("  pumpState  : %s\n",   status.pumpState    ? "ON"   : "OFF");
        Serial.printf("  soilMin    : %d\n",   settings.soilMin);
        Serial.printf("  soilMax    : %d\n",   settings.soilMax);
        Serial.printf("  Temperature: %.1f°C\n", sensor.temperature);
        Serial.printf("  Humidity   : %.1f%%\n", sensor.humidity);
        Serial.printf("  Soil       : %d%%\n", sensor.soil);
        Serial.printf("  Free Heap  : %d bytes\n", ESP.getFreeHeap());
        Serial.println("─────────────────────────");
      }

      serialBuffer = "";
    } else {
      serialBuffer += c;
    }
  }
}

// ════════════════════════════════════════════════════════════
//  SECTION 12 – INIT HARDWARE
// ════════════════════════════════════════════════════════════

void initHardware() {
  relayOFF();
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Smart Garden v1");
  lcd.setCursor(0, 1);
  lcd.print("Booting...");

  dht.begin();
  Serial.println("[BOOT] Hardware initialized");
}

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Smart Garden v1.0 (Robust Online) ===");


  initHardware();
  initFirebase();

  Serial.printf("[BOOT] Mode=%s soilMin=%d soilMax=%d\n",
                control.mode.c_str(), settings.soilMin, settings.soilMax);

  delay(1000);
  lcd.clear();
  lcd.print("Boot OK");
  delay(800);
  lcd.clear();
}

// ════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════

void loop() {
  unsigned long now = millis();

  // 1. Tangani WiFi reconnect jika terputus (Non-Blocking)
  handleWiFi();

  // 2. Tangani Serial Monitor Command
  parseSerial();

  // 3. Baca Sensor
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;
    readSensors();
  }

  // 4. Sinkronisasi Data Firebase secara berselang (Staggered Timer)
  static bool firebaseToggle = false;
  if (now - lastFirebaseIO >= FIREBASE_INTERVAL / 2) {
    lastFirebaseIO = now;
    
    // Jangan lakukan aktivitas API jika WiFi belum nyambung, biarkan handleWiFi() yang bekerja
    if (WiFi.status() == WL_CONNECTED) {
        if (firebaseToggle) {
          readFirebaseControl();
        } else {
          uploadToFirebase();
        }
        firebaseToggle = !firebaseToggle;
    }
  }

  // 5. Keselamatan dan Logika Pompa
  checkSafetyTimeout();
  evaluatePumpLogic();

  // 6. Update Layar LCD
  if (now - lastLcdSwitch >= LCD_PAGE_INTERVAL) {
    lastLcdSwitch = now;
    lcdPage = 1 - lcdPage;
    updateLCD();
  }

  delay(10);
}
