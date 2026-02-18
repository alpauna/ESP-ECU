# Power Supply Analysis 4 — Detailed Review with Corrections

**Date:** 2026-02-17
**Schematic:** SCH_ECU_1-POWER_2026-02-17.pdf (latest revision)
**BOM:** BOM_Board1_ECU_2026-02-17.xlsx (latest revision)
**References:** LM25118 datasheet (TI SNVS655), LP38692 datasheet (TI SNVS578), DMP6023LE-13 datasheet, ROHM UDZVFHTE-1713B datasheet

---

## Corrections to Previous Analyses

**Analysis 2 and 3 contained errors.** These are corrected here:

| What I said | Reality | Impact |
|-------------|---------|--------|
| "L78M05CDT-TR U7" is the analog 5VA LDO | **No U7 exists. No L78M05.** The analog LDO is **U33 LP38692SDX-3.3/NOPB** (3.3V, not 5V) | The entire L78M05 noise/PSRR analysis was about a part that isn't on the board |
| "220µF/6.3V caps C10/C11/C27 exceed voltage rating" on 6.4V rail | **C10/C11/C27 don't exist.** Output ceramics are **C97-C103: 7× 100µF/10V** (muRata GRM31CR61A107MEA8L) | The "critical" 220µF cap finding was wrong — the actual caps are properly rated |
| D6 UDZVFHTE-1713B is a "6.4V Zener" | D6 is a **13V Zener** (ROHM "13B" = 13V). The "6.4V" on the schematic is the nearby net label, not the Zener voltage | D6 is a VGS clamp for Q10, not a rail voltage reference |

---

## Current Topology (Verified from BOM + Schematic)

```
B+ → F1 (JK250-200U PTC 2A) → Q10 (DMP6023LE-13 PMOS, -60V)
    │   D6 (13V Zener): VGS clamp on Q10 gate
    │   D13 (PMEG6020EP): body diode bypass for Q10
    │   R7 (10k): Q10 gate pull-down to GND
    │
    └── VIN bus → D1 (B530C-13-F 30V Schottky, purpose TBD)
         │         D2/D3 (PESD5Z3.3 ESD protection)
         │
         └── LM25118 Buck-Boost (U1) + Q1/Q3 (CSD17575Q3) → 6.8V / 3A+
              │   R12=4.53kΩ, R9=1kΩ → Vout = 1.23 × (1 + 4.53) = 6.802V
              │   D4/D5: SMF6.5A TVS (⚠ WRONG — see below)
              │
              ├── U33 LP38692SDX-3.3/NOPB → 3.3VA (analog)
              │    R53 (10k) EN pull-up → enables immediately
              │    └── INA180A1, CD74HC4067 mux, PCA9306 (VREF1?)
              │
              ├── U6 LGS5145 → 3.3V → L3 (10µH) → 3.3VPF
              │    R55 (10k) + C108 (1µF) on EN → τ ≈ 10ms, enables ~2ms
              │    D10 (SMAJ3.3A) TVS on 3.3VPF
              │    └── ESP32-S3
              │
              ├── U8 LGS5145 → 5V → L5 (10µH) → 5VPF
              │    R54 (10k) + C107 (4.7µF) on EN → τ ≈ 47ms, enables ~10ms
              │    D12 (SMF5.0A) TVS on 5VPF
              │    └── MCP23S17 ×6, TXB0108 ×2, peripherals
              │
              ├── U9 REF5050 (via R19 33Ω filter) → 5VREF
              │    └── MCP3204 VREF
              │
              └── U10 REF5050 (via R20 33Ω filter) → 5VREF2
                   └── ADS1115 ×3 VDD

         AGND ←→ GND via L6 (BLM21PG221SN1D) + C39/C49
```

---

## Output Voltage Verification

### 6.8V rail (LM25118)

R12 = 4.53kΩ (YAGEO RC0201DR-074K53L, 1%), R9 = 1kΩ (UNI-ROYAL, 1%)

```
Vout = 1.23V × (1 + 4.53/1.0) = 1.23 × 5.53 = 6.802V
```

Worst-case (VFB ±1.5%, resistors ±1%):
- Min: 1.212 × (1 + 4.485/1.010) = 1.212 × 5.441 = **6.594V**
- Max: 1.248 × (1 + 4.575/0.990) = 1.248 × 5.621 = **7.015V**

