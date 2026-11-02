/**************************************************************************/
/*!
  @file     FT6336U.cpp
  Author: Atsushi Sasaki (https://github.com/aselectroworks)
  License: MIT (see LICENSE)
*/
/**************************************************************************/

#include "FT6336U.h"

#include <Wire.h>

FT6336U::FT6336U(uint8_t rst_n, uint8_t int_n)
: rst_n(rst_n), int_n(int_n) {
}
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_TEENSY41) || defined(ARDUINO_TEENSY40) || defined(TEENSYDUINO)
FT6336U::FT6336U(int8_t sda, int8_t scl, uint8_t rst_n, uint8_t int_n)
: sda(sda), scl(scl), rst_n(rst_n), int_n(int_n)  {
}
#endif
FT6336U::~FT6336U() {
}


void FT6336U::begin(void) {
    // Initialize I2C
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_TEENSY41) || defined(ARDUINO_TEENSY40) || defined(TEENSYDUINO)
    if(sda != -1 && scl != -1) {
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
        Wire.begin(sda, scl);
#elif defined(ARDUINO_TEENSY41) || defined(ARDUINO_TEENSY40) || defined(TEENSYDUINO) || defined(TEENSYDUINO)
        Wire.setSCL(scl);
        Wire.setSDA(sda);
        Wire.begin();
#endif
    }
    else {
        Wire.begin();
    }
#else
    Wire.begin();
#endif
    // Int Pin Configuration
    pinMode(int_n, INPUT);
    // Reset Pin Configuration
    pinMode(rst_n, OUTPUT);
    digitalWrite(rst_n, LOW);
    delay(10);
    digitalWrite(rst_n, HIGH);
    delay(500);
}
uint8_t FT6336U::read_device_mode(void) {
    return (readByte(FT6336U_ADDR_DEVICE_MODE) & 0x70) >> 4;
}
void FT6336U::write_device_mode(DEVICE_MODE_Enum mode) {
    writeByte(FT6336U_ADDR_DEVICE_MODE, (mode & 0x07) << 4);
}
uint8_t FT6336U::read_gesture_id(void) {
    return readByte(FT6336U_ADDR_GESTURE_ID);
}
uint8_t FT6336U::read_td_status(void) {
    return readByte(FT6336U_ADDR_TD_STATUS);
}
uint8_t FT6336U::read_touch_number(void) {
    return readByte(FT6336U_ADDR_TD_STATUS) & 0x0F;
}
// Touch 1 functions
uint16_t FT6336U::read_touch1_x(void) {
    uint8_t read_buf[2];
    read_buf[0] = readByte(FT6336U_ADDR_TOUCH1_X);
    read_buf[1] = readByte(FT6336U_ADDR_TOUCH1_X + 1);
    return ((read_buf[0] & 0x0f) << 8) | read_buf[1];
}
uint16_t FT6336U::read_touch1_y(void) {
    uint8_t read_buf[2];
    read_buf[0] = readByte(FT6336U_ADDR_TOUCH1_Y);
    read_buf[1] = readByte(FT6336U_ADDR_TOUCH1_Y + 1);
    return ((read_buf[0] & 0x0f) << 8) | read_buf[1];
}
uint8_t FT6336U::read_touch1_event(void) {
    return readByte(FT6336U_ADDR_TOUCH1_EVENT) >> 6;
}
uint8_t FT6336U::read_touch1_id(void) {
    return readByte(FT6336U_ADDR_TOUCH1_ID) >> 4;
}
uint8_t FT6336U::read_touch1_weight(void) {
    return readByte(FT6336U_ADDR_TOUCH1_WEIGHT);
}
uint8_t FT6336U::read_touch1_misc(void) {
    return readByte(FT6336U_ADDR_TOUCH1_MISC) >> 4;
}
// Touch 2 functions
uint16_t FT6336U::read_touch2_x(void) {
    uint8_t read_buf[2];
    read_buf[0] = readByte(FT6336U_ADDR_TOUCH2_X);
    read_buf[1] = readByte(FT6336U_ADDR_TOUCH2_X + 1);
    return ((read_buf[0] & 0x0f) << 8) | read_buf[1];
}
uint16_t FT6336U::read_touch2_y(void) {
    uint8_t read_buf[2];
    read_buf[0] = readByte(FT6336U_ADDR_TOUCH2_Y);
    read_buf[1] = readByte(FT6336U_ADDR_TOUCH2_Y + 1);
    return ((read_buf[0] & 0x0f) << 8) | read_buf[1];
}
uint8_t FT6336U::read_touch2_event(void) {
    return readByte(FT6336U_ADDR_TOUCH2_EVENT) >> 6;
}
uint8_t FT6336U::read_touch2_id(void) {
    return readByte(FT6336U_ADDR_TOUCH2_ID) >> 4;
}
uint8_t FT6336U::read_touch2_weight(void) {
    return readByte(FT6336U_ADDR_TOUCH2_WEIGHT);
}
uint8_t FT6336U::read_touch2_misc(void) {
    return readByte(FT6336U_ADDR_TOUCH2_MISC) >> 4;
}

