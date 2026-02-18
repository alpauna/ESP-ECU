# Driver Module Analysis — 4-Channel Low-Side Coil/Injector Driver

**Date:** 2026-02-18
**Context:** Remote diagnostic driver module for ESP-ECU, Modbus RTU feedback
**References:** INA180 (TI SBOS718), UCC27524A (TI SNVS883), REF5050 (TI SBOS410), TPS7A4701 (TI SBVS204), STM32F030 (ST DS9773), IRLZ44N (Infineon), IRFB4110 (Infineon), SP3485 (MaxLinear), RS-485 TTL Module (Sharvi Technology rev 1.1)

---

## 1. System Architecture

### 1.1 Purpose

A universal 4-channel low-side MOSFET driver module that:
- Receives logic-level control signals (3.3V) directly from ESP-ECU MCP23S17 expander outputs
- Switches 12V loads (coils or injectors) through discrete N-channel MOSFETs in TO-220 sockets
- Senses current on each channel via INA180A1 + shunt resistor
- Reports diagnostics back to ESP-ECU via Modbus RTU over RS-485
- Monitors board temperature via REF5050 TEMP output

### 1.2 Configuration

| Application | Modules needed | Notes |
|-------------|---------------|-------|
| V8 engine | 2× coil + 2× injector | 4 channels per module = 1 bank |
| 4-cylinder | 1× coil + 1× injector | Same module, fewer channels active |
| 6-cylinder | 2× coil + 2× injector | 2 channels spare per pair |

### 1.3 Signal Flow

```
ESP-ECU (MCP23S17)                    Driver Module                         Load
                                ┌─────────────────────────────────┐
  EN ──[wire]──────────────────▶│ Q6(2N7002)→Q5(P-FET) ──▶ +12V_LOAD ──▶ Coil/Injector
  CH1-CH4 ──[4× wire]─────────▶│ UCC27524A → Q1-Q4(MOSFET) ──▶ drain ────┘     │
                                │         source ──▶ R_shunt ──▶ GND            │
  +3.3V, GND ──[wire]─────────▶│                       │                        │
                                │              INA180A1 (×4) ──▶ ÷2 divider     │
                                │                       │                        │
                                │              STM32F030 ADC ◀──┘                │
                                │                │                               │
                                │              UART (Modbus slave)               │
RS-485 bus ◀────────────────────│──── SP3485 ◀──┘                               │
  (A, B, 12V, GND)             │                                                │
                                │  Power rails:                                  │
  12V bus (from RS-485 mod) ───▶│  ├── TPS7A4701 → 5.0V_A (INA180A1)           │
                                │  ├── REF5050 → 5.000V (cal ref + TEMP)        │
                                │  ├── AMS1117-3.3 → 3.3V (MCU, SP3485)        │
                                │  └── UCC27524A VDD (gate drivers)             │
                                │                                                │
  ISEN− (battery, post-sense) ─▶│── PTC fuse ── Enable P-FET ──▶ +12V_LOAD ───┘
                                └─────────────────────────────────┘
```

### 1.4 Two Power Domains

| Domain | Source | Voltage | Protection | Purpose |
|--------|--------|---------|------------|---------|
| **Load power** | ISEN− on ECU (battery, post-sense) | 7-36V | ECU fuse (F1) + SMBJ36A + per-module resettable fuse | Coil/injector switching via MOSFETs |
| **Electronics power** | RS-485 module TPS61089 12V bus | 12V ±0.6V | TPS61089 OVP at 13.2V | MCU, sense amps, gate drivers, VREF |

The load power comes from the ECU's **ISEN− point** — the low side of the current sense resistor, downstream of the input fuse (F1) and SMBJ36A TVS. This means the driver modules inherit the ECU's input protection. Each module gets its own resettable PTC fuse(s) at the FltVout distribution point, so a module short trips only its local fuse without blowing the main fuse F1.

The electronics power comes from the RS-485 TTL converter module's TPS61089 boost converter, which produces 12V from the ESP-ECU 5V rail. This 12V is distributed on the RS-485 bus (RJ45 or dedicated power pins) to all driver modules.

The total load current through F1 and the LM25118 (U5) current sense loop must be monitored as modules are added — see Section 2.6 for power budget tracking.

---

## 2. Power Supply Analysis

### 2.1 RS-485 Module Power Budget

The RS-485 module's TPS61089 boost converter provides the 12V bus rail for all driver modules.

**TPS61089 capacity (from 5V ESP-ECU input):**
- Max output current at 12V from 5V input: ~2.0A
- Efficiency at light load (200mA): ~80-85%
- OVP: 13.2V (output capped at 12.6V max)

**Per-driver-module consumption from 12V bus:**

| Subsystem | Current from 12V | Notes |
|-----------|-----------------|-------|
| TPS7A4701 (→5V, 50mA load) | 50mA × (5/12) / 0.99 ≈ 21mA | LDO: Iin ≈ Iout = 50mA actual |
| REF5050 (→5V, 5mA load) | 5mA + 1.4mA quiescent ≈ 6.4mA | |
| AMS1117-3.3 (→3.3V, 30mA load) | 30mA + 5mA quiescent ≈ 35mA | Iin = Iout for LDO |
| UCC27524A ×2 (quiescent) | 2 × 2.5mA = 5mA | Dynamic: adds ~1mA per kHz switching average |
| **Total per module** | **~97mA** | Conservative estimate |

**System total (4 modules):**
- 4 × 97mA = 388mA from 12V bus
- TPS61089 input current at 5V: 388mA × 12V / (5V × 0.93) ≈ 1.0A
- Well within TPS61089 2A output and ESP-ECU 5V rail capacity

**Voltage drop on bus cable:**
- 22AWG wire: ~53mΩ/m. At 400mA over 2m round-trip: 2 × 2m × 53mΩ/m × 0.4A = 85mV drop
- Bus voltage at furthest module: 12.0V - 0.085V = 11.9V → negligible

### 2.2 Driver Module Internal Rails

```
12V Bus ──┬── TPS7A4701 ──▶ 5.0V_ANALOG (INA180A1 ×4 supply)
(11.9-12.6V) │    Pdiss = (12 - 5) × 0.055A = 385mW (QFN-20, θJA=30.5°C/W → +12°C rise)
          │
          ├── REF5050 ──▶ 5.000V VREF (calibration + TEMP)
          │    Via 33Ω input filter (per ESP-ECU design): Vdrop = 33Ω × 6.4mA = 0.21V
          │    REF5050 VIN = 12V - 0.21V = 11.8V (well above 5.2V minimum)
          │    Pdiss = (11.8 - 5) × 0.0064 = 44mW (negligible)
          │
          ├── AMS1117-3.3 ──▶ 3.3V DIGITAL (STM32, SP3485)
          │    Pdiss = (12 - 3.3) × 0.035A = 305mW (SOT-223, θJA≈90°C/W → +27°C rise)
          │
          └── UCC27524A VDD (×2) ──▶ 12V direct (gate driver supply)
               No regulator needed — 12V bus is within 4.5-18V spec
```

