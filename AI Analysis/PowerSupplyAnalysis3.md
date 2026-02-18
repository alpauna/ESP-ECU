# Power Supply Analysis 3 — Critical Review

**Date:** 2026-02-17
**Schematic:** SCH_ECU_1-POWER_2026-02-17.pdf (latest revision)
**BOM:** BOM_Board1_ECU_2026-02-17.xlsx (latest revision)

---

## Changes Since Analysis 2

| Item | Analysis 2 | This revision |
|------|-----------|---------------|
| Reverse polarity PMOS | Q2 HSCB2307 (DFN-6L, -20V) | **Q10 DMP6023LE-13 (SOT-223, -60V)** |
| Body diode bypass | None explicit | **D13 PMEG6020EP** (Schottky parallel with Q10) |
| VIN bulk caps | C6 = 560µF (1×) | **C6 + C95 = 2× 560µF (1120µF total)** |
| PMEG6020EP count | 3 (D8, D9, D11) | **4 (D8, D9, D11, D13)** |

**Q10 upgrade is good:** DMP6023LE-13 at -60V VDS handles full automotive load dump. Rdson = 28mΩ at VGS=-10V (better than HSCB2307). SOT-223 provides superior thermal path. ID = -7A continuous at 25°C — ample for 2A system draw.

**C95 addition is good:** Doubling VIN bulk capacitance to 1120µF improves hot-plug damping and provides ~10ms additional hold-up time during cranking sag.

---

## CRITICAL: 220µF Ceramic Caps Exceed Voltage Rating

**This is the most serious issue in the design.**

C10, C11, C27, C43 are muRata **GRM31CR60J227ME11L** — 220µF, X5R, **6.3V rated**.

These sit on the **6.4V rail**, operating at **102% of rated voltage**.

| Cap | Location | Operating voltage | Rating | Status |
|-----|----------|------------------|--------|--------|
| C10 | 6.4V buck-boost output | 6.4V | 6.3V | **EXCEEDS RATING** |
| C11 | 6.4V buck-boost output | 6.4V | 6.3V | **EXCEEDS RATING** |
| C27 | 6.4V rail (near 3.3V converter) | 6.4V | 6.3V | **EXCEEDS RATING** |
| C43 | 5V LGS5145 input (from 6.4V) | 6.4V | 6.3V | **EXCEEDS RATING** |

### Why this matters

**1. Voltage overstress:** Operating above the rated voltage accelerates dielectric degradation. Ceramic caps don't fail gracefully — they short-circuit, which on a power rail means a dead short from 6.4V to GND. On an automotive ECU controlling ignition and fuel injection, this is a safety failure mode.

**2. DC bias derating is devastating:** X5R ceramic capacitors lose massive capacitance under DC bias. At 100%+ of rated voltage, typical retention is only **20-40% of nominal**:

| Applied voltage | % of 6.3V rating | Estimated effective capacitance |
|----------------|-------------------|-------------------------------|
| 0V | 0% | 220µF (100%) |
| 3.15V | 50% | ~130µF (60%) |
| 5.0V | 79% | ~80µF (35%) |
| 6.0V | 95% | ~55µF (25%) |
| 6.4V | 102% | **~45µF (20%)** |

Your 4× 220µF = 880µF nominal is really only **~180µF effective** on the 6.4V rail. The LM25118's control loop was likely designed assuming 880µF ceramic — with only 180µF actual, the phase margin is degraded, transient response is worse, and the output voltage ripple is 4-5× higher than calculated.

**3. muRata doesn't make 220µF at 10V+ in 1206:** The GRM31 series maxes out at 6.3V for 220µF. There is no drop-in replacement at a higher voltage in the same footprint.

### Fix options

**Option A (recommended): Use 100µF × 10V ceramics, increase quantity**
- GRM31CR71A107ME15L (muRata, 100µF, X5R, 10V, 1206) — commonly available
- Use 6× instead of 4× to compensate for the lower nominal value
- At 6.4V on a 10V cap: ~60% retention → 6 × 60µF = 360µF effective (2× better than current)

