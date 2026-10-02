/*  GC-20 Geiger Counter v3.2
    SBM-20 radiation monitor — 2.8" TFT touchscreen (TPM408 / ILI9341 + XPT2046)
    Standalone instrument. No WiFi, no cloud.
    Uses built-in font at larger sizes — reliable on all ESP8266 boards.

    Original: Prabhat (pra22@pitt.edu) — CC BY-SA 4.0
*/

// ============================================================================
// INCLUDES
// ============================================================================
#include <Arduino.h>
#include <EEPROM.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

// ============================================================================
// PIN DEFINITIONS — TPM408-2.8
// ============================================================================
#define TFT_CS      D8
#define TFT_DC      D4
#define TOUCH_CS    D2
#define BUZZER_PIN  D0
#define LED_PIN     D3
#define BATTERY_PIN A0
#define INT_PIN     5

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
#define C_CYAN        0x07FF
#define C_BLUE        0x001F

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
#define C_BTN_DIM     0x1082

// ============================================================================
// SMALL BITMAPS (PROGMEM)
// ============================================================================

// Gamma — 18x18 (3 bytes/row = 54 bytes)
const unsigned char gammaIcon[] PROGMEM = {
	0x30, 0x00, 0x78, 0x70, 0xe8, 0xe0, 0xc4, 0xe0, 0x84, 0xc0, 0x05, 0xc0, 0x05, 0x80, 0x07, 0x80, 
	0x03, 0x00, 0x07, 0x00, 0x0e, 0x00, 0x0e, 0x00, 0x1e, 0x00, 0x1e, 0x00, 0x1e, 0x00, 0x3e, 0x00, 
	0x1c, 0x00, 0x00, 0x00};

// Beta — 18x18 (3 bytes/row)
const unsigned char betaIcon[] PROGMEM = {
    0x00,0xc0,0x00,0x03,0xf0,0x00,0x07,0x18,0x00,0x06,0x18,0x00,
    0x0e,0x18,0x00,0x0e,0x18,0x00,0x0e,0xf8,0x00,0x0e,0x1c,0x00,
    0x0e,0x0c,0x00,0x0e,0x0c,0x00,0x0e,0x0c,0x00,0x0e,0x0c,0x00,
    0x0f,0x1c,0x00,0x0f,0xf8,0x00,0x0e,0x00,0x00,0x0e,0x00,0x00,
    0x0c,0x00,0x00,0x00,0x00,0x00 };

// Speaker ON — 24x16
const unsigned char speakerOn[] PROGMEM = {
    0x01,0xc0,0x00,0x02,0x40,0x00,0x04,0x42,0x00,0x08,0x41,0x00,
    0xf0,0x50,0x80,0x80,0x48,0x80,0x80,0x44,0x40,0x80,0x44,0x40,
    0x80,0x44,0x40,0x80,0x48,0x80,0xf0,0x50,0x80,0x08,0x41,0x00,
    0x04,0x42,0x00,0x02,0x40,0x00,0x01,0xc0,0x00,0x00,0x00,0x00 };

// Speaker OFF — 24x16
const unsigned char speakerOff[] PROGMEM = {
    0x80,0xe0,0x00,0x41,0x20,0x00,0x22,0x20,0x00,0x14,0x20,0x00,
    0x78,0x20,0x00,0x44,0x20,0x00,0x42,0x20,0x00,0x41,0x20,0x00,
    0x40,0xa0,0x00,0x40,0x60,0x00,0x78,0x20,0x00,0x04,0x30,0x00,
    0x02,0x28,0x00,0x01,0x24,0x00,0x00,0xe2,0x00,0x00,0x00,0x00 };

// Menu gear — 16x16
const unsigned char menuIcon[] PROGMEM = {
    0x03,0xc0,0x12,0x48,0x2c,0x34,0x40,0x02,0x23,0xc4,0x24,0x24,
    0xc8,0x13,0x88,0x11,0x88,0x11,0xc8,0x13,0x24,0x24,0x23,0xc4,
    0x40,0x02,0x2c,0x34,0x12,0x48,0x03,0xc0 };

// Timed clock — 16x16
const unsigned char timedIcon[] PROGMEM = {
    0x07,0xc0,0x18,0x30,0x29,0x28,0x41,0x04,0x61,0x0c,0x81,0x02,
    0x81,0x02,0xe1,0x0e,0x80,0x82,0x80,0x42,0x60,0x2c,0x40,0x04,
    0x29,0x28,0x19,0x30,0x07,0xc0,0x00,0x00 };

// LED ON — 16x16
const unsigned char ledOnIcon[] PROGMEM = {
    0x20,0x04,0x13,0xc8,0x04,0x20,0x08,0x10,0xa9,0x15,0x09,0x90,
    0x09,0x10,0x24,0x24,0x42,0x42,0x01,0x00,0x03,0xc0,0x00,0x00,
    0x03,0xc0,0x00,0x00,0x01,0x80,0x00,0x00 };

// LED OFF — 16x7
const unsigned char ledOffIcon[] PROGMEM = {
    0x3c,0x42,0x81,0x81,0x81,0x81,0x42,0x24,0x10,0x3c,0x00,0x3c,
    0x00,0x18 };

// Back arrow — 16x7
const unsigned char backArrow[] PROGMEM = {
    0x03,0x00,0x00,0x03,0x00,0x00,0x0f,0x00,0x00,0x0f,0x00,0x00,0x3f,0xff,0xc0,0x3f,0xff,0xc0,0xff,0xff,0xc0,0xff,0xff,0xc0,0x3f,0xff,0xc0,0x3f,0xff,0xc0,0x0f,0x00,0x00,0x0f,0x00,0x00,0x03,0x00,0x00,0x03,0x00,0x00 };

// Splash screen logo -- 65x61
const unsigned char splashLogo[] PROGMEM = {0x00,0x06,0x00,0x00,0x00,0x00,0x30,0x00,0x00,0x00,0x0f,0x00,0x00,0x00,0x00,0x78,0x00,0x00,0x00,0x1f,0x80,0x00,0x00,0x00,0xfc,0x00,0x00,0x00,0x7f,0xc0,0x00,0x00,0x01,0xfe,0x00,0x00,0x00,0xff,0xc0,0x00,0x00,0x01,0xff,0x00,0x00,0x00,0xff,0xe0,0x00,0x00,0x03,0xff,0x80,0x00,0x01,0xff,0xe0,0x00,0x00,0x07,0xff,0xc0,0x00,0x03,0xff,0xf0,0x00,0x00,0x07,0xff,0xe0,0x00,0x07,0xff,0xf8,0x00,0x00,0x0f,0xff,0xf0,0x00,0x07,0xff,0xf8,0x00,0x00,0x1f,0xff,0xf0,0x00,0x0f,0xff,0xfc,0x00,0x00,0x1f,0xff,0xf8,0x00,0x1f,0xff,0xfe,0x00,0x00,0x3f,0xff,0xfc,0x00,0x1f,0xff,0xfe,0x00,0x00,0x3f,0xff,0xfc,0x00,0x3f,0xff,0xff,0x00,0x00,0x7f,0xff,0xfe,0x00,0x3f,0xff,0xff,0x80,0x00,0xff,0xff,0xfe,0x00,0x7f,0xff,0xff,0x80,0x00,0xff,0xff,0xfe,0x00,0x7f,0xff,0xff,0xc0,0x01,0xff,0xff,0xff,0x00,0x7f,0xff,0xff,0xe0,0x03,0xff,0xff,0xff,0x00,0x7f,0xff,0xff,0xe0,0x03,0xff,0xff,0xff,0x00,0xff,0xff,0xff,0xc0,0x01,0xff,0xff,0xff,0x80,0xff,0xff,0xff,0x80,0x00,0xff,0xff,0xff,0x80,0xff,0xff,0xff,0x03,0xe0,0x7f,0xff,0xff,0x80,0xff,0xff,0xfe,0x0f,0xf8,0x3f,0xff,0xff,0x80,0xff,0xff,0xfe,0x1f,0xfc,0x3f,0xff,0xff,0x80,0xff,0xff,0xfc,0x1f,0xfc,0x1f,0xff,0xff,0x80,0xff,0xff,0xfc,0x3f,0xfe,0x1f,0xff,0xff,0x80,0xff,0xff,0xfc,0x3f,0xfe,0x1f,0xff,0xff,0x80,0x7f,0xff,0xfc,0x3f,0xfe,0x1f,0xff,0xff,0x00,0x00,0x00,0x00,0x3f,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3f,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1f,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1f,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0xf8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3c,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7f,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7f,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x80,0x00,0x00,0x00,0x00,0x00,0x01,0xff,0xff,0xc0,0x00,0x00,0x00,0x00,0x00,0x03,0xff,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x03,0xff,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0xff,0xf0,0x00,0x00,0x00,0x00,0x00,0x0f,0xff,0xff,0xf8,0x00,0x00,0x00,0x00,0x00,0x0f,0xff,0xff,0xf8,0x00,0x00,0x00,0x00,0x00,0x1f,0xff,0xff,0xfc,0x00,0x00,0x00,0x00,0x00,0x3f,0xff,0xff,0xfc,0x00,0x00,0x00,0x00,0x00,0x3f,0xff,0xff,0xfe,0x00,0x00,0x00,0x00,0x00,0x7f,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x7f,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x80,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x80,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x80,0x00,0x00,0x00,0x00,0x7f,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x3f,0xff,0xff,0xfe,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0xff,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xf0,0x00,0x00,0x00,0x00};

