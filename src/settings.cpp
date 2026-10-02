#include "settings.h"
#include <EEPROM.h>

// ============================================================================
// EEPROM LAYOUT
// ============================================================================
// 0..11  legacy v3.2/v3.3 bytes, read once for migration:
//        1:alarm (whole uSv/h) 2-3:sensitivity 4-5:deadTime 6-7:backgroundCPM
//        8:buzzer 9:led 10-11:alarmThreshold10 (v3.3)
// 32..   Record: magic(2) version(1) Settings crc8(1)
#define EEPROM_SIZE      64
#define RECORD_ADDR      32
#define RECORD_MAGIC     0x4743   // "GC"
#define RECORD_VERSION   1
#define SAVE_DELAY_MS    3000

Settings settings = {50, 175, 190, 0, 1, 1, 1, 5};

static bool          dirty        = false;
static unsigned long lastChangeMs = 0;

static uint8_t crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    while (len--) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

static uint16_t read16(int addr) {
    return ((uint16_t)EEPROM.read(addr + 1) << 8) | EEPROM.read(addr);
}

static bool validTimedInterval(uint8_t m) {
    return m == 1 || m == 2 || m == 5 || m == 10 || m == 30 || m == 60;
}

// Clamp every field so a corrupted-but-CRC-valid record can't break the math
static void sanitize(Settings& s) {
    if (s.alarmThreshold10 < 1 || s.alarmThreshold10 > 1000) s.alarmThreshold10 = 50;
    if (s.tubeSensitivity < 1 || s.tubeSensitivity > 999)    s.tubeSensitivity = 175;
    if (s.deadTime < 10 || s.deadTime > 2000)                 s.deadTime = 190;
    if (s.backgroundCPM > 500)                                s.backgroundCPM = 0;
    if (s.buzzerEnabled > 1)                                  s.buzzerEnabled = 1;
    if (s.ledEnabled > 1)                                     s.ledEnabled = 1;
    if (s.integrationMode > 2)                                s.integrationMode = 1;
    if (!validTimedInterval(s.timedInterval))                 s.timedInterval = 5;
}

static void writeRecord() {
    EEPROM.write(RECORD_ADDR, RECORD_MAGIC & 0xFF);
    EEPROM.write(RECORD_ADDR + 1, RECORD_MAGIC >> 8);
    EEPROM.write(RECORD_ADDR + 2, RECORD_VERSION);
    EEPROM.put(RECORD_ADDR + 3, settings);
    EEPROM.write(RECORD_ADDR + 3 + sizeof(Settings),
                 crc8((const uint8_t*)&settings, sizeof(Settings)));
    EEPROM.commit();   // no flash write if nothing changed
    dirty = false;
}

static void migrateLegacy() {
    Settings s = settings;   // defaults
    uint16_t t10 = read16(10);
    uint8_t  old = EEPROM.read(1);
    if (t10 >= 1 && t10 <= 1000)       s.alarmThreshold10 = t10;
    else if (old >= 2 && old <= 100)   s.alarmThreshold10 = old * 10;
    s.tubeSensitivity = read16(2);
    s.deadTime        = read16(4);
    s.backgroundCPM   = read16(6);
    s.buzzerEnabled   = EEPROM.read(8);
    s.ledEnabled      = EEPROM.read(9);
    sanitize(s);
    settings = s;
}

void loadSettings() {
    EEPROM.begin(EEPROM_SIZE);   // kept open: RAM mirror, commit() on save
    Settings s;
    EEPROM.get(RECORD_ADDR + 3, s);
    bool ok = read16(RECORD_ADDR) == RECORD_MAGIC &&
              EEPROM.read(RECORD_ADDR + 2) == RECORD_VERSION &&
              EEPROM.read(RECORD_ADDR + 3 + sizeof(Settings)) ==
                  crc8((const uint8_t*)&s, sizeof(Settings));
    if (ok) {
        sanitize(s);
        settings = s;
    } else {
        migrateLegacy();
        writeRecord();
    }
}

void markSettingsDirty() {
    dirty = true;
    lastChangeMs = millis();
}

void settingsTick(unsigned long now) {
    if (dirty && now - lastChangeMs >= SAVE_DELAY_MS) writeRecord();
}
