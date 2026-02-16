#pragma once

#include <Arduino.h>
#include <functional>

class ADS1115Reader;

static const uint8_t DIAG_CHANNEL_COUNT = 16;
static const uint8_t DIAG_BURST_SAMPLES = 8;

enum DiagFaultState : uint8_t {
    DIAG_OK            = 0,
    DIAG_WARNING       = 1,
    DIAG_FAULT         = 2,
    DIAG_INVESTIGATING = 3
};

struct DiagChannel {
    char name[16];
    char unit[6];
    float expectedMin;
    float expectedMax;
    float scaleFactor;      // millivolts -> engineering units (multiply)
    float scaleOffset;      // add after multiply
    uint8_t debounceCount;  // consecutive out-of-range reads before fault

    // Runtime state
    float lastValue;
    float lastMillivolts;
    DiagFaultState faultState;
    uint8_t faultCounter;
    uint8_t okCounter;
    uint16_t totalFaults;
    uint32_t lastReadMs;

    // Burst investigation
    float burstSamples[DIAG_BURST_SAMPLES];
    uint8_t burstIndex;
    bool burstComplete;

    void clear() {
        memset(this, 0, sizeof(*this));
        scaleFactor = 1.0f;
        scaleOffset = 0.0f;
        debounceCount = 3;
        faultState = DIAG_OK;
        expectedMin = NAN;
        expectedMax = NAN;
    }
};

enum DiagAdcPhase : uint8_t {
    DIAG_PHASE_IDLE,
    DIAG_PHASE_SELECT_MUX,
    DIAG_PHASE_START_CONV,
    DIAG_PHASE_WAIT_CONV,
    DIAG_PHASE_READ_RESULT
};

class BoardDiagnostics {
public:
    BoardDiagnostics();

    bool begin(uint8_t diagAdsAddr, const uint16_t muxSelPins[4], uint16_t muxEnPin);
    bool update(uint32_t budgetUs);

    const DiagChannel& getChannel(uint8_t ch) const { return _channels[ch]; }
    uint8_t getChannelCount() const { return DIAG_CHANNEL_COUNT; }
    uint8_t getFaultCount() const;
    uint16_t getFaultBitmask() const;
    bool hasAnyFault() const;
    uint8_t getHealthScore() const;
    uint32_t getScanCycleMs() const { return _lastScanCycleMs; }
    uint8_t getCurrentChannel() const { return _currentChannel; }
    DiagAdcPhase getCurrentPhase() const { return _phase; }
    bool isReady() const { return _ready; }
    bool isEnabled() const { return _enabled; }
    void setEnabled(bool en) { _enabled = en; }
    bool crossValidatePowerRails() const;

    typedef std::function<void(const char* fault, const char* message, bool active)> FaultCallback;
    void setFaultCallback(FaultCallback cb) { _faultCb = cb; }

private:
    ADS1115Reader* _diagAds;
    DiagChannel _channels[DIAG_CHANNEL_COUNT];

    uint16_t _muxSelPins[4];
    uint16_t _muxEnPin;

    bool _ready;
    bool _enabled;

    DiagAdcPhase _phase;
    uint8_t _currentChannel;
    uint8_t _burstChannel;
    uint8_t _burstRemaining;
    uint32_t _muxSettleUs;
    uint32_t _convStartMs;

    uint32_t _scanStartMs;
    uint32_t _lastScanCycleMs;

    void selectMuxChannel(uint8_t ch);
    void enableMux(bool en);
    void advanceChannel();

    void processReading(uint8_t ch, float millivolts);
    void evaluateFault(uint8_t ch);
    void startBurstInvestigation(uint8_t ch);
    void processBurstResult(uint8_t ch);

    void initDefaultChannels();

    FaultCallback _faultCb;

    static constexpr uint32_t MUX_SETTLE_US  = 50;
    static constexpr uint32_t CONV_TIMEOUT_MS = 15;
    static constexpr uint8_t  RECOVERY_COUNT  = 5;
};