**Option B: Use 47µF × 16V ceramics**
- GRM31CR71C476ME15L (muRata, 47µF, X5R, 16V, 1206)
- At 6.4V on a 16V cap: ~85% retention → 8 × 40µF = 320µF effective
- Better reliability margins

**Option C: Raise the intermediate rail to 6.8V** (see R12 change to 4.53kΩ)
- This makes the 6.3V cap situation worse (108% of rating!) and forces the cap change
- But solves the 5V LGS5145 headroom issue (0.88V vs 0.48V)
- Requires TVS change from SMF6.5A to SMF8.0A

**With any option:** The tantalum caps (C1-C4, C28-C29 = 6× 47µF @ 25V) and polymer caps (C15-C16 = 2× 270µF @ 16V) are correctly rated and provide the bulk energy storage. The ceramic caps supplement with low-ESR fast-transient response — but they must be within their voltage rating to do this job.

---

## CRITICAL: Q1 CSD17575Q3 (30V) Limits VIN Range

*Unchanged from Analysis 2 — still the #2 issue.*

Q1 (buck high-side FET) sees full VIN when off. CSD17575Q3 is rated **30V VDS**. The LM25118 controller handles 3-42V, but Q1 caps the effective input at **<28V** (with derating).

| Scenario | VIN | Q1 margin |
|----------|-----|-----------|
| Normal (12-14.4V) | 14.4V | 15.6V headroom ✓ |
| Jump start | 24V | 6V headroom ✓ |
| Clamped load dump | ~27V | **3V headroom** ⚠️ |
| Unclamped load dump | 40-65V | **DESTRUCTION** ✗ |

Q3 (boost low-side) only sees VOUT ≈ 6.4V — 30V is fine there.

### Fix

Replace **Q1 only** with a 60V+ N-FET. The CSD17575Q3's 2.3mΩ Rdson is exceptional; a 60V FET will have higher Rdson (typically 5-10mΩ) but this is an acceptable trade for surviving automotive transients:

- **CSD19536KTT** (TI, 100V, 4.6mΩ, SON-5×6)
- **BSZ070N08LS5** (Infineon, 80V, 7mΩ, PG-TSDSON-8)

Or add a **SMBJ30A TVS** on VIN (30V standoff, 48.4V clamp) to protect Q1.

---

## IMPORTANT: L78M05CDT-TR Analog LDO Quality

*Unchanged from Analysis 2.*

The L78M05 is a 1970s series-pass regulator. While better than the AMS1117 on noise (40µVrms vs 150µVrms), it's still a poor choice for a precision analog rail feeding the CD74HC4067 mux and PCA9306 level shifter:

| Parameter | L78M05 (current) | TPS7A4701 (recommended) |
|-----------|-------------------|------------------------|
| Noise | 40 µVrms | **4 µVrms** (10× better) |
| PSRR @ 1kHz | ~55 dB | **72 dB** |
| PSRR @ 100kHz | ~20 dB | **50 dB** |
| Dropout @ 50mA | ~0.8V | ~0.3V |
| VIN max | 35V | 36V |
| Ceramic stable | Yes | Yes |
| Package | TO-252 | SOT-223 |

The LM25118 switches at ~290kHz. The L78M05's PSRR at 290kHz is approximately **15-20dB** — meaning only 90-99% of the switching noise is rejected. The remaining 1-10% appears directly on the 5VA rail feeding your analog mux and ADC references.

**TPS7A4701-5.0** (TI, LCSC C181093): 72dB PSRR at 1kHz, 50dB at 100kHz, 4µVrms noise, SOT-223 footprint. Works from 6.4V or 6.8V intermediate rail (36V max input). **Direct drop-in for L78M05** if the footprint is TO-252/SOT-223 compatible.

Note: The LP5907-5.0 (originally proposed) has VIN max = 5.5V and LP5912-5.0 has VIN max = 6.5V — neither works reliably from 6.4V (zero margin) or 6.8V (exceeds abs max). The TPS7A4701 with its 36V input range is the correct choice.

---

