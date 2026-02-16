#pragma once

#include <Arduino.h>

struct EngineState;
class TuneTable3D;
class TuneTable2D;

class FuelManager {
public:
    static constexpr float STOICH_AFR = 14.7f;
    static constexpr float DEFAULT_CRANKING_PW_US = 5000.0f;
    static constexpr float O2_P_GAIN = 0.5f;
    static constexpr float O2_I_GAIN = 0.05f;
    static constexpr float O2_CORRECTION_LIMIT = 0.25f;

    FuelManager();
    ~FuelManager();

    void begin();
    void update(EngineState& state);

    float getBasePulseWidthUs() const { return _basePulseWidthUs; }
    float getTargetAfr() const { return _targetAfr; }
    float getVE() const { return _ve; }

    // ASE
    void setAseParams(float initialPct, uint32_t durationMs, float minCltF);
    bool isAseActive() const { return _aseActive; }
    float getAsePct() const { return _aseCurrentPct; }

    // DFCO
    void setDfcoParams(uint16_t rpmThresh, float tpsThresh, uint32_t delayMs,
                       uint16_t exitRpm, float exitTps);
    bool isDfcoActive() const { return _dfcoActive; }

    void setReqFuel(float ccPerMin, float displacementCc, uint8_t numCyl);
    float getReqFuelMs() const { return _reqFuelMs; }

    void setClosedLoopWindow(uint16_t minRpm, uint16_t maxRpm, float maxMapKpa);
    void setO2PGain(float p) { _o2PGain = p; }
    void setO2IGain(float i) { _o2IGain = i; }
    float getO2Correction(uint8_t bank) const;

    void setVeTable(TuneTable3D* table) { _veTable = table; }
    void setAfrTable(TuneTable3D* table) { _afrTable = table; }
    void setSparkTable(TuneTable3D* table) { _sparkTable = table; }

    TuneTable3D* getVeTable() const { return _veTable; }
    TuneTable3D* getAfrTable() const { return _afrTable; }
    TuneTable3D* getSparkTable() const { return _sparkTable; }

private:
    TuneTable3D* _veTable;
    TuneTable3D* _afrTable;
    TuneTable3D* _sparkTable;

    float _basePulseWidthUs;
    float _targetAfr;
    float _ve;
    float _reqFuelMs;

    struct O2ClosedLoop {
        float integral;
        float correction;
        float lastAfr;
    };
    O2ClosedLoop _o2Bank[2];

    float _o2PGain;
    float _o2IGain;
    uint16_t _closedLoopMinRpm;
    uint16_t _closedLoopMaxRpm;
    float _closedLoopMaxMapKpa;

    float _prevTps;
    float _accelEnrichRemaining;
    uint32_t _lastUpdateMs;

    // ASE state
    bool _aseActive = false;
    uint32_t _aseStartMs = 0;
    float _aseCurrentPct = 0.0f;
    float _aseInitialPct = 35.0f;
    uint32_t _aseDurationMs = 10000;
    float _aseMinCltF = 100.0f;
    bool _wasCranking = false;

    // DFCO state
    bool _dfcoActive = false;
    uint32_t _dfcoEntryStart = 0;
    uint16_t _dfcoRpmThreshold = 2500;
    float _dfcoTpsThreshold = 3.0f;
    uint32_t _dfcoEntryDelayMs = 500;
    uint16_t _dfcoExitRpm = 1800;
    float _dfcoExitTps = 5.0f;

    float calculateWarmupEnrichment(float coolantTempF);
    float calculateAccelEnrichment(float tps);
    void updateO2ClosedLoop(uint8_t bank, float actualAfr, float targetAfr);
    bool isInClosedLoopWindow(uint16_t rpm, float mapKpa) const;
};
