#include "SensorManager.h"
#include "CJ125Controller.h"
#include "ADS1115Reader.h"
#include "MCP3204Reader.h"
#include "IgnitionManager.h"
#include "InjectionManager.h"
#include "AlternatorControl.h"
#include "PinExpander.h"
#include "ECU.h"  // for EngineState
#include <esp_adc_cal.h>
#include "Logger.h"

// Default NTC thermistor params
static constexpr float DEFAULT_NTC_PULLUP = 2490.0f;
static constexpr float DEFAULT_NTC_BETA   = 3380.0f;
static constexpr float DEFAULT_EMA_ALPHA  = 0.3f;

SensorManager::SensorManager() {
    for (uint8_t i = 0; i < MAX_SENSORS; i++) _desc[i].clear();
    for (uint8_t i = 0; i < MAX_RULES; i++) _rules[i].clear();
    initDefaultDescriptors();
    initDefaultRules();
}

SensorManager::~SensorManager() {}

void SensorManager::initDefaultDescriptors() {
    // Slot 0: O2 Bank 1
    {
        SensorDescriptor& d = _desc[SLOT_O2_B1];
        d.clear();
        strncpy(d.name, "O2_B1", sizeof(d.name));
        strncpy(d.unit, "AFR", sizeof(d.unit));
        d.sourceType = SRC_GPIO_ADC;
        d.sourcePin = 3;
        d.calType = CAL_LOOKUP;
        d.calA = 10.0f;  // AFR at 0V
        d.calB = 20.0f;  // AFR at 5V
        d.emaAlpha = DEFAULT_EMA_ALPHA;
        d.value = 14.7f;
    }
    // Slot 1: O2 Bank 2
    {
        SensorDescriptor& d = _desc[SLOT_O2_B2];
        d.clear();
        strncpy(d.name, "O2_B2", sizeof(d.name));
        strncpy(d.unit, "AFR", sizeof(d.unit));
        d.sourceType = SRC_GPIO_ADC;
        d.sourcePin = 4;
        d.calType = CAL_LOOKUP;
        d.calA = 10.0f;
        d.calB = 20.0f;
        d.emaAlpha = DEFAULT_EMA_ALPHA;
        d.value = 14.7f;
    }
    // Slot 2: MAP
    {
        SensorDescriptor& d = _desc[SLOT_MAP];
        d.clear();
        strncpy(d.name, "MAP", sizeof(d.name));
        strncpy(d.unit, "kPa", sizeof(d.unit));
        d.sourceType = SRC_MCP3204;  // Preferred; falls back to ADS1115 then GPIO
        d.sourceDevice = 0;
        d.sourceChannel = 0;
        d.sourcePin = 5;
        d.calType = CAL_LINEAR;
        d.calA = 0.5f;   // vMin
        d.calB = 4.5f;   // vMax
        d.calC = 10.0f;  // engMin (kPa)
        d.calD = 105.0f; // engMax (kPa)
        d.emaAlpha = DEFAULT_EMA_ALPHA;
        d.faultBit = 0;  // FAULT_MAP
        d.faultAction = FAULT_ACT_LIMP;
    }
    // Slot 3: TPS
    {
        SensorDescriptor& d = _desc[SLOT_TPS];
        d.clear();
        strncpy(d.name, "TPS", sizeof(d.name));
        strncpy(d.unit, "%", sizeof(d.unit));
        d.sourceType = SRC_MCP3204;
        d.sourceDevice = 0;
        d.sourceChannel = 1;
        d.sourcePin = 6;
        d.calType = CAL_LINEAR;
        d.calA = 0.5f;   // vClosed
        d.calB = 4.5f;   // vOpen
        d.calC = 0.0f;   // 0%
        d.calD = 100.0f; // 100%
        d.emaAlpha = DEFAULT_EMA_ALPHA;
        d.faultBit = 1;  // FAULT_TPS
        d.faultAction = FAULT_ACT_LIMP;
    }
    // Slot 4: CLT
    {
        SensorDescriptor& d = _desc[SLOT_CLT];
        d.clear();
        strncpy(d.name, "CLT", sizeof(d.name));
        strncpy(d.unit, "F", sizeof(d.unit));
        d.sourceType = SRC_GPIO_ADC;
        d.sourcePin = 7;
        d.calType = CAL_NTC;
        d.calA = DEFAULT_NTC_PULLUP;
        d.calB = DEFAULT_NTC_BETA;
        d.calC = ADC_REF_VOLTAGE;
        d.emaAlpha = DEFAULT_EMA_ALPHA;
        d.faultBit = 2;  // FAULT_CLT
        d.faultAction = FAULT_ACT_LIMP;
    }
    // Slot 5: IAT
    {
        SensorDescriptor& d = _desc[SLOT_IAT];
        d.clear();
        strncpy(d.name, "IAT", sizeof(d.name));
        strncpy(d.unit, "F", sizeof(d.unit));
        d.sourceType = SRC_GPIO_ADC;
        d.sourcePin = 8;
        d.calType = CAL_NTC;
        d.calA = DEFAULT_NTC_PULLUP;
        d.calB = DEFAULT_NTC_BETA;
        d.calC = ADC_REF_VOLTAGE;
        d.emaAlpha = DEFAULT_EMA_ALPHA;
        d.faultBit = 3;  // FAULT_IAT
        d.faultAction = FAULT_ACT_LIMP;
    }
    // Slot 6: VBAT
    {
        SensorDescriptor& d = _desc[SLOT_VBAT];
        d.clear();
        strncpy(d.name, "VBAT", sizeof(d.name));
        strncpy(d.unit, "V", sizeof(d.unit));
        d.sourceType = SRC_GPIO_ADC;
        d.sourcePin = 9;
        d.calType = CAL_VDIVIDER;
        d.calA = 5.7f;  // divider ratio
        d.emaAlpha = DEFAULT_EMA_ALPHA;
        d.faultBit = 4;  // FAULT_VBAT
        d.faultAction = FAULT_ACT_LIMP;
        d.settleGuard = 0.5f;  // Skip validation when voltage < 0.5V (ADC not settled)
    }
    // Slot 7: OIL (disabled by default — configured via configureOilPressure())
    {
        SensorDescriptor& d = _desc[SLOT_OIL];
        d.clear();
        strncpy(d.name, "OIL", sizeof(d.name));
        strncpy(d.unit, "PSI", sizeof(d.unit));
        d.sourceType = SRC_DISABLED;
        d.faultBit = 6;  // FAULT_OIL
        d.faultAction = FAULT_ACT_LIMP;
    }
    // Slots 8-15: spare (already cleared)
}