// ============================================================================
// HARDWARE
// ============================================================================
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
XPT2046_Touchscreen ts(TOUCH_CS);

// ============================================================================
// MEASUREMENT STATE (volatile — ISR)
// ============================================================================
volatile unsigned long pulseCount      = 0;
volatile unsigned long totalPulseCount  = 0;
volatile unsigned long lastIntMicros   = 0;

// ============================================================================
// SETTINGS (EEPROM)
// ============================================================================
uint8_t  doseUnits       = 0;
uint8_t  alarmThreshold  = 5;
uint16_t tubeSensitivity = 175;
uint16_t deadTime        = 190;
uint16_t backgroundCPM   = 0;
uint8_t  buzzerEnabled   = 1;
uint8_t  ledEnabled      = 1;

// ============================================================================
// DERIVED MEASUREMENTS
// ============================================================================
float    doseRate        = 0.0f;
float    totalDose       = 0.0f;
float    avgCPM          = 0.0f;
float    uncertaintyCPM  = 0.0f;
uint8_t  doseLevel       = 0;
uint8_t  prevDoseLevel   = 255;
char     lastDoseStr[12] = "";

// ============================================================================
// RING BUFFER & INTEGRATION
// ============================================================================
uint16_t cpmRing[60];
uint8_t  ringIndex       = 0;
uint8_t  integrationMode = 1;
float    slowSum         = 0.0f;
uint16_t slowCount       = 0;
uint16_t lastValidCount  = 0;

// ============================================================================
// TIMING & PULSE OUTPUT
// ============================================================================
unsigned long currentMillis    = 0;
unsigned long prevSecMillis    = 0;
unsigned long pulseOffMicros   = 0;
unsigned long lastPulseTotal   = 0;
bool buzzerActive = false;
bool ledActive    = false;

// ============================================================================
// UI STATE
// ============================================================================
uint8_t  page             = 0;
uint8_t  selectedCalParam = 0;
uint8_t  calGuidePg       = 0;    // Calibration guide page number
bool     wasTouched       = false;
int      touchX = 0, touchY = 0;
bool     dashboardBuilt   = false;

// ============================================================================
// BATTERY
// ============================================================================
uint8_t  batteryPercent   = 0;
uint8_t  batUpdateCtr      = 0;
float    batterySmoothed   = -1;  // -1 = not initialized

// ============================================================================
// TIMED COUNT
// ============================================================================
uint8_t  timedInterval    = 5;
unsigned long timedStartMillis = 0;
unsigned long timedElapsed     = 0;
bool     timedRunning     = false;
bool     timedComplete    = false;
unsigned long timedCountsAtStart = 0;

// ============================================================================
// EEPROM ADDRESSES (10 bytes)
// ============================================================================
// 0:doseUnits 1:alarmThreshold 2-3:tubeSensitivity 4-5:deadTime
// 6-7:backgroundCPM 8:buzzerEnabled 9:ledEnabled

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
void IRAM_ATTR isr();

float  correctDeadTime(float cps, uint16_t deadUs);
float  calcDoseRate(float cpm);
float  calcUncertainty(float cpm, uint16_t periodSec);
void   formatDose(char* buf, float dose);
uint8_t getDoseLevel(float dr);

void loadSettings();
void saveSettings();

void drawFrame(const char* title);
void drawBackButton();
void drawBattery(int batX, int batY);

void drawDashboard();
void drawSettingsMenu();
void drawUnitsPage();
void drawAlertPage();
void drawCalibrationPage();
void drawCalibrationGuide();
void drawTimedCountSetup();
void drawTimedCountRunning();
void drawTimedComplete();
void updateTimedCountDisplay();
void drawAboutPage();

// Dashboard partial update helpers
void updateDashboardData();
void redrawTrendGraph();
void redrawToolbar();

// Page handlers
void handlePage0();
void handlePage1();
void handlePage2();
void handlePage3();
void handlePage4();
void handlePage5();
void handlePage6();
void handlePage7();
void handlePage8();

void handlePulseOutput();
void startTimedCount();
void stopTimedCount();
bool readTouch();

// Text helpers — default font (6x8 per char at size 1)
int textW(const char* s, uint8_t sz) { return strlen(s) * 6 * sz; }
int textH(uint8_t sz)               { return 8 * sz; }

// ============================================================================
// ISR
// ============================================================================
void IRAM_ATTR isr() {
    unsigned long now = micros();
    if ((now - lastIntMicros) > 50) {
        pulseCount++;
        totalPulseCount++;
        lastIntMicros = now;
    }
}

// ============================================================================
// MEASUREMENT MATH
// ============================================================================
float correctDeadTime(float cps, uint16_t deadUs) {
    if (cps <= 0.0f) return 0.0f;
    float tau = (float)deadUs / 1000000.0f;
    float denom = 1.0f - cps * tau;
    if (denom <= 0.01f) return cps * 1.5f;
    float corrected = cps / denom;
    if (corrected > cps * 1.5f) corrected = cps * 1.5f;
    return corrected;
}

float calcDoseRate(float cpm) {
    float eff = cpm - (float)backgroundCPM;
    if (eff < 0.0f) eff = 0.0f;
    float rate = eff / (float)tubeSensitivity;
    if (doseUnits == 1) rate /= 10.0f;
    return rate;
}

float calcUncertainty(float cpm, uint16_t periodSec) {
    if (periodSec == 0) return 0.0f;
    float n = cpm * (float)periodSec / 60.0f;
    if (n <= 0.0f) return 0.0f;
    return sqrt(n) * 60.0f / (float)periodSec;
}

void formatDose(char* buf, float dose) {
    if (dose < 0.01f)       sprintf(buf, "0.00");
    else if (dose < 1.0f)   sprintf(buf, "%.2f", dose);
    else if (dose < 10.0f)  sprintf(buf, "%.2f", dose);
    else if (dose < 100.0f) sprintf(buf, "%.1f", dose);
    else                    sprintf(buf, "%.0f", dose);
}

uint8_t getDoseLevel(float dr) {
    if (dr < 0.5f) return 0;
    if (dr < (float)alarmThreshold) return 1;
    return 2;
}

