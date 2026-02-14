#include "MQTTHandler.h"
#include "ECU.h"
#include "TransmissionManager.h"
#include <ArduinoJson.h>

MQTTHandler::MQTTHandler(Scheduler* ts)
    : _ts(ts), _tReconnect(nullptr), _ecu(nullptr) {}

void MQTTHandler::begin(const IPAddress& host, uint16_t port,
                         const String& user, const String& password) {
    _client.onConnect([this](bool sp) { onConnect(sp); });
    _client.onDisconnect([this](AsyncMqttClientDisconnectReason r) { onDisconnect(r); });
    _client.onSubscribe([this](uint16_t id, uint8_t qos) { onSubscribe(id, qos); });
    _client.onUnsubscribe([this](uint16_t id) { onUnsubscribe(id); });
    _client.onMessage([this](char* t, char* p, AsyncMqttClientMessageProperties props,
                             size_t len, size_t idx, size_t total) {
        onMessage(t, p, props, len, idx, total);
    });
    _client.onPublish([this](uint16_t id) { onPublish(id); });
    _client.setServer(host, port);
    _client.setCredentials(user.c_str(), password.c_str());

    _tReconnect = new Task(10 * TASK_SECOND, TASK_FOREVER, [this]() {
        if (_client.connected()) { _tReconnect->disable(); return; }
        Log.info("MQTT", "Connecting to MQTT...");
        _client.connect();
    }, _ts, false);
}

void MQTTHandler::startReconnect() { if (_tReconnect) _tReconnect->enableDelayed(); }
void MQTTHandler::stopReconnect() { if (_tReconnect) _tReconnect->disable(); }
void MQTTHandler::disconnect() { _client.disconnect(); }
void MQTTHandler::setECU(ECU* ecu) { _ecu = ecu; }

void MQTTHandler::publishState() {
    if (!_client.connected() || _ecu == nullptr) return;
    const EngineState& s = _ecu->getState();

    JsonDocument doc;
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
    doc["running"] = s.engineRunning;
    doc["cranking"] = s.cranking;
    doc["sequential"] = s.sequentialMode;

    // Transmission
    TransmissionManager* trans = _ecu->getTransmission();
    if (trans) {
        const TransmissionState& ts = trans->getState();
        JsonObject transObj = doc["trans"].to<JsonObject>();
        transObj["gear"] = TransmissionManager::gearToString(ts.currentGear);
        transObj["mlps"] = TransmissionManager::mlpsToString(ts.mlpsPosition);
        transObj["oss"] = ts.ossRpm;
        transObj["tss"] = ts.tssRpm;
        transObj["tft"] = ts.tftTempF;
        transObj["tcc"] = ts.tccDuty;
        transObj["epc"] = ts.epcDuty;
        transObj["shift"] = ts.shifting;
        transObj["slip"] = ts.slipRpm;
    }

    char buf[768];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    _client.publish("ecu/state", 0, false, buf, len);
}

void MQTTHandler::publishFault(const char* fault, const char* message, bool active) {
    if (!_client.connected()) return;
    JsonDocument doc;
    doc["fault"] = fault;
    doc["message"] = message;
    doc["active"] = active;
    char buf[256];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    _client.publish("ecu/fault", 0, false, buf, len);
}

void MQTTHandler::onConnect(bool sessionPresent) {
    Log.info("MQTT", "Connected (session: %s)", sessionPresent ? "yes" : "no");
    if (_tReconnect) _tReconnect->disable();
}

void MQTTHandler::onDisconnect(AsyncMqttClientDisconnectReason reason) {
    Log.warn("MQTT", "Disconnected (reason: %d)", (int)reason);
    if (WiFi.isConnected()) startReconnect();
}

void MQTTHandler::onSubscribe(uint16_t packetId, uint8_t qos) {
    Serial.printf("MQTT subscribe ack: %d qos:%d\n", packetId, qos);
}

void MQTTHandler::onUnsubscribe(uint16_t packetId) {
    Serial.printf("MQTT unsubscribe ack: %d\n", packetId);
}

void MQTTHandler::onMessage(char* topic, char* payload,
                             AsyncMqttClientMessageProperties properties,
                             size_t len, size_t index, size_t total) {
    Serial.printf("MQTT msg: %s len:%d\n", topic, len);
}

void MQTTHandler::onPublish(uint16_t packetId) {}
