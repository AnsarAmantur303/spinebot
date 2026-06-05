// Prototypes for eyes display functions
#ifndef EYES_H
#define EYES_H

#include <Arduino.h>

void initEyes();

void breathing();
void show_happy();
void show_sad();
void show_closing();
void show_sad_then();

void drawBaseEye(int cx, int cy);
void draw_happy(int cx, int cy);
void draw_sad(int cx, int cy, bool leftEye, bool tear);
void draw_closing(int cx, int cy, int closeAmount);

#endif // EYES_H
