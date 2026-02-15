#include "MCP3204Reader.h"
#include "Logger.h"

MCP3204Reader::MCP3204Reader() : _spi(nullptr), _cs(0), _vRef(5.0f), _ready(false) {}

bool MCP3204Reader::begin(SPIClass* spi, uint8_t csPin, float vRef) {
    _spi = spi;
    _cs = csPin;
    _vRef = vRef;

    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);

    // Probe: read channel 0 and verify response is plausible
    // MCP3204 single-ended command: start=1, SGL/DIFF=1, D2=0, D1=0, D0=0 for CH0
    // Byte 0: 0b00000110 (start + SGL)
    // Byte 1: 0b00000000 (D2=0, D1=0, D0=0, rest don't care)
    // Byte 2: 0b00000000 (clock out result)
    _spi->beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs, LOW);
    uint8_t b0 = _spi->transfer(0x06);  // start=1, SGL=1
    uint8_t b1 = _spi->transfer(0x00);  // CH0 (D2=0,D1=0,D0=0)
    uint8_t b2 = _spi->transfer(0x00);  // clock out result
    digitalWrite(_cs, HIGH);
    _spi->endTransaction();

    // b1 contains null bit + upper 4 bits of result, b2 contains lower 8 bits
    // If no device: all 0xFF (MISO pulled high) or all 0x00 (MISO pulled low)
    if (b0 == 0xFF && b1 == 0xFF && b2 == 0xFF) {
        Log.warn("MCP3204", "No response on CS=GPIO%d (all 0xFF)", _cs);
        return false;
    }
    if (b0 == 0x00 && b1 == 0x00 && b2 == 0x00) {
        Log.warn("MCP3204", "No response on CS=GPIO%d (all 0x00)", _cs);
        return false;
    }

    // Verify null bit: bit 4 of b1 should be 0 (null bit before 12-bit result)
    // The upper nibble of b1 should have at most bit 3..0 set (12-bit result high nibble)
    // If upper nibble of b1 has bits 7..4 set, likely ghost
    if (b1 & 0xF0) {
        Log.warn("MCP3204", "Invalid response on CS=GPIO%d (b1=0x%02X) — skipped", _cs, b1);
        return false;
    }

    _ready = true;
    uint16_t raw = ((uint16_t)(b1 & 0x0F) << 8) | b2;
    Log.info("MCP3204", "Detected on CS=GPIO%d, VRef=%.1fV, probe CH0=%d (%.0fmV)",
             _cs, _vRef, raw, raw * _vRef * 1000.0f / 4095.0f);
    return true;
}

int16_t MCP3204Reader::readChannel(uint8_t ch) {
    if (!_ready || ch > 3) return 0;

    // MCP3204 single-ended command format:
    // Byte 0: 0b0000_0 1 1 D2  — start bit=1, SGL/DIFF=1, D2
    // Byte 1: 0b D1 D0 x x x x x x — D1, D0, then don't care
    // Byte 2: 0b x x x x x x x x — clock out remaining result bits
    uint8_t cmd0 = 0x06 | ((ch >> 2) & 0x01);  // 0b00000110 | D2
    uint8_t cmd1 = (ch & 0x03) << 6;            // D1,D0 in bits 7:6

    _spi->beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs, LOW);
    _spi->transfer(cmd0);
    uint8_t hi = _spi->transfer(cmd1);
    uint8_t lo = _spi->transfer(0x00);
    digitalWrite(_cs, HIGH);
    _spi->endTransaction();

    return (int16_t)(((uint16_t)(hi & 0x0F) << 8) | lo);
}

float MCP3204Reader::readMillivolts(uint8_t ch) {
    int16_t raw = readChannel(ch);
    return (float)raw * _vRef * 1000.0f / 4095.0f;
}
