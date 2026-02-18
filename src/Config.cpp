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

    // Pin assignments
    proj.pinO2Bank1 = doc["pins"]["o2Bank1"] | 3;
    proj.pinO2Bank2 = doc["pins"]["o2Bank2"] | 4;
    proj.pinMap = doc["pins"]["map"] | 5;
    proj.pinTps = doc["pins"]["tps"] | 6;
    proj.pinClt = doc["pins"]["clt"] | 7;
    proj.pinIat = doc["pins"]["iat"] | 8;
    proj.pinVbat = doc["pins"]["vbat"] | 9;
    proj.pinAlternator = doc["pins"]["alternator"] | 41;
    proj.pinHeater1 = doc["pins"]["heater1"] | 19;
    proj.pinHeater2 = doc["pins"]["heater2"] | 20;
    proj.pinTcc = doc["pins"]["tcc"] | 45;
    proj.pinEpc = doc["pins"]["epc"] | 46;
    proj.pinHspiSck = doc["pins"]["hspiSck"] | 10;
    proj.pinHspiMosi = doc["pins"]["hspiMosi"] | 11;
    proj.pinHspiMiso = doc["pins"]["hspiMiso"] | 12;
    proj.pinHspiCs = doc["pins"]["hspiCs"] | 13;
    proj.pinMcp3204Cs = doc["pins"]["mcp3204Cs"] | 15;
    proj.pinI2cSda = doc["pins"]["i2cSda"] | 0;
    proj.pinI2cScl = doc["pins"]["i2cScl"] | 42;
    // Expander outputs (MCP23S17 #0)
    proj.pinFuelPump = doc["pins"]["fuelPump"] | 200;
    proj.pinTachOut = doc["pins"]["tachOut"] | 201;
    proj.pinCel = doc["pins"]["cel"] | 202;
    proj.pinCj125Ss1 = doc["pins"]["cj125Ss1"] | 208;
    proj.pinCj125Ss2 = doc["pins"]["cj125Ss2"] | 209;
    proj.pinAdsAlert1 = doc["pins"]["adsAlert1"] | 204;
    proj.pinAdsAlert2 = doc["pins"]["adsAlert2"] | 205;
    // Transmission solenoids (MCP23S17 #1)
    proj.pinSsA = doc["pins"]["ssA"] | 216;
    proj.pinSsB = doc["pins"]["ssB"] | 217;
    proj.pinSsC = doc["pins"]["ssC"] | 218;
    proj.pinSsD = doc["pins"]["ssD"] | 219;
    // SD card SPI
    proj.pinSdClk = doc["pins"]["sdClk"] | 47;
    proj.pinSdMiso = doc["pins"]["sdMiso"] | 48;
    proj.pinSdMosi = doc["pins"]["sdMosi"] | 38;
    proj.pinSdCs = doc["pins"]["sdCs"] | 39;
    // Shared expander interrupt GPIO (0xFF = not connected)
    proj.pinSharedInt = doc["pins"]["sharedInt"] | 0xFF;
    // Coil/injector pin arrays (MCP23S17 #4/#5)
    {
        JsonArray ca = doc["pins"]["coils"];
        for (int i = 0; i < 12; i++)
            proj.coilPins[i] = (ca && i < (int)ca.size()) ? (uint16_t)(int)ca[i] : (uint16_t)(i < 8 ? 264 + i : 0);
        JsonArray ia = doc["pins"]["injectors"];
        for (int i = 0; i < 12; i++)
            proj.injectorPins[i] = (ia && i < (int)ia.size()) ? (uint16_t)(int)ia[i] : (uint16_t)(i < 8 ? 280 + i : 0);
    }

    // Safe mode / peripherals
    proj.forceSafeMode = doc["safeMode"]["force"] | false;
    proj.i2cEnabled = doc["peripherals"]["i2c"] | true;
    proj.spiExpandersEnabled = doc["peripherals"]["spiExpanders"] | true;
    proj.expander0Enabled = doc["peripherals"]["exp0"] | true;
    proj.expander1Enabled = doc["peripherals"]["exp1"] | true;
    proj.expander2Enabled = doc["peripherals"]["exp2"] | true;
    proj.expander3Enabled = doc["peripherals"]["exp3"] | true;
    proj.expander4Enabled = doc["peripherals"]["exp4"] | true;
    proj.expander5Enabled = doc["peripherals"]["exp5"] | true;

    // Transmission
    proj.transType = doc["transmission"]["type"] | 0;
    proj.upshift12Rpm = doc["transmission"]["upshift12Rpm"] | 1500;
    proj.upshift23Rpm = doc["transmission"]["upshift23Rpm"] | 2500;
    proj.upshift34Rpm = doc["transmission"]["upshift34Rpm"] | 3000;
    proj.downshift21Rpm = doc["transmission"]["downshift21Rpm"] | 1200;
    proj.downshift32Rpm = doc["transmission"]["downshift32Rpm"] | 1800;
    proj.downshift43Rpm = doc["transmission"]["downshift43Rpm"] | 2200;
    proj.tccLockRpm = doc["transmission"]["tccLockRpm"] | 1500;
    proj.tccLockGear = doc["transmission"]["tccLockGear"] | 3;
    proj.tccApplyRate = doc["transmission"]["tccApplyRate"] | 5.0f;
    proj.epcBaseDuty = doc["transmission"]["epcBaseDuty"] | 50.0f;
    proj.epcShiftBoost = doc["transmission"]["epcShiftBoost"] | 80.0f;
    proj.shiftTimeMs = doc["transmission"]["shiftTimeMs"] | 500;
    proj.maxTftTempF = doc["transmission"]["maxTftTempF"] | 275.0f;

    // Limp mode
    proj.limpRevLimit = doc["limp"]["revLimit"] | 3000;
    proj.limpAdvanceCap = doc["limp"]["advanceCap"] | 10.0f;
    proj.limpRecoveryMs = doc["limp"]["recoveryMs"] | 5000;
    proj.limpMapMin = doc["limp"]["mapMin"] | 5.0f;
    proj.limpMapMax = doc["limp"]["mapMax"] | 120.0f;
    proj.limpTpsMin = doc["limp"]["tpsMin"] | (-5.0f);
    proj.limpTpsMax = doc["limp"]["tpsMax"] | 105.0f;
    proj.limpCltMax = doc["limp"]["cltMax"] | 280.0f;
    proj.limpIatMax = doc["limp"]["iatMax"] | 200.0f;
    proj.limpVbatMin = doc["limp"]["vbatMin"] | 10.0f;

    // Oil pressure
    proj.oilPressureMode = doc["oilPressure"]["mode"] | 0;
    proj.pinOilPressure = doc["oilPressure"]["pin"] | 0;
    proj.oilPressureActiveLow = doc["oilPressure"]["activeLow"] | true;
    proj.oilPressureMinPsi = doc["oilPressure"]["minPsi"] | 10.0f;
    proj.oilPressureMaxPsi = doc["oilPressure"]["maxPsi"] | 100.0f;
    proj.oilPressureMcpChannel = doc["oilPressure"]["mcpChannel"] | 2;
    proj.oilPressureStartupMs = doc["oilPressure"]["startupMs"] | 3000;

    // Fuel pump priming
    proj.fuelPumpPrimeMs = doc["engine"]["fuelPumpPrimeMs"] | 3000;

    // After-Start Enrichment
    proj.aseInitialPct = doc["engine"]["ase"]["initialPct"] | 35.0f;
    proj.aseDurationMs = doc["engine"]["ase"]["durationMs"] | 10000;
    proj.aseMinCltF = doc["engine"]["ase"]["minCltF"] | 100.0f;

    // DFCO
    proj.dfcoRpmThreshold = doc["engine"]["dfco"]["rpmThreshold"] | 2500;
    proj.dfcoTpsThreshold = doc["engine"]["dfco"]["tpsThreshold"] | 3.0f;
    proj.dfcoEntryDelayMs = doc["engine"]["dfco"]["entryDelayMs"] | 500;
    proj.dfcoExitRpm = doc["engine"]["dfco"]["exitRpm"] | 1800;
    proj.dfcoExitTps = doc["engine"]["dfco"]["exitTps"] | 5.0f;

    // CLT-dependent rev limit
    {
        float defaultAxis[] = {32, 60, 100, 140, 180, 220};
        float defaultVals[] = {3000, 3500, 4500, 5500, 6000, 6000};
        JsonArray cltAxis = doc["engine"]["cltRevLimit"]["axis"];
        JsonArray cltVals = doc["engine"]["cltRevLimit"]["values"];
        for (uint8_t i = 0; i < 6; i++) {
            proj.cltRevLimitAxis[i] = (cltAxis && i < cltAxis.size()) ? (float)cltAxis[i] : defaultAxis[i];
            proj.cltRevLimitValues[i] = (cltVals && i < cltVals.size()) ? (float)cltVals[i] : defaultVals[i];
        }
    }

    // Board diagnostics
    proj.diagEnabled = doc["diagnostics"]["enabled"] | false;
    {
        JsonArray muxPins = doc["diagnostics"]["muxSelPins"];
        uint16_t defaults[] = {272, 273, 274, 275};
        for (uint8_t i = 0; i < 4; i++)
            proj.diagMuxSelPins[i] = (muxPins && i < muxPins.size()) ? (uint16_t)(int)muxPins[i] : defaults[i];
    }
    proj.diagMuxEnPin = doc["diagnostics"]["muxEnPin"] | 276;
    proj.diagAlertPin = doc["diagnostics"]["alertPin"] | 203;

    // Modbus RTU
    proj.modbusEnabled   = doc["modbus"]["enabled"] | false;
    proj.pinModbusTx     = doc["modbus"]["txPin"] | 16;
    proj.pinModbusRx     = doc["modbus"]["rxPin"] | 17;
    proj.modbusBaud      = doc["modbus"]["baud"] | 9600;
    proj.modbusMaxSlaves = doc["modbus"]["maxSlaves"] | 4;

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

    JsonObject pins = doc["pins"].to<JsonObject>();
    pins["o2Bank1"] = proj.pinO2Bank1;
    pins["o2Bank2"] = proj.pinO2Bank2;
    pins["map"] = proj.pinMap;
    pins["tps"] = proj.pinTps;
    pins["clt"] = proj.pinClt;
    pins["iat"] = proj.pinIat;
    pins["vbat"] = proj.pinVbat;
    pins["alternator"] = proj.pinAlternator;
    pins["heater1"] = proj.pinHeater1;
    pins["heater2"] = proj.pinHeater2;
    pins["tcc"] = proj.pinTcc;
    pins["epc"] = proj.pinEpc;
    pins["hspiSck"] = proj.pinHspiSck;
    pins["hspiMosi"] = proj.pinHspiMosi;
    pins["hspiMiso"] = proj.pinHspiMiso;
    pins["hspiCs"] = proj.pinHspiCs;
    pins["mcp3204Cs"] = proj.pinMcp3204Cs;
    pins["i2cSda"] = proj.pinI2cSda;
    pins["i2cScl"] = proj.pinI2cScl;
    pins["fuelPump"] = proj.pinFuelPump;
    pins["tachOut"] = proj.pinTachOut;
    pins["cel"] = proj.pinCel;
    pins["cj125Ss1"] = proj.pinCj125Ss1;
    pins["cj125Ss2"] = proj.pinCj125Ss2;
    pins["adsAlert1"] = proj.pinAdsAlert1;
    pins["adsAlert2"] = proj.pinAdsAlert2;
    pins["ssA"] = proj.pinSsA;
    pins["ssB"] = proj.pinSsB;
    pins["ssC"] = proj.pinSsC;
    pins["ssD"] = proj.pinSsD;
    pins["sdClk"] = proj.pinSdClk;
    pins["sdMiso"] = proj.pinSdMiso;
    pins["sdMosi"] = proj.pinSdMosi;
    pins["sdCs"] = proj.pinSdCs;
    // Shared expander interrupt GPIO
    if (proj.pinSharedInt != 0xFF) pins["sharedInt"] = proj.pinSharedInt;
    {
        JsonArray ca = pins["coils"].to<JsonArray>();
        JsonArray ia = pins["injectors"].to<JsonArray>();
        for (int i = 0; i < 12; i++) ca.add(proj.coilPins[i]);
        for (int i = 0; i < 12; i++) ia.add(proj.injectorPins[i]);
    }

    JsonObject trans = doc["transmission"].to<JsonObject>();
    trans["type"] = proj.transType;
    trans["upshift12Rpm"] = proj.upshift12Rpm;
    trans["upshift23Rpm"] = proj.upshift23Rpm;
    trans["upshift34Rpm"] = proj.upshift34Rpm;
    trans["downshift21Rpm"] = proj.downshift21Rpm;
    trans["downshift32Rpm"] = proj.downshift32Rpm;
    trans["downshift43Rpm"] = proj.downshift43Rpm;
    trans["tccLockRpm"] = proj.tccLockRpm;
    trans["tccLockGear"] = proj.tccLockGear;
    trans["tccApplyRate"] = proj.tccApplyRate;
    trans["epcBaseDuty"] = proj.epcBaseDuty;
    trans["epcShiftBoost"] = proj.epcShiftBoost;
    trans["shiftTimeMs"] = proj.shiftTimeMs;
    trans["maxTftTempF"] = proj.maxTftTempF;

    doc["safeMode"]["force"] = false;
    JsonObject periph = doc["peripherals"].to<JsonObject>();
    periph["i2c"] = proj.i2cEnabled;
    periph["spiExpanders"] = proj.spiExpandersEnabled;
    periph["exp0"] = proj.expander0Enabled;
    periph["exp1"] = proj.expander1Enabled;
    periph["exp2"] = proj.expander2Enabled;
    periph["exp3"] = proj.expander3Enabled;
    periph["exp4"] = proj.expander4Enabled;
    periph["exp5"] = proj.expander5Enabled;

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

    // Guard: never overwrite valid SSID/MQTT with empty values
    if (_wifiSSID.length() > 0) {
        doc["wifi"]["ssid"] = _wifiSSID;
    }
    if (_wifiPassword.length() > 0) {
        doc["wifi"]["password"] = encryptPassword(_wifiPassword);
    }
    doc["wifi"]["apFallbackSeconds"] = proj.apFallbackSeconds;

    if (_mqttUser.length() > 0) {
        doc["mqtt"]["user"] = _mqttUser;
    }
    if (_mqttPassword.length() > 0) {
        doc["mqtt"]["password"] = encryptPassword(_mqttPassword);
    }
    IPAddress zeroIP(0, 0, 0, 0);
    if (_mqttHost != zeroIP) {
        doc["mqtt"]["host"] = _mqttHost.toString();
    }
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
    engine["fuelPumpPrimeMs"] = proj.fuelPumpPrimeMs;

    // ASE
    JsonObject ase = engine["ase"].to<JsonObject>();
    ase["initialPct"] = proj.aseInitialPct;
    ase["durationMs"] = proj.aseDurationMs;
    ase["minCltF"] = proj.aseMinCltF;

    // DFCO
    JsonObject dfco = engine["dfco"].to<JsonObject>();
    dfco["rpmThreshold"] = proj.dfcoRpmThreshold;
    dfco["tpsThreshold"] = proj.dfcoTpsThreshold;
    dfco["entryDelayMs"] = proj.dfcoEntryDelayMs;
    dfco["exitRpm"] = proj.dfcoExitRpm;
    dfco["exitTps"] = proj.dfcoExitTps;

    // CLT rev limit
    JsonObject cltRevLimit = engine["cltRevLimit"].to<JsonObject>();
    JsonArray cltAxis = cltRevLimit["axis"].to<JsonArray>();
    JsonArray cltVals = cltRevLimit["values"].to<JsonArray>();
    cltAxis.clear();
    cltVals.clear();
    for (uint8_t i = 0; i < 6; i++) {
        cltAxis.add(proj.cltRevLimitAxis[i]);
        cltVals.add(proj.cltRevLimitValues[i]);
    }

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

    doc["pins"]["o2Bank1"] = proj.pinO2Bank1;
    doc["pins"]["o2Bank2"] = proj.pinO2Bank2;
    doc["pins"]["map"] = proj.pinMap;
    doc["pins"]["tps"] = proj.pinTps;
    doc["pins"]["clt"] = proj.pinClt;
    doc["pins"]["iat"] = proj.pinIat;
    doc["pins"]["vbat"] = proj.pinVbat;
    doc["pins"]["alternator"] = proj.pinAlternator;
    doc["pins"]["heater1"] = proj.pinHeater1;
    doc["pins"]["heater2"] = proj.pinHeater2;
    doc["pins"]["tcc"] = proj.pinTcc;
    doc["pins"]["epc"] = proj.pinEpc;
    doc["pins"]["hspiSck"] = proj.pinHspiSck;
    doc["pins"]["hspiMosi"] = proj.pinHspiMosi;
    doc["pins"]["hspiMiso"] = proj.pinHspiMiso;
    doc["pins"]["hspiCs"] = proj.pinHspiCs;
    doc["pins"]["mcp3204Cs"] = proj.pinMcp3204Cs;
    doc["pins"]["i2cSda"] = proj.pinI2cSda;
    doc["pins"]["i2cScl"] = proj.pinI2cScl;
    doc["pins"]["fuelPump"] = proj.pinFuelPump;
    doc["pins"]["tachOut"] = proj.pinTachOut;
    doc["pins"]["cel"] = proj.pinCel;
    doc["pins"]["cj125Ss1"] = proj.pinCj125Ss1;
    doc["pins"]["cj125Ss2"] = proj.pinCj125Ss2;
    doc["pins"]["adsAlert1"] = proj.pinAdsAlert1;
    doc["pins"]["adsAlert2"] = proj.pinAdsAlert2;
    doc["pins"]["ssA"] = proj.pinSsA;
    doc["pins"]["ssB"] = proj.pinSsB;
    doc["pins"]["ssC"] = proj.pinSsC;
    doc["pins"]["ssD"] = proj.pinSsD;
    doc["pins"]["sdClk"] = proj.pinSdClk;
    doc["pins"]["sdMiso"] = proj.pinSdMiso;
    doc["pins"]["sdMosi"] = proj.pinSdMosi;
    doc["pins"]["sdCs"] = proj.pinSdCs;
    if (proj.pinSharedInt != 0xFF) doc["pins"]["sharedInt"] = proj.pinSharedInt;
    {
        JsonArray ca = doc["pins"]["coils"].to<JsonArray>();
        ca.clear();
        JsonArray ia = doc["pins"]["injectors"].to<JsonArray>();
        ia.clear();
        for (int i = 0; i < 12; i++) ca.add(proj.coilPins[i]);
        for (int i = 0; i < 12; i++) ia.add(proj.injectorPins[i]);
    }

    doc["transmission"]["type"] = proj.transType;
    doc["transmission"]["upshift12Rpm"] = proj.upshift12Rpm;
    doc["transmission"]["upshift23Rpm"] = proj.upshift23Rpm;
    doc["transmission"]["upshift34Rpm"] = proj.upshift34Rpm;
    doc["transmission"]["downshift21Rpm"] = proj.downshift21Rpm;
    doc["transmission"]["downshift32Rpm"] = proj.downshift32Rpm;
    doc["transmission"]["downshift43Rpm"] = proj.downshift43Rpm;
    doc["transmission"]["tccLockRpm"] = proj.tccLockRpm;
    doc["transmission"]["tccLockGear"] = proj.tccLockGear;
    doc["transmission"]["tccApplyRate"] = proj.tccApplyRate;
    doc["transmission"]["epcBaseDuty"] = proj.epcBaseDuty;
    doc["transmission"]["epcShiftBoost"] = proj.epcShiftBoost;
    doc["transmission"]["shiftTimeMs"] = proj.shiftTimeMs;
    doc["transmission"]["maxTftTempF"] = proj.maxTftTempF;

    doc["safeMode"]["force"] = proj.forceSafeMode;
    doc["peripherals"]["i2c"] = proj.i2cEnabled;
    doc["peripherals"]["spiExpanders"] = proj.spiExpandersEnabled;
    doc["peripherals"]["exp0"] = proj.expander0Enabled;
    doc["peripherals"]["exp1"] = proj.expander1Enabled;
    doc["peripherals"]["exp2"] = proj.expander2Enabled;
    doc["peripherals"]["exp3"] = proj.expander3Enabled;
    doc["peripherals"]["exp4"] = proj.expander4Enabled;
    doc["peripherals"]["exp5"] = proj.expander5Enabled;

    // Limp mode
    doc["limp"]["revLimit"] = proj.limpRevLimit;
    doc["limp"]["advanceCap"] = proj.limpAdvanceCap;
    doc["limp"]["recoveryMs"] = proj.limpRecoveryMs;
    doc["limp"]["mapMin"] = proj.limpMapMin;
    doc["limp"]["mapMax"] = proj.limpMapMax;
    doc["limp"]["tpsMin"] = proj.limpTpsMin;
    doc["limp"]["tpsMax"] = proj.limpTpsMax;
    doc["limp"]["cltMax"] = proj.limpCltMax;
    doc["limp"]["iatMax"] = proj.limpIatMax;
    doc["limp"]["vbatMin"] = proj.limpVbatMin;

    // Oil pressure
    doc["oilPressure"]["mode"] = proj.oilPressureMode;
    doc["oilPressure"]["pin"] = proj.pinOilPressure;
    doc["oilPressure"]["activeLow"] = proj.oilPressureActiveLow;
    doc["oilPressure"]["minPsi"] = proj.oilPressureMinPsi;
    doc["oilPressure"]["maxPsi"] = proj.oilPressureMaxPsi;
    doc["oilPressure"]["mcpChannel"] = proj.oilPressureMcpChannel;
    doc["oilPressure"]["startupMs"] = proj.oilPressureStartupMs;

    // Board diagnostics
    doc["diagnostics"]["enabled"] = proj.diagEnabled;
    {
        JsonArray muxPins = doc["diagnostics"]["muxSelPins"].to<JsonArray>();
        muxPins.clear();
        for (uint8_t i = 0; i < 4; i++) muxPins.add(proj.diagMuxSelPins[i]);
    }
    doc["diagnostics"]["muxEnPin"] = proj.diagMuxEnPin;
    doc["diagnostics"]["alertPin"] = proj.diagAlertPin;

    // Modbus RTU
    doc["modbus"]["enabled"]   = proj.modbusEnabled;
    doc["modbus"]["txPin"]     = proj.pinModbusTx;
    doc["modbus"]["rxPin"]     = proj.pinModbusRx;
    doc["modbus"]["baud"]      = proj.modbusBaud;
    doc["modbus"]["maxSlaves"] = proj.modbusMaxSlaves;

    file = SD.open(filename, FILE_WRITE);
    if (!file) return false;
    serializeJson(doc, file);
    file.close();
    return true;
}

