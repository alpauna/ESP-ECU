#pragma once

#include <Arduino.h>
#include <SPI.h>

class MCP3204Reader {
public:
    MCP3204Reader();
    bool begin(SPIClass* spi, uint8_t csPin, float vRef = 5.0f);
    int16_t readChannel(uint8_t ch);       // Raw 12-bit value (0-4095)
    float readMillivolts(uint8_t ch);      // (raw / 4095.0) * vRef * 1000
    bool isReady() const { return _ready; }
    uint8_t getCsPin() const { return _cs; }
    float getVRef() const { return _vRef; }

private:
    SPIClass* _spi;
    uint8_t _cs;
    float _vRef;
    bool _ready;
    static constexpr uint32_t SPI_SPEED = 1000000; // 1MHz (MCP3204 max 2MHz @ 5V)
};
