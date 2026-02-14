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
    TransmissionManager* _trans;
    bool _cj125Enabled;
    uint8_t _transType;

    uint8_t _firingOrder[12];
    uint8_t _crankTeeth;
    uint8_t _crankMissing;

    TaskHandle_t _realtimeTaskHandle;
    static void realtimeTask(void* param);
};
