#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define i2c_Address 0x3c
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ================================================================
//  BASE EYE
// ================================================================
void drawBaseEye(int cx, int cy) {
    display.drawCircle(cx, cy, 16, SH110X_WHITE);
    display.fillCircle(cx, cy, 12, SH110X_WHITE);
    display.fillCircle(cx, cy, 7,  SH110X_BLACK);
    display.fillCircle(cx + 3, cy - 3, 3, SH110X_WHITE);
    display.fillCircle(cx - 4, cy + 4, 1, SH110X_WHITE);
    display.drawFastHLine(cx - 14, cy + 15, 29, SH110X_WHITE);
}

// ================================================================
//  HAPPY EYE
// ================================================================
void draw_happy(int cx, int cy) {
    drawBaseEye(cx, cy);
    display.fillRect(cx - 17, cy - 20, 34, 7, SH110X_BLACK);
    display.drawLine(cx - 16, cy - 11, cx - 8,  cy - 15, SH110X_WHITE);
    display.drawLine(cx - 8,  cy - 15, cx,      cy - 16, SH110X_WHITE);
    display.drawLine(cx,      cy - 16, cx + 8,  cy - 15, SH110X_WHITE);
    display.drawLine(cx + 8,  cy - 15, cx + 16, cy - 11, SH110X_WHITE);
    display.drawLine(cx - 16, cy - 10, cx - 8,  cy - 14, SH110X_WHITE);
    display.drawLine(cx - 8,  cy - 14, cx,      cy - 15, SH110X_WHITE);
    display.drawLine(cx,      cy - 15, cx + 8,  cy - 14, SH110X_WHITE);
    display.drawLine(cx + 8,  cy - 14, cx + 16, cy - 10, SH110X_WHITE);
    display.drawLine(cx - 14, cy - 12, cx - 18, cy - 19, SH110X_WHITE);
    display.drawLine(cx - 6,  cy - 15, cx - 7,  cy - 22, SH110X_WHITE);
    display.drawLine(cx,      cy - 16, cx,      cy - 23, SH110X_WHITE);
    display.drawLine(cx + 6,  cy - 15, cx + 7,  cy - 22, SH110X_WHITE);
    display.drawLine(cx + 14, cy - 12, cx + 18, cy - 19, SH110X_WHITE);
}

// ================================================================
//  SAD EYE — sharp diagonal brow like the image + tear
//  LEFT eye:  brow goes  high-left  → low-right  (inner corner low)
//  RIGHT eye: brow goes  low-left   → high-right (inner corner low)
// ================================================================
void draw_sad(int cx, int cy, bool leftEye, bool tear) {
    drawBaseEye(cx, cy);

    // flat eyelid
    display.fillRect(cx - 17, cy - 30, 34, 18, SH110X_BLACK);
    display.drawFastHLine(cx - 16, cy - 12, 33, SH110X_WHITE);
    display.drawFastHLine(cx - 16, cy - 11, 33, SH110X_WHITE);

    // lashes
    display.drawLine(cx - 14, cy - 11, cx - 17, cy - 17, SH110X_WHITE);
    display.drawLine(cx - 6,  cy - 12, cx - 7,  cy - 18, SH110X_WHITE);
    display.drawLine(cx,      cy - 12, cx,      cy - 19, SH110X_WHITE);
    display.drawLine(cx + 6,  cy - 12, cx + 7,  cy - 18, SH110X_WHITE);
    display.drawLine(cx + 14, cy - 11, cx + 17, cy - 17, SH110X_WHITE);

    // ---- sharp diagonal eyebrow ----
    // left eye:  outer(left) is HIGH, inner(right) is LOW  → \ shape
    // right eye: outer(right) is HIGH, inner(left) is LOW  → / shape
    if (leftEye) {
        // \ shaped brow
        display.drawLine(cx - 14, cy - 26, cx + 10, cy - 18, SH110X_WHITE);
        display.drawLine(cx - 14, cy - 25, cx + 10, cy - 17, SH110X_WHITE);
        display.drawLine(cx - 14, cy - 24, cx + 10, cy - 16, SH110X_WHITE);
    } else {
        // / shaped brow
        display.drawLine(cx - 10, cy - 18, cx + 14, cy - 26, SH110X_WHITE);
        display.drawLine(cx - 10, cy - 17, cx + 14, cy - 25, SH110X_WHITE);
        display.drawLine(cx - 10, cy - 16, cx + 14, cy - 24, SH110X_WHITE);
    }

    // tear drop
    if (tear) {
        display.drawLine(cx + 8, cy + 16, cx + 8, cy + 20, SH110X_WHITE);
        display.fillCircle(cx + 8, cy + 23, 3, SH110X_WHITE);
    }
}

// ================================================================
//  CLOSING EYE
// ================================================================
void draw_closing(int cx, int cy, int closeAmount) {
    display.drawCircle(cx, cy, 16, SH110X_WHITE);
    display.fillCircle(cx, cy, 12, SH110X_WHITE);
    display.fillCircle(cx, cy, 7,  SH110X_BLACK);
    display.fillCircle(cx + 3, cy - 3, 3, SH110X_WHITE);
    display.fillRect(cx - 17, cy - 17, 34, closeAmount + 4, SH110X_BLACK);
    display.drawFastHLine(cx - 16, cy - 17 + closeAmount, 33, SH110X_WHITE);
    display.drawFastHLine(cx - 16, cy - 16 + closeAmount, 33, SH110X_WHITE);
    display.drawFastHLine(cx - 14, cy + 15, 29, SH110X_WHITE);
}

// ================================================================
//  SHOW HAPPY
// ================================================================
void show_happy() {
    display.clearDisplay();
    draw_happy(34, 34);
    draw_happy(94, 34);
    display.display();
}

// ================================================================
//  SHOW SAD — left eye no tear, right eye with tear
// ================================================================
void show_sad() {
    display.clearDisplay();
    draw_sad(34, 34, true,  false);   // left eye,  no tear
    draw_sad(94, 34, false, true);    // right eye, tear
    display.display();
}

// ================================================================
//  SHOW CLOSING
// ================================================================
void show_closing() {
    for (int i = 0; i <= 32; i += 4) {
        display.clearDisplay();
        draw_closing(34, 34, i);
        draw_closing(94, 34, i);
        display.display();
        delay(60);
    }
    delay(300);
    for (int i = 32; i >= 0; i -= 4) {
        display.clearDisplay();
        draw_closing(34, 34, i);
        draw_closing(94, 34, i);
        display.display();
        delay(60);
    }
}

// ================================================================
//  BREATHING
// ================================================================
void breathing() {
    show_happy();
    delay(1000);
    show_closing();
}

void show_sad_then() {
    show_sad();
    delay(1000);
    show_closing();
}

// ================================================================
//  SETUP
// ================================================================
void setup() {
    Serial.begin(9600);
    delay(250);
    display.begin(i2c_Address, true);
    display.clearDisplay();
    display.display();
}

// ================================================================
//  LOOP
// ================================================================
void loop() {
  if (is_slouching)  
  {
    show_sad();
    is_slouching = false;
  }
  breathing();
}