### 2.3 TPS7A4701 Configuration for 5.0V Output

The TPS7A4701 (adjustable version) output is set by:

```
VOUT = 1.4V × (1 + R1/R2)

For VOUT = 5.0V:
R1/R2 = (5.0 / 1.4) - 1 = 2.571

R2 = 10.0kΩ, R1 = 25.5kΩ → VOUT = 1.4 × 3.55 = 4.970V (−0.6%)
R2 = 10.0kΩ, R1 = 26.1kΩ → VOUT = 1.4 × 3.61 = 5.054V (+1.1%)

Recommended: R2 = 10.0kΩ (0.1%), R1 = 25.5kΩ (0.1%) → 4.970V
```

Alternatively, use **TPS7A4700** (ANY-OUT version) with pin strapping:
- VREF = 1.4V. For 5.0V: connect pin 6 (3.2V) + pin 10 (0.4V) to GND = 1.4 + 3.2 + 0.4 = 5.0V
- No external resistors needed

**Key TPS7A4701 specs at this operating point:**

| Parameter | Value |
|-----------|-------|
| Dropout at 50mA | ~15mV |
| VIN = 12V, VOUT = 5V | 7V headroom (no dropout concern) |
| Output noise | 4.0 µVRMS (10Hz–100kHz) |
| PSRR at 1kHz | ~75 dB |
| Pdiss at 55mA | (12 − 5) × 0.055 = 385mW |
| Temp rise (QFN-20, θJA=30.5°C/W) | 385mW × 30.5 = +11.7°C |

### 2.4 REF5050 Analysis

**Power supply:**
- VIN from 12V bus through 33Ω filter resistor (matches ESP-ECU design)
- VIN = 12V − (33Ω × 6.4mA) = 11.79V
- REF5050 max VIN = 18V → 11.79V OK ✓
- REF5050 min VIN = 5.2V → 11.79V OK ✓ (6.6V headroom)

**TEMP pin characteristics:**

| Parameter | Value | Notes |
|-----------|-------|-------|
| Sensitivity | ~2.6 mV/°C | Approximate, not factory-trimmed |
| Output impedance | ~60 kΩ | HIGH — requires buffering or long ADC sample time |
| Accuracy | ±15°C absolute | Useful for relative change detection and over-temp warning |
| Voltage at 25°C | ~1.45V (estimated) | Within STM32 3.3V ADC range ✓ |
| Voltage at 85°C | ~1.61V (estimated) | Within STM32 3.3V ADC range ✓ |
| Voltage at 125°C | ~1.71V (estimated) | Within STM32 3.3V ADC range ✓ |

**Buffering the TEMP pin for STM32 ADC:**
The 60kΩ source impedance would cause ADC sampling errors at default sample times. Two solutions:
1. **Long sample time:** Set ADC sample time to 239.5 cycles at 14MHz = 17.1µs. The STM32 ADC input has ~7pF capacitance. Charge time = 5 × 60kΩ × 7pF = 2.1µs ≪ 17.1µs ✓
2. **External filter cap:** 100nF on PA5 to GND. τ = 60kΩ × 100nF = 6ms. Read temperature once per second — plenty of settling time.

Recommendation: Use both — 100nF filter cap AND longest sample time on the TEMP channel.

### 2.5 FltVout — Fused Load Power Distribution

Load power for the driver modules comes from the ECU's **ISEN− point** — the output side of the current sense resistor on the power supply board. This is downstream of the main fuse (F1) and SMBJ36A TVS, so modules inherit the ECU's input protection.

**Problem:** If a module shorts (e.g., MOSFET fails short-circuit), the full battery current flows through F1. A single module fault could blow the main fuse and kill the entire ECU.

**Solution: FltVout distribution point with per-module resettable PTC fuses.**

```
ECU Power Supply Board                      FltVout Distribution
                                            (on ECU or breakout PCB)
Battery ─▶ F1 (main fuse) ─▶ SMBJ36A ─▶ ISEN ─▶ ISEN− ──┬── PTC1 ──▶ Module 1 (Coil Bank A)
                                                           ├── PTC2 ──▶ Module 2 (Coil Bank B)
                                                           ├── PTC3 ──▶ Module 3 (Inj Bank A)
                                                           └── PTC4 ──▶ Module 4 (Inj Bank B)
```

**Per-module PTC fuse sizing:**

| Module Type | Peak Current | Sustained Current | PTC Rating | Notes |
|-------------|-------------|-------------------|------------|-------|
| Injector (4ch) | 4 × 2A = 8A burst | 4 × 1A × 50% duty = 2A | 2× 1.85A PTC in parallel = **3.7A hold** | Two smaller PTCs side-by-side |
| COP trigger (4ch) | 4 × 0.5A = 2A | 4 × 0.5A × 25% duty = 0.5A | 1× 1.1A PTC | Low current, single fuse OK |
| Direct coil (4ch) | 4 × 10A = 40A burst | 4 × 10A × 25% duty = 10A | 2× 5A PTC in parallel = **10A hold** | Parallel for current + board space |

**Why parallel PTC fuses:** Larger PTC fuses have physically large footprints that waste board space. Two smaller PTCs in parallel share the current, fit in standard footprints (1812 or 2920), and provide the same total hold current. They also trip faster than a single oversized PTC because each sees half the fault current relative to its own trip threshold.

**PTC placement:** Mount the PTCs **away from the current sense shunt resistors**. PTC self-heating (especially near trip) radiates heat that would cause thermal drift in the sense shunts, degrading current measurement accuracy. Recommended: place PTCs near the input connector (J1) at the board edge, sense shunts near the MOSFETs in the center of the board, with at least 10mm separation and a ground plane thermal break between them.

**Recommended PTC parts (1812 package):**

| Current | Part Number | Hold/Trip | Max V | Package |
|---------|-------------|-----------|-------|---------|
| 1.1A | SMD1812P110TF | 1.1A/2.2A | 24V | 1812 |
| 1.85A | SMD1812P185TF | 1.85A/3.7A | 16V | 1812 |
| 5A | SMD2920P500TF | 5.0A/10A | 16V | 2920 |

### 2.6 F1 Power Budget and U5 (LM25118) Monitoring

As modules are added, the total load current through F1 increases. The LM25118 buck-boost converter (U5) on the power supply board has a current sense loop that can monitor this.

