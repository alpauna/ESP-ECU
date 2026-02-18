#pragma once

#include <Arduino.h>
#include <functional>
#include "DriverModule.h"

class ModbusManager;
class CustomPinManager;
struct EngineState;

class DriverModuleManager {
public:
    typedef std::function<void(const char* fault, const char* message, bool active)> FaultCallback;

    struct UnassignedModule {
        uint8_t address;
        uint32_t serialNumber;
        uint8_t moduleType;
    };

    DriverModuleManager();

    void begin(ModbusManager* modbus);
    void update();  // Called every 500ms — match modules, evaluate thresholds and rules

    // Descriptor / rule access (mutable for config loading)
    ModuleDescriptor* getDescriptor(uint8_t slot);
    ModuleRule* getRule(uint8_t slot);
    const ModuleDescriptor* getDescriptors() const { return _modules; }
    const ModuleRule* getRules() const { return _rules; }

    // Status
    uint8_t getMatchedCount() const;
    uint8_t getFaultedCount() const;
    uint8_t getWarnedCount() const;

    // Unassigned modules (online slaves not matched to any descriptor)
    const UnassignedModule* getUnassigned() const { return _unassigned; }
    uint8_t getUnassignedCount() const { return _unassignedCount; }

    // Wiring
    void setFaultCallback(FaultCallback cb) { _faultCb = cb; }
    void setEngineState(const EngineState* es) { _engineState = es; }
    void setCustomPinManager(CustomPinManager* cpm) { _customPins = cpm; }

private:
    ModuleDescriptor _modules[MAX_MODULES];
    ModuleRule _rules[MAX_MODULE_RULES];
    UnassignedModule _unassigned[8];
    uint8_t _unassignedCount = 0;

    ModbusManager* _modbus = nullptr;
    const EngineState* _engineState = nullptr;
    CustomPinManager* _customPins = nullptr;
    FaultCallback _faultCb;

    void matchModules();
    void evaluateThresholds();
    void evaluateRules();
    float getSourceValue(uint8_t moduleSlot, ModuleRuleSource source, uint8_t channel);
    void fireFault(const char* name, const char* msg, bool active);
    void setEnablePin(uint8_t slot, bool state);
};
