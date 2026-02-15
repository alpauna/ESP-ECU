#include "ADS1115Reader.h"
#include "Logger.h"
#include <Wire.h>

ADS1115Reader::ADS1115Reader() : _ready(false) {}

bool ADS1115Reader::begin(uint8_t addr, adsGain_t gain, uint16_t rate) {
    _ready = _ads.begin(addr);
    if (_ready) {
        // Verify the device is real — floating I2C bus can cause false positive ACK
        // Read config register (0x01) via raw Wire. ADS1115 default config has
        // specific bit patterns that a ghost device won't produce.
        Wire.beginTransmission(addr);
        Wire.write(0x01);  // Config register pointer
        Wire.endTransmission(false);  // repeated start
        uint8_t recv = Wire.requestFrom(addr, (uint8_t)2);
        if (recv != 2) {
            Log.warn("ADS", "ADS1115 ghost at 0x%02X (no data) — skipped", addr);
            _ready = false;
            return false;
        }
        uint8_t hi = Wire.read();
        uint8_t lo = Wire.read();
        uint16_t configVal = ((uint16_t)hi << 8) | lo;
        // ADS1115 config POR default is 0x8583. After setGain/setDataRate it changes.
        // But a ghost device returns 0x0000 or 0xFFFF. Check for those.
        if (configVal == 0x0000 || configVal == 0xFFFF) {
            Log.warn("ADS", "ADS1115 ghost at 0x%02X (config=0x%04X) — skipped", addr, configVal);
            _ready = false;
            return false;
        }
        _ads.setGain(gain);
        _ads.setDataRate(rate);
        Log.info("ADS", "ADS1115 initialized at 0x%02X (config=0x%04X)", addr, configVal);
    } else {
        Log.warn("ADS", "ADS1115 not found at 0x%02X", addr);
    }
    return _ready;
}

int16_t ADS1115Reader::readChannel(uint8_t ch) {
    if (!_ready || ch > 3) return 0;
    _ads.startADCReading(MUX_BY_CHANNEL[ch], false);
    uint32_t start = millis();
    while (!_ads.conversionComplete()) {
        if (millis() - start > ADS_READ_TIMEOUT_MS) {
            return 0;
        }
    }
    return _ads.getLastConversionResults();
}

float ADS1115Reader::readMillivolts(uint8_t ch) {
    if (!_ready || ch > 3) return 0.0f;
    return _ads.computeVolts(readChannel(ch)) * 1000.0f;
}
