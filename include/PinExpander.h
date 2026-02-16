#pragma once

#include <Arduino.h>
#include <SPI.h>

// Pin convention:
//   0-99    = native ESP32 GPIO  (digitalWrite / digitalRead)
// SPI MCP23S17 expanders (shared CS, HAEN hardware addressing):
//   200-215 = MCP23S17 #0 GPA0-GPB7 (hwAddr 0) — general I/O
//   216-231 = MCP23S17 #1 GPA0-GPB7 (hwAddr 1) — transmission
//   232-247 = MCP23S17 #2 GPA0-GPB7 (hwAddr 2) — expansion
//   248-263 = MCP23S17 #3 GPA0-GPB7 (hwAddr 3) — expansion
//   264-279 = MCP23S17 #4 GPA0-GPB7 (hwAddr 4) — coils
//   280-295 = MCP23S17 #5 GPA0-GPB7 (hwAddr 5) — injectors

#define SPI_EXP_PIN_OFFSET   200
#define SPI_EXP_PIN_COUNT    16
#define SPI_EXP_MAX          6

// MCP23S17 register addresses (BANK=0, sequential)
#define MCP_IODIRA     0x00
#define MCP_IODIRB     0x01
#define MCP_IPOLA      0x02
#define MCP_IPOLB      0x03
#define MCP_GPINTENA   0x04
#define MCP_GPINTENB   0x05
#define MCP_DEFVALA    0x06
#define MCP_DEFVALB    0x07
#define MCP_INTCONA    0x08
#define MCP_INTCONB    0x09
#define MCP_IOCON      0x0A
#define MCP_GPPUA      0x0C
#define MCP_GPPUB      0x0D
#define MCP_INTFA      0x0E
#define MCP_INTFB      0x0F
#define MCP_INTCAPA    0x10
#define MCP_INTCAPB    0x11
#define MCP_GPIOA      0x12
#define MCP_GPIOB      0x13
#define MCP_OLATA      0x14
#define MCP_OLATB      0x15

// Legacy aliases (used in existing code)
#define MCP23S17_IODIRA   MCP_IODIRA
#define MCP23S17_IODIRB   MCP_IODIRB
#define MCP23S17_IOCON    MCP_IOCON
#define MCP23S17_GPIOA    MCP_GPIOA
#define MCP23S17_GPIOB    MCP_GPIOB
#define MCP23S17_OLATA    MCP_OLATA
#define MCP23S17_OLATB    MCP_OLATB

// IOCON values
// HAEN(bit3) = hardware address enable for SPI
// MIRROR(bit6) = INTA and INTB mirrored (one pin covers both ports)
// ODR(bit2) = open-drain INT output (for shared wired-OR line)
#define MCP_IOCON_HAEN     0x08
#define MCP_IOCON_SHARED   0x4C  // MIRROR | HAEN | ODR

class PinExpander {
public:
    static PinExpander& instance();

    // Initialize a single MCP23S17 on shared SPI bus + shared CS pin
    // index: 0-5, hwAddr: hardware address (A2/A1/A0 pins, 0-7)
    bool begin(uint8_t index, SPIClass* spi, uint8_t csPin, uint8_t hwAddr);

    bool isReady(uint8_t index) const { return index < SPI_EXP_MAX && _ready[index]; }

    // Pin operations — pin is local offset (0 to SPI_EXP_MAX*16-1)
    void setPinMode(uint8_t pin, uint8_t mode);
    void writePin(uint8_t pin, uint8_t val);
    uint8_t readPin(uint8_t pin);
    uint16_t readAll(uint8_t index);

    // Health check — returns bitmask of failed devices (bits 0-5)
    uint8_t healthCheck();

    // Shared open-drain interrupt — all INTA pins wired to one ESP32 GPIO + 10k pull-up
    bool attachSharedInterrupt(uint8_t espGpioPin);
    bool hasSharedInterrupt() const { return _sharedIntFlag; }
    void clearSharedInterrupt() { _sharedIntFlag = false; }
    uint8_t getIntGpio() const { return _sharedIntGpio; }

    // Enable/disable interrupt-on-change for a specific expander pin (globalPin 200-295)
    void enablePinInterrupt(uint8_t globalPin);
    void disablePinInterrupt(uint8_t globalPin);

    // Check a specific device for pending interrupt — reads INTF + INTCAP, clears interrupt
    bool checkInterrupt(uint8_t index, uint16_t& changedPins, uint16_t& capturedValues);

    // Check if any pins have interrupt-on-change enabled for a device
    bool hasGpinten(uint8_t index) const { return index < SPI_EXP_MAX && _gpinten[index] != 0; }

    // Info accessors
    uint8_t getExpanderCount() const { return SPI_EXP_MAX; }
    uint8_t getCS() const { return _csPin; }
    uint8_t getHwAddr(uint8_t index) const { return index < SPI_EXP_MAX ? _hwAddr[index] : 0; }
    uint16_t getShadow(uint8_t index) const { return index < SPI_EXP_MAX ? _shadow[index] : 0; }
    uint16_t getDir(uint8_t index) const { return index < SPI_EXP_MAX ? _dir[index] : 0xFFFF; }

private:
    PinExpander() = default;

    SPIClass* _spi = nullptr;
    uint8_t _csPin = 0xFF;              // Shared CS pin for all 6 devices
    bool _ready[SPI_EXP_MAX] = {};
    uint8_t _hwAddr[SPI_EXP_MAX] = {};
    uint16_t _shadow[SPI_EXP_MAX] = {}; // Output latch shadow (OLAT)
    uint16_t _dir[SPI_EXP_MAX] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};

    // Shared interrupt state
    uint8_t _sharedIntGpio = 0xFF;
    volatile bool _sharedIntFlag = false;
    uint16_t _gpinten[SPI_EXP_MAX] = {};  // Shadow of GPINTENA|B per device

    // SPI register helpers (10MHz, direct transfer)
    void spiWriteReg(uint8_t index, uint8_t reg, uint8_t val);
    uint8_t spiReadReg(uint8_t index, uint8_t reg);
    void spiWriteReg16(uint8_t index, uint8_t reg, uint16_t val);
    uint16_t spiReadReg16(uint8_t index, uint8_t reg);

    // Generic ISR handler — arg points to volatile bool flag
    static void IRAM_ATTR intISR(void* arg);
};

// Global helpers — route by pin number (native GPIO / SPI MCP23S17)
// uint16_t pin: 0-48 = native GPIO, 200-295 = MCP23S17 expander
void xDigitalWrite(uint16_t pin, uint8_t val);
uint8_t xDigitalRead(uint16_t pin);
void xPinMode(uint16_t pin, uint8_t mode);
