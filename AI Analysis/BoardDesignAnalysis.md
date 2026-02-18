# ESP-ECU Board Design Analysis — PCB Stackup, Trace Sizing, and Ground Strategy

**Date:** 2026-02-18
**Context:** 6-layer mixed-signal ESP-ECU main board + modular daughter boards with variant substrates
**References:** IPC-2221 (trace width), IPC-2152 (updated trace), Analog Devices MT-031 (grounding), Saturn PCB Toolkit

---

## 1. Board Ecosystem Overview

The ESP-ECU system is not a single PCB — it is a family of boards, each optimized for its role:

| Board | Layers | Substrate | Copper | Purpose |
|-------|--------|-----------|--------|---------|
| **ESP-ECU Main** | 6 | FR4 (1.6mm) | 1oz all layers | MCU, sensors, SPI expanders, comms, analog front-end |
| **Power Supply** | 4 | FR4 (1.6mm) | 2oz inner power/GND | LM25118 buck-boost, F1 fuse, ISEN, PTC distribution |
| **Driver Module (Inj/COP)** | 2 | **Aluminum core (MCPCB)** | 2oz top, Al base | MOSFET switching, current sense, gate drivers — thermal priority |
| **Driver Module (Direct Coil)** | 2 | **Aluminum core (MCPCB)** | 2oz top, Al base | High-current MOSFET, needs maximum heat spreading |
| **VR Conditioner** | 2 | FR4 (1.0mm) | 1oz | Signal conditioning only, minimal thermal load |
| **RS-485 Breakout** | 2 | FR4 (1.0mm) | 1oz | TPS61089 boost, SP3485, RJ45 connectors |

**Design philosophy:** Each board uses the minimum complexity and optimal substrate for its job. The ESP-ECU main board needs 6 layers for signal integrity and ground plane management. The driver modules need aluminum substrate for heat extraction. The VR conditioner is simple enough for 2-layer FR4.

---

## 2. ESP-ECU Main Board — 6-Layer Stackup

### 2.1 Layer Assignment

```
Layer  │ Assignment              │ Copper │ Dielectric        │ Notes
───────┼─────────────────────────┼────────┼───────────────────┼──────────────────────────────
 L1    │ Signal (Top)            │ 1 oz   │                   │ Components, high-speed digital, ESP32 fanout
       │                         │        │ Prepreg 4-5 mil   │ Thin — controlled impedance to L2
 L2    │ Ground Plane            │ 1 oz   │                   │ SOLID — primary reference for L1 signals
       │                         │        │ Core 40 mil       │ Structural core
 L3    │ Power Plane (split)     │ 1 oz   │                   │ 3.3V / 5V / 6.8V / 12V zones
       │                         │        │ Prepreg 4-5 mil   │ Thin — interplane capacitance with L4
 L4    │ Signal (Inner)          │ 1 oz   │                   │ Low-speed routing, analog, power traces
       │                         │        │ Core 40 mil       │ Structural core
 L5    │ Ground Plane            │ 1 oz   │                   │ SOLID — primary reference for L6 signals
       │                         │        │ Prepreg 4-5 mil   │ Thin — controlled impedance to L6
 L6    │ Signal (Bottom)         │ 1 oz   │                   │ Components, I/O connectors, debug headers
```

**Total thickness:** ~1.6mm (63 mil) standard

**Symmetry:** L1/L6 mirror (signal), L2/L5 mirror (ground), L3/L4 mirror (power/signal). Symmetric stackup prevents board warping during reflow — critical for automotive reliability.

### 2.2 Why This Stackup

1. **L1 signals reference L2 (GND)** — 4-5 mil prepreg gives ~50Ω microstrip with 7 mil traces. ESP32-S3 SPI at 50 MHz (SD card) and 10 MHz (HSPI expanders) route here with a clean ground return directly below.

2. **L2 (GND) is unbroken** — No splits in this layer. It is the primary reference plane for all L1 signals. Return currents flow directly under their signal traces (path of least impedance at high frequency).

3. **L3 (Power) split into voltage zones:**
   - 3.3V zone (under ESP32, TXB0108 level shifters)
   - 5V zone (under MCP23S17 expanders, ADS1115, MCP3204, REF5050)
   - 6.8V zone (under LGS5145 buck output, feeds REF5050 and AMS1117-5.0)
   - 12V zone (under heater driver BTS3134, alternator driver, connector area)

4. **L4 (Inner signal)** references L5 (GND) — Route analog sensor traces and slow digital signals here. The inner layer has better shielding from external EMI (sandwiched between ground planes).

