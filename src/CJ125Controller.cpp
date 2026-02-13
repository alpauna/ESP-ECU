#include "CJ125Controller.h"
#include "ADS1115Reader.h"
#include "PinExpander.h"
#include "Logger.h"

// Bosch LSU 4.9 characteristic curve: 10-bit ADC values and corresponding lambda.
// Derived from Bosch datasheet Ip-to-Lambda data for CJ125 NORMAL_V8 mode.
// UA(V) = 1.5 + Ip(mA) * 0.4952, ADC_10bit = UA * 1023 / 5.0
const CJ125Controller::IpLambdaPoint CJ125Controller::IP_LAMBDA_TABLE[] = {
    {  39, 0.650f},
    {  58, 0.700f},
    {  94, 0.720f},
    { 144, 0.750f},
    { 155, 0.800f},
    { 184, 0.850f},
    { 219, 0.900f},
    { 266, 0.950f},
    { 307, 1.000f},
    { 348, 1.050f},
    { 381, 1.100f},
    { 431, 1.200f},
    { 479, 1.350f},
    { 498, 1.400f},
    { 536, 1.600f},
    { 560, 1.800f},
    { 573, 2.000f},
    { 596, 2.500f},
    { 611, 3.000f},
    { 621, 4.000f},
    { 631, 5.000f},
    { 638, 7.000f},
    { 643, 10.119f},
};

const uint8_t CJ125Controller::IP_LAMBDA_TABLE_SIZE =
    sizeof(IP_LAMBDA_TABLE) / sizeof(IP_LAMBDA_TABLE[0]);

CJ125Controller::CJ125Controller(SPIClass* spi)
    : _spi(spi) {}

void CJ125Controller::begin(uint8_t spiSS1, uint8_t spiSS2,
                              uint8_t heaterOut1, uint8_t heaterOut2,
                              uint8_t uaPin1, uint8_t uaPin2) {
    _banks[0].spiSS = spiSS1;
    _banks[0].heaterPin = heaterOut1;
    _banks[0].uaPin = uaPin1;
    _banks[0].ledcChannel = 1;

    _banks[1].spiSS = spiSS2;
    _banks[1].heaterPin = heaterOut2;
    _banks[1].uaPin = uaPin2;
    _banks[1].ledcChannel = 2;

    for (uint8_t i = 0; i < 2; i++) {
        // Configure UA ADC pins
        pinMode(_banks[i].uaPin, INPUT);
        analogSetPinAttenuation(_banks[i].uaPin, ADC_11db);

        // Configure heater PWM: 100Hz, 8-bit resolution via LEDC
        ledcSetup(_banks[i].ledcChannel, 100, 8);
        ledcAttachPin(_banks[i].heaterPin, _banks[i].ledcChannel);
        ledcWrite(_banks[i].ledcChannel, 0);

        // Verify CJ125 identity
        uint16_t ident = spiTransfer(i, CJ125_IDENT_REG_REQUEST);
        Log.info("CJ125", "Bank %d ident: 0x%04X", i + 1, ident);

        _banks[i].heaterState = WAIT_POWER;
        _banks[i].stateStartMs = millis();
    }

    _enabled = true;
    Log.info("CJ125", "CJ125 controller initialized (2 banks)");
}

uint16_t CJ125Controller::spiTransfer(uint8_t bank, uint16_t data) {
    _spi->beginTransaction(SPISettings(125000, MSBFIRST, SPI_MODE1));
    xDigitalWrite(_banks[bank].spiSS, LOW);
    delayMicroseconds(1);
    uint8_t msb = _spi->transfer((data >> 8) & 0xFF);
    uint8_t lsb = _spi->transfer(data & 0xFF);
    xDigitalWrite(_banks[bank].spiSS, HIGH);
    _spi->endTransaction();
    return (msb << 8) | lsb;
}

uint16_t CJ125Controller::readUA(uint8_t bank) const {
    // Read 12-bit 3.3V ADC through 2:3 voltage divider, convert to 10-bit 5V equivalent
    uint16_t adc12bit = analogRead(_banks[bank].uaPin);
    int val = (int)(adc12bit * (5.0f / 3.3f) * (1023.0f / 4095.0f));
    return (uint16_t)constrain(val, 0, 1023);
}

