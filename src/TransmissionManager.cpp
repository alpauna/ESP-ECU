#include "TransmissionManager.h"
#include "ADS1115Reader.h"
#include "PinExpander.h"
#include "Config.h"
#include "Logger.h"

// Static ISR instance pointer
static TransmissionManager* _isrInstance = nullptr;

static void IRAM_ATTR ossISR() {
    if (_isrInstance) _isrInstance->onOssPulse();
}

static void IRAM_ATTR tssISR() {
    if (_isrInstance) _isrInstance->onTssPulse();
}

TransmissionManager::TransmissionManager() {}

void TransmissionManager::configure(const ProjectInfo& proj) {
    _type = static_cast<TransType>(proj.transType);
    _upshift12Rpm = proj.upshift12Rpm;
    _upshift23Rpm = proj.upshift23Rpm;
    _upshift34Rpm = proj.upshift34Rpm;
    _downshift21Rpm = proj.downshift21Rpm;
    _downshift32Rpm = proj.downshift32Rpm;
    _downshift43Rpm = proj.downshift43Rpm;
    _tccLockRpm = proj.tccLockRpm;
    _tccLockGear = proj.tccLockGear;
    _tccApplyRate = proj.tccApplyRate;
    _epcBaseDuty = proj.epcBaseDuty;
    _epcShiftBoost = proj.epcShiftBoost;
    _shiftTimeMs = proj.shiftTimeMs;
    _maxTftTempF = proj.maxTftTempF;

    Log.info("TRANS", "Configured: %s", typeToString(_type));
}

void TransmissionManager::begin(uint8_t ssAPin, uint8_t ssBPin, uint8_t ssCPin, uint8_t ssDPin,
                                 uint8_t tccPin, uint8_t epcPin, uint8_t ossPin, uint8_t tssPin) {
    _ssAPin = ssAPin;
    _ssBPin = ssBPin;
    _ssCPin = ssCPin;
    _ssDPin = ssDPin;
    _tccPin = tccPin;
    _epcPin = epcPin;
    _ossPin = ossPin;
    _tssPin = tssPin;

    // Shift solenoids — MCP23017 outputs
    xPinMode(_ssAPin, OUTPUT);
    xDigitalWrite(_ssAPin, LOW);
    xPinMode(_ssBPin, OUTPUT);
    xDigitalWrite(_ssBPin, LOW);

    if (_type == TransType::FORD_4R100) {
        xPinMode(_ssCPin, OUTPUT);
        xDigitalWrite(_ssCPin, LOW);
        xPinMode(_ssDPin, OUTPUT);
        xDigitalWrite(_ssDPin, LOW);
    }

    // TCC PWM — 200Hz, 8-bit (skip if pin is 0xFF / disabled)
    if (_tccPin != 0xFF) {
        ledcSetup(TCC_LEDC_CH, 200, 8);
        ledcAttachPin(_tccPin, TCC_LEDC_CH);
        ledcWrite(TCC_LEDC_CH, 0);
        _tccEnabled = true;
    }

    // EPC PWM — 5kHz, 8-bit (skip if pin is 0xFF / disabled)
    if (_epcPin != 0xFF) {
        ledcSetup(EPC_LEDC_CH, 5000, 8);
        ledcAttachPin(_epcPin, EPC_LEDC_CH);
        ledcWrite(EPC_LEDC_CH, 0);
        _epcEnabled = true;
    }

    // Speed sensor ISRs (skip if pins are 0xFF / disabled)
    if (_ossPin != 0xFF && _tssPin != 0xFF) {
        _isrInstance = this;
        pinMode(_ossPin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_ossPin), ossISR, RISING);
        pinMode(_tssPin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_tssPin), tssISR, RISING);
        _speedSensorsEnabled = true;
    }

    _lastSpeedCalcMs = millis();

    Log.info("TRANS", "Started: SS_A=%d SS_B=%d TCC=%s EPC=%s OSS=%s TSS=%s",
             _ssAPin, _ssBPin,
             _tccEnabled ? String(_tccPin).c_str() : "OFF",
             _epcEnabled ? String(_epcPin).c_str() : "OFF",
             _speedSensorsEnabled ? String(_ossPin).c_str() : "OFF",
             _speedSensorsEnabled ? String(_tssPin).c_str() : "OFF");
}

void TransmissionManager::onOssPulse() {
    _ossPulseCount++;
}

void TransmissionManager::onTssPulse() {
    _tssPulseCount++;
}

