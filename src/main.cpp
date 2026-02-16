#include <Arduino.h>
#include <esp_freertos_hooks.h>
#include <esp_system.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <SimpleFTPServer.h>
#include <TaskSchedulerDeclarations.h>
#include "Logger.h"
#include "Config.h"
#include "WebHandler.h"
#include "MQTTHandler.h"
#include "ECU.h"
#include "CustomPin.h"
#include "SensorManager.h"
#include <Preferences.h>
#include "FuelManager.h"
#include "TuneTable.h"
#include <esp_log.h>

// Boot loop detection — persists across software/WDT/panic resets, NOT power-on
static RTC_NOINIT_ATTR uint32_t _bootCount;
static RTC_NOINIT_ATTR uint32_t _bootMagic;
static const uint32_t BOOT_MAGIC = 0xDEADBEEF;
static const uint32_t BOOT_LOOP_THRESHOLD = 3;

// Safe mode flag — global, accessible from WebHandler
bool _safeMode = false;
uint32_t _bootCountExposed = 0;
const char* _resetReasonStr = "UNKNOWN";

static const char* getResetReasonStr(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:  return "POWERON";
        case ESP_RST_SW:       return "SOFTWARE";
        case ESP_RST_PANIC:    return "PANIC";
        case ESP_RST_INT_WDT:  return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT:      return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO:     return "SDIO";
        default:               return "UNKNOWN";
    }
}

#ifndef AP_PASSWORD
#error "AP_PASSWORD not defined — create secrets.ini with: -D AP_PASSWORD=\\\"yourpassword\\\""
#endif

#if CIRCULAR_BUFFER_INT_SAFE
#else
#error "Needs to set CIRCULAR_BUFFER_INT_SAFE"
#endif

extern const char compile_date[] = __DATE__ " " __TIME__;

// SD card SPI pins — read from NVS on boot (bootstrap before SD card init)
static uint8_t SD_CLK  = 47;
static uint8_t SD_MISO = 48;
static uint8_t SD_MOSI = 38;
static uint8_t SD_CS   = 39;

// Config and networking
Config config;
const char* _filename = "/config.txt";
IPAddress _MQTT_HOST_DEFAULT(192, 168, 0, 46);
uint16_t _MQTT_PORT = 1883;
String _MQTT_USER = "debian";
String _MQTT_PASSWORD = "";
String _WIFI_SSID = "";
String _WIFI_PASSWORD = "";

// FTP server
FtpServer ftpSrv;
bool ftpActive = false;
unsigned long ftpStopTime = 0;

// Scheduler
Scheduler ts;

// CPU load monitoring via FreeRTOS idle hooks
static volatile uint32_t _idleCountCore0 = 0;
static volatile uint32_t _idleCountCore1 = 0;
static uint32_t _maxIdleCore0 = 0;
static uint32_t _maxIdleCore1 = 0;
static uint8_t _cpuLoadCore0 = 0;
static uint8_t _cpuLoadCore1 = 0;

static bool idleHookCore0() { _idleCountCore0++; return false; }
static bool idleHookCore1() { _idleCountCore1++; return false; }

uint8_t getCpuLoadCore0() { return _cpuLoadCore0; }
uint8_t getCpuLoadCore1() { return _cpuLoadCore1; }

// WiFi AP fallback
bool _apModeActive = false;
static uint32_t _wifiDisconnectCount = 0;