void CJ125Controller::readSensors(uint8_t bank) {
    BankState& b = _banks[bank];

    // Read UA (lambda/pump current) from ESP32 ADC
    b.uaValue = readUA(bank);

    // Read UR (Nernst cell temp) from ADS1115
    if (_ads && _ads->isReady()) {
        float urMv = _ads->readMillivolts(bank); // CH0=bank0, CH1=bank1
        int ur10 = (int)(urMv * 1023.0f / 5000.0f);
        b.urValue = (uint16_t)constrain(ur10, 0, 1023);
    }

    // Read diagnostics via SPI
    b.diagStatus = spiTransfer(bank, CJ125_DIAG_REG_REQUEST);

    // Convert UA to lambda/AFR/O2% only when in PID state
    if (b.heaterState == PID) {
        b.lambda = adcToLambda(b.uaValue);
        b.afr = b.lambda * 14.7f;
        b.oxygenPct = (b.lambda > 1.0f) ? (1.0f - 1.0f / b.lambda) * 20.95f : 0.0f;
    }
}

void CJ125Controller::setHeaterPwm(uint8_t bank, float targetV, float batteryV) {
    BankState& b = _banks[bank];
    if (batteryV < 1.0f) batteryV = 1.0f;
    float duty = constrain(targetV / batteryV, 0.0f, 1.0f);
    b.heaterPwm = (int)(duty * 255.0f);
    b.heaterDuty = duty * 100.0f;
    ledcWrite(b.ledcChannel, (uint8_t)b.heaterPwm);
}

void CJ125Controller::update(float batteryVoltage) {
    if (!_enabled) return;

    // Decimation: run CJ125 logic at ~100ms (10x from 10ms update loop)
    if (++_decimationCount < 10) return;
    _decimationCount = 0;

    for (uint8_t i = 0; i < 2; i++) {
        updateBank(i, batteryVoltage);
    }
}

void CJ125Controller::updateBank(uint8_t bank, float batteryVoltage) {
    BankState& b = _banks[bank];
    unsigned long now = millis();
    unsigned long elapsed = now - b.stateStartMs;

    switch (b.heaterState) {
        case IDLE:
            break;

        case WAIT_POWER:
            if (batteryVoltage >= MIN_BATTERY_VOLTAGE) {
                b.heaterState = CALIBRATING;
                b.stateStartMs = now;
                Log.info("CJ125", "Bank %d: power OK (%.1fV), calibrating", bank + 1, batteryVoltage);
            }
            break;

        case CALIBRATING: {
            // Send calibrate command and wait
            spiTransfer(bank, CJ125_INIT_REG1_MODE_CALIBRATE);
            vTaskDelay(pdMS_TO_TICKS(50));

            // Read reference values
            b.uaRef = readUA(bank);
            if (_ads && _ads->isReady()) {
                float urMv = _ads->readMillivolts(bank);
                int ur10 = (int)(urMv * 1023.0f / 5000.0f);
                b.urRef = (uint16_t)constrain(ur10, 0, 1023);
            }

            // Switch to normal V8 mode
            spiTransfer(bank, CJ125_INIT_REG1_MODE_NORMAL_V8);

            Log.info("CJ125", "Bank %d calibrated: UA_ref=%d, UR_ref=%d",
                     bank + 1, b.uaRef, b.urRef);

            b.heaterState = CONDENSATION;
            b.stateStartMs = now;
            break;
        }

        case CONDENSATION:
            setHeaterPwm(bank, CONDENSATION_VOLTAGE, batteryVoltage);
            if (elapsed >= CONDENSATION_DURATION_MS) {
                b.rampVoltage = RAMP_START_VOLTAGE;
                b.heaterState = RAMP_UP;
                b.stateStartMs = now;
                Log.info("CJ125", "Bank %d: condensation done, ramping up", bank + 1);
            }
            break;

        case RAMP_UP:
            readSensors(bank);
            b.rampVoltage += RAMP_RATE_V_PER_SEC * 0.1f; // 100ms interval
            if (b.rampVoltage >= RAMP_END_VOLTAGE) {
                b.rampVoltage = RAMP_END_VOLTAGE;
                b.heaterState = PID;
                b.stateStartMs = now;
                b.pidIntegral = 0.0f;
                b.pidPrevError = 0;
                Log.info("CJ125", "Bank %d: ramp complete, entering PID control", bank + 1);
            }
            setHeaterPwm(bank, b.rampVoltage, batteryVoltage);
            break;

        case PID: {
            readSensors(bank);

            // Check diagnostics — error on persistent bad status
            if (b.diagStatus != CJ125_DIAG_REG_STATUS_OK &&
                b.diagStatus != 0 &&
                (b.diagStatus & 0xFF00) != 0x2800) {
                Log.error("CJ125", "Bank %d: DIAG error 0x%04X", bank + 1, b.diagStatus);
                b.heaterState = ERROR;
                b.heaterPwm = 0;
                b.heaterDuty = 0.0f;
                ledcWrite(b.ledcChannel, 0);
                break;
            }

            // PID heater control: maintain UR at calibrated reference
            int error = (int)b.urValue - (int)b.urRef;
            b.pidIntegral += error;
            if (b.pidIntegral > PID_I_MAX) b.pidIntegral = PID_I_MAX;
            if (b.pidIntegral < PID_I_MIN) b.pidIntegral = PID_I_MIN;
            int derivative = error - b.pidPrevError;
            b.pidPrevError = error;

            float pidOutput = PID_P * error + PID_I * b.pidIntegral + PID_D * derivative;
            b.heaterPwm += (int)pidOutput;
            b.heaterPwm = constrain(b.heaterPwm, 0, 255);
            b.heaterDuty = b.heaterPwm * 100.0f / 255.0f;
            ledcWrite(b.ledcChannel, (uint8_t)b.heaterPwm);
            break;
        }

        case ERROR:
            // Heater off until restart
            ledcWrite(b.ledcChannel, 0);
            b.heaterDuty = 0.0f;
            break;
    }
}

