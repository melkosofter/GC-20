/*  GC-20 Geiger Counter v3.4
    SBM-20 radiation monitor — 2.8" TFT touchscreen (TPM408 / ILI9341 + XPT2046)
    Standalone instrument. No WiFi (radio stays asleep — core 3.x default), no cloud.
    Uses built-in font at larger sizes — reliable on all ESP8266 boards.

    Modules:
      measurement — pulse counting, dose math, alarm state, battery
      output      — click / LED flash / alarm tone on hardware timer1
      settings    — CRC-protected EEPROM record, deferred save
      main        — UI (this file)

    Original: Prabhat (pra22@pitt.edu) — CC BY-SA 4.0
*/

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

#include "config.h"
#include "bitmaps.h"
#include "settings.h"
#include "measurement.h"
#include "output.h"

// ============================================================================
// HARDWARE
// ============================================================================
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
XPT2046_Touchscreen ts(TOUCH_CS);

// ============================================================================
// UI STATE
// ============================================================================
enum Page : uint8_t {
    PG_DASHBOARD, PG_MENU, PG_ALARM, PG_CALIB, PG_GUIDE,
    PG_TIMED_SETUP, PG_TIMED_RUN, PG_ABOUT
};

Page     page             = PG_DASHBOARD;
unsigned long currentMillis = 0;
bool     newSample        = false;   // a 1 s measurement slot closed this pass
uint8_t  selectedCalParam = 0;
uint8_t  calGuidePg       = 0;
Page     guideReturnPage  = PG_MENU;
uint8_t  prevStripState   = 255;     // dashboard card/strip, 255 = force redraw
char     lastDoseStr[8]   = "";

// ============================================================================
// TOUCH
// ============================================================================
#define TOUCH_RELEASE_MS    40    // release must last this long to re-arm
#define TOUCH_REPEAT_DELAY  500   // hold time before auto-repeat starts
#define TOUCH_REPEAT_MS     120   // auto-repeat interval
#define TOUCH_FAST_AFTER_MS 3000  // hold time after which +/- steps x10

enum TouchEvent : uint8_t { TOUCH_NONE, TOUCH_PRESS, TOUCH_REPEAT };

int  touchX = 0, touchY = 0;
bool wasTouched   = false;
bool repeatOk     = false;
Page pressPage    = PG_DASHBOARD;
unsigned long pressMs = 0, lastDownMs = 0, lastRepeatMs = 0;

// ============================================================================
// TIMED COUNT
// ============================================================================
static const uint8_t TIMED_OPTIONS[] = {1, 2, 5, 10, 30, 60};   // minutes

bool     timedRunning       = false;
bool     timedComplete      = false;
unsigned long timedStartMillis = 0;
unsigned long timedElapsed  = 0;      // whole seconds shown so far
unsigned long timedFinalMs  = 0;
uint32_t timedCountsAtStart = 0;
uint32_t timedFinalCounts   = 0;

// ============================================================================
// LAYOUT
// ============================================================================
#define GR_X 3          // dashboard trend graph
#define GR_Y 148
#define GR_W 234
#define GR_H 64
#define GR_TITLE_H 10

#define CAL_ADJ_Y 195   // calibration +/- row

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
void showPage(Page p);
void updateDashboardData();
void drawTimedComplete();
void updateTimedCountDisplay();

// ============================================================================
// DRAW HELPERS — default font, size 1 = 6x8, size 2 = 12x16, size 3 = 18x24
// ============================================================================
int textW(const char* s, uint8_t sz) { return strlen(s) * 6 * sz; }

void printAt(const char* s, int x, int y, uint8_t sz, uint16_t fg, uint16_t bg) {
    tft.setTextSize(sz);
    tft.setTextColor(fg, bg);
    tft.setCursor(x, y);
    tft.print(s);
}

void printCentered(const char* s, int x, int w, int y, uint8_t sz,
                   uint16_t fg, uint16_t bg) {
    printAt(s, x + (w - textW(s, sz)) / 2, y, sz, fg, bg);
}

void drawButton(int x, int y, int w, int h, uint8_t r, uint16_t bg, uint16_t border) {
    tft.fillRoundRect(x, y, w, h, r, bg);
    tft.drawRoundRect(x, y, w, h, r, border);
}

void drawFrame(const char* title) {
    tft.fillRect(0, 0, 240, 24, C_HEADER_BG);
    tft.fillRect(0, 24, 240, 296, C_BG);
    tft.drawFastHLine(0, 24, 240, C_ACCENT);
    printCentered(title, 0, 240, 4, 2, C_ACCENT2, C_HEADER_BG);
}

void drawBackButton() {
    drawButton(3, 284, 58, 32, 5, C_BTN_BG, C_ACCENT);
    tft.drawBitmap(3 + (58 - 18) / 2, 284 + (32 - 14) / 2, backArrow, 18, 14, C_WHITE);
}

void drawBattery(int batX, int batY) {
    tft.drawRect(batX, batY, 24, 10, C_WHITE);
    tft.fillRect(batX + 24, batY + 2, 2, 6, C_WHITE);
    uint8_t fw = map(batteryPercent, 0, 100, 0, 20);
    uint16_t c = C_GREEN;
    if (batteryPercent < 30) c = C_RED;
    else if (batteryPercent < 60) c = C_YELLOW;
    tft.fillRect(batX + 1, batY + 1, 22, 8, C_HEADER_BG);
    if (fw > 0) tft.fillRect(batX + 2, batY + 1, fw, 8, c);
    char pct[8]; snprintf(pct, sizeof(pct), "%3u%%", batteryPercent);  // fixed width
    printAt(pct, batX - textW(pct, 1) - 4, batY + 1, 1, C_WHITE, C_HEADER_BG);
}

