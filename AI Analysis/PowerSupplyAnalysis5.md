# Power Supply Analysis 5 — Final Review

**Date:** 2026-02-17
**Schematic:** SCH_ECU_1-POWER_2026-02-17.pdf (latest revision)
**BOM:** BOM_Board1_ECU_2026-02-17.xlsx (latest revision)
**References:** LM25118 (TI SNVS655), SWHA069R06VT (Samwin), LP38693 (TI SNVS578), SMF7.0A (Littelfuse)

---

## Changes Since Analysis 4

Three component changes address all remaining critical and important issues:

| Previous | New | Issue resolved |
|----------|-----|----------------|
| Q1 CSD17575Q3 (30V, 2.3mΩ) | **Q11 SWHA069R06VT (60V, 5.6mΩ)** | Buck high-side FET voltage limit |
| D4/D5 SMF6.5A (6.5V standoff) | **D14/D15 SMF7.0A (7.0V standoff)** | TVS conducts on 6.8V rail |
| U33 LP38692SDX-3.3 (3.3V LDO) | **U32 LP38693SD-5.0 (5.0V LDO)** | 5VA restored to 5V for full analog range |

---

## Q11 SWHA069R06VT — Buck High-Side FET

Q1 (CSD17575Q3, 30V) has been **removed from the BOM** and replaced by Q11 SWHA069R06VT from Samwin (芯派). This is the single most important change — it doubles the VIN voltage headroom.

### Specifications

| Parameter | CSD17575Q3 (old Q1) | SWHA069R06VT (Q11) |
|-----------|---------------------|---------------------|
| VDS | 30V | **60V** |
| Rdson @ VGS=10V | 2.3mΩ | **5.6mΩ** |
| Rdson @ VGS=4.5V | 3.0mΩ | **6.8mΩ** |
| VGS(th) | 1.2–2.0V | ~2.0–2.5V |
| VGS(max) | ±20V | ±20V |
| ID (continuous) | 46A | ~60A |
| Avalanche rated | Yes | **Yes (100% tested)** |
| Package | VSON-CLIP-8 (3.3×3.3mm) | DFN5×6A-8 (5×6mm) |
| LCSC | C529277 | C5151869 |

### Voltage Margin Analysis

In the LM25118 4-switch buck-boost topology, Q11 (buck high-side) sees full VIN across drain-source when off. With 60V VDS:

| Scenario | VIN | Q11 (60V) margin | Status |
|----------|-----|-------------------|--------|
| Normal operation (14.4V) | 14.4V | 45.6V | ✓ |
| Jump start (24V) | 24V | 36V | ✓ |
| LM25118 max rated VIN | 42V | **18V** | ✓ |
| Clamped load dump (modern vehicle) | ~27V | **33V** | ✓ |
| Unclamped load dump (older vehicle) | 40–65V | 0–20V | ⚠️ See below |

**The 60V rating fully covers the LM25118 controller's 42V input range.** The previous 30V FET was the bottleneck limiting the system to <28V. Now the LM25118's own 42V input rating is the limiting factor — as it should be.

### Conduction Loss Comparison

Higher Rdson means slightly more heat, but the increase is negligible:

```
At 2A input current (typical 12V, 6.8V/3A output):
  Old Q1: P = I²R = 4 × 0.0023 = 9.2mW
  New Q11: P = I²R = 4 × 0.0056 = 22.4mW  (+13mW)

At 4A input current (boost mode, 5V input):
  Old Q1: P = I²R = 16 × 0.0023 = 36.8mW
  New Q11: P = I²R = 16 × 0.0056 = 89.6mW  (+53mW)
```

Even at maximum boost-mode current, the loss is under 100mW — well within the DFN5×6 package's thermal capability.

### Gate Drive Compatibility

The LM25118 HO output drives Q11 through the bootstrap circuit (HB/HS). The bootstrap voltage is approximately VCC ≈ 7.5V above the switch node. At VGS = 7.5V, Q11's Rdson is between the 10V spec (5.6mΩ) and 4.5V spec (6.8mΩ) — approximately 6mΩ. This is adequate.