void SensorManager::initDefaultRules() {
    // Rule 0: MAP range
    {
        FaultRule& r = _rules[0];
        r.clear();
        strncpy(r.name, "MAP_RANGE", sizeof(r.name));
        r.sensorSlot = SLOT_MAP;
        r.op = OP_RANGE;
        r.thresholdA = 5.0f;    // min kPa
        r.thresholdB = 120.0f;  // max kPa
        r.faultBit = 0;         // FAULT_MAP
        r.faultAction = FAULT_ACT_LIMP;
    }
    // Rule 1: TPS range
    {
        FaultRule& r = _rules[1];
        r.clear();
        strncpy(r.name, "TPS_RANGE", sizeof(r.name));
        r.sensorSlot = SLOT_TPS;
        r.op = OP_RANGE;
        r.thresholdA = -5.0f;   // min %
        r.thresholdB = 105.0f;  // max %
        r.faultBit = 1;         // FAULT_TPS
        r.faultAction = FAULT_ACT_LIMP;
    }
    // Rule 2: CLT high
    {
        FaultRule& r = _rules[2];
        r.clear();
        strncpy(r.name, "CLT_HIGH", sizeof(r.name));
        r.sensorSlot = SLOT_CLT;
        r.op = OP_GT;
        r.thresholdA = 280.0f;
        r.faultBit = 2;         // FAULT_CLT
        r.faultAction = FAULT_ACT_LIMP;
    }
    // Rule 3: IAT high
    {
        FaultRule& r = _rules[3];
        r.clear();
        strncpy(r.name, "IAT_HIGH", sizeof(r.name));
        r.sensorSlot = SLOT_IAT;
        r.op = OP_GT;
        r.thresholdA = 200.0f;
        r.faultBit = 3;         // FAULT_IAT
        r.faultAction = FAULT_ACT_LIMP;
    }
    // Rule 4: VBAT low
    {
        FaultRule& r = _rules[4];
        r.clear();
        strncpy(r.name, "VBAT_LOW", sizeof(r.name));
        r.sensorSlot = SLOT_VBAT;
        r.op = OP_LT;
        r.thresholdA = 10.0f;
        r.faultBit = 4;         // FAULT_VBAT
        r.faultAction = FAULT_ACT_LIMP;
    }
    // Rule 5: OIL low (RPM-dependent curve, only when engine running above idle)
    {
        FaultRule& r = _rules[5];
        r.clear();
        strncpy(r.name, "OIL_LOW", sizeof(r.name));
        r.sensorSlot = SLOT_OIL;
        r.op = OP_LT;
        r.thresholdA = 10.0f;   // Static fallback
        r.faultBit = 6;         // FAULT_OIL
        r.faultAction = FAULT_ACT_LIMP;
        r.debounceMs = 3000;
        r.requireRunning = true;
        r.gateRpmMin = 800;     // Only evaluate above idle
        r.curveSource = 0;      // RPM
        float cx[] = {800, 1500, 2500, 4000, 5500, 6500};
        float cy[] = {8.0f, 15.0f, 25.0f, 35.0f, 45.0f, 50.0f};
        memcpy(r.curveX, cx, sizeof(cx));
        memcpy(r.curveY, cy, sizeof(cy));
    }
    // Rules 6-7: spare (already cleared)
}

