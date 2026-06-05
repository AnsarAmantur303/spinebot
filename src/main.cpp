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

volatile int animState = 1;
const uint16_t SERVER_PORT = 5000;

// // ─── CAMERA PINS — ESP32-S3 / OV5640 ────────────────────────────────────────

void function_1() {
    show_happy();
}

void function_2() {
    show_sad();
}

void function_3() {
    show_closing();
}

void function_4() {
    breathing();
}

void oledTask(void* parameter) {
    (void)parameter;

    for (;;) {
        switch (animState) {
            case 1:
                show_happy();
                break;
            case 2:
                show_sad();
                break;
            case 3:
                show_star_burst();
                break;
            case 4:
                show_hearts_pulse();
                break;
            default:
                show_happy();
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(60));
    }
}

void initOledTask() {
    initEyes();

    xTaskCreatePinnedToCore(
        oledTask,
        "oledTask",
        4096,
        nullptr,
        1,
        nullptr,
        1
    );
}

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

const int SERVO1_PIN = 14;   // shoulder — CW on slouch LEFT
const int SERVO2_PIN = 15;   // shoulder — CCW on slouch RIGHT
const int SERVO3_PIN = 12;   // arm left
const int SERVO4_PIN = 13;   // arm right
// const int BUZZER_PIN = 39;   // active buzzer — beep on 20-min timer

// ─── TIMING ──────────────────────────────────────────────────────────────────

const unsigned long SLOUCH_COOLDOWN_MS  = 10000UL;       // min gap between slouch alerts

// back servo geometry
const int UPRIGHT_ANGLE = 135; // angle for upright posture
const int DEFLECT_ANGLE = 35;  // symmetric bend amount from upright
const int BACK_SERVO1_UPRIGHT = UPRIGHT_ANGLE;
const int BACK_SERVO1_SLOUCH  = UPRIGHT_ANGLE - DEFLECT_ANGLE;
const int BACK_SERVO2_UPRIGHT = 180 - UPRIGHT_ANGLE; // mirror of servo 1
const int BACK_SERVO2_SLOUCH  = 180 - (UPRIGHT_ANGLE - DEFLECT_ANGLE); // mirror of servo 1

// arm wave geometry
const int ARM_CENTER_DEG = 90;
const int ARM_MIN_DEG = ARM_CENTER_DEG - 35;
const int ARM_MAX_DEG = ARM_CENTER_DEG + 35;
const int ARM_WAVE_AMPLITUDE_DEG = 35;
const int ARM_WAVE_STEP_MS = 180;
const unsigned long ARM_WAVE_STYLE2_STEP_MS = 26;
const unsigned long ARM_WAVE_STYLE2_RESET_MS = 6;

// ─── GLOBALS ─────────────────────────────────────────────────────────────────

Servo servo1, servo2, servo3, servo4;

volatile bool is_slouching    = false;
volatile bool is_waving       = false;
volatile int wave_style       = 0; // 0: wave one hand, 1: wave both hands
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


void writeBackServos(bool slouching) {
    // Mirror movement: one servo moves to its slouch angle while the other mirrors it.
    if (slouching) {
        servo1.write(BACK_SERVO1_SLOUCH);
        servo2.write(BACK_SERVO2_SLOUCH);
    } else {
        servo1.write(BACK_SERVO1_UPRIGHT);
        servo2.write(BACK_SERVO2_UPRIGHT);
    }
}

int mirrorAngle(int angleDeg) {
    return 180 - angleDeg;
}

void writeArmNeutral() {
    servo3.write(ARM_CENTER_DEG);
    servo4.write(ARM_CENTER_DEG);
}

void doWaveStyleOneHand(int phaseDeg) {
    // Style 0: left arm waves, right arm stays on the opposite side.
    int left = ARM_CENTER_DEG + phaseDeg;
    int right = mirrorAngle(ARM_CENTER_DEG);
    servo3.write(left);
    servo4.write(right);
}

void doWaveStyleBothHands(int phaseDeg) {
    // Style 1: both arms move in perfectly mirrored opposition.
    int left = ARM_CENTER_DEG + phaseDeg;
    int right = mirrorAngle(left);
    servo3.write(left);
    servo4.write(right);
}

void writeMirroredArms(int leftDeg) {
    servo3.write(leftDeg);
    servo4.write(mirrorAngle(leftDeg));
}