// --- Sensor descriptor JSON helpers ---

static const char* sourceTypeToStr(SourceType t) {
    switch (t) {
        case SRC_GPIO_ADC:    return "gpio_adc";
        case SRC_GPIO_DIGITAL: return "gpio_digital";
        case SRC_ADS1115:     return "ads1115";
        case SRC_MCP3204:     return "mcp3204";
        case SRC_EXPANDER:      return "expander";
        case SRC_ENGINE_STATE:  return "engine_state";
        case SRC_OUTPUT_STATE:  return "output_state";
        default:                return "disabled";
    }
}
static SourceType strToSourceType(const char* s) {
    if (!s) return SRC_DISABLED;
    if (strcmp(s, "gpio_adc") == 0)      return SRC_GPIO_ADC;
    if (strcmp(s, "gpio_digital") == 0)  return SRC_GPIO_DIGITAL;
    if (strcmp(s, "ads1115") == 0)       return SRC_ADS1115;
    if (strcmp(s, "mcp3204") == 0)       return SRC_MCP3204;
    if (strcmp(s, "expander") == 0)      return SRC_EXPANDER;
    if (strcmp(s, "engine_state") == 0)  return SRC_ENGINE_STATE;
    if (strcmp(s, "output_state") == 0)  return SRC_OUTPUT_STATE;
    return SRC_DISABLED;
}
static const char* calTypeToStr(CalType t) {
    switch (t) {
        case CAL_LINEAR:   return "linear";
        case CAL_NTC:      return "ntc";
        case CAL_VDIVIDER: return "vdivider";
        case CAL_LOOKUP:   return "lookup";
        default:           return "none";
    }
}
static CalType strToCalType(const char* s) {
    if (!s) return CAL_NONE;
    if (strcmp(s, "linear") == 0)   return CAL_LINEAR;
    if (strcmp(s, "ntc") == 0)      return CAL_NTC;
    if (strcmp(s, "vdivider") == 0) return CAL_VDIVIDER;
    if (strcmp(s, "lookup") == 0)   return CAL_LOOKUP;
    return CAL_NONE;
}
static const char* faultActionToStr(FaultAction a) {
    switch (a) {
        case FAULT_ACT_LIMP:     return "limp";
        case FAULT_ACT_SHUTDOWN: return "shutdown";
        case FAULT_ACT_CEL:      return "cel";
        default:                 return "none";
    }
}
static FaultAction strToFaultAction(const char* s) {
    if (!s) return FAULT_ACT_NONE;
    if (strcmp(s, "limp") == 0)     return FAULT_ACT_LIMP;
    if (strcmp(s, "shutdown") == 0) return FAULT_ACT_SHUTDOWN;
    if (strcmp(s, "cel") == 0)      return FAULT_ACT_CEL;
    return FAULT_ACT_NONE;
}
static const char* ruleOpToStr(RuleOp op) {
    switch (op) {
        case OP_LT:    return "lt";
        case OP_GT:    return "gt";
        case OP_RANGE: return "range";
        case OP_DELTA: return "delta";
        default:       return "lt";
    }
}
static RuleOp strToRuleOp(const char* s) {
    if (!s) return OP_LT;
    if (strcmp(s, "gt") == 0)    return OP_GT;
    if (strcmp(s, "range") == 0) return OP_RANGE;
    if (strcmp(s, "delta") == 0) return OP_DELTA;
    return OP_LT;
}

