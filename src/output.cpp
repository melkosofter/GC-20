#include "output.h"
#include "config.h"
#include "settings.h"

// timer1 at TIM_DIV16 runs at 5 MHz (APB clock), independent of CPU speed
#define TICKS_PER_US     5
#define CLICK_TICKS      (CLICK_US * TICKS_PER_US)
#define TONE_HALF_TICKS  (1000000UL * TICKS_PER_US / (2 * ALARM_TONE_HZ))
#define LED_TONE_PERIODS ((CLICK_TICKS + TONE_HALF_TICKS - 1) / TONE_HALF_TICKS)

// Direct register access: digitalWrite() is too slow for ISRs
#define BUZZER_HIGH()  (GP16O |= 1)
#define BUZZER_LOW()   (GP16O &= ~1)
#define BUZZER_TOGGLE() (GP16O ^= 1)
#define LED_HIGH()     (GPOS = (1 << LED_PIN))
#define LED_LOW()      (GPOC = (1 << LED_PIN))

// Two timer modes:
//  click mode — one-shot, ends the click/flash after CLICK_US
//  tone mode  — free-running at 2x tone frequency; toggles the buzzer while
//               the beep gate is open and counts down the LED flash
static volatile bool    toneMode     = false;
static volatile bool    toneGate     = false;
static volatile uint8_t ledCountdown = 0;

static void IRAM_ATTR onTimer1() {
    if (toneMode) {
        if (toneGate) BUZZER_TOGGLE(); else BUZZER_LOW();
        if (ledCountdown && --ledCountdown == 0) LED_LOW();
    } else {
        BUZZER_LOW();
        LED_LOW();
    }
}

void IRAM_ATTR outputPulseFromIsr() {
    if (settings.ledEnabled) LED_HIGH();
    if (toneMode) {
        ledCountdown = LED_TONE_PERIODS;
    } else {
        if (settings.buzzerEnabled) BUZZER_HIGH();
        timer1_write(CLICK_TICKS);   // (re)starts the one-shot
    }
}

void outputBegin() {
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    BUZZER_LOW();
    LED_LOW();
    timer1_isr_init();
    timer1_attachInterrupt(onTimer1);
    timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);
}

void outputAlarm(bool sounding, bool fast, unsigned long now) {
    if (sounding != toneMode) {
        noInterrupts();
        toneMode = sounding;
        toneGate = false;
        BUZZER_LOW();
        LED_LOW();
        ledCountdown = 0;
        timer1_enable(TIM_DIV16, TIM_EDGE, sounding ? TIM_LOOP : TIM_SINGLE);
        timer1_write(sounding ? TONE_HALF_TICKS : CLICK_TICKS);
        interrupts();
    }
    if (sounding) {
        // Beep pattern: alarm 300 ms on / 300 ms off, overload 100/100
        uint16_t period = fast ? 200 : 600;
        toneGate = (now % period) < period / 2;
    }
}