void formatDose(char* buf, size_t len, float dose) {
    if (dose < 0.01f)        snprintf(buf, len, "0.00");
    else if (dose < 10.0f)   snprintf(buf, len, "%.2f", dose);
    else if (dose < 100.0f)  snprintf(buf, len, "%.1f", dose);
    else if (dose < 99999.f) snprintf(buf, len, "%.0f", dose);
    else                     snprintf(buf, len, "99999");
}

// ============================================================================
// TOUCH INPUT
// ============================================================================
bool hit(int x, int y, int w, int h) {
    return touchX >= x && touchX < x + w && touchY >= y && touchY < y + h;
}

bool hitBack() { return hit(3, 284, 58, 32); }

unsigned long touchHeldMs() { return currentMillis - pressMs; }

// Returns PRESS once per touch, then REPEAT while held on the same page.
// A press while the alarm sounds only mutes it.
TouchEvent readTouch() {
    unsigned long now = currentMillis;
    if (!ts.touched()) {
        // Release must persist briefly so a bouncy contact isn't a second press
        if (wasTouched && now - lastDownMs >= TOUCH_RELEASE_MS) wasTouched = false;
        return TOUCH_NONE;
    }
    lastDownMs = now;

    TouchEvent ev = TOUCH_NONE;
    if (!wasTouched) {
        wasTouched = true; repeatOk = true;
        pressMs = lastRepeatMs = now;
        pressPage = page;
        ev = TOUCH_PRESS;
    } else if (repeatOk && page == pressPage && now - pressMs >= TOUCH_REPEAT_DELAY &&
               now - lastRepeatMs >= TOUCH_REPEAT_MS) {
        lastRepeatMs = now;
        ev = TOUCH_REPEAT;
    }
    if (ev == TOUCH_NONE) return ev;

    TS_Point p = ts.getPoint();
    touchX = constrain(map(p.x, TS_MINX, TS_MAXX, 0, 239), 0, 239);
    touchY = constrain(map(p.y, TS_MINY, TS_MAXY, 0, 319), 0, 319);

    if (ev == TOUCH_PRESS && alarmSounding()) {
        muteAlarm();
        repeatOk = false;
        if (page == PG_DASHBOARD) updateDashboardData();
        return TOUCH_NONE;
    }
    return ev;
}

// ============================================================================
// DASHBOARD
// ============================================================================
void redrawTrendGraph() {
    // Title row
    printAt("CPM TREND", GR_X + 4, GR_Y + 2, 1, C_DIM, C_CARD_BG);

    // Per-second CPM for the last 60 s, oldest first
    uint32_t cpm[60];
    uint32_t maxV = 20;
    for (uint8_t i = 0; i < 60; i++) {
        cpm[i] = cpmSecondsAgo(59 - i);
        if (cpm[i] > maxV) maxV = cpm[i];
    }
    if (maxV <= 100) maxV = ((maxV + 9) / 10) * 10;
    else if (maxV <= 500) maxV = ((maxV + 24) / 25) * 25;
    else maxV = ((maxV + 49) / 50) * 50;
    char ml[12]; snprintf(ml, sizeof(ml), "%7lu", (unsigned long)maxV);  // fixed width
    printAt(ml, GR_X + GR_W - 2 - textW(ml, 1), GR_Y + 2, 1, C_DIM, C_CARD_BG);

    // Bars — each column paints its background part and its bar part,
    // so there is no full clear and no flicker
    const int pX = GR_X + 3, pW = GR_W - 6;
    const int pTop = GR_Y + GR_TITLE_H, pH = GR_H - GR_TITLE_H - 2;
    const int pB = pTop + pH;
    float bw = (float)pW / 60.0f;
    float thr = (float)settings.alarmThreshold10 / 10.0f;

    for (uint8_t i = 0; i < 60; i++) {
        uint32_t v = cpm[i];
        int bh = (int)((float)v / (float)maxV * (float)pH);
        if (bh < 1 && v > 0) bh = 1;
        if (bh > pH) bh = pH;
        int bx = pX + (int)(i * bw);
        int bwi = (int)((i + 1) * bw) - (int)(i * bw);
        if (bwi < 1) bwi = 1;

        float bd = calcDoseRate((float)v);
        uint16_t bc = (bd >= thr) ? C_RED : (bd >= ELEVATED_USVH) ? C_YELLOW : C_GREEN;
        if (bh < pH) tft.fillRect(bx, pTop, bwi, pH - bh, C_CARD_BG);
        if (bh > 0)  tft.fillRect(bx, pB - bh, bwi, bh, bc);
    }
}

