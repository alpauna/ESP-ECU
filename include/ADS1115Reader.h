#pragma once

#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

class ADS1115Reader {
    static constexpr uint32_t ADS_READ_TIMEOUT_MS = 10;  // Max wait for conversion

public:
    ADS1115Reader();
    bool begin(uint8_t addr = 0x48, adsGain_t gain = GAIN_ONE,
               uint16_t rate = RATE_ADS1115_128SPS);
    int16_t readChannel(uint8_t ch);
    float readMillivolts(uint8_t ch);
    bool isReady() const { return _ready; }

    // ALERT/RDY pin — open-drain output on ADS1115 pin 2.
    // Adafruit library configures conversion-ready mode automatically in startADCReading()
    // (Hi_thresh=0x8000, Lo_thresh=0x0000, CQUE=1CONV).
    // Pin goes LOW when conversion completes.
    //
    // Native GPIO: IRAM_ATTR ISR on FALLING edge sets volatile _convReady flag.
    //   conversionComplete() = volatile bool check (~0µs).
    // Expander pin: enablePinInterrupt() hooks shared INT ISR.
    //   conversionComplete() checks shared INT flag first (volatile, ~0µs),
    //   then confirms with SPI pin read (~20µs) only when interrupt occurred.
    // Fallback (pin=0): I2C config register poll (~200µs).
    void setAlertPin(uint16_t pin);
    bool hasAlertPin() const { return _useAlertPin; }

    // Non-blocking conversion API (for idle-time diagnostics)
    void startReading(uint8_t ch);
    bool conversionComplete();
    int16_t getLastResult();
    float getLastResultMillivolts();

private:
    Adafruit_ADS1115 _ads;
    bool _ready;
    uint16_t _alertPin;
    bool _useAlertPin;
    bool _alertIsNative;           // true = native GPIO with ISR, false = expander SPI
    volatile bool _convReady;      // Set by ISR (native GPIO) or checked via SPI (expander)

    // IRAM_ATTR ISR for native GPIO ALERT/RDY — sets volatile flag on FALLING edge
    static void IRAM_ATTR alertISR(void* arg);
};
