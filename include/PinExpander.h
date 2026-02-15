#pragma once

#include <Arduino.h>
#include <SPI.h>

// Pin convention:
//   0-99    = native ESP32 GPIO  (digitalWrite / digitalRead)
// I2C MCP23017 expanders:
//   100-115 = MCP23017 #0 GPA0-GPB7 (I2C, 0x20)
//   116-131 = MCP23017 #1 GPA0-GPB7 (I2C, 0x21)
//   132-147 = MCP23017 #2 GPA0-GPB7 (I2C, 0x22)
//   148-163 = MCP23017 #3 GPA0-GPB7 (I2C, 0x23)
// SPI MCP23S17 expanders:
//   200-215 = MCP23S17 #0 GPA0-GPB7 (SPI, HSPI) — coils
//   216-231 = MCP23S17 #1 GPA0-GPB7 (SPI, HSPI) — injectors

#define PCF_PIN_OFFSET      100
#define PCF_PIN_COUNT        16
#define PCF_MAX_EXPANDERS    4

#define SPI_EXP_PIN_OFFSET   200
#define SPI_EXP_PIN_COUNT    16
#define SPI_EXP_MAX          2

// MCP23S17 register addresses (BANK=0, sequential)
#define MCP23S17_IODIRA   0x00
#define MCP23S17_IODIRB   0x01
#define MCP23S17_IOCON    0x0A
#define MCP23S17_GPIOA    0x12
#define MCP23S17_GPIOB    0x13
#define MCP23S17_OLATA    0x14
#define MCP23S17_OLATB    0x15

class PinExpander {
public:
    static PinExpander& instance();

    // I2C MCP23017 init
    bool begin(uint8_t index, uint8_t sda, uint8_t scl, uint8_t addr);
    bool begin(uint8_t sda, uint8_t scl, uint8_t addr = 0x20);

    // SPI MCP23S17 init
    bool beginSPI(uint8_t index, SPIClass* spi, uint8_t csPin, uint8_t hwAddr = 0);

    // I2C accessors
    bool isReady(uint8_t index = 0) const { return index < PCF_MAX_EXPANDERS && _ready[index]; }
    void pinModeExp(uint8_t expPin, uint8_t mode);
    void writePin(uint8_t expPin, uint8_t val);
    uint8_t readPin(uint8_t expPin);
    uint16_t readAll(uint8_t index = 0);

    // SPI accessors
    bool isSpiReady(uint8_t index) const { return index < SPI_EXP_MAX && _spiReady[index]; }
    void pinModeSpi(uint8_t spiPin, uint8_t mode);
    void writePinSpi(uint8_t spiPin, uint8_t val);
    uint8_t readPinSpi(uint8_t spiPin);
    uint16_t readAllSpi(uint8_t index);

    // Health check — returns bitmask of failed devices (bits 0-3 = I2C, 4-5 = SPI)
    uint8_t healthCheck();

    // Info accessors
    uint8_t getSDA() const { return _sda; }
    uint8_t getSCL() const { return _scl; }
    uint8_t getAddress(uint8_t index = 0) const { return index < PCF_MAX_EXPANDERS ? _addr[index] : 0; }
    uint8_t getExpanderCount() const { return PCF_MAX_EXPANDERS; }
    uint8_t getSpiExpanderCount() const { return SPI_EXP_MAX; }
    uint8_t getSpiCS(uint8_t index) const { return index < SPI_EXP_MAX ? _spiCS[index] : 0; }
    uint8_t getSpiHwAddr(uint8_t index) const { return index < SPI_EXP_MAX ? _spiHwAddr[index] : 0; }

private:
    PinExpander() = default;

    // I2C state
    bool _ready[PCF_MAX_EXPANDERS] = {};
    uint8_t _sda = 0;
    uint8_t _scl = 0;
    uint8_t _addr[PCF_MAX_EXPANDERS] = {0x20, 0x21, 0x22, 0x23};
    bool _wireInitialized = false;

    // SPI state
    SPIClass* _spi = nullptr;
    bool _spiReady[SPI_EXP_MAX] = {};
    uint8_t _spiCS[SPI_EXP_MAX] = {};
    uint8_t _spiHwAddr[SPI_EXP_MAX] = {};
    uint16_t _spiShadow[SPI_EXP_MAX] = {};   // output latch shadow (OLAT)
    uint16_t _spiDir[SPI_EXP_MAX] = {0xFFFF, 0xFFFF}; // IODIR shadow (1=input, default all input)

    // SPI register helpers (10MHz, direct transfer)
    void spiWriteReg(uint8_t index, uint8_t reg, uint8_t val);
    uint8_t spiReadReg(uint8_t index, uint8_t reg);
    void spiWriteReg16(uint8_t index, uint8_t reg, uint16_t val);
    uint16_t spiReadReg16(uint8_t index, uint8_t reg);
};

// Global helpers — route by pin number (native GPIO / I2C MCP23017 / SPI MCP23S17)
void xDigitalWrite(uint8_t pin, uint8_t val);
uint8_t xDigitalRead(uint8_t pin);
void xPinMode(uint8_t pin, uint8_t mode);