### 3.3V rail (U6 LGS5145)

R13 = 6.2kΩ, R14 = 2kΩ. VFB = 0.812V.

```
Vout = 0.812 × (6.2 + 2) / 2 = 0.812 × 4.1 = 3.329V
```

### 5V rail (U8 LGS5145)

R17 = 9.31kΩ, R18 = 1.8kΩ. VFB = 0.812V.

```
Vout = 0.812 × (9.31 + 1.8) / 1.8 = 0.812 × 6.172 = 5.012V
```

### 3.3VA rail (U33 LP38692SDX-3.3)

Fixed 3.3V output (internal reference, ±2.5% typical).
Input = 6.8V → headroom = 3.5V → dropout at 50mA ≈ 50mV → massive margin.

### REF5050 inputs

U9/U10 powered from 6.8V through 33Ω (R19/R20) + 10µF (C50/C52):
- Filter voltage drop: 33Ω × 2mA = 0.066V
- Effective VIN: 6.8V − 0.07V = 6.73V
- REF5050 min VIN = 5.2V → headroom = **1.53V** ✓

---

## What's Been Fixed (Implementing Previous Recommendations)

### 1. Output voltage raised to 6.8V ✓

R12 changed from 4.22kΩ to 4.53kΩ. This gives much better downstream headroom:

| Device | Min input needed | Headroom at 6.4V (old) | Headroom at 6.8V (new) |
|--------|-----------------|------------------------|------------------------|
| LGS5145 → 5V (1A load, Dmax=94%) | 5.92V | 0.48V (tight) | **0.88V** ✓ |
| LGS5145 → 5V (worst Dmax=90%) | 6.16V | 0.24V (marginal) | **0.64V** ✓ |
| LGS5145 → 3.3V | 3.70V | 2.70V | **3.10V** ✓ |
| LP38692 → 3.3VA | 3.35V | 3.05V | **3.45V** ✓ |
| REF5050 (incl. 33Ω filter) | 5.27V | 1.13V | **1.53V** ✓ |

The 5V LGS5145 headroom improvement from 0.48V to 0.88V is the main win — this converter was operating on the edge at 6.4V.

### 2. Output capacitors properly voltage-rated ✓

C97-C103: 7× muRata GRM31CR61A107MEA8L (**100µF, 10V, X5R, 1206**)

Part number decoded: GRM31 (1206) / C (1.6mm height) / R (paper tape) / **6** (X5R) / **1A** (10V) / 107 (100µF) / M (±20%)

At 6.8V operating (68% of 10V rating):
- DC bias retention: ~55-75% (use muRata SimSurfing for exact value)
- Conservative effective capacitance: 7 × 55µF = **385µF**
- Optimistic effective capacitance: 7 × 75µF = **525µF**

Compare to the old 220µF/6.3V at 6.4V (102% of rating): ~20% retention = 4 × 44µF = 176µF effective. **The new caps provide 2-3× more effective capacitance with proper voltage margin.**

### 3. Enable ramp-up / power sequencing ✓

The user added RC delays on the LGS5145 EN pins:

| Converter | RC components | τ (RC) | Time to EN threshold (1.24V) | Sequence |
|-----------|--------------|--------|------------------------------|----------|
| U33 (3.3VA) | R53 (10k) pull-up, no cap | ~0 | Immediate | **1st** |
| U6 (3.3V) | R55 (10k) + C108 (1µF) | 10ms | ~2ms | **2nd** |
| U8 (5V) | R54 (10k) + C107 (4.7µF) | 47ms | ~10ms | **3rd** |

Note: If C31 (4.7µF) is also on U6 EN, and C48 (4.7µF) is also on U8 EN, the effective time constants increase but the relative order is preserved.

**Sequence analysis for PCA9306 I2C level shifter:**
- VREF1 (3.3V from U6) comes up at ~2ms ✓
- VREF2 (5V from U8) comes up at ~10ms ✓
- VREF1 before VREF2 → **correct order, prevents PCA9306 latch-up** ✓

### 4. LP38692SDX-3.3 replaces previous analog LDO ✓

The analog supply is now **3.3V** (not 5V) from U33 LP38692SDX-3.3/NOPB:

| Parameter | LP38692-3.3 (U33) |
|-----------|-------------------|
| Output | 3.3V fixed (±2.5%) |
| Input range | 2.7V – 10V (abs max 12V) |
| Max current | 1A |
| Dropout @ 1A | 450mV |
| Dropout @ 50mA | ~50mV |
| Noise | ~30 µVrms |
| PSRR @ 120Hz | ~55 dB |
| PSRR @ 100kHz | ~50 dB |
| Ceramic stable | Yes (5mΩ – 500mΩ ESR) |
| EN threshold | ~0.8–1.2V |
| SNS pin | Remote voltage sense |
| Package | WSON-6 (3×3mm) |

The LP38692 running at 3.3V from 6.8V:
- VIN margin: 6.8V vs 10V max = **3.2V margin** ✓
- Dropout: 50mV at light load, headroom = 3.5V → **massive margin** ✓
- Power dissipation: (6.8−3.3) × 50mA = 175mW → fine for WSON-6 ✓
- Noise: 30µVrms — adequate for analog mux and level shifter

**Note:** This changes the analog supply from 5V to 3.3V. The CD74HC4067 mux operates from 2-6V, so 3.3V works. However, the mux's signal pass range becomes 0-3.3V instead of 0-5V. If any analog inputs exceed 3.3V, they will be clipped by the mux. Verify that all multiplexed sensor signals stay within 0-3.3V, or power the mux from 5VPF instead.

---

## What's Done Right

This design has strong fundamentals. Specific wins beyond the fixes above:

1. **Q10 DMP6023LE-13** (-60V, 28mΩ, SOT-223) — excellent P-FET for reverse polarity protection. Handles full automotive voltage range including load dump.

2. **D6 UDZVFHTE-1713B (13V Zener)** — VGS clamp on Q10. When VIN > 13V (normal 14.4V operation), D6 limits VGS to −13V, protecting the ±20V VGS rating during load dump transients (40V+ VIN). At VIN < 13V (cranking), VGS = −VIN naturally, and D6 doesn't conduct. This is textbook automotive PMOS gate protection.

3. **D13 PMEG6020EP** — body diode bypass Schottky for Q10. Reduces power path voltage drop by bypassing Q10's body diode during forward conduction. At 2A: ~0.3V drop through D13 vs ~0.7V through body diode = 0.8W savings.

4. **C6 + C95 = 2× 560µF** (Taiyo Yuden 25V electrolytic) — 1120µF on VIN provides ~10ms hold-up during cranking transients and damps hot-plug inrush.

5. **D10 SMAJ3.3A + D12 SMF5.0A** — TVS protection on both downstream rails. SMAJ3.3A (600W peak pulse) on 3.3V and SMF5.0A (200W) on 5V protect against transients conducted through the LGS5145 converters.

6. **Mixed output capacitor bank** on 6.8V rail:

| Component | Type | Value | Role |
|-----------|------|-------|------|
| C1-C4 | KEMET T491D tantalum (25V) | 4 × 47µF | ESR damping for control loop stability |
| C15-C16 | Panasonic 16SVPG270MX polymer (16V) | 2 × 270µF | Low-ESR bulk energy storage |
| C97-C103 | muRata GRM31CR61A107MEA8L ceramic (10V) | 7 × 100µF | Ultra-low ESR fast transient response |
| C28-C29 | KEMET T491D tantalum (25V) | 2 × 47µF | Additional bulk near downstream converters |
| **Total** | | **~1528µF nominal** | **~1000-1100µF effective** |

The tantalum + polymer + ceramic mix provides the ESR profile needed for LM25118 loop stability while maintaining excellent transient response.

7. **LM25118 support circuitry** is well-designed:
   - R2 = 18.7kΩ (RT) → sets switching frequency ~250-300kHz
   - R3 = 21.5kΩ / R6 = 13kΩ (UVLO) → turn-on at VIN > 3.26V, turn-off at ~3.05V
   - R10 = 10mΩ (BOURNS CRA2512) → current sense for overcurrent protection
   - R8 = 15.4kΩ, C13 = 22pF, C17 = 270pF → Type II compensation network
   - L1 = XAL7070-332MEC 3.3µH (14A sat, 7mΩ DCR) → properly rated shielded inductor

8. **Post-filter stages** — L3/L5 (10µH) with output caps provide additional LC filtering between the LGS5145 switching converters and sensitive loads. The 3.3VPF and 5VPF rails are cleaner than the raw converter outputs.

---