// ProjectInfo with ECU defaults
ProjectInfo proj = {
    "ESP-ECU",                  // name
    compile_date,               // createdOnDate
    "ESP32-S3 Engine Control Unit", // description
    50 * 1024 * 1024,           // maxLogSize: 50MB
    10,                         // maxOldLogCount
    -21600,                     // gmtOffsetSec: UTC-6
    3600,                       // daylightOffsetSec: 1hr DST
    600,                        // apFallbackSeconds: 10 min
    "dark",                     // theme
    // Engine defaults
    8,                          // cylinders
    {1,8,4,3,6,5,7,2,0,0,0,0}, // firingOrder (SBC V8)
    36,                         // crankTeeth
    1,                          // crankMissing
    true,                       // hasCamSensor
    5700,                       // displacement cc
    240.0f,                     // injectorFlowCcMin
    1.0f,                       // injectorDeadTimeMs
    6000,                       // revLimitRpm
    4.0f,                       // maxDwellMs
    // Alternator
    13.6f,                      // altTargetVoltage
    10.0f,                      // altPidP
    5.0f,                       // altPidI
    0.0f,                       // altPidD
    // MAP sensor
    0.5f,                       // mapVoltageMin
    4.5f,                       // mapVoltageMax
    10.0f,                      // mapPressureMinKpa
    105.0f,                     // mapPressureMaxKpa
    // O2
    10.0f,                      // o2AfrAt0v
    20.0f,                      // o2AfrAt5v
    800,                        // closedLoopMinRpm
    4000,                       // closedLoopMaxRpm
    80.0f,                       // closedLoopMaxMapKpa
    false,                       // cj125Enabled
    // Pin assignments (defaults)
    3,                           // pinO2Bank1
    4,                           // pinO2Bank2
    5,                           // pinMap
    6,                           // pinTps
    7,                           // pinClt
    8,                           // pinIat
    9,                           // pinVbat
    41,                          // pinAlternator
    19,                          // pinHeater1
    20,                          // pinHeater2
    45,                          // pinTcc
    46,                          // pinEpc
    10,                          // pinHspiSck
    11,                          // pinHspiMosi
    12,                          // pinHspiMiso
    13,                          // pinHspiCs (shared CS for all MCP23S17)
    15,                          // pinMcp3204Cs
    0,                           // pinI2cSda
    42,                          // pinI2cScl
    // Expander outputs (MCP23S17 #0)
    200,                         // pinFuelPump
    201,                         // pinTachOut
    202,                         // pinCel
    208,                         // pinCj125Ss1
    209,                         // pinCj125Ss2
    // Transmission solenoids (MCP23S17 #1)
    216,                         // pinSsA
    217,                         // pinSsB
    218,                         // pinSsC
    219,                         // pinSsD
    // SD card SPI
    47,                          // pinSdClk
    48,                          // pinSdMiso
    38,                          // pinSdMosi
    39,                          // pinSdCs
    // Shared expander interrupt GPIO (0xFF = not connected)
    0xFF,                        // pinSharedInt
    // Coil pins — MCP23S17 #4 (8 active + 4 spare)
    {264,265,266,267,268,269,270,271,0,0,0,0},
    // Injector pins — MCP23S17 #5 (8 active + 4 spare)
    {280,281,282,283,284,285,286,287,0,0,0,0},
    // Safe mode / peripheral control
    false,                       // forceSafeMode
    true,                        // i2cEnabled
    true,                        // spiExpandersEnabled
    true,                        // expander0Enabled
    true,                        // expander1Enabled
    true,                        // expander2Enabled
    true,                        // expander3Enabled
    true,                        // expander4Enabled
    true,                        // expander5Enabled
    // Transmission (defaults loaded from config)
    0,                           // transType (NONE)
    1500, 2500, 3000,            // upshift12/23/34Rpm
    1200, 1800, 2200,            // downshift21/32/43Rpm
    1500,                        // tccLockRpm
    3,                           // tccLockGear
    5.0f,                        // tccApplyRate
    50.0f,                       // epcBaseDuty
    80.0f,                       // epcShiftBoost
    500,                         // shiftTimeMs
    275.0f,                      // maxTftTempF
    // Limp mode
    3000,                        // limpRevLimit
    10.0f,                       // limpAdvanceCap
    5000,                        // limpRecoveryMs
    5.0f,                        // limpMapMin
    120.0f,                      // limpMapMax
    -5.0f,                       // limpTpsMin
    105.0f,                      // limpTpsMax
    280.0f,                      // limpCltMax
    200.0f,                      // limpIatMax
    10.0f,                       // limpVbatMin
    // Oil pressure
    0,                           // oilPressureMode (disabled)
    0,                           // pinOilPressure
    true,                        // oilPressureActiveLow
    10.0f,                       // oilPressureMinPsi
    100.0f,                      // oilPressureMaxPsi
    2,                           // oilPressureMcpChannel
    3000,                        // oilPressureStartupMs
    // Fuel pump priming
    3000,                        // fuelPumpPrimeMs
    // ASE
    35.0f,                       // aseInitialPct
    10000,                       // aseDurationMs
    100.0f,                      // aseMinCltF
    // DFCO
    2500,                        // dfcoRpmThreshold
    3.0f,                        // dfcoTpsThreshold
    500,                         // dfcoEntryDelayMs
    1800,                        // dfcoExitRpm
    5.0f,                        // dfcoExitTps
    // CLT rev limit
    {32, 60, 100, 140, 180, 220},    // cltRevLimitAxis
    {3000, 3500, 4500, 5500, 6000, 6000}, // cltRevLimitValues
    // Board diagnostics
    false,                       // diagEnabled
    {203, 204, 205, 206},        // diagMuxSelPins
    207                          // diagMuxEnPin
};