// Mode Parameter Register
uint8_t FT6336U::read_touch_threshold(void) {
    return readByte(FT6336U_ADDR_THRESHOLD);
}
uint8_t FT6336U::read_filter_coefficient(void) {
    return readByte(FT6336U_ADDR_FILTER_COE);
}
uint8_t FT6336U::read_ctrl_mode(void) {
    return readByte(FT6336U_ADDR_CTRL);
}
void FT6336U::write_ctrl_mode(CTRL_MODE_Enum mode) {
    writeByte(FT6336U_ADDR_CTRL, mode);
}
uint8_t FT6336U::read_time_period_enter_monitor(void) {
    return readByte(FT6336U_ADDR_TIME_ENTER_MONITOR);
}
uint8_t FT6336U::read_active_rate(void) {
    return readByte(FT6336U_ADDR_ACTIVE_MODE_RATE);
}
uint8_t FT6336U::read_monitor_rate(void) {
    return readByte(FT6336U_ADDR_MONITOR_MODE_RATE);
}

// Gesture Parameters
uint8_t FT6336U::read_radian_value(void) {
    return readByte(FT6336U_ADDR_RADIAN_VALUE);
}
void FT6336U::write_radian_value(uint8_t val) {
    writeByte(FT6336U_ADDR_RADIAN_VALUE, val);
}
uint8_t FT6336U::read_offset_left_right(void) {
    return readByte(FT6336U_ADDR_OFFSET_LEFT_RIGHT);
}
void FT6336U::write_offset_left_right(uint8_t val) {
    writeByte(FT6336U_ADDR_OFFSET_LEFT_RIGHT, val);
}
uint8_t FT6336U::read_offset_up_down(void) {
    return readByte(FT6336U_ADDR_OFFSET_UP_DOWN);
}
void FT6336U::write_offset_up_down(uint8_t val) {
    writeByte(FT6336U_ADDR_OFFSET_UP_DOWN, val);
}
uint8_t FT6336U::read_distance_left_right(void) {
    return readByte(FT6336U_ADDR_DISTANCE_LEFT_RIGHT);
}
void FT6336U::write_distance_left_right(uint8_t val) {
    writeByte(FT6336U_ADDR_DISTANCE_LEFT_RIGHT, val);
}
uint8_t FT6336U::read_distance_up_down(void) {
    return readByte(FT6336U_ADDR_DISTANCE_UP_DOWN);
}
void FT6336U::write_distance_up_down(uint8_t val) {
    writeByte(FT6336U_ADDR_DISTANCE_UP_DOWN, val);
}
uint8_t FT6336U::read_distance_zoom(void) {
    return readByte(FT6336U_ADDR_DISTANCE_ZOOM);
}
void FT6336U::write_distance_zoom(uint8_t val) {
    writeByte(FT6336U_ADDR_DISTANCE_ZOOM, val);
}