// ============================================================================
// EEPROM
// ============================================================================
void loadSettings() {
    EEPROM.begin(512);
    uint8_t v;
    v = EEPROM.read(0); doseUnits = (v <= 1) ? v : 0;
    v = EEPROM.read(1); alarmThreshold = (v >= 2 && v <= 100) ? v : 5;
    uint8_t lo = EEPROM.read(2), hi = EEPROM.read(3);
    uint16_t tv = ((uint16_t)hi << 8) | lo;
    tubeSensitivity = (tv >= 1 && tv <= 999) ? tv : 175;
    lo = EEPROM.read(4); hi = EEPROM.read(5);
    tv = ((uint16_t)hi << 8) | lo;
    deadTime = (tv >= 10 && tv <= 2000) ? tv : 190;
    lo = EEPROM.read(6); hi = EEPROM.read(7);
    tv = ((uint16_t)hi << 8) | lo;
    backgroundCPM = (tv <= 500) ? tv : 0;
    v = EEPROM.read(8); buzzerEnabled = (v <= 1) ? v : 1;
    v = EEPROM.read(9); ledEnabled = (v <= 1) ? v : 1;
    EEPROM.end();
}

void saveSettings() {
    EEPROM.begin(512);
    EEPROM.write(0, doseUnits);
    EEPROM.write(1, alarmThreshold);
    EEPROM.write(2, tubeSensitivity & 0xFF);
    EEPROM.write(3, (tubeSensitivity >> 8) & 0xFF);
    EEPROM.write(4, deadTime & 0xFF);
    EEPROM.write(5, (deadTime >> 8) & 0xFF);
    EEPROM.write(6, backgroundCPM & 0xFF);
    EEPROM.write(7, (backgroundCPM >> 8) & 0xFF);
    EEPROM.write(8, buzzerEnabled);
    EEPROM.write(9, ledEnabled);
    EEPROM.commit();
    EEPROM.end();
}

// ============================================================================
// TOUCH INPUT
// ============================================================================
bool readTouch() {
    if (!ts.touched()) { wasTouched = false; return false; }
    if (wasTouched) return false;
    wasTouched = true;
    TS_Point p = ts.getPoint();
    touchX = map(p.x, TS_MINX, TS_MAXX, 0, 239);
    touchY = map(p.y, TS_MINY, TS_MAXY, 0, 319);
    if (touchX < 0) touchX = 0;
    if (touchX > 239) touchX = 239;
    if (touchY < 0) touchY = 0;
    if (touchY > 319) touchY = 319;
    return true;
}

// ============================================================================
// UI TOOLKIT — default font only, size 1 = 6x8, size 2 = 12x16, size 3 = 18x24
// ============================================================================
void drawFrame(const char* title) {
    tft.fillRect(0, 0, 240, 24, C_HEADER_BG);
    tft.fillRect(0, 24, 240, 296, C_BG);
    tft.drawFastHLine(0, 24, 240, C_ACCENT);
    tft.setTextSize(2);
    tft.setTextColor(C_ACCENT2, C_HEADER_BG);
    int tw = textW(title, 2);
    tft.setCursor((240 - tw) / 2, 4);
    tft.print(title);
}

void drawBackButton() {
    tft.fillRoundRect(3, 284, 58, 32, 5, C_BTN_BG);
    tft.drawRoundRect(3, 284, 58, 32, 5, C_ACCENT);
    tft.drawBitmap(3 + (58 - 18) / 2, 284 + (32 - 14) / 2,
                  backArrow, 18, 14, C_WHITE);
}

void drawBattery(int batX, int batY) {
    tft.drawRect(batX, batY, 24, 10, C_WHITE);
    tft.fillRect(batX + 24, batY + 2, 2, 6, C_WHITE);
    uint8_t fw = map(batteryPercent, 0, 100, 0, 20);
    uint16_t c = C_GREEN;
    if (batteryPercent < 30) c = C_RED;
    else if (batteryPercent < 60) c = C_YELLOW;
    if (fw > 0) tft.fillRect(batX + 2, batY + 1, fw, 8, c);
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, C_HEADER_BG);
    char pct[8]; sprintf(pct, "%u%%", batteryPercent);
    int pw = textW(pct, 1);
    tft.setCursor(batX - pw - 4, batY + 1);
    tft.print(pct);
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(38400);
    delay(100);

    tft.begin();
    tft.setRotation(2);
    tft.fillScreen(C_BLACK);

    // Splash — logo 65x61 centered + title
    tft.drawBitmap((240 - 65) / 2, 111, splashLogo, 65, 61, C_GREEN);
    tft.setTextSize(2);
    tft.setTextColor(C_WHITE, C_BG);
    const char* title = "GEIGER COUNTER";
    tft.setCursor((240 - textW(title, 2)) / 2, 186);
    tft.print(title);
    tft.setTextSize(1);
    tft.setTextColor(C_DIM, C_BG);
    const char* ver = "SBM-20  /  v3.2";
    tft.setCursor((240 - textW(ver, 1)) / 2, 206);
    tft.print(ver);

    ts.begin();
    ts.setRotation(2);

    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    pinMode(INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(INT_PIN), isr, FALLING);

    loadSettings();
    for (uint8_t i = 0; i < 60; i++) cpmRing[i] = 0;

    // Initial battery read — avoid showing 0% on boot
    int raw = analogRead(BATTERY_PIN);
    raw = constrain(raw, 607, 797);
    batterySmoothed = (float)raw;
    if      (raw >= 797) batteryPercent = 100;
    else if (raw >= 771) batteryPercent = map(raw, 771, 797, 80, 100);
    else if (raw >= 740) batteryPercent = map(raw, 740, 771, 60, 80);
    else if (raw >= 714) batteryPercent = map(raw, 714, 740, 40, 60);
    else if (raw >= 693) batteryPercent = map(raw, 693, 714, 20, 40);
    else if (raw >= 670) batteryPercent = map(raw, 670, 693, 10, 20);
    else if (raw >= 607) batteryPercent = map(raw, 607, 670, 0, 10);
    else                 batteryPercent = 0;

    delay(600);
    drawDashboard();
    dashboardBuilt = true;

    Serial.println(F("GC-20 v3.2 ready"));
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
    currentMillis = millis();
    handlePulseOutput();
    switch (page) {
        case 0: handlePage0(); break;
        case 1: handlePage1(); break;
        case 2: handlePage2(); break;
        case 3: handlePage3(); break;
        case 4: handlePage4(); break;
        case 5: handlePage5(); break;
        case 6: handlePage6(); break;
        case 7: handlePage7(); break;
        case 8: handlePage8(); break;
    }
    delay(5);
}

// ============================================================================
// PULSE OUTPUT
// ============================================================================
void handlePulseOutput() {
    unsigned long cur = totalPulseCount;
    if (cur != lastPulseTotal) {
        lastPulseTotal = cur;
        if (ledEnabled)    { digitalWrite(LED_PIN, HIGH); ledActive = true; }
        if (buzzerEnabled) { digitalWrite(BUZZER_PIN, HIGH); buzzerActive = true; }
        pulseOffMicros = micros();
    }
    if (micros() - pulseOffMicros >= 200) {
        if (ledActive)    { digitalWrite(LED_PIN, LOW); ledActive = false; }
        if (buzzerActive) { digitalWrite(BUZZER_PIN, LOW); buzzerActive = false; }
    }
}

