#include "DriverModuleManager.h"
#include "ModbusManager.h"
#include "CustomPin.h"
#include "ECU.h"
#include "PinExpander.h"
#include "Logger.h"

extern Logger Log;

DriverModuleManager::DriverModuleManager() {
    for (uint8_t i = 0; i < MAX_MODULES; i++) _modules[i].clear();
    for (uint8_t i = 0; i < MAX_MODULE_RULES; i++) _rules[i].clear();
    memset(_unassigned, 0, sizeof(_unassigned));
}

void DriverModuleManager::begin(ModbusManager* modbus) {
    _modbus = modbus;
    Log.info("DriverModuleMgr", "Started, %d descriptor slots, %d rule slots",
             MAX_MODULES, MAX_MODULE_RULES);
}

void DriverModuleManager::update() {
    if (!_modbus || !_modbus->isEnabled()) return;
    matchModules();
    evaluateThresholds();
    evaluateRules();
}

// ── Module matching ──────────────────────────────────────────────

void DriverModuleManager::matchModules() {
    // Reset runtime match state
    for (uint8_t i = 0; i < MAX_MODULES; i++) {
        _modules[i].slaveIndex = -1;
        _modules[i].matched = false;
    }

    bool slaveUsed[8] = {};
    uint8_t slaveCount = _modbus->getSlaveCount();

    // Pass 1: match by serial number (strongest match)
    for (uint8_t m = 0; m < MAX_MODULES; m++) {
        if (_modules[m].name[0] == '\0') continue;  // Empty slot
        if (_modules[m].serialNumber == 0) continue; // No serial configured
        for (uint8_t s = 0; s < slaveCount; s++) {
            if (slaveUsed[s]) continue;
            const ModbusManager::SlaveData* slave = _modbus->getSlave(s);
            if (!slave || !slave->identified) continue;
            if (slave->serialNumber == _modules[m].serialNumber) {
                _modules[m].slaveIndex = s;
                _modules[m].matched = slave->online;
                slaveUsed[s] = true;
                break;
            }
        }
    }

    // Pass 2: match by address (for descriptors without serial)
    for (uint8_t m = 0; m < MAX_MODULES; m++) {
        if (_modules[m].name[0] == '\0') continue;
        if (_modules[m].slaveIndex >= 0) continue;  // Already matched
        if (_modules[m].address == 0) continue;
        for (uint8_t s = 0; s < slaveCount; s++) {
            if (slaveUsed[s]) continue;
            const ModbusManager::SlaveData* slave = _modbus->getSlave(s);
            if (!slave) continue;
            if (slave->address == _modules[m].address) {
                _modules[m].slaveIndex = s;
                _modules[m].matched = slave->online;
                slaveUsed[s] = true;
                break;
            }
        }
    }

    // Build unassigned list (online + identified slaves not matched to any descriptor)
    uint8_t prevCount = _unassignedCount;
    _unassignedCount = 0;
    for (uint8_t s = 0; s < slaveCount; s++) {
        if (slaveUsed[s]) continue;
        const ModbusManager::SlaveData* slave = _modbus->getSlave(s);
        if (!slave || !slave->online || !slave->identified) continue;
        if (_unassignedCount < 8) {
            _unassigned[_unassignedCount].address = slave->address;
            _unassigned[_unassignedCount].serialNumber = slave->serialNumber;
            _unassigned[_unassignedCount].moduleType = slave->moduleType;
            _unassignedCount++;
        }
    }

    // Log new unassigned modules (only when count increases)
    if (_unassignedCount > prevCount) {
        for (uint8_t i = prevCount; i < _unassignedCount; i++) {
            Log.info("DriverModuleMgr", "Unassigned module detected: addr=%d serial=0x%08X type=%d",
                     _unassigned[i].address, _unassigned[i].serialNumber, _unassigned[i].moduleType);
        }
    }
}

// ── Threshold evaluation (built-in per-descriptor checks) ────────

