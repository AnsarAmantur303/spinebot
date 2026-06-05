#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define i2c_Address 0x3c
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

extern volatile int animState;

static bool animationStateChanged(int expectedState) {
    return animState != expectedState;
}

static bool delayInterruptible(uint16_t totalMs, int expectedState, uint16_t sliceMs = 10) {
    unsigned long start = millis();
    while ((millis() - start) < totalMs) {
        if (animationStateChanged(expectedState)) {
            return true;
        }

        unsigned long elapsed = millis() - start;
        unsigned long remaining = totalMs - elapsed;
        delay((remaining < sliceMs) ? remaining : sliceMs);
    }
    return animationStateChanged(expectedState);
}

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
//  SAD EYE
//  Brow: inner corner UP toward nose, outer corner LOW → universally sad
//  LEFT eye:  /  (inner=right HIGH, outer=left LOW)
//  RIGHT eye: \  (inner=left  HIGH, outer=right LOW)
//  Eyelid droops heavily over the top of the eye.
// ================================================================
void draw_sad(int cx, int cy, bool leftEye, bool tear) {
    // base eye, pupil shifted slightly down for a downcast look
    display.drawCircle(cx, cy + 2, 15, SH110X_WHITE);
    display.fillCircle(cx, cy + 2, 11, SH110X_WHITE);
    display.fillCircle(cx, cy + 4,  7, SH110X_BLACK);
    display.fillCircle(cx + 3, cy + 1, 2, SH110X_WHITE);   // small highlight
    display.drawFastHLine(cx - 14, cy + 16, 29, SH110X_WHITE); // bottom lash line

    // heavy drooping eyelid — covers top 60% of the eye
    display.fillRect(cx - 17, cy - 30, 34, 24, SH110X_BLACK);
    display.drawFastHLine(cx - 15, cy - 6,  31, SH110X_WHITE);
    display.drawFastHLine(cx - 15, cy - 5,  31, SH110X_WHITE);
    display.drawFastHLine(cx - 15, cy - 4,  31, SH110X_WHITE); // thick lid edge

    // droopy lashes — hang downward from the lid
    display.drawLine(cx - 13, cy - 5, cx - 15, cy - 9,  SH110X_WHITE);
    display.drawLine(cx - 5,  cy - 6, cx - 5,  cy - 11, SH110X_WHITE);
    display.drawLine(cx,      cy - 6, cx,      cy - 12, SH110X_WHITE);
    display.drawLine(cx + 5,  cy - 6, cx + 5,  cy - 11, SH110X_WHITE);
    display.drawLine(cx + 13, cy - 5, cx + 15, cy - 9,  SH110X_WHITE);

    // sad eyebrow: inner corner UP, outer corner DOWN
    if (leftEye) {
        // / brow — left eye, inner (right) side is high
        display.drawLine(cx - 14, cy - 16, cx + 12, cy - 26, SH110X_WHITE);
        display.drawLine(cx - 14, cy - 15, cx + 12, cy - 25, SH110X_WHITE);
        display.drawLine(cx - 14, cy - 14, cx + 12, cy - 24, SH110X_WHITE);
    } else {
        // \ brow — right eye, inner (left) side is high
        display.drawLine(cx - 12, cy - 26, cx + 14, cy - 16, SH110X_WHITE);
        display.drawLine(cx - 12, cy - 25, cx + 14, cy - 15, SH110X_WHITE);
        display.drawLine(cx - 12, cy - 24, cx + 14, cy - 14, SH110X_WHITE);
    }

    // big teardrop
    if (tear) {
        display.drawLine(cx + 7, cy + 17, cx + 7, cy + 23, SH110X_WHITE);
        display.fillCircle(cx + 7, cy + 27, 4, SH110X_WHITE);
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
    const int expectedState = animState;

    for (int i = 0; i <= 32; i += 4) {
        if (animationStateChanged(expectedState)) {
            return;
        }

        display.clearDisplay();
        draw_closing(34, 34, i);
        draw_closing(94, 34, i);
        display.display();
        if (delayInterruptible(60, expectedState)) {
            return;
        }
    }

    if (delayInterruptible(300, expectedState)) {
        return;
    }

    for (int i = 32; i >= 0; i -= 4) {
        if (animationStateChanged(expectedState)) {
            return;
        }

        display.clearDisplay();
        draw_closing(34, 34, i);
        draw_closing(94, 34, i);
        display.display();
        if (delayInterruptible(60, expectedState)) {
            return;
        }
    }
}

// ================================================================
//  STAR EYES
// ================================================================

// 4-point sparkle cross
static void draw_sparkle(int cx, int cy, int r) {
    display.drawLine(cx - r, cy,     cx + r, cy,     SH110X_WHITE);
    display.drawLine(cx,     cy - r, cx,     cy + r, SH110X_WHITE);
    display.drawLine(cx - r, cy - r, cx + r, cy + r, SH110X_WHITE);
    display.drawLine(cx - r, cy + r, cx + r, cy - r, SH110X_WHITE);
}

// 5-pointed star outline + filled center
static void draw_star_shape(int cx, int cy, int R, int r) {
    // outer and inner vertices, ×1000 unit circle, top-first clockwise
    static const int OX[] = {    0,  951,  588, -588, -951 };
    static const int OY[] = { -1000, -309,  809,  809, -309 };
    static const int IX[] = {  588,  951,    0, -951, -588 };
    static const int IY[] = { -809,  309, 1000,  309, -809 };
    for (int i = 0; i < 5; i++) {
        int ni = (i + 1) % 5;
        display.drawLine(cx + OX[i] *R/1000, cy + OY[i] *R/1000,
                         cx + IX[i] *r/1000, cy + IY[i] *r/1000, SH110X_WHITE);
        display.drawLine(cx + IX[i] *r/1000, cy + IY[i] *r/1000,
                         cx + OX[ni]*R/1000, cy + OY[ni]*R/1000, SH110X_WHITE);
    }
    display.fillCircle(cx, cy, r - 1, SH110X_WHITE);
    display.fillCircle(cx, cy, 3, SH110X_BLACK);  // pupil
    display.fillCircle(cx + 2, cy - 2, 1, SH110X_WHITE);  // glint
}

struct SparklePos { int8_t dx, dy, r; };
static const SparklePos SPARKLES[] = {
    {  0, -19, 2 }, { 13, -14, 1 }, { 19,  0, 2 },
    { 13,  14, 1 }, {  0,  19, 2 }, {-13,  14, 1 },
    {-19,   0, 2 }, {-13, -14, 1 }, {  7, -22, 1 },
    { 22,  -7, 1 }, {-22,   7, 1 }, { -7,  22, 1 },
};

// phase 0-11: each sparkle blinks offset from the others
static void draw_star_eye(int cx, int cy, uint8_t phase) {
    draw_star_shape(cx, cy, 14, 6);
    for (int i = 0; i < 12; i++) {
        if (((i + phase) % 4) < 2) {   // on 2 frames, off 2 frames
            int sx = cx + SPARKLES[i].dx;
            int sy = cy + SPARKLES[i].dy;
            if (sx > 1 && sx < 126 && sy > 1 && sy < 62)
                draw_sparkle(sx, sy, SPARKLES[i].r);
        }
    }
}

void show_star() {
    display.clearDisplay();
    draw_star_eye(34, 34, 0);
    draw_star_eye(94, 34, 3);   // offset phase so eyes twinkle independently
    display.display();
}

// Animated burst: sparkles cycle outward for ~1 second
void show_star_burst() {
    const int expectedState = animState;

    for (uint8_t frame = 0; frame < 8; frame++) {
        if (animationStateChanged(expectedState)) {
            return;
        }

        display.clearDisplay();
        draw_star_eye(34, 34, frame);
        draw_star_eye(94, 34, frame + 3);
        display.display();

        if (delayInterruptible(120, expectedState)) {
            return;
        }
    }
}

// ================================================================
//  HEART EYES
// ================================================================

// r = overall size (half-height of heart)
static void draw_heart(int cx, int cy, int r) {
    int bump_r = r * 6 / 12;          // radius of the two top bumps
    int offset = r * 5 / 12;          // horizontal offset of each bump from center
    int drop   = r * 3 / 12;          // bump centers sit this far above cy

    int bump_y  = cy - drop;
    int tri_bot = cy + r - drop;

    // two circles for the top lobes
    display.fillCircle(cx - offset, bump_y, bump_r, SH110X_WHITE);
    display.fillCircle(cx + offset, bump_y, bump_r, SH110X_WHITE);

    // filled triangle bridging down to the bottom point
    display.fillTriangle(
        cx - offset - bump_r, bump_y,
        cx + offset + bump_r, bump_y,
        cx,                   tri_bot,
        SH110X_WHITE
    );

    // glint — small dark hollow in upper-left lobe
    if (bump_r >= 4)
        display.fillCircle(cx - offset + 1, bump_y - bump_r / 2, bump_r / 3, SH110X_BLACK);
}

void show_hearts() {
    display.clearDisplay();
    draw_heart(34, 34, 11);
    draw_heart(94, 34, 11);
    display.display();
}

// Lub-dub heartbeat pulsation — runs one full beat cycle (~1.1 s)
void show_hearts_pulse() {
    const int expectedState = animState;

    // {size, hold_ms}  — lub: quick expand; dub: second softer beat; rest: contract
    static const struct { int8_t r; uint16_t ms; } BEAT[] = {
        {  9, 60  },   // resting
        { 13, 80  },   // lub  — first beat, bigger
        {  9, 60  },   // release
        { 11, 70  },   // dub  — second beat, softer
        {  8, 80  },   // release
        {  8, 300 },   // rest before next cycle
    };
    for (int i = 0; i < 6; i++) {
        if (animationStateChanged(expectedState)) {
            return;
        }

        display.clearDisplay();
        draw_heart(34, 34, BEAT[i].r);
        draw_heart(94, 34, BEAT[i].r);
        display.display();

        if (delayInterruptible(BEAT[i].ms, expectedState)) {
            return;
        }
    }
}

// ================================================================
//  BREATHING
// ================================================================
void breathing() {
    const int expectedState = animState;

    if (animationStateChanged(expectedState)) {
        return;
    }

    show_happy();

    if (delayInterruptible(1000, expectedState)) {
        return;
    }

    show_closing();
}

void show_sad_then() {
    const int expectedState = animState;

    if (animationStateChanged(expectedState)) {
        return;
    }

    show_sad();

    if (delayInterruptible(1000, expectedState)) {
        return;
    }

    show_closing();
}

// ================================================================
//  INIT (call from main setup())
// ================================================================
void initEyes() {
    delay(250);
    display.begin(i2c_Address, true);
    display.clearDisplay();
    display.display();
}

// ================================================================
//  LOOP
// ================================================================
// void loop() {
//   if (is_slouching)  
//   {
//     show_sad();
//   }
//   breathing();
// }