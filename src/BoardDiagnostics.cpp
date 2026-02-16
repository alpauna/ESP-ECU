#include "BoardDiagnostics.h"
#include "ADS1115Reader.h"
#include "PinExpander.h"
#include "Logger.h"
#include <math.h>

BoardDiagnostics::BoardDiagnostics()
    : _diagAds(nullptr), _ready(false), _enabled(false),
      _phase(DIAG_PHASE_IDLE), _currentChannel(0),
      _burstChannel(0xFF), _burstRemaining(0),
      _muxSettleUs(0), _convStartMs(0),
      _scanStartMs(0), _lastScanCycleMs(0),
      _muxEnPin(0) {
    memset(_muxSelPins, 0, sizeof(_muxSelPins));
    for (uint8_t i = 0; i < DIAG_CHANNEL_COUNT; i++)
        _channels[i].clear();
}

bool BoardDiagnostics::begin(uint8_t diagAdsAddr, const uint16_t muxSelPins[4], uint16_t muxEnPin) {
    _diagAds = new ADS1115Reader();
    if (!_diagAds->begin(diagAdsAddr)) {
        Log.warn("DIAG", "ADS1115 not found at 0x%02X — diagnostics disabled", diagAdsAddr);
        delete _diagAds;
        _diagAds = nullptr;
        return false;
    }

    for (uint8_t i = 0; i < 4; i++) {
        _muxSelPins[i] = muxSelPins[i];
        xPinMode(_muxSelPins[i], OUTPUT);
        xDigitalWrite(_muxSelPins[i], LOW);
    }
    _muxEnPin = muxEnPin;
    xPinMode(_muxEnPin, OUTPUT);
    enableMux(false);

    initDefaultChannels();

    _ready = true;
    _enabled = true;
    _scanStartMs = millis();

    Log.info("DIAG", "Board diagnostics initialized: ADS1115@0x%02X, mux S0-S3=%d-%d-%d-%d, EN=%d",
             diagAdsAddr, _muxSelPins[0], _muxSelPins[1], _muxSelPins[2], _muxSelPins[3], _muxEnPin);
    return true;
}

