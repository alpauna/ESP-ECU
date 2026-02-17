#include "PinExpander.h"
#include "Logger.h"
#include <esp_log.h>

static const uint32_t MCP23S17_SPI_SPEED = 10000000; // 10 MHz

PinExpander& PinExpander::instance() {
    static PinExpander inst;
    return inst;
}

// ---- SPI register helpers ----

void PinExpander::spiWriteReg(uint8_t index, uint8_t reg, uint8_t val) {
    uint8_t opcode = 0x40 | (_hwAddr[index] << 1); // write: R/W=0
    _spi->beginTransaction(SPISettings(MCP23S17_SPI_SPEED, MSBFIRST, SPI_MODE0));
    digitalWrite(_csPin, LOW);
    _spi->transfer(opcode);
    _spi->transfer(reg);
    _spi->transfer(val);
    digitalWrite(_csPin, HIGH);
    _spi->endTransaction();
}

uint8_t PinExpander::spiReadReg(uint8_t index, uint8_t reg) {
    uint8_t opcode = 0x41 | (_hwAddr[index] << 1); // read: R/W=1
    _spi->beginTransaction(SPISettings(MCP23S17_SPI_SPEED, MSBFIRST, SPI_MODE0));
    digitalWrite(_csPin, LOW);
    _spi->transfer(opcode);
    _spi->transfer(reg);
    uint8_t val = _spi->transfer(0x00);
    digitalWrite(_csPin, HIGH);
    _spi->endTransaction();
    return val;
}

void PinExpander::spiWriteReg16(uint8_t index, uint8_t reg, uint16_t val) {
    uint8_t opcode = 0x40 | (_hwAddr[index] << 1);
    _spi->beginTransaction(SPISettings(MCP23S17_SPI_SPEED, MSBFIRST, SPI_MODE0));
    digitalWrite(_csPin, LOW);
    _spi->transfer(opcode);
    _spi->transfer(reg);
    _spi->transfer(val & 0xFF);        // low byte (port A)
    _spi->transfer((val >> 8) & 0xFF); // high byte (port B)
    digitalWrite(_csPin, HIGH);
    _spi->endTransaction();
}

uint16_t PinExpander::spiReadReg16(uint8_t index, uint8_t reg) {
    uint8_t opcode = 0x41 | (_hwAddr[index] << 1);
    _spi->beginTransaction(SPISettings(MCP23S17_SPI_SPEED, MSBFIRST, SPI_MODE0));
    digitalWrite(_csPin, LOW);
    _spi->transfer(opcode);
    _spi->transfer(reg);
    uint8_t lo = _spi->transfer(0x00);
    uint8_t hi = _spi->transfer(0x00);
    digitalWrite(_csPin, HIGH);
    _spi->endTransaction();
    return (uint16_t)hi << 8 | lo;
}

// ---- Device initialization ----

