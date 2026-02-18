# Wideband O2 Sensor Analysis — Modular Lambda Controller

**Date:** 2026-02-18
**Context:** Wideband O2 sensing for ESP-ECU — current on-board CJ125 implementation vs modular external approach
**References:** Bosch CJ125 (datasheet), Bosch CJ135 (successor), Bosch LSU 4.9 / LSU ADV, rusEFI wideband controller, Spartan 3 (14point7), AEM 4-channel UEGO, Lambda Shield (Arduino)

---

## 1. Purpose

Evaluate whether wideband O2 sensing should remain integrated on the ESP-ECU main board (current CJ125 dual-bank implementation) or move to dedicated external modules. Assess the tradeoffs, technology options, and a generational roadmap from simple commercial units to custom per-cylinder modules.

**Design philosophy:** Same as VR conditioner and driver modules — start simple (commercial off-the-shelf), test, iterate to purpose-built modules as the platform matures. The ESP-ECU sees a lambda value regardless of which generation of controller produces it.

### 1.1 Why This Matters

Wideband O2 is the single most important feedback sensor for fuel control:
- **Closed-loop AFR correction**: Real-time lambda feedback drives the VE table learning and trim
- **Per-bank balance**: Dual-bank sensing detects bank-to-bank fuel imbalance
- **Per-cylinder sensing** (future): Detect individual cylinder fuel delivery problems, optimize per-cylinder trim
- **Diagnostics**: Catalyst monitoring, misfire detection (exhaust O2 oscillation), fuel system fault detection

### 1.2 Current Implementation — On-Board CJ125 Dual-Bank

The ESP-ECU currently integrates two Bosch CJ125 ASICs driving two LSU 4.9 wideband sensors (one per bank on a V8). This is functional but consumes significant ESP32 resources:

**Resource footprint (per bank × 2 banks):**

| Resource | Per Bank | Total (2 banks) | Notes |
|----------|----------|-----------------|-------|
| Native GPIO (ADC) | 1 (UA input) | 2 (GPIO3, GPIO4) | 12-bit ADC, 2:3 voltage divider |
| Native GPIO (PWM) | 1 (heater output) | 2 (GPIO19, GPIO20) | LEDC channels 1-2, 100Hz 8-bit PWM |
| Expander pins (MCP23017) | 1 (SPI CS) | 2 (P8, P9) | SPI chip select via I2C expander |
| SPI bus | Shared VSPI | Shared | 125kHz SPI_MODE1, shared with SD card |
| I2C bus | ADS1115 (UR temp) | 1 × ADS1115 @ 0x48 | CH0 = Bank 1 UR, CH1 = Bank 2 UR |
| LEDC channels | 1 | 2 | From a total of 8 available |
| Code complexity | ~300 lines | ~300 lines (shared) | CJ125Controller.cpp + ADS1115Reader |
| Flash usage | ~8KB | ~8KB | CJ125 + ADS1115 + lambda lookup table |
| CPU time | ~1ms per 100ms | ~2ms per 100ms | 10Hz update rate (decimated from 100Hz ECU loop) |

**Total ESP32 resources consumed:** 4 native GPIO + 2 expander pins + 2 LEDC channels + shared SPI + shared I2C = 6 dedicated pins + 2 shared buses.

**Current state machine (CJ125Controller.cpp):**
```
IDLE → WAIT_POWER (battery >11V) → CALIBRATING (SPI calibrate + read refs)
  → CONDENSATION (2V heater, 5s) → RAMP_UP (8.5V→13V at 0.4V/s) → PID (heater regulation, readings valid)
  → ERROR (diagnostic failure, heater off)
```

**PID heater control:** P=120, I=0.8, D=10, integral clamped ±250. Targets calibrated UR reference (Nernst cell resistance ~ 300Ω at 780°C operating temperature).

**Lambda lookup:** 23-point piecewise-linear interpolation from Bosch LSU 4.9 Ip characteristic curve. UA(V) = 1.5 + Ip(mA) × 0.4952.

---

## 2. The Case for Modularization

### 2.1 Problems with On-Board CJ125