// System Information
uint16_t FT6336U::read_library_version(void) {
    uint8_t read_buf[2];
    read_buf[0] = readByte(FT6336U_ADDR_LIBRARY_VERSION_H);
    read_buf[1] = readByte(FT6336U_ADDR_LIBRARY_VERSION_L);
    return ((read_buf[0] & 0x0f) << 8) | read_buf[1];
}
uint8_t FT6336U::read_chip_id(void) {
    return readByte(FT6336U_ADDR_CHIP_ID);
}
uint8_t FT6336U::read_g_mode(void) {
    return readByte(FT6336U_ADDR_G_MODE);
}
void FT6336U::write_g_mode(G_MODE_Enum mode){
    writeByte(FT6336U_ADDR_G_MODE, mode);
}
uint8_t FT6336U::read_pwrmode(void) {
    return readByte(FT6336U_ADDR_POWER_MODE);
}
uint8_t FT6336U::read_firmware_id(void) {
    return readByte(FT6336U_ADDR_FIRMARE_ID);
}
uint8_t FT6336U::read_focaltech_id(void) {
    return readByte(FT6336U_ADDR_FOCALTECH_ID);
}
uint8_t FT6336U::read_release_code_id(void) {
    return readByte(FT6336U_ADDR_RELEASE_CODE_ID);
}
uint8_t FT6336U::read_state(void) {
    return readByte(FT6336U_ADDR_STATE);
}


//coordinate diagram（FPC downwards）
////y ////////////////////264x176
                        //
                        //
                        //x
                        //
                        //
// Registers 0x02 (TD_STATUS) through 0x0E (TOUCH2_MISC) are contiguous on the
// FT6336U, covering both touch points' status/id/x/y/weight/misc -- scan()
// used to fetch them with up to 11 separate single-byte transactions (one
// each for status, id, x-hi, x-lo, y-hi, y-lo, per touch point), each paying
// its own settling delay. Bundling them into one 13-byte burst read cuts
// that to a single transaction per scan, which is what actually fixes the
// "touch feels slow" complaint (2026-08-04) -- shrinking the per-byte delay
// (tried first) instead made a bad trade: a scan-was-already-100%-successful
// design at the cost of one-time 10ms just got replaced with a design where
// each of those 11 transactions could independently hit contention with the
// SPI-heavy screen redraw and eat Wire's ~50ms internal timeout, which is
// worse, not better.
FT6336U_TouchPointType FT6336U::scan(void){
    uint8_t buf[13]; // TD_STATUS, T1_XH/EVENT, T1_XL, T1_YH/ID, T1_YL, T1_WEIGHT, T1_MISC,
                      // T2_XH/EVENT, T2_XL, T2_YH/ID, T2_YL, T2_WEIGHT, T2_MISC
    uint8_t got = readBlock(FT6336U_ADDR_TD_STATUS, buf, sizeof(buf));
    uint8_t touchCount = (got == sizeof(buf)) ? (buf[0] & 0x0F) : 0;
    touchPoint.touch_count = touchCount;

    if (touchCount == 0) {
        touchPoint.tp[0].status = release;
        touchPoint.tp[1].status = release;
    }
    else if (touchCount == 1) {
        uint8_t id1 = buf[3] >> 4; // id1 = 0 or 1
        touchPoint.tp[id1].status = (touchPoint.tp[id1].status == release) ? touch : stream;
        touchPoint.tp[id1].x = ((buf[1] & 0x0f) << 8) | buf[2];
        touchPoint.tp[id1].y = ((buf[3] & 0x0f) << 8) | buf[4];
        touchPoint.tp[~id1 & 0x01].status = release;
    }
    else {
        uint8_t id1 = buf[3] >> 4; // id1 = 0 or 1
        touchPoint.tp[id1].status = (touchPoint.tp[id1].status == release) ? touch : stream;
        touchPoint.tp[id1].x = ((buf[1] & 0x0f) << 8) | buf[2];
        touchPoint.tp[id1].y = ((buf[3] & 0x0f) << 8) | buf[4];
        uint8_t id2 = buf[9] >> 4; // id2 = 0 or 1 (~id1 & 0x01)
        touchPoint.tp[id2].status = (touchPoint.tp[id2].status == release) ? touch : stream;
        touchPoint.tp[id2].x = ((buf[7] & 0x0f) << 8) | buf[8];
        touchPoint.tp[id2].y = ((buf[9] & 0x0f) << 8) | buf[10];
    }

    return touchPoint;

}


