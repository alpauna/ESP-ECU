#include "PinExpander.h"
#include "Logger.h"
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include <esp_log.h>

static Adafruit_MCP23X17* _mcp[PCF_MAX_EXPANDERS] = {};

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
        Log.warn("MCP", "MCP23017 #%d not found at 0x%02X — expander disabled", index, addr);
        _ready[index] = false;
        return false;
    }

    // Device responded — full initialization
    _mcp[index] = new Adafruit_MCP23X17();
    bool ok = _mcp[index]->begin_I2C(addr, &Wire);

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

// ---- SPI MCP23S17 support ----

static const uint32_t MCP23S17_SPI_SPEED = 10000000; // 10 MHz

void PinExpander::spiWriteReg(uint8_t index, uint8_t reg, uint8_t val) {
    uint8_t opcode = 0x40 | (_spiHwAddr[index] << 1); // write: R/W=0
    _spi->beginTransaction(SPISettings(MCP23S17_SPI_SPEED, MSBFIRST, SPI_MODE0));
    digitalWrite(_spiCS[index], LOW);
    _spi->transfer(opcode);
    _spi->transfer(reg);
    _spi->transfer(val);
    digitalWrite(_spiCS[index], HIGH);
    _spi->endTransaction();
}

uint8_t PinExpander::spiReadReg(uint8_t index, uint8_t reg) {
    uint8_t opcode = 0x41 | (_spiHwAddr[index] << 1); // read: R/W=1
    _spi->beginTransaction(SPISettings(MCP23S17_SPI_SPEED, MSBFIRST, SPI_MODE0));
    digitalWrite(_spiCS[index], LOW);
    _spi->transfer(opcode);
    _spi->transfer(reg);
    uint8_t val = _spi->transfer(0x00);
    digitalWrite(_spiCS[index], HIGH);
    _spi->endTransaction();
    return val;
}

void PinExpander::spiWriteReg16(uint8_t index, uint8_t reg, uint16_t val) {
    uint8_t opcode = 0x40 | (_spiHwAddr[index] << 1);
    _spi->beginTransaction(SPISettings(MCP23S17_SPI_SPEED, MSBFIRST, SPI_MODE0));
    digitalWrite(_spiCS[index], LOW);
    _spi->transfer(opcode);
    _spi->transfer(reg);
    _spi->transfer(val & 0xFF);        // low byte (port A)
    _spi->transfer((val >> 8) & 0xFF); // high byte (port B)
    digitalWrite(_spiCS[index], HIGH);
    _spi->endTransaction();
}

uint16_t PinExpander::spiReadReg16(uint8_t index, uint8_t reg) {
    uint8_t opcode = 0x41 | (_spiHwAddr[index] << 1);
    _spi->beginTransaction(SPISettings(MCP23S17_SPI_SPEED, MSBFIRST, SPI_MODE0));
    digitalWrite(_spiCS[index], LOW);
    _spi->transfer(opcode);
    _spi->transfer(reg);
    uint8_t lo = _spi->transfer(0x00);
    uint8_t hi = _spi->transfer(0x00);
    digitalWrite(_spiCS[index], HIGH);
    _spi->endTransaction();
    return (uint16_t)hi << 8 | lo;
}

bool PinExpander::beginSPI(uint8_t index, SPIClass* spi, uint8_t csPin, uint8_t hwAddr) {
    if (index >= SPI_EXP_MAX || !spi) return false;

    _spi = spi;
    _spiCS[index] = csPin;
    _spiHwAddr[index] = hwAddr;
    _spiShadow[index] = 0x0000;
    _spiDir[index] = 0xFFFF; // all inputs by default

    // CS pin setup
    pinMode(csPin, OUTPUT);
    digitalWrite(csPin, HIGH);

    // Configure IOCON: HAEN=1 (bit 3), BANK=0, SEQOP=0 → 0x08
    spiWriteReg(index, MCP23S17_IOCON, 0x08);

    // Read back IOCON to verify device is present
    uint8_t readBack = spiReadReg(index, MCP23S17_IOCON);
    if (readBack != 0x08) {
        Log.warn("MCP-SPI", "MCP23S17 #%d not found (CS=%d, hwAddr=%d) — readback 0x%02X",
                 index, csPin, hwAddr, readBack);
        _spiReady[index] = false;
        return false;
    }

    // Set all pins to INPUT (IODIR = 0xFFFF)
    spiWriteReg16(index, MCP23S17_IODIRA, 0xFFFF);

    // Clear output latches
    spiWriteReg16(index, MCP23S17_OLATA, 0x0000);

    _spiReady[index] = true;
    Log.info("MCP-SPI", "MCP23S17 #%d initialized (CS=%d, hwAddr=%d, 10MHz)",
             index, csPin, hwAddr);
    return true;
}