void SensorManager::begin() {
    // Apply virtual pin overrides from pins page (runs AFTER loadSensorConfig)
    static const uint8_t pinSlots[] = {SLOT_O2_B1, SLOT_O2_B2, SLOT_MAP, SLOT_TPS, SLOT_CLT, SLOT_IAT, SLOT_VBAT};
    for (uint8_t i = 0; i < 7; i++) {
        uint8_t pin = _configuredPins[i];
        if (!isVirtualPin(pin)) continue;
        SensorDescriptor& d = _desc[pinSlots[i]];
        if (pin >= VPIN_ADS1_BASE) {
            d.sourceType = SRC_ADS1115; d.sourceDevice = 1; d.sourceChannel = pin - VPIN_ADS1_BASE;
        } else if (pin >= VPIN_ADS0_BASE) {
            d.sourceType = SRC_ADS1115; d.sourceDevice = 0; d.sourceChannel = pin - VPIN_ADS0_BASE;
        } else {
            d.sourceType = SRC_MCP3204; d.sourceDevice = 0; d.sourceChannel = pin - VPIN_MCP3204_BASE;
        }
        d.sourcePin = 0;  // No GPIO fallback for virtual pins
        Log.info("SENS", "Slot %d (%s) -> virtual pin %d (type=%d dev=%d ch=%d)",
                 pinSlots[i], d.name, pin, d.sourceType, d.sourceDevice, d.sourceChannel);
    }

    // Configure GPIO ADC pins for enabled descriptors (skip virtual/external sources)
    bool extMapTps = hasExternalMapTps();
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        SensorDescriptor& d = _desc[i];
        if (d.sourceType == SRC_GPIO_ADC && d.sourcePin > 0 && d.sourcePin <= 48) {
            // Skip MAP/TPS GPIO setup if external ADC handles them
            if (extMapTps && (i == SLOT_MAP || i == SLOT_TPS)) continue;
            pinMode(d.sourcePin, INPUT);
            analogSetPinAttenuation(d.sourcePin, ADC_11db);
        } else if (d.sourceType == SRC_GPIO_DIGITAL && d.sourcePin > 0 && d.sourcePin <= 48) {
            pinMode(d.sourcePin, INPUT_PULLUP);
        }
    }
    analogSetAttenuation(ADC_11db);
    analogReadResolution(12);

    if (_mapTpsMcp)
        Log.info("SENS", "SensorManager initialized (%d sensors, MAP/TPS via MCP3204 SPI)", getDescriptorCount());
    else if (_mapTpsAds)
        Log.info("SENS", "SensorManager initialized (%d sensors, MAP/TPS via ADS1115)", getDescriptorCount());
    else
        Log.info("SENS", "SensorManager initialized (%d sensors)", getDescriptorCount());
}

