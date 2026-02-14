#include "ECU.h"
#include "CrankSensor.h"
#include "CamSensor.h"
#include "IgnitionManager.h"
#include "InjectionManager.h"
#include "FuelManager.h"
#include "AlternatorControl.h"
#include "SensorManager.h"
#include "CJ125Controller.h"
#include "ADS1115Reader.h"
#include "TuneTable.h"
#include "Config.h"
#include "Logger.h"
#include "PinExpander.h"
#include <esp_task_wdt.h>

// GPIO pin assignments (ESP32-S3)
static const uint8_t PIN_CRANK        = 1;
static const uint8_t PIN_CAM          = 2;
static const uint8_t PIN_ALTERNATOR   = 41;

// I2C bus for PCF8575 expander
static const uint8_t I2C_SDA          = 0;   // GPIO0 — strapping pin, ext pull-up, works as I2C SDA after boot
static const uint8_t I2C_SCL          = 42;  // GPIO42 — was FUEL_PUMP, now I2C SCL

// PCF8575 outputs (pin 100+ = PCF8575 P0-P15)
static const uint8_t PIN_FUEL_PUMP    = 100; // PCF P0 — moved from GPIO42
static const uint8_t PIN_TACH_OUT     = 101; // PCF P1 — was disabled (GPIO43=TX conflict)
static const uint8_t PIN_CEL          = 102; // PCF P2 — was disabled (GPIO44=RX conflict)

// CJ125 wideband O2 controller pins (from WB_O2_Micro schematic)
static const uint8_t PIN_SPI_SS_1     = 108; // MCP23017 P8 — CJ125 Bank 1 chip select
static const uint8_t PIN_SPI_SS_2     = 109; // MCP23017 P9 — CJ125 Bank 2 chip select
static const uint8_t PIN_HEATER_OUT_1 = 19;  // LEDC — heater PWM bank 1
static const uint8_t PIN_HEATER_OUT_2 = 20;  // LEDC — heater PWM bank 2
static const uint8_t PIN_CJ125_UA_1   = 3;   // ADC — CJ125 lambda output bank 1
static const uint8_t PIN_CJ125_UA_2   = 4;   // ADC — CJ125 lambda output bank 2

static const uint8_t COIL_PINS[]     = {10, 11, 12, 13, 14, 15, 16, 17};
// INJ 1-3: native GPIO for best timing; INJ 4-8: PCF8575 P3-P7
static const uint8_t INJECTOR_PINS[] = {18, 21, 40, 103, 104, 105, 106, 107};

ECU::ECU(Scheduler* ts)
    : _ts(ts), _tUpdate(nullptr), _crankTeeth(36), _crankMissing(1),
      _realtimeTaskHandle(nullptr), _cj125(nullptr), _ads1115(nullptr),
      _cj125Enabled(false) {
    memset(&_state, 0, sizeof(_state));
    memset(_firingOrder, 0, sizeof(_firingOrder));
    // Default SBC V8 firing order
    static const uint8_t defaultOrder[] = {1,8,4,3,6,5,7,2};
    memcpy(_firingOrder, defaultOrder, 8);
    for (uint8_t i = 0; i < 8; i++) _state.injTrim[i] = 1.0f;

    _crank = new CrankSensor();
    _cam = new CamSensor();
    _ignition = new IgnitionManager();
    _injection = new InjectionManager();
    _fuel = new FuelManager();
    _alternator = new AlternatorControl();
    _sensors = new SensorManager();
}

ECU::~ECU() {
    delete _crank;
    delete _cam;
    delete _ignition;
    delete _injection;
    delete _fuel;
    delete _alternator;
    delete _sensors;
    delete _cj125;
    delete _ads1115;
}

