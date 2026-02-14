#include "ADS1115Reader.h"
#include "Logger.h"

ADS1115Reader::ADS1115Reader() : _ready(false) {}

bool ADS1115Reader::begin(uint8_t addr, adsGain_t gain, uint16_t rate) {
    _ready = _ads.begin(addr);
    if (_ready) {
        _ads.setGain(gain);
        _ads.setDataRate(rate);
        Log.info("ADS", "ADS1115 initialized at 0x%02X", addr);
    } else {
        Log.error("ADS", "ADS1115 not found at 0x%02X", addr);
    }
    return _ready;
}

int16_t ADS1115Reader::readChannel(uint8_t ch) {
    if (!_ready || ch > 3) return 0;
    return _ads.readADC_SingleEnded(ch);
}

float ADS1115Reader::readMillivolts(uint8_t ch) {
    if (!_ready || ch > 3) return 0.0f;
    return _ads.computeVolts(_ads.readADC_SingleEnded(ch)) * 1000.0f;
}