// ============================================================================
// PAGE 0 — DASHBOARD HANDLER
// ============================================================================
void handlePage0() {
    if (currentMillis - prevSecMillis >= 1000) {
        prevSecMillis = currentMillis;

        noInterrupts();
        unsigned long cts = pulseCount;
        pulseCount = 0;
        interrupts();

        float cps = (float)cts;
        float corrCPS = correctDeadTime(cps, deadTime);
        uint16_t cpmNow = (uint16_t)(corrCPS * 60.0f);

        if (avgCPM > 10.0f && (float)cpmNow > avgCPM * 5.0f) {
            cpmNow = lastValidCount;
        } else {
            lastValidCount = cpmNow;
        }

        cpmRing[ringIndex] = cpmNow;
        ringIndex = (ringIndex + 1) % 60;

        float sum = 0;
        switch (integrationMode) {
            case 0:
                for (uint8_t i = 0; i < 10; i++)
                    sum += cpmRing[(ringIndex + 60 - 1 - i) % 60];
                avgCPM = sum / 10.0f;
                break;
            case 1:
                for (uint8_t i = 0; i < 60; i++) sum += cpmRing[i];
                avgCPM = sum / 60.0f;
                break;
            case 2:
                slowSum += (float)cpmNow; slowCount++;
                if (slowCount >= 300) {
                    avgCPM = slowSum / 300.0f;
                    slowSum = avgCPM * 150.0f; slowCount = 150;
                } else if (slowCount > 0) {
                    avgCPM = slowSum / (float)slowCount;
                }
                break;
            default:
                avgCPM = (float)cpmNow;
                break;
        }

        doseRate = calcDoseRate(avgCPM);

        if (integrationMode == 0)      uncertaintyCPM = calcUncertainty(avgCPM, 10);
        else if (integrationMode == 1) uncertaintyCPM = calcUncertainty(avgCPM, 60);
        else                           uncertaintyCPM = calcUncertainty(avgCPM, slowCount < 300 ? slowCount : 300);

        totalDose = (float)totalPulseCount /
            (60.0f * (float)tubeSensitivity * (doseUnits == 1 ? 10.0f : 1.0f));

        doseLevel = getDoseLevel(doseRate);

        batUpdateCtr++;
        if (batUpdateCtr >= 5) {
            batUpdateCtr = 0;
            int raw = analogRead(BATTERY_PIN);
            // Exponential moving average
            if (batterySmoothed < 0) {
                batterySmoothed = (float)raw;
            } else {
                batterySmoothed = batterySmoothed * 0.7f + (float)raw * 0.3f;
            }
            // Li-Ion non-linear: Wemos D1 Mini (220k/100k internal) + 220k external
            // raw=607(3.2V)..797(4.2V), cap at 100 for USB charging (5V→raw≈950)
            int r = (int)batterySmoothed;
            if      (r >= 797) batteryPercent = 100;
            else if (r >= 771) batteryPercent = map(r, 771, 797, 80, 100);
            else if (r >= 740) batteryPercent = map(r, 740, 771, 60, 80);
            else if (r >= 714) batteryPercent = map(r, 714, 740, 40, 60);
            else if (r >= 693) batteryPercent = map(r, 693, 714, 20, 40);
            else if (r >= 670) batteryPercent = map(r, 670, 693, 10, 20);
            else if (r >= 607) batteryPercent = map(r, 607, 670, 0, 10);
            else               batteryPercent = 0;
            // Clamp final value
            if (batteryPercent > 100) batteryPercent = 100;
        }

        updateDashboardData();
    }

    if (!readTouch()) return;

    // Toolbar Row 1  (y=233 h=46)
    if (touchY >= 233 && touchY <= 279) {
        if (touchX >= 3 && touchX <= 77) {
            page = 1; dashboardBuilt = false; drawSettingsMenu(); return;
        }
        if (touchX >= 83 && touchX <= 157) {
            integrationMode = (integrationMode + 1) % 3;
            slowSum = avgCPM * 150.0f; slowCount = 150;
            redrawToolbar(); return;
        }
        if (touchX >= 163 && touchX <= 236) {
            page = 6; dashboardBuilt = false; drawTimedCountSetup(); return;
        }
    }
    // Toolbar Row 2  (y=283 h=32)
    if (touchY >= 283 && touchY <= 315) {
        if (touchX >= 3 && touchX <= 115) {
            buzzerEnabled = !buzzerEnabled; saveSettings(); redrawToolbar(); return;
        }
        if (touchX >= 125 && touchX <= 236) {
            ledEnabled = !ledEnabled; saveSettings(); redrawToolbar(); return;
        }
    }
}

// ============================================================================
// DASHBOARD — FULL DRAW (page entry only)
// ============================================================================
void drawDashboard() {
    tft.fillScreen(C_BG);

    // Status bar (y=0-20)
    tft.fillRect(0, 0, 240, 19, C_HEADER_BG);
    tft.setTextSize(2);
    tft.setTextColor(C_ACCENT2, C_HEADER_BG);
    tft.setCursor(4, 2);

    // Radiation icons centered
    tft.drawBitmap(102, 0, gammaIcon, 12, 18, C_WHITE);
    tft.drawBitmap(122, 0, betaIcon, 18, 18, C_YELLOW);

    // Battery
    drawBattery(210, 4);

    // Initial card + strip
    uint16_t initBg = C_CARD_OK;
    tft.fillRoundRect(3, 23, 234, 82, 6, initBg);
    tft.drawRoundRect(3, 23, 234, 82, 6, C_GREEN);

    // Static graph border (drawn once, not cleared during updates)
    tft.drawRect(3, 148, 234, 64, C_DIM);

    updateDashboardData();
    redrawToolbar();
    prevDoseLevel = 255;
    strcpy(lastDoseStr, "");
}

// ============================================================================
// DASHBOARD — PARTIAL UPDATE (every second)
// ============================================================================
void updateDashboardData() {
    uint16_t cardBg, cardBorder;
    switch (doseLevel) {
        case 0: cardBg = C_CARD_OK; cardBorder = C_GREEN; break;
        case 1: cardBg = C_CARD_WARN; cardBorder = C_YELLOW; break;
        default: cardBg = C_CARD_DANGER; cardBorder = C_RED; break;
    }

    bool lvChg = (doseLevel != prevDoseLevel);

    // Card background + unit label + status strip — only on level change
    if (lvChg) {
        tft.fillRoundRect(3, 23, 234, 82, 4, cardBg);
        tft.drawRoundRect(3, 23, 234, 82, 4, cardBorder);

        tft.setTextSize(2);
        tft.setTextColor(C_WHITE, cardBg);
        const char* us = doseUnits == 0 ? "uSv/h" : "mR/h";
        int uw = textW(us, 2);
        tft.setCursor(3 + (234 - uw) / 2, 74);
        tft.print(us);

        // Status strip
        uint16_t sc; const char* st;
        switch (doseLevel) {
            case 0: sc = C_GREEN;  st = "NORMAL"; break;
            case 1: sc = C_YELLOW; st = "ELEVATED"; break;
            default: sc = C_RED;  st = "HIGH RADIATION"; break;
        }
        tft.fillRoundRect(3, 107, 234, 16, 4, sc);
        tft.setTextSize(1);
        tft.setTextColor(C_BLACK, sc);
        int sw = textW(st, 1);
        tft.setCursor(3 + (234 - sw) / 2, 111);
        tft.print(st);

        prevDoseLevel = doseLevel;
        strcpy(lastDoseStr, ""); // force dose redraw
    }

    // Dose number — unpadded, centered; setTextColor bg fills old pixels
    char d[8];
    formatDose(d, doseRate);

    if (strcmp(d, lastDoseStr) != 0) {
        tft.setTextSize(4);
        tft.setTextColor(C_WHITE, cardBg);
        int dw = textW(d, 4);
        tft.setCursor(3 + (234 - dw) / 2, 38);
        tft.print(d);
        strcpy(lastDoseStr, d);
    }

    // Stats row — fixed-width, print() overwrites without clearing
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, C_BG);
    tft.setCursor(5, 132);  tft.print("CPM:");

    char cpmF[8]; sprintf(cpmF, "%5u", (int)avgCPM);
    tft.setTextColor(C_ACCENT2, C_BG);
    tft.setCursor(36, 132); tft.print(cpmF);

    if (uncertaintyCPM >= 1.0f && integrationMode != 0) {
        tft.setTextColor(C_DIM, C_BG);
        tft.setCursor(72, 132);  tft.print("+/-");
        char uf[6]; sprintf(uf, "%3u", (int)uncertaintyCPM);
        tft.setCursor(96, 132);  tft.print(uf);
    }

    char tf[20]; sprintf(tf, "Total:%7lu", totalPulseCount);
    tft.setTextColor(C_WHITE, C_BG);
    int tw = textW(tf, 1);
    tft.setCursor(234 - tw, 132); tft.print(tf);

    // Trend graph
    redrawTrendGraph();

    // Battery
    if (batUpdateCtr == 0) drawBattery(210, 4);
}

