#pragma once
#include <Arduino.h>
#include <esp_timer.h>

class SensorManager;
struct EngineState;

static const uint8_t MAX_CUSTOM_PINS  = 16;
static const uint8_t MAX_OUTPUT_RULES = 16;

// Custom pin modes
enum CustomPinMode : uint8_t {
    CPIN_DISABLED = 0,
    CPIN_INPUT_POLL,     // Periodic digitalRead at intervalMs
    CPIN_INPUT_ISR,      // Interrupt-driven (edge trigger, counts pulses)
    CPIN_INPUT_TIMER,    // esp_timer callback at exact intervalMs
    CPIN_ANALOG_IN,      // Periodic analogRead at intervalMs
    CPIN_OUTPUT,         // Digital output (manual or rule-linked)
    CPIN_PWM_OUT         // PWM output (manual or rule-linked)
};

enum IsrEdge : uint8_t {
    EDGE_RISING = 0,
    EDGE_FALLING,
    EDGE_CHANGE
};

// Output rule operators
enum OutputRuleOp : uint8_t {
    ORULE_LT = 0,       // source < thresholdA -> activate
    ORULE_GT,            // source > thresholdA -> activate
    ORULE_RANGE,         // thresholdA <= source <= thresholdB -> activate
    ORULE_OUTSIDE,       // source < thresholdA OR source > thresholdB -> activate
    ORULE_DELTA          // |sourceA - sourceB| > thresholdA -> activate
};

// Output rule source types
enum OutputRuleSource : uint8_t {
    OSRC_SENSOR = 0,     // SensorDescriptor slot (0-15)
    OSRC_CUSTOM_PIN,     // CustomPinDescriptor slot (0-15)
    OSRC_ENGINE_STATE,   // EngineState channel
    OSRC_OUTPUT_STATE    // Manager output channel
};

struct CustomPinDescriptor {
    char name[16];
    uint16_t pin;              // GPIO (0-48) or expander (200-295)
    CustomPinMode mode;
    IsrEdge isrEdge;
    uint32_t intervalMs;       // Poll/timer/analog interval
    uint16_t pwmFreq;          // PWM frequency Hz
    uint8_t pwmResolution;     // PWM resolution bits
    uint8_t pwmChannel;        // LEDC channel (auto-assigned 8-15)
    char cron[24];             // 6-field cron: "sec min hr dom mon dow" (Timer mode)
    bool inMap;                // Show in main pin map tables (inputs/outputs)

    // Runtime (not persisted)
    float value;
    volatile uint32_t isrCount;
    uint32_t lastUpdateMs;
    bool initialized;

    void clear() {
        memset(this, 0, sizeof(*this));
        mode = CPIN_DISABLED;
        intervalMs = 1000;
        pwmFreq = 25000;
        pwmResolution = 8;
        pwmChannel = 0xFF;
        strncpy(cron, "0 * * * * *", sizeof(cron));
    }
};

struct OutputRule {
    char name[16];
    bool enabled;

    // Source A
    OutputRuleSource sourceType;
    uint8_t sourceSlot;

    // Source B (for DELTA operator)
    OutputRuleSource sourceTypeB;
    uint8_t sourceSlotB;

    // Condition
    OutputRuleOp op;
    float thresholdA;
    float thresholdB;          // For RANGE/OUTSIDE
    float hysteresis;

    // Target
    uint8_t targetPin;         // Index into CustomPinDescriptor[] (output or PWM)
    uint8_t onValue;           // Value when condition true (1 digital, 0-255 PWM)
    uint8_t offValue;          // Value when condition false

    // Timing
    uint32_t debounceMs;
    bool requireRunning;

    // Gates
    uint16_t gateRpmMin;
    uint16_t gateRpmMax;
    float gateMapMin;
    float gateMapMax;

    // Dynamic threshold curve (6-point)
    uint8_t curveSource;       // Engine state channel (0xFF = disabled)
    float curveX[6];
    float curveY[6];

    // Runtime (not persisted)
    uint32_t debounceStart;
    bool active;

    void clear() {
        memset(this, 0, sizeof(*this));
        enabled = false;
        sourceSlot = 0xFF;
        sourceSlotB = 0xFF;
        targetPin = 0xFF;
        onValue = 1;
        gateMapMin = NAN;
        gateMapMax = NAN;
        curveSource = 0xFF;
    }
};

class CustomPinManager {
public:
    CustomPinManager();

    void begin();
    void update();
    void stop();

    CustomPinDescriptor* getDescriptor(uint8_t slot);
    OutputRule* getRule(uint8_t slot);
    const CustomPinDescriptor* getDescriptors() const { return _pins; }
    const OutputRule* getRules() const { return _rules; }

    float readSource(OutputRuleSource type, uint8_t slot);
    void setOutput(uint8_t slot, float value);

    void setSensorManager(SensorManager* sm) { _sensors = sm; }
    void setEngineState(const EngineState* es) { _engineState = es; }

private:
    CustomPinDescriptor _pins[MAX_CUSTOM_PINS];
    OutputRule _rules[MAX_OUTPUT_RULES];
    SensorManager* _sensors;
    const EngineState* _engineState;
    esp_timer_handle_t _timers[MAX_CUSTOM_PINS];
    uint8_t _nextPwmChannel;

    void initPin(uint8_t slot);
    void deinitPin(uint8_t slot);
    void pollPin(uint8_t slot);
    void evaluateRule(uint8_t slot);
    float interpolateCurve(const float* x, const float* y, uint8_t n, float input);

    static void IRAM_ATTR isrHandler(void* arg);
    static void timerCallback(void* arg);
};