// Private Function
uint8_t FT6336U::readByte(uint8_t addr) {
    uint8_t rdData = 0;
    uint8_t rdDataCount = 0;
    // Upstream retried this transaction forever on failure (do{}while(rdDataCount==0)
    // with no cap). A single glitched I2C transaction -- e.g. bus contention from
    // the SPI-heavy full-screen keyboard redraw firing at the same time as a touch
    // poll -- could then spin indefinitely. On this board that reproduced as a
    // hard "Interrupt wdt timeout on CPU0" panic (2026-08-04, tapping through the
    // on-screen WiFi setup keyboard). Capping attempts bounds the worst case to
    // ~200ms instead of freezing the board; a healthy bus still succeeds on the
    // first attempt exactly as before. The delay() belongs BEFORE requestFrom(),
    // not after a failed one -- it's what lets the first attempt actually succeed
    // during SPI contention instead of eating Wire's ~50ms internal timeout first
    // (confirmed the hard way: moving it after made touch response worse, not
    // better, 2026-08-04). This path is only used by the low-frequency config/
    // info accessors now -- scan() uses readBlock() below instead.
    const uint8_t maxAttempts = 20;
    for (uint8_t attempt = 0; attempt < maxAttempts; attempt++) {
        Wire.beginTransmission(I2C_ADDR_FT6336U);
        Wire.write(addr);
        Wire.endTransmission(false); // Restart
        delay(10);
        rdDataCount = Wire.requestFrom(I2C_ADDR_FT6336U, 1);
        if (rdDataCount != 0) break;
    }
    if (rdDataCount == 0) {
        Serial.println("[touch] I2C read timed out after retries, skipping this byte");
    }
    while(Wire.available()) {
        rdData = Wire.read();
    }
    return rdData;

}

// Same retry/settling logic as readByte(), but for scan()'s multi-register burst
// -- one transaction for the whole touch-point block instead of one per byte.
uint8_t FT6336U::readBlock(uint8_t addr, uint8_t *buf, uint8_t len) {
    uint8_t rdDataCount = 0;
    const uint8_t maxAttempts = 20;
    for (uint8_t attempt = 0; attempt < maxAttempts; attempt++) {
        Wire.beginTransmission(I2C_ADDR_FT6336U);
        Wire.write(addr);
        Wire.endTransmission(false); // Restart
        delay(10);
        rdDataCount = Wire.requestFrom(I2C_ADDR_FT6336U, len);
        if (rdDataCount == len) break;
        while (Wire.available()) Wire.read(); // drain a short read before retrying
    }
    if (rdDataCount != len) {
        Serial.println("[touch] I2C block read timed out after retries, skipping this scan");
    }
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = (i < rdDataCount) ? Wire.read() : 0;
    }
    return rdDataCount;
}
void FT6336U::writeByte(uint8_t addr, uint8_t data) {
    DEBUG_PRINTLN("")
    DEBUG_PRINT("writeI2C reg 0x")
    DEBUG_PRINT(addr, HEX)
    DEBUG_PRINT(" -> 0x") DEBUG_PRINTLN(data, HEX)

    Wire.beginTransmission(I2C_ADDR_FT6336U);
    Wire.write(addr);
    Wire.write(data);
    Wire.endTransmission();
}
