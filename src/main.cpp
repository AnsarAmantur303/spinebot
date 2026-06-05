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
#include "eyes.h"
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

// // ─── CAMERA PINS — ESP32-S3 / OV5640 ────────────────────────────────────────

// #define PWDN_GPIO    -1
// #define RESET_GPIO   -1
// #define XCLK_GPIO    15
// #define SIOD_GPIO     4
// #define SIOC_GPIO     5
// #define Y9_GPIO      16
// #define Y8_GPIO      17
// #define Y7_GPIO      18
// #define Y6_GPIO      12
// #define Y5_GPIO      10
// #define Y4_GPIO       8
// #define Y3_GPIO       9
// #define Y2_GPIO      11
// #define VSYNC_GPIO    6
// #define HREF_GPIO     7
// #define PCLK_GPIO    13

// ─── SERVO / BUZZER PINS ─────────────────────────────────────────────────────
// Camera occupies GPIOs 4-18, so use 19+ for peripherals

const int SERVO1_PIN = 14;   // shoulder — CW on slouch
const int SERVO2_PIN = 15;   // shoulder — CCW on slouch
const int SERVO3_PIN = 12;   // hand left  — wave on 15-min timer
const int SERVO4_PIN = 13;   // hand right — wave on 15-min timer
// const int BUZZER_PIN = 39;   // active buzzer — beep on 20-min timer

// ─── TIMING ──────────────────────────────────────────────────────────────────

const unsigned long SLOUCH_COOLDOWN_MS  = 10000UL;       // min gap between slouch alerts

const int           HAND_WAVE_REPS      = 5;             // up/down cycles
const int           HAND_WAVE_SPEED_MS  = 300;           // ms per half-cycle

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

bool          is_slouching    = true;
unsigned long lastSlouchAlert = 0;

// ─── HELPERS ─────────────────────────────────────────────────────────────────
// TODO: POST WIFI CONNECTIONO
// String buildUrl(const char* path) {
//     return String("http://") + SERVER_HOST + ":" + SERVER_PORT + path;
// }

// void buzzReminder() {
//     Serial.println("[REMINDER] 20 minutes sitting — time to move!");
//     digitalWrite(BUZZER_PIN, HIGH); delay(300);
//     digitalWrite(BUZZER_PIN, LOW);  delay(150);
//     digitalWrite(BUZZER_PIN, HIGH); delay(300);
//     digitalWrite(BUZZER_PIN, LOW);
// }


void straightenBack() {
    servo1.attach(SERVO1_PIN);
    servo2.attach(SERVO2_PIN);

    servo1.write(BACK_SERVO1_UPRIGHT);
    servo2.write(BACK_SERVO2_UPRIGHT);
}
void slouchBack() {
    servo1.attach(SERVO1_PIN);
    servo2.attach(SERVO2_PIN);

    servo1.write(BACK_SERVO1_SLOUCH);
    servo2.write(BACK_SERVO2_SLOUCH);
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

// ─── WIFI ────────────────────────────────────────────────────────────────────
//TODO: WIFI connection handling could be more robust with event handlers instead of polling in loop()
// ─── SETUP ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== SpineBot (ESP32-S3) ===");

    // pinMode(BUZZER_PIN, OUTPUT);
    // digitalWrite(BUZZER_PIN, LOW);

    // Allocate LEDC timers for servos — avoid timer 0 used by camera XCLK
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

// TODO:    connectWiFi();
    // initialize eyes display
    initEyes();
    Serial.println("[setup] Ready.");
}

// ─── LOOP ────────────────────────────────────────────────────────────────────

void loop() {
    unsigned long now = millis();

    // if (WiFi.status() != WL_CONNECTED) {
    //     Serial.println("[wifi] Lost — reconnecting...");
    //     connectWiFi();
    //     return;
    // }

    //  if (is_slouching && (now - lastSlouchAlert >= SLOUCH_COOLDOWN_MS)) {
    //     lastSlouchAlert = now;
    //     Serial.println("[slouch] Detected — pulsing shoulder servos");
    //     show_sad_then();
    //     slouchBack();
    // }
    // else
    // breathing();
    show_hearts_pulse();
}