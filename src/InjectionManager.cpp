#include "InjectionManager.h"
#include "PinExpander.h"
#include "Logger.h"

InjectionManager::InjectionManager()
    : _numCylinders(0), _basePulseWidthUs(0), _deadTimeMs(DEFAULT_DEAD_TIME_MS),
      _fuelCut(false) {
    memset(_injectorPins, 0, sizeof(_injectorPins));
    memset(_firingOrder, 0, sizeof(_firingOrder));
    memset(_injState, 0, sizeof(_injState));
    for (uint8_t i = 0; i < MAX_CYLINDERS; i++) _trimPercent[i] = 1.0f;
}

InjectionManager::~InjectionManager() {}

void InjectionManager::begin(uint8_t numCylinders, const uint16_t* injectorPins, const uint8_t* firingOrder) {
    _numCylinders = min(numCylinders, (uint8_t)MAX_CYLINDERS);
    memcpy(_injectorPins, injectorPins, _numCylinders * sizeof(uint16_t));
    memcpy(_firingOrder, firingOrder, _numCylinders);

    for (uint8_t i = 0; i < _numCylinders; i++) {
        if (_injectorPins[i] != 0) {
            xPinMode(_injectorPins[i], OUTPUT);
            xDigitalWrite(_injectorPins[i], LOW);
        }
        _injState[i] = {false, 0, 0};
    }

    Log.info("INJ", "Injection initialized: %d cylinders, deadTime=%.1fms", _numCylinders, _deadTimeMs);
}

void InjectionManager::setPulseWidthUs(float pw) {
    _basePulseWidthUs = constrain(pw, 0.0f, MAX_PULSE_WIDTH_US);
}

void InjectionManager::setDeadTimeMs(float dt) {
    _deadTimeMs = constrain(dt, 0.0f, 5.0f);
}

void InjectionManager::setTrim(uint8_t cyl, float trimPercent) {
    if (cyl < MAX_CYLINDERS) _trimPercent[cyl] = constrain(trimPercent, 0.5f, 1.5f);
}

float InjectionManager::getTrim(uint8_t cyl) const {
    return (cyl < MAX_CYLINDERS) ? _trimPercent[cyl] : 1.0f;
}

float InjectionManager::getEffectivePulseWidthUs(uint8_t cyl) const {
    if (cyl >= MAX_CYLINDERS || _fuelCut) return 0.0f;
    return (_basePulseWidthUs * _trimPercent[cyl]) + (_deadTimeMs * 1000.0f);
}

void InjectionManager::cutFuel() {
    _fuelCut = true;
    for (uint8_t i = 0; i < _numCylinders; i++) {
        if (_injectorPins[i] != 0) xDigitalWrite(_injectorPins[i], LOW);
        _injState[i].open = false;
    }
}

void InjectionManager::resumeFuel() {
    _fuelCut = false;
}

void InjectionManager::update(uint16_t rpm, uint16_t toothPos, bool sequential) {
    if (_fuelCut || rpm == 0 || _numCylinders == 0) return;

    int64_t nowUs = esp_timer_get_time();

    // Calculate degrees per tooth (assuming 36 tooth wheel)
    float degreesPerTooth = 360.0f / 36.0f;  // TODO: get from crank sensor
    float currentAngle = toothPos * degreesPerTooth;

    // Firing interval: 720 degrees / numCylinders (4-stroke)
    float firingIntervalDeg = 720.0f / _numCylinders;

    for (uint8_t i = 0; i < _numCylinders; i++) {
        uint8_t cylIdx = _firingOrder[i] - 1;  // firingOrder is 1-based
        if (cylIdx >= MAX_CYLINDERS) continue;

        float effectivePw = getEffectivePulseWidthUs(cylIdx);
        if (effectivePw <= 0) continue;

        if (_injectorPins[cylIdx] == 0) continue;  // No pin assigned

        if (sequential) {
            // Sequential: inject during intake stroke for each cylinder
            float injectAngle = fmodf(i * firingIntervalDeg + 360.0f, 720.0f);
            float angleWindow = degreesPerTooth * 1.5f;
            float angleDiff = currentAngle - injectAngle;
            angleDiff = fmodf(angleDiff + 720.0f, 720.0f);

            if (angleDiff >= 0 && angleDiff < angleWindow && !_injState[cylIdx].open) {
                // Open injector
                xDigitalWrite(_injectorPins[cylIdx], HIGH);
                _injState[cylIdx].open = true;
                _injState[cylIdx].openTimeUs = nowUs;
                _injState[cylIdx].scheduledPulseUs = effectivePw;
            }
        } else {
            // Batch mode: fire all injectors at TDC (tooth position 0)
            if (toothPos == 0 && !_injState[cylIdx].open) {
                xDigitalWrite(_injectorPins[cylIdx], HIGH);
                _injState[cylIdx].open = true;
                _injState[cylIdx].openTimeUs = nowUs;
                _injState[cylIdx].scheduledPulseUs = effectivePw / 2.0f;  // Half PW per event (fires twice per cycle)
            }
        }

        // Close injector when pulse width is complete
        if (_injState[cylIdx].open) {
            float elapsedUs = (float)(nowUs - _injState[cylIdx].openTimeUs);
            if (elapsedUs >= _injState[cylIdx].scheduledPulseUs) {
                xDigitalWrite(_injectorPins[cylIdx], LOW);
                _injState[cylIdx].open = false;
            }
        }
    }
}
