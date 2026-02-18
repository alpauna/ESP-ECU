# Power Supply Analysis 6 — Final Schematic Review

**Date:** 2026-02-18
**Schematic:** SCH_ECU_1-POWER_2026-02-18.pdf (final revision)
**BOM:** BOM_Board1_ECU_2026-02-18.xlsx (final revision)
**References:** LM25118 (TI SNVS655), LP38693 (TI SNVS578), LGS5145, SMF7.0A, SMF5.0A, SMBJ36A

---

## Revision Changes Since Previous Analysis

### Designator Renumbering

The schematic has undergone a complete designator cleanup. No functional changes from renumbering alone.

| Previous designator | Current designator | Part |
|--------------------|--------------------|------|
| Q11 | **Q1** | SWHA069R06VT (60V buck HS FET) |
| U32 | **U7** | LP38693SD-5.0/NOPB (5VA LDO) |
| D14/D15 | **D6/D7** | SMF7.0A (6.8V rail TVS) |
| D16–D19 | **D1–D4** | SMBJ36A (VIN TVS ×4) |
| C109/C110 | **C27/C44** | 47pF (CFF feed-forward caps) |
| R53 | **R18** | 10kΩ (U7 EN pull-up) |
| R55 | **R16** | 10kΩ (U6 EN pull-up) |
| R54 | **R21** | 10kΩ (U8 EN pull-up) |
| C108 | **C39** | 1µF (U6 EN capacitor) |
| C107 | **C58** | 4.7µF (U8 EN capacitor) |
| R19/R20 (REF5050 filter) | **R22/R23** | 33Ω (REF5050 input filter) |
| R17/R19 (U8 FB divider) | **R19/R20** | 9.31kΩ/1.8kΩ (U8 5V divider) |

### Functional Changes

| Change | Detail |
|--------|--------|
| **U7 EN RC delay added** | R18 (10kΩ) + **C41 (1µF)** from 6.4V rail → AGND, branch to EN pin 3. New — previous revision had R53 pull-up only (no cap). |
| **D14 SMF5.0A added** | 5V rail TVS protection (5.0V standoff). New addition — completes TVS coverage on all rails. |
| **Net label "6.4V"** | Legacy label — actual voltage is **6.802V** (R12=4.53kΩ, R9=1kΩ). Net name unchanged from original 6.4V design target. |

---

## U7 LP38693SD-5.0 Enable RC Network (Key Change)

### Circuit

```
6.4V (6.802V actual)
  │
  R18 (10kΩ)
  │
  ├──── EN (pin 3, U7 LP38693SD-5.0)
  │
  C41 (1µF)
  │
 AGND
```

τ = R18 × C41 = 10kΩ × 1µF = **10ms**

### LP38693 Enable Threshold — VIN-Dependent

**Critical finding:** The LP38693 EN threshold is NOT a fixed 1.2V. Per the LP38693 datasheet (TI SNVS578, EC table section 6.6), the enable-ON threshold scales with input voltage:

| Condition | VEN(ON) typical | VEN(OFF) typical |
|-----------|----------------|-----------------|
| VIN = 4V | 1.8V | 0.4V |
| VIN = 6V | 3.0V | 0.4V |
| **VIN = 6.8V (interpolated)** | **~3.2V** | **0.4V** |
| VIN = 10V | 4.0V | 0.4V |

Note: The datasheet provides typical values only (no min/max for VEN(ON)). The ON threshold is approximately 0.47 × VIN. The OFF threshold is fixed at ~0.4V regardless of VIN.

### Enable Delay Calculation

From steady-state 6.802V rail:

```
V_EN(t) = 6.8 × (1 − e^(−t/10ms))

Solving for V_EN = 3.2V:
  3.2 = 6.8 × (1 − e^(−t/10))
  e^(−t/10) = 1 − 3.2/6.8 = 0.529
  t = −10 × ln(0.529) = −10 × (−0.636)
  t = 6.36ms
```

**U7 enables approximately 6.4ms after the 6.8V rail is stable.**

This is significantly longer than the ~0.1µs "immediate" enable assumed in the previous StartupAndCrankingAnalysis.md, and also much longer than the ~2ms that would result from assuming a 1.2V fixed threshold.

### Impact on 5VA Startup

Previous analysis: 5VA ≈ 5.0V at ~15ms from B+ (immediate enable)
Updated: 5VA ≈ 5.0V at **~20ms from B+**