bool Config::loadSensorConfig(const char* filename, SensorDescriptor* desc, uint8_t maxDesc,
                               FaultRule* rules, uint8_t maxRules) {
    if (!_sdInitialized) return false;
    if (!SD.exists(filename)) return false;
    fs::File file = SD.open(filename, FILE_READ);
    if (!file) return false;
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return false;

    JsonArray sensors = doc["sensors"];
    if (!sensors) return false;  // No sensors array — caller should use defaults

    for (uint8_t i = 0; i < maxDesc && i < sensors.size(); i++) {
        JsonObject s = sensors[i];
        SensorDescriptor& d = desc[i];
        d.clear();

        const char* name = s["name"];
        if (name) strncpy(d.name, name, sizeof(d.name) - 1);
        const char* unit = s["unit"];
        if (unit) strncpy(d.unit, unit, sizeof(d.unit) - 1);

        // Source
        JsonObject src = s["source"];
        d.sourceType = strToSourceType(src["type"]);
        d.sourceDevice = src["device"] | 0;
        d.sourceChannel = src["channel"] | 0;
        d.sourcePin = src["pin"] | 0;

        // Calibration
        JsonObject cal = s["cal"];
        d.calType = strToCalType(cal["type"]);
        d.calA = cal["a"] | 0.0f;
        d.calB = cal["b"] | 0.0f;
        d.calC = cal["c"] | 0.0f;
        d.calD = cal["d"] | 0.0f;

        // Filtering
        JsonObject filt = s["filter"];
        d.emaAlpha = filt["ema"] | 0.3f;
        d.avgSamples = filt["avg"] | 1;
        if (d.avgSamples > MAX_AVG_SAMPLES) d.avgSamples = MAX_AVG_SAMPLES;

        // Validation
        JsonObject val = s["validate"];
        d.errorMin = val["errorMin"].is<float>() ? (float)val["errorMin"] : NAN;
        d.errorMax = val["errorMax"].is<float>() ? (float)val["errorMax"] : NAN;
        d.warnMin = val["warnMin"].is<float>() ? (float)val["warnMin"] : NAN;
        d.warnMax = val["warnMax"].is<float>() ? (float)val["warnMax"] : NAN;
        d.settleGuard = val["settle"] | 0.0f;

        // Fault
        JsonObject fault = s["fault"];
        d.faultBit = fault["bit"] | 0xFF;
        d.faultAction = strToFaultAction(fault["action"]);

        // State-dependent activation (default STATE_ALL for backward compat)
        d.activeStates = s["activeStates"] | 0x07;
    }

    // Load rules
    JsonArray rulesArr = doc["rules"];
    if (rulesArr) {
        for (uint8_t i = 0; i < maxRules && i < rulesArr.size(); i++) {
            JsonObject ro = rulesArr[i];
            FaultRule& r = rules[i];
            r.clear();

            const char* rname = ro["name"];
            if (rname) strncpy(r.name, rname, sizeof(r.name) - 1);
            r.sensorSlot = ro["sensor"] | 0xFF;
            r.sensorSlotB = ro["sensorB"] | 0xFF;
            r.op = strToRuleOp(ro["op"]);
            r.thresholdA = ro["a"] | 0.0f;
            r.thresholdB = ro["b"] | 0.0f;
            r.faultBit = ro["bit"] | 0xFF;
            r.faultAction = strToFaultAction(ro["action"]);
            r.debounceMs = ro["debounce"] | 0;
            r.requireRunning = ro["running"] | false;
            // Gate conditions
            r.gateRpmMin = ro["gateRpmMin"] | 0;
            r.gateRpmMax = ro["gateRpmMax"] | 0;
            r.gateMapMin = ro["gateMapMin"].is<float>() ? (float)ro["gateMapMin"] : NAN;
            r.gateMapMax = ro["gateMapMax"].is<float>() ? (float)ro["gateMapMax"] : NAN;
            // Dynamic threshold curve
            r.curveSource = ro["curveSource"] | 0xFF;
            if (r.curveSource != 0xFF) {
                JsonArray cx = ro["curveX"];
                JsonArray cy = ro["curveY"];
                for (uint8_t j = 0; j < CURVE_POINTS; j++) {
                    r.curveX[j] = (cx && j < cx.size()) ? (float)cx[j] : 0.0f;
                    r.curveY[j] = (cy && j < cy.size()) ? (float)cy[j] : 0.0f;
                }
            }
        }
    }

    Serial.printf("Sensor config loaded: %d sensors, %d rules\n",
                  (int)sensors.size(), rulesArr ? (int)rulesArr.size() : 0);
    return true;
}

