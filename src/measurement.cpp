#include "measurement.h"
#include "config.h"
#include "settings.h"
#include "output.h"

const uint16_t WIN_LEN[3] = {10, 60, 300};

// ============================================================================
// ISR STATE
// ============================================================================
static volatile uint32_t pulseCount    = 0;
volatile uint32_t        totalPulseCount = 0;
static volatile uint32_t lastIntMicros = 0;

// ============================================================================
// DERIVED STATE
// ============================================================================
float     avgCPM         = 0.0f;
float     uncertaintyCPM = 0.0f;
float     doseRate       = 0.0f;
double    totalDose      = 0.0;
bool      overload       = false;
DoseLevel doseLevel      = LVL_NORMAL;
bool      alarmActive    = false;
bool      alarmMuted     = false;
uint32_t  elapsedSec     = 0;
uint8_t   batteryPercent = 0;
bool      batteryUpdated = false;

// Raw counts per 1-second slot; every window is a running sum over it
static uint16_t secCounts[RING_LEN];
static uint16_t ringHead        = 0;     // next write position
static uint16_t ringFilled      = 0;     // valid slots, <= RING_LEN
static uint32_t winSum[3]       = {0, 0, 0};
static uint32_t lastSecCounts   = 0;
static double   corrCountsTotal = 0.0;   // dead-time-corrected counts since boot
static unsigned long prevSecMillis = 0;
static uint8_t  batUpdateCtr    = 0;
static float    batterySmoothed = -1.0f;

// ============================================================================
// ISR
// ============================================================================
static void IRAM_ATTR pulseIsr() {
    uint32_t now = micros();
    if (now - lastIntMicros > PULSE_DEBOUNCE_US) {
        pulseCount++;
        totalPulseCount++;
        lastIntMicros = now;
        outputPulseFromIsr();
    }
}

// ============================================================================
// MATH
// ============================================================================
// Non-paralyzable dead-time model: true = measured / (1 - measured * tau).
// Returns the correction factor; capped where the model stops being reliable.
float deadTimeFactor(float cps) {
    float x = cps * (float)settings.deadTime * 1e-6f;
    if (x > OVERLOAD_FRACTION) x = OVERLOAD_FRACTION;
    if (x < 0.0f) x = 0.0f;
    return 1.0f / (1.0f - x);
}

// CPM (dead-time corrected) -> uSv/h
float calcDoseRate(float cpm) {
    float eff = cpm - (float)settings.backgroundCPM;
    if (eff < 0.0f) eff = 0.0f;
    return eff / (float)settings.tubeSensitivity;
}

// Most recent 1 s slot is back = 0
static uint16_t ringAt(uint16_t back) {
    return secCounts[(ringHead + RING_LEN - 1 - back) % RING_LEN];
}

uint32_t cpmSecondsAgo(uint16_t back) {
    uint16_t c = ringAt(back);
    return (uint32_t)((float)c * deadTimeFactor((float)c) * 60.0f);
}

// ============================================================================
// BATTERY
// ============================================================================
// Li-Ion non-linear: Wemos D1 Mini (220k/100k internal) + 220k external
// raw=607(3.2V)..797(4.2V), cap at 100 for USB charging (5V->raw~950)
static uint8_t batteryPercentFromRaw(int r) {
    if      (r >= 797) return 100;
    else if (r >= 771) return map(r, 771, 797, 80, 100);
    else if (r >= 740) return map(r, 740, 771, 60, 80);
    else if (r >= 714) return map(r, 714, 740, 40, 60);
    else if (r >= 693) return map(r, 693, 714, 20, 40);
    else if (r >= 670) return map(r, 670, 693, 10, 20);
    else if (r >= 607) return map(r, 607, 670, 0, 10);
    return 0;
}

static void updateBattery() {
    int raw = analogRead(BATTERY_PIN);
    // Exponential moving average
    if (batterySmoothed < 0) batterySmoothed = (float)raw;
    else batterySmoothed = batterySmoothed * 0.7f + (float)raw * 0.3f;
    batteryPercent = batteryPercentFromRaw((int)batterySmoothed);
}

