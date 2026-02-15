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
#include "ADS1115Reader.h"
#include "MCP3204Reader.h"
#include "OtaUtils.h"

extern const char compile_date[];
extern uint8_t getCpuLoadCore0();
extern uint8_t getCpuLoadCore1();
extern bool _apModeActive;
extern bool _safeMode;
extern uint32_t _bootCountExposed;
extern const char* _resetReasonStr;

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
        if (_ecu) {
            doc["limpMode"] = _ecu->isLimpActive();
            doc["limpFaults"] = _ecu->getLimpFaults();
            const EngineState& es = _ecu->getState();
            doc["oilPressurePsi"] = es.oilPressurePsi;
            doc["oilPressureLow"] = es.oilPressureLow;
            doc["expanderFaults"] = es.expanderFaults;
        }
        doc["cpuLoad0"] = getCpuLoadCore0();
        doc["cpuLoad1"] = getCpuLoadCore1();
        doc["freeHeap"] = ESP.getFreeHeap();
        if (_ecu) {
            doc["updateUs"] = _ecu->getUpdateTimeUs();
            doc["sensorUs"] = _ecu->getSensorTimeUs();
        }
        doc["wifiSSID"] = WiFi.SSID();
        doc["wifiRSSI"] = WiFi.RSSI();
        doc["wifiIP"] = WiFi.localIP().toString();
        doc["apMode"] = _apModeActive;
        doc["safeMode"] = _safeMode;
        doc["bootCount"] = _bootCountExposed;
        doc["resetReason"] = _resetReasonStr;
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

    // Preset files from SD card
    _server.on("/presets/engines", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (!SD.exists("/engines.json")) { r->send(200, "application/json", "{}"); return; }
        r->send(SD, "/engines.json", "application/json");
    });
    _server.on("/presets/transmissions", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (!SD.exists("/transmissions.json")) { r->send(200, "application/json", "{}"); return; }
        r->send(SD, "/transmissions.json", "application/json");
    });

    _server.on("/presets/pins", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (!SD.exists("/pins.json")) { r->send(200, "application/json", "{}"); return; }
        r->send(SD, "/pins.json", "application/json");
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

            // Pin assignments
            doc["pinO2Bank1"] = proj->pinO2Bank1;
            doc["pinO2Bank2"] = proj->pinO2Bank2;
            doc["pinMap"] = proj->pinMap;
            doc["pinTps"] = proj->pinTps;
            doc["pinClt"] = proj->pinClt;
            doc["pinIat"] = proj->pinIat;
            doc["pinVbat"] = proj->pinVbat;
            doc["pinAlternator"] = proj->pinAlternator;
            doc["pinHeater1"] = proj->pinHeater1;
            doc["pinHeater2"] = proj->pinHeater2;
            doc["pinTcc"] = proj->pinTcc;
            doc["pinEpc"] = proj->pinEpc;
            doc["pinHspiSck"] = proj->pinHspiSck;
            doc["pinHspiMosi"] = proj->pinHspiMosi;
            doc["pinHspiMiso"] = proj->pinHspiMiso;
            doc["pinHspiCsCoils"] = proj->pinHspiCsCoils;
            doc["pinHspiCsInj"] = proj->pinHspiCsInj;
            doc["pinMcp3204Cs"] = proj->pinMcp3204Cs;
            doc["pinI2cSda"] = proj->pinI2cSda;
            doc["pinI2cScl"] = proj->pinI2cScl;

            // Peripherals & safe mode
            doc["forceSafeMode"] = proj->forceSafeMode;
            doc["i2cEnabled"] = proj->i2cEnabled;
            doc["spiExpandersEnabled"] = proj->spiExpandersEnabled;
            doc["expander0Enabled"] = proj->expander0Enabled;
            doc["expander1Enabled"] = proj->expander1Enabled;
            doc["expander2Enabled"] = proj->expander2Enabled;
            doc["expander3Enabled"] = proj->expander3Enabled;
            doc["spiExp0Enabled"] = proj->spiExp0Enabled;
            doc["spiExp1Enabled"] = proj->spiExp1Enabled;
            doc["safeMode"] = _safeMode;
            doc["bootCount"] = _bootCountExposed;
            doc["resetReason"] = _resetReasonStr;

            // Limp mode
            doc["limpRevLimit"] = proj->limpRevLimit;
            doc["limpAdvanceCap"] = proj->limpAdvanceCap;
            doc["limpRecoverySec"] = proj->limpRecoveryMs / 1000;
            doc["limpMapMin"] = proj->limpMapMin;
            doc["limpMapMax"] = proj->limpMapMax;
            doc["limpTpsMin"] = proj->limpTpsMin;
            doc["limpTpsMax"] = proj->limpTpsMax;
            doc["limpCltMax"] = proj->limpCltMax;
            doc["limpIatMax"] = proj->limpIatMax;
            doc["limpVbatMin"] = proj->limpVbatMin;

            // Oil pressure
            doc["oilPressureMode"] = proj->oilPressureMode;
            doc["pinOilPressure"] = proj->pinOilPressure;
            doc["oilPressureActiveLow"] = proj->oilPressureActiveLow;
            doc["oilPressureMinPsi"] = proj->oilPressureMinPsi;
            doc["oilPressureMaxPsi"] = proj->oilPressureMaxPsi;
            doc["oilPressureMcpChannel"] = proj->oilPressureMcpChannel;
            doc["oilPressureStartupSec"] = proj->oilPressureStartupMs / 1000;

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

        // Peripheral enable flags (all require reboot)
        {
            bool flags[] = {
                data["i2cEnabled"] | proj->i2cEnabled,
                data["spiExpandersEnabled"] | proj->spiExpandersEnabled,
                data["expander0Enabled"] | proj->expander0Enabled,
                data["expander1Enabled"] | proj->expander1Enabled,
                data["expander2Enabled"] | proj->expander2Enabled,
                data["expander3Enabled"] | proj->expander3Enabled,
                data["spiExp0Enabled"] | proj->spiExp0Enabled,
                data["spiExp1Enabled"] | proj->spiExp1Enabled,
            };
            bool* cur[] = {
                &proj->i2cEnabled, &proj->spiExpandersEnabled,
                &proj->expander0Enabled, &proj->expander1Enabled,
                &proj->expander2Enabled, &proj->expander3Enabled,
                &proj->spiExp0Enabled, &proj->spiExp1Enabled,
            };
            for (int i = 0; i < 8; i++) {
                if (flags[i] != *cur[i]) { *cur[i] = flags[i]; needsReboot = true; }
            }
        }
        if (data["forceSafeMode"].is<bool>()) {
            proj->forceSafeMode = data["forceSafeMode"].as<bool>();
            if (proj->forceSafeMode) needsReboot = true;
        }

        // Pin assignments (all require reboot)
        {
            uint8_t pins[] = {
                data["pinO2Bank1"] | proj->pinO2Bank1,
                data["pinO2Bank2"] | proj->pinO2Bank2,
                data["pinMap"] | proj->pinMap,
                data["pinTps"] | proj->pinTps,
                data["pinClt"] | proj->pinClt,
                data["pinIat"] | proj->pinIat,
                data["pinVbat"] | proj->pinVbat,
                data["pinAlternator"] | proj->pinAlternator,
                data["pinHeater1"] | proj->pinHeater1,
                data["pinHeater2"] | proj->pinHeater2,
                data["pinTcc"] | proj->pinTcc,
                data["pinEpc"] | proj->pinEpc,
                data["pinHspiSck"] | proj->pinHspiSck,
                data["pinHspiMosi"] | proj->pinHspiMosi,
                data["pinHspiMiso"] | proj->pinHspiMiso,
                data["pinHspiCsCoils"] | proj->pinHspiCsCoils,
                data["pinHspiCsInj"] | proj->pinHspiCsInj,
                data["pinMcp3204Cs"] | proj->pinMcp3204Cs,
                data["pinI2cSda"] | proj->pinI2cSda,
                data["pinI2cScl"] | proj->pinI2cScl
            };
            uint8_t* cur[] = {
                &proj->pinO2Bank1, &proj->pinO2Bank2, &proj->pinMap, &proj->pinTps,
                &proj->pinClt, &proj->pinIat, &proj->pinVbat, &proj->pinAlternator,
                &proj->pinHeater1, &proj->pinHeater2, &proj->pinTcc, &proj->pinEpc,
                &proj->pinHspiSck, &proj->pinHspiMosi, &proj->pinHspiMiso,
                &proj->pinHspiCsCoils, &proj->pinHspiCsInj, &proj->pinMcp3204Cs,
                &proj->pinI2cSda, &proj->pinI2cScl
            };
            for (int i = 0; i < 20; i++) {
                if (pins[i] != *cur[i]) { *cur[i] = pins[i]; needsReboot = true; }
            }
        }

        // Limp mode (live — no reboot needed)
        proj->limpRevLimit = data["limpRevLimit"] | proj->limpRevLimit;
        proj->limpAdvanceCap = data["limpAdvanceCap"] | proj->limpAdvanceCap;
        {
            uint32_t recSec = data["limpRecoverySec"] | (proj->limpRecoveryMs / 1000);
            proj->limpRecoveryMs = recSec * 1000;
        }
        proj->limpMapMin = data["limpMapMin"] | proj->limpMapMin;
        proj->limpMapMax = data["limpMapMax"] | proj->limpMapMax;
        proj->limpTpsMin = data["limpTpsMin"] | proj->limpTpsMin;
        proj->limpTpsMax = data["limpTpsMax"] | proj->limpTpsMax;
        proj->limpCltMax = data["limpCltMax"] | proj->limpCltMax;
        proj->limpIatMax = data["limpIatMax"] | proj->limpIatMax;
        proj->limpVbatMin = data["limpVbatMin"] | proj->limpVbatMin;

        // Oil pressure (mode/pin require reboot, minPsi is live)
        proj->oilPressureMode = data["oilPressureMode"] | proj->oilPressureMode;
        proj->pinOilPressure = data["pinOilPressure"] | proj->pinOilPressure;
        proj->oilPressureActiveLow = data["oilPressureActiveLow"] | proj->oilPressureActiveLow;
        proj->oilPressureMinPsi = data["oilPressureMinPsi"] | proj->oilPressureMinPsi;
        proj->oilPressureMaxPsi = data["oilPressureMaxPsi"] | proj->oilPressureMaxPsi;
        proj->oilPressureMcpChannel = data["oilPressureMcpChannel"] | proj->oilPressureMcpChannel;
        {
            uint32_t startSec = data["oilPressureStartupSec"] | (proj->oilPressureStartupMs / 1000);
            proj->oilPressureStartupMs = startSec * 1000;
        }

        // Apply live to sensor manager
        if (_ecu && _ecu->getSensorManager()) {
            _ecu->getSensorManager()->setLimpThresholds(
                proj->limpMapMin, proj->limpMapMax, proj->limpTpsMin, proj->limpTpsMax,
                proj->limpCltMax, proj->limpIatMax, proj->limpVbatMin);
        }

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

    // Safe mode clear — reset boot counter, clear force flag, reboot
    _server.on("/safemode/clear", HTTP_POST, [this](AsyncWebServerRequest* r) {
        if (!checkAuth(r)) return;
        // Clear the forceSafeMode flag in config
        if (_config && _config->getProjectInfo()) {
            _config->getProjectInfo()->forceSafeMode = false;
            _config->updateConfig("/config.txt", *_config->getProjectInfo());
        }
        r->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Safe mode cleared. Rebooting...\"}");
        if (!_tDelayedReboot)
            _tDelayedReboot = new Task(2 * TASK_SECOND, TASK_ONCE, [this]() { _shouldReboot = true; }, _ts, false);
        _tDelayedReboot->restartDelayed(2 * TASK_SECOND);
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

            // Get configurable pin values
            ProjectInfo* proj = _config ? _config->getProjectInfo() : nullptr;

            // Digital inputs
            auto addDigIn = [&](uint8_t pin, const char* name, const char* trigger, const char* desc,
                                const char* range, const char* voltage) {
                JsonObject o = inputs.add<JsonObject>();
                o["pin"] = pin; o["name"] = name; o["type"] = "digital";
                o["mode"] = trigger; o["desc"] = desc;
                o["value"] = digitalRead(pin) ? "HIGH" : "LOW";
                o["range"] = range; o["voltage"] = voltage;
            };
            addDigIn(1, "CRANK", "ISR FALLING", "36-1 trigger wheel, VR sensor. Tooth period ~460us @6000RPM, ~27.8ms @60RPM",
                     "HIGH / LOW", "3.3V");
            addDigIn(2, "CAM", "ISR RISING", "1 pulse per 2 crank revs. Phase detection for sequential mode",
                     "HIGH / LOW", "3.3V");

            // Analog inputs
            SensorManager* sens = _ecu ? _ecu->getSensorManager() : nullptr;
            auto addAnalog = [&](uint8_t pin, const char* name, uint8_t ch, const char* desc,
                                 const char* range, const char* voltage) {
                JsonObject o = inputs.add<JsonObject>();
                o["pin"] = pin; o["name"] = name; o["type"] = "analog"; o["mode"] = "ADC1";
                o["desc"] = desc; o["range"] = range; o["voltage"] = voltage;
                uint16_t raw = sens ? sens->getRawAdc(ch) : analogRead(pin);
                o["raw"] = raw;
                o["mV"] = (int)(raw * 3300.0f / 4095.0f);
            };
            uint8_t pO2b1 = proj ? proj->pinO2Bank1 : 3;
            uint8_t pO2b2 = proj ? proj->pinO2Bank2 : 4;
            uint8_t pMap = proj ? proj->pinMap : 5;
            uint8_t pTps = proj ? proj->pinTps : 6;
            uint8_t pClt = proj ? proj->pinClt : 7;
            uint8_t pIat = proj ? proj->pinIat : 8;
            uint8_t pVbat = proj ? proj->pinVbat : 9;
            addAnalog(pO2b1, "O2_BANK1", 0, "Wideband O2, 0-5V maps to AFR 10.0-20.0. 12-bit ADC, 0.81mV/step",
                      "10.0-20.0 AFR", "5V\xe2\x86\x92" "3.3V");
            addAnalog(pO2b2, "O2_BANK2", 1, "Wideband O2, 0-5V maps to AFR 10.0-20.0. 12-bit ADC, 0.81mV/step",
                      "10.0-20.0 AFR", "5V\xe2\x86\x92" "3.3V");
            bool hasMcp = sens && sens->hasMapTpsMCP3204();
            bool hasAds2 = sens && sens->hasMapTpsADS1115();
            if (hasMcp) {
                JsonObject oMap = inputs.add<JsonObject>();
                oMap["pin"] = "MCP3204@SPI CH0"; oMap["name"] = "MAP"; oMap["type"] = "analog"; oMap["mode"] = "SPI ADC";
                oMap["desc"] = "Manifold pressure via MCP3204, 0.5-4.5V = 10-105 kPa (12-bit, 1MHz SPI)";
                oMap["raw"] = 0; oMap["mV"] = 0; oMap["range"] = "10-105 kPa"; oMap["voltage"] = "5V";
                JsonObject oTps = inputs.add<JsonObject>();
                oTps["pin"] = "MCP3204@SPI CH1"; oTps["name"] = "TPS"; oTps["type"] = "analog"; oTps["mode"] = "SPI ADC";
                oTps["desc"] = "Throttle position via MCP3204, 0.5V=closed, 4.5V=WOT (12-bit, 1MHz SPI)";
                oTps["raw"] = 0; oTps["mV"] = 0; oTps["range"] = "0-100%"; oTps["voltage"] = "5V";
            } else if (hasAds2) {
                JsonObject oMap = inputs.add<JsonObject>();
                oMap["pin"] = "ADS1115@0x49 CH0"; oMap["name"] = "MAP"; oMap["type"] = "analog"; oMap["mode"] = "I2C ADC";
                oMap["desc"] = "Manifold pressure via ADS1115, 0.5-4.5V = 10-105 kPa (GAIN_TWOTHIRDS, 860SPS)";
                oMap["raw"] = 0; oMap["mV"] = 0; oMap["range"] = "10-105 kPa"; oMap["voltage"] = "5V";
                JsonObject oTps = inputs.add<JsonObject>();
                oTps["pin"] = "ADS1115@0x49 CH1"; oTps["name"] = "TPS"; oTps["type"] = "analog"; oTps["mode"] = "I2C ADC";
                oTps["desc"] = "Throttle position via ADS1115, 0.5V=closed, 4.5V=WOT (GAIN_TWOTHIRDS, 860SPS)";
                oTps["raw"] = 0; oTps["mV"] = 0; oTps["range"] = "0-100%"; oTps["voltage"] = "5V";
            } else {
                addAnalog(pMap, "MAP", 2, "Manifold pressure, 0.5-4.5V = 10-105 kPa. ~23.75 kPa/V",
                          "10-105 kPa", "5V\xe2\x86\x92" "3.3V");
                addAnalog(pTps, "TPS", 3, "Throttle position, 0.5V=closed, 4.5V=WOT. 0-100%",
                          "0-100%", "5V\xe2\x86\x92" "3.3V");
            }
            addAnalog(pClt, "CLT", 4, "Coolant NTC thermistor, 2.49k pullup, beta=3380. ~0.1F resolution",
                      "-40-300\xc2\xb0" "F", "5V\xe2\x86\x92" "3.3V");
            addAnalog(pIat, "IAT", 5, "Intake air NTC thermistor, 2.49k pullup, beta=3380. ~0.1F resolution",
                      "-40-300\xc2\xb0" "F", "5V\xe2\x86\x92" "3.3V");
            addAnalog(pVbat, "VBAT", 6, "Battery via 47k/10k divider (5.7:1). 0-3.3V ADC = 0-18.8V actual",
                      "0-18.8V", "5V\xe2\x86\x92" "3.3V");

            // Digital outputs — coils (MCP23S17 #0, SPI HSPI 10MHz)
            for (uint8_t i = 0; i < 8; i++) {
                uint8_t pin = 200 + i;
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = pin;
                char nm[8]; snprintf(nm, sizeof(nm), "COIL_%d", i+1);
                o["name"] = nm; o["type"] = "digital"; o["mode"] = "OUTPUT";
                o["desc"] = "COP coil driver via MCP23S17. HIGH=charging, LOW=fire. Dwell max 4.0ms";
                o["value"] = xDigitalRead(pin) ? "ON" : "OFF";
                o["bus"] = "SPI (HSPI 10MHz)";
                o["range"] = "HIGH / LOW"; o["voltage"] = "5V";
            }

            // Digital outputs — injectors (MCP23S17 #1, SPI HSPI 10MHz)
            for (uint8_t i = 0; i < 8; i++) {
                uint8_t pin = 216 + i;
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = pin;
                char nm[8]; snprintf(nm, sizeof(nm), "INJ_%d", i+1);
                o["name"] = nm; o["type"] = "digital"; o["mode"] = "OUTPUT";
                o["desc"] = "High-Z injector via MCP23S17. HIGH=open, LOW=closed. Dead time 1.0ms";
                o["value"] = xDigitalRead(pin) ? "ON" : "OFF";
                o["bus"] = "SPI (HSPI 10MHz)";
                o["range"] = "HIGH / LOW"; o["voltage"] = "5V";
            }

            // PWM output — alternator
            {
                AlternatorControl* alt = _ecu ? _ecu->getAlternator() : nullptr;
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = proj ? proj->pinAlternator : 41; o["name"] = "ALT_FIELD"; o["type"] = "pwm";
                o["mode"] = "LEDC 25kHz";
                o["desc"] = "Alternator field coil. 8-bit PWM (0-255), 0-95% duty. PID target 13.6V";
                o["percent"] = alt ? alt->getDuty() : 0.0f;
                o["range"] = "0-95%"; o["voltage"] = "3.3V";
            }

            // Digital output — fuel pump (PCF P0)
            {
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = 100; o["name"] = "FUEL_PUMP"; o["type"] = "digital"; o["mode"] = "OUTPUT";
                o["desc"] = "Fuel pump relay via MCP23017 P0. HIGH=pump on. Always on when ECU running";
                o["value"] = xDigitalRead(100) ? "ON" : "OFF";
                o["bus"] = "I2C"; o["range"] = "HIGH / LOW"; o["voltage"] = "5V";
            }

            // Tach output (PCF P1)
            {
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = 101; o["name"] = "TACH_OUT"; o["type"] = "digital"; o["mode"] = "OUTPUT";
                o["desc"] = "Tachometer square wave output via MCP23017 P1";
                o["value"] = xDigitalRead(101) ? "ON" : "OFF";
                o["bus"] = "I2C"; o["range"] = "HIGH / LOW"; o["voltage"] = "5V";
            }

            // CEL (PCF P2)
            {
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = 102; o["name"] = "CEL"; o["type"] = "digital"; o["mode"] = "OUTPUT";
                o["desc"] = "Check engine light via MCP23017 P2. HIGH=fault active";
                o["value"] = xDigitalRead(102) ? "ON" : "OFF";
                o["bus"] = "I2C"; o["range"] = "HIGH / LOW"; o["voltage"] = "5V";
            }

            // SPI bus — FSPI (SD card + CJ125)
            auto addSpi = [&](uint8_t pin, const char* name, const char* mode, const char* voltage) {
                JsonObject o = bus.add<JsonObject>();
                o["pin"] = pin; o["name"] = name; o["type"] = "SPI"; o["mode"] = mode; o["voltage"] = voltage;
            };
            addSpi(47, "SD_CLK", "FSPI \xe2\x80\x94 SD Card", "3.3V");
            addSpi(48, "SD_MISO", "FSPI \xe2\x80\x94 SD Card", "3.3V");
            addSpi(38, "SD_MOSI", "FSPI \xe2\x80\x94 SD Card", "3.3V");
            addSpi(39, "SD_CS", "FSPI \xe2\x80\x94 SD Card", "3.3V");
            // SPI bus — HSPI (MCP23S17 coils + injectors, 5V via TXB0108)
            uint8_t pHspiSck = proj ? proj->pinHspiSck : 10;
            uint8_t pHspiMosi = proj ? proj->pinHspiMosi : 11;
            uint8_t pHspiMiso = proj ? proj->pinHspiMiso : 12;
            uint8_t pHspiCsCoils = proj ? proj->pinHspiCsCoils : 13;
            uint8_t pHspiCsInj = proj ? proj->pinHspiCsInj : 14;
            addSpi(pHspiSck, "HSPI_SCK", "HSPI \xe2\x80\x94 MCP23S17 10MHz", "5V");
            addSpi(pHspiMosi, "HSPI_MOSI", "HSPI \xe2\x80\x94 MCP23S17 10MHz", "5V");
            addSpi(pHspiMiso, "HSPI_MISO", "HSPI \xe2\x80\x94 MCP23S17 10MHz", "5V");
            addSpi(pHspiCsCoils, "HSPI_CS0", "HSPI \xe2\x80\x94 MCP23S17 #0 (coils)", "5V");
            addSpi(pHspiCsInj, "HSPI_CS1", "HSPI \xe2\x80\x94 MCP23S17 #1 (injectors)", "5V");
            // MCP3204 CS pin (show if configured, even in safe mode)
            {
                uint8_t pMcp3204Cs = proj ? proj->pinMcp3204Cs : 15;
                MCP3204Reader* mcp = _ecu ? _ecu->getMCP3204() : nullptr;
                bool mcpReady = mcp && mcp->isReady();
                bool spiEnabled = proj && proj->spiExpandersEnabled;
                if (mcpReady || spiEnabled) {
                    addSpi(pMcp3204Cs, "HSPI_CS2", "HSPI \xe2\x80\x94 MCP3204 ADC 1MHz", "5V");
                }
            }

            // CJ125 heater PWM outputs
            {
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = proj ? proj->pinHeater1 : 19; o["name"] = "HEATER_OUT_1"; o["type"] = "pwm";
                o["mode"] = "LEDC 100Hz"; o["desc"] = "CJ125 heater bank 1 via BTS3134. 8-bit PWM";
                o["range"] = "0-100%"; o["voltage"] = "3.3V";
            }
            {
                JsonObject o = outputs.add<JsonObject>();
                o["pin"] = proj ? proj->pinHeater2 : 20; o["name"] = "HEATER_OUT_2"; o["type"] = "pwm";
                o["mode"] = "LEDC 100Hz"; o["desc"] = "CJ125 heater bank 2 via BTS3134. 8-bit PWM";
                o["range"] = "0-100%"; o["voltage"] = "3.3V";
            }

            // I2C bus (all 5V via TXB0108 level shifter)
            PinExpander& pcf = PinExpander::instance();
            {
                JsonObject o = bus.add<JsonObject>();
                o["pin"] = pcf.getSDA(); o["name"] = "I2C_SDA"; o["type"] = "I2C";
                o["mode"] = "MCP23017 @ 0x20"; o["voltage"] = "5V";
            }
            {
                JsonObject o = bus.add<JsonObject>();
                o["pin"] = pcf.getSCL(); o["name"] = "I2C_SCL"; o["type"] = "I2C";
                o["mode"] = "MCP23017 @ 0x20"; o["voltage"] = "5V";
            }
            {
                JsonObject o = bus.add<JsonObject>();
                o["pin"] = pcf.getSDA(); o["name"] = "I2C_SDA"; o["type"] = "I2C";
                o["mode"] = "ADS1115 @ 0x48 (CJ125_UR)"; o["voltage"] = "5V";
            }
            if (sens && sens->hasMapTpsADS1115() && !sens->hasMapTpsMCP3204()) {
                JsonObject o = bus.add<JsonObject>();
                o["pin"] = pcf.getSDA(); o["name"] = "I2C_SDA"; o["type"] = "I2C";
                o["mode"] = "ADS1115 @ 0x49 (MAP/TPS)"; o["voltage"] = "5V";
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
                    const char* pcfNames[] = {"FUEL_PUMP","TACH_OUT","CEL","SPARE_3","SPARE_4","SPARE_5","SPARE_6","SPARE_7",
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

            // MCP23017 expander #2 (0x22, future expansion)
            {
                JsonObject exp2 = expanders.add<JsonObject>();
                exp2["index"] = 2;
                exp2["type"] = "MCP23017";
                char addrStr2[6]; snprintf(addrStr2, sizeof(addrStr2), "0x%02X", pcf.getAddress(2));
                exp2["address"] = addrStr2;
                exp2["ready"] = pcf.isReady(2);
                exp2["sda"] = pcf.getSDA();
                exp2["scl"] = pcf.getSCL();
                if (pcf.isReady(2)) {
                    uint16_t raw = pcf.readAll(2);
                    JsonArray pins = exp2["pins"].to<JsonArray>();
                    for (uint8_t i = 0; i < 16; i++) {
                        JsonObject p = pins.add<JsonObject>();
                        p["pcfPin"] = i;
                        p["globalPin"] = PCF_PIN_OFFSET + 32 + i;
                        char nm[10]; snprintf(nm, sizeof(nm), "SPARE_%d", i);
                        p["name"] = nm;
                        p["value"] = (raw & (1 << i)) ? "HIGH" : "LOW";
                    }
                }
            }
            // MCP23017 expander #3 (0x23, future expansion)
            {
                JsonObject exp3 = expanders.add<JsonObject>();
                exp3["index"] = 3;
                exp3["type"] = "MCP23017";
                char addrStr3[6]; snprintf(addrStr3, sizeof(addrStr3), "0x%02X", pcf.getAddress(3));
                exp3["address"] = addrStr3;
                exp3["ready"] = pcf.isReady(3);
                exp3["sda"] = pcf.getSDA();
                exp3["scl"] = pcf.getSCL();
                if (pcf.isReady(3)) {
                    uint16_t raw = pcf.readAll(3);
                    JsonArray pins = exp3["pins"].to<JsonArray>();
                    for (uint8_t i = 0; i < 16; i++) {
                        JsonObject p = pins.add<JsonObject>();
                        p["pcfPin"] = i;
                        p["globalPin"] = PCF_PIN_OFFSET + 48 + i;
                        char nm[10]; snprintf(nm, sizeof(nm), "SPARE_%d", i);
                        p["name"] = nm;
                        p["value"] = (raw & (1 << i)) ? "HIGH" : "LOW";
                    }
                }
            }
            // MCP23S17 expander #0 (SPI, coils)
            {
                JsonObject spiExp0 = expanders.add<JsonObject>();
                spiExp0["index"] = 4;
                spiExp0["type"] = "MCP23S17";
                spiExp0["bus"] = "HSPI 10MHz";
                spiExp0["cs"] = pcf.getSpiCS(0);
                spiExp0["hwAddr"] = pcf.getSpiHwAddr(0);
                spiExp0["ready"] = pcf.isSpiReady(0);
                if (pcf.isSpiReady(0)) {
                    uint16_t raw = pcf.readAllSpi(0);
                    JsonArray pins = spiExp0["pins"].to<JsonArray>();
                    for (uint8_t i = 0; i < 16; i++) {
                        JsonObject p = pins.add<JsonObject>();
                        p["spiPin"] = i;
                        p["globalPin"] = SPI_EXP_PIN_OFFSET + i;
                        char nm[10];
                        if (i < 8) snprintf(nm, sizeof(nm), "COIL_%d", i+1);
                        else snprintf(nm, sizeof(nm), "SPARE_%d", i);
                        p["name"] = nm;
                        p["value"] = (raw & (1 << i)) ? "HIGH" : "LOW";
                    }
                }
            }
            // MCP23S17 expander #1 (SPI, injectors)
            {
                JsonObject spiExp1 = expanders.add<JsonObject>();
                spiExp1["index"] = 5;
                spiExp1["type"] = "MCP23S17";
                spiExp1["bus"] = "HSPI 10MHz";
                spiExp1["cs"] = pcf.getSpiCS(1);
                spiExp1["hwAddr"] = pcf.getSpiHwAddr(1);
                spiExp1["ready"] = pcf.isSpiReady(1);
                if (pcf.isSpiReady(1)) {
                    uint16_t raw = pcf.readAllSpi(1);
                    JsonArray pins = spiExp1["pins"].to<JsonArray>();
                    for (uint8_t i = 0; i < 16; i++) {
                        JsonObject p = pins.add<JsonObject>();
                        p["spiPin"] = i;
                        p["globalPin"] = SPI_EXP_PIN_OFFSET + 16 + i;
                        char nm[10];
                        if (i < 8) snprintf(nm, sizeof(nm), "INJ_%d", i+1);
                        else snprintf(nm, sizeof(nm), "SPARE_%d", i);
                        p["name"] = nm;
                        p["value"] = (raw & (1 << i)) ? "HIGH" : "LOW";
                    }
                }
            }

            // ADS1115 #0 @ 0x48 (CJ125_UR + Transmission)
            if (_ecu && _ecu->getADS1115_0()) {
                ADS1115Reader* ads0 = _ecu->getADS1115_0();
                JsonObject adsExp0 = expanders.add<JsonObject>();
                adsExp0["index"] = 6;
                adsExp0["type"] = "ADS1115";
                adsExp0["address"] = "0x48";
                adsExp0["ready"] = ads0->isReady();
                adsExp0["sda"] = pcf.getSDA();
                adsExp0["scl"] = pcf.getSCL();
                adsExp0["gain"] = "\xc2\xb1" "4.096V";
                adsExp0["rate"] = "128 SPS";
                if (ads0->isReady()) {
                    const char* chNames[] = {"CJ125_UR_1", "CJ125_UR_2", "TFT_TEMP", "MLPS"};
                    JsonArray channels = adsExp0["channels"].to<JsonArray>();
                    for (uint8_t i = 0; i < 4; i++) {
                        JsonObject ch = channels.add<JsonObject>();
                        ch["ch"] = i;
                        ch["name"] = chNames[i];
                        ch["mV"] = serialized(String(ads0->readMillivolts(i), 1));
                    }
                }
            }
            // ADS1115 #1 @ 0x49 (MAP/TPS)
            if (_ecu && _ecu->getADS1115_1()) {
                ADS1115Reader* ads1 = _ecu->getADS1115_1();
                JsonObject adsExp1 = expanders.add<JsonObject>();
                adsExp1["index"] = 7;
                adsExp1["type"] = "ADS1115";
                adsExp1["address"] = "0x49";
                adsExp1["ready"] = ads1->isReady();
                adsExp1["sda"] = pcf.getSDA();
                adsExp1["scl"] = pcf.getSCL();
                adsExp1["gain"] = "\xc2\xb1" "6.144V";
                adsExp1["rate"] = "860 SPS";
                if (ads1->isReady()) {
                    const char* chNames[] = {"MAP", "TPS", "SPARE", "SPARE"};
                    JsonArray channels = adsExp1["channels"].to<JsonArray>();
                    for (uint8_t i = 0; i < 4; i++) {
                        JsonObject ch = channels.add<JsonObject>();
                        ch["ch"] = i;
                        ch["name"] = chNames[i];
                        ch["mV"] = serialized(String(ads1->readMillivolts(i), 1));
                    }
                }
            }
            // MCP3204 SPI ADC (MAP/TPS, alternative to ADS1115 @ 0x49)
            {
                MCP3204Reader* mcp = _ecu ? _ecu->getMCP3204() : nullptr;
                bool spiEnabled = proj && proj->spiExpandersEnabled;
                if (mcp || spiEnabled) {
                    JsonObject mcpExp = expanders.add<JsonObject>();
                    mcpExp["index"] = 8;
                    mcpExp["type"] = "MCP3204";
                    mcpExp["bus"] = "HSPI 1MHz";
                    mcpExp["cs"] = mcp ? (int)mcp->getCsPin() : (proj ? (int)proj->pinMcp3204Cs : 15);
                    mcpExp["ready"] = mcp ? mcp->isReady() : false;
                    if (mcp) {
                        char vrefStr[8]; snprintf(vrefStr, sizeof(vrefStr), "%.1fV", mcp->getVRef());
                        mcpExp["vRef"] = vrefStr;
                    } else {
                        mcpExp["vRef"] = "5.0V";
                    }
                    if (mcp && mcp->isReady()) {
                        const char* chNames[] = {"MAP", "TPS", "SPARE", "SPARE"};
                        JsonArray channels = mcpExp["channels"].to<JsonArray>();
                        for (uint8_t i = 0; i < 4; i++) {
                            JsonObject ch = channels.add<JsonObject>();
                            ch["ch"] = i;
                            ch["name"] = chNames[i];
                            ch["mV"] = serialized(String(mcp->readMillivolts(i), 1));
                        }
                    }
                }
            }

            // Freed GPIO (available for future use)
            {
                uint8_t pMcp3204Cs = proj ? proj->pinMcp3204Cs : 15;
                MCP3204Reader* mcpR = _ecu ? _ecu->getMCP3204() : nullptr;
                bool mcpActive = (mcpR && mcpR->isReady()) || (proj && proj->spiExpandersEnabled);
                const uint8_t freedPins[] = {15, 16, 17, 18, 21, 40};
                for (uint8_t i = 0; i < 6; i++) {
                    if (mcpActive && freedPins[i] == pMcp3204Cs) continue;  // CS in use
                    JsonObject o = bus.add<JsonObject>();
                    o["pin"] = freedPins[i]; o["name"] = "FREE"; o["type"] = "GPIO";
                    o["mode"] = "Available — freed by MCP23S17 migration";
                }
            }

            // Transmission PWM outputs
            if (_ecu && _ecu->getTransmission()) {
                const TransmissionState& ts = _ecu->getTransmission()->getState();
                {
                    JsonObject o = outputs.add<JsonObject>();
                    o["pin"] = proj ? proj->pinTcc : 45; o["name"] = "TCC_PWM"; o["type"] = "pwm";
                    o["mode"] = "LEDC 200Hz"; o["desc"] = "Torque converter clutch PWM via MOSFET driver";
                    o["percent"] = ts.tccDuty;
                    o["range"] = "0-100%"; o["voltage"] = "3.3V";
                }
                {
                    JsonObject o = outputs.add<JsonObject>();
                    o["pin"] = proj ? proj->pinEpc : 46; o["name"] = "EPC_PWM"; o["type"] = "pwm";
                    o["mode"] = "LEDC 5kHz"; o["desc"] = "Electronic pressure control PWM via MOSFET driver";
                    o["percent"] = ts.epcDuty;
                    o["range"] = "0-100%"; o["voltage"] = "3.3V";
                }
                // Speed sensor inputs — MAP/TPS pins freed when external ADC (MCP3204 or ADS1115) takes over
                {
                    bool ossEnabled = sens && sens->hasExternalMapTps();
                    uint8_t ossPin = ossEnabled ? pMap : 0;
                    JsonObject o = inputs.add<JsonObject>();
                    o["pin"] = (int)ossPin;
                    o["name"] = "OSS"; o["type"] = "digital";
                    o["mode"] = ossEnabled ? "ISR PULSE" : "DISABLED";
                    o["desc"] = ossEnabled ? "Output shaft speed, 8 pulses/rev, ISR counting"
                                           : "Output shaft speed \xe2\x80\x94 needs external ADC to free GPIO";
                    o["rpm"] = ts.ossRpm;
                    o["range"] = "0-65535 RPM"; o["voltage"] = "3.3V";
                }
                {
                    bool tssEnabled = sens && sens->hasExternalMapTps();
                    uint8_t tssPin = tssEnabled ? pTps : 0;
                    JsonObject o = inputs.add<JsonObject>();
                    o["pin"] = (int)tssPin;
                    o["name"] = "TSS"; o["type"] = "digital";
                    o["mode"] = tssEnabled ? "ISR PULSE" : "DISABLED";
                    o["desc"] = tssEnabled ? "Turbine shaft speed, 8 pulses/rev, ISR counting"
                                           : "Turbine shaft speed \xe2\x80\x94 needs external ADC to free GPIO";
                    o["rpm"] = ts.tssRpm;
                    o["range"] = "0-65535 RPM"; o["voltage"] = "3.3V";
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
