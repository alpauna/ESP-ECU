# Power Supply Analysis 2 — ESP-ECU (Redesigned Topology)

**Date:** 2026-02-17
**Schematic:** SCH_ECU_1-POWER_2026-02-17.pdf (revised)
**BOM:** BOM_Board1_ECU_2026-02-17.xlsx (revised)
**References:** LM25118 datasheet (TI SNVS655), CSD17575Q3 datasheet, LGS5145 datasheet, L78M05 datasheet

---

## Architecture Change Summary

The power supply has been fundamentally redesigned from three independent converters off VIN to a single intermediate rail architecture:

### Previous topology (3 converters from VIN)

```
B+ → VIN (4-16V)
    ├── LM2576SX-5.0  → 5V/3A     (digital: expanders, peripherals)
    ├── LGS5145 U3    → 3.3V/1A   (ESP32)
    └── LGS5145 U4    → 6.8V/1A   (analog: REF5050, AMS1117 → 5VA)
```

### New topology (single intermediate rail)

```
B+ → F1 (PTC 2A) → Q2 (PMOS reverse polarity) → VIN
    │
    └── LM25118 Buck-Boost + Q1/Q3 (CSD17575Q3) → 6.4V / 3A+
         │
         ├── LGS5145 U6 → 3.3V → L3 post-filter → 3.3VPF
         │    └── ESP32-S3
         │
         ├── LGS5145 U8 → 5V → L5 post-filter → 5VPF
         │    └── MCP23S17 ×6, TXB0108 ×2, peripherals
         │
         ├── L78M05CDT-TR U7 → 5VA (analog)
         │    └── CD74HC4067 mux, PCA9306
         │
         ├── REF5050 U9 (via 33Ω RC filter) → 5VREF
         │    └── MCP3204 VREF
         │
         └── REF5050 U10 (via 33Ω RC filter) → 5VREF2
              └── ADS1115 ×3 VDD

         AGND ←→ GND via BLM21PG221SN1D ferrite bead + 100nF/22µF
```

### What changed

| Item | Previous | Redesigned |
|------|----------|-----------|
| Primary converter | LM2576 (5V buck, 52kHz, 40V max) | **LM25118 buck-boost (6.4V, ~290kHz, 42V)** |
| 6.8V analog buck | LGS5145 U4 from VIN | **Eliminated** — 6.4V feeds analog directly |
| 5V digital | LM2576 direct from VIN | LGS5145 U8 from 6.4V |
| 3.3V | LGS5145 from VIN | LGS5145 U6 from 6.4V |
| Analog 5V LDO | AMS1117-5.0 from 6.8V | **L78M05CDT-TR from 6.4V** |
| Power FETs | None (integrated in LM2576) | **2× CSD17575Q3 (30V N-FET)** |
| Catch diodes | B540C (40V, LM2576) + PMEG6020EP ×4 | **MBR20200CS (200V) + STPS20L15G (15V) + PMEG6020EP ×3** |
| Buck-boost inductor | N/A | **XAL7070-332MEC 3.3µH (Coilcraft)** |
| 6.4V TVS protection | N/A | **2× SMF6.5A** |
| 5V TVS protection | N/A | **SMF5.0A (D12)** |
| Cranking survival | Dies at VIN < 7.5V (cascading) | **Survives to VIN ≈ 3V** |
| Failure mode | Cascading (3 thresholds) | **Binary (works or clean reset)** |

---

## LM25118 Buck-Boost: The Big Win

The LM25118 is a **4-switch non-inverting buck-boost controller** (confirmed by TI AN-2178, SNVA534). It uses:

- **Q1** (CSD17575Q3): High-side buck FET — driven by HO/HB/HS
- **U2** (MBR20200CS): Buck catch diode — 200V, handles VIN reverse
- **L1** (XAL7070 3.3µH): Single power inductor
- **Q3** (CSD17575Q3): Low-side boost FET — driven by LO
- **D7** (STPS20L15G-TR): Boost output diode — 15V, handles VOUT reverse
- **U3/U4** (470nF): Bootstrap/bypass capacitors

