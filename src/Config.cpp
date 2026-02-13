#include "Config.h"
#include "esp_hmac.h"
#include "esp_random.h"

uint8_t Config::_aesKey[32] = {0};
bool Config::_encryptionReady = false;
String Config::_obfuscationKey = "";

void Config::setObfuscationKey(const String& key) { _obfuscationKey = key; }

Config::Config()
    : _sdInitialized(false), _mqttHost(192, 168, 0, 46), _mqttPort(1883),
      _mqttUser("debian"), _mqttPassword(""), _wifiSSID(""), _wifiPassword(""),
      _adminPassword(""), _proj(nullptr) {}

bool Config::initEncryption() {
    static const uint8_t salt[] = "ESP-ECU-Config-Encrypt-v1";
    esp_err_t err = esp_hmac_calculate(HMAC_KEY0, salt, sizeof(salt) - 1, _aesKey);
    _encryptionReady = (err == ESP_OK);
    return _encryptionReady;
}

String Config::encryptPassword(const String& plaintext) {
    if (plaintext.length() == 0) return plaintext;
    if (!_encryptionReady) {
        if (_obfuscationKey.length() == 0) return plaintext;
        size_t len = plaintext.length();
        uint8_t* xored = new uint8_t[len];
        for (size_t i = 0; i < len; i++)
            xored[i] = plaintext[i] ^ _obfuscationKey[i % _obfuscationKey.length()];
        size_t outLen = 0;
        mbedtls_base64_encode(nullptr, 0, &outLen, xored, len);
        uint8_t* b64 = new uint8_t[outLen + 1];
        mbedtls_base64_encode(b64, outLen + 1, &outLen, xored, len);
        b64[outLen] = '\0';
        String result = "$ENC$" + String((char*)b64);
        delete[] xored;
        delete[] b64;
        return result;
    }
    uint8_t iv[12];
    esp_fill_random(iv, sizeof(iv));
    size_t ptLen = plaintext.length();
    uint8_t* ciphertext = new uint8_t[ptLen];
    uint8_t tag[16];
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, _aesKey, 256);
    mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, ptLen,
        iv, sizeof(iv), nullptr, 0,
        (const uint8_t*)plaintext.c_str(), ciphertext, sizeof(tag), tag);
    mbedtls_gcm_free(&gcm);
    size_t packedLen = 12 + ptLen + 16;
    uint8_t* packed = new uint8_t[packedLen];
    memcpy(packed, iv, 12);
    memcpy(packed + 12, ciphertext, ptLen);
    memcpy(packed + 12 + ptLen, tag, 16);
    delete[] ciphertext;
    size_t b64Len = 0;
    mbedtls_base64_encode(nullptr, 0, &b64Len, packed, packedLen);
    uint8_t* b64 = new uint8_t[b64Len + 1];
    mbedtls_base64_encode(b64, b64Len + 1, &b64Len, packed, packedLen);
    b64[b64Len] = '\0';
    delete[] packed;
    String result = "$AES$" + String((char*)b64);
    delete[] b64;
    return result;
}

