#include "ModbusManager.h"
#include "ECU.h"
#include "Logger.h"

// ── Register map (per slave module) ──────────────────────────────
// Holding registers (function 0x03):
//   0-3:   Ch1-4 peak current  (uint16, 0.001A units)
//   4-7:   Ch1-4 pulse width   (uint16, 0.01ms units)
//   8-11:  Ch1-4 energy        (uint16, 0.1mJ units)
//   12-15: Ch1-4 fault flags   (uint16, bit-packed)
//   16:    Module health (high byte) + type (low byte)
//   17:    Board temperature    (int16, 0.1°C units)
//
// Input registers (function 0x04, read once on first contact):
//   0-1:   Firmware version (uint32, high word first)
//   2-3:   Serial number   (uint32, high word first)

static const uint8_t  HOLDING_REG_COUNT = 18;
static const uint8_t  INPUT_REG_COUNT   = 4;

ModbusManager::ModbusManager(Scheduler* scheduler)
    : _ts(scheduler), _taskPoll(nullptr)
{
    memset(_slaves, 0, sizeof(_slaves));
    // Assign default sequential addresses 1-8
    for (uint8_t i = 0; i < 8; i++) {
        _slaves[i].address = i + 1;
    }
}

ModbusManager::~ModbusManager() {
    if (_taskPoll) {
        _taskPoll->disable();
        delete _taskPoll;
        _taskPoll = nullptr;
    }
}

void ModbusManager::begin(uint8_t txPin, uint8_t rxPin,
                          uint32_t baud, uint8_t maxSlaves) {
    _maxSlaves = min(maxSlaves, (uint8_t)8);
    _enabled = true;

    // Init UART1 with arbitrary pin mapping (ESP32-S3 IO MUX)
    Serial1.begin(baud, SERIAL_8N1, rxPin, txPin);

    // Init ModbusMaster on Serial1 — address will be set per-slave before each transaction
    _node.begin(1, Serial1);
    // No pre/post transmission callbacks — auto-direction RS-485 hardware

    // Create poll task: 100ms interval, infinite iterations
    _taskPoll = new Task(100 * TASK_MILLISECOND, TASK_FOREVER,
        [this]() { poll(); }, _ts, false);
    _taskPoll->enable();

    Log.info("ModbusManager", "Started on UART1 TX=%d RX=%d @ %d baud, %d slaves",
             txPin, rxPin, baud, _maxSlaves);
}

void ModbusManager::poll() {
    if (!_enabled || _maxSlaves == 0) return;

    // Advance round-robin
    _currentSlave = (_currentSlave + 1) % _maxSlaves;
    SlaveData& slave = _slaves[_currentSlave];

    // Skip offline slaves unless retry interval elapsed
    if (!slave.online && slave.errorCount > 0) {
        uint32_t elapsed = millis() - slave.lastResponseMs;
        if (elapsed < RETRY_INTERVAL_MS) return;
    }

    // Set slave address for this transaction
    _node.begin(slave.address, Serial1);

    // Read identity once on first successful contact
    if (!slave.identified) {
        readSlaveIdentity(_currentSlave);
    }

    // Read holding registers
    readSlaveRegisters(_currentSlave);
}

void ModbusManager::readSlaveRegisters(uint8_t idx) {
    SlaveData& slave = _slaves[idx];

    uint8_t result = _node.readHoldingRegisters(0, HOLDING_REG_COUNT);

    if (result == _node.ku8MBSuccess) {
        slave.online = true;
        slave.lastResponseMs = millis();
        slave.consecutiveErrors = 0;

        // Store previous fault flags for change detection
        uint8_t prevFaults[4];
        for (uint8_t ch = 0; ch < 4; ch++) {
            prevFaults[ch] = slave.channels[ch].faultFlags;
        }

        parseHoldingRegisters(idx);

        // Fire fault callback on new faults
        if (_faultCb) {
            for (uint8_t ch = 0; ch < 4; ch++) {
                uint8_t newFaults = slave.channels[ch].faultFlags & ~prevFaults[ch];
                if (newFaults) {
                    char faultName[32];
                    snprintf(faultName, sizeof(faultName), "MODBUS_S%d_CH%d", slave.address, ch + 1);
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Fault flags 0x%02X on slave %d ch %d",
                             slave.channels[ch].faultFlags, slave.address, ch + 1);
                    _faultCb(faultName, msg, true);
                }
                // Clear faults
                uint8_t clearedFaults = prevFaults[ch] & ~slave.channels[ch].faultFlags;
                if (clearedFaults) {
                    char faultName[32];
                    snprintf(faultName, sizeof(faultName), "MODBUS_S%d_CH%d", slave.address, ch + 1);
                    _faultCb(faultName, "Fault cleared", false);
                }
            }
        }
    } else {
        slave.consecutiveErrors++;
        slave.errorCount++;
        if (slave.consecutiveErrors >= OFFLINE_THRESHOLD) {
            if (slave.online) {
                slave.online = false;
                Log.warn("ModbusManager", "Slave %d offline after %d errors",
                         slave.address, OFFLINE_THRESHOLD);
                if (_faultCb) {
                    char faultName[32];
                    snprintf(faultName, sizeof(faultName), "MODBUS_S%d_OFFLINE", slave.address);
                    _faultCb(faultName, "Slave offline", true);
                }
            }
        }
    }
}

void ModbusManager::readSlaveIdentity(uint8_t idx) {
    SlaveData& slave = _slaves[idx];

    uint8_t result = _node.readInputRegisters(0, INPUT_REG_COUNT);
    if (result == _node.ku8MBSuccess) {
        slave.fwVersion    = ((uint32_t)_node.getResponseBuffer(0) << 16) | _node.getResponseBuffer(1);
        slave.serialNumber = ((uint32_t)_node.getResponseBuffer(2) << 16) | _node.getResponseBuffer(3);
        slave.identified = true;
        Log.info("ModbusManager", "Slave %d identified: FW=%08X SN=%08X",
                 slave.address, slave.fwVersion, slave.serialNumber);
    }
    // Don't mark as error if identity read fails — slave may not support input registers
}

void ModbusManager::parseHoldingRegisters(uint8_t idx) {
    SlaveData& slave = _slaves[idx];

    // Channels 0-3
    for (uint8_t ch = 0; ch < 4; ch++) {
        slave.channels[ch].peakCurrentA = _node.getResponseBuffer(ch)      * 0.001f;
        slave.channels[ch].pulseWidthMs = _node.getResponseBuffer(4 + ch)  * 0.01f;
        slave.channels[ch].energyMj     = _node.getResponseBuffer(8 + ch)  * 0.1f;
        slave.channels[ch].faultFlags   = (uint8_t)_node.getResponseBuffer(12 + ch);
        slave.channels[ch].rawAdc       = _node.getResponseBuffer(12 + ch) >> 8;
    }

    // Register 16: health (high byte) + type (low byte)
    uint16_t reg16 = _node.getResponseBuffer(16);
    slave.healthPct  = (reg16 >> 8) & 0xFF;
    slave.moduleType = reg16 & 0xFF;

    // Register 17: board temperature (int16, 0.1°C)
    slave.boardTempC = (int16_t)_node.getResponseBuffer(17) * 0.1f;
}

const ModbusManager::SlaveData* ModbusManager::getSlave(uint8_t index) const {
    if (index >= _maxSlaves) return nullptr;
    return &_slaves[index];
}

uint8_t ModbusManager::getOnlineCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < _maxSlaves; i++) {
        if (_slaves[i].online) count++;
    }
    return count;
}