void SensorManager::update() {
    // Determine current engine run state for activeStates gating
    uint8_t curState = STATE_OFF;
    if (_engineState) {
        if (_engineState->engineRunning) curState = STATE_RUNNING;
        else if (_engineState->cranking) curState = STATE_CRANKING;
    }

    // Iterate all enabled descriptors: read -> filter -> calibrate
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        SensorDescriptor& d = _desc[i];
        if (d.sourceType == SRC_DISABLED) continue;

        // State-dependent activation gate
        if (!(d.activeStates & curState)) {
            d.value = 0.0f;
            d.inError = false;
            d.inWarning = false;
            continue;
        }

        // CJ125 override for O2 slots
        if (i == SLOT_O2_B1 && _cj125 && _cj125->isReady(0)) {
            d.value = _cj125->getAfr(0);
            continue;
        }
        if (i == SLOT_O2_B2 && _cj125 && _cj125->isReady(1)) {
            d.value = _cj125->getAfr(1);
            continue;
        }

        // Virtual sources: values already in engineering units, skip filter/calibrate
        if (d.sourceType == SRC_ENGINE_STATE || d.sourceType == SRC_OUTPUT_STATE) {
            d.value = readSource(d);
            d.rawVoltage = d.value;
            d.rawFiltered = d.value;
            // Still run validation below via evaluateRules()
            continue;
        }

        float voltage = readSource(d);
        float filtered = applyFilter(d, voltage);
        d.rawVoltage = filtered;
        d.value = calibrate(d, filtered);
    }

    // Evaluate fault rules
    evaluateRules();
}

