// ============================================================
//  Smart Garden v1.0 – Firebase Connection Diagnostic Tool
//  Gunakan file ini KHUSUS untuk mendiagnosa koneksi jaringan
//  dan koneksi SSL/HTTPS ke Firebase.
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ── WiFi Credentials (GANTI DENGAN WIFI ANDA) ────────────────
#define WIFI_SSID        "Affan"
#define WIFI_PASSWORD    "12345678"

// ── Firebase URL ─────────────────────────────────────────────
#define FIREBASE_URL     "smartgardenaffan-default-rtdb.asia-southeast1.firebasedatabase.app"

void printHeader(const char* title) {
  Serial.println("\n==================================================");
  Serial.printf("  %s\n", title);
  Serial.println("==================================================");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  
  printHeader("DIAGNOSTIC START: WIFI & FIREBASE");
  
  // ───────────────────────────────────────────────────────────
  //  LANGKAH 1: DIAGNOSA HARDWARE & MEMORY
  // ───────────────────────────────────────────────────────────
  printHeader("LANGKAH 1: MEMORY & DEVICE INFO");
  Serial.printf("[INFO] ESP32 Chip Model: %s\n", ESP.getChipModel());
  Serial.printf("[INFO] Free Heap (RAM): %d bytes\n", ESP.getFreeHeap());
  Serial.printf("[INFO] Min Free Heap: %d bytes\n", ESP.getMinFreeHeap());
  Serial.printf("[INFO] Max Alloc Heap Block: %d bytes\n", ESP.getMaxAllocHeap());

  // ───────────────────────────────────────────────────────────
  //  LANGKAH 2: KONEKSI WIFI & DETAIL IP
  // ───────────────────────────────────────────────────────────
  printHeader("LANGKAH 2: MENGHUBUNGKAN KE WIFI");
  Serial.printf("[WIFI] SSID: %s\n", WIFI_SSID);
  
  WiFi.disconnect(true);
  delay(1000);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000);
    Serial.printf("[WIFI] Status: %d (Mencoba menghubungkan %ds...)\n", WiFi.status(), attempts + 1);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Terhubung dengan Sukses!");
    Serial.printf("[WIFI] IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WIFI] Subnet Mask: %s\n", WiFi.subnetMask().toString().c_str());
    Serial.printf("[WIFI] Gateway IP: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("[WIFI] DNS 1 IP: %s\n", WiFi.dnsIP(0).toString().c_str());
    Serial.printf("[WIFI] DNS 2 IP: %s\n", WiFi.dnsIP(1).toString().c_str());
    Serial.printf("[WIFI] RSSI (Sinyal): %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("\n[WIFI] Gagal terhubung ke WiFi!");
    Serial.println("[!] Tolong periksa SSID & Password WiFi Anda.");
    Serial.println("[!] Pastikan Hotspot Anda aktif dan memancarkan sinyal 2.4 GHz.");
    return;
  }

  // ───────────────────────────────────────────────────────────
  //  LANGKAH 3: PING / TEST INTERNET UMUM (DNS RESOLUTION)
  // ───────────────────────────────────────────────────────────
  printHeader("LANGKAH 3: RESOLUSI DNS (MENGUJI INTERNET)");
  
  IPAddress testIP;
  // Test resolve DNS google
  Serial.print("[DNS] Menerjemahkan google.com... ");
  int err = WiFi.hostByName("google.com", testIP);
  if (err == 1) {
    Serial.printf("SUKSES -> IP: %s\n", testIP.toString().c_str());
  } else {
    Serial.println("GAGAL!");
    Serial.println("[!] ESP32 tidak dapat menerjemahkan DNS. Berarti WiFi terhubung tetapi TIDAK ADA INTERNET.");
  }
  
  // Test resolve DNS Firebase
  Serial.printf("[DNS] Menerjemahkan %s... ", FIREBASE_URL);
  err = WiFi.hostByName(FIREBASE_URL, testIP);
  if (err == 1) {
    Serial.printf("SUKSES -> IP: %s\n", testIP.toString().c_str());
  } else {
    Serial.println("GAGAL!");
    Serial.println("[!] ESP32 gagal mengenali alamat domain Firebase Anda!");
  }

  // ───────────────────────────────────────────────────────────
  //  LANGKAH 4: TESTING HTTP TANPA SSL (HTTP PORT 80)
  // ───────────────────────────────────────────────────────────
  printHeader("LANGKAH 4: MENGUJI KONEKSI HTTP (PORT 80 - NON-SSL)");
  
  WiFiClient plainClient;
  HTTPClient plainHttp;
  
  Serial.println("[HTTP] Mencoba GET ke http://httpbin.org/ip...");
  plainHttp.begin(plainClient, "http://httpbin.org/ip");
  int httpCode = plainHttp.GET();
  if (httpCode > 0) {
    Serial.printf("[HTTP] Sukses, HTTP Code: %d\n", httpCode);
    String payload = plainHttp.getString();
    Serial.printf("[HTTP] Payload: %s\n", payload.c_str());
  } else {
    Serial.printf("[HTTP] Gagal, Error: %s\n", plainHttp.errorToString(httpCode).c_str());
    Serial.println("[!] Jika ini gagal, jaringan WiFi Anda memblokir koneksi HTTP standar.");
  }
  plainHttp.end();

  // ───────────────────────────────────────────────────────────
  //  LANGKAH 5: TESTING KONEKSI TCP SECURE (HTTPS PORT 443)
  // ───────────────────────────────────────────────────────────
  printHeader("LANGKAH 5: MENGUJI KONEKSI TCP SECURE (PORT 443)");
  
  WiFiClientSecure secureClient;
  secureClient.setInsecure(); // Bypass SSL verification untuk test
  
  Serial.printf("[TCP] Mencoba koneksi socket ke %s:%d...\n", FIREBASE_URL, 443);
  unsigned long startConnect = millis();
  if (secureClient.connect(FIREBASE_URL, 443)) {
    Serial.printf("[TCP] SUKSES Terkoneksi ke Firebase Port 443! Waktu: %dms\n", (int)(millis() - startConnect));
    secureClient.stop();
  } else {
    Serial.println("[TCP] GAGAL Terkoneksi!");
    // Coba cek error kode mbedtls jika ada log otomatis
    Serial.println("[!] Koneksi socket gagal. Port 443 mungkin diblokir oleh ISP/Router atau domain salah.");
  }

  // ───────────────────────────────────────────────────────────
  //  LANGKAH 6: TESTING GET DATA FIREBASE (REST HTTPS GET)
  // ───────────────────────────────────────────────────────────
  printHeader("LANGKAH 6: MENGUJI REST API GET FIREBASE");
  
  HTTPClient secureHttp;
  String getUrl = String("https://") + FIREBASE_URL + "/control.json";
  Serial.printf("[HTTPS GET] Mengirim request ke: %s\n", getUrl.c_str());
  
  secureHttp.begin(secureClient, getUrl);
  int secureHttpCode = secureHttp.GET();
  
  if (secureHttpCode > 0) {
    Serial.printf("[HTTPS GET] Sukses, HTTP Code: %d\n", secureHttpCode);
    String payload = secureHttp.getString();
    Serial.printf("[HTTPS GET] Payload Data: %s\n", payload.c_str());
  } else {
    Serial.printf("[HTTPS GET] Gagal, Error: %s\n", secureHttp.errorToString(secureHttpCode).c_str());
  }
  secureHttp.end();

  // ───────────────────────────────────────────────────────────
  //  LANGKAH 7: TESTING WRITE DATA FIREBASE (REST HTTPS PATCH)
  // ───────────────────────────────────────────────────────────
  printHeader("LANGKAH 7: MENGUJI REST API PATCH FIREBASE (MENULIS DATA)");
  
  String patchUrl = String("https://") + FIREBASE_URL + "/.json";
  Serial.printf("[HTTPS PATCH] Mengirim request ke: %s\n", patchUrl.c_str());
  
  String testPayload = "{\"status\":{\"diagnostic\":\"SUCCESS\", \"timestamp\":" + String(millis() / 1000) + "}}";
  Serial.printf("[HTTPS PATCH] Payload body: %s\n", testPayload.c_str());
  
  secureHttp.begin(secureClient, patchUrl);
  int patchHttpCode = secureHttp.PATCH(testPayload);
  
  if (patchHttpCode > 0) {
    Serial.printf("[HTTPS PATCH] Sukses, HTTP Code: %d\n", patchHttpCode);
    String payload = secureHttp.getString();
    Serial.printf("[HTTPS PATCH] Response: %s\n", payload.c_str());
  } else {
    Serial.printf("[HTTPS PATCH] Gagal, Error: %s\n", secureHttp.errorToString(patchHttpCode).c_str());
  }
  secureHttp.end();
  
  printHeader("DIAGNOSIS SELESAI");
  Serial.println("[INFO] Silakan analisa langkah mana yang gagal.");
  Serial.println("[INFO] Salin hasil output ini untuk dianalisa bersama.");
}

void loop() {
  // Hanya berdiam diri, tidak perlu loop agar monitor bersih
  delay(1000);
}