String Config::decryptPassword(const String& encrypted) {
    if (encrypted.startsWith("$AES$")) {
        if (!_encryptionReady) return "";
        String b64Part = encrypted.substring(5);
        size_t decodedLen = 0;
        mbedtls_base64_decode(nullptr, 0, &decodedLen,
            (const uint8_t*)b64Part.c_str(), b64Part.length());
        if (decodedLen < 28) return "";
        uint8_t* decoded = new uint8_t[decodedLen];
        mbedtls_base64_decode(decoded, decodedLen, &decodedLen,
            (const uint8_t*)b64Part.c_str(), b64Part.length());
        uint8_t* iv = decoded;
        size_t ctLen = decodedLen - 12 - 16;
        uint8_t* ct = decoded + 12;
        uint8_t* tag = decoded + 12 + ctLen;
        uint8_t* pt = new uint8_t[ctLen + 1];
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, _aesKey, 256);
        int ret = mbedtls_gcm_auth_decrypt(&gcm, ctLen, iv, 12, nullptr, 0, tag, 16, ct, pt);
        mbedtls_gcm_free(&gcm);
        delete[] decoded;
        if (ret != 0) { delete[] pt; return ""; }
        pt[ctLen] = '\0';
        String result = String((char*)pt);
        delete[] pt;
        return result;
    }
    if (encrypted.startsWith("$ENC$")) {
        if (_obfuscationKey.length() == 0) return encrypted;
        String b64Part = encrypted.substring(5);
        size_t outLen = 0;
        mbedtls_base64_decode(nullptr, 0, &outLen,
            (const uint8_t*)b64Part.c_str(), b64Part.length());
        uint8_t* decoded = new uint8_t[outLen + 1];
        mbedtls_base64_decode(decoded, outLen + 1, &outLen,
            (const uint8_t*)b64Part.c_str(), b64Part.length());
        for (size_t i = 0; i < outLen; i++)
            decoded[i] ^= _obfuscationKey[i % _obfuscationKey.length()];
        decoded[outLen] = '\0';
        String result = String((char*)decoded);
        delete[] decoded;
        return result;
    }
    return encrypted;
}

void Config::setAdminPassword(const String& plaintext) { _adminPassword = plaintext; }
bool Config::verifyAdminPassword(const String& plaintext) const { return plaintext == _adminPassword; }

bool Config::initSDCard(uint8_t csPin) {
    if (!SD.begin(csPin, SPI, SD_SPI_SPEED * 1000000UL)) {
        Serial.println("\nSD initialization failed.");
        return false;
    }
    Serial.println("\nCard successfully initialized.\n");
    _sdInitialized = true;
    return true;
}