| Issue | Impact | Severity |
|-------|--------|----------|
| **CJ125 sourcing** | Bosch CJ125 is end-of-life, increasingly hard to source. SOIC-20 package, single-source (Bosch). Counterfeit risk on marketplace purchases. | High |
| **Resource consumption** | 6 dedicated pins + 2 LEDC channels + 2 shared buses for just 2 lambda readings | Medium |
| **Heater power routing** | 2A heater current routed through ESP-ECU board traces, BTS3134 high-side driver on main PCB | Medium |
| **Sensor wiring** | 6-wire LSU 4.9 connector must route to main ECU board — long harness for exhaust-mounted sensors | Medium |
| **Scalability** | Adding per-cylinder sensing (4-8 more sensors) is impossible on the main board | High |
| **Failure isolation** | A CJ125 fault (SPI error, heater short) affects the main ECU. Diagnostic error state must be handled in real-time engine code | High |
| **Mixed analog/digital** | Sensitive Ip current measurement shares PCB with noisy switching (injectors, coils, WiFi) | Medium |
| **5V I/O standard** | CJ125 operates at 5V, requires voltage dividers and level shifting on the ESP32 side — adds component count | Low |

### 2.2 Benefits of External Modules

| Benefit | Detail |
|---------|--------|
| **Sensor proximity** | Module mounts near exhaust manifold, short sensor wires (noise reduction, better signal integrity) |
| **Resource recovery** | Frees 4 GPIO + 2 expander pins + 2 LEDC channels + SPI/I2C bandwidth on ESP-ECU |
| **Technology independence** | Module can use CJ125, CJ135, discrete op-amps, or any future ASIC — ESP-ECU doesn't care |
| **Failure isolation** | Module fault doesn't crash the ECU. CAN bus timeout triggers graceful degradation (last-known-good lambda, or open-loop) |
| **Scalability** | Add modules per cylinder, per bank, or per exhaust — just more CAN nodes |
| **Hot-swap** | Replace a faulty wideband module without touching the ECU |
| **Mixed-signal isolation** | Dedicated PCB optimized for analog precision, away from digital noise |
| **Heater power** | 2A heater current stays on the module, powered locally from 12V |

### 2.3 CAN Bus — The Right Interface

**Why CAN, not Modbus RTU (RS-485)?**

The driver modules use Modbus RTU because they receive timing-critical control signals via direct wires — Modbus only carries diagnostic data at low priority. Wideband modules are different: they produce the primary control feedback (lambda value), so communication latency matters more.

| Parameter | Modbus RTU (RS-485) | CAN 2.0B |
|-----------|--------------------:|--------:|
| Bit rate | 9600-115200 baud | 500kbps-1Mbps |
| Latency (8 registers) | 15-50ms | 0.5-2ms |
| Multi-master | No (polling) | Yes (arbitration) |
| Error detection | CRC-16 | CRC-15 + bit stuffing + ACK |
| Node count | 32 (RS-485 standard) | 112+ (CAN standard) |
| Wiring | Twisted pair + GND | Twisted pair (2-wire) |
| ESP32-S3 support | UART (software protocol) | TWAI peripheral (hardware CAN) |
| Determinism | Polling dependent | Priority-based, deterministic |

**Latency analysis:** Exhaust gas transport delay from cylinder to sensor is 20-50ms at typical RPM. CAN latency of 1-2ms is negligible compared to this transport delay. Even Modbus at 15ms would technically work, but CAN's multi-master capability and hardware support on the ESP32-S3 make it the clear winner.

**ESP32-S3 TWAI (CAN) peripheral:**
- Built-in CAN 2.0B controller — no external CAN controller IC needed
- TX: any GPIO (assign via GPIO matrix)
- RX: any GPIO (assign via GPIO matrix)
- Only needs external CAN transceiver (MCP2551 or SN65HVD230, ~$0.50)
- 2 GPIO total from ESP32-S3 for an entire CAN bus with unlimited modules

---

## 3. Technology Survey

### 3.1 Bosch CJ125 (Current)

The CJ125 is a mixed-signal ASIC purpose-built for driving Bosch LSU 4.x wideband sensors.

