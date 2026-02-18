#pragma once
#include <Arduino.h>

static const uint8_t MAX_MODULES      = 8;
static const uint8_t MAX_MODULE_RULES = 8;

// Module type (matches Modbus register 16 low byte from driver module firmware)
enum ModuleType : uint8_t {
    MOD_COIL_IGNITER = 0,   // COP with built-in igniter
    MOD_INJECTOR     = 1,   // High-Z injector driver
    MOD_COIL_DIRECT  = 2,   // Direct coil primary drive
    MOD_UNKNOWN      = 0xFF
};

// What data field a rule evaluates
enum ModuleRuleSource : uint8_t {
    MSRC_PEAK_CURRENT = 0,  // Channel peak current (A)
    MSRC_PULSE_WIDTH  = 1,  // Channel pulse width (ms)
    MSRC_ENERGY       = 2,  // Channel energy (mJ)
    MSRC_BOARD_TEMP   = 3,  // Module board temperature (°C)
    MSRC_HEALTH       = 4,  // Module health %
    MSRC_ERROR_COUNT  = 5   // Cumulative comm error count
};

// Rule operators
enum ModuleRuleOp : uint8_t {
    MROP_LT    = 0,   // value < thresholdA
    MROP_GT    = 1,   // value > thresholdA
    MROP_RANGE = 2,   // value outside [thresholdA, thresholdB]
    MROP_DELTA = 3    // |channel[A] - channel[B]| > thresholdA (imbalance)
};

// Fault/warning actions
enum ModuleFaultAction : uint8_t {
    MFAULT_NONE     = 0,
    MFAULT_WARN     = 1,   // CEL + log, dashboard yellow
    MFAULT_LIMP     = 2,   // Limp mode
    MFAULT_SHUTDOWN = 3    // Kill enable pin → module outputs OFF
};

// --- Module descriptor (named slot matched to a Modbus slave) ---
struct ModuleDescriptor {
    char name[20];             // User-assigned: "Coil Bank A", "Inj Left"
    uint32_t serialNumber;     // Match key (0 = match by address only)
    uint8_t address;           // Expected Modbus address (1-8, 0 = auto-match by serial)
    ModuleType expectedType;   // Expected module type

    // Per-channel thresholds (0 = disabled)
    float overcurrentA;        // Peak current fault threshold (A)
    float openCircuitMinA;     // Minimum expected current when driven (A)
    float maxPulseWidthMs;     // Max pulse width warning (ms)

    // Module-level thresholds (0 = disabled)
    float maxBoardTempC;       // Over-temp warning (°C)
    uint8_t minHealthPct;      // Min health before warning
    uint16_t maxErrors;        // Max cumulative errors before fault

    // Action on threshold breach
    ModuleFaultAction faultAction;

    // Enable pin control (MCP23S17 expander pin, 0 = not configured)
    uint16_t enablePin;

    // Runtime (not persisted)
    int8_t slaveIndex;         // Index into ModbusManager._slaves[] (-1 = not matched)
    bool matched;              // Currently matched to an online slave
    bool faulted;              // Any fault threshold exceeded
    bool warned;               // Any warning threshold exceeded

    void clear() {
        memset(this, 0, sizeof(*this));
        expectedType = MOD_UNKNOWN;
        faultAction = MFAULT_NONE;
        slaveIndex = -1;
    }
};

// --- Module rule (fault/warning rule sourced from module channel data) ---
struct ModuleRule {
    char name[16];              // Rule name: "CoilA_OvrCur", "InjL_Imbal"
    bool enabled;
    uint8_t moduleSlot;         // Index into ModuleDescriptor[] (0-7)
    uint8_t channel;            // Channel 0-3, or 0xFF for module-level (temp/health)
    ModuleRuleSource source;
    uint8_t channelB;           // Second channel for MROP_DELTA

    ModuleRuleOp op;
    float thresholdA;
    float thresholdB;           // For MROP_RANGE upper bound
    float hysteresis;
    uint32_t debounceMs;

    ModuleFaultAction faultAction;

    // RPM gates (0 = no gate)
    uint16_t gateRpmMin;
    uint16_t gateRpmMax;

    // Runtime (not persisted)
    uint32_t debounceStart;
    bool active;

    void clear() {
        memset(this, 0, sizeof(*this));
        moduleSlot = 0xFF;
        channel = 0xFF;
        channelB = 0xFF;
        faultAction = MFAULT_NONE;
    }
};
