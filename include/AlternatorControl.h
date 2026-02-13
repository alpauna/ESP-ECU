#pragma once

#include <Arduino.h>

class AlternatorControl {
public:
    static constexpr float DEFAULT_TARGET_VOLTAGE = 13.6f;
    static constexpr float OVERVOLTAGE_CUTOFF = 15.0f;
    static constexpr float MAX_DUTY_PERCENT = 95.0f;
    static const uint32_t PWM_FREQUENCY_HZ = 25000;
    static const uint8_t PWM_RESOLUTION_BITS = 8;

    AlternatorControl();
    ~AlternatorControl();

    void begin(uint8_t pin, float targetV = DEFAULT_TARGET_VOLTAGE);
    void update(float batteryVoltage);

    float getDuty() const { return _dutyPercent; }
    float getTargetVoltage() const { return _targetVoltage; }
    bool isOvervoltage() const { return _overvoltage; }

    void setTargetVoltage(float v);
    void setPID(float p, float i, float d);

private:
    uint8_t _pin;
    float _targetVoltage;
    float _dutyPercent;
    bool _overvoltage;

    float _kp, _ki, _kd;
    float _integral;
    float _prevError;
    uint32_t _lastUpdateMs;

    void setDuty(float percent);
};
