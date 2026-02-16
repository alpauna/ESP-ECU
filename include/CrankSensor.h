#pragma once

#include <Arduino.h>

class CrankSensor {
public:
    enum SyncState { LOST, SYNCING, SYNCED };

    static const uint8_t TOOTH_HISTORY_SIZE = 8;
    static const uint16_t TOOTH_LOG_SIZE = 720;

    struct ToothLogEntry {
        uint32_t periodUs;
        uint8_t toothNum;
    };

    CrankSensor();
    ~CrankSensor();

    void begin(uint8_t pin, uint8_t teeth, uint8_t missing);

    uint16_t getRpm() const { return _rpm; }
    uint16_t getToothPosition() const { return _toothPosition; }
    SyncState getSyncState() const { return _syncState; }
    bool isSynced() const { return _syncState == SYNCED; }
    int64_t getLastToothTimeUs() const { return _lastToothTimeUs; }
    uint8_t getTotalTeeth() const { return _totalTeeth; }

    // Tooth logging
    void startToothLog();
    void stopToothLog();
    bool isToothLogComplete() const { return _toothLogComplete; }
    bool isToothLogCapturing() const { return _toothLogCapturing; }
    const volatile ToothLogEntry* getToothLog() const { return _toothLog; }
    uint16_t getToothLogSize() const { return _toothLogIdx; }

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

    // Tooth log buffer (DRAM for ISR access)
    volatile ToothLogEntry _toothLog[TOOTH_LOG_SIZE];
    volatile uint16_t _toothLogIdx;
    volatile bool _toothLogCapturing;
    volatile bool _toothLogComplete;

    static CrankSensor* _instance;
    static void IRAM_ATTR isrHandler();
    void IRAM_ATTR processTooth(int64_t nowUs);
    uint32_t averagePeriod() const;
};