## CRITICAL: D4/D5 SMF6.5A TVS on 6.8V Rail

**This is the most urgent issue remaining.**

D4 and D5 are SMF6.5A TVS diodes on the buck-boost output rail. With R12 now at 4.53kΩ, the output is **6.802V** — which is **0.3V above the 6.5V standoff voltage**.

| Parameter | SMF6.5A spec | 6.8V rail | Status |
|-----------|-------------|-----------|--------|
| VR (standoff) | 6.5V | 6.802V nominal | **0.3V OVER standoff** |
| VBR (breakdown) | 7.22V min | 7.015V worst-case max | **Approaches breakdown** |

**What happens:** At 6.8V steady state, the TVS operates 0.3V above its rated working voltage. Leakage current increases significantly — potentially tens of µA to low mA. This:
- Loads the converter continuously (wastes power)
- Heats the TVS (reduces reliability)
- At worst-case high output (7.015V), the TVS is only 0.2V below breakdown — transient ripple could push it into conduction, clamping the rail and fighting the regulator

**Fix — replace D4/D5 with:**

| Option | Part | Standoff | Breakdown (min) | Clamp @ 1A | Margin from 6.802V |
|--------|------|----------|-----------------|------------|---------------------|
| **SMF7.5A (recommended)** | Littelfuse | 7.5V | 8.33V | 12.1V | **0.70V** ✓ |
| SMF8.0A | Littelfuse | 8.0V | 8.89V | 12.9V | **1.20V** ✓ |

SMF7.5A provides 0.70V margin (adequate) with a lower clamping voltage (12.1V vs 12.9V). All downstream devices handle either clamp voltage easily (LGS5145 abs max = 60V, REF5050 abs max = 18V).

**This must be fixed before powering the board.** The TVS will conduct at 6.8V and may prevent the rail from reaching target voltage.

---

## IMPORTANT: Q1 CSD17575Q3 (30V) Limits VIN Range

*Unchanged from previous analyses — still present.*

Q1 (buck high-side FET) drain connects to VIN. When Q1 is off, VDS = VIN. The CSD17575Q3 is rated **30V VDS**.

The LM25118 handles 3V-42V, but Q1 caps the effective input at **<28V** (with 80% derating):

| Scenario | VIN | Q1 (30V) margin |
|----------|-----|-----------------|
| Normal (14.4V charging) | 14.4V | 15.6V ✓ |
| Jump start (24V) | 24V | 6V ✓ |
| Clamped load dump (modern) | ~27V | **3V** ⚠️ |
| Unclamped load dump | 40-65V | **EXCEEDS ABS MAX** ✗ |

Note: U2 (MBR20200CS, 200V) and D7 (STPS20L15G, 15V) are correctly rated — only Q1 is the weak link. Q3 (boost low-side) only sees VOUT ≈ 6.8V, so its 30V rating is fine.

**Fix options (same as before):**

1. **Replace Q1 only** with a 60V+ FET: CSD19536KTT (100V, 4.6mΩ) or BSC070N10NS (100V, 7mΩ). Keep Q3 as CSD17575Q3.

2. **Add VIN TVS**: SMBJ30A (30V standoff, 48.4V clamp) on VIN between Q10 and U1. Limits transients to <30V at Q1 drain. Note: F1 (PTC 2A) cannot limit current fast enough for load dump — TVS must handle the energy.

3. **Accept the limitation** if the vehicle has centralized load dump suppression (modern ECU-controlled alternators). Document the 28V max VIN requirement.

---

## MINOR: D12 SMF5.0A on 5V Rail

The 5V rail is nominally 5.012V (from R17/R18 calculation). SMF5.0A standoff = 5.0V.

- Nominal: 5.012V vs 5.0V standoff → **12mV above standoff**
- Worst case: 5.14V → 0.14V above standoff

At these voltages, TVS leakage is typically < 5µA — negligible and not harmful. The breakdown voltage (5.56V min) provides adequate clamping margin. **This is acceptable as-is**, though SMF5.5A would provide cleaner margin if changing D4/D5 already triggers a BOM revision.

---

## Enable Sequencing Detail

The power-up sequence with the RC delays:

```
Time →  0ms     1ms     2ms     5ms     10ms    20ms
        │       │       │       │       │       │
6.8V:   ████████████████████████████████████████████  (LM25118 output)
3.3VA:  ░░██████████████████████████████████████████  (U33 - immediate enable)
3.3V:   ░░░░░░░░████████████████████████████████████  (U6 - R55/C108, ~2ms)
5V:     ░░░░░░░░░░░░░░░░░░░░████████████████████████  (U8 - R54/C107, ~10ms)
REF:    ░░░░░░░░░░░░░░░░░░░░░░░░████████████████████  (U9/U10 - after 6.8V stable)
```

This ensures:
1. Analog reference (3.3VA) available first for clean analog startup
2. 3.3V digital (ESP32) starts before 5V digital (expanders)
3. PCA9306 VREF1 (3.3V) established before VREF2 (5V) → no latch-up ✓
4. Expanders start last → ESP32 has time to configure GPIO pins as outputs before expanders initialize

---

## Remaining Recommendations

### Priority 1: CRITICAL — Fix D4/D5 TVS

Replace SMF6.5A with **SMF7.5A** on the 6.8V output rail. Same SOD-123FL package, same price. Must be done before first power-on.

### Priority 2: IMPORTANT — Q1 voltage rating

Replace Q1 CSD17575Q3 (30V) with a 60V+ or 100V N-FET. Or add SMBJ30A TVS on VIN. Required for automotive load dump survival.

### Priority 3: RECOMMENDED — CFF feed-forward capacitors

Add 47pF across the top feedback resistor on each LGS5145:

- U8 (5V): 47pF across R17 (9.31kΩ)
- U6 (3.3V): 47pF across R13 (6.2kΩ)

These improve load transient response and add phase margin, especially important since U6/U8 now run from a buck-boost output (which has higher source impedance than a direct battery connection). Zero risk, one 0201 cap per converter.

### Priority 4: RECOMMENDED — Verify analog signal range

With U33 providing 3.3VA instead of 5VA, verify that:
- CD74HC4067 mux signal inputs stay within 0-3.3V (if mux VCC = 3.3VA)
- If any sensors output 0-5V, the mux will clip above 3.3V → consider powering mux from 5VPF instead and using the TXB0108 for control line level shifting (which is already present)
- PCA9306 VREF2: verify this connects to 5V (not 3.3VA), otherwise no level shifting occurs

### Priority 5: LAYOUT — Same as before

- Kelvin sense on R4 (INA180A1 shunt): route IN+ and IN- traces directly to the resistor pads, not through the power copper
- LM25118 switching loop (VIN→Q1→L1→Q3→PGND): keep tight, minimize loop area
- AGND star ground at L6 ferrite bead: all analog returns through L6 to single point

### Priority 6: SAFETY — Coil/injector driver pull-downs

*Still needed from every previous analysis:*
10kΩ-47kΩ pull-down resistors from each coil and injector MOSFET gate to GND. When the 5V rail drops out (or MCP23S17 expanders reset), these ensure driver gates are pulled LOW (no fire, no injection). This is passive hardware safety that no software can replace.

---

## Summary

| # | Status | Item |
|---|--------|------|
| 1 | **CRITICAL — fix now** | D4/D5: SMF6.5A → SMF7.5A (TVS conducts at 6.8V nominal) |
| 2 | **IMPORTANT** | Q1: CSD17575Q3 30V limits VIN to <28V |
| 3 | ✅ Fixed | R12 = 4.53kΩ → 6.802V output |
| 4 | ✅ Fixed | C97-C103: 100µF/10V ceramics (properly rated) |
| 5 | ✅ Fixed | Enable sequencing via RC on EN pins |
| 6 | ✅ Fixed | U33 LP38692SDX-3.3 analog LDO |
| 7 | ✅ Good | Q10 DMP6023LE-13 (-60V) reverse polarity |
| 8 | ✅ Good | D6 13V Zener VGS clamp for Q10 |
| 9 | ✅ Good | D10/D12 TVS on downstream rails |
| 10 | ✅ Good | Mixed output cap bank (tantalum + polymer + ceramic) |
| 11 | Recommended | CFF 47pF on LGS5145 feedback dividers |
| 12 | Recommended | Verify analog signal range with 3.3VA supply |
| 13 | Safety | Coil/injector driver gate pull-downs (other schematic pages) |

**Bottom line:** The design is much improved. The two remaining issues — D4/D5 TVS standoff and Q1 voltage rating — are the same two items that need attention. Everything else is solid engineering.