**Worst-case F1 current budget (V8, all modules active):**

| Load | Current | Duty Cycle | Average |
|------|---------|-----------|---------|
| 2× injector modules (8 injectors) | 8 × 1A peak | ~15% (typical cruise) | 1.2A |
| 2× coil modules (8 coils, COP) | 8 × 0.5A peak | ~25% (dwell) | 1.0A |
| ESP-ECU electronics | — | 100% | 0.3A |
| RS-485 bus (TPS61089 input) | — | 100% | 1.0A |
| **Total average** | | | **3.5A** |
| **Total peak (all firing)** | | | **~12A** |

**F1 fuse recommendation:** 15A automotive blade fuse. Provides headroom for inrush and peak, while still protecting wiring.

**U5 (LM25118) monitoring:** The LM25118's current sense resistor in the power supply sees total input current. If the ESP-ECU reads the ISENSE output (or an INA180 on the power supply board's sense resistor), it can track total system power draw and set warnings:
- Alert if average current exceeds 80% of F1 rating
- Alert on sudden current jumps (module fault before PTC trips)
- Log power consumption trends over time

### 2.7 REF5050 as ADC Calibration Reference

The STM32F030 TSSOP-20 has **no separate VREF+ pin** — ADC reference is internally tied to VDDA (3.3V from AMS1117). AMS1117 accuracy is ±1%, which limits ADC absolute accuracy.

**Solution: Software calibration via REF5050 known voltage.**

Route REF5050 output (5.000V) through a precision 2:1 resistive divider to an ADC channel:

```
REF5050 OUT (5.000V) ──── 10.0kΩ (0.1%) ──┬── STM32 PA6 (ADC IN6)
                                            │
                                       10.0kΩ (0.1%) ──── GND
                                            │
                                          100pF (anti-aliasing)
```

Divider output = 5.000V / 2 = 2.500V (known to REF5050 accuracy: ±0.05%)

**Firmware calibration algorithm:**

```c
// Read REF5050 calibration channel
uint16_t adc_ref = read_adc(ADC_CH6);  // Should be 2.500V

// Calculate actual VDDA
float vdda_actual = (2.500 * 4096.0) / adc_ref;  // e.g., if adc_ref=3103 → VDDA=3.301V

// Correct any INA180 reading
float v_adc = adc_raw * vdda_actual / 4096.0;     // True voltage at divider
float v_ina = v_adc * 2.0;                          // Undo 2:1 divider
float i_load = v_ina / (20.0 * R_SHUNT);           // INA180A1 gain = 20
```

This gives **current measurement accuracy limited only by:**
- REF5050: ±0.05% (high-grade) to ±0.1% (standard)
- INA180A1 gain error: ±1%
- Shunt resistor: ±1%
- **Total system accuracy: ~±2.1%** (RSS of independent errors)

---

## 3. Analog Signal Chain

### 3.1 Current Sensing — INA180A1

Each channel: Shunt resistor in MOSFET source path → INA180A1 → 2:1 divider → STM32 ADC.

**INA180A1 key specs:**

| Parameter | Value |
|-----------|-------|
| Gain | 20 V/V (fixed, A1 variant) |
| Bandwidth | 350 kHz (−3dB) |
| Common-mode range | −0.2V to +26V |
| Supply | 5.0V (from TPS7A4701) |
| Output swing | 5mV to 4.97V |
| Offset (VCM=12V) | ±500µV max |
| Offset (VCM=0V) | ±150µV max |

**Circuit per channel:**

```
+12V_LOAD ──▶ Coil/Injector ──▶ MOSFET Drain
                                       │
                                  MOSFET Source
                                       │
                                  ┌────▼────┐
                              IN+ │ INA180  │ OUT ── 10kΩ ──┬── STM32 ADC
                                  │  A1     │               │
                              IN− │ (20V/V) │          10kΩ ┴── GND
                                  └────┬────┘               │
                                       │               100pF (filter)
                                   R_shunt
                                       │
                                      GND
```

### 3.2 Shunt Resistor Selection

**Injector variant (R_shunt = 100mΩ, 1%, 2512 package):**

| Parameter | Calculation | Value |
|-----------|-------------|-------|
| Full-scale current | (4.97V / 20) / 0.100Ω | **2.485A** |
| ADC full-scale (after ÷2) | 4.97 / 2 = 2.485V | Within 3.3V range ✓ |
| Typical injector current | 12V / 12Ω = 1.0A | **40% of full scale** |
| INA180 output at 1.0A | 20 × 0.100 × 1.0 = 2.0V | ADC sees 1.0V |
| ADC resolution per mA | (1.0A / 2.485A) × 4096 = 1648 counts for 1A → **0.6mA/count** |
| Shunt power at 1.0A | 1.0² × 0.100 = 100mW | 2512 rated 1W ✓ |
| Shunt voltage drop at 1A | 100mV | Negligible vs 12V supply |

**Direct coil variant (R_shunt = 10mΩ, 1%, 2512 package):**

| Parameter | Calculation | Value |
|-----------|-------------|-------|
| Full-scale current | (4.97V / 20) / 0.010Ω | **24.85A** |
| Typical coil current | 5-10A | **20-40% of full scale** |
| INA180 output at 10A | 20 × 0.010 × 10 = 2.0V | ADC sees 1.0V |
| ADC resolution per mA | 10A → 1648 counts → **6.1mA/count** |
| Shunt power at 10A | 10² × 0.010 = 1.0W | 2512 rated 2W for 10mΩ ✓ |
| Shunt voltage drop at 10A | 100mV | Negligible vs 12V supply |

### 3.3 Sense Resistor Sizing Trade-offs

The shunt resistor value is a trade-off between signal-to-noise and power dissipation / voltage drop. Smaller shunts reduce wasted power and heat, but decrease signal amplitude and worsen SNR.

**Injector variant options:**

| Shunt | V_shunt at 1A | INA180 out | ADC (÷2) | Resolution | Shunt Power at 2A | Notes |
|-------|--------------|-----------|----------|-----------|-------------------|-------|
| 100mΩ | 100mV | 2.0V | 1.0V | 0.6mA/count | 400mW | Current BOM, good SNR |
| 50mΩ | 50mV | 1.0V | 0.5V | 1.2mA/count | 100mW | Less heat, acceptable SNR |
| 33mΩ | 33mV | 0.66V | 0.33V | 1.8mA/count | 44mW | Minimal heat, noisier |

At 50mΩ, the INA180A1 offset (±500µV input-referred) represents ±10mA error at 1A — still acceptable for fault detection. Going below 33mΩ for injectors risks poor open-circuit detection (signal near noise floor at low currents).