void DriverModuleManager::evaluateThresholds() {
    for (uint8_t m = 0; m < MAX_MODULES; m++) {
        ModuleDescriptor& mod = _modules[m];
        if (mod.name[0] == '\0' || mod.slaveIndex < 0) continue;

        const ModbusManager::SlaveData* slave = _modbus->getSlave(mod.slaveIndex);
        if (!slave || !slave->online) {
            // Module went offline — clear faulted state
            if (mod.faulted || mod.warned) {
                mod.faulted = false;
                mod.warned = false;
                char faultName[32];
                snprintf(faultName, sizeof(faultName), "MOD_%s", mod.name);
                fireFault(faultName, "Module offline, faults cleared", false);
            }
            continue;
        }

        bool newFault = false;
        bool newWarn = false;

        // Per-channel current thresholds
        for (uint8_t ch = 0; ch < 4; ch++) {
            const ModbusManager::ChannelData& cd = slave->channels[ch];

            // Overcurrent check
            if (mod.overcurrentA > 0 && cd.peakCurrentA > mod.overcurrentA) {
                newFault = true;
                if (!mod.faulted) {
                    char faultName[32], msg[64];
                    snprintf(faultName, sizeof(faultName), "MOD_%s_CH%d", mod.name, ch + 1);
                    snprintf(msg, sizeof(msg), "Overcurrent %.3fA > %.3fA", cd.peakCurrentA, mod.overcurrentA);
                    fireFault(faultName, msg, true);
                }
            }

            // Open circuit check (only meaningful if engine running and pulse detected)
            if (mod.openCircuitMinA > 0 && cd.pulseWidthMs > 0.1f &&
                cd.peakCurrentA < mod.openCircuitMinA) {
                newWarn = true;
                if (!mod.warned) {
                    char faultName[32], msg[64];
                    snprintf(faultName, sizeof(faultName), "MOD_%s_CH%d", mod.name, ch + 1);
                    snprintf(msg, sizeof(msg), "Open circuit %.3fA < %.3fA", cd.peakCurrentA, mod.openCircuitMinA);
                    fireFault(faultName, msg, true);
                }
            }

            // Max pulse width
            if (mod.maxPulseWidthMs > 0 && cd.pulseWidthMs > mod.maxPulseWidthMs) {
                newWarn = true;
            }
        }

        // Board temperature
        if (mod.maxBoardTempC > 0 && slave->boardTempC > mod.maxBoardTempC) {
            newWarn = true;
            if (!mod.warned) {
                char faultName[32], msg[64];
                snprintf(faultName, sizeof(faultName), "MOD_%s_TEMP", mod.name);
                snprintf(msg, sizeof(msg), "Board temp %.1fC > %.1fC", slave->boardTempC, mod.maxBoardTempC);
                fireFault(faultName, msg, true);
            }
        }

        // Health
        if (mod.minHealthPct > 0 && slave->healthPct < mod.minHealthPct) {
            newWarn = true;
        }

        // Cumulative errors
        if (mod.maxErrors > 0 && slave->errorCount > mod.maxErrors) {
            newFault = true;
        }

        // Handle state transitions
        if (newFault && !mod.faulted) {
            mod.faulted = true;
            if (mod.faultAction == MFAULT_SHUTDOWN) {
                setEnablePin(m, false);
                Log.warn("DriverModuleMgr", "SHUTDOWN: %s enable pin killed", mod.name);
            }
        } else if (!newFault && mod.faulted) {
            mod.faulted = false;
            char faultName[32];
            snprintf(faultName, sizeof(faultName), "MOD_%s", mod.name);
            fireFault(faultName, "Fault cleared", false);
            if (mod.faultAction == MFAULT_SHUTDOWN) {
                setEnablePin(m, true);
            }
        }

        if (newWarn && !mod.warned) {
            mod.warned = true;
        } else if (!newWarn && mod.warned) {
            mod.warned = false;
        }
    }
}

// ── Rule evaluation ──────────────────────────────────────────────

