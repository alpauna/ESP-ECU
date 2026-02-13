#include "Logger.h"
#include <ESPAsyncWebServer.h>
#include <stdarg.h>
#include <time.h>
#include <WiFi.h>

Logger Log;

static const char* LEVEL_NAMES[] = {"ERROR", "WARN ", "INFO ", "DEBUG"};

Logger::Logger()
    : _level(LOG_INFO), _serialEnabled(true), _mqttEnabled(false),
      _sdCardEnabled(false), _wsEnabled(false), _mqttClient(nullptr),
      _mqttTopic("ecu/log"), _ws(nullptr), _sdReady(false),
      _logFilename("/log.txt"), _maxFileSize(DEFAULT_MAX_FILE_SIZE),
      _maxRotatedFiles(DEFAULT_MAX_ROTATED_FILES), _compressionAvailable(true),
      _ringBufferMax(DEFAULT_RING_BUFFER_SIZE), _ringBufferHead(0), _ringBufferCount(0) {
    _ringBuffer.resize(_ringBufferMax);
}

void Logger::setLevel(Level level) { _level = level; }
Logger::Level Logger::getLevel() { return _level; }
const char* Logger::getLevelName(Level level) {
    if (level >= 0 && level <= LOG_DEBUG) return LEVEL_NAMES[level];
    return "UNKN ";
}

void Logger::setMqttClient(AsyncMqttClient* client, const char* topic) {
    _mqttClient = client;
    _mqttTopic = topic;
    _mqttEnabled = (client != nullptr);
}

void Logger::setLogFile(const char* filename, uint32_t maxFileSize, uint8_t maxRotatedFiles) {
    _logFilename = filename;
    _maxFileSize = maxFileSize;
    _maxRotatedFiles = maxRotatedFiles;
    _sdReady = true;
    _sdCardEnabled = true;
}

void Logger::enableSerial(bool enable) { _serialEnabled = enable; }
void Logger::enableMqtt(bool enable) { _mqttEnabled = enable && (_mqttClient != nullptr); }
void Logger::enableSdCard(bool enable) { _sdCardEnabled = enable && _sdReady; }
bool Logger::isSerialEnabled() { return _serialEnabled; }
bool Logger::isMqttEnabled() { return _mqttEnabled; }
bool Logger::isSdCardEnabled() { return _sdCardEnabled; }

void Logger::setWebSocket(AsyncWebSocket* ws) { _ws = ws; _wsEnabled = (ws != nullptr); }
void Logger::enableWebSocket(bool enable) { _wsEnabled = enable && (_ws != nullptr); }
bool Logger::isWebSocketEnabled() { return _wsEnabled; }

void Logger::setRingBufferSize(size_t maxEntries) {
    _ringBufferMax = maxEntries;
    _ringBuffer.resize(_ringBufferMax);
    _ringBufferHead = 0;
    _ringBufferCount = 0;
}

const std::vector<String>& Logger::getRingBuffer() const { return _ringBuffer; }
size_t Logger::getRingBufferHead() const { return _ringBufferHead; }
size_t Logger::getRingBufferCount() const { return _ringBufferCount; }

void Logger::addToRingBuffer(const char* msg) {
    _ringBuffer[_ringBufferHead] = String(msg);
    _ringBufferHead = (_ringBufferHead + 1) % _ringBufferMax;
    if (_ringBufferCount < _ringBufferMax) _ringBufferCount++;
}

void Logger::writeToWebSocket(const char* msg) {
    if (_ws == nullptr || _ws->count() == 0) return;
    String json = "{\"type\":\"log\",\"message\":\"";
    for (const char* p = msg; *p; p++) {
        switch (*p) {
            case '"':  json += "\\\""; break;
            case '\\': json += "\\\\"; break;
            case '\n': json += "\\n"; break;
            case '\r': json += "\\r"; break;
            case '\t': json += "\\t"; break;
            default:   json += *p; break;
        }
    }
    json += "\"}";
    _ws->textAll(json);
}

void Logger::error(const char* tag, const char* format, ...) {
    if (_level >= LOG_ERROR) { va_list args; va_start(args, format); log(LOG_ERROR, tag, format, args); va_end(args); }
}
void Logger::warn(const char* tag, const char* format, ...) {
    if (_level >= LOG_WARN) { va_list args; va_start(args, format); log(LOG_WARN, tag, format, args); va_end(args); }
}
void Logger::info(const char* tag, const char* format, ...) {
    if (_level >= LOG_INFO) { va_list args; va_start(args, format); log(LOG_INFO, tag, format, args); va_end(args); }
}
void Logger::debug(const char* tag, const char* format, ...) {
    if (_level >= LOG_DEBUG) { va_list args; va_start(args, format); log(LOG_DEBUG, tag, format, args); va_end(args); }
}

