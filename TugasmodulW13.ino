/*
 * Proyek   : Sistem Monitoring Kedekatan Objek & Alarm Histeresis IoT
 * Board    : NodeMCU ESP8266 (ESP-12E)
 * Sensor   : PIR (D0), HC-SR04 (TRIG=D5, ECHO=D6), OLED I2C 128x64 (SDA=D2, SCL=D1)
 * Cloud    : ThingSpeak (Field 1 - Field 5)
 * Parameter: D_min = 20.0 cm, D_max = 60.0 cm, Alarm ON >= 70%, Alarm OFF < 60%
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// Kredensial WiFi & ThingSpeak (gunakan placeholder saat unggah ke GitHub publik)
const char* WIFI_SSID = "NAMA_WIFI_ANDA";
const char* WIFI_PASS = "PASSWORD_WIFI_ANDA";
const char* API_KEY   = "WRITE_API_KEY_THINGSPEAK";

// Konfigurasi pin sensor
const int PIN_PIR  = D0;
const int PIN_TRIG = D5;
const int PIN_ECHO = D6;

// Parameter rentang operasi bersama & histeresis (hasil Lembar L4 & L5)
const float D_MIN    = 20.0f;
const float D_MAX    = 60.0f;
const float ALRM_ON  = 70.0f;
const float ALRM_OFF = 60.0f;

Adafruit_SSD1306 display(128, 64, &Wire, -1);

bool alarmState = false;
unsigned long lastReadMs = 0;
unsigned long lastSendMs = 0;

float bacaJarakCM() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(12);
  digitalWrite(PIN_TRIG, LOW);

  unsigned long us = pulseIn(PIN_ECHO, HIGH, 30000UL);
  if (us == 0) return NAN;

  float d = us * 0.0343f / 2.0f;
  return (d >= 2.0f && d <= 400.0f) ? d : NAN;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);

  Wire.begin(D2, D1);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED gagal inisialisasi (cek alamat 0x3C/0x3D)");
    while (true) delay(100);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Menghubungkan WiFi...");
  display.display();

  // Koneksi WiFi dengan batas waktu 10 detik agar mode lokal tetap berjalan jika offline
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long startWiFi = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startWiFi < 10000UL)) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi Terhubung! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi belum terhubung, sistem berjalan dalam mode lokal.");
  }
}

void loop() {
  unsigned long now = millis();

  // Siklus pembacaan sensor & pembaruan tampilan lokal tiap 500 ms
  if (now - lastReadMs >= 500UL) {
    lastReadMs = now;

    int pir = digitalRead(PIN_PIR);
    float d = bacaJarakCM();
    bool valid = !isnan(d);
    bool dalamRentang = valid && (d >= D_MIN) && (d <= D_MAX);

    // Hitung persentase kedekatan hanya jika valid dan dalam rentang bersama
    float K = dalamRentang ? ((D_MAX - d) / (D_MAX - D_MIN)) * 100.0f : 0.0f;

    // Logika histeresis alarm (ON >= 70%, OFF < 60%)
    if (!dalamRentang || pir == LOW) {
      alarmState = false;
    } else if (!alarmState && K >= ALRM_ON) {
      alarmState = true;
    } else if (alarmState && K < ALRM_OFF) {
      alarmState = false;
    }

    // Tampilkan status pada layar OLED
    display.clearDisplay();
    display.setCursor(0, 0);
    display.printf("PIR  : %d (%s)", pir, pir ? "GERAK" : "DIAM");
    display.setCursor(0, 12);
    if (valid) display.printf("Jarak: %.1f cm", d);
    else       display.print("Jarak: INVALID");
    display.setCursor(0, 24);
    display.printf("K(d) : %.1f %%", K);
    display.setCursor(0, 36);
    display.printf("Zona : %s", dalamRentang ? "DALAM RENTANG" : "LUAR RENTANG");
    display.setCursor(0, 48);
    display.printf("Alarm: %s | WiFi:%s", alarmState ? "ON" : "OFF",
                   WiFi.status() == WL_CONNECTED ? "OK" : "NO");
    display.display();

    // Tampilkan log pada Serial Monitor
    Serial.printf("PIR=%d | d=%s | K=%.1f%% | Rentang=%d | Alarm=%s\n",
                  pir, valid ? String(d, 1).c_str() : "INVALID",
                  K, dalamRentang ? 1 : 0, alarmState ? "ON" : "OFF");

    // Kirim data ke ThingSpeak tiap 15,5 detik saat WiFi terhubung
    if (now - lastSendMs >= 15500UL && WiFi.status() == WL_CONNECTED) {
      lastSendMs = now;
      WiFiClient client;
      HTTPClient http;
      String url = "http://api.thingspeak.com/update?api_key=" + String(API_KEY) +
                   "&field1=" + String(pir) +
                   "&field2=" + String(valid ? d : -1.0f, 1) +
                   "&field3=" + String(K, 1) +
                   "&field4=" + String(dalamRentang ? 1 : 0) +
                   "&field5=" + String(alarmState ? 1 : 0);
      http.begin(client, url);
      int code = http.GET();
      Serial.printf("ThingSpeak HTTP %d -> Entry: %s\n", code, http.getString().c_str());
      http.end();
    }
  }
}