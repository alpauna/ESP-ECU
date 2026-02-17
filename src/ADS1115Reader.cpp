#include "ADS1115Reader.h"
#include "PinExpander.h"
#include "Logger.h"
#include <Wire.h>

// ---- ISR ----

void IRAM_ATTR ADS1115Reader::alertISR(void* arg) {
    volatile bool* flag = static_cast<volatile bool*>(arg);
    *flag = true;
}

// ---- Lifecycle ----

ADS1115Reader::ADS1115Reader()
    : _ready(false), _alertPin(0), _useAlertPin(false),
      _alertIsNative(false), _convReady(false) {}

bool ADS1115Reader::begin(uint8_t addr, adsGain_t gain, uint16_t rate) {
    _ready = _ads.begin(addr);
    if (_ready) {
        // Verify the device is real — floating I2C bus can cause false positive ACK
        Wire.beginTransmission(addr);
        Wire.write(0x01);  // Config register pointer
        Wire.endTransmission(false);
        uint8_t recv = Wire.requestFrom(addr, (uint8_t)2);
        if (recv != 2) {
            Log.warn("ADS", "ADS1115 ghost at 0x%02X (no data) — skipped", addr);
            _ready = false;
            return false;
        }
        uint8_t hi = Wire.read();
        uint8_t lo = Wire.read();
        uint16_t configVal = ((uint16_t)hi << 8) | lo;
        if (configVal == 0x0000 || configVal == 0xFFFF) {
            Log.warn("ADS", "ADS1115 ghost at 0x%02X (config=0x%04X) — skipped", addr, configVal);
            _ready = false;
            return false;
        }
        _ads.setGain(gain);
        _ads.setDataRate(rate);
        Log.info("ADS", "ADS1115 initialized at 0x%02X (config=0x%04X)", addr, configVal);
    } else {
        Log.warn("ADS", "ADS1115 not found at 0x%02X", addr);
    }
    return _ready;
}

// ---- ALERT/RDY pin setup ----
//
// The Adafruit library already configures conversion-ready mode in startADCReading():
//   Hi_thresh = 0x8000, Lo_thresh = 0x0000, CQUE = 1CONV
// ALERT/RDY (open-drain) goes LOW when conversion completes.
//
// Native GPIO:  IRAM_ATTR ISR on FALLING edge → volatile _convReady flag.
//               conversionComplete() = volatile bool check (~0µs).
//
// Expander pin: Direct SPI GPIO read via xDigitalRead (~20µs).
//               10x faster than I2C config register poll (~200µs).
//               NOTE: Shared ISR gate (hasSharedInterrupt) is NOT used because
//               CustomPin::update() processes and clears the shared interrupt flag
//               before diagnostics runs, causing a race condition. Direct SPI
//               polling is simple, reliable, and fast enough.
//
// Fallback:     I2C config register poll (~200µs). Used when alertPin = 0.

void ADS1115Reader::setAlertPin(uint16_t pin) {
    _alertPin = pin;
    _useAlertPin = false;
    _alertIsNative = false;
    if (pin == 0) return;

    if (pin >= SPI_EXP_PIN_OFFSET) {
        // MCP23S17 expander pin — direct SPI GPIO read (~20µs vs ~200µs I2C)
        uint8_t expIdx = (pin - SPI_EXP_PIN_OFFSET) / SPI_EXP_PIN_COUNT;
        PinExpander& exp = PinExpander::instance();
        if (exp.isReady(expIdx)) {
            xPinMode(pin, INPUT_PULLUP);
            _useAlertPin = true;
            _alertIsNative = false;
            Log.info("ADS", "ALERT/RDY on expander pin %d (#%d) — SPI read (~20us)", pin, expIdx);
        } else {
            Log.warn("ADS", "ALERT/RDY pin %d: expander #%d not ready — I2C fallback", pin, expIdx);
        }
    } else if (pin < 49) {
        // Native ESP32 GPIO — attach IRAM_ATTR ISR on FALLING edge
        pinMode(pin, INPUT_PULLUP);
        ::attachInterruptArg(digitalPinToInterrupt(pin), alertISR,
                             (void*)&_convReady, FALLING);
        _useAlertPin = true;
        _alertIsNative = true;
        Log.info("ADS", "ALERT/RDY on GPIO%d — IRAM_ATTR ISR (FALLING), zero-cost poll", pin);
    }
}

// ---- Blocking read ----

int16_t ADS1115Reader::readChannel(uint8_t ch) {
    if (!_ready || ch > 3) return 0;

    _convReady = false;
    _ads.startADCReading(MUX_BY_CHANNEL[ch], false);
    uint32_t start = millis();

    if (_useAlertPin && _alertIsNative) {
        // Native GPIO ISR: volatile bool set by IRAM_ATTR handler (~0µs per check)
        while (!_convReady) {
            if (millis() - start > ADS_READ_TIMEOUT_MS) return 0;
        }
    } else if (_useAlertPin) {
        // Expander: direct SPI GPIO read (~20µs per check, 10x faster than I2C)
        while (xDigitalRead(_alertPin) != LOW) {
            if (millis() - start > ADS_READ_TIMEOUT_MS) return 0;
        }
    } else {
        // Fallback: I2C config register poll (~200µs per check)
        while (!_ads.conversionComplete()) {
            if (millis() - start > ADS_READ_TIMEOUT_MS) return 0;
        }
    }
    return _ads.getLastConversionResults();
}

float ADS1115Reader::readMillivolts(uint8_t ch) {
    if (!_ready || ch > 3) return 0.0f;
    return _ads.computeVolts(readChannel(ch)) * 1000.0f;
}

// ---- Non-blocking conversion API ----

void ADS1115Reader::startReading(uint8_t ch) {
    if (!_ready || ch > 3) return;
    _convReady = false;  // Clear flag before starting new conversion
    _ads.startADCReading(MUX_BY_CHANNEL[ch], false);
}

bool ADS1115Reader::conversionComplete() {
    if (!_ready) return false;

    if (_useAlertPin && _alertIsNative) {
        // Native GPIO: volatile bool set by IRAM_ATTR ISR (~0µs)
        return _convReady;
    }
    if (_useAlertPin) {
        // Expander: direct SPI GPIO read (~20µs, 10x faster than I2C)
        return (xDigitalRead(_alertPin) == LOW);
    }
    // Fallback: I2C config register poll (~200µs)
    return _ads.conversionComplete();
}

int16_t ADS1115Reader::getLastResult() {
    if (!_ready) return 0;
    return _ads.getLastConversionResults();
}

float ADS1115Reader::getLastResultMillivolts() {
    if (!_ready) return 0.0f;
    return _ads.computeVolts(getLastResult()) * 1000.0f;
}