// Core objects
ECU ecu(&ts);
WebHandler webHandler(80, &ts);
MQTTHandler mqttHandler(&ts);

// Forward declarations
void onCalcCpuLoad();
void startAPMode();

// Tasks
Task tWaitOnWiFi(TASK_SECOND, 60, []() {
    Serial.print(".");
}, &ts, false,
[]() -> bool {
    if (WiFi.isConnected()) return false;
    mqttHandler.disconnect();
    return true;
},
[]() {
    Serial.println();
    if (WiFi.isConnected()) {
        _wifiDisconnectCount = 0;
        Log.info("WiFi", "IP: %s", WiFi.localIP().toString().c_str());
    } else {
        _wifiDisconnectCount += 60;
        Log.warn("WiFi", "Connection timed out (%lu/%lu sec)",
                 _wifiDisconnectCount, proj.apFallbackSeconds);
        if (_wifiDisconnectCount >= proj.apFallbackSeconds) {
            startAPMode();
            return;
        }
    }
    mqttHandler.startReconnect();
});

Task tCpuLoad(TASK_SECOND, TASK_FOREVER, &onCalcCpuLoad, &ts, false);

// Publish ECU state via MQTT every 500ms
Task tPublishState(500 * TASK_MILLISECOND, TASK_FOREVER, []() {
    mqttHandler.publishState();
}, &ts, false);

// Diagnostic health via MQTT every 5 seconds
Task tPublishDiag(5 * TASK_SECOND, TASK_FOREVER, []() {
    mqttHandler.publishDiag();
}, &ts, false);

// WiFi signal strength monitor
Task tWifiSignal(TASK_MINUTE, TASK_FOREVER, []() {
    if (WiFi.status() == WL_CONNECTED) {
        int32_t rssi = WiFi.RSSI();
        const char* quality = rssi >= -50 ? "Excellent" : rssi >= -60 ? "Good" : rssi >= -70 ? "Fair" : rssi >= -80 ? "Weak" : "Very Weak";
        Serial.printf("[WIFI] RSSI: %d dBm (%s) | IP: %s | CH: %d\n", rssi, quality, WiFi.localIP().toString().c_str(), WiFi.channel());
    } else {
        Serial.printf("[WIFI] Disconnected\n");
    }
}, &ts, true);

// Save config to SD every 5 minutes
Task tSaveConfig(5 * TASK_MINUTE, TASK_FOREVER, []() {
    config.updateConfig(_filename, proj);
    // Also save sensor descriptors and fault rules
    if (!_safeMode) {
        SensorManager* sm = ecu.getSensorManager();
        config.saveSensorConfig(_filename, sm->getDescriptor(0), MAX_SENSORS,
                                sm->getRule(0), MAX_RULES);
    }
    Log.debug("MAIN", "Config saved to SD");
}, &ts, false);