bool Config::loadConfig(const char* filename, ProjectInfo& proj) {
    if (!SD.exists(filename)) {
        return saveConfig(filename, proj);
    }
    fs::File file = SD.open(filename, FILE_READ);
    if (!file || file.size() == 0) {
        if (file) file.close();
        return saveConfig(filename, proj);
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        Serial.printf("deserializeJson() failed: %s\n", error.c_str());
        return false;
    }

    // WiFi
    _wifiSSID = doc["wifi"]["ssid"] | "";
    const char* wPw = doc["wifi"]["password"];
    _wifiPassword = wPw ? decryptPassword(wPw) : "";
    proj.apFallbackSeconds = doc["wifi"]["apFallbackSeconds"] | 600;

    // MQTT
    const char* mHost = doc["mqtt"]["host"];
    _mqttHost.fromString(mHost ? mHost : "192.168.0.46");
    _mqttPort = doc["mqtt"]["port"] | 1883;
    _mqttUser = doc["mqtt"]["user"] | "debian";
    const char* mPw = doc["mqtt"]["password"];
    _mqttPassword = mPw ? decryptPassword(mPw) : "";

    // Logging
    proj.maxLogSize = doc["logging"]["maxLogSize"] | (50 * 1024 * 1024);
    proj.maxOldLogCount = doc["logging"]["maxOldLogCount"] | 10;

    // Timezone
    proj.gmtOffsetSec = doc["timezone"]["gmtOffset"] | (-21600);
    proj.daylightOffsetSec = doc["timezone"]["daylightOffset"] | 3600;

    // Theme
    const char* theme = doc["ui"]["theme"];
    proj.theme = theme ? String(theme) : "dark";

    // Admin password
    const char* adminPw = doc["admin"]["password"];
    _adminPassword = (adminPw && strlen(adminPw) > 0) ? decryptPassword(adminPw) : "";

    // Engine config
    proj.cylinders = doc["engine"]["cylinders"] | 8;
    JsonArray fo = doc["engine"]["firingOrder"];
    if (fo) {
        for (uint8_t i = 0; i < 12 && i < fo.size(); i++)
            proj.firingOrder[i] = fo[i];
    }
    proj.crankTeeth = doc["engine"]["crankTeeth"] | 36;
    proj.crankMissing = doc["engine"]["crankMissing"] | 1;
    proj.hasCamSensor = doc["engine"]["hasCamSensor"] | true;
    proj.displacement = doc["engine"]["displacement"] | 5700;
    proj.injectorFlowCcMin = doc["engine"]["injectorFlowCcMin"] | 240.0f;
    proj.injectorDeadTimeMs = doc["engine"]["injectorDeadTimeMs"] | 1.0f;
    proj.revLimitRpm = doc["engine"]["revLimitRpm"] | 6000;
    proj.maxDwellMs = doc["engine"]["maxDwellMs"] | 4.0f;

    // Alternator
    proj.altTargetVoltage = doc["alternator"]["targetVoltage"] | 13.6f;
    proj.altPidP = doc["alternator"]["pidP"] | 10.0f;
    proj.altPidI = doc["alternator"]["pidI"] | 5.0f;
    proj.altPidD = doc["alternator"]["pidD"] | 0.0f;

    // MAP
    proj.mapVoltageMin = doc["map"]["voltageMin"] | 0.5f;
    proj.mapVoltageMax = doc["map"]["voltageMax"] | 4.5f;
    proj.mapPressureMinKpa = doc["map"]["pressureMinKpa"] | 10.0f;
    proj.mapPressureMaxKpa = doc["map"]["pressureMaxKpa"] | 105.0f;

    // O2
    proj.o2AfrAt0v = doc["o2"]["afr_at_0v"] | 10.0f;
    proj.o2AfrAt5v = doc["o2"]["afr_at_5v"] | 20.0f;
    proj.closedLoopMinRpm = doc["o2"]["closedLoopMinRpm"] | 800;
    proj.closedLoopMaxRpm = doc["o2"]["closedLoopMaxRpm"] | 4000;
    proj.closedLoopMaxMapKpa = doc["o2"]["closedLoopMaxMapKpa"] | 80.0f;
    proj.cj125Enabled = doc["engine"]["cj125Enabled"] | false;

    Serial.printf("Config loaded: %d cyl, %d-%d trigger\n", proj.cylinders, proj.crankTeeth, proj.crankMissing);
    return true;
}