5. **L5 (GND) is unbroken** — Second solid ground reference for L4 and L6 signals.

6. **L6 (Bottom)** — Secondary component placement, I/O connectors, debug headers, power distribution traces. References L5 ground.

### 2.3 Controlled Impedance

For FR4 (Dk ≈ 4.2 at 1 GHz):

| Target | Topology | Trace Width | Gap | Dielectric | Use Case |
|--------|----------|-------------|-----|------------|----------|
| 50Ω single-ended | Microstrip (L1 over L2) | 7 mil | — | 4-5 mil prepreg | SPI CLK/MOSI/MISO, SD card bus |
| 50Ω single-ended | Stripline (L4 between L3/L5) | 5 mil | — | ~5 mil each side | Analog sensor traces |
| 100Ω differential | Edge-coupled microstrip (L1) | 5 mil trace | 5 mil gap | 4-5 mil prepreg | RS-485 A/B differential pair |
| 90Ω differential | Edge-coupled microstrip (L1) | 5 mil trace | 6 mil gap | 4-5 mil prepreg | USB (if used) |

**Always have the fab house run their impedance solver** (Polar Si9000 or equivalent) against their actual laminate specs — Dk varies by manufacturer, resin content, and frequency.

---

## 3. Ground Plane Strategy

### 3.1 Approach: Unified Ground with Zone Discipline

The ESP-ECU uses a **single, unbroken ground plane** on L2 and L5, with careful component placement to create natural analog and digital zones. No physical splits — zone discipline is maintained by placement and routing rules.

**Why unified (not split):**
- A split forces return currents to detour around the gap, increasing loop area and EMI
- Any signal trace routed across a split creates a slot antenna
- At high frequency, return current naturally follows the path directly under the trace — a solid plane confines it automatically
- Analog Devices (MT-031) recommends unified ground for most mixed-signal designs

### 3.2 Zone Layout

```
┌──────────────────────────────────────────────────────────────────┐
│                         ESP-ECU Main Board                       │
│                                                                  │
│  ┌─────────────────────┐    ┌──────────────────────────────┐    │
│  │   ANALOG ZONE       │    │   DIGITAL ZONE                │    │
│  │                     │    │                                │    │
│  │  CJ125 front-end    │    │  ESP32-S3 (WROOM module)      │    │
│  │  ADS1115 × 3        │    │  MCP23S17 × 6 (HSPI)         │    │
│  │  MCP3204 (SPI ADC)  │    │  SD card (FSPI)               │    │
│  │  REF5050 × 2        │    │  WiFi antenna keepout          │    │
│  │  NTC dividers (CLT,IAT)│ │  TXB0108 level shifters       │    │
│  │  VBAT divider        │    │  L2N7002 MOSFET shifters      │    │
│  │  Op-amp buffers      │    │  USB connector                 │    │
│  │  (MCP6002 for 5V→3.3V)│  │                                │    │
│  │                     │    │                                │    │
│  └─────────┬───────────┘    └──────────────┬─────────────────┘    │
│            │                               │                      │
│            │      TRANSITION ZONE          │                      │
│            │  (ADC boundary, I2C/SPI       │                      │
│            │   crossings, ferrite bead)    │                      │
│            └───────────────────────────────┘                      │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐    │
│  │   POWER ZONE (board edge, near connectors)               │    │
│  │                                                          │    │
│  │  12V input, BTS3134 heater drivers, alternator PWM       │    │
│  │  LEDC outputs (GPIO19, 20, 41, 45, 46)                  │    │
│  │  Fuel pump relay driver, power connectors                │    │
│  │  LGS5145 buck, AMS1117-5.0 LDO                          │    │
│  └──────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────┘
```

### 3.3 Zone Rules

| Rule | Description |
|------|-------------|
| **Never cross zones with fast signals** | Don't route a 10 MHz SPI trace over the analog zone. Don't route an analog sensor trace over the digital zone. |
| **Transition at boundaries** | Slow signals (I2C at 400 kHz, UART) can cross the transition zone. Place ferrite beads at the crossing. |
| **ADCs sit at the boundary** | The ESP32-S3 ADC inputs (GPIO 3-9) connect to filter components placed in the analog zone, with short traces. |
| **Decoupling stays in-zone** | Analog IC decoupling caps connect to the ground plane in the analog zone. Digital IC caps connect in the digital zone. |
| **Power entry in the power zone** | 12V, heater drivers, and high-current PWM outputs are grouped at the board edge near connectors. |

### 3.4 AGND Ferrite Bead Isolation

The existing design uses a **Murata BLM21PG221SN1D** ferrite bead (220Ω @ 100 MHz, 45mΩ DCR) between AGND and DGND:

```
DGND plane ────[BLM21PG221SN1D]──── AGND node
                                       │
                                    [100nF]  high-frequency bypass
                                       │
                                    [22µF]   transient decoupling
                                       │
                                      GND
```

This provides lossy broadband suppression without the LC resonance of a lumped inductor. The ferrite bead sits at the transition zone boundary.

### 3.5 Ground Stitching Vias

**Every signal via needs a nearby ground via.** When a signal changes reference layers (e.g., L1→L4 via), the return current must also transition between L2 (GND) and L5 (GND).

| Rule | Specification |
|------|---------------|
| Ground via distance from signal via | ≤ 50 mil (1.27mm) |
| Ground stitching via spacing along board edges | Every 5mm (200 mil) |
| Ground stitching around zone boundaries | Every 3mm (120 mil) |
| Ground stitching around high-speed ICs | Every 2mm (80 mil) perimeter |

---

## 4. Trace Width Calculations — IPC-2221

### 4.1 Formula

```
I = k × ΔT^0.44 × A^0.725

Where:
  I  = current (Amps)
  k  = 0.048 (external layer) or 0.024 (internal layer)
  ΔT = temperature rise above ambient (°C)
  A  = cross-sectional area (mil²) = width(mil) × thickness(mil)

For 1oz copper: thickness = 1.378 mil (35µm)
For 2oz copper: thickness = 2.756 mil (70µm)
```

Rearranged to solve for width:

```
A = (I / (k × ΔT^0.44))^(1/0.725)
Width = A / thickness
```

### 4.2 Trace Width Tables — 1oz Copper (35µm)

**External layer (k = 0.048):**

| Current | 10°C Rise | 20°C Rise | 30°C Rise |
|---------|-----------|-----------|-----------|
| 0.5A | 5 mil (0.13mm) | 3 mil (0.08mm) | 3 mil (0.08mm) |
| 1.0A | 10 mil (0.25mm) | 7 mil (0.18mm) | 5 mil (0.13mm) |
| 2.0A | 30 mil (0.76mm) | 20 mil (0.51mm) | 15 mil (0.38mm) |
| 3.0A | 50 mil (1.27mm) | 35 mil (0.89mm) | 27 mil (0.69mm) |
| 5.0A | 110 mil (2.79mm) | 75 mil (1.91mm) | 58 mil (1.47mm) |
| 10.0A | 300 mil (7.62mm) | 200 mil (5.08mm) | 155 mil (3.94mm) |

**Internal layer (k = 0.024):**

| Current | 10°C Rise | 20°C Rise | 30°C Rise |
|---------|-----------|-----------|-----------|
| 0.5A | 15 mil (0.38mm) | 10 mil (0.25mm) | 8 mil (0.20mm) |
| 1.0A | 35 mil (0.89mm) | 22 mil (0.56mm) | 17 mil (0.43mm) |
| 2.0A | 90 mil (2.29mm) | 60 mil (1.52mm) | 47 mil (1.19mm) |
| 3.0A | 160 mil (4.06mm) | 105 mil (2.67mm) | 82 mil (2.08mm) |
| 5.0A | 330 mil (8.38mm) | 220 mil (5.59mm) | 170 mil (4.32mm) |
| 10.0A | 1000+ mil (25+mm) | 660 mil (17mm) | 510 mil (13mm) |

**Key observation:** Internal layers need ~2-3× the width of external layers for the same current. Above 3A on internal layers, trace widths become impractical — use copper pours or multi-layer stitching instead.

### 4.3 Trace Width Tables — 2oz Copper (70µm)

**External layer (k = 0.048):**

| Current | 10°C Rise | 20°C Rise |
|---------|-----------|-----------|
| 5.0A | 55 mil (1.40mm) | 38 mil (0.97mm) |
| 10.0A | 150 mil (3.81mm) | 100 mil (2.54mm) |
| 15.0A | 270 mil (6.86mm) | 180 mil (4.57mm) |

2oz copper halves the trace width for the same current — important for the power supply board and driver module traces.

### 4.4 Automotive Temperature Derating

Copper resistance increases with temperature. At 85°C ambient (engine bay):

```
ρ(85°C) = ρ(20°C) × (1 + 0.00393 × (85 - 20)) = 1.255 × ρ(20°C)
```

**25.5% increase in resistance.** For automotive under-hood designs:
- Derate current capacity by 25%, OR
- Design for only 10°C rise instead of 20°C, OR
- Use one size up from the calculated trace width

**Recommendation:** Use the 10°C rise column for all automotive traces. This provides built-in margin for elevated ambient temperatures.

