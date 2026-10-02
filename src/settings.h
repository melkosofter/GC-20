// Persistent settings: CRC-protected record in emulated EEPROM, saved
// a few seconds after the last change to limit flash wear.
#pragma once
#include <Arduino.h>

struct Settings {
    uint16_t alarmThreshold10;   // alarm threshold, 0.1 uSv/h
    uint16_t tubeSensitivity;    // CPM per uSv/h
    uint16_t deadTime;           // us
    uint16_t backgroundCPM;      // tube's own background, subtracted
    uint8_t  buzzerEnabled;
    uint8_t  ledEnabled;
    uint8_t  integrationMode;    // index into WIN_LEN
    uint8_t  timedInterval;      // minutes
};

extern Settings settings;

void loadSettings();
void markSettingsDirty();
void settingsTick(unsigned long now);   // performs the deferred save
