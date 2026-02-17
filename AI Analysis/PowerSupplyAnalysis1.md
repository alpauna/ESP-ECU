# Power Supply Critical Analysis — ESP-ECU (Rev 2)

**Date:** 2026-02-17
**Schematic:** SCH_ECU_1-POWER_2026-02-17.pdf (Page 1 of 5)
**BOM:** BOM_Board1_ECU_2026-02-17.xlsx
**Datasheet reviewed:** LGS5145 Rev.A1V1.0 Dec 2020 (Legend-Si, 24 pages)

---

## Correction from Rev 1

The original analysis (Rev 1) incorrectly identified the LGS5145 as an 18-20V input part based on the catch diode part number. This was wrong on two counts:

1. **PMEG6020EP = 60V/2A Schottky** (PMEG**60**20 = 60V reverse, 2A forward), not 20V. The datasheet explicitly recommends this part (page 12: "PMEG6020ER").
2. **LGS5145 operating range: 4.5V-55V, absolute max: 60V** (page 2, Table 2.1). The datasheet states: "This regulator is ideal for 42V automotive power bus applications" (page 1).

The original critical findings #1 and #2 ("VIN TVS required" and "Verify LGS5145 max VIN") were therefore **invalid**. The LGS5145 comfortably handles automotive load dump transients up to 55V continuous and 60V peak.

---

## LGS5145 Datasheet Key Specs

| Parameter | Value | Notes |
|-----------|-------|-------|
| VIN operating | 5-55V | Recommended range |
| VIN absolute max | 60V | Table 2.1 |
| VFB reference | 0.812V typ (0.792-0.832V) | Page 3, Table 3 |
| Switching frequency | 1.2MHz typ (1.0-1.4MHz) | PWM mode |
| Output current | 1000mA continuous | |
| Switch current limit | 1.2A peak | OCP threshold |
| Rdson (high-side) | 600mΩ typ | At TJ=25°C |
| UVLO | 4.2V rising, 3.5V falling | Auto-recovery |
| Soft-start | 2.4ms | Internal, prevents inrush |
| OTP | 150°C rising, 130°C falling | Auto-recovery |
| Quiescent current | 150µA typ | VIN=12V, not switching |
| Shutdown current | 4µA | EN=0 |
| ESD (HBM) | ±2kV all pins | |
| θJA | 173°C/W | SOT-23-6 |
| Vout formula | Vout = 0.812 × (RF + RG) / RG | Page 6 |

**Built-in protections:** UVLO, OTP, OCP (cycle-by-cycle), frequency foldback (short circuit), soft-start, output short circuit recovery.

---

## Actual Output Voltage Verification

With VFB = 0.812V (typical):

| Rail | RF | RG | Calculated Vout | Label | Error |
|------|----|----|----------------|-------|-------|
| 3.3V (U3) | 6.2kΩ | 2kΩ | 0.812 × 4.1 = **3.33V** | 3.3V | +0.9% |
| 6.8V (U4) | 15kΩ | 2kΩ | 0.812 × 8.5 = **6.90V** | 6.8VA | +1.5% |

**6.8V rail tolerance range** (VFB min/max):
- VFB=0.792V: 0.792 × 8.5 = **6.73V**
- VFB=0.832V: 0.832 × 8.5 = **7.07V**

The net label "6.8VA" is approximate — typical output is 6.9V. This has no functional impact: REF5050 needs VIN ≥ 5.2V (worst-case headroom = 6.73-5.0 = 1.73V), AMS1117 needs 1.3V dropout (6.73-5.0 = 1.73V > 1.3V). Both fine.

---

## Revised Issue Analysis

### Important: LM2576 is now the weakest link

With the LGS5145 rated to 55V operating (60V abs max), the **LM2576SX-5.0** at **40V max operating (45V abs max)** is the component most vulnerable to automotive transients:

- Normal operation (12-14.4V): fine
- Jump start (24V): fine
- ISO 7637-2 pulse 5a without centralized suppression: **+65V for 400ms** — exceeds 45V absolute max
- ISO 7637-2 pulse 5a with centralized suppression (modern alternator): **~27V** — within 40V operating range

**Assessment:** Most modern vehicles with ECU-controlled alternators clamp load dump to ~27V, well within the LM2576's range. A VIN TVS clamp (e.g., **SMBJ40A**: 40V standoff, 64.5V clamping) is still good insurance for worst-case scenarios (battery disconnect during high-current charging, older vehicles without centralized protection), but this is **no longer a survival-critical issue** for most applications.

### Important: Replace AMS1117-5.0 with a low-noise LDO

*(Unchanged from Rev 1 — still the top recommendation for analog quality)*

The AMS1117 is a 1990s design. For an analog 5V rail powering a precision mux and level shifter:

| Parameter | AMS1117 | Modern Alternative |
|-----------|---------|-------------------|
| PSRR @ 1kHz | ~60 dB | 72-82 dB |
| Output noise | ~100 µVrms | 4-7 µVrms |
| Dropout | 1.3V | 0.2-0.3V |
| Ceramic cap stable | No (needs tantalum) | Yes |

The AMS1117's mediocre PSRR means 1.2MHz switching noise from the 6.8V LGS5145 buck bleeds through to 5VA. The REF5050s reject this at their inputs (33Ω RC filter, fc≈480Hz), but the mux and PCA9306 see it directly.

**Alternatives:**
- **TPS7A4701** (72dB PSRR, 4µVrms noise, 36V input, SOT-223) — serious analog LDO
- **LP5907-5.0** (82dB PSRR, 6.5µVrms, 250mA, SOT-23-5) — tiny, excellent if load is <250mA
- **AP2112K-5.0** (ceramic stable, low dropout, lower noise than AMS1117, cheap)

LP5907 is the best pick if the 5VA load is under 250mA (mux + PCA9306 + pull-ups ≈ 30-50mA — easily within budget). It's ceramic-stable, eliminating the tantalum requirement entirely.

### Important: LM2576 — large, inefficient, high EMI

The LM2576 switches at 52kHz — **23x slower** than the LGS5145's 1.2MHz. This requires a massive 100µH inductor and 1320µF of output capacitance. The LGS5145 is the superior converter in every way except raw current capacity:

| Parameter | LM2576 (U1) | LGS5145 (U3/U4) |
|-----------|-------------|------------------|
| Switching freq | 52kHz | 1.2MHz |
| Max VIN | 40V | 55V |
| Abs max VIN | 45V | 60V |
| Output current | 3A | 1A |
| Efficiency @12V→5V | ~80% | ~88% |
| Inductor needed | 100µH | 10-15µH |
| Output caps | 1320µF (6×220µF) | 44µF (2×22µF) |
| Package | D2PAK (large) | SOT-23-6 (tiny) |

**Recommendation:** Replace U1 with a second LGS5145 (or higher-current variant) for the 5V rail. If 1A is sufficient for the 5V digital rail, the LGS5145 is a direct replacement — same footprint as U3/U4, removes the D2PAK, 100µH inductor, and 6×220µF caps. If >1A is needed, consider **TPS54360B** (60V, 3.5A, 500kHz) for the 5V rail.

### Moderate: Power sequencing via EN pins

The datasheet (page 7) provides clear guidance: EN threshold is 1.4V rising / 1.0V falling, with internal 100nA pull-down. For auto-start, connect EN to VIN directly.

For sequencing (recommended):
- 5V comes up first (LM2576, always on)
- 3.3V (U3): EN tied to 5V via resistor divider — starts after 5V stabilizes
- 6.8V (U4): EN tied to 3.3V — analog last

This prevents PCA9306 latch-up (VREF2 > VREF1 with VREF1 = 0V during startup). Zero additional components if using existing resistor dividers on EN.

### Moderate: CFF feed-forward capacitor on 6.8V rail

The LGS5145 datasheet (page 6) recommends an optional feed-forward capacitor (CFF) across RF to improve transient response and loop phase margin. This is not present in the schematic.

For the 6.8V rail (RF=15k, RG=2k):
```
CFF = 1 / (2π × FSW × (RF ∥ RG))
    = 1 / (2π × 1.2MHz × (15k ∥ 2k))
    = 1 / (2π × 1.2e6 × 1765Ω)
    = 75pF
```

The datasheet suggests **47pF as a good starting point**. This adds a high-frequency zero that improves the voltage loop's phase margin, reducing output voltage overshoot during load transients. Particularly beneficial for the analog rail where the AMS1117 and REF5050 loads have minimal transient current variation, but the post-filter (L7/C40) introduces a resonant pole.

### Moderate: No input EMI filter

All three buck converters share VIN with no common-mode filtering. The LM2576's 52kHz switching current pulses propagate back through VIN to the battery harness.

**Fix:** Add a pi-filter on VIN:
```
VIN_raw → [Ferrite bead/inductor] → VIN_filtered → [bulk cap]
```
Or a common-mode choke before the power entry point. This is standard for automotive EMC (CISPR 25 Class 5).

Note: The LGS5145's 1.2MHz is actually easier to filter (smaller components) than the LM2576's 52kHz. Another reason to consider replacing U1 with an LGS5145.

### Moderate: No power-good monitoring

*(Unchanged from Rev 1)*

None of the regulators report their status to the ESP32. The diagnostics mux monitors rails but only during idle-time scans (~320ms cycle). A hardware supervisor reacts in microseconds.

**Fix:** Voltage supervisor ICs on critical rails (TPS3839G33 for 3.3V), or feed PGOOD to diagnostics mux spare CH15.

### Low: Hot-plug input voltage overshoot