**Architecture:**
```
                  CJ125 (SOIC-20)
         ┌──────────────────────────────┐
5V VCC ──┤                              ├── IA (pump current source)
GND ─────┤  Ip pump cell current ◀──────┤── IP (pump current return)
         │  source + virtual ground     │
SPI CLK ─┤                              ├── UA (Ip measurement output, 0-5V)
SPI DI ──┤  SPI interface (125kHz)      │   UA = 1.5V + Ip(mA) × 0.4952
SPI DO ──┤  Calibrate / Normal / Diag   │
SPI CS ──┤                              ├── UR (Nernst cell temp, ~300mV target)
         │  Nernst cell reference ──────┤── RE (Nernst reference electrode)
         │                              │
         │  Diagnostics: 16-bit status  │
         └──────────────────────────────┘
```

**Strengths:**
- Proven Bosch silicon, used in millions of OEM ECUs
- Handles entire Ip pump cell regulation internally
- Simple SPI interface — just calibrate, switch to normal mode, read UA/UR
- Compact (SOIC-20)

**Weaknesses:**
- End-of-life / difficult to source (lead times 26+ weeks, single-source Bosch)
- Counterfeit risk on marketplace purchases
- Fixed gain configuration (only two modes: V=8, V=17)
- No integrated ADC — requires external ADC for UA and UR readings
- No integrated heater driver — external MOSFET/BTS required
- 5V only, no 3.3V variant

**Status (2026):** Still technically in production but being superseded by CJ135. Stock availability is sporadic. Not recommended for new designs.

### 3.2 Bosch CJ135 (Successor)

The CJ135 is Bosch's next-generation wideband controller ASIC, designed for LSU 4.9 and LSU ADV sensors.

**Key improvements over CJ125:**
- Integrated 12-bit ADC — no external ADC needed for UA/UR
- Integrated heater driver — no external BTS3134/MOSFET
- Digital output (SPI) for both lambda and Nernst temperature — no analog UA/UR pins
- Supports LSU ADV sensor (next-gen, higher resolution)
- Lower component count external BOM
- 3.3V and 5V compatible SPI

**Weaknesses:**
- Limited public documentation (Bosch restricts to OEM customers)
- Not available through standard distribution (Bosch direct only)
- Evaluation boards are expensive ($200+)
- Still single-source Bosch

**Status (2026):** In production for OEM use. Difficult for hobbyist/small-volume projects to obtain. Not a practical option for ESP-ECU unless purchasing through Bosch OEM channels.

### 3.3 Discrete Op-Amp Approach (rusEFI)

The rusEFI open-source ECU project has proven a fully discrete wideband controller using commodity components — no Bosch ASIC required.

**Architecture (rusEFI wideband v0.3):**
```
         STM32F042 (TSSOP-20)
         ┌──────────────────────┐
         │  Ip pump control     │◀── Op-amp #1: Ip measurement (transimpedance)
CAN TX ──┤  Nernst monitoring   │◀── Op-amp #2: Nernst voltage buffer
CAN RX ──┤  Heater PID          │──▶ MOSFET: Heater driver (2A PWM)
         │  CAN 2.0B output     │
         │  Diagnostic LED      │
         │  12-bit ADC (built-in)│
         └──────────────────────┘
```

**BOM (~$15-20 at volume):**
- STM32F042 (CAN-capable Cortex-M0, ~$2)
- 2× precision op-amps (OPA2340 or similar, rail-to-rail, ~$2)
- 1× N-channel MOSFET for heater (IRLZ44N or similar, ~$1)
- 1× CAN transceiver (MCP2551, ~$0.50)
- Precision resistors for Ip sense and Nernst reference (~$1)
- Passives, connector, PCB (~$8-10)

**Strengths:**
- All commodity components, multi-source, always in stock
- CAN bus output — direct integration with modern ECUs
- Open-source firmware and hardware (GitHub: rusEFI/wideband)
- Proven in hundreds of running vehicles
- Per-module cost competitive with CJ125 solution
- STM32F042 has built-in 12-bit ADC and CAN controller — minimal external parts

**Weaknesses:**
- More analog design effort (op-amp gain stages, virtual ground, Ip regulation)
- PCB layout is critical for analog precision (Kelvin sense, guard traces)
- More components than CJ125 solution (but cheaper and available)
- Requires firmware development (rusEFI provides open-source reference)

