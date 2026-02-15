#pragma once

#include <Arduino.h>
#include <TaskSchedulerDeclarations.h>

class CrankSensor;
class CamSensor;
class IgnitionManager;
class InjectionManager;
class FuelManager;
class AlternatorControl;
class SensorManager;
class CJ125Controller;
class ADS1115Reader;
class TransmissionManager;
struct ProjectInfo;

struct EngineState {
    volatile uint16_t rpm;
    volatile float mapKpa;
    volatile float tps;
    volatile float afr[2];
    volatile float coolantTempF;
    volatile float iatTempF;
    volatile float batteryVoltage;
    volatile float targetAfr;
    volatile float sparkAdvanceDeg;
    volatile float injPulseWidthUs;
    volatile float injTrim[8];
    volatile bool  engineRunning;
    volatile bool  cranking;
    volatile uint8_t numCylinders;
    volatile bool  sequentialMode;
    volatile float lambda[2];
    volatile float oxygenPct[2];
    volatile bool  cj125Ready[2];
};

class ECU {
public:
    ECU(Scheduler* ts);
    ~ECU();

    void configure(const ProjectInfo& proj);
    void setPeripheralFlags(const ProjectInfo& proj);
    void begin();
    void update();

    const EngineState& getState() const { return _state; }
    const char* getStateString() const;

    CrankSensor* getCrankSensor() { return _crank; }
    CamSensor* getCamSensor() { return _cam; }
    IgnitionManager* getIgnitionManager() { return _ignition; }
    InjectionManager* getInjectionManager() { return _injection; }
    FuelManager* getFuelManager() { return _fuel; }
    AlternatorControl* getAlternator() { return _alternator; }
    SensorManager* getSensorManager() { return _sensors; }
    CJ125Controller* getCJ125() { return _cj125; }
    TransmissionManager* getTransmission() { return _trans; }
    uint32_t getUpdateTimeUs() const { return _updateTimeUs; }
    uint32_t getSensorTimeUs() const { return _sensorTimeUs; }

private:
    Scheduler* _ts;
    Task* _tUpdate;
    EngineState _state;

    CrankSensor* _crank;
    CamSensor* _cam;
    IgnitionManager* _ignition;
    InjectionManager* _injection;
    FuelManager* _fuel;
    AlternatorControl* _alternator;
    SensorManager* _sensors;
    CJ125Controller* _cj125;
    ADS1115Reader* _ads1115;
    ADS1115Reader* _ads1115_2;   // Second ADS1115 at 0x49 for MAP/TPS (frees GPIO 5/6 for OSS/TSS)
    TransmissionManager* _trans;
    bool _cj125Enabled;
    uint8_t _transType;

    uint8_t _firingOrder[12];
    uint8_t _crankTeeth;
    uint8_t _crankMissing;

    // Configurable pin assignments (from ProjectInfo)
    uint8_t _pinAlternator;
    uint8_t _pinI2cSda;
    uint8_t _pinI2cScl;
    uint8_t _pinHeater1;
    uint8_t _pinHeater2;
    uint8_t _pinCj125Ua1;
    uint8_t _pinCj125Ua2;
    uint8_t _pinCj125Ss1;    // MCP23017 P8 — CJ125 Bank 1 CS (fixed 108)
    uint8_t _pinCj125Ss2;    // MCP23017 P9 — CJ125 Bank 2 CS (fixed 109)
    uint8_t _pinTcc;
    uint8_t _pinEpc;
    uint8_t _pinHspiSck;
    uint8_t _pinHspiMosi;
    uint8_t _pinHspiMiso;
    uint8_t _pinHspiCsCoils;
    uint8_t _pinHspiCsInj;

    // Peripheral enable flags (from config)
    bool _i2cEnabled = true;
    bool _spiExpandersEnabled = true;
    bool _expander0Enabled = true;
    bool _expander1Enabled = true;
    bool _expander2Enabled = true;
    bool _expander3Enabled = true;
    bool _spiExp0Enabled = true;
    bool _spiExp1Enabled = true;

    volatile uint32_t _updateTimeUs = 0;
    volatile uint32_t _sensorTimeUs = 0;

    TaskHandle_t _realtimeTaskHandle;
    static void realtimeTask(void* param);
};