// Boot stable task — resets boot counter after 30s of stable runtime
Task tBootStable(30 * TASK_SECOND, TASK_ONCE, []() {
    _bootCount = 0;
    Serial.println("[BOOT] 30s stable — boot counter reset to 0");
}, &ts, false);

// WiFi event handler
void onWiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case SYSTEM_EVENT_STA_GOT_IP:
            _wifiDisconnectCount = 0;
            tWaitOnWiFi.disable();
            webHandler.startNtpSync();
            Log.info("WIFI", "Got ip: %s", webHandler.getWiFiIP());
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            if (_apModeActive) break;
            tWaitOnWiFi.enableDelayed();
            mqttHandler.stopReconnect();
            Log.warn("WIFI", "WiFi lost connection");
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            Serial.printf("WiFi Connected\n");
            mqttHandler.startReconnect();
            break;
    }
}

void startAPMode() {
    _apModeActive = true;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    const char* apSSID = "ESP-ECU";
    const char* apPass = AP_PASSWORD;
    WiFi.softAP(apSSID, apPass);
    IPAddress apIP = WiFi.softAPIP();
    Log.warn("WiFi", "AP MODE ACTIVE - SSID: %s IP: %s", apSSID, apIP.toString().c_str());
}

static uint8_t _cpuLoadWarmup = 30;

void onCalcCpuLoad() {
    uint32_t count0 = _idleCountCore0;
    uint32_t count1 = _idleCountCore1;
    _idleCountCore0 = 0;
    _idleCountCore1 = 0;

    if (_cpuLoadWarmup > 0) {
        _cpuLoadWarmup--;
        if (count0 > _maxIdleCore0) _maxIdleCore0 = count0;
        if (count1 > _maxIdleCore1) _maxIdleCore1 = count1;
        return;
    }

    if (count0 > _maxIdleCore0) _maxIdleCore0 = count0;
    if (count1 > _maxIdleCore1) _maxIdleCore1 = count1;

    uint8_t raw0 = (_maxIdleCore0 > 0) ? 100 - (count0 * 100 / _maxIdleCore0) : 0;
    uint8_t raw1 = (_maxIdleCore1 > 0) ? 100 - (count1 * 100 / _maxIdleCore1) : 0;

    // EMA smoothing: 25% new + 75% old
    _cpuLoadCore0 = (_cpuLoadCore0 * 3 + raw0 + 2) / 4;
    _cpuLoadCore1 = (_cpuLoadCore1 * 3 + raw1 + 2) / 4;
}

