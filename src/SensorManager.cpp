#include "SensorManager.h"
#include "CJ125Controller.h"
#include "ADS1115Reader.h"
#include <esp_adc_cal.h>
#include "Logger.h"

SensorManager::SensorManager()
    : _mapKpa(0), _tpsPercent(0), _coolantTempF(0), _iatTempF(0), _batteryVoltage(0) {
    // Default pin assignments
    _channelPins[0] = 3;  // O2 Bank 1
    _channelPins[1] = 4;  // O2 Bank 2
    _channelPins[2] = 5;  // MAP
    _channelPins[3] = 6;  // TPS
    _channelPins[4] = 7;  // CLT
    _channelPins[5] = 8;  // IAT
    _channelPins[6] = 9;  // VBAT
    memset(_rawFiltered, 0, sizeof(_rawFiltered));
    memset(_rawAdc, 0, sizeof(_rawAdc));
    _o2Afr[0] = 14.7f;
    _o2Afr[1] = 14.7f;
    // Default calibration
    _cal.mapVMin = 0.5f;
    _cal.mapVMax = 4.5f;
    _cal.mapPMin = 10.0f;
    _cal.mapPMax = 105.0f;
    _cal.o2AfrAt0v = 10.0f;
    _cal.o2AfrAt5v = 20.0f;
    _cal.tpsVClosed = 0.5f;
    _cal.tpsVOpen = 4.5f;
    _cal.vbatDividerRatio = 5.7f;  // Typical 47k/10k divider
    _cal.ntcPullupOhms = NTC_PULLUP_OHMS;
    _cal.ntcBeta = NTC_BETA;
    _cal.emaAlpha = DEFAULT_EMA_ALPHA;
}

SensorManager::~SensorManager() {}

void SensorManager::begin() {
    // Configure ADC1 pins for analog read (skip MAP/TPS if using external ADS1115)
    for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
        if (_mapTpsAds && (i == 2 || i == 3))  // index 2=MAP, 3=TPS
            continue;
        pinMode(_channelPins[i], INPUT);
        analogSetPinAttenuation(_channelPins[i], ADC_11db);
    }
    analogSetAttenuation(ADC_11db);
    analogReadResolution(12);
    if (_mapTpsAds)
        Log.info("SENS", "SensorManager initialized (%d channels, MAP/TPS via ADS1115)", NUM_CHANNELS);
    else
        Log.info("SENS", "SensorManager initialized (%d channels)", NUM_CHANNELS);
}

void SensorManager::update() {
    // Read ADC channels and apply EMA filter (skip MAP/TPS if using ADS1115)
    for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
        if (_mapTpsAds && (i == 2 || i == 3))  // index 2=MAP, 3=TPS
            continue;
        _rawAdc[i] = analogRead(_channelPins[i]);
        float raw = (float)_rawAdc[i];
        _rawFiltered[i] = _rawFiltered[i] + _cal.emaAlpha * (raw - _rawFiltered[i]);
    }

    // Convert filtered ADC values to engineering units
    float vO2_1 = adcToVoltage((uint16_t)_rawFiltered[0]);
    float vO2_2 = adcToVoltage((uint16_t)_rawFiltered[1]);
    float vClt  = adcToVoltage((uint16_t)_rawFiltered[4]);
    float vIat  = adcToVoltage((uint16_t)_rawFiltered[5]);
    float vBat  = adcToVoltage((uint16_t)_rawFiltered[6]);

    // MAP/TPS: read from second ADS1115 (CH0=MAP, CH1=TPS) if available, else native ADC
    float vMap, vTps;
    if (_mapTpsAds && _mapTpsAds->isReady()) {
        // ADS1115 at GAIN_TWOTHIRDS returns actual voltage (0-6.144V range)
        vMap = _mapTpsAds->readMillivolts(0) / 1000.0f;
        vTps = _mapTpsAds->readMillivolts(1) / 1000.0f;
    } else {
        vMap = adcToVoltage((uint16_t)_rawFiltered[2]);
        vTps = adcToVoltage((uint16_t)_rawFiltered[3]);
    }

    if (_cj125 && _cj125->isReady(0))
        _o2Afr[0] = _cj125->getAfr(0);
    else
        _o2Afr[0] = voltageToAfr(vO2_1);

    if (_cj125 && _cj125->isReady(1))
        _o2Afr[1] = _cj125->getAfr(1);
    else
        _o2Afr[1] = voltageToAfr(vO2_2);
    _mapKpa          = voltageToMapKpa(vMap);
    _tpsPercent      = voltageToTpsPercent(vTps);
    _coolantTempF    = voltageToNtcTempF(vClt);
    _iatTempF        = voltageToNtcTempF(vIat);
    _batteryVoltage  = voltageToBatteryV(vBat);
}

