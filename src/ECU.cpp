#include "ECU.h"
#include "CrankSensor.h"
#include "CamSensor.h"
#include "IgnitionManager.h"
#include "InjectionManager.h"
#include "FuelManager.h"
#include "AlternatorControl.h"
#include "SensorManager.h"
#include "TuneTable.h"
#include "Config.h"
#include "Logger.h"
#include <esp_task_wdt.h>

// GPIO pin assignments (ESP32-S3)
static const uint8_t PIN_CRANK        = 1;
static const uint8_t PIN_CAM          = 2;
static const uint8_t PIN_ALTERNATOR   = 41;
static const uint8_t PIN_FUEL_PUMP    = 42;
// GPIO43=TX0, GPIO44=RX0 — reserved for Serial (UART0)
// Tach and CEL need reassignment; disabled for Phase 1
static const uint8_t PIN_TACH_OUT     = 0;
static const uint8_t PIN_CEL          = 0;

static const uint8_t COIL_PINS[]     = {10, 11, 12, 13, 14, 15, 16, 17};
// GPIO33-37 used by OPI PSRAM — CANNOT use as GPIO
// GPIO38/39 reassigned to SD SPI
// Phase 1: only 3 usable injector pins (full 8 requires I/O expander or pin remapping)
static const uint8_t INJECTOR_PINS[] = {18, 21, 40, 0, 0, 0, 0, 0};

ECU::ECU(Scheduler* ts)
    : _ts(ts), _tUpdate(nullptr), _crankTeeth(36), _crankMissing(1),
      _realtimeTaskHandle(nullptr) {
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

    Log.info("ECU", "Configured: %d cyl, %d-%d trigger, cam=%s",
             proj.cylinders, proj.crankTeeth, proj.crankMissing,
             proj.hasCamSensor ? "yes" : "no");
}

void ECU::begin() {
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

    // Fuel pump relay ON
    if (PIN_FUEL_PUMP) {
        pinMode(PIN_FUEL_PUMP, OUTPUT);
        digitalWrite(PIN_FUEL_PUMP, HIGH);
    }

    // Tach output (disabled if pin=0)
    if (PIN_TACH_OUT) {
        pinMode(PIN_TACH_OUT, OUTPUT);
        digitalWrite(PIN_TACH_OUT, LOW);
    }

    // CEL / check engine light (disabled if pin=0)
    if (PIN_CEL) {
        pinMode(PIN_CEL, OUTPUT);
        digitalWrite(PIN_CEL, LOW);
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
