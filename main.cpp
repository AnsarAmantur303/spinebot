/**
 * SpineBot — ESP32-S3 + OV5640 Firmware
 *
 * Board:  ESP32S3 Dev Module
 * PSRAM:  OPI PSRAM  (required — camera frames go to PSRAM)
 * USB CDC: Enabled
 *
 * Flow:
 *   1. Capture JPEG frame → POST /detect to PC server
 *   2. Server returns { "is_slouching": bool, "is_person_present": bool }
 *   3. If is_slouching      → pulse shoulder servos 30° + POST /slouch_line → print GPT line
 *   4. If is_person_present → run timers:
 *        15 min → wave hand servos up/down 5 times
 *        20 min → buzz reminder to stand up
 *
 * PC server (Python) must expose:
 *   POST /detect      — body: raw JPEG bytes
 *                       returns JSON: {"is_slouching":bool,"is_person_present":bool}
 *   POST /slouch_line — no body needed
 *                       returns JSON: {"line":"<GPT response>"}
 */

#include <Arduino.h>
#include "eyes.cpp"
#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

// ─── WIFI ────────────────────────────────────────────────────────────────────

const char* WIFI_SSID     = "rinat";
const char* WIFI_PASSWORD = "12345678";

// ─── SERVER ──────────────────────────────────────────────────────────────────

const char*    SERVER_HOST = "192.168.1.100";   // ← your PC's local IP
const uint16_t SERVER_PORT = 5000;

// ─── CAMERA PINS — ESP32-S3 / OV5640 ────────────────────────────────────────

#define PWDN_GPIO    -1
#define RESET_GPIO   -1
#define XCLK_GPIO    15
#define SIOD_GPIO     4
#define SIOC_GPIO     5
#define Y9_GPIO      16
#define Y8_GPIO      17
#define Y7_GPIO      18
#define Y6_GPIO      12
#define Y5_GPIO      10
#define Y4_GPIO       8
#define Y3_GPIO       9
#define Y2_GPIO      11
#define VSYNC_GPIO    6
#define HREF_GPIO     7
#define PCLK_GPIO    13

// ─── SERVO / BUZZER PINS ─────────────────────────────────────────────────────
// Camera occupies GPIOs 4-18, so use 19+ for peripherals

const int SERVO1_PIN = 14;   // shoulder — CW on slouch
const int SERVO2_PIN = 15;   // shoulder — CCW on slouch
const int SERVO3_PIN = 12;   // hand left  — wave on 15-min timer
const int SERVO4_PIN = 13;   // hand right — wave on 15-min timer
// const int BUZZER_PIN = 39;   // active buzzer — beep on 20-min timer

// ─── TIMING ──────────────────────────────────────────────────────────────────

const unsigned long FRAME_INTERVAL_MS   = 500;           // ~2 fps detection
const unsigned long SLOUCH_COOLDOWN_MS  = 10000UL;       // min gap between slouch alerts

const unsigned long HAND_WAVE_MS        = 15UL*60*1000;  // 15 min → wave hands
const unsigned long HAND_WAVE_REPEAT_MS = 60000UL;       // re-wave every 60 s
const int           HAND_WAVE_REPS      = 5;             // up/down cycles
const int           HAND_WAVE_SPEED_MS  = 300;           // ms per half-cycle

const unsigned long SIT_REMINDER_MS     = 20UL*60*1000;  // 20 min → buzz
const unsigned long REMINDER_REPEAT_MS  = 60000UL;       // re-buzz every 60 s

// back servo geometry
const int BACK_SERVO1_UPRIGHT = 135;
const int BACK_SERVO1_SLOUCH  = 100;
const int BACK_SERVO2_UPRIGHT = 135;
const int BACK_SERVO2_SLOUCH  = 170; 

// Hand servo geometry
const int HAND_DOWN_DEG = 0;
const int HAND_UP_DEG   = 90;

// ─── GLOBALS ─────────────────────────────────────────────────────────────────

Servo servo1, servo2, servo3, servo4;

unsigned long lastSlouchAlert = 0;

bool          personPresent   = false;
unsigned long sitStartMs      = 0;

bool          handsWaved      = false;
unsigned long lastHandWaveMs  = 0;

bool          reminderActive  = false;
unsigned long lastReminderMs  = 0;

// ─── HELPERS ─────────────────────────────────────────────────────────────────

String buildUrl(const char* path) {
    return String("http://") + SERVER_HOST + ":" + SERVER_PORT + path;
}

// void buzzReminder() {
//     Serial.println("[REMINDER] 20 minutes sitting — time to move!");
//     digitalWrite(BUZZER_PIN, HIGH); delay(300);
//     digitalWrite(BUZZER_PIN, LOW);  delay(150);
//     digitalWrite(BUZZER_PIN, HIGH); delay(300);
//     digitalWrite(BUZZER_PIN, LOW);
// }
void pulseBackServos() {
    servo1.attach(SERVO1_PIN);
    servo2.attach(SERVO2_PIN);

    // Start from upright position
    servo1.write(BACK_SERVO1_UPRIGHT);
    servo2.write(BACK_SERVO2_UPRIGHT);
    delay(300);

    // Apply corrective pull
    servo1.write(BACK_SERVO1_SLOUCH);
    servo2.write(BACK_SERVO2_SLOUCH);
    delay(SERVO_HOLD_MS);

    // Return to upright
    servo1.write(BACK_SERVO1_UPRIGHT);
    servo2.write(BACK_SERVO2_UPRIGHT);
    delay(300);

    servo1.detach();
    servo2.detach();
}

