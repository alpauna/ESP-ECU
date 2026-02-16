#pragma once

#include <Arduino.h>
#include <SPI.h>

class ADS1115Reader;

class CJ125Controller {
public:
    enum HeaterState : uint8_t {
        IDLE,
        WAIT_POWER,
        CALIBRATING,
        CONDENSATION,
        RAMP_UP,
        PID,
        ERROR
    };

    CJ125Controller(SPIClass* spi);

    void begin(uint16_t spiSS1, uint16_t spiSS2,
               uint8_t heaterOut1, uint8_t heaterOut2,
               uint8_t uaPin1, uint8_t uaPin2);
    void setADS1115(ADS1115Reader* ads) { _ads = ads; }
    void update(float batteryVoltage);

    float getLambda(uint8_t bank) const;
    float getAfr(uint8_t bank) const;
    float getOxygen(uint8_t bank) const;

    HeaterState getHeaterState(uint8_t bank) const;
    const char* getHeaterStateStr(uint8_t bank) const;
    float getHeaterDuty(uint8_t bank) const;
    uint16_t getUrValue(uint8_t bank) const;
    uint16_t getUaValue(uint8_t bank) const;
    uint16_t getDiagStatus(uint8_t bank) const;
    bool isReady(uint8_t bank) const;

    bool isEnabled() const { return _enabled; }
    void setEnabled(bool en) { _enabled = en; }

private:
    // SPI register constants (from Lambda Shield)
    static const uint16_t CJ125_IDENT_REG_REQUEST        = 0x4800;
    static const uint16_t CJ125_DIAG_REG_REQUEST         = 0x7800;
    static const uint16_t CJ125_INIT_REG1_MODE_CALIBRATE = 0x569D;
    static const uint16_t CJ125_INIT_REG1_MODE_NORMAL_V8 = 0x5688;
    static const uint16_t CJ125_DIAG_REG_STATUS_OK       = 0x28FF;

    // PID constants (from Lambda Shield)
    static constexpr float PID_P     = 120.0f;
    static constexpr float PID_I     = 0.8f;
    static constexpr float PID_D     = 10.0f;
    static constexpr float PID_I_MAX = 250.0f;
    static constexpr float PID_I_MIN = -250.0f;

    // Heater constants
    static constexpr float    CONDENSATION_VOLTAGE    = 2.0f;
    static constexpr uint32_t CONDENSATION_DURATION_MS = 5000;
    static constexpr float    RAMP_START_VOLTAGE      = 8.5f;
    static constexpr float    RAMP_END_VOLTAGE        = 13.0f;
    static constexpr float    RAMP_RATE_V_PER_SEC     = 0.4f;
    static constexpr float    MIN_BATTERY_VOLTAGE     = 11.0f;

    struct BankState {
        HeaterState heaterState = IDLE;
        uint16_t uaRef = 0;        // UA calibration reference (10-bit)
        uint16_t urRef = 0;        // UR calibration reference (10-bit)
        float lambda = 0.0f;
        float afr = 14.7f;
        float oxygenPct = 0.0f;
        float heaterDuty = 0.0f;   // 0-100%
        uint16_t urValue = 0;      // Current UR ADC (10-bit equiv)
        uint16_t uaValue = 0;      // Current UA ADC (10-bit equiv)
        uint16_t diagStatus = 0;
        uint16_t spiSS = 0;        // Chip select pin (MCP23S17)
        uint8_t heaterPin = 0;     // LEDC PWM output
        uint8_t ledcChannel = 0;   // LEDC channel number
        uint8_t uaPin = 0;         // ADC input
        // PID state
        int heaterPwm = 0;         // 0-255 PWM duty
        float pidIntegral = 0.0f;
        int pidPrevError = 0;
        float rampVoltage = 0.0f;  // During RAMP_UP
        // Timing
        unsigned long stateStartMs = 0;
    };

    SPIClass* _spi;
    ADS1115Reader* _ads = nullptr;
    BankState _banks[2];
    bool _enabled = false;
    uint8_t _decimationCount = 0;

    uint16_t spiTransfer(uint8_t bank, uint16_t data);
    void updateBank(uint8_t bank, float batteryVoltage);
    void readSensors(uint8_t bank);
    void setHeaterPwm(uint8_t bank, float targetV, float batteryV);
    uint16_t readUA(uint8_t bank) const;
    float adcToLambda(uint16_t adc10bit) const;

    // Bosch LSU 4.9 Ip-to-Lambda piecewise-linear lookup
    struct IpLambdaPoint {
        float adc10bit;
        float lambda;
    };
    static const IpLambdaPoint IP_LAMBDA_TABLE[];
    static const uint8_t IP_LAMBDA_TABLE_SIZE;
};