**Performance:**
- Lambda accuracy: ±0.5% (comparable to CJ125)
- Update rate: 50-100Hz over CAN (5× faster than current CJ125 implementation)
- Heater control: PID, same quality as CJ125 approach
- Warm-up time: Same as CJ125 (~15-25 seconds, sensor dependent)

### 3.4 Commercial Standalone Controllers

Several commercial wideband controllers output analog (0-5V) or CAN signals that can be directly consumed by the ESP-ECU.

| Product | Output | Sensor | Price | Notes |
|---------|--------|--------|-------|-------|
| **Spartan 3** (14point7) | 0-5V analog + CAN | LSU 4.9 | ~$100 | Open-source firmware, proven, CAN optional add-on |
| **AEM X-Series** | 0-5V analog + CAN | LSU 4.9 | ~$180 | Professional grade, wide adoption, CAN native |
| **Innovate MTX-L Plus** | 0-5V analog | LSU 4.9 | ~$200 | Analog output only, no CAN |
| **AEM 4-Channel UEGO** | CAN (4 channels) | LSU 4.9 × 4 | ~$900 | Per-cylinder capable, CAN bus, pro racing |
| **ECUMaster Lambda-to-CAN** | CAN | LSU 4.9 | ~$150 | Purpose-built CAN wideband, compact |

**For Gen 1 approach:** A Spartan 3 with CAN output is the simplest path to modular wideband. Wire 12V + GND + sensor connector, connect CAN bus to ESP-ECU. Total integration: 2 GPIO (CAN TX/RX) + 1 transceiver chip. Done.

### 3.5 LSU 4.9 vs LSU ADV (Next-Gen Sensor)

| Parameter | LSU 4.9 | LSU ADV |
|-----------|---------|---------|
| Ip resolution | 10µA | 0.5µA (20× improvement) |
| Lambda accuracy | ±1% | ±0.3% |
| Response time (T63) | 150ms | <100ms |
| Operating temp | 780°C ±10°C | 780°C ±5°C |
| Wire count | 6 | 5 (integrated calibration resistor) |
| Controller required | CJ125 / CJ135 / discrete | CJ135 / discrete (different cal) |
| Availability | Widely available | Available, ramping volume |
| Price | ~$50-70 | ~$80-100 |

**LSU ADV is the future** for precision per-cylinder sensing. The 20× improvement in Ip resolution enables meaningful per-cylinder trim at steady-state conditions. However, LSU 4.9 is perfectly adequate for dual-bank closed-loop and is the sensor every commercial controller supports today.

**Recommendation:** Start with LSU 4.9 (proven, cheap, universal support). Design the module interface to be sensor-agnostic — the CAN message carries a lambda value, not raw sensor data. When LSU ADV controllers mature, swap the module without changing the ECU.

---

## 4. Module Design — Generational Roadmap

### 4.1 Gen 1 — Commercial Standalone (CAN Output)

**Goal:** Get modular wideband working with zero custom hardware. Validate the CAN bus integration path.

**Hardware:** Spartan 3 + CAN adapter, AEM X-Series, or ECUMaster Lambda-to-CAN.

**ESP-ECU integration:**
```
Commercial wideband        CAN bus             ESP-ECU
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│ Spartan 3    │     │   2-wire     │     │ ESP32-S3     │
│ + CAN add-on ├─────┤ CAN_H/CAN_L ├─────┤ TWAI (GPIO)  │
│              │     │   twisted    │     │ + SN65HVD230 │
│ LSU 4.9 ◀───┤     │   pair       │     │              │
└──────────────┘     └──────────────┘     └──────────────┘
```

**ESP-ECU changes:**
- Add CAN transceiver (SN65HVD230 or MCP2551) — 1 chip, 2 GPIO
- Initialize ESP32-S3 TWAI peripheral
- Parse incoming CAN frames for lambda value
- Feed lambda to SensorManager as if it came from CJ125

**Cost:** ~$100-180 per channel (commercial controller) + ~$2 for CAN transceiver on ECU
**Effort:** Minimal firmware (CAN RX + frame parsing), no custom hardware
**Timeline:** Days to integrate

### 4.2 Gen 2 — Custom Single-Channel Module (CAN Output)

