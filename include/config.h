// Hardware pins, display constants and measurement tuning.
#pragma once
#include <Arduino.h>

#define FW_VERSION "3.4"

// ============================================================================
// PIN DEFINITIONS — GC-20 / Wemos D1 mini + TPM408-2.8
// ============================================================================
#define TFT_CS      D8
#define TFT_DC      D4
#define TOUCH_CS    D2
#define BUZZER_PIN  D0      // GPIO16 — driven via GP16O register in ISRs
#define LED_PIN     D3      // GPIO0  — driven via GPOS/GPOC registers in ISRs
#define BATTERY_PIN A0
#define INT_PIN     D1      // GPIO5, falling edge per tube pulse

// TPM408-2.8 touch calibration
#define TS_MINX  320
#define TS_MINY  470
#define TS_MAXX  3800
#define TS_MAXY  3826

// ============================================================================
// COLORS (RGB565)
// ============================================================================
#define C_BLACK       0x0000
#define C_WHITE       0xFFFF
#define C_RED         0xF800
#define C_GREEN       0x07E0
#define C_YELLOW      0xFFE0

#define C_BG          0x0000
#define C_CARD_BG     0x0841
#define C_HEADER_BG   0x18C3
#define C_ACCENT      0x2A86
#define C_ACCENT2     0x3B8F
#define C_DIM         0x94B2

#define C_CARD_OK     0x0455
#define C_CARD_WARN   0x6240
#define C_CARD_DANGER 0x6000

#define C_BTN_BG      0x18C3
#define C_BTN_ON      0x0442
#define C_BTN_OFF     0x4208

// ============================================================================
// MEASUREMENT
// ============================================================================
#define ELEVATED_USVH      0.5f   // "ELEVATED" level, uSv/h
#define ALARM_HYSTERESIS   0.9f   // alarm clears below threshold * this
#define ALARM_MIN_SECONDS  10     // data needed before a dose-rate alarm
#define OVERLOAD_FRACTION  0.5f   // overload when cps * deadTime reaches this
#define PULSE_DEBOUNCE_US  50

// ============================================================================
// OUTPUT
// ============================================================================
#define CLICK_US           200    // click / LED flash length
#define ALARM_TONE_HZ      2700
