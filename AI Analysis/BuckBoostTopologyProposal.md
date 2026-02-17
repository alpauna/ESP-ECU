# Buck-Boost Topology Proposal — ESP-ECU

**Date:** 2026-02-17
**Status:** Proposal — addresses cranking brown-out and cascading failure issues

---

## Problem Statement

The current multi-rail topology has different dropout thresholds (7.5V, 6.0V, 3.7V), creating dangerous partial-power states during cranking. The 6.8V analog rail dies first, then 5V digital, while 3.3V keeps the ESP32 alive in a zombie state unable to control anything.

## Proposed Solution

Replace the LM2576 (5V buck) and LGS5145 U4 (6.8V buck) with a **single buck-boost converter at 6.4V/3A**. All downstream rails feed from this one intermediate rail.

### Current topology (3 independent rails from VIN)

```
B+ (4-16V) → VIN
    ├── LM2576      → 5V/3A     (digital: expanders, peripherals)
    ├── LGS5145 U3  → 3.3V/1A   (ESP32)
    └── LGS5145 U4  → 6.8V/1A   (analog: REF5050, AMS1117)
```

**Problem:** Each rail has different VIN dropout. During cranking, rails die in sequence creating partial-power zombie states.

### Proposed topology (single intermediate rail)

```
B+ (4-16V) → Buck-Boost → 6.4V/3A (stable from VIN=3V to 55V)
    ├── LGS5145     → 5V     (digital: expanders, peripherals)
    ├── LGS5145     → 3.3V   (ESP32) → post-filter → 3.3VPF
    ├── LP5907-5.0  → 5VA    (analog: mux, PCA9306) ← replaces AMS1117
    ├── REF5050 #1  → 5VREF  (MCP3204 VREF)
    └── REF5050 #2  → 5VREF2 (ADS1115 VDD)
```

**Result:** If the buck-boost regulates, everything works. If it can't (VIN < 3V), everything dies together — clean reset, no zombie state.

---

## Why 6.4V

| Downstream device | Minimum input | Headroom from 6.4V |
|-------------------|---------------|-------------------|
| LGS5145 → 5V | 5.0V / 0.94 + Rdson = 5.7V | 0.7V ✓ |
| LGS5145 → 3.3V | 3.3V / 0.94 + Rdson = 3.7V | 2.7V ✓ |
| REF5050 (VIN min 5.2V) | 5.2V + 33Ω × 2mA = 5.27V | 1.13V ✓ |
| LP5907-5.0 (dropout 0.25V) | 5.25V | 1.15V ✓ |

All downstream regulators have adequate headroom at 6.4V.

**Why not higher?**
- 7V or 8V would waste more power in the LDOs (more dropout to dissipate as heat)
- Higher intermediate voltage = lower efficiency in the downstream 3.3V conversion
- 6.4V keeps LDO dissipation minimal: LP5907 at 50mA load = (6.4-5.0) × 0.05 = 70mW

**Why not lower?**
- Below 6.0V, the LGS5145 for 5V hits Dmax (94%) — no margin left
- REF5050 needs VIN ≥ 5.2V — with the 33Ω input filter, we need at least 5.3V at the buck-boost output
- 6.4V gives comfortable margin for all devices

---

## What This Eliminates

| Removed | Reason |
|---------|--------|
| LM2576SX-5.0 (D2PAK) | Replaced by buck-boost. Saves 100µH inductor + 6×220µF output caps |
| LGS5145 U4 (6.8V buck) | Eliminated — 6.4V feeds analog directly. Saves 15µH inductor + output caps |
| AMS1117-5.0 | Replaced by LP5907-5.0 (see below). Saves tantalum cap requirement |
| SMBJ7.0A TVS on 6.8V | No longer needed — 6.4V buck-boost is internally protected |
| L7 10µH post-filter | No longer needed — 6.8VPF rail eliminated |
| 6×220µF output caps (LM2576) | No longer needed |

**Net component savings:** ~15 parts removed. One buck-boost IC + inductor + caps added.

---

## AMS1117 → LP5907-5.0 Swap