bool Config::saveSensorConfig(const char* filename, const SensorDescriptor* desc, uint8_t maxDesc,
                               const FaultRule* rules, uint8_t maxRules) {
    if (!_sdInitialized) return false;

    // Read existing config
    fs::File file = SD.open(filename, FILE_READ);
    if (!file) return false;
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return false;

    // Write sensors array
    JsonArray sensors = doc["sensors"].to<JsonArray>();
    sensors.clear();
    for (uint8_t i = 0; i < maxDesc; i++) {
        const SensorDescriptor& d = desc[i];
        JsonObject s = sensors.add<JsonObject>();

        s["name"] = d.name;
        s["unit"] = d.unit;

        JsonObject src = s["source"].to<JsonObject>();
        src["type"] = sourceTypeToStr(d.sourceType);
        src["device"] = d.sourceDevice;
        src["channel"] = d.sourceChannel;
        src["pin"] = d.sourcePin;

        JsonObject cal = s["cal"].to<JsonObject>();
        cal["type"] = calTypeToStr(d.calType);
        cal["a"] = d.calA;
        cal["b"] = d.calB;
        cal["c"] = d.calC;
        cal["d"] = d.calD;

        JsonObject filt = s["filter"].to<JsonObject>();
        filt["ema"] = d.emaAlpha;
        filt["avg"] = d.avgSamples;

        JsonObject val = s["validate"].to<JsonObject>();
        if (!isnan(d.errorMin)) val["errorMin"] = d.errorMin; else val["errorMin"] = (char*)nullptr;
        if (!isnan(d.errorMax)) val["errorMax"] = d.errorMax; else val["errorMax"] = (char*)nullptr;
        if (!isnan(d.warnMin))  val["warnMin"] = d.warnMin;   else val["warnMin"] = (char*)nullptr;
        if (!isnan(d.warnMax))  val["warnMax"] = d.warnMax;   else val["warnMax"] = (char*)nullptr;
        val["settle"] = d.settleGuard;

        JsonObject fault = s["fault"].to<JsonObject>();
        fault["bit"] = d.faultBit;
        fault["action"] = faultActionToStr(d.faultAction);

        s["activeStates"] = d.activeStates;
    }

    // Write rules array
    JsonArray rulesArr = doc["rules"].to<JsonArray>();
    rulesArr.clear();
    for (uint8_t i = 0; i < maxRules; i++) {
        const FaultRule& r = rules[i];
        if (r.sensorSlot >= MAX_SENSORS && r.faultBit == 0xFF) continue;  // Skip empty
        JsonObject ro = rulesArr.add<JsonObject>();
        ro["name"] = r.name;
        ro["sensor"] = r.sensorSlot;
        if (r.op == OP_DELTA) ro["sensorB"] = r.sensorSlotB;
        ro["op"] = ruleOpToStr(r.op);
        ro["a"] = r.thresholdA;
        if (r.op == OP_RANGE) ro["b"] = r.thresholdB;
        ro["bit"] = r.faultBit;
        ro["action"] = faultActionToStr(r.faultAction);
        ro["debounce"] = r.debounceMs;
        ro["running"] = r.requireRunning;
        // Gate conditions
        if (r.gateRpmMin > 0) ro["gateRpmMin"] = r.gateRpmMin;
        if (r.gateRpmMax > 0) ro["gateRpmMax"] = r.gateRpmMax;
        if (!isnan(r.gateMapMin)) ro["gateMapMin"] = r.gateMapMin;
        if (!isnan(r.gateMapMax)) ro["gateMapMax"] = r.gateMapMax;
        // Dynamic threshold curve
        if (r.curveSource != 0xFF) {
            ro["curveSource"] = r.curveSource;
            JsonArray cx = ro["curveX"].to<JsonArray>();
            JsonArray cy = ro["curveY"].to<JsonArray>();
            for (uint8_t j = 0; j < CURVE_POINTS; j++) {
                cx.add(r.curveX[j]);
                cy.add(r.curveY[j]);
            }
        }
    }

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

// --- Custom pin + output rule JSON helpers ---

#include "CustomPin.h"

static const char* cpinModeToStr(CustomPinMode m) {
    switch (m) {
        case CPIN_INPUT_POLL:  return "poll";
        case CPIN_INPUT_ISR:   return "isr";
        case CPIN_INPUT_TIMER: return "timer";
        case CPIN_ANALOG_IN:   return "analog";
        case CPIN_OUTPUT:      return "output";
        case CPIN_PWM_OUT:     return "pwm";
        default:               return "disabled";
    }
}
static CustomPinMode strToCpinMode(const char* s) {
    if (!s) return CPIN_DISABLED;
    if (strcmp(s, "poll") == 0)    return CPIN_INPUT_POLL;
    if (strcmp(s, "isr") == 0)     return CPIN_INPUT_ISR;
    if (strcmp(s, "timer") == 0)   return CPIN_INPUT_TIMER;
    if (strcmp(s, "analog") == 0)  return CPIN_ANALOG_IN;
    if (strcmp(s, "output") == 0)  return CPIN_OUTPUT;
    if (strcmp(s, "pwm") == 0)     return CPIN_PWM_OUT;
    return CPIN_DISABLED;
}
static const char* isrEdgeToStr(IsrEdge e) {
    switch (e) {
        case EDGE_FALLING: return "falling";
        case EDGE_CHANGE:  return "change";
        default:           return "rising";
    }
}
static IsrEdge strToIsrEdge(const char* s) {
    if (!s) return EDGE_RISING;
    if (strcmp(s, "falling") == 0) return EDGE_FALLING;
    if (strcmp(s, "change") == 0)  return EDGE_CHANGE;
    return EDGE_RISING;
}
static const char* oruleOpToStr(OutputRuleOp op) {
    switch (op) {
        case ORULE_GT:      return "gt";
        case ORULE_RANGE:   return "range";
        case ORULE_OUTSIDE: return "outside";
        case ORULE_DELTA:   return "delta";
        default:            return "lt";
    }
}
static OutputRuleOp strToOruleOp(const char* s) {
    if (!s) return ORULE_LT;
    if (strcmp(s, "gt") == 0)      return ORULE_GT;
    if (strcmp(s, "range") == 0)   return ORULE_RANGE;
    if (strcmp(s, "outside") == 0) return ORULE_OUTSIDE;
    if (strcmp(s, "delta") == 0)   return ORULE_DELTA;
    return ORULE_LT;
}
static const char* osrcToStr(OutputRuleSource s) {
    switch (s) {
        case OSRC_CUSTOM_PIN:    return "custom_pin";
        case OSRC_ENGINE_STATE:  return "engine_state";
        case OSRC_OUTPUT_STATE:  return "output_state";
        default:                 return "sensor";
    }
}
static OutputRuleSource strToOsrc(const char* s) {
    if (!s) return OSRC_SENSOR;
    if (strcmp(s, "custom_pin") == 0)    return OSRC_CUSTOM_PIN;
    if (strcmp(s, "engine_state") == 0)  return OSRC_ENGINE_STATE;
    if (strcmp(s, "output_state") == 0)  return OSRC_OUTPUT_STATE;
    return OSRC_SENSOR;
}

bool Config::loadCustomPins(const char* filename, CustomPinDescriptor* pins, uint8_t maxPins,
                             OutputRule* rules, uint8_t maxRules) {
    if (!_sdInitialized || !SD.exists(filename)) return false;
    fs::File file = SD.open(filename, FILE_READ);
    if (!file) return false;
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return false;

    JsonArray pinsArr = doc["pins"];
    if (pinsArr) {
        for (uint8_t i = 0; i < maxPins && i < pinsArr.size(); i++) {
            JsonObject po = pinsArr[i];
            CustomPinDescriptor& p = pins[i];
            p.clear();
            const char* nm = po["name"];
            if (nm) strncpy(p.name, nm, sizeof(p.name) - 1);
            p.pin = po["pin"] | 0;
            p.mode = strToCpinMode(po["mode"]);
            p.isrEdge = strToIsrEdge(po["isrEdge"]);
            p.intervalMs = po["interval"] | 1000;
            p.pwmFreq = po["pwmFreq"] | 25000;
            p.pwmResolution = po["pwmRes"] | 8;
            const char* cr = po["cron"];
            if (cr) strncpy(p.cron, cr, sizeof(p.cron) - 1);
            p.inMap = po["inMap"] | false;
        }
    }

    JsonArray rulesArr = doc["rules"];
    if (rulesArr) {
        for (uint8_t i = 0; i < maxRules && i < rulesArr.size(); i++) {
            JsonObject ro = rulesArr[i];
            OutputRule& r = rules[i];
            r.clear();
            const char* rname = ro["name"];
            if (rname) strncpy(r.name, rname, sizeof(r.name) - 1);
            r.enabled = ro["enabled"] | false;
            r.sourceType = strToOsrc(ro["sourceType"]);
            r.sourceSlot = ro["sourceSlot"] | 0xFF;
            r.sourceTypeB = strToOsrc(ro["sourceTypeB"]);
            r.sourceSlotB = ro["sourceSlotB"] | 0xFF;
            r.op = strToOruleOp(ro["op"]);
            r.thresholdA = ro["thresholdA"] | 0.0f;
            r.thresholdB = ro["thresholdB"] | 0.0f;
            r.hysteresis = ro["hysteresis"] | 0.0f;
            r.targetPin = ro["targetPin"] | 0xFF;
            r.onValue = ro["onValue"] | 1;
            r.offValue = ro["offValue"] | 0;
            r.debounceMs = ro["debounce"] | 0;
            r.requireRunning = ro["running"] | false;
            r.gateRpmMin = ro["gateRpmMin"] | 0;
            r.gateRpmMax = ro["gateRpmMax"] | 0;
            r.gateMapMin = ro["gateMapMin"].is<float>() ? (float)ro["gateMapMin"] : NAN;
            r.gateMapMax = ro["gateMapMax"].is<float>() ? (float)ro["gateMapMax"] : NAN;
            r.curveSource = ro["curveSource"] | 0xFF;
            if (r.curveSource != 0xFF) {
                JsonArray cx = ro["curveX"];
                JsonArray cy = ro["curveY"];
                for (uint8_t j = 0; j < 6; j++) {
                    r.curveX[j] = (cx && j < cx.size()) ? (float)cx[j] : 0.0f;
                    r.curveY[j] = (cy && j < cy.size()) ? (float)cy[j] : 0.0f;
                }
            }
        }
    }

    Serial.printf("Custom pins loaded: %d pins, %d rules\n",
                  pinsArr ? (int)pinsArr.size() : 0, rulesArr ? (int)rulesArr.size() : 0);
    return true;
}

bool Config::saveCustomPins(const char* filename, const CustomPinDescriptor* pins, uint8_t maxPins,
                             const OutputRule* rules, uint8_t maxRules) {
    if (!_sdInitialized) return false;

    JsonDocument doc;

    JsonArray pinsArr = doc["pins"].to<JsonArray>();
    for (uint8_t i = 0; i < maxPins; i++) {
        const CustomPinDescriptor& p = pins[i];
        if (p.mode == CPIN_DISABLED && p.name[0] == 0) continue;
        JsonObject po = pinsArr.add<JsonObject>();
        po["name"] = p.name;
        po["pin"] = p.pin;
        po["mode"] = cpinModeToStr(p.mode);
        po["isrEdge"] = isrEdgeToStr(p.isrEdge);
        po["interval"] = p.intervalMs;
        if (p.mode == CPIN_PWM_OUT) {
            po["pwmFreq"] = p.pwmFreq;
            po["pwmRes"] = p.pwmResolution;
        }
        if (p.mode == CPIN_INPUT_TIMER && p.cron[0]) {
            po["cron"] = p.cron;
        }
        if (p.inMap) po["inMap"] = true;
    }

    JsonArray rulesArr = doc["rules"].to<JsonArray>();
    for (uint8_t i = 0; i < maxRules; i++) {
        const OutputRule& r = rules[i];
        if (r.targetPin >= MAX_CUSTOM_PINS && !r.enabled) continue;
        JsonObject ro = rulesArr.add<JsonObject>();
        ro["name"] = r.name;
        ro["enabled"] = r.enabled;
        ro["sourceType"] = osrcToStr(r.sourceType);
        ro["sourceSlot"] = r.sourceSlot;
        if (r.op == ORULE_DELTA) {
            ro["sourceTypeB"] = osrcToStr(r.sourceTypeB);
            ro["sourceSlotB"] = r.sourceSlotB;
        }
        ro["op"] = oruleOpToStr(r.op);
        ro["thresholdA"] = r.thresholdA;
        if (r.op == ORULE_RANGE || r.op == ORULE_OUTSIDE) ro["thresholdB"] = r.thresholdB;
        ro["hysteresis"] = r.hysteresis;
        ro["targetPin"] = r.targetPin;
        ro["onValue"] = r.onValue;
        ro["offValue"] = r.offValue;
        ro["debounce"] = r.debounceMs;
        ro["running"] = r.requireRunning;
        if (r.gateRpmMin > 0) ro["gateRpmMin"] = r.gateRpmMin;
        if (r.gateRpmMax > 0) ro["gateRpmMax"] = r.gateRpmMax;
        if (!isnan(r.gateMapMin)) ro["gateMapMin"] = r.gateMapMin;
        if (!isnan(r.gateMapMax)) ro["gateMapMax"] = r.gateMapMax;
        if (r.curveSource != 0xFF) {
            ro["curveSource"] = r.curveSource;
            JsonArray cx = ro["curveX"].to<JsonArray>();
            JsonArray cy = ro["curveY"].to<JsonArray>();
            for (uint8_t j = 0; j < 6; j++) {
                cx.add(r.curveX[j]);
                cy.add(r.curveY[j]);
            }
        }
    }

    fs::File file = SD.open(filename, FILE_WRITE);
    if (!file) return false;
    serializeJson(doc, file);
    file.close();
    return true;
}
