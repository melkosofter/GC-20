// Pulse counting, rate/dose math, alarm state and battery level.
// All dose values are in uSv/h (rate) and uSv (accumulated).
#pragma once
#include <Arduino.h>

#define RING_LEN 300                 // seconds of per-second counts kept
extern const uint16_t WIN_LEN[3];    // integration windows, s: 10 / 60 / 300

enum DoseLevel : uint8_t { LVL_NORMAL, LVL_ELEVATED, LVL_ALARM, LVL_OVERLOAD };

extern volatile uint32_t totalPulseCount;

extern float     avgCPM;          // dead-time corrected, current window
extern float     uncertaintyCPM;  // 1 sigma
extern float     doseRate;        // uSv/h
extern double    totalDose;       // uSv since boot
extern bool      overload;
extern DoseLevel doseLevel;
extern bool      alarmActive;
extern bool      alarmMuted;
extern uint32_t  elapsedSec;      // measured seconds since boot
extern uint8_t   batteryPercent;
extern bool      batteryUpdated;  // set when batteryPercent was re-read this tick

void  measurementBegin();
bool  measurementTick(unsigned long now);   // true when a 1 s slot closed
void  computeRates();                       // re-derive after settings change
float deadTimeFactor(float cps);
float calcDoseRate(float cpm);
uint32_t cpmSecondsAgo(uint16_t back);      // corrected CPM of one 1 s slot
bool  alarmSounding();
void  muteAlarm();
