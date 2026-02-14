#include "WebHandler.h"
#include "AsyncJson.h"
#include "ArduinoJson.h"
#include "ECU.h"
#include "CrankSensor.h"
#include "AlternatorControl.h"
#include "SensorManager.h"
#include "CJ125Controller.h"
#include "TransmissionManager.h"
#include "FuelManager.h"
#include "TuneTable.h"
#include "PinExpander.h"
#include "OtaUtils.h"

extern const char compile_date[];
extern uint8_t getCpuLoadCore0();
extern uint8_t getCpuLoadCore1();
extern bool _apModeActive;

WebHandler::WebHandler(uint16_t port, Scheduler* ts)
    : _server(port), _ws("/ws"), _ts(ts), _ecu(nullptr),
      _config(nullptr), _shouldReboot(false), _tDelayedReboot(nullptr),
      _ntpSynced(false), _tNtpSync(nullptr) {}

void WebHandler::setFtpControl(FtpEnableCallback enableCb, FtpDisableCallback disableCb, FtpStatusCallback statusCb) {
    _ftpEnableCb = enableCb;
    _ftpDisableCb = disableCb;
    _ftpStatusCb = statusCb;
}

void WebHandler::setFtpState(bool* activePtr, unsigned long* stopTimePtr) {
    _ftpActivePtr = activePtr;
    _ftpStopTimePtr = stopTimePtr;
}

bool WebHandler::checkAuth(AsyncWebServerRequest* request) {
    if (!_config || !_config->hasAdminPassword()) return true;
    String authHeader = request->header("Authorization");
    if (!authHeader.startsWith("Basic ")) {
        request->requestAuthentication(nullptr, false);
        return false;
    }
    String b64 = authHeader.substring(6);
    size_t decodedLen = 0;
    mbedtls_base64_decode(nullptr, 0, &decodedLen, (const uint8_t*)b64.c_str(), b64.length());
    uint8_t* decoded = new uint8_t[decodedLen + 1];
    mbedtls_base64_decode(decoded, decodedLen + 1, &decodedLen, (const uint8_t*)b64.c_str(), b64.length());
    decoded[decodedLen] = '\0';
    String credentials = String((char*)decoded);
    delete[] decoded;
    int colonIdx = credentials.indexOf(':');
    if (colonIdx < 0) { request->requestAuthentication(nullptr, false); return false; }
    String password = credentials.substring(colonIdx + 1);
    if (_config->verifyAdminPassword(password)) return true;
    request->requestAuthentication(nullptr, false);
    return false;
}

void WebHandler::begin() {
    _tNtpSync = new Task(2 * TASK_HOUR, TASK_FOREVER, [this]() { syncNtpTime(); }, _ts, false);

    _server.onNotFound([](AsyncWebServerRequest* request) { request->send(404); });
    _server.onFileUpload([](AsyncWebServerRequest*, String, size_t, uint8_t*, size_t, bool) {});
    _server.onRequestBody([](AsyncWebServerRequest*, uint8_t*, size_t, size_t, size_t) {});

    _ws.onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c, AwsEventType t, void* a, uint8_t* d, size_t l) {
        onWsEvent(s, c, t, a, d, l);
    });
    _server.addHandler(&_ws);
    Log.setWebSocket(&_ws);
    Log.enableWebSocket(true);

    setupRoutes();
    _server.begin();
    Log.info("HTTP", "HTTP server started");
}

const char* WebHandler::getWiFiIP() {
    if (!WiFi.isConnected()) return NOT_AVAILABLE;
    _wifiIPStr = WiFi.localIP().toString();
    return _wifiIPStr.length() > 0 ? _wifiIPStr.c_str() : NOT_AVAILABLE;
}

void WebHandler::startNtpSync() { if (_tNtpSync) _tNtpSync->enable(); }

void WebHandler::syncNtpTime() {
    if (WiFi.status() != WL_CONNECTED) return;
    Log.info("NTP", "Syncing time...");
    configTime(_gmtOffsetSec, _daylightOffsetSec, NTP_SERVER1, NTP_SERVER2);
    struct tm timeinfo;
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 10) { vTaskDelay(pdMS_TO_TICKS(1000)); retry++; }
    if (retry < 10) {
        _ntpSynced = true;
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        Log.info("NTP", "Time synced: %s", timeStr);
    } else {
        Log.error("NTP", "Failed to sync time");
    }
}

void WebHandler::onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client,
                           AwsEventType type, void*, uint8_t*, size_t) {
    if (type == WS_EVT_CONNECT) {
        String json = "{\"status\":\"connected\",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
        client->text(json);
    }
}

void WebHandler::setTimezone(int32_t gmtOffset, int32_t daylightOffset) {
    _gmtOffsetSec = gmtOffset;
    _daylightOffsetSec = daylightOffset;
}

const char* WebHandler::getContentType(const String& path) {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".ico")) return "image/x-icon";
    if (path.endsWith(".png")) return "image/png";
    return "text/plain";
}

void WebHandler::serveFile(AsyncWebServerRequest* request, const String& path) {
    if (!_config || !_config->isSDCardInitialized()) {
        request->send(503, "text/plain", "SD card not available");
        return;
    }
    String fullPath = "/www" + path;
    fs::File file = SD.open(fullPath.c_str(), FILE_READ);
    if (!file) { request->send(404, "text/plain", "Not found: " + path); return; }
    size_t fileSize = file.size();
    if (fileSize == 0) { file.close(); request->send(200, getContentType(path), ""); return; }
    char* buf = (char*)ps_malloc(fileSize + 1);
    if (!buf) { file.close(); request->send(500, "text/plain", "Out of memory"); return; }
    file.read((uint8_t*)buf, fileSize);
    buf[fileSize] = '\0';
    file.close();
    request->send(200, getContentType(path), buf);
    free(buf);
}

