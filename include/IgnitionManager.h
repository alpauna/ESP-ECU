#pragma once

#include <Arduino.h>

class IgnitionManager {
public:
    static const uint8_t MAX_CYLINDERS = 12;
    static constexpr float DEFAULT_DWELL_MS = 3.0f;
    static const uint16_t DEFAULT_REV_LIMIT = 6000;

    IgnitionManager();
    ~IgnitionManager();

    void begin(uint8_t numCylinders, const uint16_t* coilPins, const uint8_t* firingOrder);

    void setAdvance(float deg);
    void setDwellMs(float ms);
    void setRevLimit(uint16_t rpm);
    void setConfigRevLimit(uint16_t rpm) { _configRevLimit = rpm; }
    void setMaxDwellMs(float ms) { _maxDwellMs = ms; }

    float getAdvance() const { return _advanceDeg; }
    float getDwellMs() const { return _dwellMs; }
    uint16_t getRevLimit() const { return _revLimit; }
    uint16_t getConfigRevLimit() const { return _configRevLimit; }
    bool isRevLimiting() const { return _revLimiting; }
    uint32_t getOverdwellCount() const { return _overdwellCount; }
    void resetOverdwellCount() { _overdwellCount = 0; }

    void update(uint16_t rpm, uint16_t toothPos, bool sequential);

private:
    uint8_t _numCylinders;
    uint16_t _coilPins[MAX_CYLINDERS];
    uint8_t _firingOrder[MAX_CYLINDERS];
    float _advanceDeg;
    float _dwellMs;
    float _maxDwellMs;
    uint16_t _revLimit;
    uint16_t _configRevLimit;
    bool _revLimiting;
    uint32_t _overdwellCount;

    struct CoilState {
        bool charging;
        int64_t dwellStartUs;
    };
    CoilState _coilState[MAX_CYLINDERS];

    void cutSpark();
};
