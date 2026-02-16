#pragma once

#include <Arduino.h>

class InjectionManager {
public:
    static const uint8_t MAX_CYLINDERS = 12;
    static constexpr float DEFAULT_DEAD_TIME_MS = 1.0f;
    static constexpr float MAX_PULSE_WIDTH_US = 25000.0f;

    InjectionManager();
    ~InjectionManager();

    void begin(uint8_t numCylinders, const uint16_t* injectorPins, const uint8_t* firingOrder);

    void setPulseWidthUs(float pw);
    void setDeadTimeMs(float dt);
    void setTrim(uint8_t cyl, float trimPercent);

    float getPulseWidthUs() const { return _basePulseWidthUs; }
    float getDeadTimeMs() const { return _deadTimeMs; }
    float getTrim(uint8_t cyl) const;
    float getEffectivePulseWidthUs(uint8_t cyl) const;

    void update(uint16_t rpm, uint16_t toothPos, bool sequential);
    void cutFuel();
    void resumeFuel();
    bool isFuelCut() const { return _fuelCut; }

private:
    uint8_t _numCylinders;
    uint16_t _injectorPins[MAX_CYLINDERS];
    uint8_t _firingOrder[MAX_CYLINDERS];
    float _basePulseWidthUs;
    float _deadTimeMs;
    float _trimPercent[MAX_CYLINDERS];
    bool _fuelCut;

    struct InjectorState {
        bool open;
        int64_t openTimeUs;
        float scheduledPulseUs;
    };
    InjectorState _injState[MAX_CYLINDERS];
};