void PinExpander::pinModeSpi(uint8_t spiPin, uint8_t mode) {
    uint8_t idx = spiPin / SPI_EXP_PIN_COUNT;
    uint8_t localPin = spiPin % SPI_EXP_PIN_COUNT;
    if (idx >= SPI_EXP_MAX || !_spiReady[idx]) return;

    if (mode == OUTPUT) {
        _spiDir[idx] &= ~(1 << localPin); // 0 = output
    } else {
        _spiDir[idx] |= (1 << localPin);  // 1 = input
    }
    spiWriteReg16(idx, MCP23S17_IODIRA, _spiDir[idx]);
}

void PinExpander::writePinSpi(uint8_t spiPin, uint8_t val) {
    uint8_t idx = spiPin / SPI_EXP_PIN_COUNT;
    uint8_t localPin = spiPin % SPI_EXP_PIN_COUNT;
    if (idx >= SPI_EXP_MAX || !_spiReady[idx]) return;

    if (val) {
        _spiShadow[idx] |= (1 << localPin);
    } else {
        _spiShadow[idx] &= ~(1 << localPin);
    }
    spiWriteReg16(idx, MCP23S17_OLATA, _spiShadow[idx]);
}

uint8_t PinExpander::readPinSpi(uint8_t spiPin) {
    uint8_t idx = spiPin / SPI_EXP_PIN_COUNT;
    uint8_t localPin = spiPin % SPI_EXP_PIN_COUNT;
    if (idx >= SPI_EXP_MAX || !_spiReady[idx]) return LOW;

    uint16_t gpio = spiReadReg16(idx, MCP23S17_GPIOA);
    return (gpio & (1 << localPin)) ? HIGH : LOW;
}

uint16_t PinExpander::readAllSpi(uint8_t index) {
    if (index >= SPI_EXP_MAX || !_spiReady[index]) return 0x0000;
    return spiReadReg16(index, MCP23S17_GPIOA);
}

// ---- I2C MCP23017 methods (unchanged) ----

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

// ---- Global helpers — route by pin number ----

void xDigitalWrite(uint8_t pin, uint8_t val) {
    if (pin >= SPI_EXP_PIN_OFFSET) {
        PinExpander::instance().writePinSpi(pin - SPI_EXP_PIN_OFFSET, val);
    } else if (pin >= PCF_PIN_OFFSET) {
        PinExpander::instance().writePin(pin - PCF_PIN_OFFSET, val);
    } else {
        digitalWrite(pin, val);
    }
}

uint8_t xDigitalRead(uint8_t pin) {
    if (pin >= SPI_EXP_PIN_OFFSET) {
        return PinExpander::instance().readPinSpi(pin - SPI_EXP_PIN_OFFSET);
    }
    if (pin >= PCF_PIN_OFFSET) {
        return PinExpander::instance().readPin(pin - PCF_PIN_OFFSET);
    }
    return digitalRead(pin);
}

void xPinMode(uint8_t pin, uint8_t mode) {
    if (pin >= SPI_EXP_PIN_OFFSET) {
        PinExpander::instance().pinModeSpi(pin - SPI_EXP_PIN_OFFSET, mode);
        return;
    }
    if (pin >= PCF_PIN_OFFSET) {
        PinExpander::instance().pinModeExp(pin - PCF_PIN_OFFSET, mode);
        return;
    }
    pinMode(pin, mode);
}