void BoardDiagnostics::initDefaultChannels() {
    // CH0: 3.3V rail — direct connection
    auto& ch0 = _channels[0];
    ch0.clear();
    strncpy(ch0.name, "3V3_RAIL", sizeof(ch0.name));
    strncpy(ch0.unit, "V", sizeof(ch0.unit));
    ch0.scaleFactor = 1.0f / 1000.0f;  // mV -> V
    ch0.expectedMin = 3.1f;
    ch0.expectedMax = 3.5f;

    // CH1: 5V rail — voltage divider 15k/10k (ratio 2.5)
    auto& ch1 = _channels[1];
    ch1.clear();
    strncpy(ch1.name, "5V_RAIL", sizeof(ch1.name));
    strncpy(ch1.unit, "V", sizeof(ch1.unit));
    ch1.scaleFactor = 2.5f / 1000.0f;
    ch1.expectedMin = 4.75f;
    ch1.expectedMax = 5.25f;

    // CH2: 12V battery — voltage divider 47k/10k (ratio 5.7)
    auto& ch2 = _channels[2];
    ch2.clear();
    strncpy(ch2.name, "12V_BATT", sizeof(ch2.name));
    strncpy(ch2.unit, "V", sizeof(ch2.unit));
    ch2.scaleFactor = 5.7f / 1000.0f;
    ch2.expectedMin = 11.5f;
    ch2.expectedMax = 15.0f;

    // CH3: 5V VCCB post-TXB0108
    auto& ch3 = _channels[3];
    ch3.clear();
    strncpy(ch3.name, "5V_VCCB", sizeof(ch3.name));
    strncpy(ch3.unit, "V", sizeof(ch3.unit));
    ch3.scaleFactor = 2.5f / 1000.0f;
    ch3.expectedMin = 4.5f;
    ch3.expectedMax = 5.5f;

    // CH4: Coil 1 output — RC averaged, divider ~5.7:1
    auto& ch4 = _channels[4];
    ch4.clear();
    strncpy(ch4.name, "COIL1_OUT", sizeof(ch4.name));
    strncpy(ch4.unit, "V", sizeof(ch4.unit));
    ch4.scaleFactor = 5.7f / 1000.0f;
    ch4.expectedMin = NAN;  // context-dependent
    ch4.expectedMax = NAN;

    // CH5: Injector 1 output — RC averaged, divider ~5.7:1
    auto& ch5 = _channels[5];
    ch5.clear();
    strncpy(ch5.name, "INJ1_OUT", sizeof(ch5.name));
    strncpy(ch5.unit, "V", sizeof(ch5.unit));
    ch5.scaleFactor = 5.7f / 1000.0f;
    ch5.expectedMin = NAN;
    ch5.expectedMax = NAN;

    // CH6: Alternator field PWM — RC averaged
    auto& ch6 = _channels[6];
    ch6.clear();
    strncpy(ch6.name, "ALT_FIELD", sizeof(ch6.name));
    strncpy(ch6.unit, "V", sizeof(ch6.unit));
    ch6.scaleFactor = 1.0f / 1000.0f;
    ch6.expectedMin = NAN;
    ch6.expectedMax = NAN;

    // CH7: Fuel pump relay drive — divider 5.7:1
    auto& ch7 = _channels[7];
    ch7.clear();
    strncpy(ch7.name, "FUELPUMP", sizeof(ch7.name));
    strncpy(ch7.unit, "V", sizeof(ch7.unit));
    ch7.scaleFactor = 5.7f / 1000.0f;
    ch7.expectedMin = NAN;
    ch7.expectedMax = NAN;

    // CH8: TXB0108 OE — 3.3V domain direct
    auto& ch8 = _channels[8];
    ch8.clear();
    strncpy(ch8.name, "TXB_OE", sizeof(ch8.name));
    strncpy(ch8.unit, "V", sizeof(ch8.unit));
    ch8.scaleFactor = 1.0f / 1000.0f;
    ch8.expectedMin = 2.8f;
    ch8.expectedMax = 3.5f;

    // CH9: MCP23S17 RESET# — 5V domain, divider 15k/10k
    auto& ch9 = _channels[9];
    ch9.clear();
    strncpy(ch9.name, "MCP_RESET", sizeof(ch9.name));
    strncpy(ch9.unit, "V", sizeof(ch9.unit));
    ch9.scaleFactor = 2.5f / 1000.0f;
    ch9.expectedMin = 4.0f;
    ch9.expectedMax = 5.5f;

    // CH10: PERIPH_EN GPIO — 3.3V domain
    auto& ch10 = _channels[10];
    ch10.clear();
    strncpy(ch10.name, "PERIPH_EN", sizeof(ch10.name));
    strncpy(ch10.unit, "V", sizeof(ch10.unit));
    ch10.scaleFactor = 1.0f / 1000.0f;
    ch10.expectedMin = NAN;
    ch10.expectedMax = NAN;

    // CH11: Board temperature NTC — 10k pullup, 10k NTC @ 25C
    auto& ch11 = _channels[11];
    ch11.clear();
    strncpy(ch11.name, "BOARD_TEMP", sizeof(ch11.name));
    strncpy(ch11.unit, "C", sizeof(ch11.unit));
    ch11.scaleFactor = 1.0f;  // special handling in processReading
    ch11.expectedMin = -10.0f;
    ch11.expectedMax = 85.0f;

    // CH12: CJ125 heater 1 sample — divider ~11:1
    auto& ch12 = _channels[12];
    ch12.clear();
    strncpy(ch12.name, "HEATER1", sizeof(ch12.name));
    strncpy(ch12.unit, "V", sizeof(ch12.unit));
    ch12.scaleFactor = 11.0f / 1000.0f;
    ch12.expectedMin = NAN;
    ch12.expectedMax = NAN;

    // CH13: CJ125 heater 2 sample
    auto& ch13 = _channels[13];
    ch13.clear();
    strncpy(ch13.name, "HEATER2", sizeof(ch13.name));
    strncpy(ch13.unit, "V", sizeof(ch13.unit));
    ch13.scaleFactor = 11.0f / 1000.0f;
    ch13.expectedMin = NAN;
    ch13.expectedMax = NAN;

    // CH14: Board current — shunt 0.1R, gain 20, I = V / (0.1 * 20) = V / 2
    auto& ch14 = _channels[14];
    ch14.clear();
    strncpy(ch14.name, "BOARD_CUR", sizeof(ch14.name));
    strncpy(ch14.unit, "mA", sizeof(ch14.unit));
    ch14.scaleFactor = 0.5f;  // mV / 2 = mA (0.1R shunt, gain 20)
    ch14.expectedMin = NAN;
    ch14.expectedMax = NAN;

    // CH15: Spare
    auto& ch15 = _channels[15];
    ch15.clear();
    strncpy(ch15.name, "SPARE", sizeof(ch15.name));
    strncpy(ch15.unit, "", sizeof(ch15.unit));
}

