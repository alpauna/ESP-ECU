#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <SD.h>
#include "ArduinoJson.h"
#include "mbedtls/base64.h"
#include "mbedtls/gcm.h"

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
    uint8_t pinHspiCsCoils;         // SPI — MCP23S17 coils CS (default 13)
    uint8_t pinHspiCsInj;           // SPI — MCP23S17 injectors CS (default 14)
    uint8_t pinMcp3204Cs;           // SPI — MCP3204 ADC CS (default 15)
    uint8_t pinI2cSda;              // I2C — SDA (default 0)
    uint8_t pinI2cScl;              // I2C — SCL (default 42)
    // Safe mode / peripheral control
    bool forceSafeMode;             // One-shot: enter safe mode on next reboot (default false)
    bool i2cEnabled;                // Master I2C bus enable (default true)
    bool spiExpandersEnabled;       // HSPI MCP23S17 bus enable (default true)
    bool expander0Enabled;          // MCP23017 #0 @ 0x20 (default true)
    bool expander1Enabled;          // MCP23017 #1 @ 0x21 (default true)
    bool expander2Enabled;          // MCP23017 #2 @ 0x22 (default true)
    bool expander3Enabled;          // MCP23017 #3 @ 0x23 (default true)
    bool spiExp0Enabled;            // MCP23S17 #0 — coils (default true)
    bool spiExp1Enabled;            // MCP23S17 #1 — injectors (default true)
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