void setup() {
    Serial.begin(115200);

    // Boot loop detection
    esp_reset_reason_t resetReason = esp_reset_reason();
    _resetReasonStr = getResetReasonStr(resetReason);

    if (resetReason == ESP_RST_POWERON || _bootMagic != BOOT_MAGIC) {
        _bootCount = 0;
        _bootMagic = BOOT_MAGIC;
    }
    _bootCount++;
    _bootCountExposed = _bootCount;

    bool crashReset = (resetReason == ESP_RST_PANIC || resetReason == ESP_RST_INT_WDT ||
                       resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT);

    if (_bootCount > BOOT_LOOP_THRESHOLD && crashReset) {
        _safeMode = true;
        Serial.printf("\n*** SAFE MODE *** Boot loop detected: %lu boots, last reset: %s\n",
                      _bootCount, _resetReasonStr);
    } else {
        Serial.printf("[BOOT] Reset: %s, boot count: %lu\n", _resetReasonStr, _bootCount);
    }

    // Read SD card pins from NVS (bootstrap — config is ON the SD card)
    {
        Preferences prefs;
        prefs.begin("sdpins", true);  // read-only
        SD_CLK  = prefs.getUChar("clk",  47);
        SD_MISO = prefs.getUChar("miso", 48);
        SD_MOSI = prefs.getUChar("mosi", 38);
        SD_CS   = prefs.getUChar("cs",   39);
        prefs.end();
    }

    // Initialize SD card with custom SPI pins
    SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);

    // Set XOR obfuscation key for password encryption
    Config::setObfuscationKey(XOR_KEY);

    // Derive AES-256 key from eFuse HMAC
    if (!config.initEncryption()) {
        Serial.println("WARNING: eFuse HMAC key not available. Using XOR obfuscation.");
    }

    // Initialize config from SD card
    Serial.printf("SD SPI: CLK=%d MISO=%d MOSI=%d CS=%d\n", SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    if (config.initSDCard(SD_CS)) {
        Serial.println("SD Card initialized.");
        if (config.loadConfig(_filename, proj)) {
            _WIFI_SSID = config.getWifiSSID();
            _WIFI_PASSWORD = config.getWifiPassword();
            _MQTT_HOST_DEFAULT = config.getMqttHost();
            _MQTT_PORT = config.getMqttPort();
            _MQTT_USER = config.getMqttUser();
            _MQTT_PASSWORD = config.getMqttPassword();
        }
    } else {
        Serial.println("SD Card FAILED - check wiring to CLK=47 MISO=48 MOSI=38 CS=39");
    }

    // Check forceSafeMode from config
    if (proj.forceSafeMode && !_safeMode) {
        _safeMode = true;
        proj.forceSafeMode = false;  // One-shot — clear immediately
        if (config.isSDCardInitialized()) config.updateConfig(_filename, proj);
        Serial.println("*** SAFE MODE *** Forced via config flag");
    }

    // WiFi
    WiFi.onEvent(onWiFiEvent);
    WiFi.begin(_WIFI_SSID, _WIFI_PASSWORD);

    // Config and web handler setup
    config.setProjectInfo(&proj);
    webHandler.setConfig(&config);
    webHandler.setECU(_safeMode ? nullptr : &ecu);
    webHandler.setSafeMode(_safeMode);
    webHandler.setTimezone(proj.gmtOffsetSec, proj.daylightOffsetSec);

    bool sdCardReady = config.isSDCardInitialized();

    // FTP control callbacks
    webHandler.setFtpControl(
        [sdCardReady](int durationMin) {
            if (!sdCardReady) return;
            ftpSrv.begin("admin", "admin");
            ftpActive = true;
            ftpStopTime = millis() + ((unsigned long)durationMin * 60000UL);
            Log.info("FTP", "FTP enabled for %d minutes", durationMin);
        },
        []() {
            if (ftpActive) {
                ftpSrv.end();
                ftpActive = false;
                ftpStopTime = 0;
                Log.info("FTP", "FTP disabled");
            }
        },
        []() -> String {
            int remainingMin = 0;
            if (ftpActive && ftpStopTime > 0) {
                unsigned long now = millis();
                if (ftpStopTime > now) {
                    remainingMin = (int)((ftpStopTime - now) / 60000) + 1;
                }
            }
            return "{\"active\":" + String(ftpActive ? "true" : "false") +
                   ",\"remainingMinutes\":" + String(remainingMin) + "}";
        }
    );
    webHandler.setFtpState(&ftpActive, &ftpStopTime);

    // Start web server (HTTP only — no HTTPS for ECU)
    webHandler.begin();

    // MQTT
    if (!_safeMode) {
        mqttHandler.begin(_MQTT_HOST_DEFAULT, _MQTT_PORT, _MQTT_USER, _MQTT_PASSWORD);
        mqttHandler.setECU(&ecu);
    }

    // Logger
    Log.setLevel(Logger::LOG_INFO);
    if (!_safeMode) Log.setMqttClient(mqttHandler.getClient(), "ecu/log");
    Log.setLogFile("/log.txt", proj.maxLogSize, proj.maxOldLogCount);
    Log.info("MAIN", "Logger initialized%s", _safeMode ? " (SAFE MODE)" : "");

    if (!_safeMode) {
        // Initialize ECU with config
        ecu.configure(proj);

        // Pass peripheral flags to ECU
        ecu.setPeripheralFlags(proj);

        // Load tune tables from SD card (overrides defaults if files exist)
        {
            FuelManager* fuel = ecu.getFuelManager();
            const char* names[] = {"spark", "ve", "afr"};
            TuneTable3D* tables[] = {fuel->getSparkTable(), fuel->getVeTable(), fuel->getAfrTable()};
            for (int i = 0; i < 3; i++) {
                TuneTable3D* t = tables[i];
                if (!t || !t->isInitialized()) continue;
                uint8_t cols = t->getXSize(), rows = t->getYSize();
                float* data = new float[rows * cols];
                float* xAxis = new float[cols];
                float* yAxis = new float[rows];
                if (config.loadTuneData("/tune.json", names[i], data, rows, cols, xAxis, yAxis)) {
                    t->setXAxis(xAxis);
                    t->setYAxis(yAxis);
                    t->setValues(data);
                    Serial.printf("Loaded tune table: %s\n", names[i]);
                }
                delete[] data;
                delete[] xAxis;
                delete[] yAxis;
            }
        }

        // Load sensor descriptors and fault rules from config (overrides defaults if present)
        {
            SensorManager* sm = ecu.getSensorManager();
            if (!config.loadSensorConfig(_filename, sm->getDescriptor(0), MAX_SENSORS,
                                          sm->getRule(0), MAX_RULES)) {
                // No sensors[] in config — defaults from initDefaultDescriptors() are used
                // First updateConfig will write the new sensors[] array
            }
        }

        ecu.setFaultCallback([](const char* fault, const char* msg, bool active) {
            mqttHandler.publishFault(fault, msg, active);
        });

        ecu.begin();

        // Load custom pin descriptors and output rules
        {
            CustomPinManager* cpm = ecu.getCustomPins();
            if (cpm) {
                config.loadCustomPins("/custom-pins.json",
                    cpm->getDescriptor(0), MAX_CUSTOM_PINS,
                    cpm->getRule(0), MAX_OUTPUT_RULES);
                cpm->begin();
            }
        }

        // Suppress Wire I2C error spam globally — non-present I2C devices generate noise
        esp_log_level_set("Wire", ESP_LOG_NONE);

        // Enable tasks
        tPublishState.enable();
        tPublishDiag.enable();
    }

    tSaveConfig.enable();

    // CPU load monitoring
    esp_register_freertos_idle_hook_for_cpu(idleHookCore0, 0);
    esp_register_freertos_idle_hook_for_cpu(idleHookCore1, 1);
    tCpuLoad.enable();

    // Start boot stability timer — resets boot counter after 30s stable
    tBootStable.enableDelayed(30 * TASK_SECOND);

    if (_safeMode) {
        Log.warn("MAIN", "ESP-ECU SAFE MODE — peripherals disabled, web server active");
    } else {
        Log.info("MAIN", "ESP-ECU started - %d cylinders, %d-%d trigger",
                 proj.cylinders, proj.crankTeeth, proj.crankMissing);
    }
}

void loop() {
    if (webHandler.shouldReboot()) {
        Serial.println("Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP.restart();
    }

    // FTP auto-timeout
    if (ftpActive && ftpStopTime > 0 && millis() >= ftpStopTime) {
        ftpSrv.end();
        ftpActive = false;
        ftpStopTime = 0;
        Log.info("FTP", "FTP auto-disabled (timeout)");
    }
    if (ftpActive) ftpSrv.handleFTP();

    ts.execute();
    vTaskDelay(1);  // Always yield to FreeRTOS for AsyncWebServer + idle hooks
}