void redrawToolbar() {
    static const char* const MODE_LBL[3] = {"10s", "60s", "5m"};

    // Row 1 — y=233 h=46
    drawButton(3, 233, 74, 46, 5, C_BTN_BG, C_ACCENT);
    tft.drawBitmap(3 + (74 - 16) / 2, 233 + (46 - 16) / 2, menuIcon, 16, 16, C_WHITE);

    drawButton(83, 233, 74, 46, 5, C_BTN_BG, C_ACCENT);
    printCentered(MODE_LBL[settings.integrationMode], 83, 74, 248, 2, C_WHITE, C_BTN_BG);

    drawButton(163, 233, 73, 46, 5, C_BTN_BG, C_ACCENT);
    tft.drawBitmap(163 + (73 - 16) / 2, 233 + (46 - 16) / 2, timedIcon, 16, 16, C_WHITE);

    // Row 2 — y=283 h=32
    uint16_t buzBg = settings.buzzerEnabled ? C_BTN_ON : C_BTN_OFF;
    drawButton(3, 283, 112, 32, 4, buzBg, C_DIM);
    tft.drawBitmap(3 + (112 - 24) / 2, 283 + (32 - 16) / 2,
                   settings.buzzerEnabled ? speakerOn : speakerOff, 24, 16, C_WHITE);

    uint16_t ledBg = settings.ledEnabled ? C_BTN_ON : C_BTN_OFF;
    drawButton(125, 283, 112, 32, 4, ledBg, C_DIM);
    if (settings.ledEnabled)
        tft.drawBitmap(125 + (112 - 16) / 2, 283 + (32 - 16) / 2, ledOnIcon, 16, 16, C_WHITE);
    else
        tft.drawBitmap(125 + (112 - 8) / 2, 283 + (32 - 14) / 2, ledOffIcon, 8, 14, C_WHITE);
}

void drawDashboard() {
    tft.fillScreen(C_BG);

    // Status bar
    tft.fillRect(0, 0, 240, 19, C_HEADER_BG);
    tft.drawBitmap(102, 0, gammaIcon, 12, 18, C_WHITE);
    tft.drawBitmap(122, 0, betaIcon, 18, 18, C_YELLOW);
    drawBattery(210, 4);

    // Graph frame + background, drawn once; bars repaint themselves
    tft.drawRect(GR_X, GR_Y, GR_W, GR_H, C_DIM);
    tft.fillRect(GR_X + 1, GR_Y + 1, GR_W - 2, GR_H - 2, C_CARD_BG);

    prevStripState = 255;   // card + strip drawn by updateDashboardData
    updateDashboardData();
    redrawToolbar();
}

// Partial update, every second and on state changes
void updateDashboardData() {
    uint16_t cardBg, cardBorder;
    switch (doseLevel) {
        case LVL_NORMAL:   cardBg = C_CARD_OK;   cardBorder = C_GREEN;  break;
        case LVL_ELEVATED: cardBg = C_CARD_WARN; cardBorder = C_YELLOW; break;
        default:           cardBg = C_CARD_DANGER; cardBorder = C_RED;  break;
    }

    // Card background, unit label and status strip — only when level or mute changes
    uint8_t stripState = doseLevel * 2 + (alarmMuted ? 1 : 0);
    if (stripState != prevStripState) {
        drawButton(3, 23, 234, 82, 4, cardBg, cardBorder);
        printCentered("uSv/h", 3, 234, 74, 2, C_WHITE, cardBg);

        uint16_t sc; const char* st;
        switch (doseLevel) {
            case LVL_NORMAL:   sc = C_GREEN;  st = "NORMAL"; break;
            case LVL_ELEVATED: sc = C_YELLOW; st = "ELEVATED"; break;
            case LVL_ALARM:    sc = C_RED;
                st = alarmMuted ? "ALARM (MUTED)" : "ALARM - TAP SCREEN TO MUTE"; break;
            default:           sc = C_RED;
                st = alarmMuted ? "TUBE OVERLOAD (MUTED)" : "TUBE OVERLOAD - TAP TO MUTE"; break;
        }
        tft.fillRoundRect(3, 107, 234, 16, 4, sc);
        printCentered(st, 3, 234, 111, 1, C_BLACK, sc);

        prevStripState = stripState;
        lastDoseStr[0] = '\0';   // force dose redraw
    }

    // Dose number — clear the whole band when the text changes so a shorter
    // string leaves no stale digits
    char d[8];
    if (overload) strcpy(d, "OVER");
    else formatDose(d, sizeof(d), doseRate);
    if (strcmp(d, lastDoseStr) != 0) {
        tft.fillRect(6, 38, 228, 32, cardBg);
        printCentered(d, 3, 234, 38, 4, C_WHITE, cardBg);
        strcpy(lastDoseStr, d);
    }

    // Stats row — fixed-width fields, text bg overwrites old text
    printAt("CPM:", 5, 132, 1, C_WHITE, C_BG);
    char buf[20];
    snprintf(buf, sizeof(buf), "%6lu", (unsigned long)avgCPM);
    printAt(buf, 30, 132, 1, C_ACCENT2, C_BG);
    snprintf(buf, sizeof(buf), "+/-%-5lu", (unsigned long)(uncertaintyCPM + 0.5f));
    printAt(buf, 70, 132, 1, C_DIM, C_BG);
    snprintf(buf, sizeof(buf), "Total:%7lu", (unsigned long)totalPulseCount);
    printAt(buf, 234 - textW(buf, 1), 132, 1, C_WHITE, C_BG);

    redrawTrendGraph();
    if (batteryUpdated) drawBattery(210, 4);
}

