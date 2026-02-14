#pragma once

#include <Arduino.h>

class ADS1115Reader;
struct ProjectInfo;

enum class TransType : uint8_t {
    NONE = 0,
    FORD_4R70W = 1,
    FORD_4R100 = 2
};

enum class Gear : uint8_t {
    PARK = 0,
    REVERSE,
    NEUTRAL,
    DRIVE_1,
    DRIVE_2,
    DRIVE_3,
    DRIVE_4
};

enum class MLPSPosition : uint8_t {
    PARK = 0,
    REVERSE,
    NEUTRAL,
    OD,
    DRIVE,
    LOW2,
    LOW1,
    UNKNOWN
};

struct TransmissionState {
    Gear currentGear = Gear::PARK;
    Gear targetGear = Gear::PARK;
    MLPSPosition mlpsPosition = MLPSPosition::PARK;
    uint16_t ossRpm = 0;
    uint16_t tssRpm = 0;
    float tftTempF = 0.0f;
    float tccDuty = 0.0f;
    float epcDuty = 0.0f;
    bool ssA = false;
    bool ssB = false;
    bool ssC = false;
    bool ssD = false;
    bool tccLocked = false;
    bool shifting = false;
    int16_t slipRpm = 0;
    bool overTemp = false;
};

class TransmissionManager {
public:
    TransmissionManager();

    void configure(const ProjectInfo& proj);
    void begin(uint8_t ssAPin, uint8_t ssBPin, uint8_t ssCPin, uint8_t ssDPin,
               uint8_t tccPin, uint8_t epcPin, uint8_t ossPin, uint8_t tssPin);
    void setADS1115(ADS1115Reader* ads) { _ads = ads; }
    void update(uint16_t engineRpm, float tps, float vbat);

    const TransmissionState& getState() const { return _state; }
    TransType getType() const { return _type; }

    static const char* gearToString(Gear g);
    static const char* mlpsToString(MLPSPosition p);
    static const char* typeToString(TransType t);

    // ISR handlers (called from static ISR wrappers)
    void onOssPulse();
    void onTssPulse();

private:
    TransType _type = TransType::NONE;
    TransmissionState _state;

    // Pin assignments
    uint8_t _ssAPin = 0, _ssBPin = 0, _ssCPin = 0, _ssDPin = 0;
    uint8_t _tccPin = 0, _epcPin = 0;
    uint8_t _ossPin = 0, _tssPin = 0;

    // LEDC channels
    static constexpr uint8_t TCC_LEDC_CH = 4;
    static constexpr uint8_t EPC_LEDC_CH = 6;

    // Speed sensor pulse counting
    volatile uint32_t _ossPulseCount = 0;
    volatile uint32_t _tssPulseCount = 0;
    uint32_t _lastSpeedCalcMs = 0;
    uint32_t _prevOssPulses = 0;
    uint32_t _prevTssPulses = 0;
    static constexpr uint8_t PULSES_PER_REV = 8;

    // Shift parameters (from config)
    uint16_t _upshift12Rpm = 1500;
    uint16_t _upshift23Rpm = 2500;
    uint16_t _upshift34Rpm = 3000;
    uint16_t _downshift21Rpm = 1200;
    uint16_t _downshift32Rpm = 1800;
    uint16_t _downshift43Rpm = 2200;
    uint16_t _tccLockRpm = 1500;
    uint8_t _tccLockGear = 3;
    float _tccApplyRate = 5.0f;
    float _epcBaseDuty = 50.0f;
    float _epcShiftBoost = 80.0f;
    uint16_t _shiftTimeMs = 500;
    float _maxTftTempF = 275.0f;

    // Shift state
    uint32_t _shiftStartMs = 0;
    float _tccTargetDuty = 0.0f;

    // Decimation
    uint8_t _updateCounter = 0;

    ADS1115Reader* _ads = nullptr;
    bool _tccEnabled = false;
    bool _epcEnabled = false;
    bool _speedSensorsEnabled = false;

    // Internal methods
    void calcSpeedSensors();
    void readTftTemp();
    MLPSPosition readMLPS();
    void updateGearLogic(uint16_t engineRpm, float tps);
    void applyShiftSolenoids();
    void updateTCC(uint16_t engineRpm);
    void updateEPC(float tps);
    void setSolenoid(uint8_t pin, bool on);

    // TFT thermistor conversion
    static float tftAdcToTempF(float millivolts);
    // MLPS voltage to position
    static MLPSPosition mlpsVoltageToPosition(float millivolts);
};
