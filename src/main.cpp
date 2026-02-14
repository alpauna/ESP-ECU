#include <Arduino.h>
#include <esp_freertos_hooks.h>
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
#include "FuelManager.h"
#include "TuneTable.h"
#include <esp_log.h>

#ifndef AP_PASSWORD
#error "AP_PASSWORD not defined — create secrets.ini with: -D AP_PASSWORD=\\\"yourpassword\\\""
#endif

#if CIRCULAR_BUFFER_INT_SAFE
#else
#error "Needs to set CIRCULAR_BUFFER_INT_SAFE"
#endif

extern const char compile_date[] = __DATE__ " " __TIME__;

// SD card SPI pins (ESP32-S3)
// GPIO45/46 are strapping pins, OK after boot. GPIO33-37 reserved by OPI PSRAM
static const uint8_t SD_CLK  = 47;
static const uint8_t SD_MISO = 48;
static const uint8_t SD_MOSI = 38;
static const uint8_t SD_CS   = 39;

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
    false                        // cj125Enabled
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

// Save config to SD every 5 minutes
Task tSaveConfig(5 * TASK_MINUTE, TASK_FOREVER, []() {
    config.updateConfig(_filename, proj);
    Log.debug("MAIN", "Config saved to SD");
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

static uint8_t _cpuLoadWarmup = 15;

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

    // WiFi
    WiFi.onEvent(onWiFiEvent);
    WiFi.begin(_WIFI_SSID, _WIFI_PASSWORD);

    // Config and web handler setup
    config.setProjectInfo(&proj);
    webHandler.setConfig(&config);
    webHandler.setECU(&ecu);
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
    mqttHandler.begin(_MQTT_HOST_DEFAULT, _MQTT_PORT, _MQTT_USER, _MQTT_PASSWORD);
    mqttHandler.setECU(&ecu);

    // Logger
    Log.setLevel(Logger::LOG_INFO);
    Log.setMqttClient(mqttHandler.getClient(), "ecu/log");
    Log.setLogFile("/log.txt", proj.maxLogSize, proj.maxOldLogCount);
    Log.info("MAIN", "Logger initialized");

    // Initialize ECU with config
    ecu.configure(proj);

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

    ecu.begin();

    // Suppress Wire I2C error spam globally — non-present I2C devices generate noise
    esp_log_level_set("Wire", ESP_LOG_NONE);

    // Enable tasks
    tPublishState.enable();
    tSaveConfig.enable();

    // CPU load monitoring
    esp_register_freertos_idle_hook_for_cpu(idleHookCore0, 0);
    esp_register_freertos_idle_hook_for_cpu(idleHookCore1, 1);
    tCpuLoad.enable();

    Log.info("MAIN", "ESP-ECU started - %d cylinders, %d-%d trigger",
             proj.cylinders, proj.crankTeeth, proj.crankMissing);
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

    bool bIdle = ts.execute();
    if (bIdle) {
        vTaskDelay(1); // Yield to FreeRTOS so idle hooks fire
    }
}