// ---------- Mux control ----------

void BoardDiagnostics::selectMuxChannel(uint8_t ch) {
    for (uint8_t i = 0; i < 4; i++)
        xDigitalWrite(_muxSelPins[i], (ch >> i) & 1);
}

void BoardDiagnostics::enableMux(bool en) {
    xDigitalWrite(_muxEnPin, en ? LOW : HIGH);  // CD74HC4067 EN is active LOW
}

void BoardDiagnostics::advanceChannel() {
    _currentChannel++;
    if (_currentChannel >= DIAG_CHANNEL_COUNT) {
        _currentChannel = 0;
        _lastScanCycleMs = millis() - _scanStartMs;
        _scanStartMs = millis();
    }
}

// ---------- State machine ----------

bool BoardDiagnostics::update(uint32_t budgetUs) {
    if (!_ready || !_enabled) return false;

    switch (_phase) {
    case DIAG_PHASE_IDLE: {
        if (budgetUs < 200) return false;
        uint8_t ch = (_burstChannel != 0xFF) ? _burstChannel : _currentChannel;
        selectMuxChannel(ch);
        enableMux(true);
        _muxSettleUs = micros();
        _phase = DIAG_PHASE_SELECT_MUX;
        return true;
    }

    case DIAG_PHASE_SELECT_MUX: {
        if (micros() - _muxSettleUs < MUX_SETTLE_US) return false;
        _phase = DIAG_PHASE_START_CONV;
        return true;
    }

    case DIAG_PHASE_START_CONV: {
        _diagAds->startReading(0);  // mux output -> ADS1115 AIN0
        _convStartMs = millis();
        _phase = DIAG_PHASE_WAIT_CONV;
        return true;
    }

    case DIAG_PHASE_WAIT_CONV: {
        if (!_diagAds->conversionComplete()) {
            if (millis() - _convStartMs > CONV_TIMEOUT_MS) {
                Log.warn("DIAG", "ADS1115 timeout on CH%d", _currentChannel);
                enableMux(false);
                _phase = DIAG_PHASE_IDLE;
                if (_burstChannel == 0xFF) advanceChannel();
            }
            return false;
        }
        _phase = DIAG_PHASE_READ_RESULT;
        return true;
    }

    case DIAG_PHASE_READ_RESULT: {
        float mv = _diagAds->getLastResultMillivolts();
        enableMux(false);

        if (_burstChannel != 0xFF) {
            // Burst investigation mode
            DiagChannel& c = _channels[_burstChannel];
            c.burstSamples[c.burstIndex++] = mv;
            _burstRemaining--;
            if (_burstRemaining == 0) {
                c.burstComplete = true;
                processBurstResult(_burstChannel);
                _burstChannel = 0xFF;
            }
        } else {
            processReading(_currentChannel, mv);
            evaluateFault(_currentChannel);
            advanceChannel();
        }
        _phase = DIAG_PHASE_IDLE;
        return true;
    }
    }
    return false;
}

// ---------- Reading processing ----------