void ECU::configure(const ProjectInfo& proj) {
    _state.numCylinders = proj.cylinders;
    _state.sequentialMode = proj.hasCamSensor;

    // Store firing order for begin()
    memcpy(_firingOrder, proj.firingOrder, sizeof(_firingOrder));
    _crankTeeth = proj.crankTeeth;
    _crankMissing = proj.crankMissing;

    // Configure fuel manager
    _fuel->setReqFuel(proj.injectorFlowCcMin, proj.displacement, proj.cylinders);
    _fuel->setClosedLoopWindow(proj.closedLoopMinRpm, proj.closedLoopMaxRpm, proj.closedLoopMaxMapKpa);

    // Configure sensor calibration
    _sensors->setMapCalibration(proj.mapVoltageMin, proj.mapVoltageMax,
                                proj.mapPressureMinKpa, proj.mapPressureMaxKpa);
    _sensors->setO2Calibration(proj.o2AfrAt0v, proj.o2AfrAt5v);

    // Configure ignition
    _ignition->setRevLimit(proj.revLimitRpm);
    _ignition->setMaxDwellMs(proj.maxDwellMs);

    // Configure injection
    _injection->setDeadTimeMs(proj.injectorDeadTimeMs);

    // Configure alternator PID
    _alternator->setPID(proj.altPidP, proj.altPidI, proj.altPidD);

    // CJ125 wideband O2
    _cj125Enabled = proj.cj125Enabled;

    // Create 16x16 tune tables with defaults
    static const float defaultRpmAxis[16] = {
        500, 1000, 1500, 2000, 2500, 3000, 3500, 4000,
        4500, 5000, 5500, 6000, 6500, 7000, 7500, 8000
    };
    static const float defaultMapAxis[16] = {
        10, 15, 20, 25, 30, 40, 50, 60,
        70, 80, 85, 90, 95, 100, 105, 110
    };

    float defaults[256];

    // Spark advance: 15° default
    for (int i = 0; i < 256; i++) defaults[i] = 15.0f;
    TuneTable3D* sparkTable = new TuneTable3D();
    sparkTable->init(16, 16);
    sparkTable->setXAxis(defaultRpmAxis);
    sparkTable->setYAxis(defaultMapAxis);
    sparkTable->setValues(defaults);
    _fuel->setSparkTable(sparkTable);

    // VE: 80% default
    for (int i = 0; i < 256; i++) defaults[i] = 80.0f;
    TuneTable3D* veTable = new TuneTable3D();
    veTable->init(16, 16);
    veTable->setXAxis(defaultRpmAxis);
    veTable->setYAxis(defaultMapAxis);
    veTable->setValues(defaults);
    _fuel->setVeTable(veTable);

    // AFR target: 14.7 (stoich) default
    for (int i = 0; i < 256; i++) defaults[i] = 14.7f;
    TuneTable3D* afrTable = new TuneTable3D();
    afrTable->init(16, 16);
    afrTable->setXAxis(defaultRpmAxis);
    afrTable->setYAxis(defaultMapAxis);
    afrTable->setValues(defaults);
    _fuel->setAfrTable(afrTable);

    Log.info("ECU", "Configured: %d cyl, %d-%d trigger, cam=%s",
             proj.cylinders, proj.crankTeeth, proj.crankMissing,
             proj.hasCamSensor ? "yes" : "no");
}

void ECU::begin() {
    // Initialize PCF8575 I2C expander before subsystem init
    PinExpander::instance().begin(I2C_SDA, I2C_SCL, 0x20);

    // Initialize subsystems
    _crank->begin(PIN_CRANK, _crankTeeth, _crankMissing);
    _cam->setCrankSensor(_crank);
    if (_state.sequentialMode) {
        _cam->begin(PIN_CAM);
    }

    _ignition->begin(_state.numCylinders, COIL_PINS, _firingOrder);
    _injection->begin(_state.numCylinders, INJECTOR_PINS, _firingOrder);
    _fuel->begin();
    _alternator->begin(PIN_ALTERNATOR);
    _sensors->begin();

    // Fuel pump relay ON (PCF P0)
    xPinMode(PIN_FUEL_PUMP, OUTPUT);
    xDigitalWrite(PIN_FUEL_PUMP, HIGH);

    // Tach output (PCF P1)
    xPinMode(PIN_TACH_OUT, OUTPUT);
    xDigitalWrite(PIN_TACH_OUT, LOW);

    // CEL / check engine light (PCF P2)
    xPinMode(PIN_CEL, OUTPUT);
    xDigitalWrite(PIN_CEL, LOW);

    // CJ125 wideband O2 controller
    if (_cj125Enabled) {
        // SPI chip selects via MCP23017 — deselect before init
        xPinMode(PIN_SPI_SS_1, OUTPUT);
        xDigitalWrite(PIN_SPI_SS_1, HIGH);
        xPinMode(PIN_SPI_SS_2, OUTPUT);
        xDigitalWrite(PIN_SPI_SS_2, HIGH);

        _ads1115 = new ADS1115Reader();
        _ads1115->begin(0x48);

        _cj125 = new CJ125Controller(&SPI);
        _cj125->setADS1115(_ads1115);
        _cj125->begin(PIN_SPI_SS_1, PIN_SPI_SS_2,
                       PIN_HEATER_OUT_1, PIN_HEATER_OUT_2,
                       PIN_CJ125_UA_1, PIN_CJ125_UA_2);

        _sensors->setCJ125(_cj125);
        Log.info("ECU", "CJ125 wideband O2 enabled");
    }

    // Sensor + fuel calc update task (10ms on Core 0)
    _tUpdate = new Task(10 * TASK_MILLISECOND, TASK_FOREVER, [this]() { update(); }, _ts, true);

    // Core 1 real-time task — disabled for Phase 1 (no engine connected)
    // xTaskCreatePinnedToCore(realtimeTask, "ecu_rt", 4096, this, 24, &_realtimeTaskHandle, 1);

    Log.info("ECU", "ECU started, %d cylinders", _state.numCylinders);
}

