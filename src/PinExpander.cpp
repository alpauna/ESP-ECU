#include "PinExpander.h"
#include "Logger.h"
#include <Wire.h>
#include <PCF8575.h>

static PCF8575* _pcf = nullptr;

PinExpander& PinExpander::instance() {
    static PinExpander inst;
    return inst;
}

bool PinExpander::begin(uint8_t sda, uint8_t scl, uint8_t addr) {
    _sda = sda;
    _scl = scl;
    _addr = addr;

    Wire.begin(sda, scl);
    _pcf = new PCF8575(addr, &Wire);

    if (!_pcf->begin()) {
        Log.error("PCF", "PCF8575 not found at 0x%02X (SDA=%d, SCL=%d)", addr, sda, scl);
        delete _pcf;
        _pcf = nullptr;
        _ready = false;
        return false;
    }

    // Set all pins HIGH (default input/off state for PCF8575)
    _outputState = 0xFFFF;
    _pcf->write16(_outputState);

    _ready = true;
    Log.info("PCF", "PCF8575 initialized at 0x%02X (SDA=%d, SCL=%d)", addr, sda, scl);
    return true;
}

void PinExpander::writePin(uint8_t pcfPin, uint8_t val) {
    if (!_pcf || pcfPin >= PCF_PIN_COUNT) return;
    if (val) {
        _outputState |= (1 << pcfPin);
    } else {
        _outputState &= ~(1 << pcfPin);
    }
    _pcf->write16(_outputState);
}

uint8_t PinExpander::readPin(uint8_t pcfPin) {
    if (!_pcf || pcfPin >= PCF_PIN_COUNT) return LOW;
    return _pcf->read(pcfPin);
}

uint16_t PinExpander::readAll() {
    if (!_pcf) return 0xFFFF;
    return _pcf->read16();
}

// --- Global helpers ---

void xDigitalWrite(uint8_t pin, uint8_t val) {
    if (pin >= PCF_PIN_OFFSET) {
        PinExpander::instance().writePin(pin - PCF_PIN_OFFSET, val);
    } else {
        digitalWrite(pin, val);
    }
}

uint8_t xDigitalRead(uint8_t pin) {
    if (pin >= PCF_PIN_OFFSET) {
        return PinExpander::instance().readPin(pin - PCF_PIN_OFFSET);
    }
    return digitalRead(pin);
}

void xPinMode(uint8_t pin, uint8_t mode) {
    if (pin >= PCF_PIN_OFFSET) {
        // PCF8575 has no pin mode register — write HIGH for input, LOW for output-low
        return;
    }
    pinMode(pin, mode);
}
