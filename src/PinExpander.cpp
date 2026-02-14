#include "PinExpander.h"
#include "Logger.h"
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include <esp_log.h>

static Adafruit_MCP23X17* _mcp[PCF_MAX_EXPANDERS] = {nullptr, nullptr};

PinExpander& PinExpander::instance() {
    static PinExpander inst;
    return inst;
}

bool PinExpander::begin(uint8_t sda, uint8_t scl, uint8_t addr) {
    return begin(0, sda, scl, addr);
}

bool PinExpander::begin(uint8_t index, uint8_t sda, uint8_t scl, uint8_t addr) {
    if (index >= PCF_MAX_EXPANDERS) return false;

    _sda = sda;
    _scl = scl;
    _addr[index] = addr;

    if (!_wireInitialized) {
        Wire.begin(sda, scl);
        _wireInitialized = true;
    }

    // Suppress Wire I2C error spam during probe
    esp_log_level_set("Wire", ESP_LOG_NONE);

    // Quick probe — single write transaction to check if device is present
    Wire.beginTransmission(addr);
    uint8_t probeErr = Wire.endTransmission();

    if (probeErr != 0) {
        esp_log_level_set("Wire", ESP_LOG_ERROR);
        Log.warn("MCP", "MCP23017 #%d not found at 0x%02X — expander disabled", index, addr);
        _ready[index] = false;
        return false;
    }

    // Device responded — full initialization
    _mcp[index] = new Adafruit_MCP23X17();
    bool ok = _mcp[index]->begin_I2C(addr, &Wire);

    // Restore Wire logging
    esp_log_level_set("Wire", ESP_LOG_ERROR);

    if (!ok) {
        Log.error("MCP", "MCP23017 #%d init failed at 0x%02X", index, addr);
        delete _mcp[index];
        _mcp[index] = nullptr;
        _ready[index] = false;
        return false;
    }

    // Default all pins to INPUT (high-impedance)
    for (uint8_t i = 0; i < 16; i++) {
        _mcp[index]->pinMode(i, INPUT);
    }

    _ready[index] = true;
    Log.info("MCP", "MCP23017 #%d initialized at 0x%02X (SDA=%d, SCL=%d)", index, addr, sda, scl);

    // Suppress Wire I2C error spam from non-present devices on shared bus
    esp_log_level_set("Wire", ESP_LOG_NONE);

    return true;
}

void PinExpander::pinModeExp(uint8_t expPin, uint8_t mode) {
    uint8_t idx = expPin / PCF_PIN_COUNT;
    uint8_t localPin = expPin % PCF_PIN_COUNT;
    if (idx >= PCF_MAX_EXPANDERS || !_mcp[idx]) return;
    _mcp[idx]->pinMode(localPin, mode);
}

void PinExpander::writePin(uint8_t expPin, uint8_t val) {
    uint8_t idx = expPin / PCF_PIN_COUNT;
    uint8_t localPin = expPin % PCF_PIN_COUNT;
    if (idx >= PCF_MAX_EXPANDERS || !_mcp[idx]) return;
    _mcp[idx]->digitalWrite(localPin, val);
}

uint8_t PinExpander::readPin(uint8_t expPin) {
    uint8_t idx = expPin / PCF_PIN_COUNT;
    uint8_t localPin = expPin % PCF_PIN_COUNT;
    if (idx >= PCF_MAX_EXPANDERS || !_mcp[idx]) return LOW;
    return _mcp[idx]->digitalRead(localPin);
}

uint16_t PinExpander::readAll(uint8_t index) {
    if (index >= PCF_MAX_EXPANDERS || !_mcp[index]) return 0x0000;
    return _mcp[index]->readGPIOAB();
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
