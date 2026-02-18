# Feedback Divider Analysis — 6.4V to 6.8V Rail Change

**Date:** 2026-02-17
**Context:** R9/R12 resistor selection for LM25118 buck-boost output at 6.8V
**References:** LM25118 datasheet (TI SNVS655), E96 resistor tables, PowerSupplyAnalysis3.md

---

## Background

The LM25118 buck-boost converter output voltage is set by a resistive divider from VOUT to the FB pin:

```
VOUT ──┬── R12 (top) ──┬── FB (VFB = 1.23V)
       │               │
       │            R9 (bottom)
       │               │
       └───────────────┴── GND
```

The output voltage follows:

```
Vout = VFB × (1 + R12 / R9)

where VFB = 1.23V (±1.5%, range 1.212V to 1.248V)
```

---

## Current Design (6.4V)

| Component | Value | Series |
|-----------|-------|--------|
| R12 (top) | 4.22kΩ | E96 (1%) |
| R9 (bottom) | 1.00kΩ | E96 (1%) |

```
Vout = 1.23 × (1 + 4.22 / 1.00) = 1.23 × 5.22 = 6.421V
```

**Worst-case range (VFB ±1.5%, resistors ±1%):**
- Minimum: 1.212 × (1 + 4.178/1.010) = 1.212 × 5.136 = 6.225V
- Maximum: 1.248 × (1 + 4.262/0.990) = 1.248 × 5.305 = 6.621V

---

## Target: 6.8V Output

Solving for R12 with R9 = 1.00kΩ:

```
R12 = R9 × (Vout/VFB - 1) = 1.00k × (6.8/1.23 - 1) = 1.00k × 4.528 = 4.528kΩ
```

---

## Standard E96 (1%) Resistor Combinations for 6.8V

All values below are standard E96 series resistors, available in 1% tolerance from every major distributor (Yageo, UNI-ROYAL, Vishay, etc.) in all common packages including 0201.

### Recommended: Single Resistor Change (R12 Only)

Keeping R9 = 1.00kΩ minimizes impact on the LM25118 compensation network (which is designed around the FB divider impedance).

| R9 | R12 | Calculated Vout | Error from 6.800V | Notes |
|----|-----|-----------------|--------------------|----|
| **1.00kΩ** | **4.53kΩ** | **6.802V** | **+0.03%** | **Best accuracy — recommended** |
| 1.00kΩ | 4.52kΩ | 6.790V | -0.15% | Slightly low, acceptable |
| 1.00kΩ | 4.55kΩ | 6.827V | +0.39% | Slightly high, acceptable |

**4.53kΩ is a standard E96 value.** E96 (1%) values in the 4.5–4.6kΩ range: 4.42, 4.48, 4.53, 4.59, 4.64kΩ.

### Alternative: Both Resistors Changed (Common E24 Values)

If 4.53kΩ is not in stock or a more common value is preferred, these E24 combinations also work:

| R9 | R12 | Calculated Vout | Error | Notes |
|----|-----|-----------------|-------|-------|
| 1.50kΩ | 6.81kΩ | 6.814V | +0.21% | E96 pair |
| 1.50kΩ | 6.80kΩ | 6.806V | +0.09% | E24/E96 pair, very common values |
| 2.20kΩ | 10.0kΩ | 6.818V | +0.26% | Both E24, very common |
| 2.21kΩ | 10.0kΩ | 6.795V | -0.07% | E96/E24 pair |
| 3.30kΩ | 15.0kΩ | 6.821V | +0.31% | Both E24, most common |
| 3.32kΩ | 15.0kΩ | 6.808V | +0.12% | E96/E24 pair |

---

## Worst-Case Analysis for Recommended Values (R9=1.00kΩ, R12=4.53kΩ)

Accounting for VFB tolerance (±1.5%) and resistor tolerance (±1%):

| Condition | VFB | R12 | R9 | Vout |
|-----------|-----|-----|-----|------|
| Nominal | 1.230V | 4.53kΩ | 1.00kΩ | **6.802V** |
| All max | 1.248V | 4.575kΩ | 0.990kΩ | **7.010V** |
| All min | 1.212V | 4.485kΩ | 1.010kΩ | **6.600V** |
| VFB high, R ratio nominal | 1.248V | 4.53kΩ | 1.00kΩ | 6.901V |
| VFB low, R ratio nominal | 1.212V | 4.53kΩ | 1.00kΩ | 6.703V |
| VFB nominal, R ratio high | 1.230V | 4.575kΩ | 0.990kΩ | 6.912V |
| VFB nominal, R ratio low | 1.230V | 4.485kΩ | 1.010kΩ | 6.694V |

**Worst-case output range: 6.600V to 7.010V** (±3% from nominal)