void handleDashboard() {
    if (newSample) updateDashboardData();
    if (readTouch() != TOUCH_PRESS) return;

    if (hit(3, 233, 75, 47))   { showPage(PG_MENU); return; }
    if (hit(83, 233, 75, 47)) {
        settings.integrationMode = (settings.integrationMode + 1) % 3;
        markSettingsDirty();
        computeRates(); updateDashboardData(); redrawToolbar();
        return;
    }
    if (hit(163, 233, 74, 47)) { showPage(PG_TIMED_SETUP); return; }
    if (hit(3, 283, 113, 33)) {
        settings.buzzerEnabled = !settings.buzzerEnabled;
        markSettingsDirty(); redrawToolbar(); return;
    }
    if (hit(125, 283, 113, 33)) {
        settings.ledEnabled = !settings.ledEnabled;
        markSettingsDirty(); redrawToolbar(); return;
    }
}

// ============================================================================
// SETTINGS MENU
// ============================================================================
static const char* const MENU_ITEMS[] = {
    "Alarm Threshold", "Calibration", "Calibration Guide", "About Device"
};
static const Page MENU_PAGES[] = { PG_ALARM, PG_CALIB, PG_GUIDE, PG_ABOUT };
#define MENU_COUNT 4

void drawSettingsMenu() {
    drawFrame("SETTINGS");
    drawBackButton();
    for (uint8_t i = 0; i < MENU_COUNT; i++) {
        int by = 36 + i * 46;
        drawButton(6, by, 228, 40, 5, C_CARD_BG, C_ACCENT);
        printAt(MENU_ITEMS[i], 16, by + 12, 2, C_WHITE, C_CARD_BG);
        printAt(">", 215, by + 12, 2, C_ACCENT2, C_CARD_BG);
    }
}

void handleMenu() {
    if (readTouch() != TOUCH_PRESS) return;
    if (hitBack()) { showPage(PG_DASHBOARD); return; }
    for (uint8_t i = 0; i < MENU_COUNT; i++) {
        if (hit(6, 36 + i * 46, 228, 40)) {
            if (MENU_PAGES[i] == PG_GUIDE) { guideReturnPage = PG_MENU; calGuidePg = 0; }
            showPage(MENU_PAGES[i]);
            return;
        }
    }
}

// ============================================================================
// ALARM THRESHOLD
// ============================================================================
// Threshold is stored in 0.1 uSv/h. Step: 0.1 up to 1.0, 0.5 up to 10, 5 above.
uint16_t thresholdStep(uint16_t v, bool up) {
    if (up) return (v < 10) ? 1 : (v < 100) ? 5 : 50;
    return (v <= 10) ? 1 : (v <= 100) ? 5 : 50;
}

void drawAlertValue() {
    uint16_t t = settings.alarmThreshold10;
    bool high = (t >= 200);
    drawButton(45, 50, 150, 50, 6, C_CARD_BG, high ? C_RED : C_ACCENT);
    char buf[8]; snprintf(buf, sizeof(buf), "%u.%u", t / 10, t % 10);
    printCentered(buf, 45, 150, 58, 3, high ? C_RED : C_WHITE, C_CARD_BG);
    printCentered("uSv/h", 45, 150, 86, 1, C_DIM, C_CARD_BG);
}

void drawAlertPage() {
    drawFrame("ALARM THRESHOLD");
    drawBackButton();
    drawAlertValue();

    drawButton(30, 125, 75, 50, 6, C_RED, C_WHITE);
    printCentered("-", 30, 75, 139, 3, C_WHITE, C_RED);
    drawButton(135, 125, 75, 50, 6, C_GREEN, C_WHITE);
    printCentered("+", 135, 75, 139, 3, C_WHITE, C_GREEN);

    printCentered("Beeps at or above threshold", 0, 240, 200, 1, C_DIM, C_BG);
    printCentered("Range: 0.1 - 100 uSv/h", 0, 240, 214, 1, C_DIM, C_BG);
    printCentered("Typical background: 0.1-0.3", 0, 240, 228, 1, C_DIM, C_BG);
    printCentered("Hold +/- to repeat", 0, 240, 242, 1, C_DIM, C_BG);
}

void handleAlarmPage() {
    TouchEvent ev = readTouch();
    if (ev == TOUCH_NONE) return;
    uint16_t& t = settings.alarmThreshold10;
    if (hit(30, 125, 76, 51)) {
        if (t > 1) {
            uint16_t s = thresholdStep(t, false);
            t = (t > s) ? t - s : 1;
            markSettingsDirty(); computeRates(); drawAlertValue();
        }
        return;
    }
    if (hit(135, 125, 76, 51)) {
        if (t < 1000) {
            t += thresholdStep(t, true);
            if (t > 1000) t = 1000;
            markSettingsDirty(); computeRates(); drawAlertValue();
        }
        return;
    }
    if (ev == TOUCH_PRESS && hitBack()) showPage(PG_MENU);
}

// ============================================================================
// CALIBRATION
// ============================================================================
static const char* const CAL_NAMES[] = {"Tube Sensitivity", "Dead Time", "Tube Background"};
static const char* const CAL_UNITS[] = {"CPM/uSv/h", "us", "CPM"};
static const char* const CAL_HINTS[] = {
    "Conversion slope", "Dead time correction", "Tube's own counts, subtracted"
};

uint16_t calValue(uint8_t i) {
    switch (i) {
        case 0:  return settings.tubeSensitivity;
        case 1:  return settings.deadTime;
        default: return settings.backgroundCPM;
    }
}