**Goal:** Purpose-built wideband module with CAN output. Lower per-unit cost, better integration, custom diagnostics.

**Architecture:**
```
         Custom Wideband Module
┌────────────────────────────────────────┐
│                                        │
│  12V in ──▶ LDO ──▶ 3.3V (MCU)       │
│            └──▶ 5V (op-amps)           │
│                                        │
│  LSU 4.9 ──▶ Op-amp Ip sense ──▶ ADC ──▶ STM32F042   │
│             Op-amp Nernst buf ──▶ ADC     │    │       │
│                                           │    ▼       │
│             Heater MOSFET ◀── PWM ◀───────┘  CAN TX ──┤──▶ CAN bus
│                                              CAN RX ──┤──▶ to ESP-ECU
│             Status LED                                 │
│             DIP switch (CAN ID)                        │
│                                                        │
│  Connector: 12V, GND, CAN_H, CAN_L, Sensor (6-pin)   │
└────────────────────────────────────────────────────────┘
```

**MCU: STM32F042K6T6 (LQFP-32)**
- ARM Cortex-M0, 48MHz, 32KB Flash, 6KB RAM
- Built-in CAN 2.0B controller (no external CAN controller needed)
- 12-bit ADC with 10 channels
- Hardware timers for heater PWM
- UART bootloader for firmware update
- ~$2 in volume

**Analog front-end (based on rusEFI wideband):**
- **Ip measurement:** Transimpedance amplifier converts pump cell current to voltage. OPA2340 (rail-to-rail, 5V) with precision feedback resistor. Full-scale ±2mA → 0-5V output.
- **Nernst voltage buffer:** Unity-gain buffer on Nernst cell voltage (0-1.2V). High-impedance input prevents loading the cell.
- **Virtual ground:** Precision 450mV reference for Nernst cell electrode. TL431 or resistive divider from 5V rail.
- **Heater driver:** N-channel MOSFET (IRLZ44N), PWM at 100Hz, flyback diode, current-limited to 2.5A max.

**CAN protocol:**

| CAN ID | Byte 0-1 | Byte 2-3 | Byte 4-5 | Byte 6 | Byte 7 |
|--------|----------|----------|----------|--------|--------|
| 0x600 + node | Lambda × 10000 | AFR × 1000 | Nernst temp (°C) | Heater state | Health % |

Update rate: 50Hz (20ms period) — 5× faster than current CJ125 implementation.

**BOM estimate (single channel):**

| Item | Qty | Part | Cost |
|------|-----|------|------|
| MCU | 1 | STM32F042K6T6 | $2.00 |
| CAN transceiver | 1 | MCP2551 | $0.50 |
| Op-amps | 1 | OPA2340 (dual) | $2.00 |
| Voltage reference | 1 | TL431 | $0.30 |
| LDO 3.3V | 1 | AMS1117-3.3 | $0.20 |
| LDO 5.0V | 1 | TPS7A4701 | $1.50 |
| Heater MOSFET | 1 | IRLZ44N (TO-220) | $1.00 |
| Precision resistors | 6 | 0.1% thin-film | $1.50 |
| Passives + connector | - | Misc | $3.00 |
| PCB (2-layer FR4) | 1 | ~30×50mm | $2.00 |
| **Total** | | | **~$14** |

**vs. on-board CJ125 approach:** CJ125 ($8-15 when available) + BTS3134 ($3) + ADS1115 ($5) + voltage dividers + level shifters ≈ $20-25. The discrete approach is **cheaper and more available**.

### 4.3 Gen 3 — Per-Cylinder Multi-Channel Module (CAN Output)

**Goal:** One wideband sensor per cylinder for individual cylinder fuel trim. Racing-grade capability.

**Why per-cylinder:**
- Detect individual cylinder fuel delivery problems (clogged injector, leaking valve)
- Per-cylinder VE trim — each cylinder gets its own fuel correction
- Post-catalyst individual cylinder fuel cut identification
- Required for advanced emissions strategies
- Already done in professional racing (AEM 4-channel UEGO, Motec C1xx)

