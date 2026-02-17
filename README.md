# ESP-ECU

ESP32-S3 Engine Control Unit for gas engines. Controls coil-over-plug ignition (up to 12 cylinders), sequential/batch fuel injection, dual-bank CJ125 wideband O2 closed-loop (LSU 4.9 sensors), and alternator field PWM. I/O expanded via MCP23017 I2C GPIO expander. Provides a REST API, WebSocket, and MQTT interface for remote monitoring and tuning.

## Features

- **Coil-over-plug ignition** -- Dwell + spark timing for up to 8 coils (GPIO10-17), expandable to 12 via MCP23017
- **Sequential fuel injection** -- Injector pulse width and timing for up to 8 injectors (3 native GPIO + 5 via MCP23017 I2C expander)
- **Dual-bank wideband O2** -- Bosch CJ125 SPI controller driving two LSU 4.9 wideband lambda sensors with PID heater control and 23-point piecewise-linear Ip-to-lambda lookup
- **Closed-loop AFR correction** -- O2-based fuel trim with configurable AFR targets per RPM/MAP cell
- **3D tune tables** -- 16x16 RPM x MAP interpolated lookup tables for spark advance, volumetric efficiency, and AFR targets. Editable via web UI
- **Alternator field control** -- PID-regulated PWM output for alternator voltage regulation
- **Crank/cam decoding** -- 36-1 trigger wheel with cam phase detection for sequential mode
- **Automatic transmission control** -- Ford 4R70W and 4R100 shift solenoid control, TCC PWM lockup, EPC line pressure, TFT temp monitoring, and MLPS gear range detection via MCP23S17 SPI expander (5V via TXB0108 level shifter). OSS/TSS speed sensors when ADS1115@0x49 frees GPIO 5/6
- **I/O expansion** -- 6x SPI MCP23S17 (96 pins) on shared HSPI bus with single CS, hardware addressing (HAEN), unified virtual pin routing, ghost device detection, and runtime health monitoring
- **Safe mode** -- Automatic boot loop detection with peripheral isolation. Configurable per-device enable/disable for I2C and SPI expanders via web UI
- **Limp mode** -- Sensor fault protection (MAP, TPS, CLT, IAT, VBAT), expander health monitoring, and oil pressure sensor support. Reduces rev limit, caps ignition advance, locks transmission gear, and lights CEL
- **Board diagnostics** -- CD74HC4067 16:1 analog mux + dedicated ADS1115 ADC monitors 16 test points (power rails, output drivers, board temp, current) during idle time. Fault detection with debounce, burst investigation, and power rail cross-validation
- **Oil pressure monitoring** -- Configurable as digital switch or analog sender (0-5V via MCP3204 or native GPIO), with engine-running guard and startup delay
- **Remote access** -- REST API, WebSocket, and MQTT for monitoring and tuning
- **Live dashboard** -- Real-time gauges and status at `/dashboard`
- **Web-based tuning** -- 16x16 table editor with live cursor at `/tune`
- **SD card configuration** -- WiFi, MQTT, engine, and tune table settings stored as JSON
- **Multi-output logging** -- Serial, MQTT, SD card with tar.gz compressed log rotation, and WebSocket streaming
- **OTA updates** -- Firmware upload via web interface
- **FTP server** -- File upload to SD card for web pages and config
- **PSRAM support** -- All heap allocations routed through PSRAM when available
- **WiFi AP fallback** -- Automatic AP mode for emergency recovery
- **Dual-core architecture** -- Real-time engine control on Core 1, application/networking on Core 0

## Architecture

### Dual-Core Split

**Core 1 -- Real-Time Engine Control** (dedicated FreeRTOS task via `xTaskCreatePinnedToCore`):
- Crank/cam ISR (hardware timer capture)
- RPM calculation
- Spark timing (dwell + fire)
- Injector timing (pulse width)
- No WiFi, no logging, no heap allocation on this core

**Core 0 -- Application** (Arduino loop + TaskScheduler):
- Sensor ADC reads (O2, MAP, TPS, CLT, IAT, battery voltage)
- Fuel/ignition table lookups and tuning calculations
- O2 closed-loop AFR correction
- CJ125 wideband heater state machine
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
| `src/ADS1115Reader.cpp` | ADS1115 I2C ADC wrapper (CJ125 Nernst @ 0x48, MAP/TPS @ 0x49) |
| `src/MCP3204Reader.cpp` | MCP3204 SPI 12-bit ADC for MAP/TPS (alternative to ADS1115 @ 0x49) |
| `src/TransmissionManager.cpp` | Ford 4R70W/4R100 automatic transmission controller |
| `src/PinExpander.cpp` | 6x SPI MCP23S17 GPIO expander, shared CS + HAEN, interrupt support, health check |
| `src/Config.cpp` | SD card and JSON configuration |
| `src/Logger.cpp` | Multi-output logging with tar.gz rotation |
| `src/WebHandler.cpp` | Web server and REST API |
| `src/MQTTHandler.cpp` | MQTT client with ECU topics |
| `src/BoardDiagnostics.cpp` | Built-in self-test: CD74HC4067 mux + ADS1115 health monitoring |
| `src/PSRAMAllocator.cpp` | PSRAM allocator override |
| `src/OtaUtils.cpp` | OTA firmware update from SD card |

## Hardware

**Target board:** Freenove ESP32-S3-WROOM

### GPIO Pin Mapping

**Inputs:**

| Pin | GPIO | Description |
|-----|------|-------------|
| Crank | 1 | Digital interrupt, 36-1 trigger wheel |
| Cam | 2 | Digital interrupt, phase detection |
| CJ125_UA Bank 1 | 3 | ADC -- wideband O2 lambda/pump current |
| CJ125_UA Bank 2 | 4 | ADC -- wideband O2 lambda/pump current |
| MAP | ADS1115@0x49 CH0 | I2C ADC -- Manifold Absolute Pressure (GAIN_TWOTHIRDS, 860SPS) |
| TPS | ADS1115@0x49 CH1 | I2C ADC -- Throttle Position Sensor (GAIN_TWOTHIRDS, 860SPS) |
| OSS | 5 | Digital ISR -- Output shaft speed (freed by ADS1115@0x49) |
| TSS | 6 | Digital ISR -- Turbine shaft speed (freed by ADS1115@0x49) |
| CLT | 7 | ADC -- Coolant Temperature (NTC thermistor) |
| IAT | 8 | ADC -- Intake Air Temperature (NTC thermistor) |
| VBAT | 9 | ADC -- Battery voltage (47k/10k divider, 5.7:1) |

