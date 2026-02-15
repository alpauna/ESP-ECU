#pragma once

#include <Arduino.h>

class CJ125Controller;
class ADS1115Reader;
class MCP3204Reader;

class SensorManager {
public:
    static const uint8_t NUM_CHANNELS = 7;

    static const uint16_t ADC_MAX_VALUE = 4095;
    static constexpr float ADC_REF_VOLTAGE = 3.3f;
    static constexpr float DEFAULT_EMA_ALPHA = 0.3f;

    // NTC thermistor defaults
    static constexpr float NTC_PULLUP_OHMS = 2490.0f;
    static constexpr float NTC_BETA = 3380.0f;

    // Limp fault bitmask
    static constexpr uint8_t FAULT_MAP  = 0x01;
    static constexpr uint8_t FAULT_TPS  = 0x02;
    static constexpr uint8_t FAULT_CLT  = 0x04;
    static constexpr uint8_t FAULT_IAT  = 0x08;
    static constexpr uint8_t FAULT_VBAT = 0x10;
    static constexpr uint8_t FAULT_EXPANDER = 0x20;
    static constexpr uint8_t FAULT_OIL      = 0x40;

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

    void setPins(uint8_t o2b1, uint8_t o2b2, uint8_t map, uint8_t tps,
                 uint8_t clt, uint8_t iat, uint8_t vbat);

    uint8_t getPin(uint8_t channel) const { return channel < NUM_CHANNELS ? _channelPins[channel] : 0; }

    void setCJ125(CJ125Controller* cj125) { _cj125 = cj125; }
    CJ125Controller* getCJ125() const { return _cj125; }

    void setMapTpsADS1115(ADS1115Reader* ads) { _mapTpsAds = ads; }
    bool hasMapTpsADS1115() const { return _mapTpsAds != nullptr; }

    void setMapTpsMCP3204(MCP3204Reader* mcp) { _mapTpsMcp = mcp; }
    bool hasMapTpsMCP3204() const { return _mapTpsMcp != nullptr; }
    bool hasExternalMapTps() const { return _mapTpsAds != nullptr || _mapTpsMcp != nullptr; }

    // Limp mode sensor validation
    uint8_t getLimpFaults() const { return _limpFaults; }
    void setLimpThresholds(float mapMin, float mapMax, float tpsMin, float tpsMax,
                           float cltMax, float iatMax, float vbatMin);

private:
    CJ125Controller* _cj125 = nullptr;
    ADS1115Reader* _mapTpsAds = nullptr;
    MCP3204Reader* _mapTpsMcp = nullptr;
    AdcCalibration _cal;
    float _rawFiltered[NUM_CHANNELS];
    uint16_t _rawAdc[NUM_CHANNELS];

    float _o2Afr[2];
    float _mapKpa;
    float _tpsPercent;
    float _coolantTempF;
    float _iatTempF;
    float _batteryVoltage;

    uint8_t _channelPins[NUM_CHANNELS];

    // Limp fault state and thresholds
    uint8_t _limpFaults = 0;
    float _limpMapMin = 5.0f, _limpMapMax = 120.0f;
    float _limpTpsMin = -5.0f, _limpTpsMax = 105.0f;
    float _limpCltMax = 280.0f;
    float _limpIatMax = 200.0f;
    float _limpVbatMin = 10.0f;
    void validateSensors();

    float adcToVoltage(uint16_t raw) const;
    float voltageToMapKpa(float voltage) const;
    float voltageToAfr(float voltage) const;
    float voltageToTpsPercent(float voltage) const;
    float voltageToNtcTempF(float voltage) const;
    float voltageToBatteryV(float voltage) const;
};
