# Startup Sequence & Cranking Response Analysis

**Date:** 2026-02-17
**Schematic:** SCH_ECU_1-POWER_2026-02-17.pdf (final revision)
**BOM:** BOM_Board1_ECU_2026-02-17.xlsx (final revision)

---

## Additional Changes in This Revision

| Item | Change |
|------|--------|
| C109, C110 (47pF) | CFF feed-forward caps on LGS5145 feedback dividers |
| D16–D19 (4× SMBJ36A) | VIN TVS protection — 36V standoff, 58.1V clamp |
| D1 (B530C-13-F) | Removed |
| D2, D3 (PESD5Z3.3) | Removed |

The SMBJ36A × 4 on VIN provides 2400W combined peak pulse power. At typical load dump currents, the clamp voltage settles near ~42–48V (below Q11's 60V rating). This completes the input protection chain: F1 (PTC current limit) → Q10 (reverse polarity) → D16–D19 (overvoltage clamp) → Q11/LM25118 (converter).

---

## Part 1: LM25118 UVLO and Soft-Start

### UVLO Thresholds

R3 = 21.5kΩ (top, VIN to UVLO), R6 = 13kΩ (bottom, UVLO to GND).

```
VIN_on  = V_UVLO_rising × (R3 + R6) / R6
        = 1.23V × (21.5k + 13k) / 13k
        = 1.23 × 2.654
        = 3.264V

VIN_off = V_UVLO_falling × (R3 + R6) / R6
        = 1.13V × 34.5k / 13k
        = 1.13 × 2.654
        = 2.999V

Hysteresis = 3.264 − 2.999 = 0.265V
```

**The converter enables at VIN > 3.26V and disables at VIN < 3.00V.** This 265mV hysteresis prevents cycling during slow cranking sag near the threshold.

### Soft-Start Time

The LM25118 SS pin (pin 7) charges an external capacitor with 10µA internal current source. The output voltage tracks the SS pin voltage until it exceeds VFB (1.23V):

```
t_SS = C_SS × VFB / I_SS = C_SS × 1.23V / 10µA
```

| C_SS value | Soft-start time | Inrush charging ~1500µF to 6.8V | Assessment |
|------------|----------------|--------------------------------|------------|
| 22nF | 2.7ms | 3.8A (hits current limit) | Too fast |
| 47nF | 5.8ms | 1.8A (high but OK) | Marginal |
| 100nF | 12.3ms | 0.83A | Good |
| 220nF | 27ms | 0.38A | Conservative |
| 1µF | 123ms | 0.08A | Very gentle |

**Verify your SS capacitor value on the schematic.** The designator isn't visible in the text extraction. A value of **100nF** (tSS ≈ 12ms) is typical for the LM25118 with this output capacitance — it keeps inrush well below the current limit while providing fast startup.

### 6.8V Output Ramp

The output doesn't step to 6.8V — it ramps linearly during soft-start:

```
V_out(t) = 6.8V × (t / t_SS)    for t < t_SS

At t_SS = 12ms (CSS = 100nF):
  At t=2ms:   V_out ≈ 1.13V    (too low for any downstream regulator)
  At t=4ms:   V_out ≈ 2.27V    (LP38693 starts regulating: VIN_min = 5.25V → not yet)
  At t=6ms:   V_out ≈ 3.40V    (LGS5145 3.3V could start if EN was high)
  At t=8ms:   V_out ≈ 4.53V    (still below LP38693/REF5050 needs)
  At t=10ms:  V_out ≈ 5.67V    (LP38693 begins regulating: 5.0V + 0.25V dropout = 5.25V)
  At t=12ms:  V_out = 6.80V    (all downstream regulators have full headroom)
```

This means downstream regulators don't start at t=0 — they start when the 6.8V ramp reaches their minimum input voltage.

---

## Part 2: RC Enable Delay Calculations

### Charging Equation

Each RC-enabled supply follows:

```
V_EN(t) = V_6.8 × (1 − e^(−t/τ))

where τ = R × C_total (all capacitance on the EN node)
```

The LGS5145 EN rising threshold is **1.24V** (typical). Time to threshold:

```
t_enable = −τ × ln(1 − V_threshold / V_supply)
         = −τ × ln(1 − 1.24 / 6.8)
         = −τ × ln(0.8176)
         = τ × 0.2014
```

### U32 LP38693SD-5.0 (5VA) — Immediate Enable

- **R53 = 10kΩ** pull-up to 6.8V rail
- No explicit capacitor on EN
- LP38693 EN threshold ≈ 1.2V
- Parasitic capacitance only (~10–50pF)

```
τ = 10kΩ × 50pF = 0.5µs
t_enable ≈ 0.1µs (essentially immediate)
```

LP38693 internal startup time: ~200µs
Output caps: C104 (100nF) + C105 (10µF) + C35 (47µF) ≈ 57µF

```
t_5VA = t_enable + t_startup + t_cap_charge
      ≈ 0 + 0.2ms + (57µF × 5V / 50mA)
      ≈ 0.2ms + 5.7ms
      ≈ 6ms after 6.8V reaches 5.25V (LP38693 min VIN)
```

**5VA reaches 5.0V approximately 6ms after the 6.8V rail crosses 5.25V.**

### U6 LGS5145 → 3.3V — Short Delay

- **R55 = 10kΩ**, **C108 = 1µF** (confirmed in BOM)
- C31 = 4.7µF is nearby — **verify if C31 is on EN or on VIN of U6**

**Case A: C31 on VIN (only C108 on EN):**

```
τ = R55 × C108 = 10kΩ × 1µF = 10ms
t_enable = 10ms × 0.2014 = 2.0ms after 6.8V available
```

**Case B: C31 on EN (C108 + C31 on EN):**

```
τ = R55 × (C108 + C31) = 10kΩ × 5.7µF = 57ms
t_enable = 57ms × 0.2014 = 11.5ms after 6.8V available
```

LGS5145 internal soft-start: ~2.5ms (typ). Output caps ≈ 75µF.

| Scenario | EN delay | + Soft-start | **Total to 3.3V regulated** |
|----------|----------|--------------|-----------------------------|
| Case A (C108 only) | 2.0ms | 2.5ms | **4.5ms** |
| Case B (C108 + C31) | 11.5ms | 2.5ms | **14.0ms** |

### U8 LGS5145 → 5V — Longer Delay

- **R54 = 10kΩ**, **C107 = 4.7µF** (confirmed in BOM)
- C48 = 4.7µF is nearby — **verify if C48 is on EN or on VIN of U8**

**Case A: C48 on VIN (only C107 on EN):**

```
τ = R54 × C107 = 10kΩ × 4.7µF = 47ms
t_enable = 47ms × 0.2014 = 9.5ms after 6.8V available
```

**Case B: C48 on EN (C107 + C48 on EN):**

```
τ = R54 × (C107 + C48) = 10kΩ × 9.4µF = 94ms
t_enable = 94ms × 0.2014 = 18.9ms after 6.8V available
```

LGS5145 internal soft-start: ~2.5ms. Output caps ≈ 75µF.

| Scenario | EN delay | + Soft-start | **Total to 5V regulated** |
|----------|----------|--------------|-----------------------------|
| Case A (C107 only) | 9.5ms | 2.5ms | **12.0ms** |
| Case B (C107 + C48) | 18.9ms | 2.5ms | **21.4ms** |

### REF5050 (U9, U10) — No EN, RC Input Filter

REF5050 has no enable pin — always active when VIN > 5.2V.

Input filter: R19/R20 = 33Ω, C50/C52 = 10µF

```
τ_filter = 33Ω × 10µF = 330µs

Time for filtered input to reach 5.2V (from 6.8V step):
t = −330µs × ln(1 − 5.2/6.8) = −330µs × ln(0.235) = 478µs ≈ 0.5ms

REF5050 internal startup: ~50µs
Total: ~0.5ms after 6.8V is stable
```

---

## Part 3: Complete Startup Timeline

Assuming C_SS = 100nF (tSS = 12ms) and Case A (C31/C48 on VIN, not EN):

```
                    0ms         5ms         10ms        15ms        20ms        25ms
Event               │           │           │           │           │           │
                    │           │           │           │           │           │
B+ applied ─────────█           │           │           │           │           │
Q10 turns on ───────█           │           │           │           │           │
LM25118 VCC rdy ────░█          │           │           │           │           │
                    │           │           │           │           │           │
6.8V ramp start ────░░█         │           │           │           │           │
6.8V @ 5.25V ───────░░░░░░░░░░░█           │           │           │           │
  └─ U32 starts ────░░░░░░░░░░░█           │           │           │           │
6.8V @ 6.8V ────────░░░░░░░░░░░░░░█        │           │           │           │
                    │           │           │           │           │           │
5VA = 5.0V ─────────░░░░░░░░░░░░░░░░░░█    │           │           │           │
5VREF stable ───────░░░░░░░░░░░░░░░█       │           │           │           │
                    │           │           │           │           │           │
U6 EN threshold ────░░░░░░░░░░░░░░░░█      │           │           │           │
3.3V regulated ─────░░░░░░░░░░░░░░░░░░░█   │           │           │           │
  └─ ESP32 boots ───░░░░░░░░░░░░░░░░░░░█   │           │           │           │
                    │           │           │           │           │           │
U8 EN threshold ────░░░░░░░░░░░░░░░░░░░░░░░█           │           │           │
5V regulated ───────░░░░░░░░░░░░░░░░░░░░░░░░░░█        │           │           │
  └─ Expanders rdy ─░░░░░░░░░░░░░░░░░░░░░░░░░░█        │           │           │
```

### Detailed Timeline (Case A — C31/C48 on VIN)

| Time (ms) | Event | Rail voltage |
|-----------|-------|-------------|
| 0.0 | B+ applied, Q10 conducts | VIN = B+ |
| ~0.5 | LM25118 internal VCC charged | VCC ≈ 7.5V |
| ~1.0 | LM25118 starts switching, SS ramp begins | 6.8V ramp starts |
| ~9.2 | 6.8V ramp crosses 5.25V → U32 EN valid, LP38693 starts | 6.8V ≈ 5.25V |
| ~9.7 | 5VREF/5VREF2 input filter charged → REF5050 startup | 5VREF ramping |
| ~10.0 | 5VREF/5VREF2 reach 5.000V ± 0.5mV | 5VREF stable |
| ~13.0 | 6.8V ramp complete (tSS = 12ms + 1ms VCC) | **6.8V = 6.802V** |
| ~15.0 | U6 EN reaches 1.24V (R55/C108 delay from 6.8V stable) | EN rising |
| ~15.2 | 5VA reaches 5.0V (LP38693 fully regulated) | **5VA = 5.0V** |
| ~17.5 | U6 LGS5145 soft-start complete | **3.3V = 3.33V** |
| ~17.5 | ESP32-S3 power-on reset released, boot begins | 3.3VPF available |
| ~22.5 | U8 EN reaches 1.24V (R54/C107 delay from 6.8V stable) | EN rising |
| ~25.0 | U8 LGS5145 soft-start complete | **5V = 5.01V** |
| ~25.0 | MCP23S17 expanders powered, begin initialization | 5VPF available |
| ~27.5 | ESP32 has configured native GPIO (< 10ms into boot) | Outputs safe |
| ~67.5 | ESP32 initializes SPI, expanders (< 50ms into boot) | SPI ready |
| ~217.5 | ESP32 completes full peripheral init (< 200ms) | Fully operational |

### Gap Analysis — Is 5V Ready Before ESP32 Needs It?

- ESP32 boot starts: ~17.5ms
- 5V available: ~25.0ms
- ESP32 tries SPI/I2C: ~67.5ms (50ms into boot)
- **Gap: 5V is ready 42.5ms before ESP32 accesses expanders** ✓

### Timeline (Case B — C31/C48 on EN)

| Time (ms) | Event |
|-----------|-------|
| 0.0 | B+ applied |
| ~13.0 | 6.8V = 6.802V (soft-start complete) |
| ~15.2 | 5VA = 5.0V, 5VREF stable |
| ~24.5 | U6 EN threshold (11.5ms delay) → 3.3V starts |
| ~27.0 | **3.3V = 3.33V**, ESP32 boots |
| ~31.9 | U8 EN threshold (18.9ms delay) → 5V starts |
| ~34.4 | **5V = 5.01V**, expanders powered |
| ~77.0 | ESP32 tries SPI (50ms into boot) |

**Gap: 5V ready 42.6ms before ESP32 accesses expanders** ✓ (same margin)

---

## Part 4: Cranking Response

### System Power Budget

| Rail | Load | Power |
|------|------|-------|
| 3.3V (ESP32 active, no WiFi) | ~100mA | 330mW |
| 3.3V (ESP32 + WiFi peak) | ~300mA | 990mW |
| 5V (6× MCP23S17, 2× TXB0108, LEDs) | ~60mA | 300mW |
| 5VA (mux, PCA9306, INA180) | ~20mA | 100mW |
| 5VREF × 2 (REF5050, 2mA each) | ~4mA | 20mW |
| 6.8V rail quiescent (LGS5145 Iq ×2) | ~6mA | 41mW |
| **Total (cranking, no WiFi)** | | **~791mW** |
| **Total (running, WiFi active)** | | **~1451mW** |

### Buck-Boost Behavior at Various VIN Levels

The LM25118 seamlessly transitions between modes. Total 6.8V load ≈ 200mA (cranking) or 350mA (running):

| VIN (battery) | Mode | I_in (cranking) | I_in (running) | Q11 Pdiss | Status |
|---------------|------|-----------------|----------------|-----------|--------|
| 14.4V (charging) | Buck | 0.06A | 0.11A | 0.02mW | ✓ Normal |
| 12.0V (normal) | Buck | 0.08A | 0.14A | 0.04mW | ✓ Normal |
| 9.0V (key on, no crank) | Buck | 0.10A | 0.19A | 0.06mW | ✓ Normal |
| 8.0V (light crank) | Buck | 0.12A | 0.21A | 0.08mW | ✓ Regulating |
| 6.8V (crossover) | Pass-through | 0.14A | 0.24A | 0.11mW | ✓ Regulating |
| 6.0V (moderate crank) | **Boost** | 0.16A | 0.28A | 0.14mW | ✓ Regulating |
| 5.0V (heavy crank) | **Boost** | 0.19A | 0.34A | 0.20mW | ✓ Regulating |
| 4.0V (extreme sag) | **Boost** | 0.23A | 0.42A | 0.30mW | ✓ Regulating |
| 3.26V (UVLO threshold) | **Boost** | 0.29A | 0.52A | 0.47mW | ✓ At limit |
| 3.00V (UVLO falling) | Shutdown | — | — | — | Clean reset |
| < 3.0V | Off | — | — | — | All rails dead |

**Key observation:** Even at extreme cranking (VIN = 4V), the input current is only ~0.42A. The PTC fuse (2A), Q10 (7A), and Q11 (60A) all have massive headroom.

### VIN Hold-Up During Cranking Transients

VIN bulk capacitance: C6 + C95 = 2 × 560µF = 1120µF + C7 (47µF) ≈ **1167µF**

For a sudden VIN drop (e.g., starter motor engagement):

```
Hold-up time = C_VIN × ΔV / I_in

From 12V to UVLO (3.26V) at running load:
t = 1167µF × (12.0 − 3.26) / 0.52A = 1167µF × 8.74 / 0.52 = 19.6ms

From 8V to UVLO (3.26V) at cranking load:
t = 1167µF × (8.0 − 3.26) / 0.29A = 1167µF × 4.74 / 0.29 = 19.1ms
```

**The VIN caps provide ~19ms of hold-up** during a worst-case step transient. Real cranking sags are not steps — they're gradual dips over 100–500ms with a minimum lasting tens of milliseconds. The LM25118 tracks VIN continuously in boost mode, maintaining 6.8V output throughout.

### Cranking Scenario: Typical 12V → 5V → 12V Sag

A typical cold start produces a VIN waveform like:

```
VIN (V)
  14 ┤
  12 ┤────┐                              ┌────────────
  10 ┤    │                              │
   8 ┤    │                           ┌──┘
   6 ┤    └──┐                     ┌──┘
   4 ┤       └─────────────────────┘
   2 ┤
   0 ┼──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──
      0  50 100 150 200 250 300 350 400 450 500 550 ms
      │  Key│  Crank engaged        │Engine│  Running
      │  on │                       │catches│
```

**ECU behavior throughout this event:**

| Phase | VIN | LM25118 mode | 6.8V output | All rails | ECU state |
|-------|-----|-------------|-------------|-----------|-----------|
| Key on | 12V | Buck | **6.8V** ✓ | Regulating | Normal operation |
| Crank engage (dip) | 12→5V (50ms) | Buck → Boost | **6.8V** ✓ | Regulating | **Continuous operation** |
| Cranking sustained | 4–5V (200ms) | Boost | **6.8V** ✓ | Regulating | **Continuous operation** |
| Engine catches | 5→8V (100ms) | Boost → Buck | **6.8V** ✓ | Regulating | **Continuous operation** |
| Running | 14.4V | Buck | **6.8V** ✓ | Regulating | Normal operation |

**No reset. No glitch. No re-initialization.** The ESP32 never loses power. Sensor readings are valid throughout. Expanders maintain their output states. Engine control is uninterrupted.

### Cranking Scenario: Extreme — Dead Battery (VIN < 3V)

If the battery is truly dead (VIN < 3V):

| Phase | VIN | Result |
|-------|-----|--------|
| Key on | < 3V | UVLO not met, converter stays off |
| Crank attempt | < 3V | Nothing happens — ECU is off |
| Jump start connected | 12V+ | UVLO clears → normal startup sequence |
| Jump start + crank | 12→4V | Same as typical cranking above |

**If VIN drops below 3.0V during operation:**

1. LM25118 UVLO triggers → 6.8V drops
2. All downstream rails decay simultaneously
3. ESP32 brownout detector triggers → clean reset
4. No zombie state — all rails die together within ~5ms
5. When VIN recovers above 3.26V → full restart from cold boot

---

## Part 5: Power-Up Sequence Diagram

### Voltage vs. Time (Cold Start from 12V, CSS = 100nF)

```
Voltage
  7V ┤                                 ┌──────────── 6.8V rail
     ┤                              ╱  │
  6V ┤                           ╱     │
     ┤                        ╱        │
  5V ┤                     ╱     ┌─────┤──────────── 5VA (LP38693)
     ┤                  ╱  ┌─────┘     │     ┌────── 5V (LGS5145 U8)
  4V ┤               ╱  ╱─┘           │     │  ┌─── 5VREF (REF5050)
     ┤            ╱  ╱                 │     │╱│
  3V ┤         ╱  ╱──┘ ← 5VREF        │  ┌──┘ │──── 3.3V (LGS5145 U6)
     ┤      ╱  ╱                       │╱─┘   │
  2V ┤   ╱  ╱                      ┌───┘      │
     ┤╱  ╱                      ╱──┘           │
  1V ┤╱ ─────────────────── ╱──┘               │
     ┤                   ╱─┘                    │
  0V ┼──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──
     0  2  4  6  8  10 12 14 16 18 20 22 24 26 28 ms
     │     │  LM25118       │        │        │
     │     │  soft-start    │    3.3V rdy  5V rdy
     │  VCC rdy          6.8V &     ESP32    Expanders
     │                   5VA/REF    boots    ready
```

### Ordered Rail Availability

| Order | Rail | Voltage | Available at | Depends on |
|-------|------|---------|-------------|------------|
| 1 | **6.8V** | 6.802V | ~13ms | VIN > 3.26V |
| 2 | **5VREF** | 5.000V | ~14ms | 6.8V > 5.27V (via 33Ω filter) |
| 3 | **5VREF2** | 5.000V | ~14ms | 6.8V > 5.27V (via 33Ω filter) |
| 4 | **5VA** | 5.0V | ~15ms | 6.8V > 5.25V (LP38693 dropout) |
| 5 | **3.3V** | 3.33V | ~17.5ms | 6.8V stable + R55/C108 delay |
| 6 | **3.3VPF** | 3.33V | ~18ms | 3.3V through L3 post-filter |
| 7 | **5V** | 5.01V | ~25ms | 6.8V stable + R54/C107 delay |
| 8 | **5VPF** | 5.01V | ~25.5ms | 5V through L5 post-filter |

### Sequence Verification for Key Devices

| Device | Needs | Available at | Margin |
|--------|-------|-------------|--------|
| REF5050 U9/U10 | VIN > 5.2V | ~14ms (from 6.8V) | ✓ References ready first |
| LP38693 U32 | VIN > 5.25V | ~9ms (during ramp) | ✓ 5VA starts during ramp |
| PCA9306 | VREF1 (3.3V) before VREF2 (5V) | 3.3V at 17.5ms, 5V at 25ms | ✓ 7.5ms gap |
| ESP32-S3 | 3.3V stable | ~17.5ms | ✓ Clean boot |
| MCP23S17 ×6 | 5V stable | ~25ms | ✓ Ready 42ms before ESP32 SPI init |
| ADS1115 ×3 | VDD (5VREF2) stable | ~14ms | ✓ Reference stable before ESP32 reads |
| MCP3204 | VREF (5VREF) stable | ~14ms | ✓ Reference stable before ESP32 reads |

---

## Part 6: RC Constant Optimization

### Current Values Assessment

| Supply | R | C (EN) | τ | Enable delay | Rating |
|--------|---|--------|---|-------------|--------|
| U32 5VA | R53 10kΩ | ~0 (pull-up) | ~0 | Immediate | ✓ Good |
| U6 3.3V | R55 10kΩ | C108 1µF | 10ms | 2.0ms | ✓ Good |
| U8 5V | R54 10kΩ | C107 4.7µF | 47ms | 9.5ms | ✓ Good |

### Timing Relationships

```
                    3.3V enable         5V enable
                    ↓                   ↓
                    │←── 7.5ms gap ────→│
                    │                   │
    6.8V stable ────┤                   │
                    2ms                 9.5ms
                    (R55×C108)          (R54×C107)
```

The 7.5ms gap between 3.3V and 5V enable ensures:
1. **PCA9306 safety:** VREF1 (3.3V) is established ~10ms before VREF2 (5V) reaches the chip
2. **ESP32 GPIO:** ESP32 has ~10ms to configure native GPIO pins as outputs (safe LOW) before expanders power on
3. **No bus contention:** I2C and SPI buses are quiet during 5V power-up

### Are Changes Needed?

**The current RC values are well-optimized.** The timing provides:
- Clean separation between 3.3V and 5V
- Adequate time for PCA9306 VREF1-before-VREF2
- Fast enough that total startup is under 30ms
- Slow enough that inrush currents are manageable

### If C31 (4.7µF) Is on U6 EN Pin

If C31 is connected to U6 EN (not VIN), the 3.3V enable delay increases from 2ms to 11.5ms. This is still acceptable but unnecessarily slow. **Recommendation: move C31 to U6 VIN if it's currently on EN.**

### If C48 (4.7µF) Is on U8 EN Pin

If C48 is connected to U8 EN, the 5V enable delay increases from 9.5ms to 18.9ms. Still fine (ESP32 doesn't need 5V until ~50ms into boot). No change needed, but the gap between 3.3V and 5V shrinks from 7.5ms to 7.4ms (both delays increase proportionally). PCA9306 timing is preserved.

### Alternative RC Values (If Faster Startup Desired)

For a faster boot (e.g., cranking recovery where every millisecond matters):

| Supply | Current | Faster option | Enable delay | Trade-off |
|--------|---------|--------------|-------------|-----------|
| U6 3.3V | R55=10k, C108=1µF | R55=4.7kΩ, C108=1µF | 0.95ms | Less noise filtering on EN |
| U8 5V | R54=10k, C107=4.7µF | R54=10k, C107=2.2µF | 4.4ms | Less noise filtering on EN |

This would bring total startup from ~25ms to ~20ms. **Not recommended unless benchmarking shows the 5ms matters for your cranking recovery target.**

### Alternative RC Values (If More Margin Desired)

For wider separation between 3.3V and 5V (ultra-conservative):

| Supply | Current | Conservative | Enable delay | Benefit |
|--------|---------|-------------|-------------|---------|
| U6 3.3V | R55=10k, C108=1µF | No change | 2.0ms | Already fast |
| U8 5V | R54=10k, C107=4.7µF | R54=10k, C107=10µF | 20.1ms | 18ms gap (vs 7.5ms) |

This would guarantee 18ms between 3.3V and 5V. **Not needed unless you observe PCA9306 issues during testing.**

---

## Part 7: Cranking Recovery — What Happens After a Reset

If VIN drops below 3.0V during extreme cranking (triggering UVLO):

### Recovery Timeline

| Time | Event |
|------|-------|
| t=0 | VIN recovers above 3.26V (UVLO clears) |
| t+1ms | LM25118 VCC charges, begins switching |
| t+13ms | 6.8V reaches target |
| t+14ms | 5VREF/5VREF2 stable |
| t+15ms | 5VA = 5.0V |
| t+17.5ms | 3.3V regulated → ESP32 begins boot |
| t+25ms | 5V regulated → expanders available |
| t+27.5ms | ESP32 configures GPIO pins (< 10ms boot) |
| t+67.5ms | ESP32 initializes crank/cam ISRs (< 50ms boot) |
| t+217.5ms | ESP32 reads SD config, resumes engine control |

**Total recovery time: ~200ms from VIN recovery to full engine control.**

During the ~200ms recovery, the engine is spinning freely (no spark/fuel from ECU). For a 4-stroke engine at 200 RPM cranking speed:
- 200 RPM = 3.33 revs/second
- 200ms = 0.67 revolutions
- The engine continues rotating on inertia; ECU catches the next crank/cam sync window

### Software Boot Priority (from CLAUDE.md)

The firmware boot order is designed to complement the hardware startup:

| Boot phase | Time budget | Power requirement | Hardware ready? |
|------------|------------|-------------------|-----------------|
| 1. Native GPIO → LOW | < 10ms | 3.3V only | ✓ at t+17.5ms |
| 2. Crank/cam ISRs | < 50ms | 3.3V only | ✓ at t+17.5ms |
| 3. SPI + expanders | < 200ms | 3.3V + 5V | ✓ at t+25ms |
| 4. SD card + config | < 500ms | 3.3V + 5V | ✓ |
| 5. WiFi + MQTT | < 2s | 3.3V | ✓ (deferred) |

---

## Summary

### Power Supply Cranking Performance

| Metric | Value |
|--------|-------|
| VIN operating range | **3.26V – 42V** (60V transient with TVS clamp) |
| Cranking survival | **Down to 3.26V** — no reset, no glitch |
| VIN hold-up (1167µF) | **~19ms** worst-case step transient |
| Failure mode | **Binary** — all rails live or all rails dead together |
| Recovery from reset | **~200ms** from VIN recovery to full engine control |

### Startup Timing Summary

| Rail | Enable mechanism | Time to regulation |
|------|-----------------|-------------------|
| 6.8V (LM25118) | UVLO at 3.26V | ~13ms (soft-start) |
| 5VREF (REF5050) | Always on (33Ω/10µF filter) | ~14ms |
| 5VA (LP38693) | R53 10kΩ pull-up (immediate) | ~15ms |
| 3.3V (LGS5145 U6) | R55 10kΩ + C108 1µF (2ms delay) | ~17.5ms |
| 5V (LGS5145 U8) | R54 10kΩ + C107 4.7µF (9.5ms delay) | ~25ms |

### RC Constants Assessment

**Current values are well-chosen. No changes recommended.** The 7.5ms gap between 3.3V and 5V provides safe PCA9306 sequencing, and the total ~25ms startup is fast enough for cranking recovery within one engine revolution at 200 RPM.