// ============================================================================
// PUBLIC
// ============================================================================
void measurementBegin() {
    memset(secCounts, 0, sizeof(secCounts));
    updateBattery();   // avoid showing 0% on boot
    pinMode(INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(INT_PIN), pulseIsr, FALLING);
    prevSecMillis = millis();
}

// Called every loop pass. Closes a 1 s slot when due; returns true if it did.
bool measurementTick(unsigned long now) {
    batteryUpdated = false;
    if (now - prevSecMillis < 1000) return false;
    // Fixed cadence: a late slot is followed by a shorter one, so the long-run
    // rate stays exact. Only resync after a very long stall.
    prevSecMillis += 1000;
    if (now - prevSecMillis > 10000) prevSecMillis = now;

    noInterrupts();
    uint32_t cts = pulseCount;
    pulseCount = 0;
    interrupts();
    if (cts > 65535) cts = 65535;

    // Drop the samples leaving each window, then add the new one
    for (uint8_t w = 0; w < 3; w++)
        if (ringFilled >= WIN_LEN[w]) winSum[w] -= ringAt(WIN_LEN[w] - 1);
    secCounts[ringHead] = (uint16_t)cts;
    ringHead = (ringHead + 1) % RING_LEN;
    if (ringFilled < RING_LEN) ringFilled++;
    for (uint8_t w = 0; w < 3; w++) winSum[w] += cts;

    lastSecCounts = cts;
    corrCountsTotal += (double)cts * deadTimeFactor((float)cts);
    elapsedSec++;

    computeRates();

    if (++batUpdateCtr >= 5) { batUpdateCtr = 0; updateBattery(); batteryUpdated = true; }

    // CSV for serial monitoring: sec,counts,cpm,+/-cpm,uSv/h,window,overload,alarm
    Serial.printf("%lu,%lu,%.1f,%.1f,%.3f,%u,%u,%u\n",
                  (unsigned long)elapsedSec, (unsigned long)cts, avgCPM,
                  uncertaintyCPM, doseRate, WIN_LEN[settings.integrationMode],
                  overload, alarmActive);
    return true;
}

// Derives CPM, uncertainty, dose rate, levels and alarm state from the ring.
void computeRates() {
    uint8_t mode = settings.integrationMode;
    uint16_t n = WIN_LEN[mode];
    if (ringFilled < n) n = ringFilled;
    if (n == 0) return;

    uint32_t counts = winSum[mode];
    float rawCps = (float)counts / (float)n;
    float k = deadTimeFactor(rawCps);
    avgCPM = rawCps * k * 60.0f;
    uncertaintyCPM = sqrtf((float)counts) / (float)n * 60.0f * k;

    float tau = (float)settings.deadTime * 1e-6f;
    overload = ((float)lastSecCounts * tau >= OVERLOAD_FRACTION) ||
               (rawCps * tau >= OVERLOAD_FRACTION);

    doseRate = calcDoseRate(avgCPM);

    double net = corrCountsTotal -
                 (double)settings.backgroundCPM * (double)elapsedSec / 60.0;
    totalDose = (net > 0.0) ? net / (60.0 * (double)settings.tubeSensitivity) : 0.0;

    // Need some data before a dose-rate alarm: right after boot a 1-3 s
    // window is too noisy (2 counts in 1 s already reads 0.69 uSv/h)
    float thr = (float)settings.alarmThreshold10 / 10.0f;
    bool rateAlarm = (n >= ALARM_MIN_SECONDS) && (doseRate >= thr);
    if (!alarmActive && (rateAlarm || overload)) {
        alarmActive = true; alarmMuted = false;
    } else if (alarmActive && !overload && doseRate < thr * ALARM_HYSTERESIS) {
        alarmActive = false; alarmMuted = false;
    }

    if (overload)                       doseLevel = LVL_OVERLOAD;
    else if (alarmActive)               doseLevel = LVL_ALARM;
    else if (doseRate >= ELEVATED_USVH) doseLevel = LVL_ELEVATED;
    else                                doseLevel = LVL_NORMAL;
}

bool alarmSounding() { return alarmActive && !alarmMuted; }

void muteAlarm() { if (alarmActive) alarmMuted = true; }