void drawCalCard(uint8_t i) {
    int py = 35 + i * 50;
    bool sel = (i == selectedCalParam);
    uint16_t bg = sel ? C_BTN_ON : C_CARD_BG;
    drawButton(6, py, 228, 44, 5, bg, sel ? C_ACCENT2 : C_ACCENT);
    printAt(sel ? ">" : " ", 12, py + 13, 2, sel ? C_WHITE : C_DIM, bg);
    printAt(CAL_NAMES[i], 28, py + 8, 1, sel ? C_WHITE : C_DIM, bg);
    char vbuf[20];
    snprintf(vbuf, sizeof(vbuf), "%u %s", calValue(i), CAL_UNITS[i]);
    printAt(vbuf, 228 - textW(vbuf, 1) - 6, py + 8, 1, C_WHITE, bg);
    printAt(CAL_HINTS[i], 28, py + 28, 1, C_DIM, bg);
}

// Value between the - and + buttons (85..155)
void drawCalAdjValue() {
    tft.fillRect(86, CAL_ADJ_Y, 68, 34, C_BG);
    char aval[8]; snprintf(aval, sizeof(aval), "%u", calValue(selectedCalParam));
    printCentered(aval, 85, 70, CAL_ADJ_Y + 10, 2, C_WHITE, C_BG);
}

void drawCalibrationPage() {
    drawFrame("CALIBRATION");
    drawBackButton();
    for (uint8_t i = 0; i < 3; i++) drawCalCard(i);

    drawButton(35, CAL_ADJ_Y, 50, 34, 5, C_RED, C_WHITE);
    printCentered("-", 35, 50, CAL_ADJ_Y + 10, 2, C_WHITE, C_RED);
    drawButton(155, CAL_ADJ_Y, 50, 34, 5, C_GREEN, C_WHITE);
    printCentered("+", 155, 50, CAL_ADJ_Y + 10, 2, C_WHITE, C_GREEN);
    drawCalAdjValue();

    drawButton(35, 248, 170, 26, 5, C_BTN_BG, C_ACCENT);
    printCentered("CALIBRATION GUIDE", 35, 170, 257, 1, C_WHITE, C_BTN_BG);
}

// dir = +1 / -1; holding the button for a while steps x10
void adjustCalParam(int dir) {
    int mult = (touchHeldMs() >= TOUCH_FAST_AFTER_MS) ? 10 : 1;
    int v;
    switch (selectedCalParam) {
        case 0:
            v = constrain((int)settings.tubeSensitivity + dir * mult, 1, 999);
            settings.tubeSensitivity = v; break;
        case 1:
            v = constrain((int)settings.deadTime + dir * 10 * mult, 10, 2000);
            settings.deadTime = v; break;
        default:
            v = constrain((int)settings.backgroundCPM + dir * mult, 0, 500);
            settings.backgroundCPM = v; break;
    }
    markSettingsDirty();
    computeRates();
    drawCalCard(selectedCalParam);
    drawCalAdjValue();
}

void handleCalibration() {
    TouchEvent ev = readTouch();
    if (ev == TOUCH_NONE) return;
    if (hit(35, CAL_ADJ_Y, 51, 35))  { adjustCalParam(-1); return; }
    if (hit(155, CAL_ADJ_Y, 51, 35)) { adjustCalParam(+1); return; }
    if (ev != TOUCH_PRESS) return;

    if (hitBack()) { showPage(PG_MENU); return; }
    for (uint8_t i = 0; i < 3; i++) {
        if (hit(6, 35 + i * 50, 228, 45)) {
            if (selectedCalParam != i) {
                uint8_t old = selectedCalParam;
                selectedCalParam = i;
                drawCalCard(old); drawCalCard(i); drawCalAdjValue();
            }
            return;
        }
    }
    if (hit(35, 248, 171, 27)) {
        guideReturnPage = PG_CALIB; calGuidePg = 0;
        showPage(PG_GUIDE);
    }
}

// ============================================================================
// CALIBRATION GUIDE
// ============================================================================
// One PROGMEM string per page, '\n' separated; empty line = small gap.
static const char GUIDE_P0[] PROGMEM =
    "HOW TO CALIBRATE YOUR GC-20\n"
    "\n"
    "1. SET TUBE BACKGROUND:\n"
    "The tube's OWN counts only - not\n"
    "natural radiation, which is part\n"
    "of the dose. Measure inside thick\n"
    "lead shielding. Leave 0 if unsure.\n"
    "\n"
    "2. SET DEAD TIME:\n"
    "SBM-20 default: 190us. Only change\n"
    "for different tubes.\n"
    "SBM-19=200us, SI-29BG=150us.";

static const char GUIDE_P1[] PROGMEM =
    "3. SET TUBE SENSITIVITY:\n"
    "Most important! Use a reference\n"
    "source or calibrated instrument.\n"
    "\n"
    "Method A - Check source:\n"
    "Place near known source (e.g.Cs-137).\n"
    "Adjust Sensitivity until dose\n"
    "matches expected value.\n"
    "\n"
    "Method B - Reference device:\n"
    "Compare with calibrated Geiger\n"
    "counter at 2-3 dose rates.\n"
    "\n"
    "Method C - Default:\n"
    "SBM-20: 175 CPM/uSv/h for Cs-137.\n"
    "Gives +/-20% without calibration.\n"
    "\n"
    "IMPORTANT: Higher Sensitivity =\n"
    "LOWER displayed dose. If reading\n"
    "too high, INCREASE sensitivity.";

#define GUIDE_PAGES 2