This range is within the operating window of all downstream regulators:
- LGS5145 5V: needs ≥ 5.92V input at 1A → 6.600V provides 0.68V margin ✓
- LGS5145 3.3V: needs ≥ 3.70V input → 6.600V provides 2.90V margin ✓
- L78M05/TPS7A4701 5VA: needs ≥ 5.8V/5.3V → 6.600V provides 0.8V/1.3V margin ✓
- REF5050: needs ≥ 5.27V (incl. 33Ω filter drop) → 6.600V provides 1.33V margin ✓

---

## Impact on Other Components When Raising to 6.8V

Raising from 6.4V to 6.8V affects several components on the intermediate rail. These changes are mandatory if adopting 6.8V:

### 1. Output Capacitors — MUST CHANGE (cap issue worsens)

Current GRM31CR60J227ME11L (220µF, 6.3V rated) at 6.8V = **108% of rating**:
- Voltage overstress accelerated (was 102% at 6.4V, now 108%)
- Effective capacitance drops to ~15% of nominal (was ~20% at 6.4V)
- Cap replacement from PowerSupplyAnalysis3.md is now mandatory, not just recommended

| Option | Part | Qty | Effective C at 6.8V | Rating margin |
|--------|------|-----|---------------------|---------------|
| **A (rec)** | 100µF/10V (GRM31CR71A107ME15L) | 6× | ~330µF | 32% margin |
| B | 47µF/16V (GRM31CR71C476ME15L) | 8× | ~320µF | 58% margin |

### 2. TVS Diodes — MUST CHANGE

| Current | Replacement | Why |
|---------|-------------|-----|
| SMF6.5A (6.5V standoff) | **SMF8.0A (8.0V standoff)** | 6.8V nominal exceeds 6.5V standoff by 0.3V → TVS would conduct continuously |

SMF8.0A specs: 8.0V standoff, 8.89V min breakdown, 12.9V clamp @ 1A

### 3. Downstream Headroom — IMPROVES

| Device | Min input needed | Headroom at 6.4V | Headroom at 6.8V | Change |
|--------|-----------------|-------------------|-------------------|--------|
| LGS5145 → 5V (1A) | 5.92V | 0.48V (tight) | **0.88V** | +83% better |
| LGS5145 → 3.3V | 3.70V | 2.70V | **3.10V** | +15% better |
| L78M05 → 5VA | ~5.8V | 0.6V | **1.0V** | +67% better |
| REF5050 | 5.27V | 1.13V | **1.53V** | +35% better |

The 5V LGS5145 headroom improvement from 0.48V to 0.88V is the primary reason to prefer 6.8V.

### 4. Analog LDO — Depends on Choice

| LDO | VIN max | At 6.8V |
|-----|---------|---------|
| L78M05 (current) | 35V | ✓ Works, 1.0V headroom |
| LP5912-5.0 | 6.5V | **Exceeds VIN max — cannot use** |
| **TPS7A4701-5.0** | **36V** | **✓ Works, 1.8V headroom** |

LP5912 is eliminated at 6.8V. TPS7A4701 remains the recommended analog LDO.

### 5. LDO Power Dissipation — Slight Increase

| LDO | Load | Pdiss at 6.4V | Pdiss at 6.8V | Change |
|-----|------|---------------|---------------|--------|
| L78M05/TPS7A4701 (5VA) | ~50mA | 70mW | 90mW | +20mW |
| LGS5145 (5V, switching) | ~1A | ~60mW (η≈98%) | ~55mW (η≈98%) | Negligible |
| LGS5145 (3.3V, switching) | ~0.5A | ~35mW (η≈97%) | ~35mW (η≈97%) | Negligible |

The 20mW increase on the analog LDO is negligible in SOT-223 or TO-252 packages.

### 6. LM25118 Compensation Network — May Need Adjustment

Changing VOUT from 6.4V to 6.8V shifts the buck-boost crossover point and affects the power stage transfer function. The existing compensation (R7, C13, C7 on the COMP pin) may need recalculation per TI's LM25118 design procedure. In practice, a 6% VOUT increase is unlikely to cause instability, but verify with a bench load transient test after building.

---

## Recommendation

**Use R9 = 1.00kΩ and R12 = 4.53kΩ** for a 6.802V output:

1. **Best accuracy:** +0.03% error from target
2. **Single change:** Only R12 changes (was 4.22kΩ), minimizing risk
3. **Standard value:** 4.53kΩ is a standard E96 (1%) resistor
4. **Compensation preserved:** Same R9 means same divider impedance seen by the FB pin
5. **All downstream devices have adequate headroom** even at worst-case low (6.600V)

**Mandatory companion changes when adopting 6.8V:**
- Replace 220µF/6.3V ceramics with 100µF/10V ×6 (or 47µF/16V ×8)
- Replace D4/D5 SMF6.5A with SMF8.0A
- Use TPS7A4701-5.0 instead of L78M05 or LP5912 for analog LDO (if not already planned)

**6.8V is the better intermediate rail voltage** — it solves the tight 5V headroom at 6.4V and forces the capacitor voltage rating fix that is needed regardless.
