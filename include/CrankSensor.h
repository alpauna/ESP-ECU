#pragma once

#include <Arduino.h>

class CrankSensor {
public:
    enum SyncState { LOST, SYNCING, SYNCED };

    static const uint8_t TOOTH_HISTORY_SIZE = 8;

    CrankSensor();
    ~CrankSensor();

    void begin(uint8_t pin, uint8_t teeth, uint8_t missing);

    uint16_t getRpm() const { return _rpm; }
    uint16_t getToothPosition() const { return _toothPosition; }
    SyncState getSyncState() const { return _syncState; }
    bool isSynced() const { return _syncState == SYNCED; }
    int64_t getLastToothTimeUs() const { return _lastToothTimeUs; }
    uint8_t getTotalTeeth() const { return _totalTeeth; }

private:
    uint8_t _pin;
    uint8_t _totalTeeth;
    uint8_t _missingTeeth;
    volatile uint16_t _rpm;
    volatile uint16_t _toothPosition;
    volatile SyncState _syncState;
    volatile int64_t _lastToothTimeUs;

    volatile uint32_t _toothPeriods[TOOTH_HISTORY_SIZE];
    volatile uint8_t _toothHistIdx;
    volatile uint32_t _lastPeriodUs;
    volatile uint8_t _toothCount;
    volatile bool _gapDetected;

    static CrankSensor* _instance;
    static void IRAM_ATTR isrHandler();
    void IRAM_ATTR processTooth(int64_t nowUs);
    uint32_t averagePeriod() const;
};
