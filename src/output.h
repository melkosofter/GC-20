// Click / LED flash per pulse and the alarm tone, both timed by hardware
// timer1 so their length doesn't depend on how long loop() is busy drawing.
// timer1 is owned here: don't use tone(), analogWrite() or Servo.
#pragma once
#include <Arduino.h>

void outputBegin();
void IRAM_ATTR outputPulseFromIsr();             // called from the pulse ISR
void outputAlarm(bool sounding, bool fast, unsigned long now);   // every loop