float SensorManager::readSource(SensorDescriptor& d) {
    switch (d.sourceType) {
        case SRC_GPIO_ADC: {
            if (d.sourcePin == 0) return 0.0f;
            d.rawAdc = analogRead(d.sourcePin);
            return (float)d.rawAdc * ADC_REF_VOLTAGE / (float)ADC_MAX_VALUE;
        }
        case SRC_GPIO_DIGITAL: {
            if (d.sourcePin == 0) return 0.0f;
            d.rawAdc = digitalRead(d.sourcePin);
            return (float)d.rawAdc;
        }
        case SRC_MCP3204: {
            // MCP3204 > ADS1115 > GPIO fallback for MAP/TPS slots
            if (_mapTpsMcp && _mapTpsMcp->isReady()) {
                return _mapTpsMcp->readMillivolts(d.sourceChannel) / 1000.0f;
            }
            if (_mapTpsAds && _mapTpsAds->isReady()) {
                return _mapTpsAds->readMillivolts(d.sourceChannel) / 1000.0f;
            }
            // Fallback to GPIO ADC
            if (d.sourcePin > 0) {
                d.rawAdc = analogRead(d.sourcePin);
                return (float)d.rawAdc * ADC_REF_VOLTAGE / (float)ADC_MAX_VALUE;
            }
            return 0.0f;
        }
        case SRC_ADS1115: {
            ADS1115Reader* ads = nullptr;
            if (d.sourceDevice == 0 && _ads0) {
                ads = _ads0;
            } else if (d.sourceDevice == 1 && _mapTpsAds) {
                ads = _mapTpsAds;
            }
            if (ads && ads->isReady()) {
                return ads->readMillivolts(d.sourceChannel) / 1000.0f;
            }
            return 0.0f;
        }
        case SRC_ENGINE_STATE: {
            if (!_engineState) return 0.0f;
            switch (d.sourceChannel) {
                case 0:  return (float)_engineState->rpm;
                case 1:  return _engineState->mapKpa;
                case 2:  return _engineState->tps;
                case 3:  return _engineState->afr[0];
                case 4:  return _engineState->afr[1];
                case 5:  return _engineState->coolantTempF;
                case 6:  return _engineState->iatTempF;
                case 7:  return _engineState->batteryVoltage;
                case 8:  return _engineState->sparkAdvanceDeg;
                case 9:  return _engineState->injPulseWidthUs;
                case 10: return _engineState->targetAfr;
                case 11: return _engineState->engineRunning ? 1.0f : 0.0f;
                case 12: return _engineState->cranking ? 1.0f : 0.0f;
                case 13: return _engineState->limpMode ? 1.0f : 0.0f;
                case 14: return (float)_engineState->expanderFaults;
                case 15: return _engineState->oilPressurePsi;
                default: return 0.0f;
            }
        }
        case SRC_OUTPUT_STATE: {
            switch (d.sourceChannel) {
                case 0: return _alternator ? _alternator->getDuty() : 0.0f;
                case 1: return _alternator ? (_alternator->isOvervoltage() ? 1.0f : 0.0f) : 0.0f;
                case 2: return _ignition ? (_ignition->isRevLimiting() ? 1.0f : 0.0f) : 0.0f;
                case 3: return _injection ? (_injection->isFuelCut() ? 1.0f : 0.0f) : 0.0f;
                case 4: return _ignition ? _ignition->getDwellMs() : 0.0f;
                case 5: return readCoilHealth();
                case 6: return readInjectorHealth();
                default: return 0.0f;
            }
        }
        default:
            return 0.0f;
    }
}

float SensorManager::applyFilter(SensorDescriptor& d, float rawValue) {
    // Averaging buffer
    if (d.avgSamples > 1) {
        d.avgBuffer[d.avgCount % d.avgSamples] = rawValue;
        d.avgCount++;
        if (d.avgCount >= d.avgSamples) {
            float sum = 0.0f;
            for (uint8_t j = 0; j < d.avgSamples; j++) sum += d.avgBuffer[j];
            rawValue = sum / d.avgSamples;
            d.avgCount = 0;
        } else {
            // Not enough samples yet — use last value
            return d.rawVoltage;
        }
    }

    // EMA filter
    if (d.emaAlpha > 0.0f && d.emaAlpha < 1.0f) {
        d.rawFiltered = d.rawFiltered + d.emaAlpha * (rawValue - d.rawFiltered);
        return d.rawFiltered;
    }

    d.rawFiltered = rawValue;
    return rawValue;
}

float SensorManager::calibrate(const SensorDescriptor& d, float voltage) {
    switch (d.calType) {
        case CAL_LINEAR: {
            float range = d.calB - d.calA;
            if (fabsf(range) < 0.01f) return d.calC;
            float frac = (voltage - d.calA) / range;
            return d.calC + frac * (d.calD - d.calC);
        }
        case CAL_NTC: {
            // NTC thermistor: calA=pullup, calB=beta, calC=refVoltage
            float refV = d.calC > 0.0f ? d.calC : ADC_REF_VOLTAGE;
            if (voltage <= 0.01f || voltage >= (refV - 0.01f)) return -40.0f;
            float rNtc = d.calA * voltage / (refV - voltage);
            float lnR = logf(rNtc / d.calA);
            float invT = (1.0f / 298.15f) + (1.0f / d.calB) * lnR;
            float tempC = (1.0f / invT) - 273.15f;
            return tempC * 9.0f / 5.0f + 32.0f;
        }
        case CAL_VDIVIDER: {
            return voltage * d.calA;
        }
        case CAL_LOOKUP: {
            // Narrowband O2: calA=afrAt0v, calB=afrAt5v, scale 0-3.3V to 0-5V
            float sensorV = voltage * (5.0f / ADC_REF_VOLTAGE);
            float frac = sensorV / 5.0f;
            return d.calA + frac * (d.calB - d.calA);
        }
        case CAL_NONE:
        default:
            return voltage;
    }
}