This is a true buck-boost that seamlessly transitions between buck mode (VIN > VOUT) and boost mode (VIN < VOUT):

| VIN (battery) | Mode | 6.4V output | All rails |
|---------------|------|-------------|-----------|
| 14.4V (charging) | Buck | **6.4V** ✓ | All regulating |
| 12.0V (normal) | Buck | **6.4V** ✓ | All regulating |
| 8.0V (light crank) | Buck | **6.4V** ✓ | All regulating |
| 6.4V (crossover) | Pass-through | **6.4V** ✓ | All regulating |
| 5.0V (heavy crank) | **Boost** | **6.4V** ✓ | All regulating |
| 4.0V (extreme sag) | **Boost** | **6.4V** ✓ | All regulating |
| 3.0V (dead battery) | Below min VIN | Drops out | Clean shutdown |

**The cranking brownout problem is solved.** No cascading failures, no zombie state. The ECU either works fully or resets cleanly.

### LM25118 Key Specs

| Parameter | Value |
|-----------|-------|
| Input voltage | 3V – 42V |
| VFB reference | 1.23V typ (±1.5%) |
| Switching frequency | ~290kHz (set by R2 = 18.7kΩ) |
| Topology | 4-switch non-inverting buck-boost |
| Current sense threshold | 1.25V (buck), 2.5V (buck-boost) |
| UVLO | Programmable via UVLO pin (1.23V rising, 1.13V falling) |
| Soft-start | C14 = 22nF → tSS = 22nF × 1.23V / 10µA ≈ 2.7ms |
| Package | HTSSOP-20 with exposed pad |

---

## Output Voltage Verification

### 6.4V rail (LM25118)

FB divider: R12 = 4.22kΩ (top, VOUT to FB) and R9 = 1kΩ (bottom, FB to AGND)

```
Vout = VFB × (1 + R12/R9) = 1.23V × (1 + 4.22/1.0) = 1.23 × 5.22 = 6.42V
```

Tolerance band (VFB ±1.5%):
- VFB = 1.212V: **6.33V**
- VFB = 1.248V: **6.51V**

Output range: **6.33V – 6.51V**. All downstream regulators have adequate headroom across this range.

### 3.3V rail (LGS5145 U6)

FB divider: R13 = 6.2kΩ (RF), R14 = 2kΩ (RG)

```
Vout = 0.812V × (6.2 + 2) / 2 = 0.812 × 4.1 = 3.33V
```

VFB tolerance range: **3.25V – 3.41V** ✓

### 5V rail (LGS5145 U8)

FB divider: R17 = 9.31kΩ (RF), R18 = 1.8kΩ (RG)

```
Vout = 0.812V × (9.31 + 1.8) / 1.8 = 0.812 × 6.172 = 5.01V
```

VFB tolerance range: **4.89V – 5.14V** ✓

### 5VA rail (L78M05 U7)

Fixed 5.0V output (internal reference). Input = 6.4V.

### REF5050 outputs

U9 → 5VREF, U10 → 5VREF2. Both powered from 6.4V through 33Ω input filters.

Effective input: 6.4V – 33Ω × 2mA = 6.33V. REF5050 minimum VIN = 5.2V → headroom = 1.13V ✓

---

## Critical Finding: Q1 FET Voltage Rating (CSD17575Q3 = 30V)

The CSD17575Q3 is a 30V N-channel NexFET. In the 4-switch buck-boost topology:

| Component | Sees voltage | Rating | Margin |
|-----------|-------------|--------|--------|
| **Q1** (buck high-side) | **VIN** when off | **30V** | ⚠️ **Limited to VIN < 30V** |
| Q3 (boost low-side) | VOUT ≈ 6.4V when off | 30V | 23.6V margin ✓ |
| U2 MBR20200CS (buck catch) | VIN when Q1 on | 200V | 158V margin ✓ |
| D7 STPS20L15G (boost output) | VOUT ≈ 6.4V when Q3 on | 15V | 8.6V margin ✓ |

**Q1 is the weak link.** The LM25118 controller handles 3–42V, but the 30V high-side FET limits the effective VIN range to **< 28V** (with derating):