float SensorManager::getO2Afr(uint8_t bank) const {
    return (bank < 2) ? _o2Afr[bank] : 14.7f;
}

uint16_t SensorManager::getRawAdc(uint8_t channel) const {
    return (channel < NUM_CHANNELS) ? _rawAdc[channel] : 0;
}

void SensorManager::setMapCalibration(float vMin, float vMax, float pMin, float pMax) {
    _cal.mapVMin = vMin;
    _cal.mapVMax = vMax;
    _cal.mapPMin = pMin;
    _cal.mapPMax = pMax;
}

void SensorManager::setO2Calibration(float afrAt0v, float afrAt5v) {
    _cal.o2AfrAt0v = afrAt0v;
    _cal.o2AfrAt5v = afrAt5v;
}

void SensorManager::setVbatDividerRatio(float ratio) {
    _cal.vbatDividerRatio = ratio;
}

void SensorManager::setPins(uint8_t o2b1, uint8_t o2b2, uint8_t map, uint8_t tps,
                             uint8_t clt, uint8_t iat, uint8_t vbat) {
    _channelPins[0] = o2b1;
    _channelPins[1] = o2b2;
    _channelPins[2] = map;
    _channelPins[3] = tps;
    _channelPins[4] = clt;
    _channelPins[5] = iat;
    _channelPins[6] = vbat;
}

float SensorManager::adcToVoltage(uint16_t raw) const {
    return (float)raw * ADC_REF_VOLTAGE / (float)ADC_MAX_VALUE;
}

float SensorManager::voltageToMapKpa(float voltage) const {
    float range = _cal.mapVMax - _cal.mapVMin;
    if (range < 0.01f) return _cal.mapPMin;
    float frac = (voltage - _cal.mapVMin) / range;
    return _cal.mapPMin + frac * (_cal.mapPMax - _cal.mapPMin);
}

float SensorManager::voltageToAfr(float voltage) const {
    // Scale 0-3.3V ADC to 0-5V sensor range
    float sensorV = voltage * (5.0f / ADC_REF_VOLTAGE);
    float frac = sensorV / 5.0f;
    return _cal.o2AfrAt0v + frac * (_cal.o2AfrAt5v - _cal.o2AfrAt0v);
}

float SensorManager::voltageToTpsPercent(float voltage) const {
    float range = _cal.tpsVOpen - _cal.tpsVClosed;
    if (range < 0.01f) return 0.0f;
    float pct = (voltage - _cal.tpsVClosed) / range * 100.0f;
    return constrain(pct, 0.0f, 100.0f);
}

float SensorManager::voltageToNtcTempF(float voltage) const {
    // NTC thermistor with pullup resistor
    // V = Vref * R_ntc / (R_pullup + R_ntc)
    // R_ntc = R_pullup * V / (Vref - V)
    if (voltage <= 0.01f || voltage >= (ADC_REF_VOLTAGE - 0.01f)) return -40.0f;
    float rNtc = _cal.ntcPullupOhms * voltage / (ADC_REF_VOLTAGE - voltage);
    // Steinhart-Hart simplified (Beta equation)
    // 1/T = 1/T0 + (1/B) * ln(R/R0), T0=25C=298.15K, R0=resistance at T0
    float lnR = logf(rNtc / _cal.ntcPullupOhms);
    float invT = (1.0f / 298.15f) + (1.0f / _cal.ntcBeta) * lnR;
    float tempC = (1.0f / invT) - 273.15f;
    return tempC * 9.0f / 5.0f + 32.0f;  // Convert to Fahrenheit
}

float SensorManager::voltageToBatteryV(float voltage) const {
    return voltage * _cal.vbatDividerRatio;
}
