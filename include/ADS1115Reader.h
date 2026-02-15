#pragma once

#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

class ADS1115Reader {
    static constexpr uint32_t ADS_READ_TIMEOUT_MS = 10;  // Max wait for conversion

public:
    ADS1115Reader();
    bool begin(uint8_t addr = 0x48, adsGain_t gain = GAIN_ONE,
               uint16_t rate = RATE_ADS1115_128SPS);
    int16_t readChannel(uint8_t ch);
    float readMillivolts(uint8_t ch);
    bool isReady() const { return _ready; }

private:
    Adafruit_ADS1115 _ads;
    bool _ready;
};
