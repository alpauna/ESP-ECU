#ifndef MODBUSMANAGER_H
#define MODBUSMANAGER_H

#include <Arduino.h>
#include <ModbusMaster.h>
#include <TaskSchedulerDeclarations.h>
#include <functional>

class ECU;

class ModbusManager {
public:
    typedef std::function<void(const char* fault, const char* message, bool active)> FaultCallback;

    struct ChannelData {
        float peakCurrentA;       // Peak current (A)
        float pulseWidthMs;       // Pulse width (ms)
        float energyMj;           // Pulse energy (mJ)
        uint8_t faultFlags;       // Bit flags: 0x01=overcurrent, 0x02=open, 0x04=short, 0x08=thermal
        uint16_t rawAdc;          // Raw ADC value
    };

    struct SlaveData {
        uint8_t address;          // Modbus slave address (1-247)
        bool online;              // Responded to last poll
        uint32_t lastResponseMs;  // millis() of last good response
        uint16_t errorCount;      // Cumulative comm errors
        uint8_t consecutiveErrors;// Consecutive errors (offline after 3)
        uint8_t moduleType;       // 0=coil, 1=injector
        float boardTempC;         // Module PCB temperature
        uint8_t healthPct;        // Module self-reported health 0-100
        uint32_t fwVersion;       // Firmware version (read once)
        uint32_t serialNumber;    // Serial number (read once)
        bool identified;          // Input registers read successfully
        ChannelData channels[4];  // 4 channels per module
    };

    ModbusManager(Scheduler* scheduler);
    ~ModbusManager();

    void begin(uint8_t txPin, uint8_t rxPin,
               uint32_t baud = 9600, uint8_t maxSlaves = 4);
    void setECU(ECU* ecu) { _ecu = ecu; }
    void setFaultCallback(FaultCallback cb) { _faultCb = cb; }

    const SlaveData* getSlave(uint8_t index) const;
    uint8_t getSlaveCount() const { return _maxSlaves; }
    uint8_t getOnlineCount() const;
    bool isEnabled() const { return _enabled; }

private:
    void poll();
    void readSlaveRegisters(uint8_t idx);
    void readSlaveIdentity(uint8_t idx);
    void parseHoldingRegisters(uint8_t idx);

    ModbusMaster _node;
    Scheduler* _ts;
    Task* _taskPoll;
    ECU* _ecu = nullptr;
    FaultCallback _faultCb;

    SlaveData _slaves[8];
    uint8_t _maxSlaves = 4;
    uint8_t _currentSlave = 0;
    bool _enabled = false;

    static const uint8_t OFFLINE_THRESHOLD = 3;
    static const uint32_t RETRY_INTERVAL_MS = 5000;
};

#endif