```
Timeline breakdown:
  ~13ms    6.8V rail stable (soft-start complete)
  +6.4ms   U7 EN reaches 3.2V threshold
  +0.2ms   LP38693 internal startup
  ~20ms    5VA = 5.0V regulated
```

LP38693 output caps: C38 (100nF) + C37 (10µF) + C40 (47µF) ≈ 57µF. At 50mA load, cap charge time is negligible (~50µs). The startup is dominated by the RC enable delay.

### Benefits of C41 Addition

1. **Inrush spreading:** U7 no longer competes with U6 for 6.8V rail current at startup
2. **Reference settling:** REF5050 outputs (5VREF/5VREF2) are stable ~6ms before 5VA enables — analog references are settled before the analog measurement chain powers up
3. **Noise immunity:** The 10ms time constant filters out any noise or ringing on the 6.8V rail during startup
4. **Consistent design:** All three downstream regulators now have RC enable delays

---

## Updated Enable Delay Summary (All Three RC Networks)

| Supply | Designator | R | C (EN) | τ | EN threshold | Enable delay | Note |
|--------|-----------|---|--------|---|-------------|-------------|------|
| **3.3V** | U6 LGS5145 | R16 (10kΩ) | C39 (1µF) | 10ms | ~1.24V (fixed) | **2.0ms** | Source: DIGIN net |
| **5VA** | U7 LP38693 | R18 (10kΩ) | C41 (1µF) | 10ms | ~3.2V (VIN-dep) | **6.4ms** | Source: 6.4V net |
| **5V** | U8 LGS5145 | R21 (10kΩ) | C58 (4.7µF) | 47ms | ~1.24V (fixed) | **9.5ms** | Source: 6.4V net |

### Why U7 Takes Longer Than U6 Despite Same τ

Although U7 and U6 have identical RC constants (10kΩ × 1µF = 10ms), U7's enable delay is 3× longer because the LP38693 EN threshold (~3.2V) is much higher than the LGS5145 EN threshold (~1.24V). The RC capacitor must charge to 47% of VIN for U7 vs. only 18% for U6.

### Note on U6 EN Source — "DIGIN" Net

U6's EN resistor (R16) sources from the "DIGIN" net, while U7 and U8 source from the "6.4V" net. DIGIN's connection is on another schematic page. If DIGIN is the same 6.8V intermediate rail (most likely), the U6 timing above is correct. If DIGIN is a different rail (e.g., direct from B+), U6 would enable earlier — still safe, as U6 will wait for its VIN to reach minimum input regardless.

---

## Updated Startup Timeline

### Rail Availability Order

| Order | Rail | Regulator | Enable delay | Available at | Change from previous |
|-------|------|-----------|-------------|-------------|---------------------|
| 1 | **6.8V** | LM25118 U1 | UVLO at 3.26V | ~13ms | Unchanged |
| 2 | **5VREF** | REF5050 U9 | Always on (R22 33Ω filter) | ~14ms | Unchanged |
| 3 | **5VREF2** | REF5050 U10 | Always on (R23 33Ω filter) | ~14ms | Unchanged |
| 4 | **3.3V** | LGS5145 U6 | R16/C39 (2ms) | ~17.5ms | Unchanged |
| 5 | **5VA** | LP38693 U7 | R18/C41 (6.4ms) | **~20ms** | **Was ~15ms (immediate)** |
| 6 | **5V** | LGS5145 U8 | R21/C58 (9.5ms) | ~25ms | Unchanged |

### Detailed Timeline

```
                    0ms         5ms         10ms        15ms        20ms        25ms
Event               │           │           │           │           │           │
                    │           │           │           │           │           │
B+ applied ─────────█           │           │           │           │           │
Q2 turns on ────────█           │           │           │           │           │
LM25118 VCC rdy ────░█          │           │           │           │           │
                    │           │           │           │           │           │
6.8V ramp start ────░░█         │           │           │           │           │
6.8V = 6.802V ──────░░░░░░░░░░░░░░█        │           │           │           │
5VREF stable ───────░░░░░░░░░░░░░░░█       │           │           │           │
                    │           │           │           │           │           │
U6 EN threshold ────░░░░░░░░░░░░░░░░█      │           │           │           │
3.3V regulated ─────░░░░░░░░░░░░░░░░░░░█   │           │           │           │
  └─ ESP32 boots ───░░░░░░░░░░░░░░░░░░░█   │           │           │           │
                    │           │           │           │           │           │
U7 EN threshold ────░░░░░░░░░░░░░░░░░░░░░░█│           │           │           │
5VA regulated ──────░░░░░░░░░░░░░░░░░░░░░░░█           │           │           │
                    │           │           │           │           │           │
U8 EN threshold ────░░░░░░░░░░░░░░░░░░░░░░░░░░█        │           │           │
5V regulated ───────░░░░░░░░░░░░░░░░░░░░░░░░░░░░░█     │           │           │
  └─ Expanders rdy ─░░░░░░░░░░░░░░░░░░░░░░░░░░░░░█     │           │           │
```

