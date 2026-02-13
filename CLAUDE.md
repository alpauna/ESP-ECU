# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3 Engine Control Unit for gas engines. Controls coil-over-plug ignition (up to 12 cylinders), sequential/batch fuel injection, dual-bank CJ125 wideband O2 closed-loop (LSU 4.9 sensors), and alternator field PWM. I/O expanded via MCP23017 I2C GPIO expander. Provides a REST API, WebSocket, and MQTT interface for remote monitoring and tuning.

## Build Commands

```bash
# Build
pio run -e freenove_esp32_s3_wroom

# Upload firmware (USB)
pio run -t upload -e freenove_esp32_s3_wroom

# Serial monitor
pio run -t monitor -e freenove_esp32_s3_wroom
```

## Architecture

### Dual-Core Split

**Core 1 — Real-Time Engine Control** (dedicated FreeRTOS task via `xTaskCreatePinnedToCore`):
- Crank/cam ISR (hardware timer capture)
- RPM calculation
- Spark timing (dwell + fire)
- Injector timing (pulse width)
- **No WiFi, no logging, no heap allocation** on this core

**Core 0 — Application** (Arduino loop + TaskScheduler):
- Sensor ADC reads (O2, MAP, TPS, CLT, IAT, battery voltage)
- Fuel/ignition table lookups and tuning calculations
- O2 closed-loop AFR correction
- Alternator PID control
- Web server, MQTT, logging, config, OTA

Cores communicate via shared `EngineState` struct with volatile fields.

### Source Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | Entry point, setup/loop, WiFi, tasks, core pinning |
| `src/ECU.cpp` | Top-level engine controller, EngineState management |
| `src/CrankSensor.cpp` | Crank trigger wheel decoding, RPM calculation |
| `src/CamSensor.cpp` | Cam phase detection for sequential mode |
| `src/IgnitionManager.cpp` | Coil dwell + spark timing |
| `src/InjectionManager.cpp` | Injector pulse width + timing |
| `src/FuelManager.cpp` | AFR targets, O2 correction, MAP load calc |
| `src/AlternatorControl.cpp` | PID field control for alternator |
| `src/TuneTable.cpp` | 2D/3D interpolated lookup tables |
| `src/SensorManager.cpp` | ADC reads: O2, MAP, TPS, CLT, IAT, VBAT |
| `src/CJ125Controller.cpp` | Dual-bank CJ125 wideband O2 controller (SPI + heater PID) |
| `src/ADS1115Reader.cpp` | ADS1115 I2C ADC wrapper for CJ125 Nernst cell temp |
| `src/Config.cpp` | SD card and JSON configuration |
| `src/Logger.cpp` | Multi-output logging with tar.gz rotation |
| `src/WebHandler.cpp` | Web server and REST API (HTTP only, no HTTPS) |
| `src/MQTTHandler.cpp` | MQTT client with ECU topics |
| `src/PSRAMAllocator.cpp` | PSRAM allocator override |
| `src/OtaUtils.cpp` | OTA firmware update from SD card |

### GPIO Pin Mapping (ESP32-S3)

**Inputs:**
- Crank sensor: GPIO1 (digital interrupt, 36-1 trigger)
- Cam sensor: GPIO2 (digital interrupt)
- CJ125_UA Bank 1: GPIO3 (ADC1) — wideband O2 lambda/pump current
- CJ125_UA Bank 2: GPIO4 (ADC1) — wideband O2 lambda/pump current
- MAP: GPIO5 (ADC1), TPS: GPIO6 (ADC1)
- Coolant temp: GPIO7 (ADC1), Intake air temp: GPIO8 (ADC1)
- Battery voltage: GPIO9 (ADC1, shared — 47k/10k divider 5.7:1)

**Outputs:**
- Coils 1-8: GPIO10-17 (COP ignition)
- Injectors 1-3: GPIO18, GPIO21, GPIO40 (native); Injectors 4-8: MCP23017 P3-P7 (pins 103-107)
- HEATER_OUT_1: GPIO19 (LEDC ch1 100Hz) — CJ125 heater bank 1 via BTS3134
- HEATER_OUT_2: GPIO20 (LEDC ch2 100Hz) — CJ125 heater bank 2 via BTS3134
- Alternator field PWM: GPIO41 (LEDC 25kHz)
- Fuel pump relay: MCP23017 P0 (pin 100)
- Tachometer output: MCP23017 P1 (pin 101)
- Check engine light: MCP23017 P2 (pin 102)

**SPI (VSPI, shared SD + CJ125):** CLK=GPIO47, MISO=GPIO48, MOSI=GPIO38, CS=GPIO39 (SD card)
- CJ125 SPI_SS_1: MCP23017 P8 (pin 108), SPI_SS_2: MCP23017 P9 (pin 109)
- SD card uses SPI_MODE0 @ 50MHz; CJ125 uses SPI_MODE1 @ 125kHz — `beginTransaction`/`endTransaction` handles switching

**I2C (SDA=GPIO0, SCL=GPIO42):**
- MCP23017 @ 0x20 — GPIO expander for injectors 4-8, fuel pump, tach, CEL, SPI_SS_1/2
- ADS1115 @ 0x48 — 16-bit ADC for CJ125_UR (Nernst cell temp): CH0=Bank 1, CH1=Bank 2

### Task Schedule (Core 0)