bool PinExpander::begin(uint8_t index, SPIClass* spi, uint8_t csPin, uint8_t hwAddr) {
    if (index >= SPI_EXP_MAX || !spi) return false;

    _spi = spi;
    _csPin = csPin;
    _hwAddr[index] = hwAddr;
    _shadow[index] = 0x0000;
    _dir[index] = 0xFFFF; // all inputs by default

    // CS pin setup (idempotent — safe to call for each device on shared CS)
    pinMode(csPin, OUTPUT);
    digitalWrite(csPin, HIGH);

    // Configure IOCON: HAEN + MIRROR + ODR = 0x4C
    // HAEN(bit3): hardware address enable — required for shared CS
    // MIRROR(bit6): INTA covers both ports
    // ODR(bit2): open-drain INT output — required for shared wired-OR line
    spiWriteReg(index, MCP23S17_IOCON, MCP_IOCON_SHARED);

    // Read back IOCON to verify device is present
    uint8_t readBack = spiReadReg(index, MCP23S17_IOCON);
    if (readBack != MCP_IOCON_SHARED) {
        Log.warn("MCP-SPI", "MCP23S17 #%d not found (CS=%d, hwAddr=%d) — readback 0x%02X",
                 index, csPin, hwAddr, readBack);
        _ready[index] = false;
        return false;
    }

    // Set all pins to INPUT (IODIR = 0xFFFF)
    spiWriteReg16(index, MCP23S17_IODIRA, 0xFFFF);

    // Clear output latches
    spiWriteReg16(index, MCP23S17_OLATA, 0x0000);

    // INTCON = 0 for all pins (compare against previous value = pin change mode)
    spiWriteReg(index, MCP_INTCONA, 0x00);
    spiWriteReg(index, MCP_INTCONB, 0x00);

    // Clear GPINTEN (no pins enabled initially)
    spiWriteReg(index, MCP_GPINTENA, 0x00);
    spiWriteReg(index, MCP_GPINTENB, 0x00);
    _gpinten[index] = 0;

    // Clear any pending interrupts by reading INTCAP
    spiReadReg(index, MCP_INTCAPA);
    spiReadReg(index, MCP_INTCAPB);

    _ready[index] = true;
    Log.info("MCP-SPI", "MCP23S17 #%d initialized (CS=%d, hwAddr=%d, 10MHz, IOCON=0x%02X)",
             index, csPin, hwAddr, MCP_IOCON_SHARED);
    return true;
}

// ---- Health check ----

uint8_t PinExpander::healthCheck() {
    uint8_t failed = 0;
    for (uint8_t i = 0; i < SPI_EXP_MAX; i++) {
        if (!_ready[i]) continue;
        uint8_t iocon = spiReadReg(i, MCP23S17_IOCON);
        if (iocon != MCP_IOCON_SHARED) failed |= (1 << i);
    }
    return failed;
}

// ---- Pin operations ----

void PinExpander::setPinMode(uint8_t pin, uint8_t mode) {
    uint8_t idx = pin / SPI_EXP_PIN_COUNT;
    uint8_t localPin = pin % SPI_EXP_PIN_COUNT;
    if (idx >= SPI_EXP_MAX || !_ready[idx]) return;

    if (mode == OUTPUT) {
        _dir[idx] &= ~(1 << localPin); // 0 = output
        _pullup[idx] &= ~(1 << localPin);
    } else {
        _dir[idx] |= (1 << localPin);  // 1 = input
        if (mode == INPUT_PULLUP) {
            _pullup[idx] |= (1 << localPin);
        } else {
            _pullup[idx] &= ~(1 << localPin);
        }
    }
    spiWriteReg16(idx, MCP23S17_IODIRA, _dir[idx]);
    spiWriteReg16(idx, MCP_GPPUA, _pullup[idx]);
}

void PinExpander::writePin(uint8_t pin, uint8_t val) {
    uint8_t idx = pin / SPI_EXP_PIN_COUNT;
    uint8_t localPin = pin % SPI_EXP_PIN_COUNT;
    if (idx >= SPI_EXP_MAX || !_ready[idx]) return;

    if (val) {
        _shadow[idx] |= (1 << localPin);
    } else {
        _shadow[idx] &= ~(1 << localPin);
    }
    spiWriteReg16(idx, MCP23S17_OLATA, _shadow[idx]);
}

uint8_t PinExpander::readPin(uint8_t pin) {
    uint8_t idx = pin / SPI_EXP_PIN_COUNT;
    uint8_t localPin = pin % SPI_EXP_PIN_COUNT;
    if (idx >= SPI_EXP_MAX || !_ready[idx]) return LOW;

    uint16_t gpio = spiReadReg16(idx, MCP23S17_GPIOA);
    return (gpio & (1 << localPin)) ? HIGH : LOW;
}

uint16_t PinExpander::readAll(uint8_t index) {
    if (index >= SPI_EXP_MAX || !_ready[index]) return 0x0000;
    return spiReadReg16(index, MCP23S17_GPIOA);
}

// ---- Shared open-drain interrupt ----

void IRAM_ATTR PinExpander::intISR(void* arg) {
    volatile bool* flag = (volatile bool*)arg;
    *flag = true;
}

