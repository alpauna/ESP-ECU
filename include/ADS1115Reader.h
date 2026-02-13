#pragma once

#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

class ADS1115Reader {
public:
    ADS1115Reader();
    bool begin(uint8_t addr = 0x48);
    int16_t readChannel(uint8_t ch);
    float readMillivolts(uint8_t ch);
    bool isReady() const { return _ready; }

private:
    Adafruit_ADS1115 _ads;
    bool _ready;
};