| Task | Interval | Purpose |
|------|----------|---------|
| `tPublishState` | 500ms | MQTT state publish |
| `tCpuLoad` | 1s | CPU load calculation |
| `tSaveConfig` | 5min | Persist config to SD |

ECU::update() is called every 10ms internally by the ECU class, handling sensor reads and fuel calculations.

### MQTT Topics

| Topic | Payload | Trigger |
|-------|---------|---------|
| `ecu/state` | `{"rpm":2500,"map":65,"tps":32,...}` | Every 500ms |
| `ecu/fault` | `{"fault":"O2_BANK1","message":"...","active":true}` | On fault |
| `ecu/log` | Log message string | Via Logger |

### CJ125 Wideband O2 Controller

Dual-bank Bosch CJ125 SPI controller driving two LSU 4.9 wideband lambda sensors. Disabled by default (`cj125Enabled = false` in config). When disabled, SensorManager uses linear 0-5V to AFR 10-20 mapping from GPIO3/4.

**CJ125Controller** (`CJ125Controller.h/cpp`):
- Two independent per-bank state machines, each with: IDLE → WAIT_POWER (battery >11V) → CALIBRATING (SPI calibrate cmd, read UA/UR refs) → CONDENSATION (2V heater for 5s) → RAMP_UP (8.5V→13V at +0.4V/s) → PID (heater regulation, readings valid) → ERROR
- SPI protocol: 16-bit frames at 125kHz SPI_MODE1, chip select via MCP23017 P8/P9
- PID heater control: P=120, I=0.8, D=10, integral clamped ±250. Manages PWM duty (0-255) directly targeting calibrated UR reference
- Lambda lookup: 23-point piecewise-linear interpolation from Bosch LSU 4.9 Ip characteristic curve. AFR = lambda × 14.7. O2% = (1 - 1/lambda) × 20.95 for lambda > 1.0
- CJ125_UA ADC conversion: 12-bit 3.3V ESP32 ADC through 2:3 voltage divider → 10-bit 5V equivalent: `adc10bit = adc12bit * (5.0/3.3) * (1023.0/4095.0)`
- Decimation: `update()` called every 10ms from ECU, CJ125 logic executes every 100ms (10× counter)

**ADS1115Reader** (`ADS1115Reader.h/cpp`):
- Adafruit_ADS1X15 wrapper at I2C address 0x48, GAIN_ONE (±4.096V), 128 SPS single-shot
- CH0 = Bank 1 CJ125_UR (Nernst cell temperature), CH1 = Bank 2 CJ125_UR
- UR conversion to 10-bit equivalent: `ur10bit = urMv * 1023 / 5000`

**Integration:**
- ECU stores `_cj125` and `_ads1115` pointers, creates them in `begin()` if `cj125Enabled`
- SensorManager: `setCJ125()` stores pointer; `getO2Afr()` returns CJ125 AFR when `isReady(bank)`, falls back to linear narrowband
- EngineState: `lambda[2]`, `oxygenPct[2]`, `cj125Ready[2]` populated from CJ125Controller in `ECU::update()`
- `/state` JSON: `cj125` object with `enabled` bool and per-bank `bank1`/`bank2` objects (lambda, afr, o2pct, heaterState, heaterDuty, ur, ua, diag, ready)
- Dashboard: AFR card dynamically shows CJ125 wideband view (lambda/AFR/O2%/heater per bank) when enabled, simple narrowband view when disabled

### Configuration

JSON config stored on SD card at `/config.txt`. Contains:
- WiFi/MQTT credentials
- Engine config (cylinders, firing order, crank teeth, displacement, cj125Enabled, etc.)
- Alternator PID settings
- MAP/O2 sensor calibration
- Tune tables (spark, VE, AFR target — 16x16 grids)
- UI theme, timezone, logging settings

### Tune Tables

3D lookup tables (RPM x MAP) stored in PSRAM:
- **Spark Advance Table**: degrees BTDC
- **VE Table**: volumetric efficiency %
- **AFR Target Table**: target air-fuel ratio

Editable via web UI at `/tune`. Persisted to SD card as JSON.

### Memory Management

Global `operator new`/`delete` overridden to route through PSRAM when available (same as GoodmanHPCtrl). PSRAM initialized via `__attribute__((constructor(101)))`.

### Key Build Flags

- `BOARD_ESP32_S3_WROOM`: Board-specific compilation
- `BOARD_HAS_PSRAM`: Enables PSRAM allocation
- `_TASK_TIMEOUT`, `_TASK_STD_FUNCTION`, `_TASK_HEADER_AND_CPP`: TaskScheduler features
- `CIRCULAR_BUFFER_INT_SAFE`: Required
- `DEST_FS_USES_SD`: Required by ESP32-targz
- `SD_SPI_SPEED=50`: SD card SPI speed in MHz

### Web Pages (served from SD card `/www/`)

| Page | Purpose |
|------|---------|
| `/` | Landing page with nav cards |
| `/dashboard` | Live ECU gauges and status |
| `/tune` | 16x16 table editor with live cursor |
| `/config` | WiFi/MQTT/engine/alternator/sensor settings |
| `/update` | OTA firmware upload |
| `/log/view` | Log viewer |
| `/heap/view` | Memory/CPU monitor |
| `/wifi/view` | WiFi scan and test |
| `/admin/setup` | Initial password setup |
