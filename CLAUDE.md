# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3 Engine Control Unit for gas engines. Controls coil-over-plug ignition (up to 12 cylinders), sequential/batch fuel injection, wideband O2 closed-loop, and alternator field PWM. Provides a REST API, WebSocket, and MQTT interface for remote monitoring and tuning.

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
| `src/Config.cpp` | SD card and JSON configuration |
| `src/Logger.cpp` | Multi-output logging with tar.gz rotation |
| `src/WebHandler.cpp` | Web server and REST API (HTTP only, no HTTPS) |
| `src/MQTTHandler.cpp` | MQTT client with ECU topics |
| `src/PSRAMAllocator.cpp` | PSRAM allocator override |
| `src/OtaUtils.cpp` | OTA firmware update from SD card |

### GPIO Pin Mapping (ESP32-S3)

**Inputs:**
- Crank sensor: GPIO1 (digital interrupt)
- Cam sensor: GPIO2 (digital interrupt)
- O2 Bank 1: GPIO3 (ADC1), O2 Bank 2: GPIO4 (ADC1)
- MAP: GPIO5 (ADC1), TPS: GPIO6 (ADC1)
- Coolant temp: GPIO7 (ADC1), Intake air temp: GPIO8 (ADC1)
- Battery voltage: GPIO9 (ADC1)

**Outputs:**
- Coils 1-8: GPIO10-17
- Injectors 1-8: GPIO18, GPIO21, GPIO35-40
- Alternator field PWM: GPIO41 (LEDC 25kHz)
- Fuel pump relay: GPIO42
- Tachometer output: GPIO43
- Check engine light: GPIO44

**SPI (SD Card):** CS=GPIO45, MOSI=GPIO46, CLK=GPIO47, MISO=GPIO48

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

### Configuration

JSON config stored on SD card at `/config.txt`. Contains:
- WiFi/MQTT credentials
- Engine config (cylinders, firing order, crank teeth, displacement, etc.)
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