## IMPORTANT: SMF6.5A TVS Standoff Margin

D4, D5 = SMF6.5A on the 6.4V output:

- **VR (standoff) = 6.5V** vs **6.42V nominal output** → only **0.08V margin**
- At worst-case VFB (+1.5%): output = 6.51V → **above standoff**
- If the rail is raised to 6.8V: output is **0.3V above standoff** → TVS conducts continuously!

### Fix

| Rail voltage | Recommended TVS | Standoff | Breakdown | Clamp |
|-------------|----------------|----------|-----------|-------|
| 6.4V | **SMF7.0A** | 7.0V | 7.78V min | 11.2V |
| 6.8V | **SMF8.0A** | 8.0V | 8.89V min | 12.9V |

---

## MODERATE: No VIN Overvoltage Protection for Q1

There is no TVS clamp on VIN between Q10 (reverse polarity PMOS) and the LM25118/Q1. During a load dump transient:

1. Battery voltage spikes to 40-65V
2. Q10 (DMP6023LE-13, -60V) passes the transient through — it's a switch, not a clamp
3. LM25118 (42V max) may survive depending on duration
4. **Q1 (CSD17575Q3, 30V max) is destroyed**

Even D6 (UDZVFHTE-1713B Zener) on VIN doesn't help — Zener diodes can't sink the surge current of a load dump event (hundreds of mA to amps for hundreds of ms).

**Fix:** Add **SMBJ30A** (if keeping 30V FETs) or **SMBJ40A** (if upgrading Q1 to 60V) on VIN. Handles 600W peak pulse power for 1ms, clamps voltage below FET ratings.

---

## MODERATE: No Power Sequencing

Both LGS5145 EN pins (U6 and U8) appear tied directly to the 6.4V rail for auto-start. When the 6.4V rail comes up, both 3.3V and 5V start simultaneously.

**Risk:** The PCA9306 I2C level shifter requires VREF1 (3.3V) to be present before or simultaneously with VREF2 (5V). If 5V comes up faster than 3.3V (due to different output capacitor bank sizes and loads), PCA9306 can latch up — pulling the I2C bus low permanently until power cycle.

**Fix:** Sequence via EN pin resistor dividers — 5V EN tied to 6.4V (starts immediately), 3.3V EN connected to 5V via a divider (starts after 5V stabilizes). Zero additional components if using a tap from existing 5V output caps.

---

## MODERATE: No Feed-Forward Capacitors on LGS5145s

The LGS5145 datasheet (page 6) recommends CFF across the top feedback resistor to improve transient response and phase margin. Neither U6 nor U8 has CFF.

For U8 (5V): RF = R17 = 9.31kΩ, RG = R18 = 1.8kΩ
```
CFF = 1 / (2π × 1.2MHz × (RF ∥ RG))
    = 1 / (2π × 1.2e6 × (9.31k ∥ 1.8k))
    = 1 / (2π × 1.2e6 × 1506Ω)
    ≈ 88pF → use 47pF (datasheet starting point)
```

For U6 (3.3V): RF = R13 = 6.2kΩ, RG = R14 = 2kΩ
```
CFF ≈ 75pF → use 47pF
```

One 47pF 0201 cap per LGS5145, across RF. Improves load transient response and adds phase margin, especially important now that these converters run from a 6.4V buck-boost output (not stiff VIN).

---

## MODERATE: No Input EMI Filter

VIN goes directly to the LM25118 with no common-mode filtering. The ~290kHz switching current pulses propagate back through VIN to the battery harness and can cause EMI failures under CISPR 25 testing.

**Fix:** Pi-filter on VIN:
```
VIN_raw → [Ferrite bead or CM choke] → VIN_filtered → [C6+C95 bulk caps]
```

Since there's now only ONE converter on VIN (the LM25118), the filter design is simpler than the previous 3-converter topology. A single ferrite bead (e.g., BLM31PG601SN1L, 600Ω @ 100MHz) between the input connector and the LM25118's VIN provides significant conducted emission reduction.

---

## MODERATE: LM25118 Soft-Start vs Output Cap Bank