**Architecture: 4-channel module (one per bank)**
```
         4-Channel Wideband Module
┌──────────────────────────────────────────┐
│                                          │
│  12V in ──▶ Regulators (3.3V, 5V)       │
│                                          │
│  LSU 4.9 #1 ──▶ [Op-amp + ADC ch] ──┐   │
│  LSU 4.9 #2 ──▶ [Op-amp + ADC ch] ──┤   │
│  LSU 4.9 #3 ──▶ [Op-amp + ADC ch] ──┼──▶ STM32F303 ──▶ CAN bus
│  LSU 4.9 #4 ──▶ [Op-amp + ADC ch] ──┘   │
│                                          │
│  4× Heater MOSFETs ◀── 4× PWM           │
│                                          │
│  DIP switch (CAN base ID)                │
│  Status LEDs (×4 per channel)            │
└──────────────────────────────────────────┘
```

**MCU: STM32F303CCT6 (LQFP-48)**
- ARM Cortex-M4F, 72MHz, 256KB Flash, 40KB RAM
- 4× independent 12-bit ADCs (one per channel — simultaneous sampling)
- Built-in CAN 2.0B
- 4× advanced timers for heater PWM
- Op-amp and comparator peripherals built-in (can reduce external analog parts)
- ~$4 in volume

**Key design challenge:** 4 independent heater circuits, each drawing up to 2A = 8A total from 12V. Requires robust power input, possibly MCPCB substrate (same as driver module).

**CAN protocol (4 channels per frame):**

| CAN ID | Bytes 0-1 | Bytes 2-3 | Bytes 4-5 | Bytes 6-7 |
|--------|-----------|-----------|-----------|-----------|
| 0x600 + node | Lambda CH1 × 10000 | Lambda CH2 × 10000 | Lambda CH3 × 10000 | Lambda CH4 × 10000 |
| 0x610 + node | AFR CH1 × 1000 | AFR CH2 × 1000 | AFR CH3 × 1000 | AFR CH4 × 1000 |
| 0x620 + node | Health/heater state packed | Nernst temps | Fault flags | Reserved |

Update rate: 50Hz per channel (staggered CAN frames, total bus utilization <5% at 500kbps).

**BOM estimate (4-channel):**

| Item | Cost |
|------|------|
| STM32F303 | $4.00 |
| 4× OPA2340 (8 op-amp channels) | $8.00 |
| 4× IRLZ44N heater MOSFETs | $4.00 |
| CAN transceiver | $0.50 |
| Regulators, references | $3.00 |
| Precision resistors | $4.00 |
| Passives, connectors, PCB | $10.00 |
| **Total** | **~$34** |

Per-channel cost: ~$8.50 — significantly cheaper than any commercial per-cylinder solution ($225/channel for AEM 4-channel UEGO).

**V8 configuration:** 2× 4-channel modules = 8 wideband sensors, 8 CAN frames at 50Hz, 2 GPIO on ESP-ECU.

---

## 5. ESP-ECU Integration

### 5.1 CAN Bus Hardware

The ESP32-S3 has a built-in TWAI (Two-Wire Automotive Interface) controller — Espressif's name for CAN 2.0B.

**Required hardware on ESP-ECU board:**
- CAN transceiver: SN65HVD230 (3.3V, low-power, common-mode range ±12V) — SOIC-8, ~$0.50
- 2 GPIO: TX and RX (any GPIO via ESP32-S3 GPIO matrix)
- 120Ω termination resistor (at ECU end of bus)
- TVS protection on CAN_H / CAN_L (PESD1CAN, ~$0.30)

**Suggested pin assignment:**
| Signal | GPIO | Notes |
|--------|------|-------|
| CAN_TX | GPIO43 | Via SN65HVD230 TXD |
| CAN_RX | GPIO44 | Via SN65HVD230 RXD |

These are UART0 default pins (TX0/RX0) which are typically only used for boot log output. If UART0 is needed, any other free GPIO pair works.

**Level shifting:** The SN65HVD230 operates at 3.3V and interfaces directly with the ESP32-S3 GPIO — no level shifting needed for CAN. The transceiver handles the 5V differential CAN bus to 3.3V logic conversion internally.

### 5.2 Software Architecture