// ============================================================================
// TREND GRAPH
// ============================================================================
void redrawTrendGraph() {
    const int GX = 3, GY = 148, GW = 234, GH = 64;
    const int TITLE_H = 10;  // title row height, not cleared each frame

    // Clear only the bar area, leave title + border intact
    tft.fillRect(GX + 1, GY + TITLE_H, GW - 2, GH - TITLE_H - 1, C_CARD_BG);

    // Title row
    tft.setTextSize(1);
    tft.setTextColor(C_DIM, C_CARD_BG);
    tft.setCursor(GX + 4, GY + 1);
    tft.print("CPM TREND");

    // Max label
    uint16_t maxV = 20;
    for (uint8_t i = 0; i < 60; i++) if (cpmRing[i] > maxV) maxV = cpmRing[i];
    if (maxV < 20) maxV = 20;
    if (maxV <= 100) maxV = ((maxV + 9) / 10) * 10;
    else if (maxV <= 500) maxV = ((maxV + 24) / 25) * 25;
    else maxV = ((maxV + 49) / 50) * 50;
    tft.setCursor(GX + GW - 35, GY + 1);
    tft.print(maxV);

    // Bars
    int pX = GX + 3, pW = GW - 6, pB = GY + GH - 2, pH = GH - TITLE_H - 2;
    float bw = (float)pW / 60.0f;

    for (uint8_t i = 0; i < 60; i++) {
        uint8_t idx = (ringIndex + i) % 60;
        uint16_t v = cpmRing[idx];
        int bh = (int)((float)v / (float)maxV * (float)pH);
        if (bh < 1 && v > 0) bh = 1;
        if (bh > pH) bh = pH;
        int bx = pX + (int)(i * bw);
        int bwi = (int)((i + 1) * bw) - (int)(i * bw);
        if (bwi < 1) bwi = 1;

        float bd = calcDoseRate((float)v);
        uint16_t bc = (bd >= alarmThreshold) ? C_RED : (bd >= 0.5f) ? C_YELLOW : C_GREEN;
        if (bh > 0) tft.fillRect(bx, pB - bh, bwi, bh, bc);
    }
}

// ============================================================================
// TOOLBAR REDRAW
// ============================================================================
void redrawToolbar() {
    tft.fillRect(0, 213, 240, 107, C_BG);

    const char* modeLbl;
    switch (integrationMode) {
        case 0: modeLbl = "10s"; break;
        case 1: modeLbl = "60s"; break;
        case 2: modeLbl = "5m"; break;
        default: modeLbl = "60s"; break;
    }

    // Row 1 — y=233 h=46
    tft.fillRoundRect(3, 233, 74, 46, 5, C_BTN_BG);
    tft.drawRoundRect(3, 233, 74, 46, 5, C_ACCENT);
    tft.drawBitmap(3 + (74 - 16) / 2, 233 + (46 - 16) / 2,
                  menuIcon, 16, 16, C_WHITE);

    tft.fillRoundRect(83, 233, 74, 46, 5, C_BTN_BG);
    tft.drawRoundRect(83, 233, 74, 46, 5, C_ACCENT);
    tft.setTextSize(2);
    tft.setTextColor(C_WHITE, C_BTN_BG);
    tft.setCursor(83 + (74 - textW(modeLbl, 2)) / 2, 248);
    tft.print(modeLbl);

    tft.fillRoundRect(163, 233, 73, 46, 5, C_BTN_BG);
    tft.drawRoundRect(163, 233, 73, 46, 5, C_ACCENT);
    tft.drawBitmap(163 + (73 - 16) / 2, 233 + (46 - 16) / 2,
                  timedIcon, 16, 16, C_WHITE);

    // Row 2 — y=283 h=32
    uint16_t buzBg = buzzerEnabled ? C_BTN_ON : C_BTN_OFF;
    tft.fillRoundRect(3, 283, 112, 32, 4, buzBg);
    tft.drawRoundRect(3, 283, 112, 32, 4, C_DIM);
    tft.drawBitmap(3 + (112 - 24) / 2, 283 + (32 - 16) / 2,
                  buzzerEnabled ? speakerOn : speakerOff, 24, 16, C_WHITE);

    uint16_t ledBg = ledEnabled ? C_BTN_ON : C_BTN_OFF;
    tft.fillRoundRect(125, 283, 112, 32, 4, ledBg);
    tft.drawRoundRect(125, 283, 112, 32, 4, C_DIM);
    if (ledEnabled) {
        tft.drawBitmap(125 + (112 - 16) / 2, 283 + (32 - 16) / 2,
                      ledOnIcon, 16, 16, C_WHITE);
    } else {
        tft.drawBitmap(125 + (112 - 8) / 2, 283 + (32 - 14) / 2,
                      ledOffIcon, 8, 14, C_WHITE);
    }
}

// ============================================================================
// PAGE 1 — SETTINGS MENU
// ============================================================================
void drawSettingsMenu() {
    drawFrame("SETTINGS");
    drawBackButton();

    const char* items[] = {
        "Dose Units", "Alarm Threshold", "Calibration",
        "Calibration Guide", "About Device"
    };
    for (uint8_t i = 0; i < 5; i++) {
        int by = 36 + i * 46;
        tft.fillRoundRect(6, by, 228, 40, 5, C_CARD_BG);
        tft.drawRoundRect(6, by, 228, 40, 5, C_ACCENT);
        tft.setTextSize(2);
        tft.setTextColor(C_WHITE, C_CARD_BG);
        tft.setCursor(16, by + 12);
        tft.print(items[i]);
        tft.setTextColor(C_ACCENT2, C_CARD_BG);
        tft.setCursor(215, by + 12);
        tft.print(">");
    }
}

void handlePage1() {
    if (!readTouch()) return;
    if (touchX >= 3 && touchX <= 61 && touchY >= 284 && touchY <= 316) {
        page = 0; dashboardBuilt = false; prevDoseLevel = 255;
        strcpy(lastDoseStr, ""); drawDashboard(); dashboardBuilt = true;
        return;
    }
    for (uint8_t i = 0; i < 5; i++) {
        int by = 36 + i * 46;
        if (touchX >= 6 && touchX <= 234 && touchY >= by && touchY <= by + 40) {
            switch (i) {
                case 0: page = 2; drawUnitsPage(); break;
                case 1: page = 3; drawAlertPage(); break;
                case 2: page = 4; drawCalibrationPage(); break;
                case 3: page = 5; drawCalibrationGuide(); break;
                case 4: page = 8; drawAboutPage(); break;
            }
            return;
        }
    }
}

// ============================================================================
// PAGE 2 — DOSE UNITS
// ============================================================================
void drawUnitsPage() {
    drawFrame("DOSE UNITS");
    drawBackButton();

    // uSv/h button
    bool isSv = (doseUnits == 0);
    uint16_t bg0 = isSv ? C_BTN_ON : C_BTN_DIM;
    uint16_t brd0 = isSv ? C_GREEN : C_DIM;
    tft.fillRoundRect(30, 60, 180, 50, 6, bg0);
    tft.drawRoundRect(30, 60, 180, 50, 6, brd0);
    tft.setTextSize(2);
    tft.setTextColor(C_WHITE, bg0);
    tft.setCursor(30 + (180 - textW("uSv/h", 2)) / 2, 78);
    tft.print("uSv/h");

    // mR/h button
    uint16_t bg1 = (!isSv) ? C_BTN_ON : C_BTN_DIM;
    uint16_t brd1 = (!isSv) ? C_GREEN : C_DIM;
    tft.fillRoundRect(30, 130, 180, 50, 6, bg1);
    tft.drawRoundRect(30, 130, 180, 50, 6, brd1);
    tft.setCursor(30 + (180 - textW("mR/h", 2)) / 2, 148);
    tft.print("mR/h");

    tft.setTextSize(1);
    tft.setTextColor(C_DIM, C_BG);
    const char* info = "1 mR/h = 10 uSv/h";
    tft.setCursor((240 - textW(info, 1)) / 2, 220);
    tft.print(info);
}

