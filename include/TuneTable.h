#pragma once

#include <Arduino.h>

class TuneTable2D {
public:
    TuneTable2D();
    ~TuneTable2D();

    void init(uint8_t size);
    void setAxis(const float* values);
    void setValues(const float* values);
    float lookup(float x) const;

    uint8_t getSize() const { return _size; }
    float getAxisValue(uint8_t idx) const;
    float getValue(uint8_t idx) const;
    void setAxisValue(uint8_t idx, float val);
    void setValue(uint8_t idx, float val);
    bool isInitialized() const { return _axis != nullptr; }

private:
    uint8_t _size;
    float* _axis;
    float* _values;
    uint8_t findBin(float x) const;
};

class TuneTable3D {
public:
    TuneTable3D();
    ~TuneTable3D();

    void init(uint8_t xSize, uint8_t ySize);
    void setXAxis(const float* values);
    void setYAxis(const float* values);
    void setValues(const float* values);  // Row-major: values[y * xSize + x]
    float lookup(float x, float y) const;

    uint8_t getXSize() const { return _xSize; }
    uint8_t getYSize() const { return _ySize; }
    float getXAxisValue(uint8_t idx) const;
    float getYAxisValue(uint8_t idx) const;
    float getValue(uint8_t x, uint8_t y) const;
    void setXAxisValue(uint8_t idx, float val);
    void setYAxisValue(uint8_t idx, float val);
    void setValue(uint8_t x, uint8_t y, float val);
    bool isInitialized() const { return _xAxis != nullptr; }

private:
    uint8_t _xSize;
    uint8_t _ySize;
    float* _xAxis;
    float* _yAxis;
    float* _values;
    uint8_t findBin(const float* axis, uint8_t size, float val) const;
};