void WebHandler::setupRoutes() {
    // Static pages
    _server.on("/theme.css", HTTP_GET, [this](AsyncWebServerRequest* r) { serveFile(r, "/theme.css"); });
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* r) { serveFile(r, "/index.html"); });
    _server.on("/dashboard", HTTP_GET, [this](AsyncWebServerRequest* r) { serveFile(r, "/dashboard.html"); });
    _server.on("/tune", HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (!checkAuth(r)) return;
        if (!r->hasParam("table")) { serveFile(r, "/tune.html"); return; }
        if (!_ecu) { r->send(500, "application/json", "{\"error\":\"ECU not available\"}"); return; }
        String tableName = r->getParam("table")->value();
        FuelManager* fuel = _ecu->getFuelManager();
        TuneTable3D* table = nullptr;
        if (tableName == "spark") table = fuel->getSparkTable();
        else if (tableName == "ve") table = fuel->getVeTable();
        else if (tableName == "afr") table = fuel->getAfrTable();
        if (!table || !table->isInitialized()) {
            r->send(404, "application/json", "{\"error\":\"Table not found\"}");
            return;
        }
        JsonDocument doc;
        uint8_t cols = table->getXSize(), rows = table->getYSize();
        JsonArray rpmArr = doc["rpmAxis"].to<JsonArray>();
        for (uint8_t i = 0; i < cols; i++) rpmArr.add(table->getXAxisValue(i));
        JsonArray mapArr = doc["mapAxis"].to<JsonArray>();
        for (uint8_t i = 0; i < rows; i++) mapArr.add(table->getYAxisValue(i));
        JsonArray values = doc["values"].to<JsonArray>();
        for (uint8_t y = 0; y < rows; y++) {
            JsonArray row = values.add<JsonArray>();
            for (uint8_t x = 0; x < cols; x++)
                row.add(table->getValue(x, y));
        }
        String json;
        serializeJson(doc, json);
        r->send(200, "application/json", json);
    });
    auto* tunePostHandler = new AsyncCallbackJsonWebHandler("/tune", [this](AsyncWebServerRequest* request, JsonVariant& json) {
        if (!checkAuth(request)) return;
        if (!_ecu || !_config) { request->send(500, "application/json", "{\"error\":\"Not available\"}"); return; }
        JsonObject data = json.as<JsonObject>();
        String tableName = data["table"] | String("");
        FuelManager* fuel = _ecu->getFuelManager();
        TuneTable3D* table = nullptr;
        if (tableName == "spark") table = fuel->getSparkTable();
        else if (tableName == "ve") table = fuel->getVeTable();
        else if (tableName == "afr") table = fuel->getAfrTable();
        if (!table || !table->isInitialized()) {
            request->send(404, "application/json", "{\"error\":\"Table not found\"}");
            return;
        }
        uint8_t cols = table->getXSize(), rows = table->getYSize();
        JsonArray rpmArr = data["rpmAxis"];
        if (rpmArr) {
            float* xAxis = new float[cols];
            for (uint8_t i = 0; i < cols && i < rpmArr.size(); i++) xAxis[i] = rpmArr[i];
            table->setXAxis(xAxis);
            delete[] xAxis;
        }
        JsonArray mapArr = data["mapAxis"];
        if (mapArr) {
            float* yAxis = new float[rows];
            for (uint8_t i = 0; i < rows && i < mapArr.size(); i++) yAxis[i] = mapArr[i];
            table->setYAxis(yAxis);
            delete[] yAxis;
        }
        JsonArray values = data["values"];
        if (values) {
            for (uint8_t y = 0; y < rows && y < values.size(); y++) {
                JsonArray row = values[y];
                for (uint8_t x = 0; x < cols && x < row.size(); x++)
                    table->setValue(x, y, row[x]);
            }
        }
        // Persist to SD
        float* flatData = new float[rows * cols];
        float* xOut = new float[cols];
        float* yOut = new float[rows];
        for (uint8_t x = 0; x < cols; x++) xOut[x] = table->getXAxisValue(x);
        for (uint8_t y = 0; y < rows; y++) yOut[y] = table->getYAxisValue(y);
        for (uint8_t y = 0; y < rows; y++)
            for (uint8_t x = 0; x < cols; x++)
                flatData[y * cols + x] = table->getValue(x, y);
        bool saved = _config->saveTuneData("/tune.json", tableName.c_str(), flatData, rows, cols, xOut, yOut);
        delete[] flatData; delete[] xOut; delete[] yOut;
        if (saved) request->send(200, "application/json", "{\"status\":\"ok\"}");
        else request->send(500, "application/json", "{\"error\":\"Failed to save\"}");
    });
    _server.addHandler(tunePostHandler);
    _server.on("/log/view", HTTP_GET, [this](AsyncWebServerRequest* r) { serveFile(r, "/log.html"); });
    _server.on("/heap/view", HTTP_GET, [this](AsyncWebServerRequest* r) { serveFile(r, "/heap.html"); });

    // ECU state endpoint
    _server.on("/state", HTTP_GET, [this](AsyncWebServerRequest* request) {
        JsonDocument doc;
        if (_ecu) {
            const EngineState& s = _ecu->getState();
            doc["rpm"] = s.rpm;
            doc["map"] = s.mapKpa;
            doc["tps"] = s.tps;
            JsonArray afr = doc["afr"].to<JsonArray>();
            afr.add(s.afr[0]);
            afr.add(s.afr[1]);
            doc["clt"] = s.coolantTempF;
            doc["iat"] = s.iatTempF;
            doc["vbat"] = s.batteryVoltage;
            doc["advance"] = s.sparkAdvanceDeg;
            doc["pw"] = s.injPulseWidthUs;
            doc["state"] = _ecu->getStateString();
            doc["engineRunning"] = s.engineRunning;
            doc["cranking"] = s.cranking;
            doc["sequential"] = s.sequentialMode;
            doc["cylinders"] = s.numCylinders;
            // Per-cylinder trim
            JsonArray trim = doc["injTrim"].to<JsonArray>();
            for (uint8_t i = 0; i < s.numCylinders; i++) trim.add(s.injTrim[i]);

            // CJ125 wideband O2
            CJ125Controller* cj = _ecu->getCJ125();
            JsonObject cj125 = doc["cj125"].to<JsonObject>();
            cj125["enabled"] = (cj != nullptr);
            if (cj) {
                for (uint8_t b = 0; b < 2; b++) {
                    char key[6]; snprintf(key, sizeof(key), "bank%d", b + 1);
                    JsonObject bk = cj125[key].to<JsonObject>();
                    bk["lambda"] = cj->getLambda(b);
                    bk["afr"] = cj->getAfr(b);
                    bk["o2pct"] = cj->getOxygen(b);
                    bk["heaterState"] = cj->getHeaterStateStr(b);
                    bk["heaterDuty"] = cj->getHeaterDuty(b);
                    bk["ur"] = cj->getUrValue(b);
                    bk["ua"] = cj->getUaValue(b);
                    bk["diag"] = cj->getDiagStatus(b);
                    bk["ready"] = cj->isReady(b);
                }
            }

            // Transmission
            TransmissionManager* trans = _ecu->getTransmission();
            JsonObject transObj = doc["transmission"].to<JsonObject>();
            transObj["enabled"] = (trans != nullptr);
            if (trans) {
                const TransmissionState& ts = trans->getState();
                transObj["type"] = TransmissionManager::typeToString(trans->getType());
                transObj["gear"] = TransmissionManager::gearToString(ts.currentGear);
                transObj["targetGear"] = TransmissionManager::gearToString(ts.targetGear);
                transObj["mlps"] = TransmissionManager::mlpsToString(ts.mlpsPosition);
                transObj["ossRpm"] = ts.ossRpm;
                transObj["tssRpm"] = ts.tssRpm;
                transObj["tftTempF"] = ts.tftTempF;
                transObj["tccDuty"] = ts.tccDuty;
                transObj["epcDuty"] = ts.epcDuty;
                transObj["ssA"] = ts.ssA;
                transObj["ssB"] = ts.ssB;
                transObj["ssC"] = ts.ssC;
                transObj["ssD"] = ts.ssD;
                transObj["tccLocked"] = ts.tccLocked;
                transObj["shifting"] = ts.shifting;
                transObj["slipRpm"] = ts.slipRpm;
                transObj["overTemp"] = ts.overTemp;
            }
        }
        doc["cpuLoad0"] = getCpuLoadCore0();
        doc["cpuLoad1"] = getCpuLoadCore1();
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["wifiSSID"] = WiFi.SSID();
        doc["wifiRSSI"] = WiFi.RSSI();
        doc["wifiIP"] = WiFi.localIP().toString();
        doc["apMode"] = _apModeActive;
        doc["buildDate"] = compile_date;
        struct tm ti;
        if (getLocalTime(&ti, 0)) {
            char buf[20];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
            doc["datetime"] = buf;
        }
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // WiFi scan
    _server.on("/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
        String json = "[";
        int n = WiFi.scanComplete();
        if (n == -2) { WiFi.scanNetworks(true); }
        else if (n) {
            for (int i = 0; i < n; ++i) {
                if (i) json += ",";
                json += "{\"rssi\":" + String(WiFi.RSSI(i)) +
                        ",\"ssid\":\"" + WiFi.SSID(i) + "\"" +
                        ",\"channel\":" + String(WiFi.channel(i)) +
                        ",\"secure\":" + String(WiFi.encryptionType(i)) + "}";
            }
            WiFi.scanDelete();
            if (WiFi.scanComplete() == -2) WiFi.scanNetworks(true);
        }
        json += "]";
        request->send(200, "application/json", json);
    });

    // Heap/system
    _server.on("/heap", HTTP_GET, [](AsyncWebServerRequest* request) {
        String json = "{\"free heap\":" + String(ESP.getFreeHeap()) +
                      ",\"free psram MB\":" + String(ESP.getFreePsram() / (1024.0f * 1024.0f)) +
                      ",\"used psram MB\":" + String((ESP.getPsramSize() - ESP.getFreePsram()) / (1024.0f * 1024.0f)) +
                      ",\"cpuLoad0\":" + String(getCpuLoadCore0()) +
                      ",\"cpuLoad1\":" + String(getCpuLoadCore1()) + "}";
        request->send(200, "application/json", json);
    });

    // Theme
    _server.on("/theme", HTTP_GET, [this](AsyncWebServerRequest* request) {
        String theme = "dark";
        if (_config && _config->getProjectInfo()) {
            theme = _config->getProjectInfo()->theme;
            if (theme.length() == 0) theme = "dark";
        }
        request->send(200, "application/json", "{\"theme\":\"" + theme + "\"}");
    });

    // Log endpoints
    _server.on("/log/level", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send(200, "application/json", "{\"level\":" + String(Log.getLevel()) +
                ",\"levelName\":\"" + String(Log.getLevelName(Log.getLevel())) + "\"}");
    });
    _server.on("/log/level", HTTP_POST, [](AsyncWebServerRequest* r) {
        if (r->hasParam("level")) {
            int level = r->getParam("level")->value().toInt();
            if (level >= 0 && level <= 3) { Log.setLevel((Logger::Level)level); r->send(200, "application/json", "{\"status\":\"ok\"}"); }
            else r->send(400, "application/json", "{\"error\":\"level must be 0-3\"}");
        } else r->send(400, "application/json", "{\"error\":\"missing level param\"}");
    });

    _server.on("/log", HTTP_GET, [](AsyncWebServerRequest* request) {
        size_t count = Log.getRingBufferCount();
        size_t limit = count;
        if (request->hasParam("limit")) {
            limit = request->getParam("limit")->value().toInt();
            if (limit > count) limit = count;
        }
        const auto& buffer = Log.getRingBuffer();
        size_t head = Log.getRingBufferHead();
        size_t bufSize = buffer.size();
        size_t startOffset = count - limit;
        String json = "{\"count\":" + String(limit) + ",\"entries\":[";
        for (size_t i = 0; i < limit; i++) {
            size_t idx = (head + bufSize - count + startOffset + i) % bufSize;
            if (i > 0) json += ",";
            json += "\"";
            const String& entry = buffer[idx];
            for (size_t j = 0; j < entry.length(); j++) {
                char c = entry[j];
                switch (c) {
                    case '"': json += "\\\""; break;
                    case '\\': json += "\\\\"; break;
                    case '\n': json += "\\n"; break;
                    default: json += c; break;
                }
            }
            json += "\"";
        }
        json += "]}";
        request->send(200, "application/json", json);
    });

    _server.on("/log/config", HTTP_GET, [](AsyncWebServerRequest* r) {
        String json = "{\"level\":" + String(Log.getLevel()) +
                      ",\"serial\":" + String(Log.isSerialEnabled() ? "true" : "false") +
                      ",\"mqtt\":" + String(Log.isMqttEnabled() ? "true" : "false") +
                      ",\"sdcard\":" + String(Log.isSdCardEnabled() ? "true" : "false") +
                      ",\"websocket\":" + String(Log.isWebSocketEnabled() ? "true" : "false") + "}";
        r->send(200, "application/json", json);
    });
    _server.on("/log/config", HTTP_POST, [](AsyncWebServerRequest* r) {
        if (r->hasParam("serial")) Log.enableSerial(r->getParam("serial")->value() == "true");
        if (r->hasParam("mqtt")) Log.enableMqtt(r->getParam("mqtt")->value() == "true");
        if (r->hasParam("sdcard")) Log.enableSdCard(r->getParam("sdcard")->value() == "true");
        if (r->hasParam("websocket")) Log.enableWebSocket(r->getParam("websocket")->value() == "true");
        r->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // Admin setup
    _server.on("/admin/setup", HTTP_GET, [this](AsyncWebServerRequest* r) { serveFile(r, "/admin.html"); });
    auto* adminPostHandler = new AsyncCallbackJsonWebHandler("/admin/setup", [this](AsyncWebServerRequest* request, JsonVariant& json) {
        if (!_config) { request->send(500, "application/json", "{\"error\":\"Config not available\"}"); return; }
        if (_config->hasAdminPassword()) { request->send(400, "application/json", "{\"error\":\"Admin password already set.\"}"); return; }
        JsonObject data = json.as<JsonObject>();
        String pw = data["password"] | String("");
        String confirm = data["confirm"] | String("");
        if (pw.length() < 4) { request->send(400, "application/json", "{\"error\":\"Password must be at least 4 characters.\"}"); return; }
        if (pw != confirm) { request->send(400, "application/json", "{\"error\":\"Passwords do not match.\"}"); return; }
        _config->setAdminPassword(pw);
        if (_ftpDisableCb) _ftpDisableCb();
        ProjectInfo* proj = _config->getProjectInfo();
        _config->updateConfig("/config.txt", *proj);
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Admin password set.\"}");
    });
    _server.addHandler(adminPostHandler);

    // Config page
    _server.on("/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (_config && !_config->hasAdminPassword()) { request->redirect("/admin/setup"); return; }
        if (!checkAuth(request)) return;
        if (request->hasParam("format") && request->getParam("format")->value() == "json") {
            if (!_config || !_config->getProjectInfo()) { request->send(500); return; }
            ProjectInfo* proj = _config->getProjectInfo();
            JsonDocument doc;
            doc["wifiSSID"] = _config->getWifiSSID();
            doc["wifiPassword"] = "******";
            doc["mqttHost"] = _config->getMqttHost().toString();
            doc["mqttPort"] = _config->getMqttPort();
            doc["mqttUser"] = _config->getMqttUser();
            doc["mqttPassword"] = "******";
            doc["gmtOffsetHrs"] = proj->gmtOffsetSec / 3600.0f;
            doc["daylightOffsetHrs"] = proj->daylightOffsetSec / 3600.0f;
            doc["apFallbackMinutes"] = proj->apFallbackSeconds / 60;
            doc["maxLogSize"] = proj->maxLogSize;
            doc["maxOldLogCount"] = proj->maxOldLogCount;
            doc["adminPasswordSet"] = _config->hasAdminPassword();
            doc["theme"] = proj->theme.length() > 0 ? proj->theme : "dark";
            // Engine
            doc["cylinders"] = proj->cylinders;
            JsonArray fo = doc["firingOrder"].to<JsonArray>();
            for (uint8_t i = 0; i < proj->cylinders; i++) fo.add(proj->firingOrder[i]);
            doc["crankTeeth"] = proj->crankTeeth;
            doc["crankMissing"] = proj->crankMissing;
            doc["hasCamSensor"] = proj->hasCamSensor;
            doc["displacement"] = proj->displacement;
            doc["injectorFlowCcMin"] = proj->injectorFlowCcMin;
            doc["injectorDeadTimeMs"] = proj->injectorDeadTimeMs;
            doc["revLimitRpm"] = proj->revLimitRpm;
            doc["maxDwellMs"] = proj->maxDwellMs;
            doc["altTargetVoltage"] = proj->altTargetVoltage;
            doc["altPidP"] = proj->altPidP;
            doc["altPidI"] = proj->altPidI;
            doc["altPidD"] = proj->altPidD;
            doc["mapVoltageMin"] = proj->mapVoltageMin;
            doc["mapVoltageMax"] = proj->mapVoltageMax;
            doc["mapPressureMinKpa"] = proj->mapPressureMinKpa;
            doc["mapPressureMaxKpa"] = proj->mapPressureMaxKpa;
            doc["o2AfrAt0v"] = proj->o2AfrAt0v;
            doc["o2AfrAt5v"] = proj->o2AfrAt5v;
            doc["closedLoopMinRpm"] = proj->closedLoopMinRpm;
            doc["closedLoopMaxRpm"] = proj->closedLoopMaxRpm;
            doc["closedLoopMaxMapKpa"] = proj->closedLoopMaxMapKpa;
            doc["cj125Enabled"] = proj->cj125Enabled;
            // Transmission
            doc["transType"] = proj->transType;
            doc["upshift12Rpm"] = proj->upshift12Rpm;
            doc["upshift23Rpm"] = proj->upshift23Rpm;
            doc["upshift34Rpm"] = proj->upshift34Rpm;
            doc["downshift21Rpm"] = proj->downshift21Rpm;
            doc["downshift32Rpm"] = proj->downshift32Rpm;
            doc["downshift43Rpm"] = proj->downshift43Rpm;
            doc["tccLockRpm"] = proj->tccLockRpm;
            doc["tccLockGear"] = proj->tccLockGear;
            doc["tccApplyRate"] = proj->tccApplyRate;
            doc["epcBaseDuty"] = proj->epcBaseDuty;
            doc["epcShiftBoost"] = proj->epcShiftBoost;
            doc["shiftTimeMs"] = proj->shiftTimeMs;
            doc["maxTftTempF"] = proj->maxTftTempF;
            String json;
            serializeJson(doc, json);
            request->send(200, "application/json", json);
            return;
        }
        serveFile(request, "/config.html");
    });

    auto* configPostHandler = new AsyncCallbackJsonWebHandler("/config", [this](AsyncWebServerRequest* request, JsonVariant& json) {
        if (!checkAuth(request)) return;
        if (!_config || !_config->getProjectInfo()) { request->send(500); return; }
        ProjectInfo* proj = _config->getProjectInfo();
        JsonObject data = json.as<JsonObject>();
        bool needsReboot = false;
        String errors = "";

        String newSSID = data["wifiSSID"] | _config->getWifiSSID();
        if (newSSID != _config->getWifiSSID()) { _config->setWifiSSID(newSSID); needsReboot = true; }

        String wifiPw = data["wifiPassword"] | String("******");
        if (wifiPw != "******" && wifiPw.length() > 0) {
            String curPw = data["curWifiPw"] | String("");
            if (curPw == _config->getWifiPassword() || _config->verifyAdminPassword(curPw))
                { _config->setWifiPassword(wifiPw); needsReboot = true; }
            else errors += "WiFi password: current password incorrect. ";
        }

        String mqttHost = data["mqttHost"] | _config->getMqttHost().toString();
        IPAddress newMqttHost; newMqttHost.fromString(mqttHost);
        if (newMqttHost != _config->getMqttHost()) { _config->setMqttHost(newMqttHost); needsReboot = true; }
        uint16_t mqttPort = data["mqttPort"] | _config->getMqttPort();
        if (mqttPort != _config->getMqttPort()) { _config->setMqttPort(mqttPort); needsReboot = true; }

        String adminPw = data["adminPassword"] | String("");
        if (adminPw.length() > 0) {
            if (!_config->hasAdminPassword()) {
                _config->setAdminPassword(adminPw);
                if (_ftpDisableCb) _ftpDisableCb();
            } else {
                String curAdminPw = data["curAdminPw"] | String("");
                if (_config->verifyAdminPassword(curAdminPw)) _config->setAdminPassword(adminPw);
                else errors += "Admin password: current password incorrect. ";
            }
        }

        float gmtHrs = data["gmtOffsetHrs"] | (proj->gmtOffsetSec / 3600.0f);
        float dstHrs = data["daylightOffsetHrs"] | (proj->daylightOffsetSec / 3600.0f);
        proj->gmtOffsetSec = (int32_t)(gmtHrs * 3600);
        proj->daylightOffsetSec = (int32_t)(dstHrs * 3600);

        proj->apFallbackSeconds = (data["apFallbackMinutes"] | (proj->apFallbackSeconds / 60)) * 60;
        proj->maxLogSize = data["maxLogSize"] | proj->maxLogSize;
        proj->maxOldLogCount = data["maxOldLogCount"] | proj->maxOldLogCount;

        String theme = data["theme"] | proj->theme;
        if (theme == "dark" || theme == "light") proj->theme = theme;

        // Engine config
        proj->cylinders = data["cylinders"] | proj->cylinders;
        proj->crankTeeth = data["crankTeeth"] | proj->crankTeeth;
        proj->crankMissing = data["crankMissing"] | proj->crankMissing;
        proj->hasCamSensor = data["hasCamSensor"] | proj->hasCamSensor;
        proj->displacement = data["displacement"] | proj->displacement;
        proj->injectorFlowCcMin = data["injectorFlowCcMin"] | proj->injectorFlowCcMin;
        proj->injectorDeadTimeMs = data["injectorDeadTimeMs"] | proj->injectorDeadTimeMs;
        proj->revLimitRpm = data["revLimitRpm"] | proj->revLimitRpm;
        proj->maxDwellMs = data["maxDwellMs"] | proj->maxDwellMs;
        proj->altTargetVoltage = data["altTargetVoltage"] | proj->altTargetVoltage;
        proj->altPidP = data["altPidP"] | proj->altPidP;
        proj->altPidI = data["altPidI"] | proj->altPidI;
        proj->altPidD = data["altPidD"] | proj->altPidD;
        proj->mapVoltageMin = data["mapVoltageMin"] | proj->mapVoltageMin;
        proj->mapVoltageMax = data["mapVoltageMax"] | proj->mapVoltageMax;
        proj->mapPressureMinKpa = data["mapPressureMinKpa"] | proj->mapPressureMinKpa;
        proj->mapPressureMaxKpa = data["mapPressureMaxKpa"] | proj->mapPressureMaxKpa;
        proj->o2AfrAt0v = data["o2AfrAt0v"] | proj->o2AfrAt0v;
        proj->o2AfrAt5v = data["o2AfrAt5v"] | proj->o2AfrAt5v;
        proj->closedLoopMinRpm = data["closedLoopMinRpm"] | proj->closedLoopMinRpm;
        proj->closedLoopMaxRpm = data["closedLoopMaxRpm"] | proj->closedLoopMaxRpm;
        proj->closedLoopMaxMapKpa = data["closedLoopMaxMapKpa"] | proj->closedLoopMaxMapKpa;
        if (data["cj125Enabled"].is<bool>()) proj->cj125Enabled = data["cj125Enabled"].as<bool>();

        // Transmission config
        {
            uint8_t newTransType = data["transType"] | proj->transType;
            if (newTransType != proj->transType) { proj->transType = newTransType; needsReboot = true; }
        }
        proj->upshift12Rpm = data["upshift12Rpm"] | proj->upshift12Rpm;
        proj->upshift23Rpm = data["upshift23Rpm"] | proj->upshift23Rpm;
        proj->upshift34Rpm = data["upshift34Rpm"] | proj->upshift34Rpm;
        proj->downshift21Rpm = data["downshift21Rpm"] | proj->downshift21Rpm;
        proj->downshift32Rpm = data["downshift32Rpm"] | proj->downshift32Rpm;
        proj->downshift43Rpm = data["downshift43Rpm"] | proj->downshift43Rpm;
        proj->tccLockRpm = data["tccLockRpm"] | proj->tccLockRpm;
        proj->tccLockGear = data["tccLockGear"] | proj->tccLockGear;
        proj->tccApplyRate = data["tccApplyRate"] | proj->tccApplyRate;
        proj->epcBaseDuty = data["epcBaseDuty"] | proj->epcBaseDuty;
        proj->epcShiftBoost = data["epcShiftBoost"] | proj->epcShiftBoost;
        proj->shiftTimeMs = data["shiftTimeMs"] | proj->shiftTimeMs;
        proj->maxTftTempF = data["maxTftTempF"] | proj->maxTftTempF;

        bool saved = _config->updateConfig("/config.txt", *proj);

        JsonDocument respDoc;
        if (!saved) respDoc["error"] = "Failed to save config";
        else if (errors.length() > 0) respDoc["error"] = errors;
        else if (needsReboot) { respDoc["message"] = "Settings saved. Rebooting in 2s..."; respDoc["reboot"] = true; }
        else respDoc["message"] = "Settings saved and applied.";
        String response;
        serializeJson(respDoc, response);
        request->send(200, "application/json", response);

        if (needsReboot && saved && errors.length() == 0) {
            if (!_tDelayedReboot)
                _tDelayedReboot = new Task(2 * TASK_SECOND, TASK_ONCE, [this]() { _shouldReboot = true; }, _ts, false);
            _tDelayedReboot->restartDelayed(2 * TASK_SECOND);
        }
    });
    _server.addHandler(configPostHandler);

    // OTA Update
    _server.on("/update", HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (!checkAuth(r)) return;
        serveFile(r, "/update.html");
    });
    _server.on("/update", HTTP_POST, [this](AsyncWebServerRequest* r) {
        if (!checkAuth(r)) return;
        r->send(200, "text/plain", _otaUploadOk ? "OK" : "FAIL");
        _otaUploadOk = false;
    }, nullptr, [this](AsyncWebServerRequest*, uint8_t* data, size_t len, size_t index, size_t total) {
        if (index == 0) {
            _otaUploadOk = false;
            _otaFile = SD.open("/firmware.new", FILE_WRITE);
            if (!_otaFile) return;
            Log.info("OTA", "Saving firmware to SD (%u bytes)", total);
        }
        if (_otaFile) {
            if (_otaFile.write(data, len) != len) { _otaFile.close(); SD.remove("/firmware.new"); _otaFile = File(); }
        }
        if (index + len == total && _otaFile) { _otaFile.close(); _otaUploadOk = true; Log.info("OTA", "Firmware saved"); }
    });

    _server.on("/apply", HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (!checkAuth(r)) return;
        bool exists = firmwareBackupExists("/firmware.new");
        size_t size = exists ? firmwareBackupSize("/firmware.new") : 0;
        r->send(200, "application/json", "{\"exists\":" + String(exists ? "true" : "false") + ",\"size\":" + String(size) + "}");
    });
    _server.on("/apply", HTTP_POST, [this](AsyncWebServerRequest* r) {
        if (!checkAuth(r)) return;
        if (!firmwareBackupExists("/firmware.new")) { r->send(400, "text/plain", "FAIL: no firmware"); return; }
        bool ok = applyFirmwareFromSD();
        r->send(200, "text/plain", ok ? "OK" : "FAIL");
        if (ok) {
            if (!_tDelayedReboot) _tDelayedReboot = new Task(2 * TASK_SECOND, TASK_ONCE, [this]() { _shouldReboot = true; }, _ts, false);
            _tDelayedReboot->restartDelayed(2 * TASK_SECOND);
        }
    });

    _server.on("/revert", HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (!checkAuth(r)) return;
        bool exists = firmwareBackupExists();
        size_t size = exists ? firmwareBackupSize() : 0;
        r->send(200, "application/json", "{\"exists\":" + String(exists ? "true" : "false") + ",\"size\":" + String(size) + "}");
    });
    _server.on("/revert", HTTP_POST, [this](AsyncWebServerRequest* r) {
        if (!checkAuth(r)) return;
        if (!firmwareBackupExists()) { r->send(400, "text/plain", "FAIL: no backup"); return; }
        bool ok = revertFirmwareFromSD();
        r->send(200, "text/plain", ok ? "OK" : "FAIL");
        if (ok) {
            if (!_tDelayedReboot) _tDelayedReboot = new Task(2 * TASK_SECOND, TASK_ONCE, [this]() { _shouldReboot = true; }, _ts, false);
            _tDelayedReboot->restartDelayed(2 * TASK_SECOND);
        }
    });

    // Reboot
    _server.on("/reboot", HTTP_POST, [this](AsyncWebServerRequest* r) {
        if (!checkAuth(r)) return;
        r->send(200, "text/plain", "OK");
        if (!_tDelayedReboot) _tDelayedReboot = new Task(2 * TASK_SECOND, TASK_ONCE, [this]() { _shouldReboot = true; }, _ts, false);
        _tDelayedReboot->restartDelayed(2 * TASK_SECOND);
    });

    // Pin map page
    _server.on("/pins", HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (r->hasParam("format") && r->getParam("format")->value() == "json") {
            JsonDocument doc;
            JsonArray inputs = doc["inputs"].to<JsonArray>();
            JsonArray outputs = doc["outputs"].to<JsonArray>();
            JsonArray bus = doc["bus"].to<JsonArray>();

            // Digital inputs
            auto addDigIn = [&](uint8_t pin, const char* name, const char* trigger, const char* desc) {
                JsonObject o = inputs.add<JsonObject>();
                o["pin"] = pin; o["name"] = name; o["type"] = "digital";
                o["mode"] = trigger; o["desc"] = desc;
                o["value"] = digitalRead(pin) ? "HIGH" : "LOW";
            };
            addDigIn(1, "CRANK", "ISR FALLING", "36-1 trigger wheel, VR sensor. Tooth period ~460us @6000RPM, ~27.8ms @60RPM");
            addDigIn(2, "CAM", "ISR RISING", "1 pulse per 2 crank revs. Phase detection for sequential mode");

            // Analog inputs
            SensorManager* sens = _ecu ? _ecu->getSensorManager() : nullptr;
            auto addAnalog = [&](uint8_t pin, const char* name, uint8_t ch, const char* desc) {
                JsonObject o = inputs.add<JsonObject>();
                o["pin"] = pin; o["name"] = name; o["type"] = "analog"; o["mode"] = "ADC1";
                o["desc"] = desc;
                uint16_t raw = sens ? sens->getRawAdc(ch) : analogRead(pin);
                o["raw"] = raw;
                o["mV"] = (int)(raw * 3300.0f / 4095.0f);
            };
            addAnalog(3, "O2_BANK1", 0, "Wideband O2, 0-5V maps to AFR 10.0-20.0. 12-bit ADC, 0.81mV/step");
            addAnalog(4, "O2_BANK2", 1, "Wideband O2, 0-5V maps to AFR 10.0-20.0. 12-bit ADC, 0.81mV/step");
            addAnalog(5, "MAP", 2, "Manifold pressure, 0.5-4.5V = 10-105 kPa. ~23.75 kPa/V");
            addAnalog(6, "TPS", 3, "Throttle position, 0.5V=closed, 4.5V=WOT. 0-100%");
            addAnalog(7, "CLT", 4, "Coolant NTC thermistor, 2.49k pullup, beta=3380. ~0.1F resolution");
            addAnalog(8, "IAT", 5, "Intake air NTC thermistor, 2.49k pullup, beta=3380. ~0.1F resolution");
            addAnalog(9, "VBAT", 6, "Battery via 47k/10k divider (5.7:1). 0-3.3V ADC = 0-18.8V actual");

            // Digital outputs — coils
            const uint8_t coilPins[] = {10,11,12,13,14,15,16,17};
            for (uint8_t i = 0; i < 8; i++) {
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = coilPins[i];
                char nm[8]; snprintf(nm, sizeof(nm), "COIL_%d", i+1);
                o["name"] = nm; o["type"] = "digital"; o["mode"] = "OUTPUT";
                char desc[64]; snprintf(desc, sizeof(desc), "COP coil driver. HIGH=charging, LOW=fire. Dwell max 4.0ms");
                o["desc"] = desc;
                o["value"] = digitalRead(coilPins[i]) ? "ON" : "OFF";
            }

            // Digital outputs — injectors (native + PCF8575)
            const uint8_t injPins[] = {18,21,40,103,104,105,106,107};
            for (uint8_t i = 0; i < 8; i++) {
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = injPins[i];
                char nm[8]; snprintf(nm, sizeof(nm), "INJ_%d", i+1);
                o["name"] = nm; o["type"] = "digital"; o["mode"] = "OUTPUT";
                if (injPins[i] >= PCF_PIN_OFFSET) {
                    o["desc"] = "High-Z injector via MCP23017. HIGH=open, LOW=closed";
                    o["value"] = xDigitalRead(injPins[i]) ? "ON" : "OFF";
                    o["bus"] = "I2C";
                } else {
                    o["desc"] = "High-Z injector driver. HIGH=open, LOW=closed. Dead time 1.0ms";
                    o["value"] = digitalRead(injPins[i]) ? "ON" : "OFF";
                }
            }

            // PWM output — alternator
            {
                AlternatorControl* alt = _ecu ? _ecu->getAlternator() : nullptr;
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = 41; o["name"] = "ALT_FIELD"; o["type"] = "pwm";
                o["mode"] = "LEDC 25kHz";
                o["desc"] = "Alternator field coil. 8-bit PWM (0-255), 0-95% duty. PID target 13.6V";
                o["percent"] = alt ? alt->getDuty() : 0.0f;
            }

            // Digital output — fuel pump (PCF P0)
            {
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = 100; o["name"] = "FUEL_PUMP"; o["type"] = "digital"; o["mode"] = "OUTPUT";
                o["desc"] = "Fuel pump relay via MCP23017 P0. HIGH=pump on. Always on when ECU running";
                o["value"] = xDigitalRead(100) ? "ON" : "OFF";
                o["bus"] = "I2C";
            }

            // Tach output (PCF P1)
            {
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = 101; o["name"] = "TACH_OUT"; o["type"] = "digital"; o["mode"] = "OUTPUT";
                o["desc"] = "Tachometer square wave output via MCP23017 P1";
                o["value"] = xDigitalRead(101) ? "ON" : "OFF";
                o["bus"] = "I2C";
            }

            // CEL (PCF P2)
            {
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = 102; o["name"] = "CEL"; o["type"] = "digital"; o["mode"] = "OUTPUT";
                o["desc"] = "Check engine light via MCP23017 P2. HIGH=fault active";
                o["value"] = xDigitalRead(102) ? "ON" : "OFF";
                o["bus"] = "I2C";
            }

            // SPI bus
            auto addSpi = [&](uint8_t pin, const char* name) {
                JsonObject o = bus.add<JsonObject>();
                o["pin"] = pin; o["name"] = name; o["type"] = "SPI"; o["mode"] = "SD Card";
            };
            addSpi(47, "SD_CLK");
            addSpi(48, "SD_MISO");
            addSpi(38, "SD_MOSI");
            addSpi(39, "SD_CS");

            // CJ125 heater PWM outputs
            {
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = 19; o["name"] = "HEATER_OUT_1"; o["type"] = "pwm";
                o["mode"] = "LEDC 100Hz"; o["desc"] = "CJ125 heater bank 1 via BTS3134. 8-bit PWM";
            }
            {
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = 20; o["name"] = "HEATER_OUT_2"; o["type"] = "pwm";
                o["mode"] = "LEDC 100Hz"; o["desc"] = "CJ125 heater bank 2 via BTS3134. 8-bit PWM";
            }

            // I2C bus
            PinExpander& pcf = PinExpander::instance();
            {
                JsonObject o = bus.add<JsonObject>();
                o["pin"] = pcf.getSDA(); o["name"] = "I2C_SDA"; o["type"] = "I2C"; o["mode"] = "MCP23017 @ 0x20";
            }
            {
                JsonObject o = bus.add<JsonObject>();
                o["pin"] = pcf.getSCL(); o["name"] = "I2C_SCL"; o["type"] = "I2C"; o["mode"] = "MCP23017 @ 0x20";
            }
            {
                JsonObject o = bus.add<JsonObject>();
                o["pin"] = pcf.getSDA(); o["name"] = "I2C_SDA"; o["type"] = "I2C"; o["mode"] = "ADS1115 @ 0x48 (CJ125_UR)";
            }

            // MCP23017 expander #0
            JsonArray expanders = doc["expanders"].to<JsonArray>();
            {
                JsonObject exp0 = expanders.add<JsonObject>();
                exp0["index"] = 0;
                exp0["type"] = "MCP23017";
                char addrStr[6]; snprintf(addrStr, sizeof(addrStr), "0x%02X", pcf.getAddress(0));
                exp0["address"] = addrStr;
                exp0["ready"] = pcf.isReady(0);
                exp0["sda"] = pcf.getSDA();
                exp0["scl"] = pcf.getSCL();
                if (pcf.isReady(0)) {
                    uint16_t raw = pcf.readAll(0);
                    JsonArray pins = exp0["pins"].to<JsonArray>();
                    const char* pcfNames[] = {"FUEL_PUMP","TACH_OUT","CEL","INJ_4","INJ_5","INJ_6","INJ_7","INJ_8",
                                               "SPI_SS_1","SPI_SS_2","SPARE_10","SPARE_11","SPARE_12","SPARE_13","SPARE_14","SPARE_15"};
                    for (uint8_t i = 0; i < 16; i++) {
                        JsonObject p = pins.add<JsonObject>();
                        p["pcfPin"] = i;
                        p["globalPin"] = PCF_PIN_OFFSET + i;
                        p["name"] = pcfNames[i];
                        p["value"] = (raw & (1 << i)) ? "HIGH" : "LOW";
                    }
                }
            }
            // MCP23017 expander #1 (transmission, 5V via PCA9306)
            {
                JsonObject exp1 = expanders.add<JsonObject>();
                exp1["index"] = 1;
                exp1["type"] = "MCP23017";
                char addrStr[6]; snprintf(addrStr, sizeof(addrStr), "0x%02X", pcf.getAddress(1));
                exp1["address"] = addrStr;
                exp1["ready"] = pcf.isReady(1);
                exp1["sda"] = pcf.getSDA();
                exp1["scl"] = pcf.getSCL();
                exp1["note"] = "5V via PCA9306DCUR level shifter";
                if (pcf.isReady(1)) {
                    uint16_t raw = pcf.readAll(1);
                    JsonArray pins = exp1["pins"].to<JsonArray>();
                    const char* exp1Names[] = {"SS_A","SS_B","SS_C","SS_D",
                                                "SPARE_4","SPARE_5","SPARE_6","SPARE_7",
                                                "SPARE_8","SPARE_9","SPARE_10","SPARE_11",
                                                "SPARE_12","SPARE_13","SPARE_14","SPARE_15"};
                    for (uint8_t i = 0; i < 16; i++) {
                        JsonObject p = pins.add<JsonObject>();
                        p["pcfPin"] = i;
                        p["globalPin"] = PCF_PIN_OFFSET + 16 + i;
                        p["name"] = exp1Names[i];
                        p["value"] = (raw & (1 << i)) ? "HIGH" : "LOW";
                    }
                }
            }

            // Transmission PWM outputs
            if (_ecu && _ecu->getTransmission()) {
                const TransmissionState& ts = _ecu->getTransmission()->getState();
                {
                    JsonObject o = outputs.add<JsonObject>();
                    o["pin"] = 45; o["name"] = "TCC_PWM"; o["type"] = "pwm";
                    o["mode"] = "LEDC 200Hz"; o["desc"] = "Torque converter clutch PWM via MOSFET driver";
                    o["percent"] = ts.tccDuty;
                }
                {
                    JsonObject o = outputs.add<JsonObject>();
                    o["pin"] = 46; o["name"] = "EPC_PWM"; o["type"] = "pwm";
                    o["mode"] = "LEDC 5kHz"; o["desc"] = "Electronic pressure control PWM via MOSFET driver";
                    o["percent"] = ts.epcDuty;
                }
                // Speed sensor inputs (disabled when pin is 0xFF)
                {
                    JsonObject o = inputs.add<JsonObject>();
                    o["pin"] = 5; o["name"] = "OSS"; o["type"] = "digital"; o["mode"] = "ISR RISING";
                    o["desc"] = "Output shaft speed (8 pulses/rev)";
                    o["rpm"] = ts.ossRpm;
                }
                {
                    JsonObject o = inputs.add<JsonObject>();
                    o["pin"] = 6; o["name"] = "TSS"; o["type"] = "digital"; o["mode"] = "ISR RISING";
                    o["desc"] = "Turbine shaft speed (8 pulses/rev)";
                    o["rpm"] = ts.tssRpm;
                }
            }

            // Crank sync state
            if (_ecu) {
                CrankSensor* crank = _ecu->getCrankSensor();
                if (crank) {
                    const char* syncStr[] = {"LOST", "SYNCING", "SYNCED"};
                    doc["crankSync"] = syncStr[crank->getSyncState()];
                    doc["rpm"] = crank->getRpm();
                }
            }

            String json;
            serializeJson(doc, json);
            r->send(200, "application/json", json);
            return;
        }
        serveFile(r, "/pins.html");
    });

    // FTP
    _server.on("/ftp", HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (!checkAuth(r)) return;
        r->send(200, "application/json", _ftpStatusCb ? _ftpStatusCb() : "{\"active\":false}");
    });
    auto* ftpPostHandler = new AsyncCallbackJsonWebHandler("/ftp", [this](AsyncWebServerRequest* r, JsonVariant& json) {
        if (!checkAuth(r)) return;
        JsonObject data = json.as<JsonObject>();
        int duration = data["duration"] | 0;
        if (duration > 0 && _ftpEnableCb) { _ftpEnableCb(duration); r->send(200, "application/json", "{\"status\":\"ok\"}"); }
        else if (_ftpDisableCb) { _ftpDisableCb(); r->send(200, "application/json", "{\"status\":\"ok\"}"); }
        else r->send(500, "application/json", "{\"error\":\"FTP not available\"}");
    });
    _server.addHandler(ftpPostHandler);

    // WiFi scan/test
    _server.on("/wifi/view", HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (!checkAuth(r)) return;
        serveFile(r, "/wifi.html");
    });
    _server.on("/wifi/status", HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (!checkAuth(r)) return;
        String json = "{\"status\":\"" + _wifiTestState + "\"";
        if (_wifiTestMessage.length() > 0) json += ",\"message\":\"" + _wifiTestMessage + "\"";
        json += "}";
        r->send(200, "application/json", json);
    });
    auto* wifiTestHandler = new AsyncCallbackJsonWebHandler("/wifi/test", [this](AsyncWebServerRequest* request, JsonVariant& json) {
        if (!checkAuth(request)) return;
        if (_wifiTestState == "testing") { request->send(409, "application/json", "{\"error\":\"Test in progress\"}"); return; }
        JsonObject data = json.as<JsonObject>();
        String ssid = data["ssid"] | String("");
        String password = data["password"] | String("");
        String curPassword = data["curPassword"] | String("");
        if (ssid.length() == 0) { request->send(400, "application/json", "{\"error\":\"SSID required\"}"); return; }
        bool verified = false;
        if (_config->getWifiPassword().length() > 0) verified = (curPassword == _config->getWifiPassword());
        else if (_config->hasAdminPassword()) verified = _config->verifyAdminPassword(curPassword);
        else verified = true;
        if (!verified) { request->send(403, "application/json", "{\"error\":\"Current password incorrect\"}"); return; }
        _wifiOldSSID = _config->getWifiSSID();
        _wifiOldPassword = _config->getWifiPassword();
        _wifiTestNewSSID = ssid;
        _wifiTestNewPassword = password;
        _wifiTestState = "testing";
        _wifiTestMessage = "";
        _wifiTestCountdown = 15;
        if (!_tWifiTest) {
            _tWifiTest = new Task(TASK_SECOND, TASK_FOREVER, [this]() {
                if (_wifiTestCountdown == 15) {
                    if (_apModeActive) WiFi.mode(WIFI_AP_STA); else WiFi.disconnect(true);
                    WiFi.begin(_wifiTestNewSSID.c_str(), _wifiTestNewPassword.c_str());
                }
                _wifiTestCountdown--;
                if (WiFi.status() == WL_CONNECTED) {
                    String newIP = WiFi.localIP().toString();
                    _config->setWifiSSID(_wifiTestNewSSID);
                    _config->setWifiPassword(_wifiTestNewPassword);
                    ProjectInfo* proj = _config->getProjectInfo();
                    _config->updateConfig("/config.txt", *proj);
                    _wifiTestState = "success";
                    _wifiTestMessage = newIP;
                    _tWifiTest->disable();
                    if (!_tDelayedReboot)
                        _tDelayedReboot = new Task(3 * TASK_SECOND, TASK_ONCE, [this]() { _shouldReboot = true; }, _ts, false);
                    _tDelayedReboot->restartDelayed(3 * TASK_SECOND);
                    return;
                }
                if (_wifiTestCountdown == 0) {
                    WiFi.disconnect(true);
                    if (_apModeActive) WiFi.mode(WIFI_AP); else WiFi.begin(_wifiOldSSID.c_str(), _wifiOldPassword.c_str());
                    _wifiTestState = "failed";
                    _wifiTestMessage = "Could not connect to " + _wifiTestNewSSID;
                    _tWifiTest->disable();
                }
            }, _ts, false);
        }
        _tWifiTest->restartDelayed(TASK_SECOND);
        request->send(200, "application/json", "{\"status\":\"testing\"}");
    });
    _server.addHandler(wifiTestHandler);
}