float SensorManager::readEngineStateChannel(uint8_t channel) const {
    if (!_engineState) return 0.0f;
    switch (channel) {
        case 0:  return (float)_engineState->rpm;
        case 1:  return _engineState->mapKpa;
        case 2:  return _engineState->tps;
        case 3:  return _engineState->afr[0];
        case 4:  return _engineState->afr[1];
        case 5:  return _engineState->coolantTempF;
        case 6:  return _engineState->iatTempF;
        case 7:  return _engineState->batteryVoltage;
        case 8:  return _engineState->sparkAdvanceDeg;
        case 9:  return _engineState->injPulseWidthUs;
        case 10: return _engineState->targetAfr;
        case 11: return _engineState->oilPressurePsi;
        default: return 0.0f;
    }
}

float SensorManager::interpolateCurve(const float* xs, const float* ys, uint8_t n, float x) {
    if (n == 0) return 0.0f;
    if (x <= xs[0]) return ys[0];
    for (uint8_t i = 1; i < n; i++) {
        if (xs[i] <= xs[i - 1]) return ys[i - 1];  // End of valid points
        if (x <= xs[i]) {
            float frac = (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
            return ys[i - 1] + frac * (ys[i] - ys[i - 1]);
        }
    }
    return ys[n - 1];
}

void SensorManager::evaluateRules() {
    uint8_t limpFaults = 0;
    uint8_t celFaults = 0;
    uint32_t now = millis();

    for (uint8_t i = 0; i < MAX_RULES; i++) {
        FaultRule& r = _rules[i];
        if (r.sensorSlot >= MAX_SENSORS) continue;
        if (r.faultBit == 0xFF) continue;

        const SensorDescriptor& d = _desc[r.sensorSlot];
        if (d.sourceType == SRC_DISABLED) continue;

        // Skip if rule requires engine running and it's not
        if (r.requireRunning && !_engineRunning) {
            r.debounceStart = 0;
            r.active = false;
            continue;
        }

        // Gate checks
        if (_engineState) {
            float rpm = (float)_engineState->rpm;
            float mapKpa = _engineState->mapKpa;
            if (r.gateRpmMin > 0 && rpm < r.gateRpmMin) { r.active = false; r.debounceStart = 0; continue; }
            if (r.gateRpmMax > 0 && rpm > r.gateRpmMax) { r.active = false; r.debounceStart = 0; continue; }
            if (!isnan(r.gateMapMin) && mapKpa < r.gateMapMin) { r.active = false; r.debounceStart = 0; continue; }
            if (!isnan(r.gateMapMax) && mapKpa > r.gateMapMax) { r.active = false; r.debounceStart = 0; continue; }
        }

        // Check settle guard on the sensor
        if (d.settleGuard > 0.0f && fabsf(d.value) < d.settleGuard) {
            r.debounceStart = 0;
            r.active = false;
            continue;
        }

        // Dynamic threshold: use curve if enabled, otherwise static thresholdA
        float effectiveThreshA = r.thresholdA;
        if (r.curveSource != 0xFF && _engineState) {
            float x = readEngineStateChannel(r.curveSource);
            effectiveThreshA = interpolateCurve(r.curveX, r.curveY, CURVE_POINTS, x);
        }

        bool triggered = false;
        switch (r.op) {
            case OP_LT:
                triggered = (d.value < effectiveThreshA);
                break;
            case OP_GT:
                triggered = (d.value > effectiveThreshA);
                break;
            case OP_RANGE:
                triggered = (d.value < effectiveThreshA || d.value > r.thresholdB);
                break;
            case OP_DELTA: {
                if (r.sensorSlotB < MAX_SENSORS) {
                    const SensorDescriptor& db = _desc[r.sensorSlotB];
                    if (db.sourceType != SRC_DISABLED) {
                        triggered = (fabsf(d.value - db.value) > effectiveThreshA);
                    }
                }
                break;
            }
        }

        if (triggered) {
            if (r.debounceMs > 0) {
                if (r.debounceStart == 0) {
                    r.debounceStart = now;
                } else if (now - r.debounceStart >= r.debounceMs) {
                    r.active = true;
                    uint8_t bit = (1 << r.faultBit);
                    if (r.faultAction == FAULT_ACT_LIMP || r.faultAction == FAULT_ACT_SHUTDOWN) {
                        limpFaults |= bit;
                    } else if (r.faultAction == FAULT_ACT_CEL) {
                        celFaults |= bit;
                    }
                }
            } else {
                r.active = true;
                uint8_t bit = (1 << r.faultBit);
                if (r.faultAction == FAULT_ACT_LIMP || r.faultAction == FAULT_ACT_SHUTDOWN) {
                    limpFaults |= bit;
                } else if (r.faultAction == FAULT_ACT_CEL) {
                    celFaults |= bit;
                }
            }
        } else {
            r.debounceStart = 0;
            r.active = false;
        }
    }

    _limpFaults = limpFaults;
    _celFaults = celFaults;
}

float SensorManager::getO2Afr(uint8_t bank) const {
    if (bank == 0) return _desc[SLOT_O2_B1].value;
    if (bank == 1) return _desc[SLOT_O2_B2].value;
    return 14.7f;
}

uint16_t SensorManager::getRawAdc(uint8_t channel) const {
    return (channel < MAX_SENSORS) ? _desc[channel].rawAdc : 0;
}

uint8_t SensorManager::getDescriptorCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_SENSORS; i++)
        if (_desc[i].sourceType != SRC_DISABLED) count++;
    return count;
}

