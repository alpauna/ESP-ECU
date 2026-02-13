#include "AlternatorControl.h"
#include "Logger.h"

AlternatorControl::AlternatorControl()
    : _pin(0), _targetVoltage(DEFAULT_TARGET_VOLTAGE), _dutyPercent(0),
      _overvoltage(false), _kp(10.0f), _ki(5.0f), _kd(0.0f),
      _integral(0), _prevError(0), _lastUpdateMs(0) {}

AlternatorControl::~AlternatorControl() {}

void AlternatorControl::begin(uint8_t pin, float targetV) {
    _pin = pin;
    _targetVoltage = targetV;
    _lastUpdateMs = millis();

    // Configure LEDC for 25kHz PWM (channel 0)
    ledcSetup(0, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
    ledcAttachPin(_pin, 0);
    setDuty(0);

    Log.info("ALT", "Alternator control on pin %d, target %.1fV", _pin, _targetVoltage);
}

void AlternatorControl::update(float batteryVoltage) {
    // Overvoltage safety
    if (batteryVoltage > OVERVOLTAGE_CUTOFF) {
        _overvoltage = true;
        _integral = 0;
        setDuty(0);
        return;
    }
    _overvoltage = false;

    uint32_t now = millis();
    float dt = (now - _lastUpdateMs) / 1000.0f;
    _lastUpdateMs = now;
    if (dt <= 0.0f || dt > 1.0f) dt = 0.1f;  // Clamp for first call or overflow

    float error = _targetVoltage - batteryVoltage;

    // PID calculation
    _integral += error * dt;
    _integral = constrain(_integral, -MAX_DUTY_PERCENT / _ki, MAX_DUTY_PERCENT / _ki);

    float derivative = (dt > 0.001f) ? (error - _prevError) / dt : 0.0f;
    _prevError = error;

    float output = _kp * error + _ki * _integral + _kd * derivative;
    output = constrain(output, 0.0f, MAX_DUTY_PERCENT);

    setDuty(output);
}

void AlternatorControl::setTargetVoltage(float v) {
    _targetVoltage = constrain(v, 12.0f, 15.0f);
    Log.info("ALT", "Target voltage set to %.1fV", _targetVoltage);
}

void AlternatorControl::setPID(float p, float i, float d) {
    _kp = p;
    _ki = i;
    _kd = d;
    _integral = 0;
    _prevError = 0;
    Log.info("ALT", "PID set: P=%.2f I=%.2f D=%.2f", _kp, _ki, _kd);
}

void AlternatorControl::setDuty(float percent) {
    _dutyPercent = constrain(percent, 0.0f, MAX_DUTY_PERCENT);
    uint32_t maxDutyVal = (1 << PWM_RESOLUTION_BITS) - 1;
    uint32_t dutyVal = (uint32_t)(_dutyPercent / 100.0f * maxDutyVal);
    ledcWrite(0, dutyVal);  // Channel 0
}