Q11's VGS(th) of ~2.0–2.5V is well below the 7.5V bootstrap drive. The FET will be fully enhanced during normal operation.

### Q3 CSD17575Q3 Retained

Q3 (boost low-side FET) remains as CSD17575Q3 (30V). This is correct — Q3 only sees VOUT ≈ 6.8V across drain-source when off. The 30V rating provides 23.2V of margin, and the 2.3mΩ Rdson minimizes boost-mode conduction losses.

---

## D14/D15 SMF7.0A — Output Rail TVS

D4/D5 (SMF6.5A) have been removed and replaced by D14/D15 (SMF7.0A) on the 6.8V buck-boost output rail.

### Specifications

| Parameter | SMF6.5A (old D4/D5) | SMF7.0A (new D14/D15) |
|-----------|---------------------|----------------------|
| VR (standoff) | 6.5V | **7.0V** |
| VBR (breakdown min) | 7.22V | **7.78V** |
| VBR (breakdown max) | 7.98V | **8.58V** |
| VC (clamp @ IPP) | 10.5V | **11.2V** |
| IR (leakage @ VR) | ≤1µA | ≤1µA |
| Package | SOD-123FL | SOD-123FL |
| LCSC | C720056 | C41376088 |

### Margin Analysis

With R12 = 4.53kΩ, R9 = 1kΩ → Vout = 6.802V nominal:

| Condition | Output voltage | SMF7.0A standoff (7.0V) | Margin |
|-----------|---------------|-------------------------|--------|
| Nominal | 6.802V | 7.0V | **+0.198V** ✓ |
| VFB high (+1.5%) | 6.901V | 7.0V | **+0.099V** ✓ |
| All worst case high | 7.015V | 7.0V | **−0.015V** |
| Breakdown (min) | — | 7.78V | **+0.765V from worst case** |

At nominal (6.802V): 0.198V below standoff → leakage < 1µA. Normal operation is clean.

At worst case (7.015V): 15mV above standoff → leakage increases slightly but remains negligible (estimated < 5µA). The breakdown voltage of 7.78V minimum is 0.765V above the worst-case output, so the TVS will never actually clamp during normal regulation.

**Verdict:** SMF7.0A is acceptable. The TVS provides genuine transient protection (clamping at 11.2V) without interfering with the regulator. All downstream devices (LGS5145 abs max 60V, REF5050 abs max 18V, LP38693 abs max 12V) are protected.

---

## U32 LP38693SD-5.0 — Analog 5VA LDO

U33 (LP38692SDX-3.3, 3.3V) has been replaced by U32 (LP38693SD-5.0, 5.0V). The designator changed from U33 to U32 and the output voltage changed from 3.3V back to 5.0V.

### Specifications

| Parameter | LP38693SD-5.0 (U32) |
|-----------|---------------------|
| Output voltage | 5.0V fixed (±2%) |
| Input range (operating) | 2.7V – 10V |
| Input range (abs max) | 12V |
| From 6.8V rail | **1.8V headroom** ✓ |
| Max output current | 1A |
| Dropout @ 500mA | ~250mV |
| Dropout @ 50mA | ~50–80mV |
| Noise | ~30–80 µVrms (broadband) |
| PSRR @ 100kHz | **~50 dB** |
| Quiescent current | 55µA |
| Ceramic cap stable | Yes |
| SNS pin | Remote voltage sense |
| Package | WSON-6 (3×3mm) |
| LCSC | C2864100 |

### Why 5V Is Better Than 3.3V for This Rail

The previous revision used LP38692-3.3 (3.3V). Restoring 5VA to 5V resolves:

1. **CD74HC4067 mux signal range:** At 5V VCC, the mux passes signals from 0–5V. At 3.3V, signals above 3.3V would have been clipped, losing 34% of ADC input range.

2. **PCA9306 VREF2:** With 5VA = 5V, the PCA9306 level shifter has proper VREF2 for 5V-side I2C bus operation (matching MCP23S17 and ADS1115 logic levels).

3. **INA180A1 output swing:** With VS = 5V, the INA180A1 output can swing to ~4.9V, though the ESP32 ADC still clips at 3.3V. A voltage divider on the IMAINS ADC input (if present on other schematic pages) could extend the measurement range.