void Logger::log(Level level, const char* tag, const char* format, va_list args) {
    char msgBuffer[384];
    vsnprintf(msgBuffer, sizeof(msgBuffer), format, args);
    struct tm timeinfo;
    char timeStr[20] = "----/--/-- --:--:--";
    // Zero timeout — never block. Time becomes available after NTP sync.
    if (getLocalTime(&timeinfo, 0)) {
        strftime(timeStr, sizeof(timeStr), "%Y/%m/%d %H:%M:%S", &timeinfo);
    }
    snprintf(_buffer, sizeof(_buffer), "[%s] [%s] [%s] %s", timeStr, getLevelName(level), tag, msgBuffer);
    addToRingBuffer(_buffer);
    if (_serialEnabled) writeToSerial(_buffer);
    if (_mqttEnabled) writeToMqtt(_buffer);
    if (_sdCardEnabled) writeToSdCard(_buffer);
    if (_wsEnabled) writeToWebSocket(_buffer);
}

void Logger::writeToSerial(const char* msg) { Serial.println(msg); }

void Logger::writeToMqtt(const char* msg) {
    if (_mqttClient == nullptr || !_mqttClient->connected()) return;
    _mqttClient->publish(_mqttTopic.c_str(), 0, false, msg);
}

void Logger::writeToSdCard(const char* msg) {
    if (!_sdReady) return;
    fs::File logFile = SD.open(_logFilename.c_str(), FILE_READ);
    if (logFile) {
        size_t sz = logFile.size();
        logFile.close();
        if (sz > _maxFileSize) rotateLogFiles();
    }
    logFile = SD.open(_logFilename.c_str(), FILE_APPEND);
    if (!logFile) return;
    logFile.println(msg);
    logFile.close();
}

String Logger::getRotatedFilename(uint8_t index) {
    int dotIndex = _logFilename.lastIndexOf('.');
    String baseName = (dotIndex > 0) ? _logFilename.substring(0, dotIndex) : _logFilename;
    return baseName + "." + String(index) + ".tar.gz";
}

void Logger::rotateLogFiles() {
    if (!_sdReady) return;
    String oldestFile = getRotatedFilename(_maxRotatedFiles);
    if (SD.exists(oldestFile.c_str())) SD.remove(oldestFile.c_str());
    for (int i = _maxRotatedFiles - 1; i >= 1; i--) {
        String oldName = getRotatedFilename(i);
        String newName = getRotatedFilename(i + 1);
        if (SD.exists(oldName.c_str())) SD.rename(oldName.c_str(), newName.c_str());
    }
    String compressedName = getRotatedFilename(1);
    if (_compressionAvailable && compressFile(_logFilename.c_str(), compressedName.c_str())) {
        Serial.printf("[Logger] Compressed %s -> %s\n", _logFilename.c_str(), compressedName.c_str());
    } else {
        int dotIdx = _logFilename.lastIndexOf('.');
        String baseName = (dotIdx > 0) ? _logFilename.substring(0, dotIdx) : _logFilename;
        String fallbackName = baseName + ".1.txt";
        SD.rename(_logFilename.c_str(), fallbackName.c_str());
    }
}

bool Logger::compressFile(const char* srcPath, const char* destPath) {
    if (!_sdReady || !_compressionAvailable) return false;
    fs::File srcFile = SD.open(srcPath, FILE_READ);
    if (!srcFile) return false;
    srcFile.close();
    String tmpDir = "/_log_rotate";
    String tmpPath = tmpDir + "/" + String(srcPath);
    SD.mkdir(tmpDir.c_str());
    SD.rename(srcPath, tmpPath.c_str());
    fs::File outFile = SD.open(destPath, FILE_WRITE);
    if (!outFile) { SD.rename(tmpPath.c_str(), srcPath); SD.rmdir(tmpDir.c_str()); return false; }
    size_t result = TarGzPacker::compress(&SD, tmpDir.c_str(), &outFile);
    outFile.close();
    bool success = (result > 0);
    if (success) { SD.remove(tmpPath.c_str()); }
    else { SD.rename(tmpPath.c_str(), srcPath); if (SD.exists(destPath)) SD.remove(destPath); }
    SD.rmdir(tmpDir.c_str());
    return success;
}