float CJ125Controller::adcToLambda(uint16_t adc10bit) const {
    // Piecewise linear interpolation from LSU 4.9 characteristic table
    if (adc10bit <= (uint16_t)IP_LAMBDA_TABLE[0].adc10bit)
        return IP_LAMBDA_TABLE[0].lambda;

    for (uint8_t i = 1; i < IP_LAMBDA_TABLE_SIZE; i++) {
        if ((float)adc10bit <= IP_LAMBDA_TABLE[i].adc10bit) {
            float adcLow = IP_LAMBDA_TABLE[i - 1].adc10bit;
            float adcHigh = IP_LAMBDA_TABLE[i].adc10bit;
            float lamLow = IP_LAMBDA_TABLE[i - 1].lambda;
            float lamHigh = IP_LAMBDA_TABLE[i].lambda;
            float frac = ((float)adc10bit - adcLow) / (adcHigh - adcLow);
            return lamLow + frac * (lamHigh - lamLow);
        }
    }

    return IP_LAMBDA_TABLE[IP_LAMBDA_TABLE_SIZE - 1].lambda;
}

// Getters
float CJ125Controller::getLambda(uint8_t bank) const {
    return (bank < 2) ? _banks[bank].lambda : 0.0f;
}

float CJ125Controller::getAfr(uint8_t bank) const {
    return (bank < 2) ? _banks[bank].afr : 14.7f;
}

float CJ125Controller::getOxygen(uint8_t bank) const {
    return (bank < 2) ? _banks[bank].oxygenPct : 0.0f;
}

CJ125Controller::HeaterState CJ125Controller::getHeaterState(uint8_t bank) const {
    return (bank < 2) ? _banks[bank].heaterState : IDLE;
}

const char* CJ125Controller::getHeaterStateStr(uint8_t bank) const {
    if (bank >= 2) return "UNKNOWN";
    switch (_banks[bank].heaterState) {
        case IDLE:         return "IDLE";
        case WAIT_POWER:   return "WAIT_POWER";
        case CALIBRATING:  return "CALIBRATING";
        case CONDENSATION: return "CONDENSATION";
        case RAMP_UP:      return "RAMP_UP";
        case PID:          return "PID";
        case ERROR:        return "ERROR";
        default:           return "UNKNOWN";
    }
}

float CJ125Controller::getHeaterDuty(uint8_t bank) const {
    return (bank < 2) ? _banks[bank].heaterDuty : 0.0f;
}

uint16_t CJ125Controller::getUrValue(uint8_t bank) const {
    return (bank < 2) ? _banks[bank].urValue : 0;
}

uint16_t CJ125Controller::getUaValue(uint8_t bank) const {
    return (bank < 2) ? _banks[bank].uaValue : 0;
}

uint16_t CJ125Controller::getDiagStatus(uint8_t bank) const {
    return (bank < 2) ? _banks[bank].diagStatus : 0;
}

bool CJ125Controller::isReady(uint8_t bank) const {
    if (bank >= 2) return false;
    return _banks[bank].heaterState == PID;
}
