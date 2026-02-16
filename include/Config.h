#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <SD.h>
#include "ArduinoJson.h"
#include "mbedtls/base64.h"
#include "mbedtls/gcm.h"
#include "SensorDescriptor.h"

struct ProjectInfo {
    String name;
    String createdOnDate;
    String description;
    uint32_t maxLogSize;
    uint8_t maxOldLogCount;
    int32_t gmtOffsetSec;
    int32_t daylightOffsetSec;
    uint32_t apFallbackSeconds;
    String theme;
    // Engine
    uint8_t cylinders;
    uint8_t firingOrder[12];
    uint8_t crankTeeth;
    uint8_t crankMissing;
    bool hasCamSensor;
    uint16_t displacement;
    float injectorFlowCcMin;
    float injectorDeadTimeMs;
    uint16_t revLimitRpm;
    float maxDwellMs;
    // Alternator
    float altTargetVoltage;
    float altPidP;
    float altPidI;
    float altPidD;
    // MAP
    float mapVoltageMin;
    float mapVoltageMax;
    float mapPressureMinKpa;
    float mapPressureMaxKpa;
    // O2
    float o2AfrAt0v;
    float o2AfrAt5v;
    uint16_t closedLoopMinRpm;
    uint16_t closedLoopMaxRpm;
    float closedLoopMaxMapKpa;
    bool cj125Enabled;
    // Pin assignments (configurable, requires reboot)
    uint8_t pinO2Bank1;             // ADC — O2 bank 1 (default 3)
    uint8_t pinO2Bank2;             // ADC — O2 bank 2 (default 4)
    uint8_t pinMap;                 // ADC — MAP sensor (default 5)
    uint8_t pinTps;                 // ADC — TPS sensor (default 6)
    uint8_t pinClt;                 // ADC — coolant temp (default 7)
    uint8_t pinIat;                 // ADC — intake air temp (default 8)
    uint8_t pinVbat;                // ADC — battery voltage (default 9)
    uint8_t pinAlternator;          // PWM — alternator field (default 41)
    uint8_t pinHeater1;             // PWM — CJ125 heater bank 1 (default 19)
    uint8_t pinHeater2;             // PWM — CJ125 heater bank 2 (default 20)
    uint8_t pinTcc;                 // PWM — torque converter clutch (default 45)
    uint8_t pinEpc;                 // PWM — electronic pressure control (default 46)
    uint8_t pinHspiSck;             // SPI — HSPI clock (default 10)
    uint8_t pinHspiMosi;            // SPI — HSPI MOSI (default 11)
    uint8_t pinHspiMiso;            // SPI — HSPI MISO (default 12)
    uint8_t pinHspiCs;              // SPI — shared MCP23S17 CS (default 13)
    uint8_t pinMcp3204Cs;           // SPI — MCP3204 ADC CS (default 15)
    uint8_t pinI2cSda;              // I2C — SDA (default 0)
    uint8_t pinI2cScl;              // I2C — SCL (default 42)
    // Expander outputs (configurable, default on MCP23S17 #0)
    // uint16_t for expander pins — values 200-295 exceed uint8_t max
    uint16_t pinFuelPump;           // MCP23S17 #0 P0 (default 200)
    uint16_t pinTachOut;            // MCP23S17 #0 P1 (default 201)
    uint16_t pinCel;                // MCP23S17 #0 P2 (default 202)
    uint16_t pinCj125Ss1;           // MCP23S17 #0 P8 — CJ125 Bank 1 CS (default 208)
    uint16_t pinCj125Ss2;           // MCP23S17 #0 P9 — CJ125 Bank 2 CS (default 209)
    // Transmission solenoids (MCP23S17 #1)
    uint16_t pinSsA;                // MCP23S17 #1 P0 (default 216)
    uint16_t pinSsB;                // MCP23S17 #1 P1 (default 217)
    uint16_t pinSsC;                // MCP23S17 #1 P2 — 4R100 only (default 218)
    uint16_t pinSsD;                // MCP23S17 #1 P3 — 4R100 only (default 219)
    // SD card SPI (stored in NVS for bootstrap)
    uint8_t pinSdClk;              // default 47
    uint8_t pinSdMiso;             // default 48
    uint8_t pinSdMosi;             // default 38
    uint8_t pinSdCs;               // default 39
    // Shared expander interrupt GPIO (all 6 INTA → one ESP32 GPIO via open-drain wired-OR)
    uint8_t pinSharedInt;           // default 0xFF (not connected)
    // Dynamic arrays (sized by proj.cylinders)
    uint16_t coilPins[12];         // default {264,265,...271,0,0,0,0} (MCP23S17 #4)
    uint16_t injectorPins[12];     // default {280,281,...287,0,0,0,0} (MCP23S17 #5)
    // Safe mode / peripheral control
    bool forceSafeMode;             // One-shot: enter safe mode on next reboot (default false)
    bool i2cEnabled;                // Master I2C bus enable — ADS1115 only (default true)
    bool spiExpandersEnabled;       // HSPI MCP23S17 bus enable (default true)
    bool expander0Enabled;          // MCP23S17 #0 — general I/O (default true)
    bool expander1Enabled;          // MCP23S17 #1 — transmission (default true)
    bool expander2Enabled;          // MCP23S17 #2 — expansion (default true)
    bool expander3Enabled;          // MCP23S17 #3 — expansion (default true)
    bool expander4Enabled;          // MCP23S17 #4 — coils (default true)
    bool expander5Enabled;          // MCP23S17 #5 — injectors (default true)
    // Transmission
    uint8_t transType;              // 0=NONE, 1=4R70W, 2=4R100
    uint16_t upshift12Rpm;          // default 1500
    uint16_t upshift23Rpm;          // default 2500
    uint16_t upshift34Rpm;          // default 3000
    uint16_t downshift21Rpm;        // default 1200
    uint16_t downshift32Rpm;        // default 1800
    uint16_t downshift43Rpm;        // default 2200
    uint16_t tccLockRpm;            // default 1500
    uint8_t tccLockGear;            // default 3
    float tccApplyRate;             // default 5.0 (%/100ms)
    float epcBaseDuty;              // default 50.0%
    float epcShiftBoost;            // default 80.0%
    uint16_t shiftTimeMs;           // default 500
    float maxTftTempF;              // default 275.0
    // Limp mode
    uint16_t limpRevLimit;          // Rev limit in limp (default 3000)
    float limpAdvanceCap;           // Max advance in limp deg (default 10.0)
    uint32_t limpRecoveryMs;        // Recovery delay ms (default 5000)
    float limpMapMin;               // MAP fault min kPa (default 5.0)
    float limpMapMax;               // MAP fault max kPa (default 120.0)
    float limpTpsMin;               // TPS fault min % (default -5.0)
    float limpTpsMax;               // TPS fault max % (default 105.0)
    float limpCltMax;               // CLT fault max F (default 280.0)
    float limpIatMax;               // IAT fault max F (default 200.0)
    float limpVbatMin;              // VBAT fault min V (default 10.0)
    // Oil pressure
    uint8_t oilPressureMode;        // 0=disabled, 1=digital, 2=analog (default 0)
    uint8_t pinOilPressure;         // GPIO pin (default 0 = not set)
    bool oilPressureActiveLow;      // Digital: LOW = low pressure (default true)
    float oilPressureMinPsi;        // Analog: fault threshold PSI (default 10.0)
    float oilPressureMaxPsi;        // Analog: max PSI at 4.5V (default 100.0)
    uint8_t oilPressureMcpChannel;  // MCP3204 channel for analog (default 2)
    uint32_t oilPressureStartupMs;  // Delay after engine start before checking (default 3000)
    // Fuel pump priming
    uint32_t fuelPumpPrimeMs;       // Prime duration in ms (default 3000)
    // After-Start Enrichment
    float aseInitialPct;            // Initial enrichment % (default 35.0)
    uint32_t aseDurationMs;         // Decay duration ms (default 10000)
    float aseMinCltF;               // Only trigger ASE below this CLT (default 100.0)
    // DFCO
    uint16_t dfcoRpmThreshold;      // Min RPM to enable DFCO (default 2500, 0=disabled)
    float dfcoTpsThreshold;         // TPS below this = closed throttle (default 3.0)
    uint32_t dfcoEntryDelayMs;      // Delay before cutting fuel (default 500)
    uint16_t dfcoExitRpm;           // Resume fuel below this RPM (default 1800)
    float dfcoExitTps;              // Resume fuel above this TPS (default 5.0)
    // CLT-dependent rev limit (6-point curve)
    float cltRevLimitAxis[6];       // CLT in F
    float cltRevLimitValues[6];     // RPM limits
};

