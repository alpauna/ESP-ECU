#include "CustomPin.h"
#include "SensorManager.h"
#include "ECU.h"
#include "PinExpander.h"
#include "Logger.h"
#include <time.h>

// --- Cron field matching ---
// Supports: number, *, */N, comma-separated, ranges (e.g. "1-5")
static bool matchCronField(const char* field, int value) {
    if (!field || field[0] == '*') {
        if (field && field[1] == '/') {
            int step = atoi(field + 2);
            return step > 0 && (value % step) == 0;
        }
        return true;
    }
    // Comma-separated list (may contain ranges)
    char buf[16];
    strncpy(buf, field, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* tok = strtok(buf, ",");
    while (tok) {
        char* dash = strchr(tok, '-');
        if (dash) {
            *dash = '\0';
            int lo = atoi(tok), hi = atoi(dash + 1);
            if (value >= lo && value <= hi) return true;
        } else {
            if (atoi(tok) == value) return true;
        }
        tok = strtok(nullptr, ",");
    }
    return false;
}

static bool cronMatches(const char* cron, const struct tm* t) {
    if (!cron || !cron[0] || !t) return false;
    char buf[32];
    strncpy(buf, cron, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    // Parse 6 fields: sec min hr dom mon dow
    char* fields[6] = {};
    int n = 0;
    char* p = buf;
    while (*p && n < 6) {
        while (*p == ' ') p++;
        if (!*p) break;
        fields[n++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    if (n < 6) return false;
    return matchCronField(fields[0], t->tm_sec)  &&
           matchCronField(fields[1], t->tm_min)  &&
           matchCronField(fields[2], t->tm_hour) &&
           matchCronField(fields[3], t->tm_mday) &&
           matchCronField(fields[4], t->tm_mon + 1) &&  // tm_mon is 0-11
           matchCronField(fields[5], t->tm_wday);       // tm_wday is 0=Sun
}

CustomPinManager::CustomPinManager()
    : _sensors(nullptr), _engineState(nullptr), _nextPwmChannel(8) {
    for (uint8_t i = 0; i < MAX_CUSTOM_PINS; i++) {
        _pins[i].clear();
        _timers[i] = nullptr;
    }
    for (uint8_t i = 0; i < MAX_OUTPUT_RULES; i++) {
        _rules[i].clear();
    }
}

CustomPinDescriptor* CustomPinManager::getDescriptor(uint8_t slot) {
    return (slot < MAX_CUSTOM_PINS) ? &_pins[slot] : nullptr;
}

OutputRule* CustomPinManager::getRule(uint8_t slot) {
    return (slot < MAX_OUTPUT_RULES) ? &_rules[slot] : nullptr;
}

void CustomPinManager::begin() {
    for (uint8_t i = 0; i < MAX_CUSTOM_PINS; i++) {
        if (_pins[i].mode != CPIN_DISABLED) {
            initPin(i);
        }
    }
    Log.info("CPIN", "CustomPinManager started");
}

void CustomPinManager::stop() {
    for (uint8_t i = 0; i < MAX_CUSTOM_PINS; i++) {
        deinitPin(i);
    }
}

void CustomPinManager::initPin(uint8_t slot) {
    CustomPinDescriptor& p = _pins[slot];
    if (p.mode == CPIN_DISABLED) return;

    bool isExpander = (p.pin >= SPI_EXP_PIN_OFFSET);

    switch (p.mode) {
        case CPIN_INPUT_POLL:
        case CPIN_ANALOG_IN:
            if (isExpander)
                xPinMode(p.pin, INPUT);
            else
                pinMode(p.pin, INPUT);
            break;

        case CPIN_INPUT_ISR: {
            if (isExpander) {
                PinExpander& exp = PinExpander::instance();
                if (exp.getIntGpio() != 0xFF) {
                    // Use hardware interrupt-on-change via shared open-drain INT line
                    xPinMode(p.pin, INPUT);
                    exp.enablePinInterrupt(p.pin);
                    p.isrCount = 0;
                    Log.info("CPIN", "Pin %d '%s' ISR via shared expander INT", p.pin, p.name);
                } else {
                    // No INT GPIO wired — fall back to polling
                    Log.warn("CPIN", "ISR not supported on expander pin %d (no INT wired)", p.pin);
                    p.mode = CPIN_INPUT_POLL;
                    xPinMode(p.pin, INPUT);
                }
                break;
            }
            pinMode(p.pin, INPUT);
            int edge = RISING;
            if (p.isrEdge == EDGE_FALLING) edge = FALLING;
            else if (p.isrEdge == EDGE_CHANGE) edge = CHANGE;
            p.isrCount = 0;
            attachInterruptArg(digitalPinToInterrupt(p.pin), isrHandler, &_pins[slot], edge);
            break;
        }

        case CPIN_INPUT_TIMER: {
            // Cron-based timer: pin is read when cron expression matches current time
            // Supports both GPIO and expander pins
            if (isExpander)
                xPinMode(p.pin, INPUT);
            else
                pinMode(p.pin, INPUT);
            Log.info("CPIN", "Pin %d '%s' cron: %s", p.pin, p.name, p.cron);
            break;
        }

        case CPIN_OUTPUT:
            if (isExpander) {
                xPinMode(p.pin, OUTPUT);
                xDigitalWrite(p.pin, LOW);
            } else {
                pinMode(p.pin, OUTPUT);
                digitalWrite(p.pin, LOW);
            }
            p.value = 0;
            break;

        case CPIN_PWM_OUT:
            if (isExpander) {
                Log.warn("CPIN", "PWM not supported on expander pin %d, using digital", p.pin);
                p.mode = CPIN_OUTPUT;
                xPinMode(p.pin, OUTPUT);
                xDigitalWrite(p.pin, LOW);
            } else {
                if (p.pwmChannel == 0xFF && _nextPwmChannel <= 15) {
                    p.pwmChannel = _nextPwmChannel++;
                }
                if (p.pwmChannel <= 15) {
                    ledcSetup(p.pwmChannel, p.pwmFreq, p.pwmResolution);
                    ledcAttachPin(p.pin, p.pwmChannel);
                    ledcWrite(p.pwmChannel, 0);
                }
            }
            p.value = 0;
            break;

        default:
            break;
    }

    p.initialized = true;
    p.lastUpdateMs = millis();
    Log.info("CPIN", "Pin %d '%s' initialized (mode %d)", p.pin, p.name, p.mode);
}

void CustomPinManager::deinitPin(uint8_t slot) {
    CustomPinDescriptor& p = _pins[slot];
    if (!p.initialized) return;

    if (p.mode == CPIN_INPUT_ISR) {
        if (p.pin < SPI_EXP_PIN_OFFSET) {
            detachInterrupt(digitalPinToInterrupt(p.pin));
        } else {
            PinExpander::instance().disablePinInterrupt(p.pin);
        }
    }
    if (_timers[slot]) {
        esp_timer_stop(_timers[slot]);
        esp_timer_delete(_timers[slot]);
        _timers[slot] = nullptr;
    }
    if (p.mode == CPIN_PWM_OUT && p.pwmChannel <= 15) {
        ledcDetachPin(p.pin);
    }
    p.initialized = false;
}

void CustomPinManager::update() {
    uint32_t now = millis();

    // Get current time for cron matching (only once per update cycle)
    struct tm timeinfo = {};
    bool haveTime = (getLocalTime(&timeinfo, 0) && timeinfo.tm_year > 100);

    // Check shared expander interrupt — scan all devices in priority order (0 first, 5 last)
    {
        PinExpander& exp = PinExpander::instance();
        if (exp.hasSharedInterrupt()) {
            for (uint8_t ei = 0; ei < SPI_EXP_MAX; ei++) {
                if (!exp.isReady(ei) || !exp.hasGpinten(ei)) continue;
                uint16_t changed, captured;
                if (exp.checkInterrupt(ei, changed, captured)) {
                    for (uint8_t i = 0; i < MAX_CUSTOM_PINS; i++) {
                        CustomPinDescriptor& cp = _pins[i];
                        if (!cp.initialized || cp.mode != CPIN_INPUT_ISR) continue;
                        if (cp.pin < SPI_EXP_PIN_OFFSET) continue;
                        uint8_t spiIdx = (cp.pin - SPI_EXP_PIN_OFFSET) / SPI_EXP_PIN_COUNT;
                        if (spiIdx != ei) continue;
                        uint8_t localPin = (cp.pin - SPI_EXP_PIN_OFFSET) % SPI_EXP_PIN_COUNT;
                        if (changed & (1 << localPin)) {
                            cp.isrCount++;
                            cp.value = (captured & (1 << localPin)) ? 1.0f : 0.0f;
                        }
                    }
                }
            }
            exp.clearSharedInterrupt();
        }
    }

    // Poll inputs
    for (uint8_t i = 0; i < MAX_CUSTOM_PINS; i++) {
        CustomPinDescriptor& p = _pins[i];
        if (!p.initialized) continue;

        if (p.mode == CPIN_INPUT_POLL || p.mode == CPIN_ANALOG_IN) {
            if (now - p.lastUpdateMs >= p.intervalMs) {
                pollPin(i);
                p.lastUpdateMs = now;
            }
        } else if (p.mode == CPIN_INPUT_ISR) {
            // Value updated by native ISR (isrCount) or expander interrupt check above
            if (p.pin < SPI_EXP_PIN_OFFSET) p.value = (float)p.isrCount;
            // Expander ISR pins: value already set by checkInterrupt above
        } else if (p.mode == CPIN_INPUT_TIMER && haveTime) {
            // Cron matching: check once per second (avoid duplicate fires within same second)
            uint32_t curSec = timeinfo.tm_sec + timeinfo.tm_min * 60 + timeinfo.tm_hour * 3600;
            if (curSec != p.lastUpdateMs && cronMatches(p.cron, &timeinfo)) {
                pollPin(i);
                p.lastUpdateMs = curSec;  // Track last fired second-of-day
            }
        }
    }

    // Evaluate output rules
    for (uint8_t i = 0; i < MAX_OUTPUT_RULES; i++) {
        if (_rules[i].enabled && _rules[i].targetPin < MAX_CUSTOM_PINS) {
            evaluateRule(i);
        }
    }
}

void CustomPinManager::pollPin(uint8_t slot) {
    CustomPinDescriptor& p = _pins[slot];
    bool isExpander = (p.pin >= SPI_EXP_PIN_OFFSET);

    if (p.mode == CPIN_ANALOG_IN) {
        if (!isExpander) {
            p.value = (float)analogRead(p.pin);
        }
    } else {
        // Digital poll
        int val = isExpander ? xDigitalRead(p.pin) : digitalRead(p.pin);
        p.value = (float)val;
    }
}

void IRAM_ATTR CustomPinManager::isrHandler(void* arg) {
    CustomPinDescriptor* p = (CustomPinDescriptor*)arg;
    p->isrCount++;
}

void CustomPinManager::timerCallback(void* arg) {
    CustomPinDescriptor* p = (CustomPinDescriptor*)arg;
    if (p->pin < SPI_EXP_PIN_OFFSET) {
        p->value = (float)digitalRead(p->pin);
    }
}

float CustomPinManager::readSource(OutputRuleSource type, uint8_t slot) {
    switch (type) {
        case OSRC_SENSOR:
            if (_sensors && slot < MAX_SENSORS)
                return _sensors->getDescriptor(slot)->value;
            break;
        case OSRC_CUSTOM_PIN:
            if (slot < MAX_CUSTOM_PINS)
                return _pins[slot].value;
            break;
        case OSRC_ENGINE_STATE:
            // Slots 100+ map to custom pin values (inMap pins exposed as engine channels)
            if (slot >= 100) {
                uint8_t cpSlot = slot - 100;
                if (cpSlot < MAX_CUSTOM_PINS) return _pins[cpSlot].value;
                break;
            }
            if (_engineState) {
                switch (slot) {
                    case 0: return (float)_engineState->rpm;
                    case 1: return _engineState->mapKpa;
                    case 2: return _engineState->tps;
                    case 3: return _engineState->coolantTempF;
                    case 4: return _engineState->iatTempF;
                    case 5: return _engineState->batteryVoltage;
                    case 6: return _engineState->afr[0];
                    case 7: return _engineState->afr[1];
                    case 8: return _engineState->sparkAdvanceDeg;
                    case 9: return (float)(_engineState->injPulseWidthUs / 1000.0f);
                    case 10: return _engineState->engineRunning ? 1.0f : 0.0f;
                    case 11: return _engineState->oilPressurePsi;
                }
            }
            break;
        case OSRC_OUTPUT_STATE:
            // Could extend to read alternator duty, fuel pump state, etc.
            break;
    }
    return NAN;
}

void CustomPinManager::setOutput(uint8_t slot, float value) {
    if (slot >= MAX_CUSTOM_PINS) return;
    CustomPinDescriptor& p = _pins[slot];
    if (!p.initialized) return;

    if (p.mode == CPIN_OUTPUT) {
        bool on = (value > 0.5f);
        if (p.pin >= SPI_EXP_PIN_OFFSET) xDigitalWrite(p.pin, on ? HIGH : LOW);
        else digitalWrite(p.pin, on ? HIGH : LOW);
        p.value = on ? 1.0f : 0.0f;
    } else if (p.mode == CPIN_PWM_OUT && p.pwmChannel <= 15) {
        uint32_t duty = constrain((uint32_t)value, 0, (1 << p.pwmResolution) - 1);
        ledcWrite(p.pwmChannel, duty);
        p.value = (float)duty;
    }
}

float CustomPinManager::interpolateCurve(const float* x, const float* y, uint8_t n, float input) {
    if (n == 0) return 0;
    if (input <= x[0]) return y[0];
    if (input >= x[n - 1]) return y[n - 1];
    for (uint8_t i = 0; i < n - 1; i++) {
        if (input >= x[i] && input <= x[i + 1]) {
            float t = (input - x[i]) / (x[i + 1] - x[i]);
            return y[i] + t * (y[i + 1] - y[i]);
        }
    }
    return y[n - 1];
}

void CustomPinManager::evaluateRule(uint8_t slot) {
    OutputRule& r = _rules[slot];
    if (!r.enabled || r.targetPin >= MAX_CUSTOM_PINS) return;

    // Gate checks
    if (r.requireRunning && _engineState && !_engineState->engineRunning) {
        if (r.active) {
            setOutput(r.targetPin, r.offValue);
            r.active = false;
        }
        return;
    }
    if (_engineState) {
        uint16_t rpm = _engineState->rpm;
        if (r.gateRpmMin > 0 && rpm < r.gateRpmMin) return;
        if (r.gateRpmMax > 0 && rpm > r.gateRpmMax) return;
        if (!isnan(r.gateMapMin) && _engineState->mapKpa < r.gateMapMin) return;
        if (!isnan(r.gateMapMax) && _engineState->mapKpa > r.gateMapMax) return;
    }

    float srcA = readSource(r.sourceType, r.sourceSlot);
    if (isnan(srcA)) return;

    // Dynamic threshold from curve
    float threshA = r.thresholdA;
    float threshB = r.thresholdB;
    if (r.curveSource != 0xFF) {
        float curveInput;
        if (r.curveSource >= 100)
            curveInput = readSource(OSRC_CUSTOM_PIN, r.curveSource - 100);
        else if (_engineState)
            curveInput = readSource(OSRC_ENGINE_STATE, r.curveSource);
        else
            curveInput = NAN;
        if (!isnan(curveInput)) {
            threshA = interpolateCurve(r.curveX, r.curveY, 6, curveInput);
        }
    }

    // Evaluate condition
    bool condMet = false;
    float hyst = r.hysteresis;
    switch (r.op) {
        case ORULE_LT:
            condMet = r.active ? (srcA < threshA + hyst) : (srcA < threshA);
            break;
        case ORULE_GT:
            condMet = r.active ? (srcA > threshA - hyst) : (srcA > threshA);
            break;
        case ORULE_RANGE:
            condMet = (srcA >= threshA && srcA <= threshB);
            break;
        case ORULE_OUTSIDE:
            condMet = (srcA < threshA || srcA > threshB);
            break;
        case ORULE_DELTA: {
            float srcB = readSource(r.sourceTypeB, r.sourceSlotB);
            if (!isnan(srcB)) {
                float delta = fabsf(srcA - srcB);
                condMet = r.active ? (delta > threshA - hyst) : (delta > threshA);
            }
            break;
        }
    }

    // Debounce
    uint32_t now = millis();
    if (condMet != r.active) {
        if (r.debounceMs > 0) {
            if (r.debounceStart == 0) {
                r.debounceStart = now;
            } else if (now - r.debounceStart >= r.debounceMs) {
                r.active = condMet;
                r.debounceStart = 0;
                setOutput(r.targetPin, condMet ? r.onValue : r.offValue);
            }
        } else {
            r.active = condMet;
            setOutput(r.targetPin, condMet ? r.onValue : r.offValue);
        }
    } else {
        r.debounceStart = 0;
    }
}