void BoardDiagnostics::processReading(uint8_t ch, float millivolts) {
    DiagChannel& c = _channels[ch];
    c.lastMillivolts = millivolts;

    if (ch == 11) {
        // NTC thermistor: 10k pullup to 3.3V, NTC to GND
        // R_ntc = R_pullup * V_adc / (V_ref - V_adc)
        float vRef = 3300.0f;  // mV
        if (millivolts > 0.0f && millivolts < vRef) {
            float rNtc = 10000.0f * millivolts / (vRef - millivolts);
            // Simplified Beta equation: T = 1 / (1/T0 + (1/B) * ln(R/R0)) - 273.15
            float beta = 3950.0f;
            float t0 = 298.15f;  // 25C in Kelvin
            float r0 = 10000.0f;
            float tKelvin = 1.0f / (1.0f / t0 + (1.0f / beta) * logf(rNtc / r0));
            c.lastValue = tKelvin - 273.15f;
        } else {
            c.lastValue = NAN;
        }
    } else {
        c.lastValue = millivolts * c.scaleFactor + c.scaleOffset;
    }
    c.lastReadMs = millis();
}

// ---------- Fault detection ----------

void BoardDiagnostics::evaluateFault(uint8_t ch) {
    DiagChannel& c = _channels[ch];
    if (isnan(c.expectedMin) && isnan(c.expectedMax)) return;  // no thresholds configured

    bool outOfRange = false;
    if (!isnan(c.expectedMin) && c.lastValue < c.expectedMin) outOfRange = true;
    if (!isnan(c.expectedMax) && c.lastValue > c.expectedMax) outOfRange = true;

    if (outOfRange) {
        c.okCounter = 0;
        if (c.faultCounter < 255) c.faultCounter++;
        if (c.faultCounter >= c.debounceCount && c.faultState == DIAG_OK) {
            c.faultState = DIAG_WARNING;
            startBurstInvestigation(ch);
        }
    } else {
        c.faultCounter = 0;
        if (c.okCounter < 255) c.okCounter++;
        if (c.okCounter >= RECOVERY_COUNT && c.faultState != DIAG_OK) {
            DiagFaultState prev = c.faultState;
            c.faultState = DIAG_OK;
            if (prev == DIAG_FAULT && _faultCb) {
                char msg[64];
                snprintf(msg, sizeof(msg), "%s recovered: %.2f%s", c.name, c.lastValue, c.unit);
                _faultCb("DIAG", msg, false);
            }
            Log.info("DIAG", "%s recovered: %.2f%s", c.name, c.lastValue, c.unit);
        }
    }
}

void BoardDiagnostics::startBurstInvestigation(uint8_t ch) {
    _burstChannel = ch;
    _burstRemaining = DIAG_BURST_SAMPLES;
    _channels[ch].burstIndex = 0;
    _channels[ch].burstComplete = false;
    _channels[ch].faultState = DIAG_INVESTIGATING;
    Log.warn("DIAG", "Investigating %s: last=%.2f%s (range %.2f-%.2f)",
             _channels[ch].name, _channels[ch].lastValue, _channels[ch].unit,
             _channels[ch].expectedMin, _channels[ch].expectedMax);
}

void BoardDiagnostics::processBurstResult(uint8_t ch) {
    DiagChannel& c = _channels[ch];
    float sum = 0, minV = 1e9f, maxV = -1e9f;

    for (uint8_t i = 0; i < DIAG_BURST_SAMPLES; i++) {
        float v;
        if (ch == 11) {
            // NTC conversion for burst samples
            float vRef = 3300.0f;
            float mv = c.burstSamples[i];
            if (mv > 0.0f && mv < vRef) {
                float rNtc = 10000.0f * mv / (vRef - mv);
                float tK = 1.0f / (1.0f / 298.15f + (1.0f / 3950.0f) * logf(rNtc / 10000.0f));
                v = tK - 273.15f;
            } else {
                v = NAN;
            }
        } else {
            v = c.burstSamples[i] * c.scaleFactor + c.scaleOffset;
        }
        if (isnan(v)) continue;
        sum += v;
        if (v < minV) minV = v;
        if (v > maxV) maxV = v;
    }

    float mean = sum / DIAG_BURST_SAMPLES;
    float variance = 0;
    for (uint8_t i = 0; i < DIAG_BURST_SAMPLES; i++) {
        float v;
        if (ch == 11) {
            float vRef = 3300.0f;
            float mv = c.burstSamples[i];
            if (mv > 0.0f && mv < vRef) {
                float rNtc = 10000.0f * mv / (vRef - mv);
                float tK = 1.0f / (1.0f / 298.15f + (1.0f / 3950.0f) * logf(rNtc / 10000.0f));
                v = tK - 273.15f;
            } else {
                continue;
            }
        } else {
            v = c.burstSamples[i] * c.scaleFactor + c.scaleOffset;
        }
        variance += (v - mean) * (v - mean);
    }
    float stddev = sqrtf(variance / DIAG_BURST_SAMPLES);

    bool allOutOfRange = true;
    if (!isnan(c.expectedMin) && minV >= c.expectedMin) allOutOfRange = false;
    if (!isnan(c.expectedMax) && maxV <= c.expectedMax) allOutOfRange = false;

    if (allOutOfRange) {
        c.faultState = DIAG_FAULT;
        c.totalFaults++;
        Log.error("DIAG", "%s FAULT: mean=%.2f range=[%.2f,%.2f] sd=%.3f",
                  c.name, mean, minV, maxV, stddev);
        if (_faultCb) {
            char msg[80];
            snprintf(msg, sizeof(msg), "%s fault: %.2f%s (mean=%.2f sd=%.3f)",
                     c.name, c.lastValue, c.unit, mean, stddev);
            _faultCb("DIAG", msg, true);
        }
    } else if (stddev > fabsf(c.expectedMax - c.expectedMin) * 0.1f) {
        c.faultState = DIAG_WARNING;
        Log.warn("DIAG", "%s intermittent: mean=%.2f sd=%.3f", c.name, mean, stddev);
    } else {
        c.faultState = DIAG_OK;
        c.faultCounter = 0;
        Log.info("DIAG", "%s transient cleared: burst mean=%.2f", c.name, mean);
    }
}