---

## 5. ESP-ECU Trace Sizing Guide

### 5.1 Signal Categories and Trace Widths

| Signal | Current | Layer | Width | Via Count | Notes |
|--------|---------|-------|-------|-----------|-------|
| **Analog inputs** (GPIO 3-9) | <1mA | L1 → L4 | 8 mil | 1 + GND stitch | Guard ring on both sides, 10 mil clearance |
| **I2C bus** (GPIO 0, 42) | <10mA | L1 | 8 mil | — | Ferrite-damped at zone boundary |
| **HSPI bus** (GPIO 10-13) | <50mA | L1 | 10 mil | — | Short stubs to MCP23S17, 50Ω impedance |
| **FSPI bus** (GPIO 38, 47, 48, 39) | <100mA | L1 | 12 mil | — | 50 MHz SD card, controlled impedance |
| **Crank/Cam** (GPIO 1, 2) | <1mA | L1 | 8 mil | 1 + GND stitch | Post-level-shifter, short trace to ESP32 |
| **MCP23S17 outputs** (coils, inj) | <25mA | L1/L6 | 10 mil | — | Logic-level to level shifters/connectors |
| **Fuel pump relay** (MCP#0 P0) | ~200mA | L1/L6 | 20 mil | 2 vias | Relay coil drive, not motor current |
| **Tach output** (MCP#0 P1) | ~100mA | L1/L6 | 15 mil | 1 via | Gauge driver |
| **CEL output** (MCP#0 P2) | ~50mA | L1/L6 | 10 mil | 1 via | LED driver |
| **TCC solenoid** (GPIO 45) | 1-3A | L1 | 50 mil | 5 vias | LEDC 200 Hz, through MOSFET driver |
| **EPC solenoid** (GPIO 46) | 1-2A | L1 | 35 mil | 3 vias | LEDC 5 kHz, through MOSFET driver |
| **Alternator field** (GPIO 41) | 3-7A | L1 + L6 | 60 mil × 2 layers | 8 vias | Multi-layer stitched, 25 kHz PWM |
| **Heater PWM 1** (GPIO 19) | 2-3A (gate) | L1 | 50 mil | 5 vias | BTS3134 gate drive (BTS3134 handles load current) |
| **Heater PWM 2** (GPIO 20) | 2-3A (gate) | L1 | 50 mil | 5 vias | BTS3134 gate drive |
| **12V input bus** | 5-10A total | L1 + L6 pour | 200 mil pour | 15+ vias | Copper pour, not trace — both sides stitched |
| **5V power rail** | ~500mA | L3 plane | Zone pour | — | Distributed via L3 power plane |
| **3.3V power rail** | ~300mA | L3 plane | Zone pour | — | Distributed via L3 power plane |
| **GND return** | All | L2 + L5 planes | Full pour | Stitching grid | Unbroken solid pours |

### 5.2 Multi-Layer Stitching for High-Current Traces

For traces carrying >3A, route on **multiple layers simultaneously** connected by via arrays:

```
Example: Alternator field trace (5A)

L1:  ====[via array]====== 60 mil trace ======[via array]====
                ↕                                    ↕
L6:  ====[via array]====== 60 mil trace ======[via array]====

Via array: 8 vias in staggered 2×4 grid, 1.0mm pitch
Each via: 0.3mm drill, 25µm plating = ~0.5A per via at 10°C rise
8 vias × 0.5A = 4A via capacity (adequate for 5A with margin from parallel traces)
```

**Via current capacity (standard 0.3mm drill, 25µm plating):**

| Temperature Rise | Current per Via | Notes |
|-----------------|----------------|-------|
| 10°C | ~0.5A | Conservative, use for automotive |
| 20°C | ~0.7A | Standard |
| 30°C | ~0.8A | Acceptable for benign environments |

**Via count guide:**

| Total Current | Vias Needed (10°C rise) | Array Pattern |
|---------------|------------------------|---------------|
| 1A | 3 | 1×3 line |
| 2A | 5 | staggered 2×3 |
| 3A | 7 | staggered 2×4 |
| 5A | 12 | staggered 3×4 |
| 10A | 25 | staggered 5×5 |

**Always add 30% margin** for automotive derating.

### 5.3 Via Array Design Rules

| Rule | Specification |
|------|---------------|
| Minimum via-to-via pitch | 1.0mm (40 mil) center-to-center |
| Preferred drill size | 0.3mm (12 mil) finished hole |
| Plating thickness | 25µm standard, 35-50µm for high-current |
| Pattern | Staggered grid for uniform current distribution |
| Placement | Distributed along full trace transition, not clustered |

---

## 6. Copper Specifications

### 6.1 Properties

| Property | 1oz Copper | 2oz Copper |
|----------|-----------|-----------|
| Thickness | 35µm (1.378 mil) | 70µm (2.756 mil) |
| Sheet resistance | 0.49 mΩ/square | 0.245 mΩ/square |
| Resistivity (20°C) | 1.724 µΩ·cm | 1.724 µΩ·cm |
| Thermal conductivity | 385 W/m·K | 385 W/m·K |
| Temp coefficient | +0.393%/°C | +0.393%/°C |
| Min trace/space | 4/4 mil typical | 5/5 or 6/6 mil (harder to etch) |
| Cost premium | Baseline | +15-30% |

### 6.2 Trace Resistance Example

A 50mm long, 0.5mm (20 mil) wide, 1oz trace:
- Number of squares: 50mm / 0.5mm = 100
- Resistance: 0.49 mΩ × 100 = **49 mΩ**
- At 2A: voltage drop = 98mV, power dissipation = 196mW

At 85°C ambient: resistance increases to 49 × 1.255 = **61.5 mΩ** (123mV drop at 2A).

### 6.3 When to Use 2oz

| Situation | Recommendation |
|-----------|---------------|
| Signal traces (<1A) | 1oz — adequate, better etch resolution |
| Power traces (1-3A) on external layers | 1oz with wider traces (50+ mil) |
| Power traces (3-5A) | 2oz or multi-layer stitching with 1oz |
| Power traces (>5A) | 2oz + copper pour + via stitching |
| Power plane (L3) | Consider 2oz for 12V zone if carrying heater/alternator current |
| Ground planes (L2, L5) | 1oz — solid pour, very low effective resistance |
| Driver module boards | 2oz — high-current MOSFET paths, thermal mass helps |

---

## 7. Module-Specific Board Designs

### 7.1 Driver Module — Aluminum Substrate (MCPCB)

The driver modules (coil/injector) switch significant power through TO-220 MOSFETs. While the per-channel power dissipation is modest (22mW for injector, 94mW for direct coil), the **PTC fuses, sense resistors, LDOs, and enable P-FET** all contribute heat in a small area. An aluminum substrate provides:

- **Thermal conductivity 1-5 W/m·K** (vs FR4 at 0.25 W/m·K) — 4-20× better heat spreading
- **Metal base plate** acts as a built-in heatsink
- **Even temperature distribution** — prevents hot spots near sense resistors and PTC fuses
- **Direct mounting** to enclosure or heatsink bar without additional thermal interface

**MCPCB stackup (driver module):**

```
Layer        │ Material          │ Thickness │ Notes
─────────────┼───────────────────┼───────────┼──────────────────────
Copper (top) │ 2oz copper        │ 70µm      │ Traces, pads, components
Dielectric   │ Thermal prepreg   │ 75-150µm  │ 1-5 W/m·K (key spec)
Aluminum     │ 5052 or 6061 Al   │ 1.0-1.6mm │ Heat spreader / base plate
```

**Dielectric selection:**

| Thermal Conductivity | Cost | Application |
|---------------------|------|-------------|
| 1.0 W/m·K | Low | Standard LED boards, adequate for injector variant |
| 2.0 W/m·K | Medium | Recommended for COP variant |
| 3.0-5.0 W/m·K | High | Direct coil variant (10A per channel, 370mW peak per MOSFET) |

**Thermal resistance comparison:**

| Parameter | FR4 (1.6mm) | MCPCB (1.0 W/m·K) | MCPCB (3.0 W/m·K) |
|-----------|-------------|--------------------|--------------------|
| θ board (junction to base) | ~100°C/W | ~15°C/W | ~5°C/W |
| ΔT at 1W total | +100°C | +15°C | +5°C |
| ΔT at 5W total | +500°C (impossible) | +75°C | +25°C |

For the direct coil variant (worst case: 4 channels × 370mW peak = 1.48W MOSFET + 4W sense shunt + LDO heat ≈ 6-7W total):
- FR4: ΔT = 600-700°C → **not viable without massive heatsink**
- MCPCB 1.0 W/m·K: ΔT = ~100°C → marginal at 85°C ambient
- MCPCB 3.0 W/m·K: ΔT = ~35°C → 85°C + 35°C = **120°C** junction, within limits

**Recommendation:** Use 3.0 W/m·K MCPCB for direct coil variant, 1.0-2.0 W/m·K for injector/COP variant.

**Layout considerations for MCPCB:**

| Rule | Specification |
|------|---------------|
| Single copper layer (top only) | Aluminum base is not routable — it's a thermal pad |
| Via isolation | No plated through-holes to aluminum (shorts to base). Use thermal vias only if intentional grounding to base. |
| Ground connection to base | Optional: one or more vias connecting GND copper to aluminum base for electrical grounding + thermal path |
| Component clearance from board edge | ≥2mm (aluminum base is cut, leaving exposed edge) |
| Scoring/routing | V-scoring preferred; milling possible but generates aluminum chips |
| Solder mask | Standard — but note MCPCB dielectric has lower Tg than FR4 |

**MOSFET thermal path on MCPCB:**

```
MOSFET junction (TO-220 tab = drain)
    ↓ ~1°C/W (junction to case)
TO-220 tab / socket
    ↓ ~0.5°C/W (thermal pad if used)
2oz copper pad (top layer)
    ↓ ~3-5°C/W (through thermal dielectric)
Aluminum base plate
    ↓ ~1-2°C/W (to enclosure or heatsink)
Ambient air
```

Total θJA ≈ 6-9°C/W on MCPCB vs 60-90°C/W on FR4. An order of magnitude improvement.

### 7.2 Driver Module — Trace Sizing (2oz MCPCB, External Only)

Single-layer MCPCB — all routing on the top copper layer:

| Trace | Current | Width (2oz, 10°C rise) | Notes |
|-------|---------|----------------------|-------|
| MOSFET drain (12V_LOAD to coil connector) | 2-10A per channel | 60-150 mil | Copper pour preferred |
| MOSFET source to shunt | 2-10A per channel | 60-150 mil | Kelvin 4-wire to INA180 |
| Shunt to GND | 2-10A per channel | 60-150 mil | Wide pour, star ground |
| INA180 output to divider to STM32 ADC | <1mA | 8 mil | Keep away from power traces |
| UCC27524A output to MOSFET gate | <100mA (peak 5A for 12ns) | 15 mil | Short, direct, 100Ω series R close to gate |
| Control inputs (EN, CH1-4) | <10mA | 10 mil | Input pulldowns to GND |
| SP3485 RS-485 A/B | <50mA | 10 mil | Differential pair routing |
| 12V bus input to regulators | ~100mA | 20 mil | Power distribution |
| 5V analog rail | ~50mA | 15 mil | Clean routing, away from power |
| 3.3V digital rail | ~35mA | 10 mil | Local to STM32 + SP3485 |
| P-FET enable circuit | <10mA | 10 mil | Q5/Q6/R23/R24/R25 area |
| PTC fuse to P-FET to load | 2-10A | 100-200 mil | Wide pour, thermal isolation from shunts |

### 7.3 VR Conditioner Module — 2-Layer FR4

Simple board, minimal thermal load:

| Layer | Assignment |
|-------|-----------|
| Top | Components, signal routing, ground pour |
| Bottom | Ground pour (near-solid), power distribution |

- **Board size:** 25×15mm (Gen 1) to 30×20mm (Gen 2)
- **Copper:** 1oz both layers
- **Substrate:** FR4 1.0mm (thinner = smaller package)
- **Controlled impedance:** Not needed (all signals <5 MHz)
- **All traces:** 8-10 mil (signal current <50mA everywhere)

### 7.4 Power Supply Board — 4-Layer FR4

The LM25118 buck-boost converter, F1 fuse, ISEN shunt, SMBJ36A TVS, and PTC distribution:

| Layer | Assignment | Copper |
|-------|-----------|--------|
| L1 (Top) | Components, high-current traces | 2oz |
| L2 | Ground plane (solid) | 2oz |
| L3 | Power plane (12V input, regulated output zones) | 2oz |
| L4 (Bottom) | Components, routing, ground pour | 2oz |

**2oz copper on all layers** — this board handles the full system current (3.5A average, 12A peak for V8). The LM25118 inductor, input/output capacitors, and sense resistor all carry high-current pulses at the switching frequency.

---

## 8. Power Distribution — ESP-ECU Main Board

### 8.1 Power Rail Routing on L3

L3 (Power plane) is split into voltage zones. Each zone is a copper pour, not individual traces:

```
┌──────────────────────────────────────────────┐
│                    L3 (Power)                 │
│                                              │
│  ┌────────┐  ┌────────┐  ┌───────┐  ┌────┐ │
│  │  3.3V  │  │  5.0V  │  │ 6.8V  │  │12V │ │
│  │  zone  │  │  zone  │  │ zone  │  │zone│ │
│  │        │  │        │  │       │  │    │ │
│  │ ESP32  │  │ MCP23S │  │ LGS51 │  │BTS3│ │
│  │ TXB01  │  │ ADS111 │  │ REF50 │  │Conn│ │
│  │        │  │ MCP320 │  │ AMS11 │  │    │ │
│  └────────┘  └────────┘  └───────┘  └────┘ │
│                                              │
│  Zones connected via regulators:             │
│  12V → LGS5145 → 6.8V → AMS1117 → 5.0V     │
│  5.0V → ESP32 internal LDO → 3.3V           │
└──────────────────────────────────────────────┘
```

### 8.2 Decoupling Strategy

| Rail | Bulk Cap | Bypass Cap | Placement |
|------|----------|-----------|-----------|
| 12V input | 100µF/25V electrolytic + 10µF ceramic | 100nF at each IC VDD | Near power connector, BTS3134 |
| 6.8V (LGS5145 output) | 22µF ceramic (output cap per datasheet) | 100nF at REF5050 VIN | Near LGS5145 |
| 5.0V (AMS1117 output) | 47µF tantalum (LDO stability) + 10µF ceramic | 100nF at each MCP23S17, ADS1115, MCP3204 | Distributed, each IC within 5mm |
| 3.3V (ESP32) | 10µF ceramic | 100nF at TXB0108 VCCA, each sensor divider | Near ESP32 module, distributed |
| AGND | 22µF ceramic + 100nF | — | At ferrite bead connection point |

### 8.3 Current Budget Summary

| Rail | Average Current | Peak Current | Source |
|------|----------------|-------------|--------|
| 12V battery input | 3.5A | 12A | Battery via F1 fuse |
| 12V electronics (RS-485 bus) | ~400mA | ~600mA | TPS61089 on RS-485 module |
| 6.8V | ~100mA | ~150mA | LGS5145 buck from 12V |
| 5.0V | ~500mA | ~700mA | AMS1117 from 6.8V (6× MCP23S17 + ADS1115 + MCP3204 + sensors) |
| 3.3V | ~300mA | ~500mA | ESP32 internal LDO (MCU + WiFi TX + PSRAM) |
| Heater 1 (GPIO 19) | 5-8A avg | 15A peak | Battery via BTS3134 |
| Heater 2 (GPIO 20) | 5-8A avg | 15A peak | Battery via BTS3134 |
| Alternator field (GPIO 41) | 3-5A avg | 7A peak | Battery via external driver |

---

## 9. Signal Integrity Considerations

### 9.1 High-Speed Signal Routing (L1)

| Signal | Frequency | Impedance | Routing Rules |
|--------|-----------|-----------|---------------|
| FSPI CLK (GPIO 47) | 50 MHz | 50Ω microstrip | Length-match MOSI/MISO to ±100 mil of CLK |
| HSPI CLK (GPIO 10) | 10 MHz | 50Ω microstrip | Short stubs to MCP23S17 (<20mm) |
| RS-485 A/B | 115.2 kbaud | 100Ω differential | Tight coupling, 120Ω termination at each end |
| I2C SDA/SCL | 400 kHz | — | No impedance control needed, 4.7kΩ pullups |

### 9.2 Analog Signal Routing (L1 → L4)

| Signal | Sensitivity | Routing Rules |
|--------|-------------|---------------|
| CJ125_UA (GPIO 3, 4) | ±1mV accuracy | Guard ring both sides, no digital traces within 20 mil, route on L4 if possible |
| CLT/IAT NTC (GPIO 7, 8) | ±10mV | Keep short (<25mm from divider to ESP32 pad) |
| VBAT divider (GPIO 9) | ±50mV | Not critical — large signal (2V+) |
| ADS1115 I2C (GPIO 0, 42) | — | Ferrite bead at analog/digital boundary |
| MCP3204 SPI (HSPI shared) | — | Separate analog return under MCP3204, away from MCP23S17 digital noise |

### 9.3 EMI Mitigation

| Source | Mitigation |
|--------|-----------|
| 25 kHz alternator PWM (GPIO 41) | Keep trace short, route near GND plane, RC snubber at driver output |
| 100 Hz heater PWM (GPIO 19, 20) | BTS3134 has built-in slew rate control. Keep gate drive trace short. |
| 50 MHz SD card SPI | Controlled impedance, series termination resistors (22-33Ω) near source |
| 10 MHz HSPI | Controlled impedance, matched stubs, ground stitching at via transitions |
| ESP32-S3 WiFi (2.4 GHz) | Antenna keepout zone (no copper pour, no traces) per WROOM module datasheet |
| MCP23S17 output switching | Level shifters (TXB0108, BSS138) at ESP-ECU board edge buffer fast edges from reaching sensitive traces |

---

## 10. Thermal Management

### 10.1 Hot Spots on ESP-ECU Main Board

| Component | Power Dissipation | Package | θJA | Junction Temp at 85°C Ambient |
|-----------|------------------|---------|-----|-------------------------------|
| BTS3134 Heater 1 | Gate drive only (~50mW) | — | — | Negligible (BTS3134 handles load current off-board) |
| BTS3134 Heater 2 | Gate drive only (~50mW) | — | — | Negligible |
| AMS1117-5.0 (6.8V→5V) | (6.8-5)×0.5A = 0.9W | SOT-223 | ~90°C/W | 85 + 81 = **166°C** — EXCEEDS 125°C MAX |
| LGS5145 buck | Switching losses ~200mW | QFN | ~40°C/W | 85 + 8 = 93°C — OK |
| ESP32-S3 WROOM | ~500mW (WiFi TX) | Module | ~30°C/W | 85 + 15 = 100°C — OK |

**AMS1117-5.0 thermal problem:** At 500mA load from 6.8V, the AMS1117 dissipates 0.9W in SOT-223. At 85°C ambient, junction reaches 166°C — well above the 125°C maximum.

**Solutions:**
1. **Reduce input voltage:** Use a 5.5V intermediate buck instead of 6.8V. Pdiss = (5.5-5)×0.5 = 0.25W → 85 + 22.5 = 107.5°C — marginal but OK.
2. **Replace AMS1117 with switching regulator:** TPS562200 (SOT-23-6, 2A, 90%+ efficiency) → Pdiss ≈ 50mW → no thermal concern.
3. **Add thermal vias:** Grid of 5×5 thermal vias (0.3mm drill, 1mm pitch) under AMS1117 thermal pad, connecting to L2 and L5 ground planes. Reduces θJA to ~50°C/W → 85 + 45 = 130°C — still marginal.
4. **Copper pour heatsink:** Large copper pour on L1 around AMS1117 (10×10mm minimum) + thermal vias to L2/L5. Reduces θJA to ~40°C/W → 85 + 36 = 121°C — barely acceptable.

**Recommendation:** Replace AMS1117-5.0 with a switching regulator for the 5V rail. The 6.8V→5V drop is too large for an LDO at 500mA in an 85°C environment.

### 10.2 Thermal Via Grid Specification

For QFN and thermal-pad packages:

| Parameter | Value |
|-----------|-------|
| Via drill | 0.3mm (12 mil) |
| Via pitch | 1.0-1.2mm (40-48 mil) |
| Grid pattern | 3×3 minimum, 5×5 for >1W packages |
| Connection | Pad on L1 → via → GND pour on L2 → via → GND pour on L5 |
| Fill | Epoxy fill for via-in-pad (prevents solder wicking during reflow) |

---

## 11. Open Items

1. **AMS1117-5.0 thermal:** Must be replaced with a switching regulator or input voltage reduced. Current design exceeds safe junction temperature at 85°C ambient. Evaluate TPS562200 or similar.

2. **Fab house stackup confirmation:** Send target stackup to PCB fabricator for impedance modeling. Get actual Dk values for their specific prepreg and core materials.

3. **MCPCB vendor selection for driver modules:** Get quotes for 2oz copper, 3.0 W/m·K dielectric, aluminum base. Verify JLCPCB/PCBWay offer this stackup. Standard lead time is typically longer than FR4.

4. **Via plating specification:** Request 35µm plating (vs standard 25µm) for high-current via arrays on the power supply board. Verify fab house capability.

5. **WiFi antenna keepout:** Verify ESP32-S3 WROOM module datasheet for exact keepout dimensions. No copper pour (any layer) within the keepout zone.

6. **Ground plane slot audit:** After routing, run DRC to check for unintentional slots in L2 and L5 ground planes. Any slot under a signal trace is a potential EMI issue.

7. **Thermal simulation:** Run thermal FEA on the driver module MCPCB with worst-case (direct coil variant, 4 channels at max duty) to verify junction temperatures stay within spec.

8. **BTS3134 integration:** Confirm BTS3134 heater driver is off-board (separate from ESP-ECU main board) or on-board. If on-board, the 12V heater current (10-15A per bank) flows through the main board and dominates the thermal budget. If off-board, only the gate drive signal (50mW) is on the main board.

9. **2oz copper on L3 (Power):** Evaluate whether the 12V zone on L3 should be 2oz. This adds cost but halves voltage drop for the heater and alternator current paths. Mixed copper weight (1oz signal, 2oz power) is available from most fab houses.