void DriverModuleManager::evaluateRules() {
    uint32_t now = millis();
    uint16_t rpm = _engineState ? _engineState->rpm : 0;

    for (uint8_t r = 0; r < MAX_MODULE_RULES; r++) {
        ModuleRule& rule = _rules[r];
        if (!rule.enabled || rule.moduleSlot >= MAX_MODULES) continue;

        ModuleDescriptor& mod = _modules[rule.moduleSlot];
        if (mod.slaveIndex < 0 || !mod.matched) continue;

        // RPM gates
        if (rule.gateRpmMin > 0 && rpm < rule.gateRpmMin) continue;
        if (rule.gateRpmMax > 0 && rpm > rule.gateRpmMax) continue;

        float val = getSourceValue(rule.moduleSlot, rule.source, rule.channel);
        bool condition = false;

        switch (rule.op) {
            case MROP_LT:
                condition = rule.active
                    ? (val < rule.thresholdA + rule.hysteresis)
                    : (val < rule.thresholdA);
                break;
            case MROP_GT:
                condition = rule.active
                    ? (val > rule.thresholdA - rule.hysteresis)
                    : (val > rule.thresholdA);
                break;
            case MROP_RANGE:
                condition = rule.active
                    ? (val < rule.thresholdA + rule.hysteresis || val > rule.thresholdB - rule.hysteresis)
                    : (val < rule.thresholdA || val > rule.thresholdB);
                break;
            case MROP_DELTA: {
                float valB = getSourceValue(rule.moduleSlot, rule.source, rule.channelB);
                float delta = fabsf(val - valB);
                condition = rule.active
                    ? (delta > rule.thresholdA - rule.hysteresis)
                    : (delta > rule.thresholdA);
                break;
            }
        }

        // Debounce
        if (condition) {
            if (!rule.active) {
                if (rule.debounceStart == 0) {
                    rule.debounceStart = now;
                } else if (now - rule.debounceStart >= rule.debounceMs) {
                    rule.active = true;
                    rule.debounceStart = 0;
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Rule %s triggered on %s", rule.name, mod.name);
                    fireFault(rule.name, msg, true);
                    if (rule.faultAction == MFAULT_SHUTDOWN) {
                        setEnablePin(rule.moduleSlot, false);
                    }
                }
            }
        } else {
            rule.debounceStart = 0;
            if (rule.active) {
                rule.active = false;
                fireFault(rule.name, "Rule cleared", false);
                if (rule.faultAction == MFAULT_SHUTDOWN) {
                    setEnablePin(rule.moduleSlot, true);
                }
            }
        }
    }
}

float DriverModuleManager::getSourceValue(uint8_t moduleSlot, ModuleRuleSource source, uint8_t channel) {
    if (moduleSlot >= MAX_MODULES) return 0;
    ModuleDescriptor& mod = _modules[moduleSlot];
    if (mod.slaveIndex < 0) return 0;

    const ModbusManager::SlaveData* slave = _modbus->getSlave(mod.slaveIndex);
    if (!slave) return 0;

    switch (source) {
        case MSRC_PEAK_CURRENT:
            return (channel < 4) ? slave->channels[channel].peakCurrentA : 0;
        case MSRC_PULSE_WIDTH:
            return (channel < 4) ? slave->channels[channel].pulseWidthMs : 0;
        case MSRC_ENERGY:
            return (channel < 4) ? slave->channels[channel].energyMj : 0;
        case MSRC_BOARD_TEMP:
            return slave->boardTempC;
        case MSRC_HEALTH:
            return (float)slave->healthPct;
        case MSRC_ERROR_COUNT:
            return (float)slave->errorCount;
        default:
            return 0;
    }
}

// ── Enable pin control ───────────────────────────────────────────

void DriverModuleManager::setEnablePin(uint8_t slot, bool state) {
    if (slot >= MAX_MODULES) return;
    uint16_t pin = _modules[slot].enablePin;
    if (pin == 0) return;

    // Use expander write for pins >= 200, GPIO for native pins
    if (pin >= 200) {
        xDigitalWrite(pin, state ? HIGH : LOW);
    } else {
        digitalWrite(pin, state ? HIGH : LOW);
    }
}

void DriverModuleManager::fireFault(const char* name, const char* msg, bool active) {
    if (_faultCb) _faultCb(name, msg, active);
}

// ── Status queries ───────────────────────────────────────────────

ModuleDescriptor* DriverModuleManager::getDescriptor(uint8_t slot) {
    return (slot < MAX_MODULES) ? &_modules[slot] : nullptr;
}

ModuleRule* DriverModuleManager::getRule(uint8_t slot) {
    return (slot < MAX_MODULE_RULES) ? &_rules[slot] : nullptr;
}

uint8_t DriverModuleManager::getMatchedCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_MODULES; i++) {
        if (_modules[i].matched) count++;
    }
    return count;
}

uint8_t DriverModuleManager::getFaultedCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_MODULES; i++) {
        if (_modules[i].faulted) count++;
    }
    return count;
}

uint8_t DriverModuleManager::getWarnedCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_MODULES; i++) {
        if (_modules[i].warned) count++;
    }
    return count;
}