| Scenario | VIN | Q1 (30V) status |
|----------|-----|-----------------|
| Normal operation | 12–14.4V | ✓ Safe (50% of rating) |
| Jump start | 24V | ✓ Safe (80% of rating) |
| Clamped load dump (modern vehicle) | ~27V | ⚠️ Marginal (90% of rating) |
| Unclamped load dump | 40–65V | ✗ **Exceeds absolute max** |

The CSD17575Q3 IS avalanche-rated, which provides some transient protection. But sustained overvoltage above 30V will damage it.

### Recommendation

**Option A (preferred):** Replace Q1 with a 60V or 80V N-FET. Examples:
- **CSD19536KTT** (TI, 100V, 4.6mΩ, SON-5×6) — matches LM25118's 42V input fully
- **BSC070N10NS** (Infineon, 100V, 7mΩ, PG-TDSON-8)
- Keep Q3 as CSD17575Q3 (30V is perfect for the VOUT-side boost FET)

**Option B:** Add a VIN TVS clamp (SMBJ30A: 30V standoff, 48.4V clamping) to prevent VIN from exceeding 30V at Q1. This limits the LM25118's useful input range but protects Q1.

**Option C:** Accept the 30V limit and rely on the vehicle's centralized load dump protection (modern ECU-controlled alternators clamp to ~27V). Add a note that the design is not suitable for older vehicles without centralized suppression.

---

## Moderate Finding: L78M05 for Analog 5VA Rail

The L78M05CDT-TR replaces the AMS1117-5.0. While this eliminates the AMS1117's ceramic cap instability and tantalum cap requirement, the L78M05 is a 1970s-era series-pass regulator — not an LDO and not low-noise:

| Parameter | AMS1117-5.0 (old) | L78M05CDT-TR (new) | LP5912-5.0 (recommended) |
|-----------|--------------------|--------------------|-----------------------|
| Dropout @ 50mA | ~0.8V | ~0.8–1.0V | **0.1V** |
| Min VIN for 5V | ~5.8V | ~5.8–6.0V | **5.1V** |
| Max VIN | 18V | **35V** | 6.5V |
| Noise (10Hz–100kHz) | ~150 µVrms | **40 µVrms** | **12 µVrms** |
| PSRR @ 1kHz | ~60 dB | ~55 dB | **75 dB** |
| PSRR @ 100kHz | ~30 dB | ~20 dB | **40 dB** |
| Ceramic cap stable | No (needs tantalum) | **Yes** | **Yes** |
| Max current | 1A | 500mA | 500mA |
| Quiescent current | 5mA | 6mA | **30µA** |
| Package | SOT-223 | TO-252 (DPAK) | WSON 2×2mm |

**The L78M05 is 3.75× better than the AMS1117 on noise** (40 vs 150 µVrms) — this is a meaningful improvement. And it's ceramic cap stable, eliminating the tantalum requirement. So the swap is a step forward.

However, it's still 3.3× worse than the LP5912 on noise (40 vs 12 µVrms), and its high-frequency PSRR is poor (~20dB at 100kHz means LM25118's ~290kHz switching noise bleeds through to the analog rail).

### L78M05 dropout at 6.4V input

The L78M05 datasheet specifies 7V minimum input for guaranteed regulation at full load (500mA). But the analog load is only ~50mA (mux + PCA9306 + pull-ups). At 50mA:

- **Typical dropout: ~0.8V** → headroom = 6.4V – 5.0V – 0.8V = **0.6V** ✓
- **Worst case at 125°C: ~1.2V** → headroom = 6.4V – 5.0V – 1.2V = **0.2V** ⚠️

This is tight at temperature extremes. Not a hard failure, but marginal.

### Recommendation

**Replace L78M05 with LP5912-5.0** (TI, LCSC available):
- 12 µVrms noise — 3.3× better than L78M05, 12.5× better than AMS1117
- 75dB PSRR @ 1kHz — effectively suppresses LM25118 switching noise
- 95mV dropout at 500mA — rock-solid regulation from 6.4V
- VIN max = 6.5V — the 6.4V intermediate rail is within spec (0.08V margin to abs max)
- Ceramic cap stable, tiny 2×2mm WSON, 30µA quiescent