void drawCalibrationGuide() {
    drawFrame("CALIBRATION GUIDE");
    drawBackButton();

    const char* p = (calGuidePg == 0) ? GUIDE_P0 : GUIDE_P1;
    char line[40];
    int yPos = 34;
    while (yPos < 268) {
        uint8_t n = 0;
        char c;
        while ((c = pgm_read_byte(p)) != '\0' && c != '\n') {
            if (n < sizeof(line) - 1) line[n++] = c;
            p++;
        }
        line[n] = '\0';
        if (n == 0) {
            yPos += 4;
        } else {
            uint16_t color = C_WHITE;
            if (line[0] >= '1' && line[0] <= '3' && line[1] == '.') color = C_ACCENT2;
            else if (strncmp(line, "Method", 6) == 0)               color = C_YELLOW;
            else if (strncmp(line, "IMPORTANT", 9) == 0)            color = C_RED;
            printAt(line, 6, yPos, 1, color, C_BG);
            yPos += 12;
        }
        if (c == '\0') break;
        p++;   // skip '\n'
    }

    char pi[8]; snprintf(pi, sizeof(pi), "%u/%u", calGuidePg + 1, GUIDE_PAGES);
    printCentered(pi, 0, 240, 274, 1, C_DIM, C_BG);

    if (calGuidePg > 0) {
        drawButton(70, 284, 70, 32, 4, C_BTN_BG, C_ACCENT);
        printCentered("PREV", 70, 70, 296, 1, C_WHITE, C_BTN_BG);
    }
    if (calGuidePg < GUIDE_PAGES - 1) {
        drawButton(160, 284, 70, 32, 4, C_BTN_BG, C_ACCENT);
        printCentered("NEXT", 160, 70, 296, 1, C_WHITE, C_BTN_BG);
    }
}

void handleGuide() {
    if (readTouch() != TOUCH_PRESS) return;
    if (hitBack()) { calGuidePg = 0; showPage(guideReturnPage); return; }
    if (calGuidePg > 0 && hit(70, 284, 70, 32)) {
        calGuidePg--; drawCalibrationGuide(); return;
    }
    if (calGuidePg < GUIDE_PAGES - 1 && hit(160, 284, 70, 32)) {
        calGuidePg++; drawCalibrationGuide(); return;
    }
}

// ============================================================================
// TIMED COUNT — SETUP
// ============================================================================
void drawTimedCountSetup() {
    drawFrame("TIMED COUNT");
    printAt("Select duration:", 10, 40, 1, C_DIM, C_BG);

    for (uint8_t i = 0; i < 6; i++) {
        int bx = (i % 3) * 78 + 6, by = 58 + (i / 3) * 50;
        bool act = (settings.timedInterval == TIMED_OPTIONS[i]);
        uint16_t bg = act ? C_BTN_ON : C_BTN_BG;
        drawButton(bx, by, 72, 42, 5, bg, act ? C_ACCENT2 : C_ACCENT);
        bool hour = (TIMED_OPTIONS[i] == 60);
        char lb[6]; snprintf(lb, sizeof(lb), "%u", hour ? 1 : TIMED_OPTIONS[i]);
        printAt(lb, bx + 8, by + 12, 2, C_WHITE, bg);
        printAt(hour ? "hour" : "min", bx + 8, by + 30, 1, C_WHITE, bg);
    }

    drawButton(40, 175, 160, 40, 6, C_BTN_ON, C_GREEN);
    printCentered("START", 40, 160, 186, 2, C_WHITE, C_BTN_ON);
    drawButton(40, 228, 160, 32, 5, C_BTN_BG, C_ACCENT);
    printCentered("CANCEL", 40, 160, 240, 1, C_WHITE, C_BTN_BG);
}

void startTimedCount() {
    timedRunning = true; timedComplete = false;
    timedCountsAtStart = totalPulseCount;
    timedStartMillis = currentMillis; timedElapsed = 0;
}

void stopTimedCount() {
    timedRunning = false; timedComplete = true;
    timedFinalMs = currentMillis - timedStartMillis;
    timedFinalCounts = totalPulseCount - timedCountsAtStart;
}

void handleTimedSetup() {
    if (readTouch() != TOUCH_PRESS) return;
    for (uint8_t i = 0; i < 6; i++) {
        int bx = (i % 3) * 78 + 6, by = 58 + (i / 3) * 50;
        if (hit(bx, by, 73, 43)) {
            if (settings.timedInterval != TIMED_OPTIONS[i]) {
                settings.timedInterval = TIMED_OPTIONS[i];
                markSettingsDirty();
                drawTimedCountSetup();
            }
            return;
        }
    }
    if (hit(40, 175, 161, 41)) { startTimedCount(); showPage(PG_TIMED_RUN); return; }
    if (hit(40, 228, 161, 33)) { showPage(PG_DASHBOARD); return; }
}

// ============================================================================
// TIMED COUNT — RUNNING / RESULT
// ============================================================================
void drawTimedCountRunning() {
    drawFrame("TIMED COUNT");
    tft.drawRect(15, 50, 210, 18, C_ACCENT);   // progress bar outline

    drawButton(25, 115, 190, 50, 6, C_CARD_BG, C_ACCENT);
    printCentered("Live CPM", 25, 190, 120, 1, C_DIM, C_CARD_BG);

    drawButton(40, 215, 160, 42, 6, C_BTN_OFF, C_RED);
    printCentered("STOP", 40, 160, 228, 2, C_WHITE, C_BTN_OFF);
    updateTimedCountDisplay();
}