bool Config::saveConfig(const char* filename, ProjectInfo& proj) {
    if (SD.exists(filename)) {
        fs::File f = SD.open(filename, FILE_READ);
        if (f && f.size() > 0) { f.close(); return false; }
        if (f) f.close();
    }
    fs::File file = SD.open(filename, FILE_WRITE);
    if (!file) return false;

    JsonDocument doc;
    doc["project"] = proj.name;
    doc["created"] = proj.createdOnDate;
    doc["description"] = proj.description;

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"] = "";
    wifi["password"] = "";
    wifi["apFallbackSeconds"] = proj.apFallbackSeconds;

    JsonObject mqtt = doc["mqtt"].to<JsonObject>();
    mqtt["host"] = "192.168.0.46";
    mqtt["port"] = 1883;
    mqtt["user"] = "debian";
    mqtt["password"] = "";

    JsonObject logging = doc["logging"].to<JsonObject>();
    logging["maxLogSize"] = proj.maxLogSize;
    logging["maxOldLogCount"] = proj.maxOldLogCount;

    JsonObject tz = doc["timezone"].to<JsonObject>();
    tz["gmtOffset"] = proj.gmtOffsetSec;
    tz["daylightOffset"] = proj.daylightOffsetSec;

    doc["ui"]["theme"] = proj.theme.length() > 0 ? proj.theme : "dark";
    doc["admin"]["password"] = "";

    JsonObject engine = doc["engine"].to<JsonObject>();
    engine["cylinders"] = proj.cylinders;
    JsonArray fo = engine["firingOrder"].to<JsonArray>();
    for (uint8_t i = 0; i < proj.cylinders; i++) fo.add(proj.firingOrder[i]);
    engine["crankTeeth"] = proj.crankTeeth;
    engine["crankMissing"] = proj.crankMissing;
    engine["hasCamSensor"] = proj.hasCamSensor;
    engine["displacement"] = proj.displacement;
    engine["injectorFlowCcMin"] = proj.injectorFlowCcMin;
    engine["injectorDeadTimeMs"] = proj.injectorDeadTimeMs;
    engine["revLimitRpm"] = proj.revLimitRpm;
    engine["maxDwellMs"] = proj.maxDwellMs;
    engine["cj125Enabled"] = proj.cj125Enabled;

    JsonObject alt = doc["alternator"].to<JsonObject>();
    alt["targetVoltage"] = proj.altTargetVoltage;
    alt["pidP"] = proj.altPidP;
    alt["pidI"] = proj.altPidI;
    alt["pidD"] = proj.altPidD;

    JsonObject map = doc["map"].to<JsonObject>();
    map["voltageMin"] = proj.mapVoltageMin;
    map["voltageMax"] = proj.mapVoltageMax;
    map["pressureMinKpa"] = proj.mapPressureMinKpa;
    map["pressureMaxKpa"] = proj.mapPressureMaxKpa;

    JsonObject o2 = doc["o2"].to<JsonObject>();
    o2["afr_at_0v"] = proj.o2AfrAt0v;
    o2["afr_at_5v"] = proj.o2AfrAt5v;
    o2["closedLoopMinRpm"] = proj.closedLoopMinRpm;
    o2["closedLoopMaxRpm"] = proj.closedLoopMaxRpm;
    o2["closedLoopMaxMapKpa"] = proj.closedLoopMaxMapKpa;

    serializeJson(doc, file);
    file.close();
    return true;
}

bool Config::updateConfig(const char* filename, ProjectInfo& proj) {
    if (!_sdInitialized) return false;
    fs::File file = SD.open(filename, FILE_READ);
    if (!file) return false;
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return false;

    doc["project"] = proj.name;
    doc["description"] = proj.description;

    doc["wifi"]["ssid"] = _wifiSSID;
    doc["wifi"]["password"] = encryptPassword(_wifiPassword);
    doc["wifi"]["apFallbackSeconds"] = proj.apFallbackSeconds;

    doc["mqtt"]["user"] = _mqttUser;
    doc["mqtt"]["password"] = encryptPassword(_mqttPassword);
    doc["mqtt"]["host"] = _mqttHost.toString();
    doc["mqtt"]["port"] = _mqttPort;

    doc["logging"]["maxLogSize"] = proj.maxLogSize;
    doc["logging"]["maxOldLogCount"] = proj.maxOldLogCount;
    doc["timezone"]["gmtOffset"] = proj.gmtOffsetSec;
    doc["timezone"]["daylightOffset"] = proj.daylightOffsetSec;
    doc["ui"]["theme"] = proj.theme.length() > 0 ? proj.theme : "dark";
    doc["admin"]["password"] = encryptPassword(_adminPassword);

    JsonObject engine = doc["engine"].to<JsonObject>();
    engine["cylinders"] = proj.cylinders;
    JsonArray fo = engine["firingOrder"].to<JsonArray>();
    fo.clear();
    for (uint8_t i = 0; i < proj.cylinders; i++) fo.add(proj.firingOrder[i]);
    engine["crankTeeth"] = proj.crankTeeth;
    engine["crankMissing"] = proj.crankMissing;
    engine["hasCamSensor"] = proj.hasCamSensor;
    engine["displacement"] = proj.displacement;
    engine["injectorFlowCcMin"] = proj.injectorFlowCcMin;
    engine["injectorDeadTimeMs"] = proj.injectorDeadTimeMs;
    engine["revLimitRpm"] = proj.revLimitRpm;
    engine["maxDwellMs"] = proj.maxDwellMs;
    engine["cj125Enabled"] = proj.cj125Enabled;

    doc["alternator"]["targetVoltage"] = proj.altTargetVoltage;
    doc["alternator"]["pidP"] = proj.altPidP;
    doc["alternator"]["pidI"] = proj.altPidI;
    doc["alternator"]["pidD"] = proj.altPidD;

    doc["map"]["voltageMin"] = proj.mapVoltageMin;
    doc["map"]["voltageMax"] = proj.mapVoltageMax;
    doc["map"]["pressureMinKpa"] = proj.mapPressureMinKpa;
    doc["map"]["pressureMaxKpa"] = proj.mapPressureMaxKpa;

    doc["o2"]["afr_at_0v"] = proj.o2AfrAt0v;
    doc["o2"]["afr_at_5v"] = proj.o2AfrAt5v;
    doc["o2"]["closedLoopMinRpm"] = proj.closedLoopMinRpm;
    doc["o2"]["closedLoopMaxRpm"] = proj.closedLoopMaxRpm;
    doc["o2"]["closedLoopMaxMapKpa"] = proj.closedLoopMaxMapKpa;

    file = SD.open(filename, FILE_WRITE);
    if (!file) return false;
    serializeJson(doc, file);
    file.close();
    return true;
}