void servoTask(void* parameter) {
    (void)parameter;

    servo1.attach(SERVO1_PIN);
    servo2.attach(SERVO2_PIN);
    servo3.attach(SERVO3_PIN);
    servo4.attach(SERVO4_PIN);

    bool lastPosture = is_slouching;
    bool lastWavingState = is_waving;
    int lastWaveStyle = -1;
    writeBackServos(lastPosture);
    writeArmNeutral();

    int wavePhaseDeg = ARM_WAVE_AMPLITUDE_DEG;
    int waveStepDeg = -ARM_WAVE_AMPLITUDE_DEG;
    unsigned long lastWaveStepMs = millis();
    int style2Angle = ARM_MIN_DEG;
    bool style2ResetPending = false;
    unsigned long style2ResetMs = 0;

    for (;;) {
        bool currentPosture = is_slouching;
        if (currentPosture != lastPosture) {
            writeBackServos(currentPosture);
            Serial.printf("[servo] posture -> %s\n", currentPosture ? "SLOUCH" : "UPRIGHT");
            lastPosture = currentPosture;
        }

        if (!is_waving) {
            if (lastWavingState) {
                writeArmNeutral();
                Serial.println("[servo] waving -> OFF");
            }
            lastWavingState = false;
            lastWaveStyle = -1;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (!lastWavingState) {
            Serial.printf("[servo] waving -> ON (style %d)\n", wave_style);
            wavePhaseDeg = ARM_WAVE_AMPLITUDE_DEG;
            waveStepDeg = -ARM_WAVE_AMPLITUDE_DEG;
            lastWaveStepMs = millis();
            style2Angle = ARM_MIN_DEG;
            style2ResetPending = false;
            style2ResetMs = 0;
        }

        if (wave_style != lastWaveStyle) {
            lastWaveStyle = wave_style;
            if (wave_style == 2) {
                style2Angle = ARM_MIN_DEG;
                style2ResetPending = false;
                style2ResetMs = 0;
                writeMirroredArms(style2Angle);
            } else if (wave_style == 3) {
                writeArmNeutral();
            }
        }

        unsigned long nowMs = millis();
        switch (wave_style) {
            case 0:
                if (nowMs - lastWaveStepMs >= ARM_WAVE_STEP_MS) {
                    doWaveStyleOneHand(wavePhaseDeg);

                    wavePhaseDeg += waveStepDeg;
                    if (wavePhaseDeg >= ARM_WAVE_AMPLITUDE_DEG || wavePhaseDeg <= -ARM_WAVE_AMPLITUDE_DEG) {
                        waveStepDeg = -waveStepDeg;
                    }
                    lastWaveStepMs = nowMs;
                }
                break;

            case 1:
                if (nowMs - lastWaveStepMs >= ARM_WAVE_STEP_MS) {
                    doWaveStyleBothHands(wavePhaseDeg);

                    wavePhaseDeg += waveStepDeg;
                    if (wavePhaseDeg >= ARM_WAVE_AMPLITUDE_DEG || wavePhaseDeg <= -ARM_WAVE_AMPLITUDE_DEG) {
                        waveStepDeg = -waveStepDeg;
                    }
                    lastWaveStepMs = nowMs;
                }
                break;

            case 2:
                if (!style2ResetPending) {
                    if (nowMs - lastWaveStepMs >= ARM_WAVE_STYLE2_STEP_MS) {
                        writeMirroredArms(style2Angle);
                        style2Angle += 2;
                        if (style2Angle >= ARM_MAX_DEG) {
                            style2Angle = ARM_MAX_DEG;
                            style2ResetPending = true;
                            style2ResetMs = nowMs;
                        }
                        lastWaveStepMs = nowMs;
                    }
                } else if (nowMs - style2ResetMs >= ARM_WAVE_STYLE2_RESET_MS) {
                    style2Angle = ARM_MIN_DEG;
                    writeMirroredArms(style2Angle);
                    style2ResetPending = false;
                    lastWaveStepMs = nowMs;
                }
                break;

            case 3:
                writeArmNeutral();
                break;

            default:
                writeArmNeutral();
                break;
        }

            lastWavingState = true;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
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
    xTaskCreatePinnedToCore(
        servoTask,
        "servoTask",
        4096,
        nullptr,
        1,
        nullptr,
        1
    );
    initOledTask();
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
    delay(2000);
    animState++;
    is_slouching = !is_slouching;
    is_waving = true;
    wave_style++;
    if(wave_style > 2) wave_style = 2;
    if(animState > 4) animState = 1;
}

void setSlouching(bool slouching) {
    is_slouching = slouching;
}

void setWaving(bool waving) {
    is_waving = waving;
}

void setWaveStyle(int style) {
    wave_style = style;
}