// Dynamic part, once per second while running and once more at the end
void updateTimedCountDisplay() {
    unsigned long elMs = timedComplete ? timedFinalMs : currentMillis - timedStartMillis;
    uint32_t cts = timedComplete ? timedFinalCounts : totalPulseCount - timedCountsAtStart;
    unsigned long el = elMs / 1000;
    unsigned long tot = (unsigned long)settings.timedInterval * 60;
    unsigned long rem = (el < tot) ? (tot - el) : 0;

    // Progress bar (inner 208x16)
    int fw = timedComplete ? 208 : (int)((float)el / (float)tot * 208.0f);
    if (fw > 208) fw = 208;
    if (fw > 0)   tft.fillRect(16, 51, fw, 16, timedComplete ? C_GREEN : C_ACCENT2);
    if (fw < 208) tft.fillRect(16 + fw, 51, 208 - fw, 16, C_BG);

    // Status line
    tft.fillRect(0, 92, 240, 20, C_BG);
    char tb[24];
    if (timedComplete) {
        snprintf(tb, sizeof(tb), "DONE %lu:%02lu", el / 60, el % 60);
        printCentered(tb, 0, 240, 94, 2, C_GREEN, C_BG);
    } else {
        snprintf(tb, sizeof(tb), "Remain: %lu:%02lu", rem / 60, rem % 60);
        printCentered(tb, 0, 240, 94, 2, C_WHITE, C_BG);
    }

    // Dead-time corrected CPM over the whole run
    float rawCps = (elMs > 0) ? (float)cts * 1000.0f / (float)elMs : 0.0f;
    float tCPM = rawCps * deadTimeFactor(rawCps) * 60.0f;
    tft.fillRect(27, 134, 186, 26, C_CARD_BG);
    char cb[12]; snprintf(cb, sizeof(cb), "%lu", (unsigned long)(tCPM + 0.5f));
    printCentered(cb, 25, 190, 136, 3, C_WHITE, C_CARD_BG);

    tft.fillRect(0, 180, 240, 10, C_BG);
    char cntBuf[24]; snprintf(cntBuf, sizeof(cntBuf), "Counts: %lu", (unsigned long)cts);
    printCentered(cntBuf, 0, 240, 181, 1, C_DIM, C_BG);
}

void drawTimedComplete() {
    updateTimedCountDisplay();
    printCentered("Result CPM", 25, 190, 120, 1, C_DIM, C_CARD_BG);

    // Final result with 1-sigma statistical uncertainty
    float sec = (float)timedFinalMs / 1000.0f;
    float rawCps = (sec > 0.0f) ? (float)timedFinalCounts / sec : 0.0f;
    float k = deadTimeFactor(rawCps);
    float cpm = rawCps * k * 60.0f;
    float sigma = (sec > 0.0f) ? sqrtf((float)timedFinalCounts) / sec * 60.0f * k : 0.0f;
    float pct = (cpm > 0.0f) ? sigma / cpm * 100.0f : 0.0f;

    tft.fillRect(0, 196, 240, 124, C_BG);   // clears the STOP button
    char buf[32], d[8];
    formatDose(d, sizeof(d), calcDoseRate(cpm));
    snprintf(buf, sizeof(buf), "%s uSv/h", d);
    printCentered(buf, 0, 240, 200, 2, C_GREEN, C_BG);
    snprintf(buf, sizeof(buf), "%.1f +/- %.1f CPM (%.1f%%)", cpm, sigma, pct);
    printCentered(buf, 0, 240, 224, 1, C_WHITE, C_BG);

    drawButton(80, 284, 156, 32, 5, C_BTN_ON, C_GREEN);
    printCentered("CLOSE", 80, 156, 293, 2, C_WHITE, C_BTN_ON);
    drawBackButton();
}

void handleTimedRun() {
    if (timedRunning) {
        unsigned long el = (currentMillis - timedStartMillis) / 1000;
        unsigned long tot = (unsigned long)settings.timedInterval * 60;
        if (el >= tot) {
            stopTimedCount();   // keeps the actual elapsed ms and its counts
            drawTimedComplete();
        } else if (el != timedElapsed) {
            timedElapsed = el;
            updateTimedCountDisplay();
        }
    }
    if (readTouch() != TOUCH_PRESS) return;
    if (!timedComplete) {
        if (hit(40, 215, 161, 43)) { stopTimedCount(); drawTimedComplete(); }
    } else if (hitBack() || hit(80, 284, 156, 32)) {
        showPage(PG_DASHBOARD);
    }
}

