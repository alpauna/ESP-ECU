#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <AsyncMqttClient.h>
class AsyncWebSocket;
#include <SD.h>
#include <ESP32-targz.h>
#include <vector>

class Logger {
public:
    enum Level { LOG_ERROR = 0, LOG_WARN = 1, LOG_INFO = 2, LOG_DEBUG = 3 };

    static const uint32_t DEFAULT_MAX_FILE_SIZE = 50 * 1024 * 1024;
    static const uint8_t DEFAULT_MAX_ROTATED_FILES = 10;
    static const size_t DEFAULT_RING_BUFFER_SIZE = 500;

    Logger();

    void setLevel(Level level);
    Level getLevel();
    const char* getLevelName(Level level);

    void error(const char* tag, const char* format, ...);
    void warn(const char* tag, const char* format, ...);
    void info(const char* tag, const char* format, ...);
    void debug(const char* tag, const char* format, ...);

    void setMqttClient(AsyncMqttClient* client, const char* topic);
    void setLogFile(const char* filename,
                    uint32_t maxFileSize = DEFAULT_MAX_FILE_SIZE,
                    uint8_t maxRotatedFiles = DEFAULT_MAX_ROTATED_FILES);

    void setWebSocket(AsyncWebSocket* ws);
    void setRingBufferSize(size_t maxEntries);
    const std::vector<String>& getRingBuffer() const;
    size_t getRingBufferHead() const;
    size_t getRingBufferCount() const;

    void enableSerial(bool enable);
    void enableMqtt(bool enable);
    void enableSdCard(bool enable);
    void enableWebSocket(bool enable);

    bool isSerialEnabled();
    bool isMqttEnabled();
    bool isSdCardEnabled();
    bool isWebSocketEnabled();

private:
    void log(Level level, const char* tag, const char* format, va_list args);
    void writeToSerial(const char* msg);
    void writeToMqtt(const char* msg);
    void writeToSdCard(const char* msg);
    void writeToWebSocket(const char* msg);
    void addToRingBuffer(const char* msg);
    void rotateLogFiles();
    bool compressFile(const char* srcPath, const char* destPath);
    String getRotatedFilename(uint8_t index);

    Level _level;
    bool _serialEnabled;
    bool _mqttEnabled;
    bool _sdCardEnabled;
    bool _wsEnabled;

    AsyncMqttClient* _mqttClient;
    String _mqttTopic;
    AsyncWebSocket* _ws;

    bool _sdReady;
    String _logFilename;
    uint32_t _maxFileSize;
    uint8_t _maxRotatedFiles;
    bool _compressionAvailable;

    std::vector<String> _ringBuffer;
    size_t _ringBufferMax;
    size_t _ringBufferHead;
    size_t _ringBufferCount;

    char _buffer[512];
};

extern Logger Log;

#endif
