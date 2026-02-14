#pragma once

#include <Arduino.h>

// Pin convention:
//   0-99    = native ESP32 GPIO  (digitalWrite / digitalRead)
//   100-115 = MCP23017 #0 GPA0-GPB7 (I2C expander at address 0x20)
//   116-131 = MCP23017 #1 GPA0-GPB7 (I2C expander at address 0x21)
//
// When USE_SPI_EXPANDERS is defined (hardware installed):
//   200-215 = MCP23S17 #0 GPA0-GPB7 (SPI, HSPI) — coils
//   216-231 = MCP23S17 #1 GPA0-GPB7 (SPI, HSPI) — injectors

#define PCF_PIN_OFFSET 100
#define PCF_PIN_COUNT  16
#define PCF_MAX_EXPANDERS 2

class PinExpander {
public:
    static PinExpander& instance();

    // Init expander at index (0 or 1)
    bool begin(uint8_t index, uint8_t sda, uint8_t scl, uint8_t addr);
    // Convenience: init expander 0
    bool begin(uint8_t sda, uint8_t scl, uint8_t addr = 0x20);

    bool isReady(uint8_t index = 0) const { return index < PCF_MAX_EXPANDERS && _ready[index]; }

    void pinModeExp(uint8_t expPin, uint8_t mode);
    void writePin(uint8_t expPin, uint8_t val);
    uint8_t readPin(uint8_t expPin);
    uint16_t readAll(uint8_t index = 0);

    uint8_t getSDA() const { return _sda; }
    uint8_t getSCL() const { return _scl; }
    uint8_t getAddress(uint8_t index = 0) const { return index < PCF_MAX_EXPANDERS ? _addr[index] : 0; }
    uint8_t getExpanderCount() const { return PCF_MAX_EXPANDERS; }

private:
    PinExpander() = default;
    bool _ready[PCF_MAX_EXPANDERS] = {false, false};
    uint8_t _sda = 0;
    uint8_t _scl = 0;
    uint8_t _addr[PCF_MAX_EXPANDERS] = {0x20, 0x21};
    bool _wireInitialized = false;
};

// Global helpers — route by pin number
void xDigitalWrite(uint8_t pin, uint8_t val);
uint8_t xDigitalRead(uint8_t pin);
void xPinMode(uint8_t pin, uint8_t mode);
