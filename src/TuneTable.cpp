#include "TuneTable.h"

// --- TuneTable2D ---

TuneTable2D::TuneTable2D() : _size(0), _axis(nullptr), _values(nullptr) {}

TuneTable2D::~TuneTable2D() {
    free(_axis);
    free(_values);
}

void TuneTable2D::init(uint8_t size) {
    free(_axis);
    free(_values);
    _size = size;
    _axis = (float*)ps_malloc(size * sizeof(float));
    _values = (float*)ps_malloc(size * sizeof(float));
    if (_axis) memset(_axis, 0, size * sizeof(float));
    if (_values) memset(_values, 0, size * sizeof(float));
}

void TuneTable2D::setAxis(const float* values) {
    if (_axis && values) memcpy(_axis, values, _size * sizeof(float));
}

void TuneTable2D::setValues(const float* values) {
    if (_values && values) memcpy(_values, values, _size * sizeof(float));
}

float TuneTable2D::getAxisValue(uint8_t idx) const {
    return (idx < _size && _axis) ? _axis[idx] : 0.0f;
}

float TuneTable2D::getValue(uint8_t idx) const {
    return (idx < _size && _values) ? _values[idx] : 0.0f;
}

void TuneTable2D::setAxisValue(uint8_t idx, float val) {
    if (idx < _size && _axis) _axis[idx] = val;
}

void TuneTable2D::setValue(uint8_t idx, float val) {
    if (idx < _size && _values) _values[idx] = val;
}

uint8_t TuneTable2D::findBin(float x) const {
    if (_size < 2 || !_axis) return 0;
    if (x <= _axis[0]) return 0;
    for (uint8_t i = 1; i < _size; i++) {
        if (x <= _axis[i]) return i - 1;
    }
    return _size - 2;
}

float TuneTable2D::lookup(float x) const {
    if (!_axis || !_values || _size == 0) return 0.0f;
    if (_size == 1) return _values[0];

    uint8_t bin = findBin(x);
    float x0 = _axis[bin];
    float x1 = _axis[bin + 1];
    float range = x1 - x0;
    if (range < 0.001f) return _values[bin];

    float frac = (x - x0) / range;
    frac = constrain(frac, 0.0f, 1.0f);
    return _values[bin] + frac * (_values[bin + 1] - _values[bin]);
}

// --- TuneTable3D ---

TuneTable3D::TuneTable3D() : _xSize(0), _ySize(0), _xAxis(nullptr), _yAxis(nullptr), _values(nullptr) {}

TuneTable3D::~TuneTable3D() {
    free(_xAxis);
    free(_yAxis);
    free(_values);
}

void TuneTable3D::init(uint8_t xSize, uint8_t ySize) {
    free(_xAxis);
    free(_yAxis);
    free(_values);
    _xSize = xSize;
    _ySize = ySize;
    _xAxis = (float*)ps_malloc(xSize * sizeof(float));
    _yAxis = (float*)ps_malloc(ySize * sizeof(float));
    _values = (float*)ps_malloc(xSize * ySize * sizeof(float));
    if (_xAxis) memset(_xAxis, 0, xSize * sizeof(float));
    if (_yAxis) memset(_yAxis, 0, ySize * sizeof(float));
    if (_values) memset(_values, 0, xSize * ySize * sizeof(float));
}

void TuneTable3D::setXAxis(const float* values) {
    if (_xAxis && values) memcpy(_xAxis, values, _xSize * sizeof(float));
}

void TuneTable3D::setYAxis(const float* values) {
    if (_yAxis && values) memcpy(_yAxis, values, _ySize * sizeof(float));
}

void TuneTable3D::setValues(const float* values) {
    if (_values && values) memcpy(_values, values, _xSize * _ySize * sizeof(float));
}

float TuneTable3D::getXAxisValue(uint8_t idx) const {
    return (idx < _xSize && _xAxis) ? _xAxis[idx] : 0.0f;
}

float TuneTable3D::getYAxisValue(uint8_t idx) const {
    return (idx < _ySize && _yAxis) ? _yAxis[idx] : 0.0f;
}

float TuneTable3D::getValue(uint8_t x, uint8_t y) const {
    if (x < _xSize && y < _ySize && _values) return _values[y * _xSize + x];
    return 0.0f;
}

void TuneTable3D::setXAxisValue(uint8_t idx, float val) {
    if (idx < _xSize && _xAxis) _xAxis[idx] = val;
}

void TuneTable3D::setYAxisValue(uint8_t idx, float val) {
    if (idx < _ySize && _yAxis) _yAxis[idx] = val;
}

void TuneTable3D::setValue(uint8_t x, uint8_t y, float val) {
    if (x < _xSize && y < _ySize && _values) _values[y * _xSize + x] = val;
}

uint8_t TuneTable3D::findBin(const float* axis, uint8_t size, float val) const {
    if (size < 2 || !axis) return 0;
    if (val <= axis[0]) return 0;
    for (uint8_t i = 1; i < size; i++) {
        if (val <= axis[i]) return i - 1;
    }
    return size - 2;
}

float TuneTable3D::lookup(float x, float y) const {
    if (!_xAxis || !_yAxis || !_values || _xSize == 0 || _ySize == 0) return 0.0f;
    if (_xSize == 1 && _ySize == 1) return _values[0];

    uint8_t xBin = findBin(_xAxis, _xSize, x);
    uint8_t yBin = findBin(_yAxis, _ySize, y);

    float x0 = _xAxis[xBin];
    float x1 = _xAxis[min((uint8_t)(xBin + 1), (uint8_t)(_xSize - 1))];
    float y0 = _yAxis[yBin];
    float y1 = _yAxis[min((uint8_t)(yBin + 1), (uint8_t)(_ySize - 1))];

    float xFrac = (x1 - x0 > 0.001f) ? constrain((x - x0) / (x1 - x0), 0.0f, 1.0f) : 0.0f;
    float yFrac = (y1 - y0 > 0.001f) ? constrain((y - y0) / (y1 - y0), 0.0f, 1.0f) : 0.0f;

    // Bilinear interpolation
    uint8_t xBin1 = min((uint8_t)(xBin + 1), (uint8_t)(_xSize - 1));
    uint8_t yBin1 = min((uint8_t)(yBin + 1), (uint8_t)(_ySize - 1));

    float v00 = _values[yBin  * _xSize + xBin];
    float v10 = _values[yBin  * _xSize + xBin1];
    float v01 = _values[yBin1 * _xSize + xBin];
    float v11 = _values[yBin1 * _xSize + xBin1];

    float top    = v00 + xFrac * (v10 - v00);
    float bottom = v01 + xFrac * (v11 - v01);
    return top + yFrac * (bottom - top);
}
