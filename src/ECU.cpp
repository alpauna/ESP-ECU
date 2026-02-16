#include "ECU.h"
#include <Wire.h>
#include "CrankSensor.h"
#include "CamSensor.h"
#include "IgnitionManager.h"
#include "InjectionManager.h"
#include "FuelManager.h"
#include "AlternatorControl.h"
#include "SensorManager.h"
#include "CJ125Controller.h"
#include "ADS1115Reader.h"
#include "MCP3204Reader.h"
#include "TransmissionManager.h"
#include "CustomPin.h"
#include "TuneTable.h"
#include "Config.h"
#include "Logger.h"
#include "PinExpander.h"
#include <esp_task_wdt.h>
#include <esp_log.h>

// Locked GPIO pin assignments — time-critical, not configurable
static const uint8_t PIN_CRANK        = 1;
static const uint8_t PIN_CAM          = 2;

static SPIClass hspi(HSPI);

ECU::ECU(Scheduler* ts)
    : _ts(ts), _tUpdate(nullptr), _crankTeeth(36), _crankMissing(1),
      _realtimeTaskHandle(nullptr), _cj125(nullptr), _ads1115(nullptr),
      _ads1115_2(nullptr), _mcp3204(nullptr), _trans(nullptr), _customPins(nullptr),
      _cj125Enabled(false), _transType(0),
      _pinAlternator(41), _pinI2cSda(0), _pinI2cScl(42),
      _pinHeater1(19), _pinHeater2(20), _pinCj125Ua1(3), _pinCj125Ua2(4),
      _pinCj125Ss1(208), _pinCj125Ss2(209),
      _pinTcc(45), _pinEpc(46),
      _pinHspiSck(10), _pinHspiMosi(11), _pinHspiMiso(12),
      _pinHspiCs(13), _pinMcp3204Cs(15),
      _pinFuelPump(200), _pinTachOut(201), _pinCel(202),
      _pinSsA(216), _pinSsB(217), _pinSsC(218), _pinSsD(219),
      _pinSharedInt(0xFF) {
    memset(&_state, 0, sizeof(_state));
    memset(_firingOrder, 0, sizeof(_firingOrder));
    // Default coil/injector pins (MCP23S17 #4 and #5)
    for (uint8_t i = 0; i < 12; i++) {
        _coilPins[i] = (i < 8) ? 264 + i : 0;     // MCP23S17 #4: 264-271
        _injectorPins[i] = (i < 8) ? 280 + i : 0;  // MCP23S17 #5: 280-287
    }
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
    delete _ads1115_2;
    delete _mcp3204;
    delete _trans;
    delete _customPins;
    delete _cltRevLimitTable;
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
    _ignition->setConfigRevLimit(proj.revLimitRpm);
    _ignition->setMaxDwellMs(proj.maxDwellMs);

    // Configure injection
    _injection->setDeadTimeMs(proj.injectorDeadTimeMs);

    // Configure alternator PID
    _alternator->setPID(proj.altPidP, proj.altPidI, proj.altPidD);

    // Pin assignments from config
    _pinAlternator = proj.pinAlternator;
    _pinI2cSda = proj.pinI2cSda;
    _pinI2cScl = proj.pinI2cScl;
    _pinHeater1 = proj.pinHeater1;
    _pinHeater2 = proj.pinHeater2;
    _pinCj125Ua1 = proj.pinO2Bank1;  // CJ125 UA shares O2 ADC pins
    _pinCj125Ua2 = proj.pinO2Bank2;
    _pinTcc = proj.pinTcc;
    _pinEpc = proj.pinEpc;
    _pinHspiSck = proj.pinHspiSck;
    _pinHspiMosi = proj.pinHspiMosi;
    _pinHspiMiso = proj.pinHspiMiso;
    _pinHspiCs = proj.pinHspiCs;
    _pinMcp3204Cs = proj.pinMcp3204Cs;

    // Configure sensor pin assignments
    _sensors->setPins(proj.pinO2Bank1, proj.pinO2Bank2, proj.pinMap, proj.pinTps,
                      proj.pinClt, proj.pinIat, proj.pinVbat);

    // Configurable expander/output pins
    _pinFuelPump = proj.pinFuelPump;
    _pinTachOut = proj.pinTachOut;
    _pinCel = proj.pinCel;
    _pinSsA = proj.pinSsA;
    _pinSsB = proj.pinSsB;
    _pinSsC = proj.pinSsC;
    _pinSsD = proj.pinSsD;
    memcpy(_coilPins, proj.coilPins, sizeof(_coilPins));
    memcpy(_injectorPins, proj.injectorPins, sizeof(_injectorPins));
    // Shared expander interrupt GPIO
    _pinSharedInt = proj.pinSharedInt;

    // CJ125 wideband O2
    _cj125Enabled = proj.cj125Enabled;

    // Transmission
    _transType = proj.transType;
    if (_transType > 0) {
        _trans = new TransmissionManager();
        _trans->configure(proj);
    }

    // Limp mode
    _limpRevLimit = proj.limpRevLimit;
    _limpAdvanceCap = proj.limpAdvanceCap;
    _limpRecoveryMs = proj.limpRecoveryMs;
    _sensors->setLimpThresholds(proj.limpMapMin, proj.limpMapMax,
        proj.limpTpsMin, proj.limpTpsMax, proj.limpCltMax, proj.limpIatMax, proj.limpVbatMin);

    // Oil pressure
    _oilPressureMode = proj.oilPressureMode;
    _pinOilPressure = proj.pinOilPressure;
    _oilPressureActiveLow = proj.oilPressureActiveLow;
    _oilPressureMinPsi = proj.oilPressureMinPsi;
    _oilPressureMaxPsi = proj.oilPressureMaxPsi;
    _oilPressureMcpChannel = proj.oilPressureMcpChannel;
    // Fuel pump priming
    _fuelPumpPrimeMs = proj.fuelPumpPrimeMs;

    // ASE
    _fuel->setAseParams(proj.aseInitialPct, proj.aseDurationMs, proj.aseMinCltF);

    // DFCO
    _fuel->setDfcoParams(proj.dfcoRpmThreshold, proj.dfcoTpsThreshold,
                         proj.dfcoEntryDelayMs, proj.dfcoExitRpm, proj.dfcoExitTps);

    // CLT-dependent rev limit
    _cltRevLimitTable = new TuneTable2D();
    _cltRevLimitTable->init(6);
    _cltRevLimitTable->setAxis(proj.cltRevLimitAxis);
    _cltRevLimitTable->setValues(proj.cltRevLimitValues);

    // oilPressureStartupMs handled by rule debounce in SensorManager

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

void ECU::setPeripheralFlags(const ProjectInfo& proj) {
    _i2cEnabled = proj.i2cEnabled;
    _spiExpandersEnabled = proj.spiExpandersEnabled;
    _expander0Enabled = proj.expander0Enabled;
    _expander1Enabled = proj.expander1Enabled;
    _expander2Enabled = proj.expander2Enabled;
    _expander3Enabled = proj.expander3Enabled;
    _expander4Enabled = proj.expander4Enabled;
    _expander5Enabled = proj.expander5Enabled;
}

void ECU::begin() {
    // I2C bus — retained for ADS1115 ADCs only (no MCP23017 expanders)
    if (_i2cEnabled) {
        Wire.begin(_pinI2cSda, _pinI2cScl);
        Wire.setTimeOut(10);  // 10ms I2C bus timeout (capital O = I2C, not Stream)
        // Suppress Wire I2C error spam during probe
        esp_log_level_set("Wire", ESP_LOG_NONE);
        Log.info("ECU", "I2C bus initialized (SDA=%d, SCL=%d) — ADS1115 only", _pinI2cSda, _pinI2cScl);
    } else {
        Log.warn("ECU", "I2C bus disabled — ADS1115 skipped");
    }

    // HSPI bus for 6x MCP23S17 expanders (shared CS, HAEN hardware addressing)
    if (_spiExpandersEnabled) {
        hspi.begin(_pinHspiSck, _pinHspiMiso, _pinHspiMosi);
        PinExpander& exp = PinExpander::instance();
        static const char* expNames[] = {"general I/O", "transmission", "expansion",
                                          "expansion", "coils", "injectors"};
        bool enabled[] = {_expander0Enabled, _expander1Enabled, _expander2Enabled,
                         _expander3Enabled, _expander4Enabled, _expander5Enabled};
        for (uint8_t i = 0; i < SPI_EXP_MAX; i++) {
            if (enabled[i]) {
                if (!exp.begin(i, &hspi, _pinHspiCs, i))
                    Log.warn("ECU", "MCP23S17 #%d (%s) not detected", i, expNames[i]);
            } else {
                Log.info("ECU", "MCP23S17 #%d (%s) disabled via config", i, expNames[i]);
            }
        }

        // Attach shared interrupt line (open-drain wired-OR, all INTA → one GPIO)
        if (_pinSharedInt != 0xFF)
            exp.attachSharedInterrupt(_pinSharedInt);
    } else {
        Log.warn("ECU", "SPI expanders disabled — all MCP23S17 skipped");
    }

    // Initialize subsystems
    _crank->begin(PIN_CRANK, _crankTeeth, _crankMissing);
    _cam->setCrankSensor(_crank);
    if (_state.sequentialMode) {
        _cam->begin(PIN_CAM);
    }

    _ignition->begin(_state.numCylinders, _coilPins, _firingOrder);
    _injection->begin(_state.numCylinders, _injectorPins, _firingOrder);
    _fuel->begin();
    _alternator->begin(_pinAlternator);

    // Probe MCP3204 SPI ADC for MAP/TPS (priority over ADS1115 @ 0x49)
    if (_spiExpandersEnabled) {
        _mcp3204 = new MCP3204Reader();
        if (_mcp3204->begin(&hspi, _pinMcp3204Cs, 5.0f)) {
            _sensors->setMapTpsMCP3204(_mcp3204);
            Log.info("ECU", "MCP3204 @ SPI CS=%d found — MAP/TPS via SPI, GPIO %d/%d freed for OSS/TSS",
                     _pinMcp3204Cs, _sensors->getPin(2), _sensors->getPin(3));
        } else {
            delete _mcp3204;
            _mcp3204 = nullptr;
        }
    }
    // Fallback: probe second ADS1115 at 0x49 for MAP/TPS (only if MCP3204 not found)
    if (!_mcp3204 && _i2cEnabled) {
        _ads1115_2 = new ADS1115Reader();
        if (_ads1115_2->begin(0x49, GAIN_TWOTHIRDS, RATE_ADS1115_860SPS)) {
            _sensors->setMapTpsADS1115(_ads1115_2);
            Log.info("ECU", "ADS1115 @ 0x49 found — MAP/TPS via I2C, GPIO %d/%d freed for OSS/TSS",
                     _sensors->getPin(2), _sensors->getPin(3));
        } else {
            delete _ads1115_2;
            _ads1115_2 = nullptr;
            Log.warn("ECU", "No external MAP/TPS ADC found — using native ADC (GPIO %d/%d), OSS/TSS disabled",
                     _sensors->getPin(2), _sensors->getPin(3));
        }
    }

    _sensors->begin();

    // Wire manager pointers for virtual sensor sources (SRC_ENGINE_STATE, SRC_OUTPUT_STATE)
    _sensors->setEngineStatePtr(&_state);
    _sensors->setIgnitionManager(_ignition);
    _sensors->setInjectionManager(_injection);
    _sensors->setAlternatorControl(_alternator);

    // Fuel pump relay — prime on startup, then off until engine runs
    if (_spiExpandersEnabled && _expander0Enabled) {
        xPinMode(_pinFuelPump, OUTPUT);
        xDigitalWrite(_pinFuelPump, HIGH);
        _fuelPumpPriming = true;
        _fuelPumpRunning = true;
        _fuelPumpPrimeStart = millis();

        // Tach output
        xPinMode(_pinTachOut, OUTPUT);
        xDigitalWrite(_pinTachOut, LOW);

        // CEL / check engine light
        xPinMode(_pinCel, OUTPUT);
        xDigitalWrite(_pinCel, LOW);
    }

    // CJ125 wideband O2 controller — requires I2C (ADS1115) and expander #0 (SPI CS pins)
    if (_cj125Enabled && _i2cEnabled && _spiExpandersEnabled && _expander0Enabled) {
        // SPI chip selects via MCP23S17 #0 — deselect before init
        xPinMode(_pinCj125Ss1, OUTPUT);
        xDigitalWrite(_pinCj125Ss1, HIGH);
        xPinMode(_pinCj125Ss2, OUTPUT);
        xDigitalWrite(_pinCj125Ss2, HIGH);

        _ads1115 = new ADS1115Reader();
        _ads1115->begin(0x48);
        _sensors->setADS1115(_ads1115);

        _cj125 = new CJ125Controller(&SPI);
        _cj125->setADS1115(_ads1115);
        _cj125->begin(_pinCj125Ss1, _pinCj125Ss2,
                       _pinHeater1, _pinHeater2,
                       _pinCj125Ua1, _pinCj125Ua2);

        _sensors->setCJ125(_cj125);
        Log.info("ECU", "CJ125 wideband O2 enabled");
    }

    // Transmission controller — requires I2C (ADS1115 for TFT/MLPS) and MCP23S17 #1 (solenoids)
    if (_trans && _i2cEnabled && _spiExpandersEnabled && _expander1Enabled) {
        // ADS1115 shared: CJ125 uses CH0/CH1, transmission uses CH2/CH3
        if (!_ads1115) {
            _ads1115 = new ADS1115Reader();
            _ads1115->begin(0x48);
            _sensors->setADS1115(_ads1115);
        }
        _trans->setADS1115(_ads1115);
        // OSS/TSS: MAP/TPS pins available only if external ADC (MCP3204 or ADS1115) took over
        bool extAdc = _sensors->hasExternalMapTps();
        uint8_t ossPin = extAdc ? _sensors->getPin(2) : 0xFF;
        uint8_t tssPin = extAdc ? _sensors->getPin(3) : 0xFF;
        _trans->begin(_pinSsA, _pinSsB, _pinSsC, _pinSsD,
                      _pinTcc, _pinEpc, ossPin, tssPin);
        Log.info("ECU", "Transmission controller enabled: %s, OSS=%s, TSS=%s",
                 TransmissionManager::typeToString(_trans->getType()),
                 ossPin != 0xFF ? String(String("GPIO") + String(ossPin)).c_str() : "DISABLED",
                 tssPin != 0xFF ? String(String("GPIO") + String(tssPin)).c_str() : "DISABLED");
    } else if (_trans && (!_i2cEnabled || !_spiExpandersEnabled || !_expander1Enabled)) {
        Log.warn("ECU", "Transmission controller skipped — I2C or expander #1 disabled");
    }

    // Oil pressure via sensor descriptor (slot 7)
    _sensors->configureOilPressure(_oilPressureMode, _pinOilPressure, _oilPressureActiveLow,
                                    _oilPressureMinPsi, _oilPressureMaxPsi, _oilPressureMcpChannel);
    if (_oilPressureMode > 0) {
        Log.info("ECU", "Oil pressure: %s on %s",
                 _oilPressureMode == 1 ? "digital switch" : "analog sender",
                 (_oilPressureMode == 2 && _mcp3204 && _mcp3204->isReady())
                     ? String(String("MCP3204 CH") + String(_oilPressureMcpChannel)).c_str()
                     : String(String("GPIO ") + String(_pinOilPressure)).c_str());
    }

    // Custom pin manager (begin() called from main after loading config)
    _customPins = new CustomPinManager();
    _customPins->setSensorManager(_sensors);
    _customPins->setEngineState(&_state);

    // Sensor + fuel calc update task (10ms on Core 0)
    _tUpdate = new Task(10 * TASK_MILLISECOND, TASK_FOREVER, [this]() { update(); }, _ts, true);

    // Core 1 real-time task — disabled for Phase 1 (no engine connected)
    // xTaskCreatePinnedToCore(realtimeTask, "ecu_rt", 4096, this, 24, &_realtimeTaskHandle, 1);

    Log.info("ECU", "ECU started, %d cylinders", _state.numCylinders);
}

void ECU::update() {
    uint32_t t0 = micros();
    // Core 0: read sensors and run fuel/ignition calculations
    _sensors->update();
    uint32_t t1 = micros();
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

    // Pass engine running state to sensor manager (needed for rule evaluation)
    _sensors->setEngineRunning(_state.engineRunning);

    // Fuel pump priming logic
    updateFuelPump();

    // Oil pressure from sensor descriptor slot 7
    _state.oilPressurePsi = _sensors->getOilPressurePsi();
    _state.oilPressureLow = _sensors->isOilPressureLow();

    // Expander health check (every 100 cycles = ~1s at 10ms)
    if (++_expanderHealthCounter >= 100) {
        _expanderHealthCounter = 0;
        checkExpanderHealth();
    }

    // CLT-dependent rev limit (before limp — limp has its own limit)
    if (!_limpActive && _cltRevLimitTable && _cltRevLimitTable->isInitialized()) {
        uint16_t cltLimit = (uint16_t)_cltRevLimitTable->lookup(_state.coolantTempF);
        uint16_t configLimit = _ignition->getConfigRevLimit();
        _ignition->setRevLimit(min(cltLimit, configLimit));
    }

    // Limp mode check (before fuel calc to set rev limit)
    checkLimpMode();

    // Run fuel calculation (updates sparkAdvanceDeg, injPulseWidthUs, targetAfr)
    _fuel->update(_state);

    // Cap advance if limp active
    if (_limpActive) {
        _state.sparkAdvanceDeg = constrain(_state.sparkAdvanceDeg, -10.0f, _limpAdvanceCap);
    }

    // Copy subsystem state to shared EngineState
    _state.overdwellCount = _ignition->getOverdwellCount();
    _state.aseActive = _fuel->isAseActive();
    _state.asePct = _fuel->getAsePct();
    _state.dfcoActive = _fuel->isDfcoActive();

    // Push calculated values to ignition and injection
    _ignition->setAdvance(_state.sparkAdvanceDeg);
    _injection->setPulseWidthUs(_state.injPulseWidthUs);

    // Update per-cylinder trims
    for (uint8_t i = 0; i < _state.numCylinders; i++) {
        _state.injTrim[i] = _injection->getTrim(i);
    }

    // Alternator control (100ms effective via PID internal dt)
    _alternator->update(_state.batteryVoltage);

    // Transmission control
    if (_trans) {
        _trans->update(_state.rpm, _state.tps, _state.batteryVoltage);
    }
    // Custom pin I/O and output rules
    if (_customPins) {
        _customPins->update();
    }
    uint32_t t2 = micros();
    _updateTimeUs = t2 - t0;
    _sensorTimeUs = t1 - t0;
}

void ECU::checkLimpMode() {
    // Rules engine separates limp (rev limit + advance cap) from CEL-only faults
    uint8_t faults = _sensors->getLimpFaults();
    if (_expanderFaults) faults |= SensorManager::FAULT_EXPANDER;
    uint8_t celOnly = _sensors->getCelFaults();

    // --- Limp mode logic (rev limit, advance cap, trans lock) ---
    if (faults != 0) {
        _limpRecoveryStart = 0;  // reset recovery timer
        if (!_limpActive) {
            _limpActive = true;
            _normalRevLimit = _ignition->getRevLimit();
            _ignition->setRevLimit(_limpRevLimit);
            if (_trans) _trans->setLimpMode(true);
            Log.warn("ECU", "LIMP MODE: faults=0x%02X", faults);
            if (_faultCb) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Sensor fault 0x%02X", faults);
                _faultCb("LIMP", msg, true);
            }
        }
        _limpFaults = faults;
    } else if (_limpActive) {
        // All limp faults clear — run recovery timer
        if (_limpRecoveryStart == 0) {
            _limpRecoveryStart = millis();
        } else if (millis() - _limpRecoveryStart >= _limpRecoveryMs) {
            _limpActive = false;
            _limpFaults = 0;
            _ignition->setRevLimit(_normalRevLimit);
            if (_trans) _trans->setLimpMode(false);
            Log.info("ECU", "LIMP MODE cleared — sensors recovered");
            if (_faultCb) _faultCb("LIMP", "All sensors recovered", false);
        }
    }

    // --- CEL control: ON for any limp OR cel-only fault, OFF when both clear ---
    _celFaults = celOnly;
    bool celNeeded = (_limpActive || celOnly != 0);
    if (_i2cEnabled && _expander0Enabled)
        xDigitalWrite(_pinCel, celNeeded ? HIGH : LOW);

    // Update shared state
    _state.limpMode = _limpActive;
    _state.limpFaults = _limpFaults;
    _state.celFaults = _celFaults;
}

void ECU::checkExpanderHealth() {
    _expanderFaults = PinExpander::instance().healthCheck();
    _state.expanderFaults = _expanderFaults;
}

void ECU::updateFuelPump() {
    if (!_i2cEnabled || !_expander0Enabled) return;
    if (_fuelPumpPriming) {
        if (millis() - _fuelPumpPrimeStart >= _fuelPumpPrimeMs) {
            _fuelPumpPriming = false;
            if (!_state.engineRunning) {
                xDigitalWrite(_pinFuelPump, LOW);
                _fuelPumpRunning = false;
            }
        }
    } else if (_state.engineRunning && !_fuelPumpRunning) {
        xDigitalWrite(_pinFuelPump, HIGH);
        _fuelPumpRunning = true;
    } else if (!_state.engineRunning && !_fuelPumpPriming && _fuelPumpRunning) {
        xDigitalWrite(_pinFuelPump, LOW);
        _fuelPumpRunning = false;
    }
    _state.fuelPumpOn = _fuelPumpRunning;
    _state.fuelPumpPriming = _fuelPumpPriming;
}

// Oil pressure is now handled by SensorManager descriptor slot 7 + fault rules

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