Note: LP5907-5.0 (originally proposed) has VIN max of only 5.5V — it **cannot** be used from 6.4V directly. The LP5912 is the correct choice for this voltage range.

If LP5912 VIN max (6.5V) margin is too tight, alternatives with wider input range:
- **TPS7A4701** (TI, 36V max, 4µVrms, SOT-223) — wide margin, excellent analog LDO
- **TPS7A4901** (TI, 36V max, 7µVrms, SOT-223) — similar, slightly higher noise

---

## Moderate Finding: SMF6.5A TVS Standoff Margin on 6.4V Rail

D4, D5 = SMF6.5A protect the 6.4V output rail:

| Parameter | SMF6.5A spec | 6.4V rail |
|-----------|-------------|-----------|
| VR (standoff) | 6.5V | 6.42V nominal |
| VBR (breakdown) | 7.22V min | — |
| VC (clamp @ IPP) | 10.5V | — |

The TVS won't conduct until 7.22V minimum, so normal operation is fine. But the margin between the rated standoff (6.5V) and the nominal output (6.42V) is only **0.08V**. At worst-case VFB (+1.5%), the output could reach 6.51V — slightly above the rated working voltage.

At 6.51V, TVS leakage is typically a few µA (not harmful but not ideal for long-term reliability).

### Recommendation

Consider **SMF7.0A** (7.0V standoff, 7.78V min breakdown, 11.2V clamp) for more comfortable margin. The 7.78V breakdown still protects downstream regulators (LGS5145 abs max = 60V, L78M05 max = 35V, REF5050 max = 18V).

---

## Minor Finding: 5V LGS5145 Headroom from 6.4V

U8 (LGS5145, 5V from 6.4V) headroom analysis:

```
Min VIN for 5V = VOUT / Dmax + Rdson × ILOAD
At Dmax = 94% (typical): 5.0/0.94 + 0.6Ω × I
    At 1A: 5.32 + 0.60 = 5.92V → headroom = 0.48V (tight)
    At 500mA: 5.32 + 0.30 = 5.62V → headroom = 0.78V (OK)
At Dmax = 90% (worst case): 5.0/0.90 + 0.6Ω × I
    At 1A: 5.56 + 0.60 = 6.16V → headroom = 0.24V (marginal!)
    At 500mA: 5.56 + 0.30 = 5.86V → headroom = 0.54V (OK)
```

At worst-case Dmax (90%) with 1A load, the headroom is only 0.24V. If the 6.4V rail has ripple (typical 50–100mV for a buck-boost), instantaneous headroom could approach zero.

**In practice:** The 5V digital load is unlikely to sustain 1A continuously. Typical load (6× MCP23S17 + 2× TXB0108 + LEDs + pull-ups) is probably 300–500mA. At 500mA with worst-case Dmax, headroom is 0.54V — adequate.

**If 5V load approaches 1A:** Consider raising the intermediate rail to 6.8V, or accept the tight margin at full load. The 6.4V choice optimizes for LDO dissipation; 6.8V would give better buck margin at the cost of slightly more LDO heat.

---

## LM25118 Inductor Ripple Assessment

With L1 = 3.3µH and FSW ≈ 290kHz, buck mode at VIN = 12V:

```
ΔI = (VIN - VOUT) × VOUT / (FSW × L × VIN)
   = (12 - 6.4) × 6.4 / (290k × 3.3µ × 12)
   = 35.84 / 11.484 = 3.12A peak-to-peak
```

At 3A DC load, this is **~100% ripple** — higher than the typical 30–40% target. Peak inductor current reaches ~4.56A.

**However:** The XAL7070-332MEC from Coilcraft has:
- Saturation current: ~14A → peak current is well within limits
- DCR: 7mΩ → I²R loss = 3.13² × 0.007 = 69mW (negligible)
- Shielded construction → good EMI containment

