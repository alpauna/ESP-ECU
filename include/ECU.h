#pragma once

#include <Arduino.h>
#include <functional>
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
class MCP3204Reader;
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
    volatile bool  limpMode;
    volatile uint8_t limpFaults;
    volatile float oilPressurePsi;
    volatile bool  oilPressureLow;
    volatile uint8_t expanderFaults;
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
    ADS1115Reader* getADS1115_0() { return _ads1115; }
    ADS1115Reader* getADS1115_1() { return _ads1115_2; }
    MCP3204Reader* getMCP3204() { return _mcp3204; }
    uint32_t getUpdateTimeUs() const { return _updateTimeUs; }
    uint32_t getSensorTimeUs() const { return _sensorTimeUs; }
    bool isLimpActive() const { return _limpActive; }
    uint8_t getLimpFaults() const { return _limpFaults; }

    typedef std::function<void(const char* fault, const char* message, bool active)> FaultCallback;
    void setFaultCallback(FaultCallback cb) { _faultCb = cb; }

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
    MCP3204Reader* _mcp3204;     // MCP3204 SPI ADC for MAP/TPS (alternative to ADS1115 @ 0x49)
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
    uint8_t _pinMcp3204Cs;

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

    // Limp mode
    bool _limpActive = false;
    uint8_t _limpFaults = 0;
    uint16_t _limpRevLimit = 3000;
    float _limpAdvanceCap = 10.0f;
    uint32_t _limpRecoveryMs = 5000;
    uint32_t _limpRecoveryStart = 0;
    uint16_t _normalRevLimit = 6000;
    void checkLimpMode();
    FaultCallback _faultCb;

    // Expander health
    uint8_t _expanderHealthCounter = 0;
    uint8_t _expanderFaults = 0;
    void checkExpanderHealth();

    // Oil pressure
    uint8_t _oilPressureMode = 0;       // 0=disabled, 1=digital, 2=analog
    uint8_t _pinOilPressure = 0;
    bool _oilPressureActiveLow = true;
    float _oilPressureMinPsi = 10.0f;
    float _oilPressureMaxPsi = 100.0f;
    uint8_t _oilPressureMcpChannel = 2;
    uint32_t _oilPressureStartupMs = 3000;
    uint32_t _engineRunStartMs = 0;
    bool _engineWasRunning = false;
    float _oilPressurePsi = 0.0f;
    bool _oilPressureLow = false;
    bool _oilPressureFault = false;
    void checkOilPressure();

    TaskHandle_t _realtimeTaskHandle;
    static void realtimeTask(void* param);
};
