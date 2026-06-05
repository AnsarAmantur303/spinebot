#pragma once

void initEyes();        // call once from setup() — starts the eyes task

void show_happy();
void show_sad();
void show_closing();

void breathing();       // show_happy → 1 s → blink closed
void show_sad_then();   // show_sad  → 1 s → blink closed

void show_star();       // star-shaped eyes with radiating sparkles (static)
void show_star_burst(); // animated version — sparkles cycle ~1 s

void show_hearts();         // heart-shaped eyes (static)
void show_hearts_pulse();   // lub-dub heartbeat pulsation (~1.1 s per cycle)

void drawBaseEye(int cx, int cy);
void draw_happy(int cx, int cy);
void draw_sad(int cx, int cy, bool leftEye, bool tear);
void draw_closing(int cx, int cy, int closeAmount);
