#include "CamSensor.h"
#include "CrankSensor.h"
#include "Logger.h"

CamSensor* CamSensor::_instance = nullptr;

CamSensor::CamSensor()
    : _pin(0), _crankSensor(nullptr), _phase(PHASE_UNKNOWN),
      _lastPulseToothPos(0), _lastPulseTimeUs(0), _pulseReceived(false) {}

CamSensor::~CamSensor() {
    if (_instance == this) {
        detachInterrupt(digitalPinToInterrupt(_pin));
        _instance = nullptr;
    }
}

void CamSensor::begin(uint8_t pin) {
    _pin = pin;
    _phase = PHASE_UNKNOWN;
    _pulseReceived = false;
    _lastPulseTimeUs = 0;

    _instance = this;
    pinMode(_pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(_pin), isrHandler, RISING);

    Log.info("CAM", "Cam sensor on pin %d", _pin);
}

void IRAM_ATTR CamSensor::isrHandler() {
    if (!_instance) return;
    _instance->_lastPulseTimeUs = esp_timer_get_time();
    _instance->_pulseReceived = true;
    if (_instance->_crankSensor) {
        _instance->_lastPulseToothPos = _instance->_crankSensor->getToothPosition();
    }
}

bool CamSensor::isPresent() const {
    // Cam is present if we received a pulse within the timeout window
    if (_lastPulseTimeUs == 0) return false;
    int64_t elapsed = esp_timer_get_time() - _lastPulseTimeUs;
    return elapsed < (int64_t)SIGNAL_TIMEOUT_MS * 1000;
}

bool CamSensor::hasCamSignal() const {
    return isPresent() && _phase != PHASE_UNKNOWN;
}

void CamSensor::update() {
    // Check for timeout — lose cam signal
    if (_lastPulseTimeUs > 0) {
        int64_t elapsed = esp_timer_get_time() - _lastPulseTimeUs;
        if (elapsed > (int64_t)SIGNAL_TIMEOUT_MS * 1000) {
            _phase = PHASE_UNKNOWN;
            return;
        }
    }

    // Process new cam pulse
    if (_pulseReceived) {
        _pulseReceived = false;
        // Determine phase based on crank tooth position at cam pulse
        // For a 4-stroke engine, cam rotates at half crank speed
        // Phase 0: compression TDC for cylinder 1
        // Phase 1: exhaust TDC for cylinder 1
        if (_crankSensor && _crankSensor->isSynced()) {
            uint16_t toothPos = _lastPulseToothPos;
            uint8_t halfTeeth = _crankSensor->getTotalTeeth() / 2;
            // Cam pulse in first half of crank revolution = Phase 0
            _phase = (toothPos < halfTeeth) ? PHASE_0 : PHASE_1;
        }
    }
}
