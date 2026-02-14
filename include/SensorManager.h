#pragma once

#include <Arduino.h>

class CJ125Controller;
class ADS1115Reader;

class SensorManager {
public:
    static const uint8_t O2_BANK1_PIN = 3;
    static const uint8_t O2_BANK2_PIN = 4;
    static const uint8_t MAP_PIN = 5;
    static const uint8_t TPS_PIN = 6;
    static const uint8_t CLT_PIN = 7;
    static const uint8_t IAT_PIN = 8;
    static const uint8_t VBAT_PIN = 9;
    static const uint8_t NUM_CHANNELS = 7;

    static const uint16_t ADC_MAX_VALUE = 4095;
    static constexpr float ADC_REF_VOLTAGE = 3.3f;
    static constexpr float DEFAULT_EMA_ALPHA = 0.3f;

    // NTC thermistor defaults
    static constexpr float NTC_PULLUP_OHMS = 2490.0f;
    static constexpr float NTC_BETA = 3380.0f;

    struct AdcCalibration {
        float mapVMin, mapVMax, mapPMin, mapPMax;
        float o2AfrAt0v, o2AfrAt5v;
        float tpsVClosed, tpsVOpen;
        float vbatDividerRatio;
        float ntcPullupOhms, ntcBeta;
        float emaAlpha;
    };

    SensorManager();
    ~SensorManager();

    void begin();
    void update();

    float getO2Afr(uint8_t bank) const;
    float getMapKpa() const { return _mapKpa; }
    float getTpsPercent() const { return _tpsPercent; }
    float getCoolantTempF() const { return _coolantTempF; }
    float getIatTempF() const { return _iatTempF; }
    float getBatteryVoltage() const { return _batteryVoltage; }
    uint16_t getRawAdc(uint8_t channel) const;

    void setMapCalibration(float vMin, float vMax, float pMin, float pMax);
    void setO2Calibration(float afrAt0v, float afrAt5v);
    void setVbatDividerRatio(float ratio);

    void setCJ125(CJ125Controller* cj125) { _cj125 = cj125; }
    CJ125Controller* getCJ125() const { return _cj125; }

    void setMapTpsADS1115(ADS1115Reader* ads) { _mapTpsAds = ads; }
    bool hasMapTpsADS1115() const { return _mapTpsAds != nullptr; }

private:
    CJ125Controller* _cj125 = nullptr;
    ADS1115Reader* _mapTpsAds = nullptr;
    AdcCalibration _cal;
    float _rawFiltered[NUM_CHANNELS];
    uint16_t _rawAdc[NUM_CHANNELS];

    float _o2Afr[2];
    float _mapKpa;
    float _tpsPercent;
    float _coolantTempF;
    float _iatTempF;
    float _batteryVoltage;

    static const uint8_t _channelPins[NUM_CHANNELS];

    float adcToVoltage(uint16_t raw) const;
    float voltageToMapKpa(float voltage) const;
    float voltageToAfr(float voltage) const;
    float voltageToTpsPercent(float voltage) const;
    float voltageToNtcTempF(float voltage) const;
    float voltageToBatteryV(float voltage) const;
};