C14 = 22nF → soft-start time ≈ 2.7ms. The output capacitor bank is ~1168µF.

Average charging current during soft-start:
```
I_charge = COUT × VOUT / tSS = 1168µF × 6.4V / 2.7ms = 2.77A
```

This is 2.77A just to charge caps, before any load current. Combined with downstream converter inrush, the LM25118 may hit current limit during every startup, causing the output to staircase up over multiple soft-start cycles.

**Fix:** Increase C14 to **100nF** → tSS ≈ 12ms → I_charge ≈ 0.62A. Gentler startup, more margin for load current.

---

## LOW: INA180A1 Current Sense Range

R4 = 100mΩ, gain = 20V/V. ESP32 ADC clips at 3.3V.

Max measurable current: 3.3V / 20 / 0.1Ω = **1.65A**

At 12V input with 6.4V/3A output: I_in ≈ 1.72A → **clips**. During WiFi + full sensor scan, input current can exceed the measurement range.

Not critical (diagnostics only, not control), but consider:
- Reducing R4 to 50mΩ → range doubles to 3.3A, resolution halves
- Or reducing gain by using INA180A2 instead (gain 50 gives better resolution at lower currents but worse range — wrong direction)

---

## LOW: D1 (B530C-13-F) Purpose Unclear

B530C-13-F is a 30V/5A Schottky on the VIN bus. Its specific function isn't obvious from the schematic:
- If it's a reverse blocking diode: redundant with Q10 PMOS
- If it's a catch/clamp diode: 30V is below automotive transient levels
- If it's an OR-ing diode for dual power sources: adds ~0.5V drop

Verify this is intentional and correctly oriented. A stray Schottky on VIN adds forward voltage drop and power loss at every milliamp drawn.

---

## Summary: Priority Action List

| # | Severity | Issue | Fix | Cost |
|---|----------|-------|-----|------|
| 1 | **CRITICAL** | 220µF caps rated 6.3V on 6.4V rail | Replace with 100µF/10V ×6 or 47µF/16V ×8 | ~$0.50 |
| 2 | **CRITICAL** | Q1 CSD17575Q3 30V on VIN bus | Upgrade to 60V+ N-FET (CSD19536KTT) | $0.30 |
| 3 | **IMPORTANT** | L78M05 — 40µVrms noise, 20dB PSRR@290kHz | TPS7A4701-5.0 (4µVrms, 50dB@100kHz) | $0.50 |
| 4 | **IMPORTANT** | SMF6.5A standoff 0.08V above nominal | SMF7.0A (6.4V rail) or SMF8.0A (6.8V rail) | $0 |
| 5 | **MODERATE** | No VIN TVS — Q1 unprotected | Add SMBJ30A or SMBJ40A on VIN | $0.15 |
| 6 | **MODERATE** | No power sequencing (PCA9306 latch-up risk) | EN pin dividers: 5V→immediate, 3.3V→after 5V | $0 |
| 7 | **MODERATE** | No CFF on LGS5145 feedback | 47pF across R17 and R13 | $0.02 |
| 8 | **MODERATE** | No input EMI filter | Ferrite bead on VIN before LM25118 | $0.30 |
| 9 | **MODERATE** | Soft-start too fast for cap bank | C14: 22nF → 100nF | $0 |
| 10 | **SAFETY** | Coil/injector driver pull-downs | 10k-47k to GND on each gate (other schematic pages) | $0.10/gate |

### If raising to 6.8V (R12 = 4.53kΩ):

All items above apply, plus:
- 220µF cap issue becomes even worse (108% of rating) — cap change is mandatory
- D4/D5 must change to SMF8.0A (current SMF6.5A would conduct continuously)
- LP5907 and LP5912 are ruled out — use TPS7A4701 for analog LDO
- 5V LGS5145 headroom improves from 0.48V to 0.88V (significant win)
- L78M05 dropout margin improves from 0.6V to 1.0V

**6.8V is the better choice overall** — it solves the tight 5V headroom issue and forces the cap voltage rating fix that needs to happen regardless.