// ============================================================================
// ABOUT
// ============================================================================
void drawAboutPage() {
    drawFrame("ABOUT GC-20");
    drawBackButton();

    int y = 36;
    auto line = [&](uint16_t color, int advance) {
        tft.setTextColor(color, C_BG);
        tft.setCursor(8, y);
        y += advance;
    };
    tft.setTextSize(1);

    line(C_ACCENT2, 16); tft.print(F("GC-20 Geiger Counter v" FW_VERSION));
    line(C_DIM, 18);     tft.print(F("Standalone Radiation Monitor"));
    line(C_WHITE, 14);   tft.print(F("Tube: SBM-20 Geiger-Muller"));
    line(C_WHITE, 14);   tft.print(F("MCU:  ESP8266 @ 160 MHz"));
    line(C_WHITE, 14);   tft.print(F("Display: ILI9341 2.8\" 240x320"));
    line(C_WHITE, 14);   tft.print(F("Touch:  XPT2046 (TPM408-2.8)"));
    y += 6;

    line(C_ACCENT2, 14); tft.print(F("Calibration:"));
    line(C_WHITE, 14);
    tft.print(F("Sensitivity: ")); tft.print(settings.tubeSensitivity); tft.print(F(" CPM/uSv/h"));
    line(C_WHITE, 14);
    tft.print(F("Dead Time: ")); tft.print(settings.deadTime); tft.print(F(" us"));
    line(C_WHITE, 14);
    tft.print(F("Tube background: ")); tft.print(settings.backgroundCPM); tft.print(F(" CPM"));
    y += 6;

    line(C_ACCENT2, 14); tft.print(F("Session:"));
    line(C_WHITE, 14);   tft.print(F("Pulses: ")); tft.print(totalPulseCount);
    char db[8]; formatDose(db, sizeof(db), (float)totalDose);
    line(C_WHITE, 14);   tft.print(F("Dose: ")); tft.print(db); tft.print(F(" uSv"));
    unsigned long us = elapsedSec;
    line(C_WHITE, 14);
    tft.print(F("Uptime: ")); tft.print(us / 86400); tft.print(F("d "));
    tft.print((us % 86400) / 3600); tft.print(F("h "));
    tft.print((us % 3600) / 60); tft.print(F("m"));
    y += 10;
    line(C_DIM, 14);     tft.print(F("License: CC BY-SA 4.0"));
}

void handleAbout() {
    if (readTouch() != TOUCH_PRESS) return;
    if (hitBack()) showPage(PG_MENU);
}

// ============================================================================
// NAVIGATION
// ============================================================================
void showPage(Page p) {
    page = p;
    switch (p) {
        case PG_DASHBOARD:   drawDashboard(); break;
        case PG_MENU:        drawSettingsMenu(); break;
        case PG_ALARM:       drawAlertPage(); break;
        case PG_CALIB:       drawCalibrationPage(); break;
        case PG_GUIDE:       drawCalibrationGuide(); break;
        case PG_TIMED_SETUP: drawTimedCountSetup(); break;
        case PG_TIMED_RUN:   drawTimedCountRunning(); break;
        case PG_ABOUT:       drawAboutPage(); break;
    }
}

// ============================================================================
// SERIAL SERVICE COMMANDS (not saved to EEPROM — for bench testing)
//   T<n>  alarm threshold, n in 0.1 uSv/h     M<0..2>  window 10s/60s/5m
//   S     print settings
// ============================================================================
void handleSerialCommands() {
    static char buf[16];
    static uint8_t len = 0;
    while (Serial.available()) {
        char c = Serial.read();
        if (c != '\n' && c != '\r') {
            if (len < sizeof(buf) - 1) buf[len++] = c;
            continue;
        }
        if (len == 0) continue;
        buf[len] = '\0';
        len = 0;
        long v = atol(buf + 1);
        switch (toupper(buf[0])) {
            case 'T': if (v >= 1 && v <= 1000) settings.alarmThreshold10 = v; break;
            case 'M': if (v >= 0 && v <= 2) settings.integrationMode = v; break;
            case 'S': break;
            default:  Serial.println(F("# commands: T<0.1uSv/h> M<0..2> S")); continue;
        }
        computeRates();
        if (page == PG_DASHBOARD) { updateDashboardData(); redrawToolbar(); }
        Serial.printf("# thr=%u.%u uSv/h sens=%u dead=%uus bg=%u mode=%u buz=%u led=%u\n",
                      settings.alarmThreshold10 / 10, settings.alarmThreshold10 % 10,
                      settings.tubeSensitivity, settings.deadTime, settings.backgroundCPM,
                      WIN_LEN[settings.integrationMode], settings.buzzerEnabled,
                      settings.ledEnabled);
    }
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(38400);

    tft.begin();
    tft.setRotation(2);
    tft.fillScreen(C_BLACK);

    // Splash — logo 65x61 centered + title
    tft.drawBitmap((240 - 65) / 2, 111, splashLogo, 65, 61, C_GREEN);
    printCentered("GEIGER COUNTER", 0, 240, 186, 2, C_WHITE, C_BG);
    printCentered("SBM-20  /  v" FW_VERSION, 0, 240, 206, 1, C_DIM, C_BG);

    ts.begin();
    ts.setRotation(2);

    loadSettings();
    outputBegin();
    delay(600);
    measurementBegin();   // starts counting; first slot begins now

    showPage(PG_DASHBOARD);

    Serial.println(F("GC-20 v" FW_VERSION " ready"));
    Serial.println(F("sec,counts,cpm,unc_cpm,usvh,window_s,overload,alarm"));
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
    currentMillis = millis();
    newSample = measurementTick(currentMillis);
    outputAlarm(alarmSounding(), overload, currentMillis);
    settingsTick(currentMillis);
    handleSerialCommands();

    switch (page) {
        case PG_DASHBOARD:   handleDashboard(); break;
        case PG_MENU:        handleMenu(); break;
        case PG_ALARM:       handleAlarmPage(); break;
        case PG_CALIB:       handleCalibration(); break;
        case PG_GUIDE:       handleGuide(); break;
        case PG_TIMED_SETUP: handleTimedSetup(); break;
        case PG_TIMED_RUN:   handleTimedRun(); break;
        case PG_ABOUT:       handleAbout(); break;
    }
    delay(5);
}