**Direct coil variant:** The 10mΩ shunt is already small. Going to 5mΩ halves the resolution but also halves the 1W dissipation at 10A. Consider this if board thermal management is tight.

**Recommendation:** Start with the current BOM values (100mΩ / 10mΩ). If thermal testing shows the sense shunts run too hot near the PTC fuses (despite the layout separation), drop to 50mΩ / 5mΩ respectively. The REF5050 calibration compensates for the reduced signal by maintaining ADC accuracy.

### 3.4 INA180A1 Error Budget

At 1.0A injector current (100mΩ shunt):

| Error Source | Magnitude | Effect on Reading |
|-------------|-----------|-------------------|
| INA180A1 offset (VCM=12V) | ±500µV input-referred | ±500µV / (20 × 0.1Ω) = **±0.25mA** |
| INA180A1 gain error | ±1% | ±1% × 1.0A = **±10mA** |
| Shunt tolerance | ±1% | ±1% × 1.0A = **±10mA** |
| ADC quantization | 0.6mA/count | **±0.3mA** (half-count) |
| VDDA drift (corrected via REF5050) | <±0.1% (after cal) | **±1mA** |
| Divider resistor tolerance | ±0.1% (0.1% resistors) | **±1mA** |
| **Total (RSS)** | | **±14.2mA (±1.4%)** |

This is more than adequate for detecting:
- Open injector (0mA when driven — expected ~1A)
- Stuck-open injector (current without drive signal)
- Short circuit (current >> expected)
- Degraded coil (current profile changed)

### 3.4 STM32F030 ADC Channel Assignment

| Pin | ADC Channel | Signal | Divider | Full-Scale Input |
|-----|-------------|--------|---------|-----------------|
| PA0 | IN0 | INA180 CH1 output | 2:1 (10k/10k) | 0–2.485V |
| PA1 | IN1 | INA180 CH2 output | 2:1 (10k/10k) | 0–2.485V |
| PA2 | IN2 | INA180 CH3 output | 2:1 (10k/10k) | 0–2.485V |
| PA3 | IN3 | INA180 CH4 output | 2:1 (10k/10k) | 0–2.485V |
| PA5 | IN5 | REF5050 TEMP | Direct (100nF filter) | ~1.0–1.8V |
| PA6 | IN6 | REF5050 OUT ÷2 | 2:1 (10k/10k, 0.1%) | 2.500V fixed |
| — | IN16 | Internal temp sensor | — | ~1.43V at 25°C |
| — | IN17 | VREFINT (1.23V) | — | Backup calibration |

**ADC configuration:**
- 12-bit resolution, 14MHz ADC clock
- Scan mode: channels 0-3 at 7.5 cycles sample time (fast, low source impedance through dividers)
- Channel 5 (TEMP): 239.5 cycles sample time (60kΩ source impedance)
- Channel 6 (REF cal): 71.5 cycles sample time (5kΩ source impedance)
- DMA transfer to buffer, ~10kHz effective sample rate per channel

---

## 4. MOSFET and Gate Driver Analysis

### 4.1 Gate Driver — UCC27524A

Two UCC27524ADBVR (dual gate driver) provide 4 channels.

**Input compatibility with 3.3V MCP23S17 outputs:**

| Parameter | UCC27524A Spec | MCP23S17 Output | Margin |
|-----------|---------------|-----------------|--------|
| VIH (min) | 2.0V | 3.3V (VOH) | +1.3V ✓ |
| VIL (max) | 1.3V | 0V (VOL) | −1.3V ✓ |
| Hysteresis | 0.9V | — | Excellent noise immunity |
| Input clamp | −5V to VDD+0.3V | 0–3.3V | Within range ✓ |

**Switching performance at VDD = 12V:**

| Parameter | Value | Notes |
|-----------|-------|-------|
| Propagation delay | 13ns typical, 25ns max | Input to output |
| Rise time (1.8nF load) | 9ns | IRLZ44N Ciss = 1700pF ≈ 1.7nF |
| Fall time (1.8nF load) | 9ns | |
| Peak source current | 5A | |
| Peak sink current | 5A | |
| Channel-to-channel match | 1ns typical | |

**MOSFET switching time calculation:**
- IRLZ44N Qg at Vgs=10V: 48nC (datasheet at Vgs=5V, conservatively ~60nC at Vgs=12V)
- Turn-on time: Qg / I_source = 60nC / 5A = **12ns**
- Turn-off time: Qg / I_sink = 60nC / 5A = **12ns**

For ignition dwell timing at 1µs resolution: 12ns switching << 1µs ✓
For injector pulse width at 10µs resolution: 12ns switching << 10µs ✓

### 4.2 MOSFET — IRLZ44NPBF (Injector / COP-with-igniter Variant)

| Parameter | Condition | Value |
|-----------|-----------|-------|
| VDS max | | 55V |
| ID continuous | TC = 25°C | 47A |
| Rds(on) | VGS = 10V | 22mΩ max |
| Rds(on) | VGS = 5V | 28mΩ max |
| Rds(on) | VGS = 4.5V | 35mΩ max |
| VGS(th) | ID = 250µA | 1.0–2.0V |
| Qg total | VGS = 5V | 48nC |

At VDD = 12V gate drive, IRLZ44N is fully enhanced well below its rated Rds(on).

**Power dissipation at 1A (injector):**
- Conduction: 1.0² × 0.022 = 22mW
- Switching (at 100Hz injector frequency): negligible
- **Total: ~22mW per channel → no heatsink needed**

**Power dissipation at 0.5A (COP trigger):**
- Conduction: 0.5² × 0.022 = 5.5mW
- **Negligible thermal impact**

### 4.3 MOSFET — IRFB4110PBF (Direct Coil Drive Variant)

| Parameter | Condition | Value |
|-----------|-----------|-------|
| VDS max | | 100V |
| ID continuous | TC = 25°C | 180A |
| Rds(on) | VGS = 10V | 3.7mΩ typical |
| VGS(th) | | 2.0–4.0V |
| Qg total | VGS = 10V | 150nC |

At VDD = 12V: Rds(on) < 3.7mΩ (fully enhanced).

**Switching time with UCC27524A:**
- Qg = 150nC, I_driver = 5A → t_sw = 150nC / 5A = **30ns**
- Dwell cutoff precision: 30ns ≪ typical 1µs requirement ✓

**Power dissipation at 10A (direct coil primary):**
- Conduction: 10² × 0.0037 = 370mW
- Duty cycle: ~25% at 6000 RPM (dwell ~4ms / 20ms period) → avg = 93mW
- Switching loss at 300Hz: P_sw = 0.5 × V × I × (t_rise + t_fall) × f = 0.5 × 12 × 10 × 60ns × 300 = 1.1mW
- **Total: ~94mW average → TO-220 easily handles this without heatsink**

