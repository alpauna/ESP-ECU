#include "CrankSensor.h"
#include "Logger.h"

CrankSensor* CrankSensor::_instance = nullptr;

CrankSensor::CrankSensor()
    : _pin(0), _totalTeeth(36), _missingTeeth(1), _rpm(0), _toothPosition(0),
      _syncState(LOST), _lastToothTimeUs(0), _toothHistIdx(0),
      _lastPeriodUs(0), _toothCount(0), _gapDetected(false),
      _toothLogIdx(0), _toothLogCapturing(false), _toothLogComplete(false) {
    memset((void*)_toothPeriods, 0, sizeof(_toothPeriods));
    memset((void*)_toothLog, 0, sizeof(_toothLog));
}

CrankSensor::~CrankSensor() {
    if (_instance == this) {
        detachInterrupt(digitalPinToInterrupt(_pin));
        _instance = nullptr;
    }
}

void CrankSensor::begin(uint8_t pin, uint8_t teeth, uint8_t missing) {
    _pin = pin;
    _totalTeeth = teeth;
    _missingTeeth = missing;
    _syncState = LOST;
    _rpm = 0;
    _toothPosition = 0;
    _toothCount = 0;
    _gapDetected = false;
    _lastToothTimeUs = 0;
    _lastPeriodUs = 0;

    _instance = this;
    pinMode(_pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(_pin), isrHandler, FALLING);

    Log.info("CRANK", "Crank sensor on pin %d (%d-%d)", _pin, _totalTeeth, _missingTeeth);
}

void IRAM_ATTR CrankSensor::isrHandler() {
    if (_instance) _instance->processTooth(esp_timer_get_time());
}

void IRAM_ATTR CrankSensor::processTooth(int64_t nowUs) {
    if (_lastToothTimeUs == 0) {
        _lastToothTimeUs = nowUs;
        return;
    }

    uint32_t periodUs = (uint32_t)(nowUs - _lastToothTimeUs);
    _lastToothTimeUs = nowUs;

    // Ignore very short periods (noise)
    if (periodUs < 50) return;

    // Store in period history
    _toothPeriods[_toothHistIdx] = periodUs;
    _toothHistIdx = (_toothHistIdx + 1) % TOOTH_HISTORY_SIZE;

    // Detect missing tooth gap: period > 1.5x average normal tooth period
    uint32_t avgPeriod = averagePeriod();
    bool isGap = (avgPeriod > 0) && (periodUs > avgPeriod + (avgPeriod >> 1));

    switch (_syncState) {
        case LOST:
            if (isGap) {
                _syncState = SYNCING;
                _toothCount = 0;
                _toothPosition = 0;
            }
            break;

        case SYNCING:
            _toothCount++;
            _toothPosition = _toothCount;
            if (isGap) {
                // Verify: gap should come after (totalTeeth - missingTeeth) real teeth
                uint8_t expectedTeeth = _totalTeeth - _missingTeeth;
                if (_toothCount == expectedTeeth) {
                    _syncState = SYNCED;
                } else {
                    // Wrong count — restart sync
                }
                _toothCount = 0;
                _toothPosition = 0;
            }
            break;

        case SYNCED:
            _toothCount++;
            _toothPosition = _toothCount;
            if (isGap) {
                uint8_t expectedTeeth = _totalTeeth - _missingTeeth;
                if (_toothCount != expectedTeeth) {
                    // Lost sync
                    _syncState = LOST;
                    _rpm = 0;
                }
                _toothCount = 0;
                _toothPosition = 0;
            }
            break;
    }

    _lastPeriodUs = periodUs;

    // Tooth logging (if active)
    if (_toothLogCapturing && _toothLogIdx < TOOTH_LOG_SIZE) {
        _toothLog[_toothLogIdx].periodUs = periodUs;
        _toothLog[_toothLogIdx].toothNum = _toothPosition;
        _toothLogIdx++;
        if (_toothLogIdx >= TOOTH_LOG_SIZE) {
            _toothLogCapturing = false;
            _toothLogComplete = true;
        }
    }

    // Calculate RPM from tooth period
    // One revolution = totalTeeth tooth periods
    // RPM = 60,000,000 / (periodUs * totalTeeth)
    if (periodUs > 0 && _syncState != LOST) {
        uint32_t rpmCalc = 60000000UL / ((uint32_t)periodUs * _totalTeeth);
        if (rpmCalc < 20000) _rpm = (uint16_t)rpmCalc;
    }
}

void CrankSensor::startToothLog() {
    _toothLogIdx = 0;
    _toothLogComplete = false;
    _toothLogCapturing = true;
}

void CrankSensor::stopToothLog() {
    _toothLogCapturing = false;
}

uint32_t CrankSensor::averagePeriod() const {
    uint32_t sum = 0;
    uint8_t count = 0;
    for (uint8_t i = 0; i < TOOTH_HISTORY_SIZE; i++) {
        if (_toothPeriods[i] > 0) {
            sum += _toothPeriods[i];
            count++;
        }
    }
    return (count > 0) ? (sum / count) : 0;
}
