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

void searching();
void setSlouching(bool slouching);
void setPersonPresent(bool present);
void setWaving(bool waving);
void setWaveStyle(int style);

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
            case 5:
                searching();
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
const int TOUCH_LEFT_PIN = 16;
const int TOUCH_RIGHT_PIN = 17;
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
const int ARM_NEUTRAL_DEG = 90;
const int ARM_LEFT_BASE_DEG = 10;
const int ARM_RIGHT_BASE_DEG = 170;
const int ARM_WAVE_DEFLECTION_DEG = 35;
const int ARM_WAVE_STEP_MS = 180;
const unsigned long ARM_WAVE_STYLE2_STEP_MS = 260;
const unsigned long ARM_WAVE_STYLE2_RESET_MS = 60;
const unsigned long TOUCH_PAIR_MIN_MS = 200UL;
const unsigned long TOUCH_PAIR_MAX_MS = 300UL;
const unsigned long WAVE_DURATION_MS   = 2000UL;

// ─── GLOBALS ─────────────────────────────────────────────────────────────────

Servo servo1, servo2, servo3, servo4;

volatile bool is_slouching    = false;
volatile bool is_person_present = false;
volatile bool is_waving       = false;
volatile int wave_style       = 0; // 0: wave one hand, 1: wave both hands
volatile bool touched_L       = false;
volatile bool touched_R       = false;
volatile bool touch_debug_override = false;
volatile unsigned long touched_L_ms = 0;
volatile unsigned long touched_R_ms = 0;
volatile unsigned long wave_active_until_ms = 0;
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

void writeBackNeutral() {
    writeBackServos(false);
}

int mirrorAngle(int angleDeg) {
    return 180 - angleDeg;
}

int armMotionAngle(int baseDeg, int deflectionDeg) {
    return constrain(baseDeg + deflectionDeg, 0, 180);
}

int armLeftAngleForDeflection(int deflectionDeg) {
    return armMotionAngle(ARM_LEFT_BASE_DEG, deflectionDeg);
}

int armRightAngleForDeflection(int deflectionDeg) {
    return armMotionAngle(ARM_RIGHT_BASE_DEG, deflectionDeg);
}

void writeArmAngles(int leftDeg, int rightDeg) {
    servo3.write(leftDeg);
    servo4.write(rightDeg);
}

void writeArmDeflection(int deflectionDeg) {
    writeArmAngles(
        armLeftAngleForDeflection(deflectionDeg),
        armRightAngleForDeflection(deflectionDeg)
    );
}

void writeArmNeutral() {
    servo3.write(ARM_NEUTRAL_DEG);
    servo4.write(ARM_NEUTRAL_DEG);
}

void doWaveStyleOneHand(int phaseDeg) {
    // Style 0: left arm waves, right arm stays at its base pose.
    writeArmAngles(armLeftAngleForDeflection(phaseDeg), ARM_RIGHT_BASE_DEG);
}

void doWaveStyleBothHands(int phaseDeg) {
    // Style 1: both arms move together in the same direction.
    writeArmDeflection(phaseDeg);
}

void writeMirroredArms(int leftDeg) {
    writeArmAngles(leftDeg, leftDeg);
}

void searching() {
    show_closing();
}