### 4.4 100V Flyback Clamp (Direct Coil Variant)

When the MOSFET turns off, the coil's stored energy (½LI²) drives the drain voltage up until clamped.

**With SMBJ80A TVS (drain-to-source):**
- Standoff: 80V
- Breakdown: 89V min
- Clamping at 1A: 129V max
- IRFB4110 VDS max: 100V

**Concern:** Clamping voltage (129V) exceeds MOSFET VDS (100V) at high pulse current.

**Mitigation options:**
1. **Use IRFB4310 (100V) + SMBJ80A** — 129V clamp still exceeds rating. Marginal.
2. **Use SMBJ60A instead** — Standoff 60V, clamp 96V at 1A. Fits within 100V VDS with margin. But 60V standoff means TVS starts conducting at ~67V breakdown, which limits useful flyback energy absorption.
3. **Use a 68V zener + fast diode** — More predictable clamp voltage.
4. **Use IRFB4115PBF (150V, 104A, 11mΩ)** — Higher VDS headroom. Clamp at 129V is safely within 150V.

**Recommendation:** For direct coil drive, use **IRFB4115PBF (150V)** with SMBJ80A. This gives 21V margin between clamp (129V) and VDS max (150V).

For injector variant: SMBJ40A clamp = 65V, IRLZ44N VDS = 55V → marginal.
Better: **SMBJ33A** — Standoff 33V, clamp 53V at 1A. Within 55V VDS with 2V margin.
Or use **IRLZ44N (55V) + SMBJ33A (53V clamp)** ✓

**Updated TVS recommendations:**

| Variant | MOSFET | VDS max | TVS | Clamp voltage | Margin |
|---------|--------|---------|-----|---------------|--------|
| Injector | IRLZ44NPBF | 55V | SMBJ33A | 53.3V | 1.7V |
| COP trigger | IRLZ44NPBF | 55V | SMBJ33A | 53.3V | 1.7V |
| Direct coil | IRFB4115PBF | 150V | SMBJ80A | 129V | 21V |

---

## 5. Enable / Misfire Protection Circuit

### 5.1 Three-Layer Protection

**Layer 1 — Input pulldowns (R13-R16, 10kΩ):**
Each control input has a 10kΩ pulldown to GND. When the ESP-ECU is not connected, unpowered, or a wire breaks, all inputs are pulled LOW → gate drivers output LOW → MOSFETs OFF.

**Layer 2 — Gate pulldowns (R5-R8, 10kΩ):**
Each MOSFET gate has a 10kΩ resistor to source. Even if the gate driver IC loses power or latches, the gate discharges through this resistor. Time constant = 10kΩ × Ciss = 10kΩ × 1700pF = 17µs. Gate fully discharged within 85µs (5τ).

**Layer 3 — Enable P-FET kill switch (Q5 + Q6):**
A P-channel MOSFET (DMP6023LE or similar) on the +12V_LOAD rail cuts ALL power to the load outputs. Default state is OFF.

### 5.2 Enable Circuit Detail

```
ISEN− (battery, post-sense) ──┬── PTC fuse(s) ──┬── +12V_BATT
                   │
                   │ Source
                  ┌┴┐
            Q5    │ │  DMP6023LE (P-FET)
            -60V  │ │  -3.3A, 50mΩ
                  └┬┘
                   │ Drain ──────── +12V_LOAD (to coil/injector connectors)
                   │
              Gate │──── R23 (10kΩ) ──── +12V_BATT (default: gate = source = OFF)
                   │
                   └──── Q6 drain
                            │
                   Q6 gate ◀── R25 (100Ω) ◀── EN pin (3.3V from ESP-ECU)
                            │
                   Q6 source ── GND
                            │
                   EN pin ── R24 (10kΩ) ── GND  (default: EN pulled LOW)
```

**Operating states:**

| State | EN pin | Q6 (N-FET) | Q5 gate | Q5 (P-FET) | +12V_LOAD |
|-------|--------|------------|---------|------------|-----------|
| Default (no ESP-ECU) | 0V (pulldown) | OFF | +12V (pullup) | OFF | 0V — **SAFE** |
| ESP-ECU active | 3.3V | ON | ~0V (pulled to GND) | ON | Battery V |
| EN wire broken | 0V (pulldown) | OFF | +12V | OFF | 0V — **SAFE** |
| ESP-ECU crash/reset | 0V (GPIO default) | OFF | +12V | OFF | 0V — **SAFE** |

### 5.3 Gate Series Resistors (R9-R12, 100Ω)

Between each UCC27524A output and MOSFET gate:
- Limits dV/dt ringing from gate trace inductance
- Reduces EMI from fast switching edges
- Slows switching by: Δt = R × Ciss = 100Ω × 1700pF = 170ns (IRLZ44N)
- This is still fast enough for injector timing (ms-scale pulse widths)
- For direct coil variant: 100Ω × 3300pF (IRFB4115 Ciss) = 330ns — still fine for dwell control

### 5.4 High-Side P-MOSFET Sizing

The enable P-FET must handle the full load current of all 4 channels simultaneously. P-FETs are preferred for simplicity — they sit on the high side, gate is pulled to source (OFF) by default, and an N-FET level shifter drives the gate low to turn on.

**P-FET selection by variant:**

| Variant | Peak Load | Sustained Load | Recommended P-FET | VDS | ID | Rds(on) | Package |
|---------|-----------|----------------|--------------------|----|-----|---------|---------|
| Injector | 4 × 2A = 8A | 4 × 1A × 50% = 2A | DMP6023LE | -60V | -3.3A | 50mΩ | SOT-23 |
| COP trigger | 4 × 0.5A = 2A | ~0.5A avg | DMP6023LE | -60V | -3.3A | 50mΩ | SOT-23 |
| Direct coil | 4 × 10A = 40A | 4 × 10A × 25% = 10A | **IRF4905PBF** | -55V | -74A | 20mΩ | TO-220 |

**For injector/COP variants:** The DMP6023LE in SOT-23 handles 3.3A continuous easily. At 2A sustained: Pdiss = 2² × 0.050 = 200mW, well within SOT-23 thermal limits.

**For direct coil variants:** The DMP6023LE is undersized (3.3A max). Use **IRF4905PBF** (-55V, -74A, 20mΩ Rds(on), TO-220) or similar high-current P-FET. At 10A sustained: Pdiss = 10² × 0.020 = 2W. Needs a small heatsink or PCB copper pour. Alternatively, use **FQP27P06** (-60V, -27A, 70mΩ, TO-220) which handles 10A with 7W capacity.