bool PinExpander::attachSharedInterrupt(uint8_t espGpioPin) {
    if (espGpioPin == 0xFF) return false;

    _sharedIntGpio = espGpioPin;
    _sharedIntFlag = false;

    // Configure ESP32 GPIO: active-low open-drain with external pull-up
    pinMode(espGpioPin, INPUT_PULLUP);
    ::attachInterruptArg(digitalPinToInterrupt(espGpioPin), intISR,
                         (void*)&_sharedIntFlag, FALLING);

    Log.info("MCP-SPI", "Shared INT on GPIO %d (open-drain, active-low, %d devices)",
             espGpioPin, SPI_EXP_MAX);
    return true;
}

void PinExpander::enablePinInterrupt(uint8_t globalPin) {
    if (globalPin < SPI_EXP_PIN_OFFSET) return;
    uint8_t pin = globalPin - SPI_EXP_PIN_OFFSET;
    uint8_t idx = pin / SPI_EXP_PIN_COUNT;
    uint8_t localPin = pin % SPI_EXP_PIN_COUNT;
    if (idx >= SPI_EXP_MAX || !_ready[idx] || _sharedIntGpio == 0xFF) return;

    _gpinten[idx] |= (1 << localPin);
    spiWriteReg(idx, MCP_GPINTENA, _gpinten[idx] & 0xFF);
    spiWriteReg(idx, MCP_GPINTENB, (_gpinten[idx] >> 8) & 0xFF);
    Log.info("MCP-SPI", "INT enabled: exp #%d pin %d (local %d)", idx, globalPin, localPin);
}

void PinExpander::disablePinInterrupt(uint8_t globalPin) {
    if (globalPin < SPI_EXP_PIN_OFFSET) return;
    uint8_t pin = globalPin - SPI_EXP_PIN_OFFSET;
    uint8_t idx = pin / SPI_EXP_PIN_COUNT;
    uint8_t localPin = pin % SPI_EXP_PIN_COUNT;
    if (idx >= SPI_EXP_MAX || !_ready[idx]) return;

    _gpinten[idx] &= ~(1 << localPin);
    spiWriteReg(idx, MCP_GPINTENA, _gpinten[idx] & 0xFF);
    spiWriteReg(idx, MCP_GPINTENB, (_gpinten[idx] >> 8) & 0xFF);
}

bool PinExpander::checkInterrupt(uint8_t index, uint16_t& changedPins, uint16_t& capturedValues) {
    if (index >= SPI_EXP_MAX || !_ready[index]) return false;

    // Read INTF — which pin(s) caused the interrupt
    uint8_t intfA = spiReadReg(index, MCP_INTFA);
    uint8_t intfB = spiReadReg(index, MCP_INTFB);
    changedPins = (uint16_t)intfB << 8 | intfA;

    if (changedPins == 0) return false;

    // Read INTCAP — captured port values at time of interrupt (also clears the interrupt)
    uint8_t capA = spiReadReg(index, MCP_INTCAPA);
    uint8_t capB = spiReadReg(index, MCP_INTCAPB);
    capturedValues = (uint16_t)capB << 8 | capA;

    return true;
}

// ---- Global helpers — route by pin number ----

void xDigitalWrite(uint16_t pin, uint8_t val) {
    if (pin >= SPI_EXP_PIN_OFFSET) {
        PinExpander::instance().writePin(pin - SPI_EXP_PIN_OFFSET, val);
    } else {
        digitalWrite(pin, val);
    }
}

uint8_t xDigitalRead(uint16_t pin) {
    if (pin >= SPI_EXP_PIN_OFFSET) {
        return PinExpander::instance().readPin(pin - SPI_EXP_PIN_OFFSET);
    }
    return digitalRead(pin);
}

void xPinMode(uint16_t pin, uint8_t mode) {
    if (pin >= SPI_EXP_PIN_OFFSET) {
        PinExpander::instance().setPinMode(pin - SPI_EXP_PIN_OFFSET, mode);
    } else {
        pinMode(pin, mode);
    }
}
