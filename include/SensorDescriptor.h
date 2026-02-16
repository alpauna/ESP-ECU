#pragma once

#include <Arduino.h>
#include <math.h>

static const uint8_t MAX_SENSORS = 16;
static const uint8_t MAX_RULES   = 8;
static const uint8_t MAX_AVG_SAMPLES = 32;

// --- Source types ---
enum SourceType : uint8_t {
    SRC_DISABLED    = 0,
    SRC_GPIO_ADC    = 1,   // Native ESP32 ADC (12-bit, 0-3.3V)
    SRC_GPIO_DIGITAL = 2,  // Digital GPIO input (HIGH/LOW)
    SRC_ADS1115     = 3,   // ADS1115 I2C ADC (16-bit)
    SRC_MCP3204     = 4,   // MCP3204 SPI ADC (12-bit, 0-5V)
    SRC_EXPANDER    = 5,   // MCP23017/MCP23S17 digital input
    SRC_ENGINE_STATE = 6,  // Read from EngineState field (sourceChannel selects field)
    SRC_OUTPUT_STATE = 7   // Read from output manager state (sourceChannel selects field)
};

// --- Calibration types ---
enum CalType : uint8_t {
    CAL_LINEAR   = 0,  // Linear: calA=vMin, calB=vMax, calC=engMin, calD=engMax
    CAL_NTC      = 1,  // NTC thermistor: calA=pullupOhms, calB=beta, calC=refVoltage(3.3)
    CAL_VDIVIDER = 2,  // Voltage divider: calA=ratio
    CAL_LOOKUP   = 3,  // Narrowband O2: calA=afrAt0v, calB=afrAt5v
    CAL_NONE     = 4   // Raw passthrough (digital, CJ125 override)
};

// --- Engine run state bitmask (for state-dependent activation) ---
enum EngineRunState : uint8_t {
    STATE_OFF      = 0x01,
    STATE_CRANKING = 0x02,
    STATE_RUNNING  = 0x04,
    STATE_ALL      = 0x07  // default — active in all states
};

// --- Fault action ---
enum FaultAction : uint8_t {
    FAULT_ACT_NONE     = 0,
    FAULT_ACT_LIMP     = 1,
    FAULT_ACT_SHUTDOWN  = 2,
    FAULT_ACT_CEL      = 3   // CEL only — no rev limit or advance cap
};

// --- Rule operators ---
enum RuleOp : uint8_t {
    OP_LT    = 0,  // sensor < thresholdA
    OP_GT    = 1,  // sensor > thresholdA
    OP_RANGE = 2,  // sensor outside [thresholdA, thresholdB]
    OP_DELTA = 3   // |sensor[A] - sensor[B]| > thresholdA
};

// --- Sensor descriptor ---
struct SensorDescriptor {
    char name[12];    // Short name: "MAP", "TPS", "CLT", etc.
    char unit[6];     // Display unit: "kPa", "%", "F", "V", "PSI"

    // Source
    SourceType sourceType;
    uint8_t sourceDevice;   // ADS1115: 0=@0x48, 1=@0x49. MCP3204: 0. Expander: 0-5
    uint8_t sourceChannel;  // Channel on device
    uint8_t sourcePin;      // GPIO pin (for SRC_GPIO_ADC/DIGITAL or fallback)

    // Calibration
    CalType calType;
    float calA, calB, calC, calD;

    // Filtering
    float emaAlpha;     // 0.0 = disabled, 0.01-1.0 = EMA weight
    uint8_t avgSamples; // 1 = no averaging, 2-32 = collect N then average

    // Validation
    float errorMin;     // Value below -> error fault (NAN = disabled)
    float errorMax;     // Value above -> error fault (NAN = disabled)
    float warnMin;      // Value below -> warning (NAN = disabled)
    float warnMax;      // Value above -> warning (NAN = disabled)
    float settleGuard;  // Skip validation if abs(rawValue) < guard

    // Fault
    uint8_t faultBit;   // Bit position in fault bitmask (0-7), 0xFF = none
    FaultAction faultAction;
    uint8_t activeStates; // Bitmask of EngineRunState values (default STATE_ALL=0x07)

    // Runtime (not persisted)
    float value;        // Current engineering value
    float rawVoltage;   // Current voltage after source read
    float rawFiltered;  // Filtered raw ADC value
    uint16_t rawAdc;    // Last raw ADC reading
    uint8_t avgCount;   // Current position in averaging buffer
    float avgBuffer[MAX_AVG_SAMPLES];
    bool inError;
    bool inWarning;

    void clear() {
        memset(this, 0, sizeof(*this));
        sourceType = SRC_DISABLED;
        calType = CAL_NONE;
        emaAlpha = 0.3f;
        avgSamples = 1;
        errorMin = NAN;
        errorMax = NAN;
        warnMin = NAN;
        warnMax = NAN;
        settleGuard = 0.0f;
        faultBit = 0xFF;
        faultAction = FAULT_ACT_NONE;
        activeStates = STATE_ALL;
        value = 0.0f;
        rawVoltage = 0.0f;
        rawFiltered = 0.0f;
        rawAdc = 0;
        avgCount = 0;
        inError = false;
        inWarning = false;
    }
};

// --- Fault rule ---
static const uint8_t CURVE_POINTS = 6;

struct FaultRule {
    char name[16];          // Rule name for display/logging
    uint8_t sensorSlot;     // Primary sensor index (0-15)
    uint8_t sensorSlotB;    // Secondary sensor for OP_DELTA (0-15)
    RuleOp op;
    float thresholdA;
    float thresholdB;       // Used for OP_RANGE upper bound
    uint8_t faultBit;       // Bit to set in fault bitmask (0-7)
    FaultAction faultAction;
    uint32_t debounceMs;    // Must be true for this long before triggering
    bool requireRunning;    // Only evaluate when engine is running

    // Gate conditions (rule only evaluates when ALL gates are satisfied)
    uint16_t gateRpmMin;    // 0 = no gate
    uint16_t gateRpmMax;    // 0 = no gate
    float gateMapMin;       // NAN = no gate
    float gateMapMax;       // NAN = no gate

    // Dynamic threshold curve (replaces static thresholdA when enabled)
    uint8_t curveSource;    // Engine state channel for X-axis (0xFF = disabled)
    float curveX[CURVE_POINTS];
    float curveY[CURVE_POINTS];

    // Runtime (not persisted)
    uint32_t debounceStart;
    bool active;

    void clear() {
        memset(this, 0, sizeof(*this));
        sensorSlot = 0xFF;
        sensorSlotB = 0xFF;
        faultBit = 0xFF;
        faultAction = FAULT_ACT_NONE;
        gateMapMin = NAN;
        gateMapMax = NAN;
        curveSource = 0xFF;
    }
};