uint8_t SensorManager::getRuleCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_RULES; i++)
        if (_rules[i].sensorSlot < MAX_SENSORS && _rules[i].faultBit != 0xFF) count++;
    return count;
}

uint8_t SensorManager::getPin(uint8_t channel) const {
    // Return configured pin (may be virtual 250+) for first 7 slots, else descriptor pin
    if (channel < 7 && _configuredPins[channel] != 0) return _configuredPins[channel];
    return channel < MAX_SENSORS ? _desc[channel].sourcePin : 0;
}

void SensorManager::setMapCalibration(float vMin, float vMax, float pMin, float pMax) {
    _desc[SLOT_MAP].calA = vMin;
    _desc[SLOT_MAP].calB = vMax;
    _desc[SLOT_MAP].calC = pMin;
    _desc[SLOT_MAP].calD = pMax;
}

void SensorManager::setO2Calibration(float afrAt0v, float afrAt5v) {
    _desc[SLOT_O2_B1].calA = afrAt0v;
    _desc[SLOT_O2_B1].calB = afrAt5v;
    _desc[SLOT_O2_B2].calA = afrAt0v;
    _desc[SLOT_O2_B2].calB = afrAt5v;
}

void SensorManager::setVbatDividerRatio(float ratio) {
    _desc[SLOT_VBAT].calA = ratio;
}

void SensorManager::setPins(uint8_t o2b1, uint8_t o2b2, uint8_t map, uint8_t tps,
                             uint8_t clt, uint8_t iat, uint8_t vbat) {
    _configuredPins[0] = o2b1; _configuredPins[1] = o2b2;
    _configuredPins[2] = map;  _configuredPins[3] = tps;
    _configuredPins[4] = clt;  _configuredPins[5] = iat;
    _configuredPins[6] = vbat;
    _desc[SLOT_O2_B1].sourcePin = o2b1;
    _desc[SLOT_O2_B2].sourcePin = o2b2;
    _desc[SLOT_MAP].sourcePin = map;
    _desc[SLOT_TPS].sourcePin = tps;
    _desc[SLOT_CLT].sourcePin = clt;
    _desc[SLOT_IAT].sourcePin = iat;
    _desc[SLOT_VBAT].sourcePin = vbat;
}

