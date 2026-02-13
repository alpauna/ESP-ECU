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