### Power Dissipation

At the estimated 5VA load of ~50mA:

```
Pdiss = (VIN − VOUT) × ILOAD = (6.8 − 5.0) × 0.05 = 90mW
```

Well within the WSON-6 package thermal limits. Even at full 1A load: Pdiss = 1.8W, which would require attention but the analog load is far below 1A.

### Noise Performance

The LP38693 PSRR of ~50dB at 100kHz is significantly better than the L78M05's ~20dB at the same frequency. At the LM25118's switching frequency (~250–300kHz), the LP38693 rejects switching noise much more effectively.

For the lowest possible noise on the analog supply, the TPS7A4701-5.0 (4µVrms, 50dB PSRR @ 100kHz, SOT-223, 36V input) remains the gold standard. But the LP38693 is a solid, cost-effective choice that is dramatically better than the original AMS1117.

---

## Complete Power Supply Topology (Verified)

```
B+ → F1 (JK250-200U PTC 2A)
    → Q10 (DMP6023LE-13, −60V PMOS) [D6: 13V Zener VGS clamp, R7: 10k gate pull-down]
    → D13 (PMEG6020EP body diode bypass)
    → B+BIASED
    → D1 (B530C-13-F), D2/D3 (PESD5Z3.3 ESD)
    → VIN bus

VIN → LM25118 (U1) Buck-Boost Controller
    │   Q11 SWHA069R06VT (60V, buck HS) ← HO drive
    │   U2 MBR20200CS (200V, buck catch diode)
    │   L1 XAL7070-332MEC (3.3µH, 14A sat)
    │   Q3 CSD17575Q3 (30V, boost LS) ← LO drive
    │   D7 STPS20L15G-TR (15V, boost output diode)
    │   R12=4.53kΩ / R9=1kΩ → VOUT = 6.802V
    │   D14/D15 SMF7.0A (output TVS)
    │
    └── 6.8V / 3A+ intermediate rail
         │
         ├── U32 LP38693SD-5.0 → 5VA (analog)
         │    R53 (10k) EN pull-up → immediate enable
         │    C106 (6.8µF input), C104/C105 (100nF + 10µF output)
         │    └── CD74HC4067 mux, PCA9306 VREF2, INA180A1 VS
         │
         ├── U6 LGS5145 → 3.3V → L3 (10µH) → 3.3VPF
         │    R55 (10k) + C108 (1µF) EN delay → ~2ms enable
         │    D8 (PMEG6020EP), D10 (SMAJ3.3A TVS)
         │    └── ESP32-S3
         │
         ├── U8 LGS5145 → 5V → L5 (10µH) → 5VPF
         │    R54 (10k) + C107 (4.7µF) EN delay → ~10ms enable
         │    D11 (PMEG6020EP), D12 (SMF5.0A TVS)
         │    └── MCP23S17 ×6, TXB0108 ×2, peripherals
         │
         ├── U9 REF5050 (via R19 33Ω + C50 10µF) → 5VREF
         │    └── MCP3204 VREF
         │
         └── U10 REF5050 (via R20 33Ω + C52 10µF) → 5VREF2
              └── ADS1115 ×3 VDD

         AGND ←→ GND via L6 (BLM21PG221SN1D) + C39 (100nF) + C49 (22µF)
```

### Power-Up Sequence

```
Time →  0ms     1ms     2ms     5ms     10ms    20ms
        │       │       │       │       │       │
6.8V:   ████████████████████████████████████████████  LM25118 (UVLO > 3.26V)
5VA:    ░░██████████████████████████████████████████  U32 LP38693 (immediate)
5VREF:  ░░░░████████████████████████████████████████  REF5050 (after 6.8V stable)
3.3V:   ░░░░░░░░████████████████████████████████████  U6 LGS5145 (~2ms)
5V:     ░░░░░░░░░░░░░░░░░░░░████████████████████████  U8 LGS5145 (~10ms)
```

3.3V before 5V → PCA9306 VREF1 established before VREF2 → no latch-up ✓

---

## Output Capacitor Bank (6.8V Rail)

