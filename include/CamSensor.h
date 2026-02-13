#pragma once

#include <Arduino.h>

class CrankSensor;

class CamSensor {
public:
    enum Phase { PHASE_UNKNOWN = -1, PHASE_0 = 0, PHASE_1 = 1 };

    static const uint32_t SIGNAL_TIMEOUT_MS = 2000;

    CamSensor();
    ~CamSensor();

    void begin(uint8_t pin);
    void setCrankSensor(CrankSensor* crank) { _crankSensor = crank; }

    bool isPresent() const;
    bool hasCamSignal() const;
    Phase getPhase() const { return _phase; }
    uint16_t getLastPulseToothPosition() const { return _lastPulseToothPos; }

    void update();

private:
    uint8_t _pin;
    CrankSensor* _crankSensor;
    volatile Phase _phase;
    volatile uint16_t _lastPulseToothPos;
    volatile int64_t _lastPulseTimeUs;
    volatile bool _pulseReceived;

    static CamSensor* _instance;
    static void IRAM_ATTR isrHandler();
};