**Outputs:**

| Pin | GPIO / Bus | Description |
|-----|------------|-------------|
| Coils 1-8 | MCP23S17 #4 P0-P7 | COP ignition via SPI (HSPI 10MHz), pins 264-271 |
| Injectors 1-8 | MCP23S17 #5 P0-P7 | High-Z injectors via SPI (HSPI 10MHz), pins 280-287 |
| HEATER_OUT_1 | 19 | LEDC ch1 100Hz -- CJ125 heater bank 1 via BTS3134 |
| HEATER_OUT_2 | 20 | LEDC ch2 100Hz -- CJ125 heater bank 2 via BTS3134 |
| Alternator field | 41 | LEDC 25kHz PWM |
| TCC_PWM | 45 | LEDC ch4 200Hz -- Torque converter clutch (strapping pin, OK after boot) |
| EPC_PWM | 46 | LEDC ch6 5kHz -- Electronic pressure control (strapping pin, OK after boot) |
| Fuel pump relay | MCP23S17 #0 P0 | SPI expander (pin 200) |
| Tachometer output | MCP23S17 #0 P1 | SPI expander (pin 201) |
| Check engine light | MCP23S17 #0 P2 | SPI expander (pin 202) |
| ADS1115 ALERT #0 | MCP23S17 #0 P3 | ADS1115 @0x4A ALERT/RDY input (pin 203) |
| ADS1115 ALERT #1 | MCP23S17 #0 P4 | ADS1115 @0x48 ALERT/RDY input (pin 204) |
| ADS1115 ALERT #2 | MCP23S17 #0 P5 | ADS1115 @0x49 ALERT/RDY input (pin 205) |
| CJ125 SS1 | MCP23S17 #0 P8 | CJ125 Bank 1 chip select (pin 208) |
| CJ125 SS2 | MCP23S17 #0 P9 | CJ125 Bank 2 chip select (pin 209) |
| SS_A | MCP23S17 #1 P0 | Shift Solenoid A (5V via TXB0108, pin 216) |
| SS_B | MCP23S17 #1 P1 | Shift Solenoid B (5V via TXB0108, pin 217) |
| SS_C | MCP23S17 #1 P2 | Shift Solenoid C -- 4R100 only (5V via TXB0108, pin 218) |
| SS_D | MCP23S17 #1 P3 | Coast Clutch -- 4R100 only (5V via TXB0108, pin 219) |

**SPI Bus — FSPI (shared SD + CJ125):**

| Pin | GPIO | Description |
|-----|------|-------------|
| CLK | 47 | SPI clock |
| MISO | 48 | SPI data in |
| MOSI | 38 | SPI data out |
| SD_CS | 39 | SD card chip select (SPI_MODE0 @ 50MHz) |
| SPI_SS_1 | MCP23S17 #0 P8 | CJ125 Bank 1 chip select (SPI_MODE1 @ 125kHz) |
| SPI_SS_2 | MCP23S17 #0 P9 | CJ125 Bank 2 chip select (SPI_MODE1 @ 125kHz) |

**SPI Bus — HSPI (6x MCP23S17, shared CS with hardware addressing):**

| Pin | GPIO | Description |
|-----|------|-------------|
| HSPI_SCK | 10 | SPI clock (10 MHz) |
| HSPI_MOSI | 11 | SPI data out |
| HSPI_MISO | 12 | SPI data in (10kΩ pull-up to VCC recommended) |
| HSPI_CS | 13 | Shared chip select for all 6 MCP23S17 devices |
| INT | 15 | Shared open-drain interrupt line (10kΩ pull-up to 3.3V) |