void touchTask(void* parameter) {
    (void)parameter;

    bool lastRawL = false;
    bool lastRawR = false;

    for (;;) {
        if (!touch_debug_override) {
            bool currentL = digitalRead(TOUCH_LEFT_PIN) == HIGH;
            bool currentR = digitalRead(TOUCH_RIGHT_PIN) == HIGH;
            unsigned long nowMs = millis();

            if (currentL && !lastRawL) {
                touched_L_ms = nowMs;
            }
            if (currentR && !lastRawR) {
                touched_R_ms = nowMs;
            }

            touched_L = currentL;
            touched_R = currentR;
            lastRawL = currentL;
            lastRawR = currentR;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void setTouchDebugState(bool overrideActive, bool leftTouched, bool rightTouched) {
    unsigned long nowMs = millis();

    if (overrideActive && leftTouched && !touched_L) {
        touched_L_ms = nowMs;
    }
    if (overrideActive && rightTouched && !touched_R) {
        touched_R_ms = nowMs;
    }

    touch_debug_override = overrideActive;
    touched_L = leftTouched;
    touched_R = rightTouched;
}

void startWave(int style, int animationState) {
    setWaveStyle(style);
    setWaving(true);
    animState = is_slouching ? 2 : animationState;
    wave_active_until_ms = millis() + WAVE_DURATION_MS;
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

    int wavePhaseDeg = ARM_WAVE_DEFLECTION_DEG;
    int waveStepDeg = -ARM_WAVE_DEFLECTION_DEG;
    unsigned long lastWaveStepMs = millis();
    int style2Deflection = 0;
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
            wavePhaseDeg = ARM_WAVE_DEFLECTION_DEG;
            waveStepDeg = -ARM_WAVE_DEFLECTION_DEG;
            lastWaveStepMs = millis();
            style2Deflection = 0;
            style2ResetPending = false;
            style2ResetMs = 0;
        }

        if (wave_style != lastWaveStyle) {
            lastWaveStyle = wave_style;
            if (wave_style == 2) {
                style2Deflection = 0;
                style2ResetPending = false;
                style2ResetMs = 0;
                writeArmDeflection(style2Deflection);
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
                    if (wavePhaseDeg >= ARM_WAVE_DEFLECTION_DEG || wavePhaseDeg <= -ARM_WAVE_DEFLECTION_DEG) {
                        waveStepDeg = -waveStepDeg;
                    }
                    lastWaveStepMs = nowMs;
                }
                break;

            case 1:
                if (nowMs - lastWaveStepMs >= ARM_WAVE_STEP_MS) {
                    doWaveStyleBothHands(wavePhaseDeg);

                    wavePhaseDeg += waveStepDeg;
                    if (wavePhaseDeg >= ARM_WAVE_DEFLECTION_DEG || wavePhaseDeg <= -ARM_WAVE_DEFLECTION_DEG) {
                        waveStepDeg = -waveStepDeg;
                    }
                    lastWaveStepMs = nowMs;
                }
                break;

            case 2:
                if (!style2ResetPending) {
                    if (nowMs - lastWaveStepMs >= ARM_WAVE_STYLE2_STEP_MS) {
                        writeArmDeflection(style2Deflection);
                        style2Deflection += 2;
                        if (style2Deflection >= ARM_WAVE_DEFLECTION_DEG) {
                            style2Deflection = ARM_WAVE_DEFLECTION_DEG;
                            style2ResetPending = true;
                            style2ResetMs = nowMs;
                        }
                        lastWaveStepMs = nowMs;
                    }
                } else if (nowMs - style2ResetMs >= ARM_WAVE_STYLE2_RESET_MS) {
                    style2Deflection = 0;
                    writeArmDeflection(style2Deflection);
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

    pinMode(TOUCH_LEFT_PIN, INPUT_PULLDOWN);
    pinMode(TOUCH_RIGHT_PIN, INPUT_PULLDOWN);

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
    xTaskCreatePinnedToCore(
        touchTask,
        "touchTask",
        2048,
        nullptr,
        1,
        nullptr,
        1
    );
    initOledTask();
    Serial.println("[setup] Ready.");
}

// ─── LOOP ────────────────────────────────────────────────────────────────────

void triggerOneHandWave() {
    startWave(0, 1);
}

void triggerBothHandsWave() {
    startWave(1, 3);
}

void printDebugHelp() {
    Serial.println("[debug] commands:");
    Serial.println("[debug]   p = toggle person present");
    Serial.println("[debug]   s = toggle slouching");
    Serial.println("[debug]   w = toggle waving");
    Serial.println("[debug]   0..3 = set wave style");
    Serial.println("[debug]   l = left touch on");
    Serial.println("[debug]   L = left touch off");
    Serial.println("[debug]   r = right touch on");
    Serial.println("[debug]   R = right touch off");
    Serial.println("[debug]   b = both touches on");
    Serial.println("[debug]   n = both touches off");
    Serial.println("[debug]   u = disable touch override");
}

void handleDebugSerialInput() {
    while (Serial.available() > 0) {
        char command = static_cast<char>(Serial.read());

        if (command == '\n' || command == '\r' || command == ' ') {
            continue;
        }

        switch (command) {
            case 'h':
            case '?':
                printDebugHelp();
                break;

            case 'p':
                setPersonPresent(!is_person_present);
                Serial.printf("[debug] is_person_present=%d\n", is_person_present);
                break;

            case 's':
                setSlouching(!is_slouching);
                Serial.printf("[debug] is_slouching=%d\n", is_slouching);
                break;

            case 'w':
                setWaving(!is_waving);
                Serial.printf("[debug] is_waving=%d\n", is_waving);
                break;

            case '0':
            case '1':
            case '2':
            case '3':
                setWaveStyle(command - '0');
                Serial.printf("[debug] wave_style=%d\n", wave_style);
                break;

            case 'l':
                setTouchDebugState(true, true, false);
                Serial.printf("[debug] touched_L=%d touched_R=%d\n", touched_L, touched_R);
                break;

            case 'L':
                setTouchDebugState(true, false, touched_R);
                Serial.printf("[debug] touched_L=%d touched_R=%d\n", touched_L, touched_R);
                break;

            case 'r':
                setTouchDebugState(true, false, true);
                Serial.printf("[debug] touched_L=%d touched_R=%d\n", touched_L, touched_R);
                break;

            case 'R':
                setTouchDebugState(true, touched_L, false);
                Serial.printf("[debug] touched_L=%d touched_R=%d\n", touched_L, touched_R);
                break;

            case 'b':
                setTouchDebugState(true, true, true);
                Serial.printf("[debug] touched_L=%d touched_R=%d\n", touched_L, touched_R);
                break;

            case 'n':
                setTouchDebugState(true, false, false);
                Serial.printf("[debug] touched_L=%d touched_R=%d\n", touched_L, touched_R);
                break;

            case 'u':
                touch_debug_override = false;
                Serial.println("[debug] touch override disabled");
                break;

            default:
                Serial.printf("[debug] unknown command '%c'\n", command);
                printDebugHelp();
                break;
        }
    }
}

void loop() {
    handleDebugSerialInput();

    static bool lastPersonPresent = false;
    static bool lastTouchL = false;
    static bool lastTouchR = false;
    static bool pairedTouchConsumed = false;

    unsigned long nowMs = millis();

    bool personPresent = is_person_present;
    bool touchL = touched_L;
    bool touchR = touched_R;
    bool leftTouchedEdge = touchL && !lastTouchL;
    bool rightTouchedEdge = touchR && !lastTouchR;

    bool touchPairActive = false;
    if (touchL && touchR) {
        unsigned long deltaMs = (touched_L_ms > touched_R_ms) ? (touched_L_ms - touched_R_ms) : (touched_R_ms - touched_L_ms);
        touchPairActive = deltaMs >= TOUCH_PAIR_MIN_MS && deltaMs <= TOUCH_PAIR_MAX_MS;
    }

    if (!touchL || !touchR) {
        pairedTouchConsumed = false;
    }

    if (is_waving && wave_active_until_ms != 0 && nowMs >= wave_active_until_ms) {
        setWaving(false);
        wave_active_until_ms = 0;
    }

    if (personPresent) {
        if (is_slouching) {
            writeBackServos(true);
            animState = 2;
        } else {
            writeBackServos(false);
            animState = 1;
        }

        if (!lastPersonPresent) {
            Serial.println("[presence] person entered — happy + one-hand wave");
            triggerOneHandWave();
        }

        if (touchPairActive && !pairedTouchConsumed) {
            Serial.println("[touch] paired touches — star burst + both hands");
            triggerBothHandsWave();
            pairedTouchConsumed = true;
        } else if (leftTouchedEdge || rightTouchedEdge) {
            Serial.println("[touch] one sensor touched — happy + one-hand wave");
            triggerOneHandWave();
        } else if (!lastPersonPresent) {
            triggerOneHandWave();
        }
    } else {
        if (lastPersonPresent) {
            Serial.println("[presence] person left — neutral back + searching");
        }
        writeBackNeutral();
        setWaving(false);
        setWaveStyle(0);
        animState = 5;
        wave_active_until_ms = 0;
    }

    lastPersonPresent = personPresent;
    lastTouchL = touchL;
    lastTouchR = touchR;

    vTaskDelay(pdMS_TO_TICKS(20));
}

void setSlouching(bool slouching) {
    is_slouching = slouching;
}

void setPersonPresent(bool present) {
    is_person_present = present;
}

void setWaving(bool waving) {
    is_waving = waving;
}

void setWaveStyle(int style) {
    wave_style = style;
}