class Config {
public:
    Config();

    bool initSDCard(uint8_t csPin = SS);
    bool loadConfig(const char* filename, ProjectInfo& proj);
    bool saveConfig(const char* filename, ProjectInfo& proj);
    bool updateConfig(const char* filename, ProjectInfo& proj);

    String getWifiSSID() const { return _wifiSSID; }
    String getWifiPassword() const { return _wifiPassword; }
    IPAddress getMqttHost() const { return _mqttHost; }
    uint16_t getMqttPort() const { return _mqttPort; }
    String getMqttUser() const { return _mqttUser; }
    String getMqttPassword() const { return _mqttPassword; }

    void setWifiSSID(const String& ssid) { _wifiSSID = ssid; }
    void setWifiPassword(const String& password) { _wifiPassword = password; }
    void setMqttHost(const IPAddress& host) { _mqttHost = host; }
    void setMqttPort(uint16_t port) { _mqttPort = port; }
    void setMqttUser(const String& user) { _mqttUser = user; }
    void setMqttPassword(const String& password) { _mqttPassword = password; }

    bool hasAdminPassword() const { return _adminPassword.length() > 0; }
    void setAdminPassword(const String& plaintext);
    bool verifyAdminPassword(const String& plaintext) const;

    bool isSDCardInitialized() const { return _sdInitialized; }
    void setProjectInfo(ProjectInfo* proj) { _proj = proj; }
    ProjectInfo* getProjectInfo() { return _proj; }