void waveHands() {
    Serial.println("[hands] 15 min sitting — waving hands!");

    servo3.attach(SERVO3_PIN);
    servo4.attach(SERVO4_PIN);

    servo3.write(HAND_DOWN_DEG);
    servo4.write(HAND_DOWN_DEG);
    delay(300);

    for (int i = 0; i < HAND_WAVE_REPS; i++) {
        servo3.write(HAND_UP_DEG);
        servo4.write(HAND_UP_DEG);
        delay(HAND_WAVE_SPEED_MS);

        servo3.write(HAND_DOWN_DEG);
        servo4.write(HAND_DOWN_DEG);
        delay(HAND_WAVE_SPEED_MS);
    }

    servo3.detach();
    servo4.detach();
}

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

    outSlouching     = doc["is_slouching"]     | false;
    outPersonPresent = doc["is_person_present"] | false;
    return true;
}

// ─── CAMERA INIT ─────────────────────────────────────────────────────────────

bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO;
    config.pin_d1       = Y3_GPIO;
    config.pin_d2       = Y4_GPIO;
    config.pin_d3       = Y5_GPIO;
    config.pin_d4       = Y6_GPIO;
    config.pin_d5       = Y7_GPIO;
    config.pin_d6       = Y8_GPIO;
    config.pin_d7       = Y9_GPIO;
    config.pin_xclk     = XCLK_GPIO;
    config.pin_pclk     = PCLK_GPIO;
    config.pin_vsync    = VSYNC_GPIO;
    config.pin_href     = HREF_GPIO;
    config.pin_sccb_sda = SIOD_GPIO;   // note: sccb (not sscb) for ESP32-S3 IDF
    config.pin_sccb_scl = SIOC_GPIO;
    config.pin_pwdn     = PWDN_GPIO;
    config.pin_reset    = RESET_GPIO;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode    = CAMERA_GRAB_LATEST;   // always use the freshest frame
    config.fb_location  = CAMERA_FB_IN_PSRAM;   // requires OPI PSRAM enabled
    config.frame_size   = FRAMESIZE_QVGA;        // 320×240 — fast over WiFi
    config.jpeg_quality = 12;
    config.fb_count     = 2;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[camera] init failed: 0x%x\n", err);
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    s->set_vflip(s, 1);
    s->set_hmirror(s, 0);

    Serial.println("[camera] OK");
    return true;
}

// ─── WIFI ────────────────────────────────────────────────────────────────────

void connectWiFi() {
    Serial.printf("[wifi] Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
        delay(500);
        Serial.print(".");
        tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[wifi] Connected — IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[wifi] Failed — check credentials");
    }
}

// ─── SETUP ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== SpineBot (ESP32-S3) ===");

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // Allocate LEDC timers for servos — avoid timer 0 used by camera XCLK
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

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

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[wifi] Lost — reconnecting...");
        connectWiFi();
        return;
    }

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

        if (!ok) return;

        Serial.printf("[detect] person=%d  slouching=%d\n", isPersonPresent, isSlouching);

        // ── Sit timers ───────────────────────────────────────────────────────
        if (isPersonPresent) {
            if (!personPresent) {
                personPresent  = true;
                sitStartMs     = now;
                handsWaved     = false;
                lastHandWaveMs = 0;
                reminderActive = false;
                lastReminderMs = 0;
                Serial.println("[timer] Person detected — timers started");
            }

            unsigned long elapsed = now - sitStartMs;

            // 15 min → wave hands
            if (elapsed >= HAND_WAVE_MS) {
                if (!handsWaved || (now - lastHandWaveMs >= HAND_WAVE_REPEAT_MS)) {
                    waveHands();
                    handsWaved     = true;
                    lastHandWaveMs = now;
                }
            }

            // 20 min → buzz
            if (elapsed >= SIT_REMINDER_MS) {
                if (!reminderActive || (now - lastReminderMs >= REMINDER_REPEAT_MS)) {
                    buzzReminder();
                    reminderActive = true;
                    lastReminderMs = now;
                }
            }

        } else {
            if (personPresent) {
                personPresent  = false;
                handsWaved     = false;
                reminderActive = false;
                Serial.println("[timer] Person left — timers reset");
            }
        }

        // ── Slouch handling ──────────────────────────────────────────────────
        if (isSlouching && (now - lastSlouchAlert >= SLOUCH_COOLDOWN_MS)) {
            lastSlouchAlert = now;
            Serial.println("[slouch] Detected — pulsing shoulder servos");
            pulseShoulderServos();

            String line = fetchSlouchLine();
            if (line.length() > 0) {
                Serial.println("─────────────────────────────────");
                Serial.println(line);
                Serial.println("─────────────────────────────────");
            }
        }
    }
}