// ---------- Accessors ----------

uint8_t BoardDiagnostics::getFaultCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < DIAG_CHANNEL_COUNT; i++) {
        if (_channels[i].faultState == DIAG_FAULT || _channels[i].faultState == DIAG_WARNING)
            count++;
    }
    return count;
}

uint16_t BoardDiagnostics::getFaultBitmask() const {
    uint16_t mask = 0;
    for (uint8_t i = 0; i < DIAG_CHANNEL_COUNT; i++) {
        if (_channels[i].faultState == DIAG_FAULT)
            mask |= (1 << i);
    }
    return mask;
}

bool BoardDiagnostics::hasAnyFault() const {
    for (uint8_t i = 0; i < DIAG_CHANNEL_COUNT; i++) {
        if (_channels[i].faultState == DIAG_FAULT) return true;
    }
    return false;
}

uint8_t BoardDiagnostics::getHealthScore() const {
    uint8_t faults = 0, warnings = 0, configured = 0;
    for (uint8_t i = 0; i < DIAG_CHANNEL_COUNT; i++) {
        if (isnan(_channels[i].expectedMin) && isnan(_channels[i].expectedMax)) continue;
        configured++;
        if (_channels[i].faultState == DIAG_FAULT) faults++;
        else if (_channels[i].faultState == DIAG_WARNING) warnings++;
    }
    if (configured == 0) return 100;
    uint8_t penalty = faults * 15 + warnings * 5;
    return (penalty >= 100) ? 0 : (100 - penalty);
}

bool BoardDiagnostics::crossValidatePowerRails() const {
    const DiagChannel& v33 = _channels[0];
    const DiagChannel& v5  = _channels[1];
    const DiagChannel& v12 = _channels[2];
    const DiagChannel& vccb = _channels[3];

    if (v33.lastReadMs == 0 || v5.lastReadMs == 0 || v12.lastReadMs == 0) return true;

    // If 12V is low, derived rails should also be low — not a fault in individual regulators
    if (v12.lastValue < 11.0f && v5.lastValue < 4.5f && v33.lastValue < 3.0f)
        return true;  // consistent: battery problem

    // 5V normal but VCCB significantly different — TXB0108 supply issue
    if (v5.lastValue > 4.5f && v5.lastValue < 5.5f &&
        vccb.lastReadMs > 0 && fabsf(v5.lastValue - vccb.lastValue) > 1.0f)
        return false;

    // 5V normal but 3.3V out — 3.3V regulator issue
    if (v5.lastValue > 4.5f && v33.lastValue < 2.8f)
        return false;

    // 12V normal but 5V out — 5V regulator issue
    if (v12.lastValue > 11.0f && v5.lastValue < 4.0f)
        return false;

    return true;
}