The AMS1117 cannot reliably run from 6.4V — at max rated load (1A), its 1.3V dropout leaves only 0.1V margin. While the actual analog load is only ~50mA (dropout ~0.8V, which works), there's no reason to keep a 1990s LDO when the LP5907 is superior in every way:

| Parameter | AMS1117-5.0 | LP5907-5.0 |
|-----------|-------------|------------|
| Dropout @ 50mA | ~0.8V | ~0.1V |
| Min VIN for 5V out | ~5.8V | ~5.1V |
| PSRR @ 1kHz | ~60 dB | **82 dB** |
| Output noise | ~100 µVrms | **6.5 µVrms** |
| Ceramic cap stable | No (needs tantalum) | **Yes** |
| Max current | 1A | 250mA |
| Package | SOT-223 | SOT-23-5 |

The LP5907 swap solves two problems at once:
1. Adequate headroom from 6.4V (5.1V minimum vs 5.8V)
2. **15x lower noise** on the analog 5VA rail — directly improves ADC accuracy

At 50mA load from 6.4V: Pdiss = (6.4-5.0) × 0.05 = 70mW. Well within SOT-23-5 thermal limits.

---

## Cranking Brown-out: Solved

### With buck-boost at 6.4V

A buck-boost converter maintains its output voltage regardless of whether VIN is above or below the output:

| VIN (battery) | Buck-boost mode | 6.4V output | All downstream rails |
|---------------|----------------|-------------|---------------------|
| 14.4V (charging) | Buck | **6.4V** ✓ | All regulating |
| 12.0V (normal) | Buck | **6.4V** ✓ | All regulating |
| 8.0V (light crank) | Buck | **6.4V** ✓ | All regulating |
| 6.4V (crossover) | Pass-through | **6.4V** ✓ | All regulating |
| 5.0V (heavy crank) | **Boost** | **6.4V** ✓ | All regulating |
| 4.0V (extreme sag) | **Boost** | **6.4V** ✓ | All regulating |
| 3.0V (dead battery) | Below min VIN | Drops out | Clean shutdown, all rails die together |

**No more cascading failures. No more zombie state.** The ECU either works fully or resets cleanly.

### Comparison with current topology

| Battery voltage | Current design | Buck-boost design |
|----------------|---------------|-------------------|
| 7.2V (60% of 12V) | 6.8VA dying, analog garbage | **All rails nominal** |
| 6.0V (50% of 12V) | 5V dying, expanders resetting | **All rails nominal** |
| 4.8V (40% of 12V) | Only 3.3V alive, zombie state | **All rails nominal** |
| 3.0V | All dead | All dead (clean reset) |

---

## Buck-Boost IC Selection Criteria

| Parameter | Requirement | Why |
|-----------|-------------|-----|
| Input range | 3-55V (or wider) | Covers dead battery to automotive load dump |
| Output | 6.4V fixed (adjustable) | Single intermediate rail |
| Output current | ≥ 3A | ESP32 + WiFi + expanders + sensors + analog |
| Switching freq | ≥ 500kHz | Small inductor/caps, easy to filter |
| Topology | 4-switch buck-boost | Seamless buck/boost transition |
| Protections | UVLO, OCP, OTP | Automotive requirement |

### Candidate ICs

| Part | VIN range | IOUT | FSW | Package | Notes |
|------|-----------|------|-----|---------|-------|
| **TPS55340** (TI) | 2.9-42V | 5A switch | 100-570kHz | HTSSOP-14 | Boost/SEPIC/flyback; needs external FET for buck-boost |
| **LM5175** (TI) | 3.5-42V | 4A (2 per phase) | 100-600kHz | HTSSOP-24 | True 4-switch buck-boost, automotive-friendly |
| **LTC3789** (ADI) | 4-38V | 20A+ (ext FETs) | 200-600kHz | SSOP-24 | High-current 4-switch, external FETs |
| **TPS63060** (TI) | 2.5-12V | 2A | 2.4MHz | VQFN-14 | Compact, but limited input range |
| **TPS63802** (TI) | 1.3-5.5V | 2A | 2.4MHz | VQFN-11 | Too low input range |
| **MP8859** (MPS) | 2.8-22V | 3A | 1.4MHz | QFN-20 | Good fit if 22V max is sufficient |