void handlePage2() {
    if (!readTouch()) return;
    if (touchX >= 3 && touchX <= 61 && touchY >= 284 && touchY <= 316) {
        saveSettings(); page = 1; drawSettingsMenu(); return;
    }
    if (touchX >= 30 && touchX <= 210 && touchY >= 60 && touchY <= 110) {
        doseUnits = 0; drawUnitsPage(); return;
    }
    if (touchX >= 30 && touchX <= 210 && touchY >= 130 && touchY <= 180) {
        doseUnits = 1; drawUnitsPage(); return;
    }
}

// ============================================================================
// PAGE 3 — ALARM THRESHOLD
// ============================================================================
void drawAlertPage() {
    drawFrame("ALARM THRESHOLD");
    drawBackButton();

    uint16_t brd = (alarmThreshold >= 20) ? C_RED : C_ACCENT;
    tft.fillRoundRect(45, 50, 150, 50, 6, C_CARD_BG);
    tft.drawRoundRect(45, 50, 150, 50, 6, brd);
    tft.setTextSize(3);
    tft.setTextColor(alarmThreshold >= 20 ? C_RED : C_WHITE, C_CARD_BG);
    char buf[8]; sprintf(buf, "%u", alarmThreshold);
    tft.setCursor(45 + (150 - textW(buf, 3)) / 2, 64);
    tft.print(buf);

    // - button — red
    tft.fillRoundRect(30, 125, 75, 50, 6, C_RED);
    tft.drawRoundRect(30, 125, 75, 50, 6, C_WHITE);
    tft.setTextSize(3);
    tft.setTextColor(C_WHITE, C_RED);
    tft.setCursor(30 + (75 - textW("-", 3)) / 2, 139);
    tft.print("-");

    // + button — green
    tft.fillRoundRect(135, 125, 75, 50, 6, C_GREEN);
    tft.drawRoundRect(135, 125, 75, 50, 6, C_WHITE);
    tft.setCursor(135 + (75 - textW("+", 3)) / 2, 139);
    tft.print("+");

    tft.setTextSize(1);
    tft.setTextColor(C_DIM, C_BG);
    const char* info1 = "Alarm triggers above threshold";
    const char* info2 = "Range: 2 - 100";
    tft.setCursor((240 - textW(info1, 1)) / 2, 210);
    tft.print(info1);
    tft.setCursor((240 - textW(info2, 1)) / 2, 224);
    tft.print(info2);
}

void handlePage3() {
    if (!readTouch()) return;
    if (touchX >= 3 && touchX <= 61 && touchY >= 284 && touchY <= 316) {
        saveSettings(); page = 1; drawSettingsMenu(); return;
    }
    if (touchX >= 30 && touchX <= 105 && touchY >= 125 && touchY <= 175) {
        if (alarmThreshold > 2) { alarmThreshold--; drawAlertPage(); } return;
    }
    if (touchX >= 135 && touchX <= 210 && touchY >= 125 && touchY <= 175) {
        if (alarmThreshold < 100) { alarmThreshold++; drawAlertPage(); } return;
    }
}

// ============================================================================
// PAGE 4 — CALIBRATION
// ============================================================================
void drawCalibrationPage() {
    drawFrame("CALIBRATION");
    drawBackButton();

    const char* names[] = {"Tube Sensitivity", "Dead Time", "Background CPM"};
    uint16_t vals[] = {tubeSensitivity, deadTime, backgroundCPM};
    const char* units[] = {"CPM/uSv/h", "us", "CPM"};

    for (uint8_t i = 0; i < 3; i++) {
        int py = 35 + i * 50;
        bool sel = (i == selectedCalParam);
        uint16_t bg = sel ? C_BTN_ON : C_CARD_BG;
        uint16_t brd = sel ? C_ACCENT2 : C_ACCENT;

        tft.fillRoundRect(6, py, 228, 44, 5, bg);
        tft.drawRoundRect(6, py, 228, 44, 5, brd);

        // Selection indicator
        tft.setTextSize(2);
        tft.setTextColor(sel ? C_WHITE : C_DIM, bg);
        tft.setCursor(12, py + 13);
        tft.print(sel ? ">" : " ");

        // Parameter name
        tft.setTextSize(1);
        tft.setCursor(28, py + 8);
        tft.print(names[i]);

        // Value
        tft.setTextSize(1);
        tft.setTextColor(C_WHITE, bg);
        char vbuf[20];
        sprintf(vbuf, "%u %s", vals[i], units[i]);
        int vw = textW(vbuf, 1);
        tft.setCursor(228 - vw - 6, py + 8);
        tft.print(vbuf);

        // Sub-label
        tft.setTextColor(C_DIM, bg);
        tft.setCursor(28, py + 28);
        switch (i) {
            case 0: tft.print("Conversion slope"); break;
            case 1: tft.print("Dead time correction"); break;
            case 2: tft.print("Subtracted from readings"); break;
        }
    }

    // Adjustment controls — no label, just - / value / +
    int aY = 195;
    // - button red
    tft.fillRoundRect(35, aY, 50, 34, 5, C_RED);
    tft.drawRoundRect(35, aY, 50, 34, 5, C_WHITE);
    tft.setTextSize(2);
    tft.setTextColor(C_WHITE, C_RED);
    tft.setCursor(35 + (50 - textW("-", 2)) / 2, aY + 10);
    tft.print("-");

    // Current value
    char aval[12];
    switch (selectedCalParam) {
        case 0: sprintf(aval, "%u", tubeSensitivity); break;
        case 1: sprintf(aval, "%u us", deadTime); break;
        case 2: sprintf(aval, "%u", backgroundCPM); break;
    }
    tft.setTextSize(2);
    tft.setTextColor(C_WHITE, C_BG);
    tft.setCursor(95 + (50 - textW(aval, 2)) / 2, aY + 10);
    tft.print(aval);

    // + button green
    tft.fillRoundRect(155, aY, 50, 34, 5, C_GREEN);
    tft.drawRoundRect(155, aY, 50, 34, 5, C_WHITE);
    tft.setCursor(155 + (50 - textW("+", 2)) / 2, aY + 10);
    tft.print("+");

    // Cal guide button
    tft.fillRoundRect(35, 248, 170, 26, 5, C_BTN_BG);
    tft.drawRoundRect(35, 248, 170, 26, 5, C_ACCENT);
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, C_BTN_BG);
    const char* gl = "CALIBRATION GUIDE";
    tft.setCursor(35 + (170 - textW(gl, 1)) / 2, 267);
    tft.print(gl);
}

void handlePage4() {
    if (!readTouch()) return;
    if (touchX >= 3 && touchX <= 61 && touchY >= 284 && touchY <= 316) {
        saveSettings(); page = 1; drawSettingsMenu(); return;
    }
    for (uint8_t i = 0; i < 3; i++) {
        int py = 35 + i * 50;
        if (touchX >= 6 && touchX <= 234 && touchY >= py && touchY <= py + 44) {
            if (selectedCalParam != i) { selectedCalParam = i; drawCalibrationPage(); }
            return;
        }
    }
    if (touchX >= 35 && touchX <= 85 && touchY >= 195 && touchY <= 229) {
        switch (selectedCalParam) {
            case 0: if (tubeSensitivity > 1) tubeSensitivity--; break;
            case 1: if (deadTime > 10) deadTime -= 10; break;
            case 2: if (backgroundCPM > 0) backgroundCPM--; break;
        }
        drawCalibrationPage(); return;
    }
    if (touchX >= 155 && touchX <= 205 && touchY >= 195 && touchY <= 229) {
        switch (selectedCalParam) {
            case 0: if (tubeSensitivity < 999) tubeSensitivity++; break;
            case 1: if (deadTime < 2000) deadTime += 10; break;
            case 2: if (backgroundCPM < 500) backgroundCPM++; break;
        }
        drawCalibrationPage(); return;
    }
    if (touchX >= 35 && touchX <= 205 && touchY >= 248 && touchY <= 274) {
        calGuidePg = 0; page = 5; drawCalibrationGuide(); return;
    }
}