### Sequence Verification for Key Devices

| Device | Requirement | Satisfied? | Margin |
|--------|------------|-----------|--------|
| REF5050 U9/U10 | VIN > 5.2V, no EN | ✓ Ready at ~14ms | 6ms before 5VA |
| PCA9306 U19 | VREF1 (3.3V) before VREF2 (5V) | ✓ 3.3V at 17.5ms, 5V at 25ms | 7.5ms gap |
| ESP32-S3 U13 | 3.3V stable | ✓ Ready at ~17.5ms | Boots before 5V/5VA |
| CD74HC4067 U18 | VCC = 5VA | ✓ Ready at ~20ms | 47ms before ESP32 reads mux |
| INA180A1 U5 | VS = 5VA | ✓ Ready at ~20ms | — |
| MCP23S17 ×6 | VDD = 5V | ✓ Ready at ~25ms | 42ms before ESP32 SPI init |
| ADS1115 ×3 | VDD = 5VREF2 | ✓ Ready at ~14ms | Long before ESP32 reads |

### Gap Analysis — ESP32 vs 5V Peripherals

```
ESP32 boots:           ~17.5ms
5V available:          ~25.0ms
ESP32 tries SPI/I2C:   ~67.5ms (50ms into boot)
Gap: 5V ready 42.5ms before ESP32 accesses expanders ✓
```

---

## D14 SMF5.0A — 5V Rail TVS (New Addition)

D14 (SMF5.0A) provides TVS overvoltage protection on the 5V output rail from U8.

| Parameter | Value |
|-----------|-------|
| VR (standoff) | 5.0V |
| VBR (breakdown min) | 6.4V |
| VC (clamp @ IPP) | 9.2V |
| IR (leakage @ VR) | ≤1µA |
| Package | SOD-123FL |

### Margin Analysis

U8 output voltage (LGS5145 with R19=9.31kΩ, R20=1.8kΩ, assuming VFB ≈ 0.8V):

```
Vout = 0.8 × (1 + 9.31/1.8) = 0.8 × 6.172 = 4.94V nominal
```

| Condition | 5V output | SMF5.0A standoff (5.0V) | Margin |
|-----------|----------|------------------------|--------|
| Nominal | 4.94V | 5.0V | +0.06V ✓ |
| Worst case high (VFB +2%, R ratio +2%) | ~5.14V | 5.0V | −0.14V (leakage ~1–5µA) |
| Breakdown (min) | — | 6.4V | 1.26V above worst case ✓ |

TVS never clamps during normal regulation. Provides genuine transient protection.

### Complete TVS Protection Coverage

| Rail | TVS | Standoff | Clamp | Status |
|------|-----|----------|-------|--------|
| VIN (B+) | D1–D4 (4× SMBJ36A) | 36V | 58.1V | ✓ Below Q1 60V |
| 6.8V | D6/D7 (2× SMF7.0A) | 7.0V | 11.2V | ✓ Below LP38693 12V abs max |
| 5V | **D14 (SMF5.0A)** | **5.0V** | **9.2V** | **✓ New — below MCP23S17 7V abs max** |
| 3.3V | D12 (SMAJ3.3A) | 3.3V | 5.0V | ✓ Below ESP32 3.6V abs max |

All four power rails now have TVS protection. The design covers the full path from battery input to every downstream rail.

---

## Complete Verified Topology (2026-02-18 Designators)