**P-FET gate drive consideration:**
- Q5 (P-FET) gate must be pulled to GND to turn ON. With 12V battery rail, Vgs = -12V when Q6 pulls gate to GND.
- DMP6023LE: Vgs(th) = -1.0 to -3.0V, max Vgs = ±20V → -12V drive is fine
- IRF4905: Vgs(th) = -2.0 to -4.0V, max Vgs = ±20V → -12V drive is fine
- Gate pullup R23 (10kΩ to +12V) ensures gate = source = OFF when Q6 is off

### 5.5 Enable Pin — ESP-ECU Integration

The enable signal comes from a dedicated MCP23S17 expander pin configured in the module descriptor's `enablePin` field. The DriverModuleManager controls it:

**Normal operation:** DriverModuleManager keeps the enable pin HIGH → Q6 ON → Q5 gate pulled LOW → P-FET ON → load powered.

**Fault shutdown:** When DriverModuleManager detects overcurrent, stuck coil, or thermal fault with `MFAULT_SHUTDOWN` action, it drives the enable pin LOW → Q6 OFF → Q5 gate pulled to +12V → P-FET OFF → 12V instantly cut to all 4 channels.

**ESP-ECU firmware path:**
```
DriverModuleManager::evaluateThresholds()
  → overcurrent detected on module "Coil Bank A"
  → faultAction == MFAULT_SHUTDOWN
  → setEnablePin(slot, false)
  → xDigitalWrite(enablePin, LOW)    // MCP23S17 expander pin
  → Q6 OFF → Q5 gate → +12V → P-FET OFF → 12V LOAD KILLED
  → fireFault("MOD_Coil Bank A", "Overcurrent ...", true)
  → MQTT publishes fault, dashboard pill turns red
```

**Recommended enable pin allocation (V8):**

| Module | Enable Pin | MCP23S17 | Notes |
|--------|-----------|----------|-------|
| Coil Bank A (cyl 1-4) | Pin 210 | #0 P10 | Kills all 4 coils on bank A |
| Coil Bank B (cyl 5-8) | Pin 211 | #0 P11 | Kills all 4 coils on bank B |
| Injector Bank A (cyl 1-4) | Pin 212 | #0 P12 | Kills all 4 injectors on bank A |
| Injector Bank B (cyl 5-8) | Pin 213 | #0 P13 | Kills all 4 injectors on bank B |

Each pin pair (coil+injector on same bank) can optionally share a single enable for complete bank shutdown.

**Watchdog integration:** The ECU main loop can periodically re-assert enable HIGH. If the main loop stalls (crash/watchdog), enable stays LOW → modules disarmed. The MCP23S17 powers up with all outputs LOW (default), so modules are always OFF until explicitly enabled.

---

## 6. Thermal Analysis

### 6.1 MOSFET Thermal Budget

**Injector variant (1A, 22mΩ, TO-220):**
- Pdiss = 22mW (continuous) → ΔT = 22mW × 62°C/W (TO-220 to air) = 1.4°C
- **No heatsink needed**

**Direct coil variant (10A peak, 25% duty, 3.7mΩ average, TO-220):**
- Pdiss = 370mW peak × 25% duty = 93mW average
- ΔT = 93mW × 62°C/W = 5.8°C above ambient
- **No heatsink needed**

### 6.2 LDO Thermal Budget

| LDO | Pdiss | Package | θJA | ΔT |
|-----|-------|---------|-----|-----|
| TPS7A4701 (12→5V, 55mA) | 385mW | QFN-20 | 30.5°C/W | +11.7°C |
| AMS1117-3.3 (12→3.3V, 35mA) | 305mW | SOT-223 | ~90°C/W | +27.4°C |

At 85°C ambient (engine bay): AMS1117 junction = 85 + 27.4 = **112.4°C** (max rated 125°C — 12.6°C margin).

**Improvement option:** Replace AMS1117 with **AP2112K-3.3** (SOT-23-5, higher efficiency) or add a 78L05-style pre-regulator to reduce AMS1117 input voltage. But 12.6°C margin is acceptable for a module that's monitoring its own temperature.

### 6.3 Board Temperature Monitoring via REF5050 TEMP

**Temperature thresholds (configurable in Modbus holding register or firmware):**

| Level | Temperature | Action |
|-------|-------------|--------|
| Normal | < 70°C | Green — health = 100% |
| Warning | 70–85°C | Orange — health degrades, Modbus fault bit 3 set |
| Fault | > 85°C | Red — self-disable outputs (optional), ESP-ECU notified |
| Critical | > 100°C | Self-shutdown — disable enable relay, set latching fault |

The STM32 firmware reads the TEMP pin every second, applies a simple moving average, and updates the Modbus temperature register (register 17, int16, 0.1°C units) and health byte (register 16 high byte).

---

## 7. Communication — Modbus RTU

### 7.1 RS-485 Physical Layer

**On-board transceiver: SP3485EN-L/TR (SOIC-8)**
- 3.3V supply from AMS1117
- Half-duplex, DE/RE controlled by STM32 PA4
- 10 Mbps max — ample for 9600 baud
- ESD: ±15kV HBM on bus pins

The driver module has its own SP3485 on-board. The RS-485 bus connects via a 4-pin header or RJ45 (A, B, 12V, GND) carrying both data and power.

### 7.2 Modbus Address Selection

3-position DIP switch (SW1) provides addresses 1–8:
- SW1.1 = bit 0 (×1)
- SW1.2 = bit 1 (×2)
- SW1.3 = bit 2 (×4)
- Address = switch value + 1 (so all-off = address 1, not 0)

### 7.3 Register Map

Same as ESP-ECU ModbusManager implementation (already committed).

**Holding registers (function 0x03):**

| Register | Content | Scale |
|----------|---------|-------|
| 0-3 | Ch1-4 peak current | uint16, ×0.001A |
| 4-7 | Ch1-4 pulse width | uint16, ×0.01ms |
| 8-11 | Ch1-4 energy per pulse | uint16, ×0.1mJ |
| 12-15 | Ch1-4 fault flags | uint16, bit-packed |
| 16 | Health (high byte) + type (low byte) | uint8 + uint8 |
| 17 | Board temperature | int16, ×0.1°C |

**Input registers (function 0x04, read-only):**

| Register | Content |
|----------|---------|
| 0-1 | Firmware version (uint32) |
| 2-3 | Serial number (uint32) |

---

## 8. Firmware Architecture (STM32F030)

### 8.1 Overview

Bare-metal C (no RTOS needed), ~8KB flash usage of 16KB available.

### 8.2 Main Loop (1ms tick via SysTick)