```
WidebandCAN (new)              SensorManager (existing)
┌─────────────────────┐       ┌──────────────────────────┐
│ TWAI RX interrupt   │       │                          │
│ Parse CAN frames    │──────▶│ getLambda(bank)           │
│ Per-node lambda     │       │  - If CAN wideband: use  │
│ Per-node health     │       │    WidebandCAN values     │
│ Timeout detection   │       │  - If CJ125: use CJ125   │
│ Fault reporting     │       │  - Else: narrowband fallback │
└─────────────────────┘       └──────────────────────────┘
```

**WidebandCAN manager:**
- Initializes TWAI peripheral at 500kbps
- Registers RX filter for wideband CAN IDs (0x600-0x63F)
- On each frame: parse lambda, AFR, health, heater state
- Per-node timeout: if no frame received for 500ms, mark node offline
- Provides `getLambda(node)`, `getAfr(node)`, `getHealth(node)`, `isOnline(node)`
- Integrates with SensorManager as an alternative lambda source

**Graceful degradation:**
1. CAN wideband online → use CAN lambda (highest priority)
2. CAN timeout → fall back to on-board CJ125 if present
3. CJ125 not ready → fall back to narrowband 0-5V analog
4. No sensor → open-loop (VE table only, no correction)

### 5.3 Firmware Migration Path

The CJ125 on-board implementation remains **compilable and functional** — it's controlled by `cj125Enabled` in config. The CAN wideband is an additional source, not a replacement. Both can coexist:

| Config | Lambda Source |
|--------|---------------|
| `cj125Enabled: true, canWidebandEnabled: false` | On-board CJ125 (current behavior) |
| `cj125Enabled: false, canWidebandEnabled: true` | External CAN wideband module |
| `cj125Enabled: true, canWidebandEnabled: true` | CAN preferred, CJ125 fallback |
| `cj125Enabled: false, canWidebandEnabled: false` | Narrowband analog only |

### 5.4 CAN Bus Expansion

Once the CAN bus exists on the ESP-ECU, it becomes available for other modules too:

| Module | CAN ID Range | Purpose |
|--------|-------------|---------|
| Wideband O2 | 0x600-0x63F | Lambda, AFR, health |
| EGT (thermocouple) | 0x640-0x67F | Exhaust gas temperature per cylinder |
| Knock sensor | 0x680-0x6BF | Knock intensity per cylinder |
| Boost controller | 0x6C0-0x6FF | Wastegate/BOV position, boost pressure |
| Data logger | 0x700-0x73F | High-speed data capture module |

This transforms the ESP-ECU from a monolithic controller into a CAN-based distributed engine management system. The driver modules remain on RS-485 Modbus (they need direct wire control, not bus latency), but all sensor/feedback modules move to CAN.

---

## 6. Comparison Summary

### 6.1 On-Board vs Modular

| Criterion | On-Board CJ125 | Gen 1 (Commercial) | Gen 2 (Custom Single) | Gen 3 (Custom 4-Ch) |
|-----------|:--------------:|:-------------------:|:---------------------:|:--------------------:|
| **Hardware effort** | Done (existing) | None (buy) | Medium (PCB design) | High (PCB + analog) |
| **Firmware effort** | Done (existing) | Low (CAN RX) | Medium (CAN + controller) | Medium-High |
| **Per-channel cost** | ~$12-25 | ~$100-180 | ~$14 | ~$8.50 |
| **Channels** | 2 (fixed) | 1 per unit | 1 per unit | 4 per unit |
| **Max channels** | 2 | Limited by budget | 8+ (CAN nodes) | 8+ (2 modules) |
| **Sensor proximity** | Far (ECU board) | Close (standalone) | Close (module) | Close (module) |
| **CJ125 dependency** | Yes | No | No | No |
| **ESP32 pins used** | 6 + 2 buses | 2 (CAN) | 2 (CAN) | 2 (CAN) |
| **Failure isolation** | Poor | Good | Good | Good |
| **Update rate** | 10Hz | 10-50Hz | 50Hz | 50Hz |
| **LSU ADV ready** | No (CJ125 only) | Depends on controller | Yes (firmware) | Yes (firmware) |

### 6.2 Recommended Roadmap

1. **Now:** Keep on-board CJ125 working. Add CAN transceiver to ESP-ECU board design (2 GPIO + 1 chip). This is a $1 investment that opens the door to everything.

