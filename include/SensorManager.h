#pragma once

#include <Arduino.h>
#include "SensorDescriptor.h"

class CJ125Controller;
class ADS1115Reader;
class MCP3204Reader;
class IgnitionManager;
class InjectionManager;
class AlternatorControl;
struct EngineState;

class SensorManager {
public:
    // Backward-compatible slot indices
    static const uint8_t SLOT_O2_B1  = 0;
    static const uint8_t SLOT_O2_B2  = 1;
    static const uint8_t SLOT_MAP    = 2;
    static const uint8_t SLOT_TPS    = 3;
    static const uint8_t SLOT_CLT    = 4;
    static const uint8_t SLOT_IAT    = 5;
    static const uint8_t SLOT_VBAT   = 6;
    static const uint8_t SLOT_OIL    = 7;

    // Legacy constants
    static const uint16_t ADC_MAX_VALUE = 4095;
    static constexpr float ADC_REF_VOLTAGE = 3.3f;

    // Fault bitmask constants (backward compatible)
    static constexpr uint8_t FAULT_MAP      = 0x01;
    static constexpr uint8_t FAULT_TPS      = 0x02;
    static constexpr uint8_t FAULT_CLT      = 0x04;
    static constexpr uint8_t FAULT_IAT      = 0x08;
    static constexpr uint8_t FAULT_VBAT     = 0x10;
    static constexpr uint8_t FAULT_EXPANDER = 0x20;
    static constexpr uint8_t FAULT_OIL      = 0x40;

    SensorManager();
    ~SensorManager();

    void begin();
    void update();

    // --- Descriptor access ---
    SensorDescriptor* getDescriptor(uint8_t slot) { return slot < MAX_SENSORS ? &_desc[slot] : nullptr; }
    const SensorDescriptor* getDescriptor(uint8_t slot) const { return slot < MAX_SENSORS ? &_desc[slot] : nullptr; }
    uint8_t getDescriptorCount() const;  // Number of enabled descriptors
    void initDefaultDescriptors();

    // --- Rule access ---
    FaultRule* getRule(uint8_t idx) { return idx < MAX_RULES ? &_rules[idx] : nullptr; }
    const FaultRule* getRule(uint8_t idx) const { return idx < MAX_RULES ? &_rules[idx] : nullptr; }
    uint8_t getRuleCount() const;  // Number of active rules
    void initDefaultRules();

    // --- Backward-compatible getters (read from descriptor slots) ---
    float getO2Afr(uint8_t bank) const;
    float getMapKpa() const { return _desc[SLOT_MAP].value; }
    float getTpsPercent() const { return _desc[SLOT_TPS].value; }
    float getCoolantTempF() const { return _desc[SLOT_CLT].value; }
    float getIatTempF() const { return _desc[SLOT_IAT].value; }
    float getBatteryVoltage() const { return _desc[SLOT_VBAT].value; }
    float getOilPressurePsi() const { return _desc[SLOT_OIL].value; }
    bool isOilPressureLow() const { return _desc[SLOT_OIL].inError; }
    uint16_t getRawAdc(uint8_t channel) const;

    // --- Backward-compatible setters (update descriptor calibration) ---
    void setMapCalibration(float vMin, float vMax, float pMin, float pMax);
    void setO2Calibration(float afrAt0v, float afrAt5v);
    void setVbatDividerRatio(float ratio);

    void setPins(uint8_t o2b1, uint8_t o2b2, uint8_t map, uint8_t tps,
                 uint8_t clt, uint8_t iat, uint8_t vbat);

    uint8_t getPin(uint8_t channel) const;

    void setCJ125(CJ125Controller* cj125) { _cj125 = cj125; }
    CJ125Controller* getCJ125() const { return _cj125; }

    void setADS1115(ADS1115Reader* ads) { _ads0 = ads; }           // 0x48
    void setMapTpsADS1115(ADS1115Reader* ads) { _mapTpsAds = ads; } // 0x49
    bool hasMapTpsADS1115() const { return _mapTpsAds != nullptr; }

    void setMapTpsMCP3204(MCP3204Reader* mcp) { _mapTpsMcp = mcp; }
    bool hasMapTpsMCP3204() const { return _mapTpsMcp != nullptr; }
    bool hasExternalMapTps() const { return _mapTpsAds != nullptr || _mapTpsMcp != nullptr; }

    // Virtual pin constants for external ADC channels
    static const uint8_t VPIN_MCP3204_BASE = 240;  // 240-243: MCP3204 CH0-CH3
    static const uint8_t VPIN_ADS0_BASE    = 244;  // 244-247: ADS1115 #0 (0x48) CH0-CH3
    static const uint8_t VPIN_ADS1_BASE    = 248;  // 248-251: ADS1115 #1 (0x49) CH0-CH3
    static bool isVirtualPin(uint8_t pin) { return pin >= VPIN_MCP3204_BASE; }

    // --- Limp/fault ---
    uint8_t getLimpFaults() const { return _limpFaults; }
    uint8_t getCelFaults() const { return _celFaults; }
    void setLimpThresholds(float mapMin, float mapMax, float tpsMin, float tpsMax,
                           float cltMax, float iatMax, float vbatMin);

    // Engine running state (needed for rule evaluation)
    void setEngineRunning(bool running) { _engineRunning = running; }

    // Read an engine state channel by index (for dynamic threshold curves)
    float readEngineStateChannel(uint8_t channel) const;

    // Manager pointers for virtual sensor sources
    void setEngineStatePtr(const EngineState* es) { _engineState = es; }
    void setIgnitionManager(IgnitionManager* ign) { _ignition = ign; }
    void setInjectionManager(InjectionManager* inj) { _injection = inj; }
    void setAlternatorControl(AlternatorControl* alt) { _alternator = alt; }

    // Oil pressure configuration (writes to descriptor slot 7)
    void configureOilPressure(uint8_t mode, uint8_t pin, bool activeLow,
                              float minPsi, float maxPsi, uint8_t mcpChannel);

private:
    CJ125Controller* _cj125 = nullptr;
    ADS1115Reader* _ads0 = nullptr;       // 0x48 (CJ125/trans shared)
    ADS1115Reader* _mapTpsAds = nullptr;  // 0x49 (MAP/TPS)
    MCP3204Reader* _mapTpsMcp = nullptr;

    uint8_t _configuredPins[7] = {};      // Pin assignments from setPins() (preserved across loadSensorConfig)

    const EngineState* _engineState = nullptr;
    IgnitionManager* _ignition = nullptr;
    InjectionManager* _injection = nullptr;
    AlternatorControl* _alternator = nullptr;

    SensorDescriptor _desc[MAX_SENSORS];
    FaultRule _rules[MAX_RULES];

    uint8_t _limpFaults = 0;
    uint8_t _celFaults = 0;
    bool _engineRunning = false;

    // Read raw value from source device for a descriptor
    float readSource(SensorDescriptor& d);

    // Apply calibration to convert voltage to engineering units
    float calibrate(const SensorDescriptor& d, float voltage);

    // Apply EMA filter
    float applyFilter(SensorDescriptor& d, float rawValue);

    // Evaluate all fault rules
    void evaluateRules();

    // Piecewise-linear interpolation for dynamic threshold curves
    static float interpolateCurve(const float* xs, const float* ys, uint8_t n, float x);

    // Coil/injector health via MCP23S17 readback
    float readCoilHealth();
    float readInjectorHealth();
};