    bool initEncryption();
    static bool isEncryptionReady() { return _encryptionReady; }
    static void setObfuscationKey(const String& key);
    static String encryptPassword(const String& plaintext);
    static String decryptPassword(const String& encrypted);

    // Sensor descriptor persistence
    bool loadSensorConfig(const char* filename, SensorDescriptor* desc, uint8_t maxDesc,
                          FaultRule* rules, uint8_t maxRules);
    bool saveSensorConfig(const char* filename, const SensorDescriptor* desc, uint8_t maxDesc,
                          const FaultRule* rules, uint8_t maxRules);

    // Custom pin persistence
    bool loadCustomPins(const char* filename, struct CustomPinDescriptor* pins, uint8_t maxPins,
                        struct OutputRule* rules, uint8_t maxRules);
    bool saveCustomPins(const char* filename, const struct CustomPinDescriptor* pins, uint8_t maxPins,
                        const struct OutputRule* rules, uint8_t maxRules);

    // Tune table persistence
    bool saveTuneData(const char* filename, const char* tableName,
                      const float* data, uint8_t rows, uint8_t cols,
                      const float* xAxis, const float* yAxis);
    bool loadTuneData(const char* filename, const char* tableName,
                      float* data, uint8_t rows, uint8_t cols,
                      float* xAxis, float* yAxis);

private:
    bool _sdInitialized;
    String _wifiSSID;
    String _wifiPassword;
    IPAddress _mqttHost;
    uint16_t _mqttPort;
    String _mqttUser;
    String _mqttPassword;
    String _adminPassword;
    ProjectInfo* _proj;

    static uint8_t _aesKey[32];
    static bool _encryptionReady;
    static String _obfuscationKey;
};

#endif