// ============================================================================
// PAGE 5 — CALIBRATION GUIDE
// ============================================================================
void drawCalibrationGuide() {
    drawFrame("CALIBRATION GUIDE");
    drawBackButton();

    tft.setTextSize(1);
    const char* linesP0[] = {
        "HOW TO CALIBRATE YOUR GC-20","",
        "1. SET BACKGROUND CPM:","Place outdoors, away from buildings.",
        "Run SLOW 5m mode 5-10 min. Note","stable CPM and enter as Background.","",
        "2. SET DEAD TIME:","SBM-20 default: 190us. Only change",
        "for different tubes.","SBM-19=200us, SI-29BG=150us."
    };
    const char* linesP1[] = {
        "3. SET TUBE SENSITIVITY:","Most important! Use a reference",
        "source or calibrated instrument.","",
        "Method A - Check source:","Place near known source (e.g.Cs-137).",
        "Adjust Sensitivity until dose","matches expected value.","",
        "Method B - Reference device:","Compare with calibrated Geiger",
        "counter at 2-3 dose rates.","",
        "Method C - Default:","SBM-20: 175 CPM/uSv/h for Cs-137.",
        "Gives +/-20% without calibration.","",
        "IMPORTANT: Higher Sensitivity =",
        "LOWER displayed dose. If reading","too high, INCREASE sensitivity."
    };

    const char** lines = (calGuidePg == 0) ? linesP0 : linesP1;
    uint8_t n = (calGuidePg == 0) ? sizeof(linesP0)/sizeof(linesP0[0])
                                  : sizeof(linesP1)/sizeof(linesP1[0]);
    int yPos = 34;
    for (uint8_t i = 0; i < n && yPos < 268; i++) {
        if (strlen(lines[i]) == 0) { yPos += 4; continue; }
        tft.setCursor(6, yPos);
        if (lines[i][0] >= '1' && lines[i][0] <= '3' && lines[i][1] == '.')
            tft.setTextColor(C_ACCENT2, C_BG);
        else if (strncmp(lines[i], "Method", 6) == 0)
            tft.setTextColor(C_YELLOW, C_BG);
        else if (strncmp(lines[i], "IMPORTANT", 9) == 0)
            tft.setTextColor(C_RED, C_BG);
        else
            tft.setTextColor(C_WHITE, C_BG);
        tft.print(lines[i]);
        yPos += 12;
    }

    // Page indicator
    char pi[8]; sprintf(pi, "%u/2", calGuidePg + 1);
    tft.setTextColor(C_DIM, C_BG);
    tft.setCursor((240 - textW(pi, 1)) / 2, 278);
    tft.print(pi);

    // Prev / Next buttons
    if (calGuidePg > 0) {
        tft.fillRoundRect(50, 284, 60, 28, 4, C_BTN_BG);
        tft.drawRoundRect(50, 284, 60, 28, 4, C_ACCENT);
        tft.setCursor(50 + (60 - textW("PREV", 1)) / 2, 296);
        tft.print("PREV");
    }
    if (calGuidePg < 1) {
        tft.fillRoundRect(130, 284, 60, 28, 4, C_BTN_BG);
        tft.drawRoundRect(130, 284, 60, 28, 4, C_ACCENT);
        tft.setCursor(130 + (60 - textW("NEXT", 1)) / 2, 296);
        tft.print("NEXT");
    }
}

void handlePage5() {
    if (!readTouch()) return;
    // Back button
    if (touchX >= 3 && touchX <= 61 && touchY >= 284 && touchY <= 312) {
        calGuidePg = 0;
        page = 4; drawCalibrationPage(); return;
    }
    // Prev
    if (calGuidePg > 0 && touchX >= 50 && touchX <= 110 && touchY >= 284 && touchY <= 312) {
        calGuidePg--; drawCalibrationGuide(); return;
    }
    // Next
    if (calGuidePg < 1 && touchX >= 130 && touchX <= 190 && touchY >= 284 && touchY <= 312) {
        calGuidePg++; drawCalibrationGuide(); return;
    }
}

// ============================================================================
// PAGE 6 — TIMED COUNT SETUP
// ============================================================================
void drawTimedCountSetup() {
    drawFrame("TIMED COUNT");
    tft.setTextSize(1);
    tft.setTextColor(C_DIM, C_BG);
    tft.setCursor(10, 40);
    tft.print("Select duration:");

    const uint8_t ints[] = {1, 2, 5, 10, 30, 60};
    for (uint8_t i = 0; i < 6; i++) {
        int bx = (i % 3) * 78 + 6, by = 58 + (i / 3) * 50;
        bool act = (timedInterval == ints[i]);
        uint16_t bg = act ? C_BTN_ON : C_BTN_BG;
        uint16_t brd = act ? C_ACCENT2 : C_ACCENT;
        tft.fillRoundRect(bx, by, 72, 42, 5, bg);
        tft.drawRoundRect(bx, by, 72, 42, 5, brd);
        tft.setTextSize(2);
        tft.setTextColor(C_WHITE, bg);
        char lb[6]; sprintf(lb, "%u", ints[i]);
        tft.setCursor(bx + 8, by + 12);
        tft.print(lb);
        tft.setTextSize(1);
        tft.setCursor(bx + 8, by + 30);
        tft.print(i < 5 ? "min" : "hour");
    }

    tft.fillRoundRect(40, 175, 160, 40, 6, C_BTN_ON);
    tft.drawRoundRect(40, 175, 160, 40, 6, C_GREEN);
    tft.setTextSize(2);
    tft.setTextColor(C_WHITE, C_BTN_ON);
    tft.setCursor(40 + (160 - textW("START", 2)) / 2, 186);
    tft.print("START");

    tft.fillRoundRect(40, 228, 160, 32, 5, C_BTN_BG);
    tft.drawRoundRect(40, 228, 160, 32, 5, C_ACCENT);
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, C_BTN_BG);
    tft.setCursor(40 + (160 - textW("CANCEL", 1)) / 2, 240);
    tft.print("CANCEL");
}

void handlePage6() {
    if (!readTouch()) return;
    const uint8_t ints[] = {1, 2, 5, 10, 30, 60};
    for (uint8_t i = 0; i < 6; i++) {
        int bx = (i % 3) * 78 + 6, by = 58 + (i / 3) * 50;
        if (touchX >= bx && touchX <= bx + 72 && touchY >= by && touchY <= by + 42) {
            timedInterval = ints[i]; drawTimedCountSetup(); return;
        }
    }
    if (touchX >= 40 && touchX <= 200 && touchY >= 175 && touchY <= 215) {
        startTimedCount(); page = 7; drawTimedCountRunning(); return;
    }
    if (touchX >= 40 && touchX <= 200 && touchY >= 228 && touchY <= 260) {
        page = 0; dashboardBuilt = false; prevDoseLevel = 255;
        strcpy(lastDoseStr, ""); drawDashboard(); dashboardBuilt = true; return;
    }
}

// ============================================================================
// PAGE 7 — TIMED COUNT RUNNING
// ============================================================================
void drawTimedCountRunning() {
    drawFrame("TIMED COUNT");

    // Static elements — progress bar frame + Live CPM card + button
    tft.drawRect(15, 50, 210, 18, C_ACCENT);  // progress bar outline (static)

    tft.fillRoundRect(25, 115, 190, 50, 6, C_CARD_BG);
    tft.drawRoundRect(25, 115, 190, 50, 6, C_ACCENT);
    tft.setTextSize(1);
    tft.setTextColor(C_DIM, C_CARD_BG);
    tft.setCursor(25 + (190 - textW("Live CPM", 1)) / 2, 130);
    tft.print("Live CPM");

    if (!timedComplete) {
        tft.fillRoundRect(40, 215, 160, 42, 6, C_BTN_OFF);
        tft.drawRoundRect(40, 215, 160, 42, 6, C_RED);
        tft.setTextSize(2);
        tft.setTextColor(C_WHITE, C_BTN_OFF);
        tft.setCursor(40 + (160 - textW("STOP", 2)) / 2, 226);
        tft.print("STOP");
    } else {
        drawTimedComplete();
    }
}