void TransmissionManager::update(uint16_t engineRpm, float tps, float vbat) {
    _updateCounter++;

    // Speed sensors every 100ms (10 × 10ms update cycle)
    if (_speedSensorsEnabled && _updateCounter % 10 == 0) {
        calcSpeedSensors();
    }

    // TFT and MLPS every 200ms
    if (_updateCounter % 20 == 0) {
        readTftTemp();
        _state.mlpsPosition = readMLPS();
    }

    // Engine off protection — all solenoids off
    if (engineRpm == 0) {
        _state.currentGear = Gear::PARK;
        _state.targetGear = Gear::PARK;
        _state.shifting = false;
        _state.tccDuty = 0;
        _state.epcDuty = 0;
        _state.tccLocked = false;
        setSolenoid(_ssAPin, false);
        setSolenoid(_ssBPin, false);
        if (_type == TransType::FORD_4R100) {
            setSolenoid(_ssCPin, false);
            setSolenoid(_ssDPin, false);
        }
        if (_tccEnabled) ledcWrite(TCC_LEDC_CH, 0);
        if (_epcEnabled) ledcWrite(EPC_LEDC_CH, 0);
        _state.ssA = _state.ssB = _state.ssC = _state.ssD = false;
        return;
    }

    // Over-temp protection
    _state.overTemp = (_state.tftTempF > _maxTftTempF);

    // Slip RPM
    _state.slipRpm = (int16_t)_state.tssRpm - (int16_t)_state.ossRpm;

    // Gear logic every 100ms
    if (_updateCounter % 10 == 0) {
        updateGearLogic(engineRpm, tps);
        applyShiftSolenoids();
        updateTCC(engineRpm);
        updateEPC(tps);
    }
}

void TransmissionManager::calcSpeedSensors() {
    uint32_t now = millis();
    uint32_t elapsed = now - _lastSpeedCalcMs;
    if (elapsed < 50) return;

    noInterrupts();
    uint32_t ossPulses = _ossPulseCount;
    uint32_t tssPulses = _tssPulseCount;
    interrupts();

    uint32_t ossDelta = ossPulses - _prevOssPulses;
    uint32_t tssDelta = tssPulses - _prevTssPulses;
    _prevOssPulses = ossPulses;
    _prevTssPulses = tssPulses;
    _lastSpeedCalcMs = now;

    // RPM = (pulses / PULSES_PER_REV) * (60000 / elapsed_ms)
    _state.ossRpm = (uint16_t)((ossDelta * 60000UL) / (PULSES_PER_REV * elapsed));
    _state.tssRpm = (uint16_t)((tssDelta * 60000UL) / (PULSES_PER_REV * elapsed));
}

void TransmissionManager::readTftTemp() {
    if (!_ads) return;
    float mv = _ads->readMillivolts(2);
    _state.tftTempF = tftAdcToTempF(mv);
}

MLPSPosition TransmissionManager::readMLPS() {
    if (!_ads) return MLPSPosition::UNKNOWN;
    float mv = _ads->readMillivolts(3);
    return mlpsVoltageToPosition(mv);
}

void TransmissionManager::updateGearLogic(uint16_t engineRpm, float tps) {
    MLPSPosition mlps = _state.mlpsPosition;

    // Non-drive positions
    if (mlps == MLPSPosition::PARK) {
        _state.currentGear = Gear::PARK;
        _state.targetGear = Gear::PARK;
        _state.shifting = false;
        return;
    }
    if (mlps == MLPSPosition::REVERSE) {
        _state.currentGear = Gear::REVERSE;
        _state.targetGear = Gear::REVERSE;
        _state.shifting = false;
        return;
    }
    if (mlps == MLPSPosition::NEUTRAL) {
        _state.currentGear = Gear::NEUTRAL;
        _state.targetGear = Gear::NEUTRAL;
        _state.shifting = false;
        return;
    }

    // Shift in progress — wait for shift timer
    if (_state.shifting) {
        if (millis() - _shiftStartMs >= _shiftTimeMs) {
            _state.currentGear = _state.targetGear;
            _state.shifting = false;
        }
        return;
    }

    // Determine max gear based on MLPS position
    Gear maxGear = Gear::DRIVE_4;
    if (mlps == MLPSPosition::DRIVE) maxGear = Gear::DRIVE_3;
    else if (mlps == MLPSPosition::LOW2) maxGear = Gear::DRIVE_2;
    else if (mlps == MLPSPosition::LOW1) maxGear = Gear::DRIVE_1;

    Gear cur = _state.currentGear;

    // Default to 1st if coming from P/R/N
    if (cur == Gear::PARK || cur == Gear::REVERSE || cur == Gear::NEUTRAL) {
        cur = Gear::DRIVE_1;
        _state.currentGear = cur;
    }

    Gear newTarget = cur;

    // Upshift logic
    if (cur == Gear::DRIVE_1 && engineRpm >= _upshift12Rpm && maxGear >= Gear::DRIVE_2)
        newTarget = Gear::DRIVE_2;
    else if (cur == Gear::DRIVE_2 && engineRpm >= _upshift23Rpm && maxGear >= Gear::DRIVE_3)
        newTarget = Gear::DRIVE_3;
    else if (cur == Gear::DRIVE_3 && engineRpm >= _upshift34Rpm && maxGear >= Gear::DRIVE_4)
        newTarget = Gear::DRIVE_4;
    // Downshift logic
    else if (cur == Gear::DRIVE_4 && engineRpm <= _downshift43Rpm)
        newTarget = Gear::DRIVE_3;
    else if (cur == Gear::DRIVE_3 && engineRpm <= _downshift32Rpm)
        newTarget = Gear::DRIVE_2;
    else if (cur == Gear::DRIVE_2 && engineRpm <= _downshift21Rpm)
        newTarget = Gear::DRIVE_1;

    // Enforce max gear limit
    if (static_cast<uint8_t>(newTarget) > static_cast<uint8_t>(maxGear))
        newTarget = maxGear;

    // High OSS force upshift (safety)
    if (_state.ossRpm > 6000 && cur < Gear::DRIVE_4 && maxGear >= Gear::DRIVE_4)
        newTarget = static_cast<Gear>(static_cast<uint8_t>(cur) + 1);

    if (newTarget != cur) {
        _state.targetGear = newTarget;
        _state.shifting = true;
        _shiftStartMs = millis();
    }
}

