#pragma once

#include <Arduino.h>

// Pin convention:
//   0-99   = native ESP32 GPIO  (digitalWrite / digitalRead)
//   100-115 = PCF8575 P0-P15    (I2C expander at address 0x20)

#define PCF_PIN_OFFSET 100
#define PCF_PIN_COUNT  16

class PinExpander {
public:
    static PinExpander& instance();

    bool begin(uint8_t sda, uint8_t scl, uint8_t addr = 0x20);
    bool isReady() const { return _ready; }

    void writePin(uint8_t pcfPin, uint8_t val);
    uint8_t readPin(uint8_t pcfPin);
    uint16_t readAll();

    uint8_t getSDA() const { return _sda; }
    uint8_t getSCL() const { return _scl; }
    uint8_t getAddress() const { return _addr; }

private:
    PinExpander() = default;
    bool _ready = false;
    uint8_t _sda = 0;
    uint8_t _scl = 0;
    uint8_t _addr = 0x20;
    uint16_t _outputState = 0xFFFF; // PCF8575 default: all HIGH (inputs)
};

// Global helpers — route by pin number
void xDigitalWrite(uint8_t pin, uint8_t val);
uint8_t xDigitalRead(uint8_t pin);
void xPinMode(uint8_t pin, uint8_t mode);