```
Every 1ms:
  ├── Read ADC DMA buffer (4× current + TEMP + REF cal)
  ├── For each channel:
  │     ├── Track peak current (max since last Modbus read)
  │     ├── Detect pulse edges (rising/falling) → compute pulse width
  │     ├── Accumulate energy: E += V × I × dt
  │     └── Fault detection:
  │           ├── Overcurrent: peak > threshold (configurable per channel)
  │           ├── Open circuit: drive signal HIGH but current < 50mA for > 1ms
  │           ├── Short circuit: current > 100mA with no drive signal
  │           └── Thermal: board temp > threshold
  ├── Every 100ms:
  │     └── Update health score, temperature register
  └── Modbus UART interrupt handler (runs asynchronously)
```

### 8.3 Modbus Slave Implementation

- UART1 at 9600 baud (PA9 TX, PA10 RX)
- PA4 controls SP3485 DE/RE (HIGH = transmit, LOW = receive)
- Supports function codes 0x03 (read holding) and 0x04 (read input)
- CRC-16 validation on all frames
- Response timeout: 3.5 character times (per Modbus spec)
- Library option: lightweight bare-metal implementation (~500 lines) or `libmodbus` STM32 port

### 8.4 ADC Calibration Sequence (on boot)

```c
void calibrate_adc(void) {
    // 1. Read VREFINT channel (internal 1.23V reference)
    uint16_t vrefint_raw = read_adc(ADC_CH_VREFINT);
    float vdda_from_vrefint = 3.3 * (*VREFINT_CAL) / vrefint_raw;

    // 2. Read REF5050 calibration channel (external 2.500V known)
    uint16_t ref_raw = read_adc(ADC_CH_REF);
    float vdda_from_ref = 2.500 * 4096.0 / ref_raw;

    // 3. Use REF5050 value (more accurate than VREFINT)
    g_vdda = vdda_from_ref;

    // 4. Verify: both should agree within ~2%
    float diff_pct = fabs(vdda_from_vrefint - vdda_from_ref) / vdda_from_ref * 100;
    if (diff_pct > 5.0) set_fault(FAULT_ADC_CAL);
}
```

---

## 9. PCB Design Considerations

### 9.1 Layout Guidelines

- **2-layer PCB**, ~60×80mm board size
- **Power MOSFET area**: Wide traces (≥2mm for 10A drain path), short source-to-shunt path
- **Kelvin connection** on shunt resistors: 4-wire sense to INA180A1 inputs, separate from power traces
- **PTC fuse / shunt separation**: Place PTC fuses near J1 input connector at board edge. Place sense shunts near MOSFETs in board center. Minimum 10mm separation with ground plane thermal break between them. PTC self-heating near trip point causes thermal drift in nearby sense resistors.
- **Ground partitioning**: Star ground topology — analog sense ground and power return ground meet at one point
- **Gate traces**: Short, direct from UCC27524A to MOSFET gate pad. 100Ω series resistor placed close to gate.
- **Decoupling**: 100nF ceramic within 5mm of every IC VDD pin
- **RS-485 traces**: A/B as differential pair, short stub to SP3485
- **TO-220 sockets**: Aligned along one board edge for easy access. Consider screw-mount heatsink bar option.

### 9.2 Connector Placement

```
┌──────────────────────────────────────────┐
│  J5(SWD)  J6(BOOT)                       │
│                                          │
│  ┌──────┐  ┌──────┐  ┌──────┐  ┌──────┐ │
│  │ Q1   │  │ Q2   │  │ Q3   │  │ Q4   │ │  ← TO-220 sockets
│  │TO-220│  │TO-220│  │TO-220│  │TO-220│ │
│  └──────┘  └──────┘  └──────┘  └──────┘ │
│                                          │
│  [INA180×4] [UCC27524×2] [STM32]        │
│                                          │
│  [TPS7A4701] [REF5050] [AMS1117]        │
│  [SP3485]                                │
│                                          │
│  J1(12V)  J2(Control)  J3(Output)  J4(485)│
└──────────────────────────────────────────┘
```

### 9.3 Conformal Coating

Recommended for engine bay environment. Exclude TO-220 sockets and connectors from coating.

---

## 10. Failure Mode Analysis

| Failure | Detection | Response | Safe? |
|---------|-----------|----------|-------|
| ESP-ECU power loss | EN pin drops LOW (10kΩ pulldown) | P-FET kills all outputs | ✓ |
| EN wire broken | EN pulled LOW | All outputs OFF | ✓ |
| Control wire broken | Input pulldown (10kΩ) holds LOW | Affected channel OFF | ✓ |
| Control wire shorted to 12V | Gate driver drives MOSFET ON continuously | Overcurrent fault detected by INA180, reported via Modbus | Partial — depends on load |
| MOSFET fails short | Continuous current flow | INA180 detects current without drive signal → short-circuit fault | Module reports fault |
| MOSFET fails open | No current when driven | INA180 detects open-circuit (0mA with drive signal) | ✓ (no output) |
| Gate driver fails | Outputs float, gate pulldown discharges | MOSFET turns OFF within ~85µs | ✓ |
| STM32 hangs | Modbus stops responding | ESP-ECU marks module offline after 3 missed polls | Outputs still controlled by direct signals (unaffected) |
| RS-485 bus failure | No communication | ESP-ECU loses diagnostics, but outputs still work (direct wired) | Outputs work, diagnostics lost |
| 12V bus power loss | Gate drivers lose VDD, MCU loses power | All MOSFETs turn OFF (gate pulldowns), diagnostics stop | ✓ |
| Board overheating | REF5050 TEMP exceeds threshold | STM32 sets thermal fault, reports via Modbus | ✓ |

**Key safety property:** The control path (ESP-ECU → wire → gate driver → MOSFET) is independent of the diagnostic path (Modbus/RS-485). Loss of diagnostics does not affect control. Loss of control (EN=LOW or wire break) always results in outputs OFF.

---

## 11. Complete Updated BOM (Per Module)

### Active Components

| Ref | Qty | Part | Value/Description | Package |
|-----|-----|------|-------------------|---------|
| U1 | 1 | STM32F030F4P6TR | Cortex-M0 MCU 48MHz | TSSOP-20 |
| U2,U3 | 2 | UCC27524ADBVR | Dual 5A gate driver | SOT-23-6 |
| U4-U7 | 4 | INA180A1IDBVR | 20V/V current sense amp | SOT-23-5 |
| U8 | 1 | SP3485EN-L/TR | 3.3V RS-485 transceiver | SOIC-8 |
| U9 | 1 | TPS7A4701RGTR | Ultra-low-noise LDO → 5.0V analog | QFN-20 |
| U10 | 1 | REF5050AIDR | 5.000V precision VREF + TEMP | SOIC-8 |
| U11 | 1 | AMS1117-3.3 | 3.3V LDO | SOT-223 |

