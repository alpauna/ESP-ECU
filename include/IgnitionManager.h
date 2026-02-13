#pragma once

#include <Arduino.h>

class IgnitionManager {
public:
    static const uint8_t MAX_CYLINDERS = 12;
    static constexpr float DEFAULT_DWELL_MS = 3.0f;
    static const uint16_t DEFAULT_REV_LIMIT = 6000;

    IgnitionManager();
    ~IgnitionManager();

    void begin(uint8_t numCylinders, const uint8_t* coilPins, const uint8_t* firingOrder);

    void setAdvance(float deg);
    void setDwellMs(float ms);
    void setRevLimit(uint16_t rpm);
    void setMaxDwellMs(float ms) { _maxDwellMs = ms; }

    float getAdvance() const { return _advanceDeg; }
    float getDwellMs() const { return _dwellMs; }
    uint16_t getRevLimit() const { return _revLimit; }
    bool isRevLimiting() const { return _revLimiting; }

    void update(uint16_t rpm, uint16_t toothPos, bool sequential);

private:
    uint8_t _numCylinders;
    uint8_t _coilPins[MAX_CYLINDERS];
    uint8_t _firingOrder[MAX_CYLINDERS];
    float _advanceDeg;
    float _dwellMs;
    float _maxDwellMs;
    uint16_t _revLimit;
    bool _revLimiting;

    struct CoilState {
        bool charging;
        int64_t dwellStartUs;
    };
    CoilState _coilState[MAX_CYLINDERS];

    void cutSpark();
};
