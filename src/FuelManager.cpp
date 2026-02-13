#include "FuelManager.h"
#include "ECU.h"
#include "TuneTable.h"
#include "Logger.h"

FuelManager::FuelManager()
    : _veTable(nullptr), _afrTable(nullptr), _sparkTable(nullptr),
      _basePulseWidthUs(0), _targetAfr(STOICH_AFR), _ve(0), _reqFuelMs(0),
      _o2PGain(O2_P_GAIN), _o2IGain(O2_I_GAIN),
      _closedLoopMinRpm(800), _closedLoopMaxRpm(4000), _closedLoopMaxMapKpa(80.0f),
      _prevTps(0), _accelEnrichRemaining(0), _lastUpdateMs(0) {
    memset(_o2Bank, 0, sizeof(_o2Bank));
}

FuelManager::~FuelManager() {}

void FuelManager::begin() {
    _lastUpdateMs = millis();
    Log.info("FUEL", "FuelManager initialized, reqFuel=%.2fms", _reqFuelMs);
}

void FuelManager::setReqFuel(float ccPerMin, float displacementCc, uint8_t numCyl) {
    // REQ_FUEL = displacement per cylinder / (injector flow rate * 2)
    // Units: cc / (cc/min) = min → convert to ms
    if (ccPerMin > 0 && numCyl > 0) {
        float displacementPerCyl = displacementCc / numCyl;
        _reqFuelMs = (displacementPerCyl / ccPerMin) * 60.0f * 1000.0f;
    }
    Log.info("FUEL", "REQ_FUEL = %.3f ms (flow=%0.f cc/min, disp=%0.f cc, %d cyl)",
             _reqFuelMs, ccPerMin, displacementCc, numCyl);
}

void FuelManager::setClosedLoopWindow(uint16_t minRpm, uint16_t maxRpm, float maxMapKpa) {
    _closedLoopMinRpm = minRpm;
    _closedLoopMaxRpm = maxRpm;
    _closedLoopMaxMapKpa = maxMapKpa;
}

float FuelManager::getO2Correction(uint8_t bank) const {
    return (bank < 2) ? _o2Bank[bank].correction : 0.0f;
}

void FuelManager::update(EngineState& state) {
    uint32_t now = millis();
    float dt = (now - _lastUpdateMs) / 1000.0f;
    _lastUpdateMs = now;
    if (dt <= 0 || dt > 1.0f) dt = 0.01f;

    float rpm = state.rpm;
    float mapKpa = state.mapKpa;
    float tps = state.tps;

    // Cranking enrichment
    if (state.cranking) {
        _basePulseWidthUs = DEFAULT_CRANKING_PW_US;
        _targetAfr = 12.0f;  // Rich for cranking
        state.targetAfr = _targetAfr;
        state.injPulseWidthUs = _basePulseWidthUs;
        _prevTps = tps;
        return;
    }

    // VE table lookup
    if (_veTable && _veTable->isInitialized()) {
        _ve = _veTable->lookup(rpm, mapKpa);
    } else {
        _ve = 80.0f;  // Default 80% VE
    }

    // AFR target table lookup
    if (_afrTable && _afrTable->isInitialized()) {
        _targetAfr = _afrTable->lookup(rpm, mapKpa);
    } else {
        _targetAfr = STOICH_AFR;
    }

    // Spark advance table lookup
    if (_sparkTable && _sparkTable->isInitialized()) {
        state.sparkAdvanceDeg = _sparkTable->lookup(rpm, mapKpa);
    }

    // Base pulse width: REQ_FUEL * VE/100 * (STOICH / TARGET_AFR)
    _basePulseWidthUs = _reqFuelMs * 1000.0f * (_ve / 100.0f) * (STOICH_AFR / _targetAfr);

    // Warmup enrichment (CLT-based)
    float warmupMult = calculateWarmupEnrichment(state.coolantTempF);
    _basePulseWidthUs *= warmupMult;

    // Acceleration enrichment (TPS-based)
    float accelAdd = calculateAccelEnrichment(tps);
    _basePulseWidthUs += accelAdd;

    // O2 closed-loop correction
    if (isInClosedLoopWindow(rpm, mapKpa)) {
        updateO2ClosedLoop(0, state.afr[0], _targetAfr);
        updateO2ClosedLoop(1, state.afr[1], _targetAfr);
        // Average correction from both banks
        float avgCorrection = (_o2Bank[0].correction + _o2Bank[1].correction) / 2.0f;
        _basePulseWidthUs *= (1.0f + avgCorrection);
    }

    // Clamp and update state
    _basePulseWidthUs = constrain(_basePulseWidthUs, 0.0f, 25000.0f);
    state.targetAfr = _targetAfr;
    state.injPulseWidthUs = _basePulseWidthUs;
    _prevTps = tps;
}

float FuelManager::calculateWarmupEnrichment(float coolantTempF) {
    // Linear warmup enrichment: +40% at 32°F, 0% at 160°F
    if (coolantTempF >= 160.0f) return 1.0f;
    if (coolantTempF <= 32.0f) return 1.4f;
    float frac = (coolantTempF - 32.0f) / (160.0f - 32.0f);
    return 1.4f - 0.4f * frac;
}

float FuelManager::calculateAccelEnrichment(float tps) {
    float tpsRate = (tps - _prevTps);  // TPS change per update cycle
    if (tpsRate > 5.0f) {
        // Add enrichment proportional to TPS rate
        _accelEnrichRemaining = tpsRate * 50.0f;  // us per %TPS/sec
    }
    float enrich = 0;
    if (_accelEnrichRemaining > 0) {
        enrich = _accelEnrichRemaining;
        _accelEnrichRemaining *= 0.8f;  // Decay
        if (_accelEnrichRemaining < 10.0f) _accelEnrichRemaining = 0;
    }
    return enrich;
}

void FuelManager::updateO2ClosedLoop(uint8_t bank, float actualAfr, float targetAfr) {
    if (bank >= 2) return;
    float error = targetAfr - actualAfr;  // Positive = too lean, need more fuel

    _o2Bank[bank].integral += error * _o2IGain;
    _o2Bank[bank].integral = constrain(_o2Bank[bank].integral, -O2_CORRECTION_LIMIT, O2_CORRECTION_LIMIT);

    _o2Bank[bank].correction = (_o2PGain * error) + _o2Bank[bank].integral;
    _o2Bank[bank].correction = constrain(_o2Bank[bank].correction, -O2_CORRECTION_LIMIT, O2_CORRECTION_LIMIT);
    _o2Bank[bank].lastAfr = actualAfr;
}

bool FuelManager::isInClosedLoopWindow(uint16_t rpm, float mapKpa) const {
    return rpm >= _closedLoopMinRpm &&
           rpm <= _closedLoopMaxRpm &&
           mapKpa <= _closedLoopMaxMapKpa;
}