### Variant A+B: Injector / COP-with-igniter

| Ref | Qty | Part | Notes |
|-----|-----|------|-------|
| Q1-Q4 | 4 | IRLZ44NPBF | 55V 47A 22mΩ logic-level N-FET, TO-220 |
| R1-R4 | 4 | 100mΩ 1% 2512 | Current sense shunt, 1W |
| D1-D4 | 4 | SMBJ33A | 33V TVS flyback clamp |

### Variant C: Direct Coil Drive

| Ref | Qty | Part | Notes |
|-----|-----|------|-------|
| Q1-Q4 | 4 | IRFB4115PBF | 150V 104A 11mΩ N-FET, TO-220 |
| R1-R4 | 4 | 10mΩ 1% 2512 | Current sense shunt, 2W |
| D1-D4 | 4 | SMBJ80A | 80V TVS flyback clamp |

### Common Components (All Variants)

| Ref | Qty | Part | Notes |
|-----|-----|------|-------|
| Q5 | 1 | DMP6023LE-7 | P-FET -60V -3.3A enable switch, SOT-23 |
| Q6 | 1 | 2N7002 | N-FET enable driver, SOT-23 |
| D5-D8 | 4 | SS54 | 5A 40V Schottky flyback freewheel, SMA |
| Z1-Z4 | 4 | BZT52C15S | 15V zener gate clamp, SOD-323 |
| R5-R8 | 4 | 10kΩ 0402 | MOSFET gate pulldown |
| R9-R12 | 4 | 100Ω 0402 | Gate series resistor (dV/dt) |
| R13-R16 | 4 | 10kΩ 0402 | Control input pulldown |
| R17 | 1 | 10kΩ 0402 | STM32 NRST pullup |
| R18 | 1 | 10kΩ 0402 | STM32 BOOT0 pulldown |
| R19 | 1 | 120Ω 0402 | RS-485 termination (jumper select) |
| R20 | 1 | 470Ω 0402 | Status LED resistor |
| R21,R22 | 2 | 560Ω 0603 | RS-485 bias resistors |
| R23 | 1 | 10kΩ 0402 | Q5 gate pullup (default OFF) |
| R24 | 1 | 10kΩ 0402 | EN input pulldown |
| R25 | 1 | 100Ω 0402 | Q6 gate series |
| R26 | 1 | 33Ω 0402 | REF5050 input filter |
| R27,R28 | 2 | 10.0kΩ 0.1% 0402 | REF5050 output divider (calibration) |
| R29-R36 | 8 | 10.0kΩ 0.1% 0402 | INA180 output dividers (4× 2:1 pairs) |
| R37 | 1 | 25.5kΩ 0.1% 0402 | TPS7A4701 R1 (VOUT set) |
| R38 | 1 | 10.0kΩ 0.1% 0402 | TPS7A4701 R2 (VOUT set) |
| C1-C10 | 10 | 100nF 0402 | IC decoupling |
| C11,C12 | 2 | 10µF 0805 | AMS1117 in/out, 3.3V bulk |
| C13 | 1 | 10µF 0805 | TPS7A4701 output |
| C14 | 1 | 10µF 0805 | TPS7A4701 input |
| C15 | 1 | 1µF 0402 | REF5050 output |
| C16 | 1 | 100nF 0402 | REF5050 NR pin (noise reduction) |
| C17 | 1 | 100µF/25V 8×10mm | 12V bus input bulk |
| C18-C21 | 4 | 100pF 0402 | ADC divider anti-aliasing caps |
| C22 | 1 | 100nF 0402 | REF5050 TEMP filter cap |
| LED1 | 1 | Green 0603 | Status/heartbeat |
| SW1 | 1 | DSWB03LHGET | 3-pos DIP, Modbus address |
| F1,F2 | 2 | SMD1812P185TF (or sized per variant) | Resettable PTC fuse, parallel pair, 1812 |
| J1 | 1 | 2-pin 5.08mm screw | Power: ISEN− (battery, post-sense), GND |
| J2 | 1 | 7-pin 2.54mm header | Control: EN, CH1-4, +3.3V, GND |
| J3 | 1 | 5-pin 5.08mm screw | Output: OUT1-4, GND |
| J4 | 1 | 4-pin 2.54mm header | RS-485 bus: A, B, +12V, GND |
| J5 | 1 | 4-pin 1.27mm header | SWD: SWDIO, SWCLK, 3.3V, GND |
| SK1-SK4 | 4 | TO-220 socket | MOSFET socket (optional) |

**Total component count:** ~75 unique parts, ~95 placements

---

## 12. Open Items

1. **RS-485 module modification:** Current RJ45 pinout carries only A/B data + AGND. Need to add +12V power pin to the bus connector to distribute TPS61089 12V to driver modules. Allocate one unused RJ45 pair for 12V+GND.

2. **FltVout distribution design:** Need a breakout PCB or wiring harness to distribute ISEN− to individual module PTC fuses. Could be a simple 4-way fused distribution block on the ECU or a small breakout board near the modules.

3. **F1 main fuse sizing:** Verify F1 can handle total system current (estimated 3.5A average, 12A peak for V8). 15A automotive blade fuse recommended. Monitor via U5 (LM25118) current sense if possible.

4. **U5 (LM25118) current monitoring integration:** If the LM25118 current sense output is accessible, add an ADC channel or comparator to the ESP-ECU to track total system draw. This enables early warning of module faults before PTC fuses trip.

5. **ESP-ECU enable pin allocation:** Need one MCP23S17 expander pin per enable line. For V8 (4 modules), use 4 expander pins, or share enable across module pairs (2 enable lines for 4 modules).

6. **STM32 firmware:** Bare-metal Modbus slave implementation needed. Can start from open-source STM32 Modbus libraries (e.g., `liblightmodbus` or custom minimal implementation).

7. **IRFB4115PBF availability:** Verify LCSC/JLCPCB stock. Alternatives: IRFB4110PBF (100V, less margin but more common), IPP100N06S4-02 (60V, insufficient for direct coil).

8. **PCB thermal simulation:** The AMS1117 at 305mW in SOT-223 runs warm (112°C junction at 85°C ambient). Consider upgrading to AP2112K-3.3 (SOT-23-5, lower dropout → less Pdiss) or adding 12V→5V pre-regulator to reduce AMS1117 headroom.

9. **Sense resistor thermal validation:** After PCB layout, verify that PTC fuse self-heating (especially near trip) does not affect nearby sense shunts. Target ≥10mm separation with ground plane thermal break. If thermal coupling is observed, consider dropping to 50mΩ / 5mΩ shunts.
