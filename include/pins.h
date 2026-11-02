// Freenove ESP32-S3 2.8" CYD (FNK0104AB) pin map.
// Verified against Freenove/Freenove_ESP32_S3_Display (Tutorial_With_Touch,
// Sketch_07.2_Echo and Libraries/FNK0104AB/TFT_eSPI_Setups_v1.3). Display pins
// (TFT_MISO/MOSI/SCLK/CS/DC/RST/BL) are supplied to TFT_eSPI via build_flags
// in platformio.ini, not repeated here.
#pragma once

// ---- Capacitive touch (FT6336U, I2C) ----
#define PIN_TOUCH_SDA   16
#define PIN_TOUCH_SCL   15
#define PIN_TOUCH_RST   18
#define PIN_TOUCH_INT   17

// ---- Audio codec (ES8311, I2S data + I2C control) ----
#define PIN_I2S_MCLK    4
#define PIN_I2S_BCLK    5
#define PIN_I2S_DIN     6   // microphone data into ESP32 (codec ADC output)
#define PIN_I2S_WS      7
#define PIN_I2S_DOUT    8   // speaker data out of ESP32 (codec DAC input)
#define PIN_CODEC_SDA   16  // shared I2C bus with touch controller
#define PIN_CODEC_SCL   15
#define PIN_PA_ENABLE   1   // FM8002E amplifier enable, ACTIVE LOW

#define ES8311_I2C_ADDR 0x18