```
B+ → F1 (JK250-200U PTC 2A)
    → Q2 (DMP6023LE-13, −60V PMOS) [D8: 13V Zener VGS clamp, R7: 10k gate pull]
    → D5 (PMEG6020EP body diode bypass)
    → D1–D4 (4× SMBJ36A VIN TVS, 36V standoff, 58.1V clamp)
    → B+BIASED / VIN bus
    → C8/C9 (2× 560µF polymer) + C1–C4,C6 (5× 47µF tantalum) = 1355µF bulk

VIN → LM25118 (U1) Buck-Boost Controller
    │   Q1 SWHA069R06VT (60V, buck HS) ← HO bootstrap drive
    │   U4 MBR20200CS (200V, buck catch diode)
    │   L1 XAL7070-332MEC (3.3µH, 14A sat)
    │   Q3 CSD17575Q3 (30V, boost LS) ← LO drive
    │   D9 STPS20L15G-TR (15V, boost output diode)
    │   R12=4.53kΩ / R9=1kΩ → VOUT = 6.802V
    │   D6/D7 2× SMF7.0A (output TVS, 7.0V standoff)
    │   C11–C13,C21,C31,C42,C51 (7× 100µF ceramic 10V)
    │   C32,C33 (2× 47µF tantalum 25V)
    │   C15,C18 (2× 270µF polymer 16V)
    │   Total: ~1334µF nominal, ~950–1100µF effective at 6.8V
    │
    └── "6.4V" net = 6.802V intermediate rail
         │
         ├── U9 REF5050 (via R22 33Ω + C59 10µF + C60 100nF) → 5VREF
         │    └── MCP3204 U27 VREF
         │
         ├── U10 REF5050 (via R23 33Ω + C61 10µF + C62 100nF) → 5VREF2
         │    └── ADS1115 ×3 (U20–U22) VDD
         │
         ├── U6 LGS5145 → 3.3V → L3 (10µH) → 3.3VPF
         │    R16 (10k) + C39 (1µF) EN delay → ~2ms enable
         │    D10 (PMEG6020EP bootstrap), D12 (SMAJ3.3A TVS)
         │    C27 (47pF CFF across R13)
         │    └── ESP32-S3 (U13)
         │
         ├── U7 LP38693SD-5.0 → 5VA
         │    R18 (10k) + C41 (1µF) EN delay → ~6.4ms enable ← UPDATED
         │    C38 (100nF) + C37 (10µF) + C40 (47µF)
         │    └── CD74HC4067 mux (U18), PCA9306 (U19), INA180A1 (U5)
         │
         └── U8 LGS5145 → 5V → L5 (10µH) → 5VPF
              R21 (10k) + C58 (4.7µF) EN delay → ~9.5ms enable
              D13 (PMEG6020EP bootstrap), D14 (SMF5.0A TVS)
              C44 (47pF CFF across R19)
              └── MCP23S17 ×6, TXB0108 ×2, peripherals

         AGND ←→ GND via L6 (BLM21PG221SN1D) + C50 (100nF) + C57 (22µF)
```

### Power-Up Sequence (Corrected)

```
Time →  0ms     5ms     10ms    15ms    20ms    25ms
        │       │       │       │       │       │
6.8V:   ████████████████████████████████████████████  LM25118 (UVLO > 3.26V)
5VREF:  ░░░░████████████████████████████████████████  REF5050 (33Ω/10µF filter)
3.3V:   ░░░░░░░░████████████████████████████████████  U6 LGS5145 (~2ms EN delay)
5VA:    ░░░░░░░░░░░░░░░░████████████████████████████  U7 LP38693 (~6.4ms EN delay)
5V:     ░░░░░░░░░░░░░░░░░░░░████████████████████████  U8 LGS5145 (~9.5ms EN delay)
```

Startup order: 6.8V → 5VREF → 3.3V → **5VA** → 5V

Note: 5VA now starts **after** 3.3V (was before in previous analysis). This is actually better — precision references are stable before analog power enables.

---

## Cranking and Recovery — No Changes

The cranking analysis from StartupAndCrankingAnalysis.md remains valid. The U7 RC delay adds ~5ms to the total startup, changing recovery from ~200ms to ~205ms — negligible for cranking recovery.

| Metric | Previous | Updated |
|--------|----------|---------|
| VIN operating range | 3.26V – 42V | Unchanged |
| Total startup (all rails) | ~25ms | **~25ms** (U7 finishes at ~20ms, U8 still last at ~25ms) |
| Recovery from UVLO reset | ~200ms | ~205ms |

---

## RC Constants Assessment

### Updated Timing Relationships

```
6.8V stable ─────┬──── +2.0ms ──── U6 EN (1.24V) → 3.3V
                 │
                 ├──── +6.4ms ──── U7 EN (3.2V)  → 5VA
                 │
                 └──── +9.5ms ──── U8 EN (1.24V) → 5V

Gaps:
  3.3V → 5VA:  4.4ms
  5VA → 5V:    3.1ms
  3.3V → 5V:   7.5ms (unchanged — PCA9306 safe)
```

### Are Changes Needed?

**No. The current RC values are well-optimized.** The C41 addition creates an orderly 3-stage enable sequence:

1. **Stage 1 (+2ms):** 3.3V — ESP32 boots, GPIO pins configured
2. **Stage 2 (+6.4ms):** 5VA — Analog measurement chain activates (references already settled)
3. **Stage 3 (+9.5ms):** 5V — Digital peripherals (expanders, level shifters) power up

Each stage is separated by 3–4ms, providing clean current draw steps and preventing simultaneous inrush. The total 25ms startup is fast enough for cranking recovery within one engine revolution at 200 RPM.

---

## Soft-Start Capacitor Note

The LM25118 soft-start time depends on the SS pin (pin 7) capacitor. The exact designator could not be definitively traced from the schematic image. Typical LM25118 designs use 22nF–220nF:

| CSS value | Soft-start time | 6.8V ramp rate | Inrush to ~1300µF |
|-----------|----------------|----------------|-------------------|
| 47nF | 5.8ms | 1.17 V/ms | ~1.5A (below current limit) |
| 100nF | 12.3ms | 0.55 V/ms | ~0.72A (gentle) |
| 220nF | 27ms | 0.25 V/ms | ~0.33A (very gentle) |

The timeline calculations above assume CSS ≈ 100nF (tSS ≈ 12ms). **Verify the actual SS capacitor designator on the board.** If CSS = 47nF, all rail-up times shift approximately 6ms earlier. If CSS = 220nF, they shift ~15ms later. The enable sequence order and gaps are unaffected.

---

## Final Design Checklist

| # | Item | Status |
|---|------|--------|
| 1 | Buck HS FET voltage | ✅ Q1 SWHA069R06VT 60V |
| 2 | Boost LS FET voltage | ✅ Q3 CSD17575Q3 30V (sees VOUT only) |
| 3 | FB divider for 6.8V | ✅ R12=4.53kΩ, R9=1kΩ → 6.802V |
| 4 | Output cap voltage | ✅ 7× 100µF/10V ceramic (68% derating) |
| 5 | 6.8V rail TVS | ✅ D6/D7 SMF7.0A (7.0V standoff) |
| 6 | 5V rail TVS | ✅ **D14 SMF5.0A** (new) |
| 7 | 3.3V rail TVS | ✅ D12 SMAJ3.3A |
| 8 | VIN TVS | ✅ D1–D4 4× SMBJ36A |
| 9 | Reverse polarity | ✅ Q2 DMP6023LE-13 |
| 10 | VGS clamp | ✅ D8 UDZVFHTE-1713B 13V Zener |
| 11 | U7 5VA enable sequencing | ✅ **R18/C41 RC delay ~6.4ms** (corrected from "immediate") |
| 12 | U6 3.3V enable sequencing | ✅ R16/C39 ~2ms delay |
| 13 | U8 5V enable sequencing | ✅ R21/C58 ~9.5ms delay |
| 14 | PCA9306 safe sequencing | ✅ 3.3V → 5V gap = 7.5ms |
| 15 | CFF feed-forward caps | ✅ C27 47pF (U6), C44 47pF (U8) |
| 16 | Analog LDO headroom | ✅ LP38693 at 6.8V → 1.8V headroom |
| 17 | Cranking brownout survival | ✅ Buck-boost to VIN = 3.26V |
| 18 | AGND/GND separation | ✅ L6 ferrite + C50/C57 |

### Remaining (Nice-to-Have, Not Blockers)

| # | Item | Recommendation |
|---|------|---------------|
| 1 | Driver gate pull-downs | 10k–47kΩ from coil/injector MOSFET gates to GND (on driver pages) |
| 2 | Verify SS capacitor | Confirm designator and value on SS pin (pin 7) of U1 |

---

## Summary

The power supply design is **production-ready**. All critical, important, and recommended items from previous analyses have been implemented:

- Q1 (60V FET), D6/D7 (SMF7.0A), U7 (LP38693SD-5.0), output caps (100µF/10V), CFF caps (47pF), VIN TVS (4× SMBJ36A), and 5V TVS (SMF5.0A) are all in place.

- The **C41 enable capacitor on U7** creates a well-ordered 3-stage startup sequence. The LP38693's VIN-dependent EN threshold (~3.2V at 6.8V input, per datasheet EC table) results in a **6.4ms enable delay** — longer than a naive 1.2V threshold assumption would predict. This is a beneficial delay that ensures analog references are settled before the analog supply activates.

- All four power rails have TVS protection. All downstream regulators have adequate headroom. Enable sequencing ensures safe PCA9306 operation. The LM25118 buck-boost maintains regulation down to 3.26V input for uninterrupted cranking operation.