bool Config::saveTuneData(const char* filename, const char* tableName,
                          const float* data, uint8_t rows, uint8_t cols,
                          const float* xAxis, const float* yAxis) {
    if (!_sdInitialized) return false;

    JsonDocument doc;
    // Read existing file if present
    fs::File file = SD.open(filename, FILE_READ);
    if (file) { deserializeJson(doc, file); file.close(); }

    JsonObject table = doc[tableName].to<JsonObject>();
    JsonArray xArr = table["xAxis"].to<JsonArray>();
    xArr.clear();
    for (uint8_t i = 0; i < cols; i++) xArr.add(xAxis[i]);

    JsonArray yArr = table["yAxis"].to<JsonArray>();
    yArr.clear();
    for (uint8_t i = 0; i < rows; i++) yArr.add(yAxis[i]);

    JsonArray dataArr = table["data"].to<JsonArray>();
    dataArr.clear();
    for (uint8_t r = 0; r < rows; r++) {
        JsonArray row = dataArr.add<JsonArray>();
        for (uint8_t c = 0; c < cols; c++)
            row.add(data[r * cols + c]);
    }

    file = SD.open(filename, FILE_WRITE);
    if (!file) return false;
    serializeJson(doc, file);
    file.close();
    return true;
}

bool Config::loadTuneData(const char* filename, const char* tableName,
                          float* data, uint8_t rows, uint8_t cols,
                          float* xAxis, float* yAxis) {
    if (!_sdInitialized) return false;
    fs::File file = SD.open(filename, FILE_READ);
    if (!file) return false;
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return false;

    JsonObject table = doc[tableName];
    if (table.isNull()) return false;

    JsonArray xArr = table["xAxis"];
    if (xArr && xAxis) {
        for (uint8_t i = 0; i < cols && i < xArr.size(); i++)
            xAxis[i] = xArr[i];
    }

    JsonArray yArr = table["yAxis"];
    if (yArr && yAxis) {
        for (uint8_t i = 0; i < rows && i < yArr.size(); i++)
            yAxis[i] = yArr[i];
    }

    JsonArray dataArr = table["data"];
    if (dataArr && data) {
        for (uint8_t r = 0; r < rows && r < dataArr.size(); r++) {
            JsonArray row = dataArr[r];
            for (uint8_t c = 0; c < cols && c < row.size(); c++)
                data[r * cols + c] = row[c];
        }
    }
    return true;
}
