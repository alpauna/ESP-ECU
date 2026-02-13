#ifndef WEBHANDLER_H
#define WEBHANDLER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Update.h>
#include <SD.h>
#include <WiFi.h>
#include <time.h>
#include <functional>
#include <TaskSchedulerDeclarations.h>
#include "Logger.h"
#include "Config.h"

class ECU;

class WebHandler {
public:
    WebHandler(uint16_t port, Scheduler* ts);
    void begin();
    void startNtpSync();
    void setTimezone(int32_t gmtOffset, int32_t daylightOffset);
    void setConfig(Config* config) { _config = config; }
    void setECU(ECU* ecu) { _ecu = ecu; }
    bool shouldReboot() const { return _shouldReboot; }
    const char* getWiFiIP();

    typedef std::function<void(int)> FtpEnableCallback;
    typedef std::function<void()> FtpDisableCallback;
    typedef std::function<String()> FtpStatusCallback;
    void setFtpControl(FtpEnableCallback enableCb, FtpDisableCallback disableCb, FtpStatusCallback statusCb);
    void setFtpState(bool* activePtr, unsigned long* stopTimePtr);

private:
    AsyncWebServer _server;
    AsyncWebSocket _ws;

    Scheduler* _ts;
    ECU* _ecu;
    Config* _config;

    bool _shouldReboot;
    Task* _tDelayedReboot;
    bool _ntpSynced;
    Task* _tNtpSync;

    static constexpr float MB_MULTIPLIER = 1.0f / (1024.0f * 1024.0f);

    static constexpr const char* NTP_SERVER1 = "192.168.0.1";
    static constexpr const char* NTP_SERVER2 = "time.nist.gov";
    int32_t _gmtOffsetSec = -21600;
    int32_t _daylightOffsetSec = 3600;
    static constexpr const char* NOT_AVAILABLE = "NA";
    String _wifiIPStr;

    FtpEnableCallback _ftpEnableCb;
    FtpDisableCallback _ftpDisableCb;
    FtpStatusCallback _ftpStatusCb;
    bool* _ftpActivePtr = nullptr;
    unsigned long* _ftpStopTimePtr = nullptr;

    File _otaFile;
    bool _otaUploadOk = false;

    String _wifiTestState = "idle";
    String _wifiTestMessage;
    String _wifiTestNewSSID;
    String _wifiTestNewPassword;
    String _wifiOldSSID;
    String _wifiOldPassword;
    Task* _tWifiTest = nullptr;
    uint8_t _wifiTestCountdown = 0;

    bool checkAuth(AsyncWebServerRequest* request);
    void syncNtpTime();
    void setupRoutes();
    void serveFile(AsyncWebServerRequest* request, const String& path);
    static const char* getContentType(const String& path);
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                   AwsEventType type, void* arg, uint8_t* data, size_t len);
};

#endif