void TransmissionManager::applyShiftSolenoids() {
    Gear g = _state.shifting ? _state.targetGear : _state.currentGear;

    bool a = false, b = false, c = false, d = false;

    if (_type == TransType::FORD_4R70W) {
        // 4R70W solenoid patterns:
        // 1st: SS-A ON,  SS-B OFF
        // 2nd: SS-A OFF, SS-B OFF
        // 3rd: SS-A OFF, SS-B ON
        // 4th: SS-A ON,  SS-B ON
        switch (g) {
            case Gear::DRIVE_1: a = true;  b = false; break;
            case Gear::DRIVE_2: a = false; b = false; break;
            case Gear::DRIVE_3: a = false; b = true;  break;
            case Gear::DRIVE_4: a = true;  b = true;  break;
            case Gear::REVERSE: a = false; b = false; break;
            default:            a = false; b = false; break;
        }
    } else if (_type == TransType::FORD_4R100) {
        // 4R100 solenoid patterns:
        // 1st: SS-A ON,  SS-B OFF, SS-C OFF, SS-D OFF
        // 2nd: SS-A OFF, SS-B OFF, SS-C ON,  SS-D OFF
        // 3rd: SS-A OFF, SS-B ON,  SS-C ON,  SS-D OFF
        // 4th: SS-A ON,  SS-B ON,  SS-C ON,  SS-D OFF
        // Rev: SS-A OFF, SS-B OFF, SS-C OFF, SS-D ON (coast clutch)
        switch (g) {
            case Gear::DRIVE_1: a = true;  b = false; c = false; d = false; break;
            case Gear::DRIVE_2: a = false; b = false; c = true;  d = false; break;
            case Gear::DRIVE_3: a = false; b = true;  c = true;  d = false; break;
            case Gear::DRIVE_4: a = true;  b = true;  c = true;  d = false; break;
            case Gear::REVERSE: a = false; b = false; c = false; d = true;  break;
            default:            a = false; b = false; c = false; d = false; break;
        }
    }

    setSolenoid(_ssAPin, a);
    setSolenoid(_ssBPin, b);
    _state.ssA = a;
    _state.ssB = b;

    if (_type == TransType::FORD_4R100) {
        setSolenoid(_ssCPin, c);
        setSolenoid(_ssDPin, d);
        _state.ssC = c;
        _state.ssD = d;
    }
}