**Best fit for automotive (full VIN range):** LM5175 — true 4-switch buck-boost, 3.5-42V input, 3A output at 6.4V, 600kHz, HTSSOP-24. Handles cranking down to 3.5V and load dump up to 42V.

**Best fit if VIN stays above 4V:** TPS55340 with external low-side FET — simpler, cheaper, proven in automotive. Or MP8859 if 22V input max is acceptable (needs VIN TVS clamp for load dump).

---

## Revised Full Power Architecture

```
B+ → F1 (PTC 2A) → Q1 (PMOS reverse polarity) → VIN (3-55V)
    │
    └── Buck-Boost (LM5175 or similar) → 6.4V / 3A
         │
         ├── LGS5145 U3 → 5V digital
         │    └── MCP23S17 ×6, TXB0108 ×2, peripherals
         │
         ├── LGS5145 U4 → 3.3V → L4 post-filter → 3.3VPF
         │    └── ESP32-S3
         │
         ├── LP5907-5.0 → 5VA (analog)
         │    └── CD74HC4067 mux, PCA9306
         │
         ├── REF5050 #1 (via 33Ω RC filter) → 5VREF
         │    └── MCP3204 VREF
         │
         └── REF5050 #2 (via 33Ω RC filter) → 5VREF2
              └── ADS1115 ×3 VDD

         AGND ←→ GND via BLM21PG221SN1D ferrite bead + 100nF/22µF
```

### What changes from current design

| Item | Current | Proposed |
|------|---------|----------|
| Primary converter | LM2576 (5V buck from VIN) | Buck-boost (6.4V from VIN) |
| 6.8V analog buck | LGS5145 U4 from VIN | **Eliminated** — 6.4V feeds analog directly |
| 5V digital | LM2576 direct | LGS5145 from 6.4V |
| 3.3V | LGS5145 from VIN | LGS5145 from 6.4V |
| Analog LDO | AMS1117-5.0 from 6.8V | LP5907-5.0 from 6.4V |
| REF5050 input | 6.8V via 33Ω filter | 6.4V via 33Ω filter |
| Cranking survival | Dies at VIN < 7.5V | **Survives down to ~3.5V** |
| Failure mode | Cascading (3 thresholds) | **Binary (works or resets)** |

---

## Remaining Recommendations (carried forward)

These items from the previous analyses still apply:

| Priority | Action | Status |
|----------|--------|--------|
| **SAFETY** | Pull-down resistors on coil/injector driver gates | Still needed — prevents uncontrolled fire if buck-boost dies |
| **Strong rec** | Power sequencing via EN pins (5V before 3.3V before analog) | Simpler now — all from 6.4V, use RC delays on EN |
| **Recommended** | CFF 47pF feed-forward on LGS5145 feedback dividers | Still applies to the 5V and 3.3V LGS5145s |
| **Recommended** | Input EMI filter on VIN before buck-boost | Single converter to filter now (simpler) |
| **Layout** | Kelvin sense on R4 (INA180A1 shunt) | Unchanged |
| **Layout** | Keep buck-boost switching loop tight (per IC datasheet) | New layout concern |

---

## Summary

The buck-boost intermediate rail at 6.4V/3A:

1. **Eliminates cranking brown-out** — maintains 6.4V output even at 3.5V battery
2. **Eliminates cascading failures** — all rails live or die together
3. **Eliminates the zombie state** — no partial-power conditions
4. **Removes 2 converters** — LM2576 and 6.8V LGS5145 replaced by one buck-boost
5. **Enables LP5907 swap** — 15x lower noise on analog rail, ceramic-stable
6. **Reduces BOM** — ~15 fewer components (big inductor, 6 large caps, TVS, post-filter)
7. **Single-point EMI filtering** — one converter to filter instead of three

The only added complexity is the buck-boost IC itself (larger than SOT-23-6, needs its own inductor and caps). But the overall design is simpler, more robust, and solves every issue identified in the cranking and power supply analyses.
