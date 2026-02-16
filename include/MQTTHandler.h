#ifndef MQTTHANDLER_H
#define MQTTHANDLER_H

#include <Arduino.h>
#include <AsyncMqttClient.h>
#include <WiFi.h>
#include <TaskSchedulerDeclarations.h>
#include "Logger.h"

class ECU;

class MQTTHandler {
public:
    MQTTHandler(Scheduler* ts);
    void begin(const IPAddress& host, uint16_t port,
               const String& user, const String& password);
    AsyncMqttClient* getClient() { return &_client; }
    bool connected() const { return _client.connected(); }
    void setECU(ECU* ecu);
    void publishState();
    void publishFault(const char* fault, const char* message, bool active);
    void publishDiag();
    void startReconnect();
    void stopReconnect();
    void disconnect();

private:
    AsyncMqttClient _client;
    Scheduler* _ts;
    Task* _tReconnect;
    ECU* _ecu;

    void onConnect(bool sessionPresent);
    void onDisconnect(AsyncMqttClientDisconnectReason reason);
    void onSubscribe(uint16_t packetId, uint8_t qos);
    void onUnsubscribe(uint16_t packetId);
    void onMessage(char* topic, char* payload,
                   AsyncMqttClientMessageProperties properties,
                   size_t len, size_t index, size_t total);
    void onPublish(uint16_t packetId);
};

#endif