And the massive output capacitance (4×47µF tantalum + 2×220µF ceramic + 2×270µF Panasonic SVPG ≈ **1,168µF**) yields very low output voltage ripple:

```
ΔV ≈ ΔI / (8 × FSW × COUT) = 3.12 / (8 × 290k × 1168µ) ≈ 1.1mV
```

**Verdict:** The inductor ripple is high but the output voltage ripple is negligible. The main trade-off is slightly increased core losses in L1 and higher RMS current in the output caps. The Panasonic SVPG capacitors (16SVPG270MX) are low-ESR polymer types that handle ripple current well. This is a valid design choice — optimizing for small inductor size at the cost of higher ripple, compensated by large output capacitance.

---

## Output Capacitance Review

### 6.4V rail (LM25118 output)

| Component | Type | Value | ESR class |
|-----------|------|-------|-----------|
| C1-C4 | KEMET T491D tantalum | 4 × 47µF | Medium ESR (~0.7Ω) |
| C28, C29 | KEMET T491D tantalum | 2 × 47µF | Medium ESR |
| C10, C11 | muRata ceramic (C1206) | 2 × 220µF | Very low ESR |
| C15, C16 | Panasonic SVPG polymer | 2 × 270µF | Low ESR (~20mΩ) |
| C27 | muRata ceramic (C1206) | 1 × 220µF | Very low ESR |
| **Total** | | **~1,168µF** | Mixed ESR profile |

The mix of tantalum (damping), ceramic (low ESR, fast transient response), and polymer (bulk capacitance, low ESR) is a textbook-quality output filter. The tantalum caps provide the ESR needed for control loop stability, while ceramics handle high-frequency transients and polymer provides bulk energy storage.

### 3.3V rail output

C22=22µF + C23=22µF + C24=4.7µF + C25=4.7µF + C26=22µF = **75.4µF** ceramic. Good for LGS5145.

### 5V rail output

C37=22µF + C38=22µF + C40=4.7µF + C41=4.7µF + C42=22µF = **75.4µF** ceramic. Matches 3.3V rail. Good.

### 5VA rail (L78M05 output)

C35 = 47µF tantalum. The L78M05 is stable with any capacitor type, and the tantalum provides adequate output filtering.

---

## What's Done Right

This redesign addresses nearly every recommendation from the previous analyses. Specific wins:

1. **Single intermediate rail architecture** — Only one converter touches VIN. All downstream rails share a single failure threshold. No more cascading rail failures during cranking.

2. **Buck-boost topology** — The LM25118 in 4-switch buck-boost mode maintains 6.4V from VIN as low as ~3V. This solves the cranking brownout problem completely.

3. **CSD17575Q3 NexFETs** — Excellent 2.3mΩ Rdson at VGS=10V. Very low conduction losses. (Q3 is perfect for the boost position; Q1 voltage rating is the one concern.)

4. **XAL7070 Coilcraft inductor** — High-quality shielded power inductor with 14A saturation. Properly rated for the ripple current.

5. **MBR20200CS (200V) as buck catch diode** — Massive voltage margin for automotive transients. Correct placement in the buck half-bridge.

6. **STPS20L15G (15V/20A) as boost output diode** — Properly rated for VOUT-side. Low forward voltage reduces losses.

7. **Panasonic SVPG polymer output caps** — Excellent low-ESR bulk capacitance. Superior to the old 6×220µF ceramic approach for energy storage.

8. **AMS1117 eliminated** — No more tantalum cap dependency for LDO stability. L78M05 is ceramic-stable.

9. **Output TVS protection** — SMF6.5A on 6.4V, SMF5.0A on 5V. The old design only had SMBJ7.0A on the 6.8V analog rail.

10. **D2/D3 PESD5Z3.3** and **D10 SMAJ3.3A** on 3.3V — ESD and transient protection retained.

11. **Both LGS5145s from 6.4V** — Consistent input voltage, both start/stop together, proper behavior during cranking.

12. **REF5050 input filters** — R19/R20 = 33Ω with C50/C52 = 10µF. RC filter fc ≈ 480Hz. Same proven design, now powered from cleaner 6.4V buck-boost output.

