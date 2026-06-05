/**
 * SpineBot — ESP32-CAM Firmware
 *
 * Board: AI Thinker ESP32-CAM  (change platformio.ini board to "esp32cam")
 *
 * Flow:
 *   1. Capture JPEG frame → POST /detect to PC server
 *   2. Server returns { "is_slouching": bool, "is_person_present": bool }
 *   3. If is_slouching  → pulse servos 30° + POST /slouch_line → print GPT line
 *   4. If is_person_present → run 20-min sit timer → remind to move
 *
 * PC server (Python) must expose:
 *   POST /detect      — body: raw JPEG bytes
 *                       returns JSON: {"is_slouching":bool,"is_person_present":bool}
 *   POST /slouch_line — no body needed
 *                       returns JSON: {"line":"<GPT response>"}
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include "esp_camera.h"

// ─── CONFIG ──────────────────────────────────────────────────────────────────

const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// IP of the PC running the Python server — update this after connecting
const char* SERVER_HOST = "192.168.1.100";
const uint16_t SERVER_PORT = 5000;

// Servo GPIO pins (AI Thinker: GPIO 12 & 13 are free when not using SD card)
const int SERVO1_PIN = 12;   // moves clockwise on slouch
const int SERVO2_PIN = 13;   // moves counterclockwise on slouch

// Optional: active-buzzer pin for the 20-min movement reminder (LOW = off)
const int BUZZER_PIN = 4;

// Timing
const unsigned long FRAME_INTERVAL_MS        = 500;           // ~2 fps detection
const unsigned long SLOUCH_COOLDOWN_MS       = 10000UL;       // min gap between slouch alerts
const unsigned long SERVO_HOLD_MS            = 800;           // how long servos stay rotated
const unsigned long SIT_REMINDER_MS          = 20UL*60*1000;  // 20 minutes
const unsigned long REMINDER_REPEAT_MS       = 60000UL;       // re-buzz every 60 s until they move

// Servo center and travel
const int SERVO_CENTER_DEG = 90;
const int SERVO_TRAVEL_DEG = 30;

// ─── AI THINKER ESP32-CAM PINS ──────────────────────────────────────────────

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ─── GLOBALS ─────────────────────────────────────────────────────────────────

Servo servo1;
Servo servo2;

// Slouch state
unsigned long lastSlouchAlert = 0;

// Sit timer
bool        personPresent      = false;
unsigned long sitStartMs        = 0;
unsigned long lastReminderMs    = 0;
bool        reminderActive      = false;   // true once 20 min has elapsed

// ─── HELPERS ─────────────────────────────────────────────────────────────────

String buildUrl(const char* path) {
    return String("http://") + SERVER_HOST + ":" + SERVER_PORT + path;
}

void buzzReminder() {
    Serial.println("[REMINDER] You've been sitting 20 minutes — time to move!");
    digitalWrite(BUZZER_PIN, HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN, LOW);
    delay(150);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN, LOW);
}

// Attach servos, move them, then detach (saves power and stops jitter)
void pulseServos() {
    servo1.attach(SERVO1_PIN);
    servo2.attach(SERVO2_PIN);

    // Return to center first in case previous move wasn't cleaned up
    servo1.write(SERVO_CENTER_DEG);
    servo2.write(SERVO_CENTER_DEG);
    delay(300);

    // Rotate: servo1 CW (+30°), servo2 CCW (−30°)
    servo1.write(SERVO_CENTER_DEG + SERVO_TRAVEL_DEG);
    servo2.write(SERVO_CENTER_DEG - SERVO_TRAVEL_DEG);
    delay(SERVO_HOLD_MS);

    // Return to center
    servo1.write(SERVO_CENTER_DEG);
    servo2.write(SERVO_CENTER_DEG);
    delay(300);

    servo1.detach();
    servo2.detach();
}

// POST /slouch_line — returns the GPT line or empty string on failure
String fetchSlouchLine() {
    HTTPClient http;
    http.begin(buildUrl("/slouch_line"));
    http.addHeader("Content-Type", "application/json");

    int code = http.POST("{}");
    if (code != 200) {
        Serial.printf("[slouch_line] HTTP %d\n", code);
        http.end();
        return "";
    }

    String body = http.getString();
    http.end();

    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[slouch_line] JSON parse error");
        return "";
    }

    return doc["line"].as<String>();
}

// POST /detect with raw JPEG — fills out & err on success
bool postFrame(camera_fb_t* fb, bool& outSlouching, bool& outPersonPresent) {
    HTTPClient http;
    http.begin(buildUrl("/detect"));
    http.addHeader("Content-Type", "image/jpeg");
    http.setTimeout(4000);

    int code = http.POST(fb->buf, fb->len);
    if (code != 200) {
        Serial.printf("[detect] HTTP %d\n", code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[detect] JSON parse error");
        return false;
    }

    outSlouching      = doc["is_slouching"]      | false;
    outPersonPresent  = doc["is_person_present"]  | false;
    return true;
}

// ─── CAMERA INIT ─────────────────────────────────────────────────────────────

bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    // Use lower resolution to keep frames small and HTTP fast
    config.frame_size   = FRAMESIZE_QVGA;   // 320×240
    config.jpeg_quality = 12;               // 0–63, lower = better
    config.fb_count     = 1;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[camera] init failed: 0x%x\n", err);
        return false;
    }

    // Flip if camera is mounted upside-down
    sensor_t* s = esp_camera_sensor_get();
    s->set_vflip(s, 1);
    s->set_hmirror(s, 0);

    return true;
}

// ─── WIFI ────────────────────────────────────────────────────────────────────

void connectWiFi() {
    Serial.printf("[wifi] Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[wifi] Connected — IP: %s\n", WiFi.localIP().toString().c_str());
}

// ─── SETUP ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== SpineBot ===");

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    if (!initCamera()) {
        Serial.println("[FATAL] Camera init failed — halting");
        while (true) delay(1000);
    }

    connectWiFi();

    Serial.println("[setup] Ready.");
}

// ─── LOOP ────────────────────────────────────────────────────────────────────

void loop() {
    static unsigned long lastFrameMs = 0;
    unsigned long now = millis();

    // ── WiFi watchdog ───────────────────────────────────────────────────────
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[wifi] Lost connection — reconnecting...");
        connectWiFi();
        return;
    }

    // ── Capture & detect at FRAME_INTERVAL_MS ───────────────────────────────
    if (now - lastFrameMs >= FRAME_INTERVAL_MS) {
        lastFrameMs = now;

        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("[camera] Frame capture failed");
            return;
        }

        bool isSlouching     = false;
        bool isPersonPresent = false;

        bool ok = postFrame(fb, isSlouching, isPersonPresent);
        esp_camera_fb_return(fb);

        if (!ok) return;   // HTTP error already printed

        Serial.printf("[detect] person=%d  slouching=%d\n", isPersonPresent, isSlouching);

        // ── Person-present sit timer ─────────────────────────────────────────
        if (isPersonPresent) {
            if (!personPresent) {
                // Person just sat down — start timer
                personPresent   = true;
                sitStartMs      = now;
                reminderActive  = false;
                lastReminderMs  = 0;
                Serial.println("[timer] Person detected — sit timer started");
            }

            unsigned long elapsed = now - sitStartMs;

            if (elapsed >= SIT_REMINDER_MS) {
                // First trigger or periodic repeat
                if (!reminderActive || (now - lastReminderMs >= REMINDER_REPEAT_MS)) {
                    buzzReminder();
                    reminderActive = true;
                    lastReminderMs = now;
                }
            }
        } else {
            if (personPresent) {
                // Person left — reset timer
                personPresent  = false;
                reminderActive = false;
                Serial.println("[timer] Person left — sit timer reset");
            }
        }

        // ── Slouch handling ──────────────────────────────────────────────────
        if (isSlouching && (now - lastSlouchAlert >= SLOUCH_COOLDOWN_MS)) {
            lastSlouchAlert = now;

            Serial.println("[slouch] Slouch detected — activating servos");
            pulseServos();

            Serial.println("[slouch] Fetching GPT line...");
            String line = fetchSlouchLine();
            if (line.length() > 0) {
                Serial.println("─────────────────────────────────");
                Serial.println(line);
                Serial.println("─────────────────────────────────");
            }
        }
    }
}