The LGS5145 datasheet (page 20) explicitly warns about hot-plug VIN overshoot with ceramic-only input caps. The stray inductance of the input wiring + low-ESR ceramic caps forms an under-damped LC circuit that can overshoot to **2× nominal input voltage**.

The current design has **C5 = 560µF electrolytic** on VIN, which matches the datasheet's recommended solution (Figure 20b: add aluminum electrolytic). The high ESR of the electrolytic damps the LC resonance. **This is already handled correctly.**

### Low: FB+ filter is overbuilt

R3 = 2Ω in a **5W through-hole wirewound** (13mm × 10mm, C5808000) just to filter the battery voltage for monitoring. Steady-state current is <1mA.

**Fix:** Replace with a 10Ω 0805 SMD resistor — saves PCB space, provides better filtering (τ = 10Ω × 280µF = 2.8ms, fc = 57Hz). The 5W rating is ~50x overkill for this application.

### Layout: Kelvin sensing on R4

*(Unchanged from Rev 1)*

Route separate sense traces from the shunt resistor pads directly to U2 IN+/IN-, not sharing copper with the power path. At 100mΩ, even 5mΩ trace resistance is 5% error.

### Layout: Per-buck input caps

The LGS5145 datasheet (page 13-14) emphasizes: "CIN must be at least one ceramic capacitor" placed close to VIN(PIN5) and GND(PIN2). Current design:
- U3 (3.3V): C25=4.7µF + C26=4.7µF + C27=100nF — good
- U4 (6.8V): C35=6.8µF + C43=100nF — good
- Both share the main VIN rail's C5=560µF bulk capacitor — good for hot-plug damping

**Status:** Already addressed in the redesign.

### Layout: High-speed switching loop (from datasheet page 14)

The datasheet (page 14, Figure 14.2) emphasizes keeping the high-speed switching current loop short: SW pin → inductor → Schottky diode → input bypass capacitor → back to GND. This is the only path with nanosecond-scale rise/fall times. Keep traces short, use ground plane under the switching node, and shield FB traces from SW noise.

---

## Revised Priority Ranking

| Priority | Issue | Cost | Impact |
|----------|-------|------|--------|
| **Strong rec** | Low-noise LDO (LP5907/TPS7A47) replacing AMS1117 | $0.30 swap | Cleaner analog 5VA rail, ceramic-stable |
| **Strong rec** | Power sequencing via EN pins | $0 (resistor dividers) | Prevents PCA9306 latch-up at power-on |
| **Recommended** | Replace LM2576 with LGS5145 or modern buck | $1 + layout | 23x faster switching, smaller BOM, higher VIN |
| **Recommended** | CFF 47pF feed-forward cap on 6.8V feedback | $0.01 + 1 pad | Better transient response, per datasheet |
| **Recommended** | VIN EMI pi-filter | $0.50 | EMC compliance |
| **Recommended** | VIN TVS clamp (SMBJ40A) | $0.15 + 1 pad | Insurance against unclamped load dump |
| **Nice-to-have** | Power-good supervisor (TPS3839G33) | $0.30 + 1 GPIO | Hardware rail monitoring |
| **Nice-to-have** | Hold-up capacitance for safe shutdown | $0.20 | SD card corruption prevention |
| **Layout review** | Kelvin sense on R4, switching loop per DS page 14 | $0 | Accuracy, EMI reduction |

---

## Current Design Strengths

The power supply design is fundamentally sound. After the datasheet review:

- **LGS5145 is an excellent choice** — 55V automotive-rated, 1.2MHz, 1A, SKIP mode, full protection suite, SOT-23-6. Superior to LM2576 in every metric except current capacity.
- **PMEG6020EP (60V/2A Schottky)** — correctly rated for VIN up to 55V, exactly as the datasheet recommends.
- **PMOS reverse polarity** (Q1 HSCB2307) — superior to series diode
- **PTC resettable fuse** (F1) — auto-recovers after overcurrent
- **Dual REF5050 precision references** with 33Ω RC input filters — excellent noise rejection
- **KEMET T491D tantalum** output caps on AMS1117 — correct ESR for LDO stability
- **SMBJ7.0A TVS** on 6.8V analog rail — protects downstream precision circuits
- **BLM21PG221SN1D ferrite bead** AGND isolation — resonance-free ground filtering
- **INA180A1** current sense with 100mΩ shunt — proper gain for 0-2A range
- **SMAJ3.3A + PESD5Z3.3** on 3.3V — transient + ESD protection
- **Post-filter stages** (L4→3.3VPF, L7→6.8VPF) — extra switching noise rejection
- **560µF electrolytic on VIN** — provides hot-plug damping per LGS5145 datasheet recommendation
- **Separate analog power domain** — dedicated 6.8V buck → REF5050/AMS1117 → AGND isolation is textbook correct
- **Input caps on both LGS5145s** — C25/C26 for U3, C35 for U4, per datasheet requirements

The design is production-ready as-is. The recommendations above are improvements that would elevate it from good to excellent.
