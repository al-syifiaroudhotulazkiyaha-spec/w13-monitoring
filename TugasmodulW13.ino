#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// Kredensial WiFi & ThingSpeak
const char* WIFI_SSID = "Wifi elek";
const char* WIFI_PASS = "suksesduniaakherat77";
const char* API_KEY   = "1T0D87J9SX8QZ75A";

// Konfigurasi pin sensor
const int PIN_PIR  = D0;
const int PIN_TRIG = D5;
const int PIN_ECHO = D6;

// Parameter rentang operasi bersama & histeresis (L4 & L5)
const float D_MIN    = 20.0f;
const float D_MAX    = 60.0f;
const float ALRM_ON  = 70.0f;
const float ALRM_OFF = 60.0f;

Adafruit_SSD1306 display(128, 64, &Wire, -1);

bool alarmState = false;
unsigned long lastReadMs = 0;
unsigned long lastSendMs = 0;
const unsigned long INTERVAL_KIRIM = 15500UL; // 15,5 detik

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
    Serial.println("OLED tidak terdeteksi (cek alamat 0x3C/0x3D)");
    while (true) delay(100);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Menghubungkan WiFi...");
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("\nMenghubungkan ke WiFi");
  unsigned long tMulai = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - tMulai < 15000UL)) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] TERHUBUNG! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] GAGAL TERHUBUNG! Pastikan hotspot 'Wifi elek' aktif (2.4 GHz).");
  }
}

void loop() {
  unsigned long now = millis();

  // 1. Pembacaan lokal tiap 500 ms
  if (now - lastReadMs >= 500UL) {
    lastReadMs = now;

    int pir = digitalRead(PIN_PIR);
    float d = bacaJarakCM();
    bool valid = !isnan(d);
    bool dalamRentang = valid && (d >= D_MIN) && (d <= D_MAX);

    float K = dalamRentang ? ((D_MAX - d) / (D_MAX - D_MIN)) * 100.0f : 0.0f;

    if (!dalamRentang || pir == LOW) {
      alarmState = false;
    } else if (!alarmState && K >= ALRM_ON) {
      alarmState = true;
    } else if (alarmState && K < ALRM_OFF) {
      alarmState = false;
    }

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

    Serial.printf("PIR=%d | d=%s | K=%.1f%% | Rentang=%d | Alarm=%s\n",
                  pir, valid ? String(d, 1).c_str() : "INVALID",
                  K, dalamRentang ? 1 : 0, alarmState ? "ON" : "OFF");
  }

  // 2. Pengiriman ke ThingSpeak tiap 15,5 detik (urutan Field 1-5 sesuai tabel)
  if (now - lastSendMs >= INTERVAL_KIRIM) {
    unsigned long selisihMs = now - lastSendMs;
    lastSendMs = now;

    if (WiFi.status() == WL_CONNECTED) {
      int pir = digitalRead(PIN_PIR);
      float d = bacaJarakCM();
      bool valid = !isnan(d);
      bool dalamRentang = valid && (d >= D_MIN) && (d <= D_MAX);
      float K = dalamRentang ? ((D_MAX - d) / (D_MAX - D_MIN)) * 100.0f : 0.0f;

      WiFiClient client;
      HTTPClient http;
      String url = "http://api.thingspeak.com/update?api_key=" + String(API_KEY) +
                   "&field1=" + String(valid ? d : -1.0f, 1) +   // Field 1: Jarak (cm)
                   "&field2=" + String(pir) +                    // Field 2: PIR (0/1)
                   "&field3=" + String(alarmState ? 1 : 0) +     // Field 3: Alarm (0/1)
                   "&field4=" + String(K, 1) +                   // Field 4: Kedekatan (%)
                   "&field5=" + String(dalamRentang ? 1 : 0);    // Field 5: Rentang valid (0/1)

      http.begin(client, url);
      int httpCode = http.GET();
      String entryId = http.getString();
      http.end();

      Serial.println("------------------------------------------------------------");
      Serial.printf("[ThingSpeak] Status HTTP: %d | Interval kirim: %.1f detik | Entry ID: %s\n",
                    httpCode, selisihMs / 1000.0f, entryId.c_str());
      Serial.println("------------------------------------------------------------");
    } else {
      Serial.printf("[ThingSpeak] GAGAL KIRIM (WiFi terputus) | Interval cek: %.1f detik\n",
                    selisihMs / 1000.0f);
    }
  }
}