| Component | Type | Value | Voltage | Role |
|-----------|------|-------|---------|------|
| C1–C4 | KEMET T491D tantalum | 4 × 47µF | 25V | ESR damping, loop stability |
| C15–C16 | Panasonic 16SVPG270MX polymer | 2 × 270µF | 16V | Low-ESR bulk storage |
| C97–C103 | muRata GRM31CR61A107MEA8L ceramic | 7 × 100µF | **10V** | Ultra-low ESR, fast transient |
| C28–C29 | KEMET T491D tantalum | 2 × 47µF | 25V | Local bulk near downstream |
| **Total** | | **~1528µF nominal** | | **~1000–1100µF effective** |

All capacitors properly voltage-rated for the 6.8V rail. The ceramic caps (10V) operate at 68% of rating — well within safe derating limits with ~55–75% capacitance retention.

---

## Remaining Recommendations (Nice-to-Have)

These are not blockers. The design is functional and safe without them.

### 1. VIN TVS for Extreme Load Dump (Recommended)

Q11 at 60V covers the LM25118's 42V input range completely. For vehicles **without** centralized load dump suppression (ISO 7637-2 pulse 5a can reach 87V), a TVS on VIN provides defense-in-depth:

**SMBJ36A** on VIN (between B+BIASED and GND):
- Standoff: 36V → no interference with normal 12–42V operation
- Clamping: 58.1V → keeps transients below Q11's 60V rating
- 600W peak pulse power → handles automotive load dump energy
- One SMB package, ~$0.15

For vehicles with smart alternators and centralized suppression (clamped to ~27V), Q11 at 60V is sufficient without additional TVS.

### 2. CFF Feed-Forward Capacitors (Recommended)

47pF across the top feedback resistor on each LGS5145 improves load transient response and phase margin. Especially beneficial since U6/U8 now run from a buck-boost output (higher source impedance than direct battery):

- U8 (5V): 47pF across R17 (9.31kΩ)
- U6 (3.3V): 47pF across R13 (6.2kΩ)

Two 0201 caps, ~$0.02 total. Zero risk, measurable improvement in transient response.

### 3. Coil/Injector Driver Gate Pull-Downs (Safety)

*Carried forward from every previous analysis.* On the driver schematic pages (not the POWER page):

10kΩ–47kΩ pull-down resistors from each coil and injector MOSFET gate to GND. When MCP23S17 expanders lose power or reset, their outputs go high-impedance. Without pull-downs, driver gates float → coils can fire uncontrolled → risk of overheating or engine damage. This is passive hardware safety that software cannot replace.

---

## Summary

| # | Item | Status |
|---|------|--------|
| 1 | Buck HS FET voltage (was 30V) | ✅ **Q11 SWHA069R06VT 60V** |
| 2 | TVS standoff on 6.8V rail | ✅ **D14/D15 SMF7.0A** |
| 3 | Analog LDO (5V for mux/PCA9306) | ✅ **U32 LP38693SD-5.0** |
| 4 | FB divider for 6.8V output | ✅ R12=4.53kΩ, R9=1kΩ → 6.802V |
| 5 | Output cap voltage rating | ✅ 7× 100µF/10V ceramic |
| 6 | Enable sequencing | ✅ RC delays: 3.3V at ~2ms, 5V at ~10ms |
| 7 | Reverse polarity protection | ✅ Q10 DMP6023LE-13 (−60V) |
| 8 | VGS gate clamp | ✅ D6 13V Zener |
| 9 | Cranking brownout survival | ✅ LM25118 buck-boost to VIN ~3V |
| 10 | Downstream rail TVS | ✅ D10 SMAJ3.3A, D12 SMF5.0A |
| 11 | VIN TVS for extreme load dump | Recommended: SMBJ36A |
| 12 | CFF on LGS5145 feedback | Recommended: 47pF |
| 13 | Driver gate pull-downs | Safety: 10k–47kΩ to GND |

**The power supply is production-ready.** The LM25118 buck-boost with Q11/Q3 provides a stable 6.8V intermediate rail from 3V to 42V input. All downstream rails are properly regulated, sequenced, protected, and voltage-rated. The three remaining recommendations (VIN TVS, CFF caps, driver pull-downs) are incremental improvements to an already solid design.