void TransmissionManager::updateTCC(uint16_t engineRpm) {
    uint8_t gearNum = static_cast<uint8_t>(_state.currentGear);

    // Unlock TCC during shifts, over-temp, or if not in high enough gear
    bool shouldLock = !_state.shifting &&
                      !_state.overTemp &&
                      gearNum >= (_tccLockGear + static_cast<uint8_t>(Gear::DRIVE_1) - 1) &&
                      engineRpm >= _tccLockRpm &&
                      _state.currentGear >= Gear::DRIVE_1;

    // Over-temp: force unlock to reduce heat
    if (_state.overTemp) shouldLock = false;

    _tccTargetDuty = shouldLock ? 100.0f : 0.0f;

    // Ramp toward target
    if (_state.tccDuty < _tccTargetDuty) {
        _state.tccDuty += _tccApplyRate;
        if (_state.tccDuty > _tccTargetDuty) _state.tccDuty = _tccTargetDuty;
    } else if (_state.tccDuty > _tccTargetDuty) {
        _state.tccDuty -= _tccApplyRate * 2.0f; // Faster release
        if (_state.tccDuty < 0) _state.tccDuty = 0;
    }

    _state.tccLocked = (_state.tccDuty >= 95.0f);
    if (_tccEnabled) {
        uint8_t pwm = (uint8_t)(_state.tccDuty * 255.0f / 100.0f);
        ledcWrite(TCC_LEDC_CH, pwm);
    }
}

void TransmissionManager::updateEPC(float tps) {
    float duty = _epcBaseDuty;

    // During shifts, boost EPC for firmer engagement
    if (_state.shifting) {
        duty = _epcShiftBoost;
    }

    // TPS adjustment: more throttle = more line pressure = lower EPC duty
    // (EPC is normally-high, lower duty = more pressure on Ford trans)
    duty -= tps * 0.3f;
    if (duty < 10.0f) duty = 10.0f;
    if (duty > 95.0f) duty = 95.0f;

    _state.epcDuty = duty;
    if (_epcEnabled) {
        uint8_t pwm = (uint8_t)(duty * 255.0f / 100.0f);
        ledcWrite(EPC_LEDC_CH, pwm);
    }
}

void TransmissionManager::setSolenoid(uint8_t pin, bool on) {
    xDigitalWrite(pin, on ? HIGH : LOW);
}

float TransmissionManager::tftAdcToTempF(float millivolts) {
    // Ford TFT sensor: NTC thermistor with beta ~3700
    // 5V supply, 2.49k pull-up. R_therm = R_pullup * (Vout / (Vsupply - Vout))
    if (millivolts <= 0 || millivolts >= 4990) return -40.0f;
    float v = millivolts / 1000.0f;
    float rTherm = 2490.0f * (v / (5.0f - v));

    // Steinhart-Hart simplified (beta equation)
    // T(K) = 1 / (1/T0 + (1/B) * ln(R/R0))
    static constexpr float T0 = 298.15f;  // 25°C in Kelvin
    static constexpr float R0 = 2200.0f;  // Resistance at 25°C
    static constexpr float BETA = 3700.0f;

    float tKelvin = 1.0f / (1.0f / T0 + (1.0f / BETA) * logf(rTherm / R0));
    float tCelsius = tKelvin - 273.15f;
    return tCelsius * 9.0f / 5.0f + 32.0f;
}

MLPSPosition TransmissionManager::mlpsVoltageToPosition(float millivolts) {
    // Ford MLPS: resistive voltage divider, distinct voltage per position
    // Typical thresholds (mV) for 5V supply:
    if (millivolts < 300)  return MLPSPosition::PARK;
    if (millivolts < 900)  return MLPSPosition::REVERSE;
    if (millivolts < 1500) return MLPSPosition::NEUTRAL;
    if (millivolts < 2100) return MLPSPosition::OD;
    if (millivolts < 2700) return MLPSPosition::DRIVE;
    if (millivolts < 3500) return MLPSPosition::LOW2;
    if (millivolts < 4500) return MLPSPosition::LOW1;
    return MLPSPosition::UNKNOWN;
}

const char* TransmissionManager::gearToString(Gear g) {
    switch (g) {
        case Gear::PARK:    return "P";
        case Gear::REVERSE: return "R";
        case Gear::NEUTRAL: return "N";
        case Gear::DRIVE_1: return "1";
        case Gear::DRIVE_2: return "2";
        case Gear::DRIVE_3: return "3";
        case Gear::DRIVE_4: return "4";
        default:            return "?";
    }
}

const char* TransmissionManager::mlpsToString(MLPSPosition p) {
    switch (p) {
        case MLPSPosition::PARK:    return "P";
        case MLPSPosition::REVERSE: return "R";
        case MLPSPosition::NEUTRAL: return "N";
        case MLPSPosition::OD:      return "OD";
        case MLPSPosition::DRIVE:   return "D";
        case MLPSPosition::LOW2:    return "2";
        case MLPSPosition::LOW1:    return "1";
        default:                    return "?";
    }
}

const char* TransmissionManager::typeToString(TransType t) {
    switch (t) {
        case TransType::FORD_4R70W: return "4R70W";
        case TransType::FORD_4R100: return "4R100";
        default:                    return "None";
    }
}