void ECU::update() {
    // Core 0: read sensors and run fuel/ignition calculations
    _sensors->update();
    _cam->update();

    // Update shared state from sensors
    _state.rpm = _crank->getRpm();
    _state.mapKpa = _sensors->getMapKpa();
    _state.tps = _sensors->getTpsPercent();
    _state.afr[0] = _sensors->getO2Afr(0);
    _state.afr[1] = _sensors->getO2Afr(1);
    _state.coolantTempF = _sensors->getCoolantTempF();
    _state.iatTempF = _sensors->getIatTempF();
    _state.batteryVoltage = _sensors->getBatteryVoltage();

    // CJ125 wideband O2 update
    if (_cj125) {
        _cj125->update(_state.batteryVoltage);
        for (uint8_t i = 0; i < 2; i++) {
            _state.lambda[i] = _cj125->getLambda(i);
            _state.oxygenPct[i] = _cj125->getOxygen(i);
            _state.cj125Ready[i] = _cj125->isReady(i);
        }
    }

    // Engine state detection
    _state.cranking = (_state.rpm > 0 && _state.rpm < 400);
    _state.engineRunning = (_state.rpm >= 400);
    _state.sequentialMode = _cam->hasCamSignal();

    // Run fuel calculation (updates sparkAdvanceDeg, injPulseWidthUs, targetAfr)
    _fuel->update(_state);

    // Push calculated values to ignition and injection
    _ignition->setAdvance(_state.sparkAdvanceDeg);
    _injection->setPulseWidthUs(_state.injPulseWidthUs);

    // Update per-cylinder trims
    for (uint8_t i = 0; i < _state.numCylinders; i++) {
        _state.injTrim[i] = _injection->getTrim(i);
    }

    // Alternator control (100ms effective via PID internal dt)
    _alternator->update(_state.batteryVoltage);
}

void ECU::realtimeTask(void* param) {
    ECU* ecu = (ECU*)param;

    // Detach this task from the Task Watchdog Timer — high-priority RT task
    // manages its own timing via vTaskDelay, not idle task monitoring
    esp_task_wdt_delete(NULL);

    // Core 1: real-time ignition and injection timing
    while (true) {
        uint16_t rpm = ecu->_state.rpm;

        if (rpm > 0 && ecu->_crank->isSynced()) {
            uint16_t toothPos = ecu->_crank->getToothPosition();
            bool seq = ecu->_state.sequentialMode;
            ecu->_ignition->update(rpm, toothPos, seq);
            ecu->_injection->update(rpm, toothPos, seq);
            vTaskDelay(1); // ~1ms yield when engine running
        } else {
            vTaskDelay(pdMS_TO_TICKS(10)); // 10ms sleep when engine off — save CPU
        }
    }
}

const char* ECU::getStateString() const {
    if (_state.rpm == 0) return "OFF";
    if (_state.cranking) return "CRANKING";
    if (_state.engineRunning) return "RUNNING";
    return "OFF";
}