void SensorManager::setLimpThresholds(float mapMin, float mapMax, float tpsMin, float tpsMax,
                                       float cltMax, float iatMax, float vbatMin) {
    // Update rule thresholds to match old behavior
    for (uint8_t i = 0; i < MAX_RULES; i++) {
        FaultRule& r = _rules[i];
        if (r.sensorSlot == SLOT_MAP && r.op == OP_RANGE) {
            r.thresholdA = mapMin;
            r.thresholdB = mapMax;
        } else if (r.sensorSlot == SLOT_TPS && r.op == OP_RANGE) {
            r.thresholdA = tpsMin;
            r.thresholdB = tpsMax;
        } else if (r.sensorSlot == SLOT_CLT && r.op == OP_GT) {
            r.thresholdA = cltMax;
        } else if (r.sensorSlot == SLOT_IAT && r.op == OP_GT) {
            r.thresholdA = iatMax;
        } else if (r.sensorSlot == SLOT_VBAT && r.op == OP_LT) {
            r.thresholdA = vbatMin;
        }
    }
}

void SensorManager::configureOilPressure(uint8_t mode, uint8_t pin, bool activeLow,
                                          float minPsi, float maxPsi, uint8_t mcpChannel) {
    SensorDescriptor& d = _desc[SLOT_OIL];
    if (mode == 0) {
        d.sourceType = SRC_DISABLED;
        return;
    }
    if (mode == 1) {
        // Digital switch
        d.sourceType = SRC_GPIO_DIGITAL;
        d.sourcePin = pin;
        d.calType = CAL_NONE;
        d.calA = activeLow ? 1.0f : 0.0f;  // Store activeLow flag in calA
        d.calD = maxPsi;  // Store maxPsi for digital (reports 0 or maxPsi)
    } else if (mode == 2) {
        // Analog sender
        d.sourceType = SRC_MCP3204;
        d.sourceDevice = 0;
        d.sourceChannel = mcpChannel;
        d.sourcePin = pin;  // GPIO fallback
        d.calType = CAL_LINEAR;
        d.calA = 0.5f;    // 0.5V = 0 PSI
        d.calB = 4.5f;    // 4.5V = maxPsi
        d.calC = 0.0f;
        d.calD = maxPsi;
        d.emaAlpha = DEFAULT_EMA_ALPHA;
        d.avgSamples = 4;
    }
    d.faultBit = 6;  // FAULT_OIL
    d.faultAction = FAULT_ACT_LIMP;

    // Update the OIL_LOW rule threshold
    for (uint8_t i = 0; i < MAX_RULES; i++) {
        if (_rules[i].sensorSlot == SLOT_OIL) {
            _rules[i].thresholdA = minPsi;
            break;
        }
    }
}

float SensorManager::readCoilHealth() {
    PinExpander& exp = PinExpander::instance();
    if (!exp.isReady(4)) return 0.0f;  // MCP23S17 #4 = coils
    uint16_t actual = exp.readAll(4);
    uint16_t shadow = exp.getShadow(4);
    uint16_t dir = exp.getDir(4);
    // Only check output pins (dir bit = 0 means output)
    uint16_t outputMask = ~dir;
    uint16_t mismatch = (actual ^ shadow) & outputMask;
    uint8_t count = 0;
    while (mismatch) { count += mismatch & 1; mismatch >>= 1; }
    return (float)count;
}

float SensorManager::readInjectorHealth() {
    PinExpander& exp = PinExpander::instance();
    if (!exp.isReady(5)) return 0.0f;  // MCP23S17 #5 = injectors
    uint16_t actual = exp.readAll(5);
    uint16_t shadow = exp.getShadow(5);
    uint16_t dir = exp.getDir(5);
    uint16_t outputMask = ~dir;
    uint16_t mismatch = (actual ^ shadow) & outputMask;
    uint8_t count = 0;
    while (mismatch) { count += mismatch & 1; mismatch >>= 1; }
    return (float)count;
}