2. **Gen 1 (test phase):** Buy a Spartan 3 + CAN adapter or AEM X-Series. Connect to ESP-ECU CAN bus. Write WidebandCAN manager firmware (~200 lines). Validate CAN integration, test failover between CAN and CJ125. Run both simultaneously and compare readings.

3. **Gen 2 (validation phase):** Design and build a single-channel custom module based on rusEFI wideband reference design. Validate analog precision against Spartan 3 / AEM readings. Iterate PCB layout for noise and accuracy.

4. **Gen 3 (production phase):** Scale to 4-channel module for per-cylinder sensing. This is only needed for V8 per-cylinder trim or professional racing applications. Most users will be well-served by 2× Gen 2 modules (one per bank).

---

## 7. PCB Design Notes

### 7.1 Gen 2 Single-Channel Module

- **Layers:** 2-layer FR4, 1oz copper, 1.6mm
- **Board size:** ~30mm × 50mm
- **Ground strategy:** Single solid ground plane (bottom layer). Analog and digital share the same ground — no split. Route analog traces away from digital/heater traces.
- **Analog section:** Op-amps, Nernst reference, Ip sense resistors grouped together. Short traces, Kelvin connections to sense resistors. Guard traces around Nernst voltage buffer input.
- **Power section:** Heater MOSFET with thermal pad, flyback diode, current sense resistor. Wide traces (2mm+ for 2A heater current).
- **Sensor connector:** 6-pin for LSU 4.9 (Ip+, Ip-, Nernst, Heater+, Heater-, Shield)
- **Bus connector:** 4-pin (12V, GND, CAN_H, CAN_L)
- **Conformal coating:** Recommended (engine bay environment)

### 7.2 Gen 3 Four-Channel Module

- **Layers:** 2-layer MCPCB (aluminum substrate) — same as driver module
- **Board size:** ~60mm × 80mm (same form factor as driver module)
- **Rationale for MCPCB:** 4 heater circuits × 2A = 8A total dissipation. At ~0.5W per heater MOSFET + shunt, total ~2W in MOSFETs plus trace I²R losses. MCPCB keeps junction temperatures in check.
- **Shared enclosure:** Could potentially share the same enclosure/connector standard as the driver modules. Same mounting, same power input, same CAN bus connector.

> **See:** [BoardDesignAnalysis.md](BoardDesignAnalysis.md) Section 7 for MCPCB stackup details and thermal calculations.

---

## 8. Open Items

1. **CAN GPIO assignment** — Confirm GPIO43/GPIO44 availability or choose alternates. Check against current pin map for conflicts.
2. **CAN bus speed** — 500kbps is standard automotive. Confirm ESP32-S3 TWAI supports this with chosen transceiver.
3. **CAN protocol standardization** — Define frame format compatible with existing wideband CAN standards (many controllers use a de facto standard frame layout). Consider compatibility with Spartan 3 and AEM X-Series CAN protocols for Gen 1 interop.
4. **rusEFI wideband licensing** — rusEFI hardware is open-source (GPL). Verify license compatibility for Gen 2 if basing PCB design on their reference.
5. **LSU ADV evaluation** — Obtain LSU ADV sensor and CJ135 eval board for characterization. Compare resolution and response time against LSU 4.9.
6. **Per-cylinder exhaust plumbing** — Per-cylinder wideband requires sensor bungs in individual exhaust runners (before merge collector). This is straightforward on a tubular header but may require custom fabrication on factory cast manifolds.
7. **Heater inrush current** — 4-channel module draws up to 8A heater current during warm-up. Stagger heater start times (1-2s offset per channel) to limit inrush on the 12V supply.
8. **On-board CJ125 deprecation** — Once CAN wideband is validated, the CJ125 code and hardware can be removed from the ESP-ECU board design, recovering 4 GPIO + 2 expander pins + 2 LEDC channels. Keep the code compilable behind `cj125Enabled` for legacy boards.
9. **ESP-ECU board Rev 2** — When CAN transceiver is added, also consider: removing CJ125 footprints, adding CAN termination jumper, adding CAN bus ESD protection, and routing CAN_H/CAN_L as a differential pair on the 6-layer stackup (L1 or L6, 100Ω impedance).