13. **INA180A1 current monitoring** — R4 = 100mΩ shunt with gain-20 amplifier retained for battery current diagnostics.

14. **Ferrite bead AGND isolation** — BLM21PG221SN1D with C39=100nF + C49=22µF retained. Resonance-free analog ground filtering.

15. **Post-filter stages** — L3 (10µH) → 3.3VPF and L5 (10µH) → 5VPF provide additional switching noise rejection for sensitive circuits.

---

## Remaining Recommendations

### From previous analyses (still applicable)

| Priority | Action | Status |
|----------|--------|--------|
| **SAFETY** | Pull-down resistors on coil/injector driver gates | **Still needed** — prevents uncontrolled fire if 5V rail dies |
| **Strong rec** | Power sequencing via LGS5145 EN pins (5V before 3.3V) | Simpler now — both from 6.4V, use RC delays on EN |
| **Recommended** | CFF 47pF feed-forward on LGS5145 feedback dividers | Still applies to U6 (3.3V) and U8 (5V) |
| **Recommended** | Input EMI filter on VIN before buck-boost | Single converter to filter now (simpler) |
| **Layout** | Kelvin sense on R4 (INA180A1 shunt) | Unchanged |
| **Layout** | Keep LM25118 high-current switching loops tight | Per LM25118 datasheet layout guidelines |

### New recommendations from this revision

| Priority | Action | Why |
|----------|--------|-----|
| **Important** | Replace Q1 with ≥60V FET (e.g., CSD19536KTT) | 30V rating limits VIN to <28V; automotive transients can exceed this |
| **Important** | Replace L78M05 with LP5912-5.0 (or TPS7A4701) | 3.3× lower noise, 20dB better high-freq PSRR for analog rail |
| **Recommended** | Replace D4/D5 SMF6.5A with SMF7.0A | 0.08V standoff margin is too tight; SMF7.0A gives 0.58V |
| **Nice-to-have** | Verify 5V LGS5145 load is under 700mA | Ensures adequate headroom at worst-case Dmax from 6.4V |

---

## Priority Summary

| # | Priority | Item | Cost |
|---|----------|------|------|
| 1 | **SAFETY** | Coil/injector driver pull-down resistors | ~$0.10 per gate |
| 2 | **Important** | Q1: upgrade to 60V+ FET | $0.30 swap |
| 3 | **Important** | U7: LP5912-5.0 or TPS7A4701 for analog 5VA | $0.50 swap |
| 4 | **Recommended** | D4/D5: SMF7.0A for 6.4V TVS | $0 swap (same package/price) |
| 5 | **Recommended** | CFF 47pF on U6/U8 feedback dividers | $0.02 |
| 6 | **Recommended** | Power sequencing via EN delays | $0 (resistor dividers) |
| 7 | **Layout** | LM25118 switching loop, R4 Kelvin sense | $0 |

---

## Summary

This is an **excellent redesign** that implements the buck-boost intermediate rail architecture proposed in the BuckBoostTopologyProposal.md. The LM25118 in 4-switch non-inverting buck-boost mode with CSD17575Q3 FETs is a well-engineered solution that:

1. **Solves the cranking brownout completely** — maintains 6.4V down to ~3V battery
2. **Eliminates cascading rail failures** — all downstream rails share one intermediate rail
3. **Eliminates the zombie state** — ESP32 can't outlive its I/O anymore
4. **Removes the LM2576** — no more 52kHz switching, 100µH inductor, or 6×220µF caps
5. **Removes the 6.8V analog buck** — simplified topology with one fewer conversion stage
6. **Replaces AMS1117 with ceramic-stable LDO** — no more tantalum cap dependency

The three items that need attention are:
- **Q1 voltage rating** (30V FET in a 42V-capable controller — upgrade to 60V+)
- **Analog LDO noise** (L78M05 is better than AMS1117 but LP5912 would be 3.3× better)
- **6.4V TVS margin** (SMF6.5A standoff is only 0.08V above nominal — use SMF7.0A)

With those three component swaps, this power supply would be production-grade for automotive engine control.