// Called only when timed count finishes
void drawTimedComplete() {
    unsigned long el = (currentMillis - timedStartMillis) / 1000;
    unsigned long ctsRun = totalPulseCount - timedCountsAtStart;
    float tCPM = (el > 0) ? (float)ctsRun * 60.0f / (float)el : 0.0f;

    tft.setTextSize(2);
    tft.setTextColor(C_GREEN, C_BG);
    const char* fl = "Final CPM:";
    char fbuf[20]; sprintf(fbuf, "%s %u", fl, (int)tCPM);
    tft.setCursor((240 - textW(fbuf, 2)) / 2, 220);
    tft.print(fbuf);

    tft.fillRoundRect(40, 252, 160, 36, 5, C_BTN_ON);
    tft.drawRoundRect(40, 252, 160, 36, 5, C_GREEN);
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, C_BTN_ON);
    tft.setCursor(40 + (160 - textW("CLOSE", 1)) / 2, 266);
    tft.print("CLOSE");
    drawBackButton();
}

// Partial update — only dynamic data (called every second while running)
void updateTimedCountDisplay() {
    unsigned long el = (currentMillis - timedStartMillis) / 1000;
    unsigned long tot = (unsigned long)timedInterval * 60;
    unsigned long rem = (el < tot) ? (tot - el) : 0;

    // Progress bar fill (clear old, draw new)
    tft.fillRect(16, 51, 208, 16, C_BG);
    int fw = (int)((float)el / (float)tot * 210.0f);
    if (fw > 210) fw = 210;
    if (fw > 0) tft.fillRect(16, 51, fw - 1, 16, timedComplete ? C_GREEN : C_ACCENT2);

    // Time remaining (centered)
    tft.fillRect(15, 78, 210, 20, C_BG);
    tft.setTextSize(2);
    if (timedComplete) {
        tft.setTextColor(C_GREEN, C_BG);
        tft.setCursor((240 - textW("COMPLETE!", 2)) / 2, 95);
        tft.print("COMPLETE!");
    } else {
        tft.setTextColor(C_WHITE, C_BG);
        char tb[20]; sprintf(tb, "Remain: %lu:%02lu", rem / 60, rem % 60);
        tft.setCursor((240 - textW(tb, 2)) / 2, 95);
        tft.print(tb);
    }

    // Live CPM number (centered in card)
    unsigned long ctsRun = totalPulseCount - timedCountsAtStart;
    float tCPM = (el > 0) ? (float)ctsRun * 60.0f / (float)el : 0.0f;
    tft.fillRect(26, 138, 188, 26, C_CARD_BG);
    tft.setTextSize(3);
    tft.setTextColor(C_WHITE, C_CARD_BG);
    char cb[10]; sprintf(cb, "%lu", (unsigned long)tCPM);
    tft.setCursor(25 + (190 - textW(cb, 3)) / 2, 140);
    tft.print(cb);

    // Counts (centered)
    tft.fillRect(15, 175, 210, 14, C_BG);
    tft.setTextSize(1);
    tft.setTextColor(C_DIM, C_BG);
    char cntBuf[24]; sprintf(cntBuf, "Counts: %lu", ctsRun);
    tft.setCursor((240 - textW(cntBuf, 1)) / 2, 186);
    tft.print(cntBuf);
}

void handlePage7() {
    if (timedRunning && !timedComplete) {
        unsigned long el = (currentMillis - timedStartMillis) / 1000;
        unsigned long tot = (unsigned long)timedInterval * 60;
        if (el != timedElapsed) {
            timedElapsed = el;
            if (el >= tot) {
                timedComplete = true; timedRunning = false;
                drawTimedComplete();
            } else {
                updateTimedCountDisplay();
            }
        }
    }
    if (!readTouch()) return;
    if (!timedComplete) {
        if (touchX >= 40 && touchX <= 200 && touchY >= 215 && touchY <= 257) {
            stopTimedCount(); drawTimedComplete(); return;
        }
    } else {
        if ((touchX >= 3 && touchX <= 61 && touchY >= 284 && touchY <= 316) ||
            (touchX >= 40 && touchX <= 200 && touchY >= 252 && touchY <= 288)) {
            page = 0; dashboardBuilt = false; prevDoseLevel = 255;
            strcpy(lastDoseStr, ""); drawDashboard(); dashboardBuilt = true; return;
        }
    }
}

void startTimedCount() {
    timedRunning = true; timedComplete = false;
    timedCountsAtStart = totalPulseCount;
    timedStartMillis = currentMillis; timedElapsed = 0;
}

void stopTimedCount() {
    timedRunning = false; timedComplete = true;
    timedElapsed = (currentMillis - timedStartMillis) / 1000;
}

// ============================================================================
// PAGE 8 — ABOUT
// ============================================================================
void drawAboutPage() {
    drawFrame("ABOUT GC-20");
    drawBackButton();

    tft.setTextSize(1);
    int y = 36;

    tft.setTextColor(C_ACCENT2, C_BG); tft.setCursor(8, y); y += 16;
    tft.print("GC-20 Geiger Counter v3.2");
    tft.setTextColor(C_DIM, C_BG); tft.setCursor(8, y); y += 18;
    tft.print("Standalone Radiation Monitor");
    tft.setTextColor(C_WHITE, C_BG); tft.setCursor(8, y); y += 14;
    tft.print("Tube: SBM-20 Geiger-Muller");
    tft.setCursor(8, y); y += 14;
    tft.print("MCU:  ESP8266 @ 160 MHz");
    tft.setCursor(8, y); y += 14;
    tft.print("Display: ILI9341 2.8\" 240x320");
    tft.setCursor(8, y); y += 14;
    tft.print("Touch:  XPT2046 (TPM408-2.8)");
    y += 6;

    tft.setTextColor(C_ACCENT2, C_BG); tft.setCursor(8, y); y += 14;
    tft.print("Calibration:");
    tft.setTextColor(C_WHITE, C_BG); tft.setCursor(8, y); y += 14;
    tft.print("Sensitivity: "); tft.print(tubeSensitivity); tft.print(" CPM/uSv/h");
    tft.setCursor(8, y); y += 14;
    tft.print("Dead Time: "); tft.print(deadTime); tft.print(" us");
    tft.setCursor(8, y); y += 14;
    tft.print("Background: "); tft.print(backgroundCPM); tft.print(" CPM");
    y += 6;

    tft.setTextColor(C_ACCENT2, C_BG); tft.setCursor(8, y); y += 14;
    tft.print("Session:");
    tft.setTextColor(C_WHITE, C_BG); tft.setCursor(8, y); y += 14;
    tft.print("Pulses: "); tft.print(totalPulseCount);
    tft.setCursor(8, y); y += 14;
    char db[12]; formatDose(db, totalDose);
    tft.print("Dose: "); tft.print(db); tft.print(doseUnits == 0 ? " uSv" : " mR");
    unsigned long us = currentMillis / 1000;
    tft.setCursor(8, y); y += 14;
    tft.print("Uptime: "); tft.print(us / 86400); tft.print("d ");
    tft.print((us % 86400) / 3600); tft.print("h ");
    tft.print((us % 3600) / 60); tft.print("m");
    tft.setTextColor(C_DIM, C_BG); tft.setCursor(8, y + 10);
    tft.print("License: CC BY-SA 4.0");
}

void handlePage8() {
    if (!readTouch()) return;
    if (touchX >= 3 && touchX <= 61 && touchY >= 284 && touchY <= 316) {
        page = 1; drawSettingsMenu(); return;
    }
}