All 6 MCP23S17 devices share the same CS line. Each device is addressed individually via a 3-bit hardware address (A2:A1:A0 pins) embedded in the SPI command byte. See [I/O Expanders](#io-expanders) for details.

**I2C Bus (SDA=GPIO0, SCL=GPIO42, level-shifted to 5V via PCA9306DCUR):**

| Device | Address | Description |
|--------|---------|-------------|
| PCA9306DCUR | — | I2C level shifter: VREF1=3.3V (ESP32), VREF2=5V from AMS1117-5.0 |
| REF5050AIDR | — | Precision 5.000V reference → ADS1115 VDD (clean supply for internal ref) |
| ADS1115 #0 | 0x48 | 16-bit ADC @ 5V -- CJ125_UR (CH0/1), TFT temp (CH2), MLPS (CH3) |
| ADS1115 #1 | 0x49 | 16-bit ADC @ 5V -- MAP (CH0), TPS (CH1). Frees GPIO 5/6 for OSS/TSS |
| ADS1115 #2 | 0x4A | 16-bit ADC @ 5V -- Diagnostics mux output (AIN0) |

**GPIO Allocation Summary:**

All 6 MCP23S17 share a single CS (GPIO 13) and a single interrupt line (GPIO 15). I2C bus is used only for ADS1115 ADCs. GPIO 14, 16-18, 21, 40 are free. GPIO 22-25 do not exist on ESP32-S3. GPIO 26-32 are reserved for SPI flash. GPIO 33-37 are reserved for OPI PSRAM. ADS1115 at 0x49 reads MAP/TPS via I2C, freeing GPIO 5/6 for OSS/TSS speed sensor inputs.

| Range | Assignment |
|-------|------------|
| 0 | I2C SDA |
| 1-2 | Crank + Cam ISR inputs (locked, not configurable) |
| 3-4 | CJ125 wideband O2 ADC |
| 5-6 | OSS/TSS speed ISR (freed from MAP/TPS by ADS1115@0x49) |
| 7-9 | Sensor ADC (CLT, IAT, VBAT) |
| 10-13 | HSPI bus (SCK, MOSI, MISO, shared CS) — 6x MCP23S17 |
| 14, 16-18, 21, 40 | **FREE** — available for custom I/O |
| 15 | Shared interrupt line (MCP23S17 INTA, open-drain with pull-up) |
| 19-20 | CJ125 heater PWM |
| 38-39 | SD card SPI (MOSI, CS) |
| 41 | Alternator field PWM |
| 42 | I2C SCL |
| 43-44 | UART TX/RX (Serial) |
| 45-46 | TCC/EPC PWM (strapping pins, OK after boot) |
| 47-48 | SD card SPI (CLK, MISO) |

**Virtual Pin Ranges:**

| Range | Bus | HW Addr | Device | Priority |
|-------|-----|---------|--------|----------|
| 200-215 | SPI | 0 (A=000) | MCP23S17 #0 — General I/O (inputs) | Highest |
| 216-231 | SPI | 1 (A=001) | MCP23S17 #1 — Trans solenoids + spare | High |
| 232-247 | SPI | 2 (A=010) | MCP23S17 #2 — Expansion / custom I/O | Medium |
| 248-263 | SPI | 3 (A=011) | MCP23S17 #3 — Expansion / custom I/O | Medium |
| 264-279 | SPI | 4 (A=100) | MCP23S17 #4 — Coils (outputs only) | Low |
| 280-295 | SPI | 5 (A=101) | MCP23S17 #5 — Injectors (outputs only) | Lowest |

## CJ125 Wideband O2 Controller

Dual-bank Bosch CJ125 SPI controller driving two LSU 4.9 wideband lambda sensors. Disabled by default (`cj125Enabled = false` in config). When disabled, SensorManager falls back to linear 0-5V to AFR 10-20 mapping from GPIO3/4.

**Heater state machine** (non-blocking, per bank):

```
IDLE -> WAIT_POWER (battery > 11V) -> CALIBRATING -> CONDENSATION (2V, 5s)
    -> RAMP_UP (8.5V -> 13V at +0.4V/s) -> PID (heater regulation, readings valid)
    -> ERROR (diagnostic failure)
```

- SPI protocol: 16-bit frames at 125kHz SPI_MODE1, chip select via MCP23S17 #0 P8/P9
- PID heater control: P=120, I=0.8, D=10, integral clamped +/-250
- Lambda lookup: 23-point piecewise-linear interpolation from Bosch LSU 4.9 Ip characteristic curve
- Decimation: `update()` called every 10ms from ECU, CJ125 logic executes every 100ms

The CJ125 SPI register constants and PID tuning values are derived from the [Lambda Shield](https://github.com/Bylund/Lambda-Shield-Example) project by Bylund.

## I/O Expanders

The ECU extends GPIO capacity through 6x Microchip MCP23S17 SPI GPIO expanders, all sharing a single HSPI bus and a single chip select (CS) line. Each device is addressed individually via 3-bit hardware addressing (HAEN). All pin operations route through `xDigitalWrite()` / `xDigitalRead()` / `xPinMode()` which automatically dispatch to native GPIO or SPI expander based on pin number.

### Shared CS with Hardware Addressing (HAEN)

All 6 MCP23S17 devices share the same 4 SPI wires (SCK, MOSI, MISO, CS). When CS goes LOW, all chips see the SPI traffic, but only the chip whose hardware address matches the address in the SPI command byte responds. The address is set by tying each chip's A0/A1/A2 pins to VCC (3.3V) or GND on the PCB — no additional logic or components required.

**SPI command byte format:**

```
Bit:    7    6    5    4    3    2    1    0
        0    1    0    0   A2   A1   A0   R/W
        ─────────────────  ──────────────  ───
        Fixed prefix       Hardware addr   0=write, 1=read
```

**Hardware address wiring:**

| Device | A2 | A1 | A0 | Addr | Wiring |
|--------|----|----|-----|------|--------|
| #0 | GND | GND | GND | 0 | All three to ground |
| #1 | GND | GND | VCC | 1 | A0 to 3.3V |
| #2 | GND | VCC | GND | 2 | A1 to 3.3V |
| #3 | GND | VCC | VCC | 3 | A0 + A1 to 3.3V |
| #4 | VCC | GND | GND | 4 | A2 to 3.3V |
| #5 | VCC | GND | VCC | 5 | A2 + A0 to 3.3V |

The A0/A1/A2 pins are dedicated address pins, separate from the 16 I/O pins (GPA0-7, GPB0-7). No I/O capacity is lost — each chip provides 16 full I/O pins regardless of address configuration.

### Device Allocation and Priority

Devices are numbered by priority — input-focused chips get the lowest addresses and are scanned first during interrupt handling. Output-only chips are scanned last since they rarely (if ever) generate interrupts.

| Device | Pins | Purpose | Interrupt Priority |
|--------|------|---------|--------------------|
| #0 (addr 0) | 200-215 | General I/O — fuel pump, tach, CEL, ADS1115 ALERT/RDY, CJ125 CS | **Highest** — scan first |
| #1 (addr 1) | 216-231 | Transmission solenoids (SS-A/B/C/D) + spare I/O | High |
| #2 (addr 2) | 232-247 | Expansion — custom I/O, user-defined pins | Medium |
| #3 (addr 3) | 248-263 | Expansion — custom I/O, user-defined pins | Medium |
| #4 (addr 4) | 264-279 | Coils 1-8 (P0-P7), diag mux S0-S3+EN (P8-P12), 3 spare (P13-P15) | **Low** — output only |
| #5 (addr 5) | 280-295 | Injectors 1-8 (P0-P7), 8 spare (P8-P15) | **Lowest** — output only |

**Recommendation:** Configure input-responsive functions (custom pin ISR, digital switch inputs, feedback signals) on devices #0-#3. Reserve devices #4-#5 for outputs only (coils, injectors). This ensures interrupt scanning prioritizes chips that are most likely to have pending state changes.

### Shared Open-Drain Interrupt Line

All 6 MCP23S17 INTA pins are wired together to a single ESP32 GPIO (default GPIO 15) via an open-drain bus with a 10kΩ pull-up resistor to 3.3V. This provides pin-change interrupt detection across all 96 expander pins using just one ESP32 GPIO.

**Wiring diagram:**

```
                             10kΩ
                       3.3V ──┤├──┬── ESP32 GPIO 15 (INPUT_PULLUP, FALLING edge)
                                  │
  MCP#0 INTA (open-drain) ───────┤
  MCP#1 INTA (open-drain) ───────┤
  MCP#2 INTA (open-drain) ───────┤
  MCP#3 INTA (open-drain) ───────┤
  MCP#4 INTA (open-drain) ───────┤
  MCP#5 INTA (open-drain) ───────┘
```

**How it works:**

1. Each chip is configured with IOCON bits: **MIRROR=1** (INTA covers both ports A and B), **ODR=1** (open-drain output), **INTPOL=0** (active-low)
2. When any enabled pin changes state on any chip, that chip pulls the shared line LOW
3. The ESP32 FALLING-edge ISR fires and sets a volatile flag
4. In the main `update()` loop, the firmware scans chips in priority order (#0 first, #5 last):
   - Read **INTF** register (2 bytes) — bitmask of which pins triggered
   - Read **INTCAP** register (2 bytes) — captured pin values at time of interrupt (also clears the interrupt)
   - Skip chips with GPINTEN = 0x0000 (no interrupts enabled, typically output-only chips #4/#5)
5. Matched pin changes are dispatched to custom pin ISR handlers

**Performance:** Scanning all 6 chips takes ~25μs at 10 MHz SPI (6 x 2 register reads x 3 bytes each). In practice, only chips with GPINTEN > 0 are scanned, so output-only chips add zero overhead.

**Why open-drain?** Push-pull interrupt outputs cannot be wired together — if one chip drives HIGH while another drives LOW, you get a short circuit. Open-drain outputs only pull LOW or float, so any chip can assert the shared line without conflict. The 10kΩ pull-up returns the line to HIGH when no interrupts are pending.

### SPI Performance

The custom thin SPI driver operates at 10 MHz with shadow registers, achieving ~3-5μs per write vs ~30μs with the Adafruit library at 1 MHz. Shadow registers (local copy of OLAT) eliminate read-modify-write cycles — each pin change is a single 3-byte SPI write. The MCP3204 ADC (1 MHz, SPI_MODE0) coexists on the same HSPI bus; `beginTransaction()` sets the clock and mode per-device.

### Ghost Device Detection

SPI expanders are probed during `begin()` by writing IOCON (with HAEN=1, MIRROR=1, ODR=1) and reading it back. A real device returns the written value; a missing/ghost device returns 0xFF or 0x00. Devices that fail probe are marked not-ready and all pin operations become no-ops.

### Runtime Health Monitoring

`PinExpander::healthCheck()` probes all initialized devices every ~1 second by reading the IOCON register via SPI. Failed devices are reported as a bitmask in `expanderFaults` and trigger limp mode. The health check accepts the configured IOCON value (0x68 = HAEN+MIRROR+ODR) as valid.

### Level Shifting (3.3V ESP32 to 5V MCP23S17)

The MCP23S17 devices operate at 5V for compatibility with automotive solenoid drivers (BTS3134, ULN2803) and 5V sensors. The ESP32-S3 operates at 3.3V. Level shifting is required because the MCP23S17's CMOS input threshold at 5V is VIH = 0.7 x VDD = 3.5V, which exceeds the ESP32's 3.3V output HIGH.

**TXB0108 for data lines (SCK, MOSI, MISO):**

The TI TXB0108 bidirectional level shifter handles the four SPI data lines (SCK, MOSI, MISO, and the shared interrupt line). The TXB0108 uses internal current sources (~4mA) to auto-detect data direction, which works well for signals without external pull-ups or with pull-ups >= 50k ohm.

**MOSFET for CS line (cannot use TXB0108):**

The shared CS line has a 10k ohm pull-up to 5V (required for reliable idle-HIGH when CS is not driven). This pull-up is too strong for the TXB0108 -- its ~4mA current source cannot overcome the pull-up current, causing the output to latch HIGH and never drive LOW. TI's datasheet specifies external pull-ups must be > 50k ohm.

Instead, a discrete N-channel MOSFET (L2N7002SLLT1G) in source-follower configuration provides reliable level shifting for the CS line:

```
         Q4 — CS Level Shifter

         3.3V                          5V
          |                             |
         [R] R51 10k                   [R] R52 10k
          |                             |
     G ---+                        D ---+--- MCP23S17_CS (all 6)
     |    |                        |
     Q4 L2N7002SLLT1G             Q4
     |    |                        |
     S ---+--- ESP32 GPIO 13      (same FET)
```

- **Gate (G):** Tied to 3.3V rail
- **Source (S):** ESP32 GPIO 13 with R51 (10k) pull-up to 3.3V
- **Drain (D):** MCP23S17 CS (all 6 tied together) with R52 (10k) pull-up to 5V

**How it works:**

| ESP32 CS | Source (V) | Vgs (V) | FET | Drain (5V side) | MCP23S17 CS |
|----------|-----------|---------|-----|-----------------|-------------|
| LOW (0V) | 0V | 3.3V (on) | ON | ~0V (pulled low through FET) | LOW (selected) |
| HIGH (3.3V) | 3.3V | 0V (off) | OFF | 5V (pulled up by R52) | HIGH (deselected) |

The MOSFET adds < 5ns propagation delay and < 1 ohm on-resistance (Rds_on), which is negligible at 10 MHz SPI. The 10k pull-up on the drain provides the HIGH level and also serves as the idle pull-up for the shared CS bus.

**Why not a simple resistor divider?** CS is bidirectional in the sense that the idle state is maintained by the pull-up, not actively driven. A resistor divider would fight the pull-up and waste current. The MOSFET cleanly switches between driven-LOW and pulled-HIGH.

**MOSFET for RESET line (active reset from ESP32 EN):**

A second L2N7002SLLT1G drives all 6 MCP23S17 RESET pins from the ESP32 EN signal. Same source-follower topology as CS -- both EN and RESET are active-LOW, so the non-inverting level shifter gives correct polarity. A 100nF gate capacitor (C76) delays the gate charge at power-on, preventing a RESET glitch while the 3.3V rail ramps.

```
         Q5 — RESET Level Shifter

         3.3V                          5V
          |                             |
         [R] R55 10k                   [R] R56 10k
          |                             |
     G ---+---||--- GND           D ---+--- MCP23S17_RESET (all 6)
     |   C76 100nF                |
     Q5 L2N7002SLLT1G            Q5
     |    |                       |
     S ---+--- ESP32 EN          (same FET)
```

- **Gate (G):** 3.3V rail with C76 (100nF) to GND
- **Source (S):** ESP32 EN pin with R55 (10k) pull-up to 3.3V
- **Drain (D):** MCP23S17 RESET (all 6 tied together) with R56 (10k) pull-up to 5V

| ESP32 EN | Source (V) | Vgs (V) | FET | Drain (5V side) | MCP23S17 RESET |
|----------|-----------|---------|-----|-----------------|----------------|
| LOW (reset) | 0V | 3.3V (on) | ON | ~0V | LOW (devices reset) |
| HIGH (running) | 3.3V | 0V (off) | OFF | 5V (pulled up by R56) | HIGH (devices run) |

**Gate capacitor C76:** At cold power-on, the 100nF cap delays the gate voltage rise, preventing a brief RESET glitch while the 3.3V rail ramps. On warm reset (EN cycles but 3.3V stays up), the gate is already charged so the cap has no effect -- RESET releases as soon as EN goes HIGH. If additional hold time is needed on warm reset, a small cap (100nF-1uF) can be added on the drain side (RESET to GND) to slow the R56 charge-up.

**Why active RESET matters:** Without active RESET, the MCP23S17s retain their last output state across an ESP32 software reset / WDT / panic. Coils and injectors could stay energized for the ~100-500ms it takes the ESP32 to reboot and call `PinExpander::begin()`. With EN driving RESET, all outputs go to high-Z (IODIR resets to 0xFFFF) the instant the ESP32 enters reset.

**Bill of materials for level shifting:**

| Part | Qty | Function |
|------|-----|----------|
| TXB0108PWR (TSSOP-20) | 1 | 8-bit bidirectional level shifter for SCK, MOSI, MISO, INT |
| L2N7002SLLT1G (SOT-23) | 2 | N-channel MOSFETs: Q4 for CS, Q5 for RESET |
| R51, R55 — 10k ohm 0402 | 2 | Source pull-ups to 3.3V (CS, RESET) |
| R52, R56 — 10k ohm 0402 | 2 | Drain pull-ups to 5V (CS, RESET) |
| C76 — 100nF 0402 | 1 | Gate RC on Q5 (RESET startup delay) |
| 100nF 0402 | 2 | Bypass caps: VCCA (3.3V) and VCCB (5V) on TXB0108 |

### I2C Bus (ADS1115 only)

With all GPIO expanders on SPI, the I2C bus (SDA=GPIO0, SCL=GPIO42) is used exclusively for the ADS1115 ADCs. A PCA9306DCUR (LCSC C33196) level shifter translates between the 3.3V ESP32 I2C domain and the 5V ADS1115 domain. All three ADS1115s operate at VDD=5V from a REF5050AIDR precision voltage reference (±0.1% accuracy, 8ppm/°C drift, 3µVpp/V noise), providing a clean reference supply that maximizes effective ADC resolution. The CD74HC4067 mux and PCA9306 run from a separate AMS1117-5.0 LDO. Both 5V rails share a dedicated analog ground (AGND) connected to the digital GND plane via a Murata BLM21PG221SN1D ferrite bead + 100nF/22µF decoupling.

| Device | Address | Description |
|--------|---------|-------------|
| PCA9306DCUR | — | I2C level shifter: VREF1=3.3V (ESP32), VREF2=5V (ADS1115s) |
| ADS1115 #0 | 0x48 | CJ125_UR (CH0/1), TFT temp (CH2), MLPS (CH3) |
| ADS1115 #1 | 0x49 | MAP (CH0), TPS (CH1) — frees GPIO 5/6 for OSS/TSS |
| ADS1115 #2 | 0x4A | Diagnostics mux output (AIN0) — dedicated, no contention |

**PCA9306 specs:** Ron ~3.5 ohm typical, <1.7ns switching, 400kHz I2C, 400pF bus capacitance budget. EN tied to VREF2 via 200k ohm pull-up per TI reference design.

**Dedicated analog power supply:** An LGS5145 buck converter (C5123971) generates 6.8V (R_top=15k, R_bot=2k, Vref=0.8V), which feeds two REF5050 precision references and an AMS1117-5.0 LDO. A SMBJ7.0A TVS (C19077563) clamps the 6.8V rail against transients. The AGND plane connects to digital GND via a Murata BLM21PG221SN1D ferrite bead (220Ω@100MHz, 45mΩ DCR, C85840) with 100nF (C85923) + 22µF ceramic decoupling on the AGND side. The ferrite bead provides lossy broadband noise suppression without the resonance risk of an LC inductor filter, while the dual capacitors handle both high-frequency and transient filtering. The AMS1117-5.0 output uses a KEMET T491D tantalum capacitor (47µF, C117039) for LDO stability — the AMS1117 requires output ESR of 0.1-0.5Ω which tantalum provides (~0.7-1.5Ω).

**REF5050AIDR ×2 (LCSC C27804):** Precision 5.000V reference, ±0.1% initial accuracy, 8ppm/°C temp drift, 3µVpp/V noise, ±10mA output, VIN min 5.2V. SOIC-8 package, 22ppm long-term drift per 1000h. Both fed from 6.8V buck via 33Ω input RC filter (R=33Ω + C=10µF/100nF, fc≈480Hz) for switching noise rejection. Headroom = 1.8V.
- **REF5050 #1:** VREF input on MCP3204 SPI ADC — sets the 12-bit full-scale range to exactly 5.000V
- **REF5050 #2:** VDD for all 3 ADS1115 I2C ADCs — provides clean supply for internal bandgap reference (~0.6mA total load)

**AMS1117-5.0 (LCSC C6187):** 5V LDO, 1A output, 1.3V dropout. Supplies VCC to CD74HC4067 mux and VREF2 to PCA9306 level shifter. Input from 6.8V buck (1.8V headroom). Output capacitor is 47µF KEMET T491D tantalum (ESR ~0.7-1.5Ω) for LDO stability.

### ADS1115 Conversion-Ready (ALERT/RDY)

Each ADS1115 has an open-drain ALERT/RDY output (pin 6) that goes LOW when a conversion completes. The Adafruit library configures conversion-ready mode automatically in `startADCReading()` (Hi_thresh=0x8000, Lo_thresh=0x0000, CQUE=1CONV). This eliminates the need to poll the I2C config register — a three-tier detection scheme provides the fastest available method based on pin type:

| Method | Per-check cost | Mechanism | Used when |
|--------|---------------|-----------|-----------|
| Native GPIO ISR | ~0 µs | `IRAM_ATTR` ISR on FALLING edge sets volatile flag | ALERT/RDY wired to ESP32 GPIO < 49 |
| Expander SPI read | ~20 µs | Direct `xDigitalRead()` via MCP23S17 SPI | ALERT/RDY wired to MCP23S17 pin (200+) |
| I2C config poll | ~200 µs | `conversionComplete()` reads ADS1115 config register | No ALERT/RDY pin configured (pin = 0) |

The expander SPI read is 10x faster than I2C config register polling. Native GPIO ISR is effectively zero-cost — the volatile flag is set by hardware interrupt and checked with a single memory read.

**ALERT/RDY pin assignments (MCP23S17 #0, INPUT_PULLUP):**

| ADS1115 | Address | ALERT/RDY Pin | MCP23S17 #0 | Config key | Default |
|---------|---------|---------------|-------------|------------|---------|
| #0 (CJ125) | 0x48 | 204 | P4 | `pins.adsAlert1` | 204 |
| #1 (MAP/TPS) | 0x49 | 205 | P5 | `pins.adsAlert2` | 205 |
| #2 (Diagnostics) | 0x4A | 203 | P3 | `diagnostics.alertPin` | 203 |

**Wiring:** ADS1115 ALERT/RDY (open-drain) → MCP23S17 input pin with internal pull-up enabled (GPPUA/B register). No external pull-up resistor needed. Set any pin to 0 in config to disable ALERT/RDY and fall back to I2C polling for that device.

## Board Diagnostics

Built-in self-test and health monitoring system using a CD74HC4067SM96 16:1 analog multiplexer and a dedicated ADS1115 ADC. Diagnostics run only during idle time within the 10ms ECU update cycle — never impacting real-time engine control. The system monitors power rails, output drivers, control signals, board temperature, and current draw, detecting faults automatically with debounce and burst-sample investigation.

Disabled by default (`diagEnabled = false`). Enable in config and connect the hardware to activate.

### CD74HC4067SM96 Pinout (LCSC C98457)

[TI CD74HC4067 Datasheet](https://www.ti.com/lit/ds/symlink/cd74hc4067.pdf) — SSOP-24 package, 2V-6V, 70 ohm Ron

| Pin | Name | Connection |
|-----|------|------------|
| 1 | COM (common I/O) | ADS1115 @ 0x4A AIN0 (analog signal to ADC) |
| 2 | I7 | CH7: Fuel pump relay drive (47k/10k divider) |
| 3 | I6 | CH6: Alternator field PWM (RC average) |
| 4 | I5 | CH5: Injector 1 output (47k/10k divider) |
| 5 | I4 | CH4: Coil 1 output (47k/10k divider) |
| 6 | I3 | CH3: 5V VCCB post-TXB0108 (15k/10k divider) |
| 7 | I2 | CH2: 12V battery (47k/10k divider) |
| 8 | I1 | CH1: 5V rail (15k/10k divider) |
| 9 | I0 | CH0: 3.3V rail (direct) |
| 10 | S0 | MCP23S17 #4 P8 (pin 272) — select bit 0 |
| 11 | S1 | MCP23S17 #4 P9 (pin 273) — select bit 1 |
| 12 | GND | Ground |
| 13 | S2 | MCP23S17 #4 P10 (pin 274) — select bit 2 |
| 14 | S3 | MCP23S17 #4 P11 (pin 275) — select bit 3 |
| 15 | I15 | CH15: Spare (user-defined) |
| 16 | I14 | CH14: Board current (0.1R shunt, gain 20) |
| 17 | I13 | CH13: CJ125 heater 2 sample (divider 11:1) |
| 18 | I12 | CH12: CJ125 heater 1 sample (divider 11:1) |
| 19 | I11 | CH11: Board temperature NTC (10k pullup, 10k NTC @ 25C) |
| 20 | I10 | CH10: PERIPH_EN GPIO (3.3V domain) |
| 21 | I9 | CH9: MCP23S17 RESET# (15k/10k divider, 5V domain) |
| 22 | I8 | CH8: TXB0108 OE (3.3V domain, direct) |
| 23 | EN | MCP23S17 #4 P12 (pin 276) — active LOW enable |
| 24 | VCC | 3.3V supply (operates in 3.3V domain, inputs use voltage dividers for higher rails) |

**Channel select truth table:**

| S3 | S2 | S1 | S0 | Channel | Test Point |
|----|----|----|-----|---------|------------|
| 0 | 0 | 0 | 0 | I0 | 3V3_RAIL — 3.3V rail direct |
| 0 | 0 | 0 | 1 | I1 | 5V_RAIL — 5V rail (15k/10k divider) |
| 0 | 0 | 1 | 0 | I2 | 12V_BATT — Battery (47k/10k divider) |
| 0 | 0 | 1 | 1 | I3 | 5V_VCCB — TXB0108 5V side (15k/10k divider) |
| 0 | 1 | 0 | 0 | I4 | COIL1_OUT — Coil 1 drive (47k/10k divider) |
| 0 | 1 | 0 | 1 | I5 | INJ1_OUT — Injector 1 drive (47k/10k divider) |
| 0 | 1 | 1 | 0 | I6 | ALT_FIELD — Alternator PWM (RC average) |
| 0 | 1 | 1 | 1 | I7 | FUELPUMP — Fuel pump relay (47k/10k divider) |
| 1 | 0 | 0 | 0 | I8 | TXB_OE — TXB0108 output enable (3.3V direct) |
| 1 | 0 | 0 | 1 | I9 | MCP_RESET — MCP23S17 RESET# (15k/10k divider) |
| 1 | 0 | 1 | 0 | I10 | PERIPH_EN — Peripheral enable GPIO (3.3V direct) |
| 1 | 0 | 1 | 1 | I11 | BOARD_TEMP — NTC thermistor (10k/10k divider) |
| 1 | 1 | 0 | 0 | I12 | HEATER1 — CJ125 heater bank 1 (divider 11:1) |
| 1 | 1 | 0 | 1 | I13 | HEATER2 — CJ125 heater bank 2 (divider 11:1) |
| 1 | 1 | 1 | 0 | I14 | BOARD_CUR — Board current (0.1R shunt, gain 20) |
| 1 | 1 | 1 | 1 | I15 | SPARE — User-defined |

### Diagnostics Hardware BOM

| Part | LCSC | Qty | Function |
|------|------|-----|----------|
| CD74HC4067SM96 (SSOP-24) | C98457 | 1 | 16:1 analog mux @ 5V — routes test points to ADC |
| ADS1115IDGST (VSSOP-10) | C468683 | 1 | 16-bit I2C ADC @ 0x4A, VDD=5V — reads mux output on AIN0 |
| PCA9306DCUR (VSSOP-8) | C33196 | 1 | I2C level shifter 3.3V ↔ 5V (shared by all 3 ADS1115) |
| REF5050AIDR (SOIC-8) | C27804 | 2 | Precision 5.000V reference — #1: MCP3204 VREF, #2: ADS1115 VDD |
| AMS1117-5.0 (SOT-223) | C6187 | 1 | 5V LDO — mux VCC + PCA9306 VREF2, fed from 6.8V buck (47µF tantalum output) |
| BLM21PG221SN1D ferrite bead (0805) | C85840 | 1 | AGND-GND isolation (220Ω@100MHz, 45mΩ DCR, lossy broadband filter) |
| 22µF ceramic 0805 | C907991 | 1 | AGND bulk decoupling (parallel with 100nF, transient filtering) |
| 47k ohm 0402 | — | 4 | Voltage divider high-side (12V/coil/inj/fuelpump test points) |
| 15k ohm 0402 | — | 3 | Voltage divider high-side (5V/VCCB/RESET test points) |
| 10k ohm 0402 | — | 8 | Voltage divider low-side and NTC pullup |
| 10k NTC 0402 | — | 1 | Board temperature sensor (beta 3950, R0=10k @ 25C) |
| 0.1 ohm 2512 | C49164916 | 1 | Board current shunt resistor (R4) |
| INA180A1IDBVR (SOT-23-5) | C122228 | 1 | Current sense amplifier (gain 20V/V) for 0.1Ω shunt, 0-2A range |
| 100nF 0402 | — | 2 | Bypass caps: VCC on CD74HC4067 and ADS1115 |
| 10uF 0402 | — | 1 | Bulk bypass for ADS1115 |

### Diagnostics Architecture

**Idle-time scavenging:** Diagnostics only execute when the ECU's 10ms update cycle has budget remaining (elapsed < 9.5ms). Each call advances one phase of a non-blocking state machine — a full 16-channel scan takes ~320ms without blocking any single cycle.

**State machine phases (per channel):**

1. SELECT_MUX — Set S0-S3 via xDigitalWrite on MCP23S17 #4 P8-P11, enable mux via P12
2. START_CONV — Start ADS1115 single-shot conversion on AIN0
3. WAIT_CONV — Check ALERT/RDY via SPI pin read (~20µs) or I2C poll fallback (~200µs), timeout 15ms
4. READ_RESULT — Read millivolts, apply scale factor, evaluate fault
5. IDLE — Advance to next channel

**Fault detection:** 3 consecutive out-of-range readings trigger burst investigation (8 rapid samples). Burst analysis computes mean/stddev to classify as hard fault, intermittent, or transient. Cross-validation correlates power rail readings (3.3V vs 5V vs 12V).

**Health score:** 100 - (faults x 15 + warnings x 5). Published via REST (`/diag`), MQTT (`ecu/diag`), and web UI (`/diag/view`).

### I2C Bus (with Diagnostics)

All ADS1115s operate at VDD=5V from a dedicated REF5050AIDR precision reference (#2), with I2C level-shifted via PCA9306DCUR (VREF1=3.3V, VREF2=5V from AMS1117-5.0). The MCP3204 uses a separate REF5050 (#1) on its VREF pin for 12-bit full-scale accuracy. Dedicated AGND plane with ferrite bead isolation from digital GND.

| Device | Address | ALERT/RDY | Description |
|--------|---------|-----------|-------------|
| PCA9306DCUR | — | — | I2C level shifter 3.3V ↔ 5V (LCSC C33196) |
| ADS1115 #0 | 0x48 | Pin 204 (MCP#0 P4) | CJ125_UR (CH0/1), TFT temp (CH2), MLPS (CH3) |
| ADS1115 #1 | 0x49 | Pin 205 (MCP#0 P5) | MAP (CH0), TPS (CH1) — frees GPIO 5/6 for OSS/TSS |
| ADS1115 #2 | 0x4A | Pin 203 (MCP#0 P3) | Diagnostics mux output (AIN0) — dedicated, no contention |

## Safe Mode

The ECU includes boot loop detection and per-peripheral enable/disable to recover from hardware faults without reflashing.

**Boot loop detection:** An `RTC_NOINIT_ATTR` counter persists across soft resets, watchdog timeouts, and panic reboots (reset on power-on). If the counter exceeds 3, the ECU enters safe mode automatically. A 30-second stability timer resets the counter after a successful boot.

**Safe mode behavior:** Skips `ECU::configure()`, `ECU::begin()`, tune table loading, MQTT, and state publishing. Retains WiFi, web server, FTP, logger, config save, and CPU load monitoring -- allowing remote configuration changes to fix the problem.

**Peripheral control:** Eight individual enable flags (persisted to `peripherals.*` in config JSON) allow disabling specific I2C or SPI devices:

| Flag | Controls | Default |
|------|----------|---------|
| `i2cEnabled` | I2C bus (ADS1115 ADCs) | true |
| `spiExpandersEnabled` | HSPI bus (all 6 MCP23S17) | true |
| `expander0Enabled` - `expander5Enabled` | Individual MCP23S17 #0-#5 | true |

**Web interface:** The config page has a "Peripherals & Safe Mode" fieldset with checkboxes (master bus disable greys out children). A red banner appears on the dashboard in safe mode with boot count, reset reason, and an "Exit Safe Mode" button. `POST /safemode/clear` clears the flag and reboots. `proj.forceSafeMode` is a one-shot flag to enter safe mode on next reboot.

## Limp Mode

Limp mode protects the engine when critical sensors fail, I/O expanders go offline, or oil pressure drops. When active, it reduces the rev limit (default 3000 RPM), caps ignition advance (default 10 deg), locks the current transmission gear, unlocks TCC, and turns on the check engine light.

**Fault sources** (bitmask in `limpFaults`):

| Bit | Constant | Trigger |
|-----|----------|---------|
| 0x01 | `FAULT_MAP` | MAP reading outside configurable min/max range |
| 0x02 | `FAULT_TPS` | TPS reading outside configurable min/max range |
| 0x04 | `FAULT_CLT` | Coolant temp exceeds configurable max |
| 0x08 | `FAULT_IAT` | Intake air temp exceeds configurable max |
| 0x10 | `FAULT_VBAT` | Battery voltage below configurable min |
| 0x20 | `FAULT_EXPANDER` | Any initialized I2C/SPI expander fails health check |
| 0x40 | `FAULT_OIL` | Low oil pressure while engine running |

**Recovery:** All faults must clear for a configurable recovery delay (default 5 seconds) before limp mode exits. This prevents rapid cycling from intermittent faults.

**Oil pressure sensor:** Configurable as disabled (default), digital switch, or analog sender:

- **Digital mode:** GPIO pin with internal pull-up. Configurable polarity (`oilPressureActiveLow`, default true). LOW = low pressure fault.
- **Analog mode:** Reads voltage from MCP3204 SPI ADC (priority) or native GPIO (fallback). Linear conversion: 0.5V = 0 PSI, 4.5V = max PSI (configurable, default 100). Faults when PSI drops below threshold (default 10 PSI).
- **Engine-running guard:** Oil pressure is only checked when RPM >= 400, with a configurable startup delay (default 3 seconds) to allow pressure to build after engine start.

**MQTT:** Limp state and fault bitmask published in `ecu/state`. Fault enter/exit events published to `ecu/fault`.

**Dashboard:** Red limp mode banner with per-fault pills (MAP, TPS, CLT, IAT, VBAT, EXP, OIL). Oil pressure card shows live PSI value and OK/LOW status (auto-hidden when oil pressure is disabled).

**Config page:** "Limp Mode" fieldset with rev limit, advance cap, recovery delay, and 7 sensor fault thresholds. "Oil Pressure" fieldset with mode dropdown and mode-dependent field visibility. All sensor thresholds are live (no reboot). Oil pressure mode and pin changes require reboot.

## Screenshots

| | |
|---|---|
| ![Home](screenshots/index.png) | ![Dashboard](screenshots/dashboard.png) |
| Home | Dashboard |
| ![Tune](screenshots/tune.png) | ![Config](screenshots/config.png) |
| Tune Tables | Configuration |
| ![Pins](screenshots/pins.png) | ![Update](screenshots/update.png) |
| Pin Map | OTA Update |
| ![Log](screenshots/log-view.png) | ![Heap](screenshots/heap-view.png) |
| Log Viewer | System / Heap |
| ![WiFi](screenshots/wifi-view.png) | |
| WiFi Setup | |

## Web Pages

All pages served from SD card `/www/` directory.

| Page | Purpose |
|------|---------|
| `/` | Landing page with nav cards |
| `/dashboard` | Live ECU gauges and status |
| `/tune` | 16x16 table editor with live cursor |
| `/pins` | GPIO pin map with live state |
| `/config` | WiFi/MQTT/engine/alternator/sensor settings |
| `/update` | OTA firmware upload |
| `/log/view` | Log viewer |
| `/heap/view` | Memory/CPU monitor |
| `/wifi/view` | WiFi scan and test |
| `/diag/view` | Board diagnostics health and channels |
| `/admin/setup` | Initial password setup |

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or IDE extension)
- USB cable connected to ESP32-S3 board

### Secrets Setup

Create `secrets.ini` in the project root (gitignored):

```ini
[secrets]
build_flags =
	-D AP_PASSWORD=\"your-ap-password\"
	-D XOR_KEY=\"your-random-base64-key\"
```

### Build and Upload

```bash
# Build
pio run -e freenove_esp32_s3_wroom

# Upload firmware
pio run -t upload -e freenove_esp32_s3_wroom

# Serial monitor
pio run -t monitor -e freenove_esp32_s3_wroom
```

## Dependencies

Managed automatically by PlatformIO (`lib_deps` in `platformio.ini`).

| Library | Author | Purpose |
|---------|--------|---------|
| [TaskScheduler](https://github.com/arkhipenko/TaskScheduler) | Anatoli Arkhipenko | Cooperative multitasking on Core 0 |
| [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | ESP32Async | Async HTTP and WebSocket server |
| [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) | ESP32Async | TCP transport for async web server |
| [AsyncMqttClient](https://github.com/marvinroger/async-mqtt-client) | Marvin Roger | MQTT client with auto-reconnect |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | Benoit Blanchon | JSON parsing and serialization |
| [CircularBuffer](https://github.com/rlogiacco/CircularBuffer) | Roberto Lo Giacco | Lock-free circular buffer for ISR queues |
| [ESP32-targz](https://github.com/tobozo/ESP32-targz) | tobozo | tar.gz compression for log rotation |
| [SimpleFTPServer](https://github.com/xreef/SimpleFTPServer) | Renzo Mischianti | FTP server for SD card file uploads |
| [Adafruit MCP23017](https://github.com/adafruit/Adafruit-MCP23017-Arduino-Library) | Adafruit | I2C GPIO expander driver |
| [Adafruit ADS1X15](https://github.com/adafruit/Adafruit_ADS1X15) | Adafruit | 16-bit I2C ADC for CJ125 Nernst cell temp |
| [StringStream](https://github.com/0xtj/StringStream) | 0xtj | String-based stream wrapper |

## Acknowledgments

This project builds on the work of many open-source projects and their authors. Thank you to everyone who made their code freely available.

### CJ125 Wideband O2

- **[Lambda Shield](https://github.com/Bylund/Lambda-Shield-Example)** by [Bylund](https://github.com/Bylund) -- The CJ125 SPI register constants, PID tuning values, and heater control state machine are derived from this project. Lambda Shield provided the foundational reference for interfacing with the Bosch CJ125 wideband controller chip
- **Bosch CJ125 and LSU 4.9 datasheets** -- The Ip-to-lambda characteristic curve lookup table is derived from the official Bosch LSU 4.9 sensor datasheet

### Core Libraries

- **[TaskScheduler](https://github.com/arkhipenko/TaskScheduler)** by [Anatoli Arkhipenko](https://github.com/arkhipenko) -- Cooperative multitasking framework that manages all periodic tasks on Core 0
- **[ArduinoJson](https://github.com/bblanchon/ArduinoJson)** by [Benoit Blanchon](https://github.com/bblanchon) -- JSON engine used for all configuration, API responses, and MQTT payloads
- **[ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer)** and **[AsyncTCP](https://github.com/ESP32Async/AsyncTCP)** by [ESP32Async](https://github.com/ESP32Async) -- Non-blocking web server and WebSocket support
- **[AsyncMqttClient](https://github.com/marvinroger/async-mqtt-client)** by [Marvin Roger](https://github.com/marvinroger) -- MQTT client for telemetry publishing

### Hardware Drivers

- **[Adafruit MCP23017](https://github.com/adafruit/Adafruit-MCP23017-Arduino-Library)** and **[Adafruit ADS1X15](https://github.com/adafruit/Adafruit_ADS1X15)** by [Adafruit](https://github.com/adafruit) -- I2C GPIO expander and 16-bit ADC drivers. Adafruit's open-source hardware libraries and documentation are invaluable for embedded projects
- **[CircularBuffer](https://github.com/rlogiacco/CircularBuffer)** by [Roberto Lo Giacco](https://github.com/rlogiacco) -- ISR-safe circular buffer used for interrupt-driven input queues

### Infrastructure

- **[ESP32-targz](https://github.com/tobozo/ESP32-targz)** by [tobozo](https://github.com/tobozo) -- tar.gz compression for log file rotation on SD card
- **[SimpleFTPServer](https://github.com/xreef/SimpleFTPServer)** by [Renzo Mischianti](https://github.com/xreef) -- FTP server for uploading web pages and config to the SD card
- **[Arduino-ESP32](https://github.com/espressif/arduino-esp32)** by [Espressif](https://github.com/espressif) -- The Arduino framework for ESP32 that makes all of this possible
- **[PlatformIO](https://platformio.org/)** -- Build system and dependency management

## License

This project is provided as-is for educational and personal use.
