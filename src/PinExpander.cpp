#include "PinExpander.h"
#include "Logger.h"
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include <esp_log.h>

static Adafruit_MCP23X17* _mcp = nullptr;

PinExpander& PinExpander::instance() {
    static PinExpander inst;
    return inst;
}

bool PinExpander::begin(uint8_t sda, uint8_t scl, uint8_t addr) {
    _sda = sda;
    _scl = scl;
    _addr = addr;

    Wire.begin(sda, scl);

    // Suppress Wire I2C error spam during probe
    esp_log_level_set("Wire", ESP_LOG_NONE);

    // Quick probe — single write transaction to check if device is present
    Wire.beginTransmission(addr);
    uint8_t probeErr = Wire.endTransmission();

    if (probeErr != 0) {
        esp_log_level_set("Wire", ESP_LOG_ERROR);
        Log.warn("MCP", "MCP23017 not found at 0x%02X — expander disabled", addr);
        _ready = false;
        return false;
    }

    // Device responded — full initialization (keep Wire logging suppressed
    // in case of transient errors during multi-register setup)
    _mcp = new Adafruit_MCP23X17();
    bool ok = _mcp->begin_I2C(addr, &Wire);

    // Restore Wire logging
    esp_log_level_set("Wire", ESP_LOG_ERROR);

    if (!ok) {
        Log.error("MCP", "MCP23017 init failed at 0x%02X", addr);
        delete _mcp;
        _mcp = nullptr;
        _ready = false;
        return false;
    }

    // Default all pins to INPUT (high-impedance)
    for (uint8_t i = 0; i < 16; i++) {
        _mcp->pinMode(i, INPUT);
    }

    _ready = true;
    Log.info("MCP", "MCP23017 initialized at 0x%02X (SDA=%d, SCL=%d)", addr, sda, scl);
    return true;
}

void PinExpander::pinModeExp(uint8_t expPin, uint8_t mode) {
    if (!_mcp || expPin >= PCF_PIN_COUNT) return;
    _mcp->pinMode(expPin, mode);
}

void PinExpander::writePin(uint8_t expPin, uint8_t val) {
    if (!_mcp || expPin >= PCF_PIN_COUNT) return;
    _mcp->digitalWrite(expPin, val);
}

uint8_t PinExpander::readPin(uint8_t expPin) {
    if (!_mcp || expPin >= PCF_PIN_COUNT) return LOW;
    return _mcp->digitalRead(expPin);
}

uint16_t PinExpander::readAll() {
    if (!_mcp) return 0x0000;
    return _mcp->readGPIOAB();
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
        PinExpander::instance().pinModeExp(pin - PCF_PIN_OFFSET, mode);
        return;
    }
    pinMode(pin, mode);
}
