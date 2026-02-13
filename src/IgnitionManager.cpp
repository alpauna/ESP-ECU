#include "IgnitionManager.h"
#include "Logger.h"

IgnitionManager::IgnitionManager()
    : _numCylinders(0), _advanceDeg(10.0f), _dwellMs(DEFAULT_DWELL_MS),
      _maxDwellMs(4.0f), _revLimit(DEFAULT_REV_LIMIT), _revLimiting(false) {
    memset(_coilPins, 0, sizeof(_coilPins));
    memset(_firingOrder, 0, sizeof(_firingOrder));
    memset(_coilState, 0, sizeof(_coilState));
}

IgnitionManager::~IgnitionManager() {}

void IgnitionManager::begin(uint8_t numCylinders, const uint8_t* coilPins, const uint8_t* firingOrder) {
    _numCylinders = min(numCylinders, (uint8_t)MAX_CYLINDERS);
    memcpy(_coilPins, coilPins, _numCylinders);
    memcpy(_firingOrder, firingOrder, _numCylinders);

    for (uint8_t i = 0; i < _numCylinders; i++) {
        if (_coilPins[i] != 0) {
            pinMode(_coilPins[i], OUTPUT);
            digitalWrite(_coilPins[i], LOW);
        }
        _coilState[i] = {false, 0};
    }

    Log.info("IGN", "Ignition initialized: %d cylinders, advance=%.1f deg", _numCylinders, _advanceDeg);
}

void IgnitionManager::setAdvance(float deg) {
    _advanceDeg = constrain(deg, -10.0f, 60.0f);
}

void IgnitionManager::setDwellMs(float ms) {
    _dwellMs = constrain(ms, 0.5f, _maxDwellMs);
}

void IgnitionManager::setRevLimit(uint16_t rpm) {
    _revLimit = rpm;
}

void IgnitionManager::update(uint16_t rpm, uint16_t toothPos, bool sequential) {
    // Rev limiter — cut spark above limit
    if (rpm > _revLimit) {
        if (!_revLimiting) {
            _revLimiting = true;
            cutSpark();
        }
        return;
    }
    _revLimiting = false;

    if (rpm == 0 || _numCylinders == 0) return;

    // Calculate degrees per tooth
    // For a 36-1 wheel: 360/36 = 10 degrees per tooth
    // toothPos is current tooth count (0 = TDC reference after gap)
    float degreesPerTooth = 360.0f / 36.0f;  // TODO: get from crank sensor
    float currentAngle = toothPos * degreesPerTooth;

    // Dwell time in microseconds
    float dwellUs = _dwellMs * 1000.0f;

    // Degrees per microsecond at current RPM
    float degPerUs = (rpm * 360.0f) / 60000000.0f;
    float dwellDeg = dwellUs * degPerUs;

    // Firing interval: 720 degrees / numCylinders (4-stroke)
    float firingIntervalDeg = 720.0f / _numCylinders;

    int64_t nowUs = esp_timer_get_time();

    for (uint8_t i = 0; i < _numCylinders; i++) {
        uint8_t cylIdx = _firingOrder[i] - 1;  // firingOrder is 1-based
        if (cylIdx >= MAX_CYLINDERS) continue;

        // Calculate spark angle for this cylinder
        float sparkAngle = fmodf(i * firingIntervalDeg - _advanceDeg, 720.0f);
        if (sparkAngle < 0) sparkAngle += 720.0f;

        // In wasted spark mode, fire every 360 degrees
        if (!sequential) {
            sparkAngle = fmodf(sparkAngle, 360.0f);
        }

        float dwellStartAngle = sparkAngle - dwellDeg;
        if (dwellStartAngle < 0) dwellStartAngle += sequential ? 720.0f : 360.0f;

        // Check if we should start charging the coil
        float angleWindow = degreesPerTooth * 1.5f;
        float angleDiff = currentAngle - dwellStartAngle;
        if (!sequential) angleDiff = fmodf(angleDiff, 360.0f);
        if (angleDiff < 0) angleDiff += sequential ? 720.0f : 360.0f;

        if (angleDiff >= 0 && angleDiff < angleWindow && !_coilState[cylIdx].charging) {
            // Start dwell (charge coil)
            digitalWrite(_coilPins[cylIdx], HIGH);
            _coilState[cylIdx].charging = true;
            _coilState[cylIdx].dwellStartUs = nowUs;
        }

        // Check if we should fire (release coil)
        float sparkDiff = currentAngle - sparkAngle;
        if (!sequential) sparkDiff = fmodf(sparkDiff, 360.0f);
        if (sparkDiff < 0) sparkDiff += sequential ? 720.0f : 360.0f;

        if (sparkDiff >= 0 && sparkDiff < angleWindow && _coilState[cylIdx].charging) {
            // Fire spark
            digitalWrite(_coilPins[cylIdx], LOW);
            _coilState[cylIdx].charging = false;
        }

        // Safety: max dwell time exceeded
        if (_coilState[cylIdx].charging) {
            float elapsedMs = (nowUs - _coilState[cylIdx].dwellStartUs) / 1000.0f;
            if (elapsedMs > _maxDwellMs) {
                digitalWrite(_coilPins[cylIdx], LOW);
                _coilState[cylIdx].charging = false;
            }
        }
    }
}

void IgnitionManager::cutSpark() {
    for (uint8_t i = 0; i < _numCylinders; i++) {
        if (_coilPins[i] != 0) digitalWrite(_coilPins[i], LOW);
        _coilState[i].charging = false;
    }
}
