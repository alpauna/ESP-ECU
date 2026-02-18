# VR Sensor Conditioner Module Analysis

**Date:** 2026-02-18
**Context:** Variable reluctance (VR) sensor signal conditioning for ESP-ECU crank/cam/VSS inputs
**References:** MAX9926 (Analog Devices), LM1815 (TI), LM393 (TI), BSS138 (ON Semi), IRLZ44N (Infineon)

---

## 1. Purpose

A small, universal VR sensor conditioner module that:
- Accepts raw differential AC from any variable reluctance inductive pickup
- Conditions it to a clean 5V square wave with precise zero-crossing edges
- Outputs a digital signal compatible with the ESP-ECU 5V I/O standard
- Mounts near the sensor to keep noisy analog wiring short, clean digital wiring long
- Works for **any** VR sensor application: crank trigger, cam phase, VSS, driveshaft speed, wheel speed

**Design philosophy:** Start simple, test, make better. The module is a swappable black box — the ESP-ECU sees a clean digital pulse train regardless of which generation of conditioner is behind it. Iteration happens at the module level without changing the ECU board or firmware.

### 1.1 Applications

| Application | Sensor | Teeth | Signal | Notes |
|-------------|--------|-------|--------|-------|
| Crank position | 36-1 trigger wheel | 36 (1 missing) | ~0.5V cranking to ~70V at 6000 RPM | Primary timing reference |
| Cam phase | Single-tooth or multi-lobe | 1-4 | Lower amplitude (smaller target) | Sequential mode sync |
| Vehicle speed (VSS) | Transfer case / trans output | 8-40 teeth | Varies with gear ratio | Speed calculation, TCC lockup |
| Driveshaft speed | Tone ring on driveshaft | Varies | Similar to VSS | 1999 F-150: rear driveshaft VR sensor for ABS / transfer case |
| Wheel speed (ABS) | Tone ring on hub | 48-100 teeth | Low amplitude at low speed | ABS inputs if needed |

The 1999 F-150 uses VR sensors for the rear ABS (driveshaft mounted) and transfer case speed. Same module handles all of these — just wire the VR sensor to the input, digital output to the ESP-ECU.

### 1.2 Current ESP-ECU Crank/Cam Implementation

**Pins (hardcoded in ECU.cpp):**
- Crank: GPIO1 — `FALLING` edge interrupt, `INPUT_PULLUP`
- Cam: GPIO2 — `RISING` edge interrupt, `INPUT_PULLUP`

**Current assumption:** Clean 3.3V digital signal arriving at the ESP32 pin — either from a Hall effect sensor or pre-conditioned VR. No on-board conditioning exists.

**Problem:** With the new 5V I/O standard (Section 1.4 of DriverModuleAnalysis), GPIO1 and GPIO2 must not see external signals directly. A VR sensor produces raw AC from millivolts to 100V+ — this absolutely cannot touch an ESP32 pin. Even a conditioned 5V signal needs level shifting to 3.3V before the GPIO.

**Solution:** VR conditioner module outputs 5V square wave → BSS138 level shifter on ESP-ECU board → 3.3V to GPIO1/GPIO2. Firmware unchanged — it still sees clean digital edges.

---

## 2. VR Sensor Signal Characteristics

### 2.1 How a VR Sensor Works

A VR (variable reluctance) sensor is a passive inductive pickup — a permanent magnet wrapped with a coil, positioned near a ferrous toothed wheel. As teeth pass the sensor:

1. **Tooth approaching:** Increasing magnetic flux → positive voltage (Faraday's law: V = -dΦ/dt)
2. **Tooth aligned:** Maximum flux, zero rate of change → **zero crossing** (this is the precise tooth position)
3. **Tooth departing:** Decreasing flux → negative voltage

The output is roughly sinusoidal with **two zero crossings per tooth**:
- **Negative-going zero crossing** (steep, at the tooth edge) — used for triggering
- **Positive-going zero crossing** (shallow, between teeth) — ignored

### 2.2 Amplitude vs RPM

The output amplitude is proportional to dΦ/dt, which is proportional to RPM. This creates an enormous dynamic range:

| Condition | RPM | Approx. Peak-to-Peak | Notes |
|-----------|-----|---------------------|-------|
| Cranking | 50-200 | 0.3–1.5V | Barely above noise floor |
| Idle | 700-900 | 1–5V | Stable, moderate amplitude |
| Cruise | 2000-3000 | 10–25V | Strong signal |
| High RPM | 5000-6000 | 30–70V | Very high amplitude |
| Redline | 7000-8000+ | 50–100V+ | Can exceed 100V with tight air gap |

**Dynamic range: ~200:1 (46 dB)** from cranking to redline. This is the fundamental challenge — the conditioner must work reliably across this entire range.

### 2.3 The 36-1 Trigger Wheel

The ESP-ECU uses a 36-1 trigger wheel (36 teeth, 1 missing) as the primary crank position reference:

```
Normal teeth:     ∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿  (35 pulses)
Missing tooth:    ∿∿∿∿∿___________∿∿∿∿∿                    (gap = 2× normal period)
```

- **Normal tooth period** at 1000 RPM: 60 / (1000 × 36) = **1.67ms**
- **Missing tooth gap:** 2 × 1.67ms = **3.33ms** (detected when period > 1.5× average)
- **At cranking (150 RPM):** Normal period = 11.1ms, gap = 22.2ms
- **At 6000 RPM:** Normal period = 0.278ms (3.6 kHz tooth frequency), gap = 0.556ms

The signal amplitude in the missing tooth gap is different — the waveform "stretches" and may have slightly lower amplitude. A good conditioner must not false-trigger during this gap.

### 2.4 Common Mode and Noise

- VR sensor is a passive inductor — pure AC output, no DC bias, centered around 0V
- In a differential configuration (VR+ and VR- both wired to the conditioner), common-mode noise from ignition, alternator, and wiring harness is rejected
- In a single-ended configuration (VR- tied to ground), common-mode rejection is poor — every ground bounce becomes signal noise
- **Recommendation:** Always use differential input conditioning

---

## 3. Signal Conditioning Approaches

### 3.1 Approach Comparison

| Approach | Complexity | Adaptive? | Zero-Crossing? | Noise Immunity | Cost | Best For |
|----------|-----------|-----------|----------------|---------------|------|----------|
| **Discrete comparator (LM393)** | Low | No (fixed threshold) | No | Moderate | ~$0.50 | Prototyping, simple test |
| **LM1815** | Low | Yes (peak-track) | Yes | Moderate (single-ended) | ~$3 | Legacy, hobbyist |
| **MAX9926 (dual)** | Medium | Yes (adaptive 33%) | Yes | Excellent (differential) | ~$5 | Production, wide RPM range |

### 3.2 Discrete Comparator — LM393 (Start Simple)

The simplest approach: a comparator with fixed hysteresis. Good for first prototype and testing.

**Circuit:**

```
VR Sensor                          LM393 Comparator                    Output
                                  ┌─────────────────┐
VR+ ──[10kΩ]──┬──[clamp]────────▶│+ (non-inverting) │
              │                   │                   │──── 5V square wave
             [1kΩ shunt]         │                   │     (open-collector
              │                   │                   │      + 10kΩ pullup to 5V)
VR- ──[10kΩ]──┴──[clamp]────────▶│- (inverting)     │
                                  │                   │
                                  │  VREF (divider)   │
                                  └─────────────────┘

Input protection: Back-to-back 5.1V zener clamps on each input (BAV99 + BZT52C5V1)
Hysteresis: 100kΩ feedback from output to non-inverting input (~50mV hysteresis)
VREF: 2.5V via 10kΩ/10kΩ divider from 5V supply (sets the comparator midpoint)
```

**Pros:**
- Cheap, simple, easy to solder and debug
- Available everywhere (LM393 is ubiquitous)
- Good enough for testing the digital interface to ESP-ECU

**Cons:**
- Fixed threshold — trigger point shifts with amplitude (timing error varies with RPM)
- No zero-crossing detection — triggers at threshold, not at tooth edge
- Poor at cranking (signal may be below threshold)
- May false-trigger during 36-1 missing tooth gap at low RPM
- Needs input clamping to survive 100V+ at high RPM

**Timing error estimate:** At 6000 RPM with 50mV threshold on a 70Vpp signal, the trigger occurs ~0.04% before the zero crossing (negligible). At cranking with 50mV threshold on a 0.5Vpp signal, the trigger occurs ~10% before the zero crossing (~1.1ms on a 11.1ms tooth period). This is significant but may be acceptable for initial testing.

**When to use:** First prototype. Get the digital interface working, verify the ESP-ECU firmware sees teeth correctly, test the level shifter. Then upgrade to MAX9926.

### 3.3 MAX9926 — Dual Adaptive VR Conditioner (Production)

Purpose-built for exactly this application. Handles both crank and cam in one IC.

**Key specs:**

| Parameter | Value |
|-----------|-------|
| Supply | 4.5–5.5V |
| Channels | 2 (dual) |
| Input type | Differential (VR+/VR- per channel) |
| Adaptive threshold | 33% of previous peak amplitude |
| Zero-crossing detection | Negative-going, speed-invariant |
| Propagation delay | ~3µs typical |
| Output type | Open-drain (needs external pullup) |
| Timeout (no signal) | ~85ms (threshold decays) |
| Input voltage tolerance | Up to 300Vpp with 10kΩ series input resistors |
| Package | 16-pin QSOP |
| Temp range | -40°C to +125°C (automotive) |
| Quiescent current | ~4.7mA |

**How adaptive zero-crossing works:**
1. Peak-detects the incoming signal amplitude
2. Sets arming threshold to 33% of previous peak
3. Signal must exceed threshold to "arm" the zero-crossing detector (prevents noise false triggers)
4. Once armed, waits for the precise negative-going zero crossing
5. Generates clean output edge at the zero crossing
6. Zero crossing is **speed-invariant** — always at the same physical tooth position regardless of RPM

**Circuit (Mode A2 — minimum external components):**

```
                        MAX9926 (QSOP-16)
                    ┌──────────────────────┐
VR1+ ──[10kΩ]──────│ INA+    VCC ─── 5V   │──── 5V
                    │                      │
VR1- ──[10kΩ]──────│ INA-    GND ─── GND  │──── GND
                    │                      │
                    │ ZERO_EN ── VCC       │ (enable zero-crossing)
                    │ INT_THRS ── VCC      │ (use internal adaptive threshold)
                    │                      │
                    │ COUTA ───────────────│──── CRANK_OUT (open-drain)
                    │                      │        │
VR2+ ──[10kΩ]──────│ INB+                 │       [10kΩ] pullup to 5V
                    │                      │        │
VR2- ──[10kΩ]──────│ INB-                 │──── 5V square wave to ESP-ECU
                    │                      │
                    │ COUTB ───────────────│──── CAM_OUT (open-drain)
                    │                      │        │
                    └──────────────────────┘       [10kΩ] pullup to 5V

Optional: 1kΩ shunt across each VR+/VR- pair (low-impedance load, improves noise rejection)
Optional: 100pF caps on INA+/INA- for high-frequency EMI filtering
```

**Why open-drain output is ideal:**
The MAX9926 open-drain output can be pulled up to **any voltage** — pull it to 5V for the standard I/O bus, then level-shift to 3.3V at the ESP-ECU. Or if the module is mounted on the ESP-ECU board, pull directly to 3.3V (no level shifter needed). This flexibility is why open-drain is preferred over push-pull for this interface.

**Propagation delay impact:**
- MAX9926 delay: ~3µs
- At 6000 RPM, one tooth period = 278µs
- Delay = 3/278 = **1.1% of tooth period** — well within acceptable limits
- This is a fixed delay (not RPM-dependent), so firmware can compensate if needed
- Compared to LM393 threshold-based triggering, the MAX9926's timing error is constant and predictable

### 3.4 LM1815 — Legacy (Not Recommended for New Design)

The classic VR conditioner from National Semiconductor / TI. Still active but considered legacy.

| Parameter | LM1815 | MAX9926 |
|-----------|--------|---------|
| Input | Single-ended (GND referenced) | Differential (CMRR) |
| Channels | 1 | 2 |
| Noise immunity | Poor (shared ground path) | Excellent (differential rejection) |
| Missing tooth handling | Unreliable with noise | Robust adaptive threshold |
| Package | 8-pin DIP/SOIC | 16-pin QSOP |
| Supply | 2–12V | 4.5–5.5V |

**Verdict:** The LM1815's single-ended input means every ground bounce from ignition coils, alternator ripple, and starter motor current shows up as signal noise. In an engine bay, this is a serious problem. Use the MAX9926 for any new design.

---

## 4. Module Design

### 4.1 Design Philosophy

**Start simple, test, iterate:**

| Generation | IC | Features | Purpose |
|------------|-----|----------|---------|
| **Gen 1** | LM393 (discrete comparator) | Fixed threshold, input clamping, 5V output | Get the interface working, test wiring, verify firmware |
| **Gen 2** | MAX9926 (dual adaptive) | Adaptive threshold, zero-crossing, differential input | Production quality, wide RPM range, reliable missing-tooth detection |
| **Gen 3** | MAX9926 + STM32 (like driver modules) | Adds Modbus diagnostics: signal amplitude, frequency, health, noise floor | Full diagnostic feedback, signal quality monitoring |

All three generations present the same interface to the ESP-ECU: **5V digital square wave, one pulse per tooth.** The ECU firmware doesn't change — only the module improves.

### 4.2 Module Interface

**Input connector (J1) — VR sensor:**

| Pin | Signal | Notes |
|-----|--------|-------|
| 1 | VR+ | Sensor positive lead |
| 2 | VR- | Sensor negative lead (or shield/ground for single-ended sensors) |

**Output connector (J2) — to ESP-ECU:**

| Pin | Signal | Notes |
|-----|--------|-------|
| 1 | OUT | 5V CMOS square wave (one edge per tooth) |
| 2 | +5V | Power from ESP-ECU (feeds the conditioner IC) |
| 3 | GND | Common ground |

For dual-channel (crank + cam on one module):

| Pin | Signal | Notes |
|-----|--------|-------|
| 1 | CRANK_OUT | 5V square wave, crank teeth |
| 2 | CAM_OUT | 5V square wave, cam pulses |
| 3 | +5V | Power |
| 4 | GND | Ground |

### 4.3 Gen 1 Module — LM393 Discrete (Prototype)

**Board size:** ~25×15mm (tiny, mounts near sensor)

**Schematic:**

```
                    +5V
                     │
                    [10kΩ] R5 (output pullup)
                     │
J1.VR+ ──[10kΩ R1]──┬──[D1 BAV99]── +5V       J2.OUT ◀──┘
                     │      │                         │
                     │   [D2 BAV99]── GND         LM393 output
                     │                                │
                    (+) LM393                         │
                     │     │                     [100kΩ R6] feedback
                     │   output ──────────────────────┘
                    (−) LM393
                     │
J1.VR- ──[10kΩ R2]──┤
                     │
                    [10kΩ R3]──┬──[10kΩ R4]── GND
                               │
                              VREF (2.5V midpoint)

Input clamping: D1/D2 (BAV99 dual diode) clamp each input to 0V–5V range
Shunt: Optional 1kΩ across VR+/VR- at connector (low-Z load for noise immunity)
Hysteresis: R6 (100kΩ) from output to (+) input gives ~50mV hysteresis window
Decoupling: 100nF + 10µF on +5V rail
```

**BOM (Gen 1):**

| Ref | Qty | Part | Package | Notes |
|-----|-----|------|---------|-------|
| U1 | 1 | LM393DR | SOIC-8 | Dual comparator (only 1 channel used per module) |
| R1,R2 | 2 | 10kΩ | 0402 | Input series resistors (voltage limiting) |
| R3,R4 | 2 | 10kΩ | 0402 | VREF divider (2.5V) |
| R5 | 1 | 10kΩ | 0402 | Output pullup to 5V |
| R6 | 1 | 100kΩ | 0402 | Hysteresis feedback |
| D1,D2 | 2 | BAV99 | SOT-23 | Dual input clamping diodes |
| C1 | 1 | 100nF | 0402 | Supply decoupling |
| C2 | 1 | 10µF | 0805 | Supply bulk |
| J1 | 1 | 2-pin JST | — | VR sensor input |
| J2 | 1 | 3-pin JST | — | Output + power |

**Total: ~11 parts, <$2 BOM**

### 4.4 Gen 2 Module — MAX9926 Dual Adaptive (Production)

**Board size:** ~30×20mm

Handles **both crank and cam** on a single module — two differential VR inputs, two 5V square wave outputs.

**BOM (Gen 2):**

| Ref | Qty | Part | Package | Notes |
|-----|-----|------|---------|-------|
| U1 | 1 | MAX9926UAEE+ | QSOP-16 | Dual adaptive VR conditioner |
| R1-R4 | 4 | 10kΩ | 0402 | Input series resistors (2 per channel, VR+/VR-) |
| R5,R6 | 2 | 10kΩ | 0402 | Output pullups to 5V |
| R7,R8 | 2 | 1kΩ | 0402 | Optional input shunts (across VR+/VR- per channel) |
| C1-C4 | 4 | 100pF | 0402 | Input EMI filters (1 per VR pin) |
| C5 | 1 | 100nF | 0402 | Supply decoupling |
| C6 | 1 | 10µF | 0805 | Supply bulk |
| J1 | 1 | 4-pin connector | — | Dual VR sensor input (VR1+, VR1-, VR2+, VR2-) |
| J2 | 1 | 4-pin connector | — | Outputs + power (CRANK, CAM, +5V, GND) |

**Total: ~15 parts, ~$7 BOM**

**Configuration (Mode A2):**
- ZERO_EN → VCC (zero-crossing enabled)
- INT_THRS → VCC (internal adaptive threshold)
- BIAS → float (internal bias reference)

This is the recommended production module. The adaptive 33% threshold handles the full dynamic range from cranking to redline, and the zero-crossing output eliminates RPM-dependent timing jitter.

### 4.5 Gen 3 Module — MAX9926 + STM32 Diagnostics (Future)

Same as Gen 2 but adds an STM32F030 MCU (like the driver modules) for:
- Monitoring VR signal amplitude (ADC on the differential amp output)
- Counting teeth and verifying pattern integrity
- Reporting signal quality, noise floor, and frequency via Modbus RTU
- Detecting sensor degradation (decreasing amplitude at given RPM over time)
- Dashboard integration: signal quality pill (green/yellow/red)

This is a future enhancement — get Gen 1/Gen 2 working first.

---

## 5. ESP-ECU Integration

### 5.1 Level Shifting — 5V Module Output to 3.3V ESP32 GPIO

The conditioner module outputs 5V square waves. The ESP32 GPIO1/GPIO2 need 3.3V. A BSS138 MOSFET level shifter on the ESP-ECU board handles this.

**BSS138 bidirectional level shifter circuit (per signal):**

```
        3.3V (ESP32 side)              5V (module side)
         │                              │
        [10kΩ] R_LV                    [10kΩ] R_HV
         │                              │
GPIO ────┤                              ├──── Module OUT
         │                              │
         ├── Source ──── Gate ── 3.3V    ├── Drain
         │         BSS138               │
         └──────────────────────────────┘
```

**Propagation delay:** The BSS138 switches in ~5ns, but the RC time constant of the pullup + load capacitance dominates the rising edge:
- With 10kΩ pullup and ~30pF load: τ = 300ns → rise time ~600ns
- For 4.7kΩ pullup: τ = 141ns → rise time ~280ns
- Crank tooth frequency at 8000 RPM: 4.8 kHz (period = 208µs)
- 600ns rise time / 208µs period = **0.3%** — negligible

**Recommendation:** Use 4.7kΩ pullups for faster edges. Two BSS138 (one for crank, one for cam) in SOT-23 — trivial board space.

**Alternative — open-drain direct pull to 3.3V:**
If the MAX9926 output is open-drain (which it is), and the module is powered from 5V, you can pull the output up to **3.3V instead of 5V** at the ESP-ECU end. Then no level shifter is needed — the open-drain output only sinks to GND or floats, and the 3.3V pullup gives direct ESP32-compatible logic levels. However, this means the wire carries 3.3V logic (less noise margin than 5V). For short wires on the ECU board this is fine; for longer wires to a remote module, use 5V + BSS138.

### 5.2 Pin Assignment Update

Current hardcoded pins in ECU.cpp:
- Crank: GPIO1 (FALLING edge, INPUT_PULLUP)
- Cam: GPIO2 (RISING edge, INPUT_PULLUP)

These pin assignments stay the same — the firmware doesn't change. The only physical change is:

**Before (current):** VR sensor → ??? → GPIO1/GPIO2 (exposed to external signals)
**After:** VR sensor → Conditioner module → 5V square wave → BSS138 level shifter on ESP-ECU → 3.3V → GPIO1/GPIO2 (protected)

### 5.3 Hall Effect Sensor Compatibility

Many modern crank/cam sensors are Hall effect (digital output, usually 5V open-collector or push-pull) rather than VR. The same level-shifting path handles these:

| Sensor Type | Conditioner Module? | Level Shifter? | Notes |
|-------------|-------------------|----------------|-------|
| VR (inductive) | Yes — LM393 or MAX9926 | Yes — BSS138 5V→3.3V | Full conditioning needed |
| Hall effect (5V open-collector) | No — already digital | Yes — BSS138 5V→3.3V | Just level shift |
| Hall effect (5V push-pull) | No — already digital | Yes — BSS138 5V→3.3V | Just level shift |
| Hall effect (12V) | No — already digital | Yes — voltage divider or BSS138 | May need resistive divider first |

The BSS138 level shifter on the ESP-ECU board is always needed regardless of sensor type — it's the protective boundary. The conditioner module is only needed for VR sensors.

### 5.4 Firmware Considerations

**No firmware changes needed** for the conditioner module — the CrankSensor and CamSensor classes already expect clean digital edges:

- CrankSensor: FALLING edge ISR, 50µs noise filter, 8-tooth rolling average gap detection
- CamSensor: RISING edge ISR, 2000ms timeout, phase detection based on crank tooth position

The 50µs noise filter in CrankSensor.cpp provides an additional layer of protection against any ringing or glitches that survive the conditioner. At 8000 RPM (max RPM cap = 20,000 in firmware), one tooth period = 208µs, so 50µs rejection safely filters spurious edges without losing real teeth.

**Polarity configuration:** The MAX9926 triggers on the negative-going zero crossing, producing a falling edge output. The LM393 triggers on the positive-going crossing by default (comparator trip direction). Ensure the output polarity matches what the firmware expects:
- Crank: FALLING edge → MAX9926 native polarity is correct
- Cam: RISING edge → may need to invert (use the second comparator in LM393 as an inverter, or swap VR+/VR- on the MAX9926 input)

---

## 6. Physical Installation

### 6.1 Mounting Location

**Near the sensor, not near the ECU.** The raw VR signal is a high-impedance, low-amplitude analog signal susceptible to EMI pickup from ignition coils, alternator, and starter motor. The conditioned digital output is a low-impedance, rail-to-rail square wave that is virtually immune to noise.

```
[VR Sensor] ──── short shielded wire (6-12") ────▶ [Conditioner Module]
                                                           │
                                                    long wire (any length)
                                                           │
                                                    ▼
                                                [ESP-ECU BSS138 level shifter]
                                                           │
                                                    GPIO1/GPIO2
```

### 6.2 Wiring

- **VR sensor to module:** Twisted pair or shielded cable, keep under 12 inches. Shield grounded at the module end only (avoid ground loops).
- **Module to ESP-ECU:** Standard wire in the engine harness. 5V digital signal is robust against EMI. Can run alongside other wiring without concern.
- **Power:** Module powered from ESP-ECU 5V rail via the output cable (J2 pin 3). Current draw is minimal (~5-10mA).

### 6.3 PCB Design

> **Full board design analysis:** See [BoardDesignAnalysis.md](BoardDesignAnalysis.md) for complete trace sizing tables, stackup recommendations, and ground plane strategy for all ESP-ECU system boards.

The VR conditioner is the simplest board in the ESP-ECU ecosystem — 2-layer FR4, no thermal concerns:

| Parameter | Specification |
|-----------|---------------|
| Layers | 2 (top signal + bottom ground pour) |
| Substrate | FR4, 1.0mm thickness (thinner = smaller package) |
| Copper | 1oz both layers |
| Board size | 25×15mm (Gen 1) to 30×20mm (Gen 2) |
| Controlled impedance | Not needed (all signals <5 MHz) |
| All traces | 8-10 mil (signal current <50mA everywhere) |
| Ground | Near-solid bottom pour, star ground at comparator/MAX9926 GND pin |

No aluminum substrate needed — total board dissipation is <50mW (comparator IC + pullups only).

### 6.4 Enclosure

The module should be potted or conformal-coated for engine bay environment. A small heat-shrink enclosure or snap-fit case keeps it protected. The module is small enough to zip-tie to the engine harness near the sensor connector.

---

## 7. Driveshaft / VSS Application — 1999 F-150

### 7.1 Ford F-150 Speed Sensors

The 1999 F-150 (especially 4WD with 4R70W/4R100 transmission) uses VR sensors for:

- **VSS (Vehicle Speed Sensor):** On the transfer case output or transmission tailshaft. Produces ~8 pulses per driveshaft revolution (tooth count varies by model).
- **Rear ABS sensor:** On the rear differential ring gear or driveshaft tone ring. Higher tooth count (~50 teeth) for finer resolution.
- **Front ABS sensors:** On the front hubs (if 4WD). Similar VR pickups.

### 7.2 Using the VR Conditioner for Driveshaft Speed

Same module, different application:

| Parameter | Crank (36-1) | Driveshaft VSS |
|-----------|-------------|----------------|
| Tooth count | 36 (1 missing) | 8-16 (no missing) |
| Max frequency | ~4.8 kHz (8000 RPM) | ~500 Hz (80 MPH) |
| Amplitude range | 0.5–100V | 0.2–30V (lower RPM range) |
| Conditioner | MAX9926 or LM393 | Same — identical module |
| ESP-ECU input | GPIO1 (crank ISR) | Any available GPIO with interrupt |

The firmware would treat the driveshaft speed sensor like another toothed wheel — count pulses per unit time to calculate RPM, then convert to vehicle speed using the tire circumference and final drive ratio.

For the transmission manager, driveshaft speed is valuable for:
- TCC (Torque Converter Clutch) lockup control — compare engine RPM to driveshaft RPM
- Shift quality monitoring — track output shaft speed changes during shifts
- Speedometer drive — replace the mechanical speedometer cable with electronic output

### 7.3 ESP-ECU Firmware Extension

A new `SpeedSensor` class (or extension of `CrankSensor` with different tooth count) could handle VSS:
- Same ISR-driven tooth counting
- No missing tooth detection needed (VSS wheels usually have even teeth)
- RPM-to-MPH conversion: `MPH = (driveshaft_RPM × tire_circumference) / (final_drive_ratio × 1056)`
- Pin assignment: configurable GPIO (through the level shifter, like all other I/O)

---

## 8. Open Items

1. **Gen 1 prototype:** Build and test LM393 module on a bench with a signal generator simulating VR output. Verify clean square wave output at 5V, test with ESP-ECU level shifter → GPIO.

2. **Gen 2 MAX9926 sourcing:** Verify LCSC/Mouser/DigiKey stock for MAX9926UAEE+. Alternative: two MAX9924 (single-channel) if the dual part is unavailable.

3. **Output polarity:** Verify that MAX9926 COUTA falling edge aligns with CrankSensor FALLING interrupt. Verify cam polarity (RISING edge) — may need VR+/VR- swap or firmware edge selection.

4. **ESP-ECU board mod:** Add two BSS138 level shifters (SOT-23) near GPIO1/GPIO2 with 4.7kΩ pullups. Simple bodge-wire mod for existing boards, designed-in for next revision.

5. **VSS firmware:** Design SpeedSensor class for driveshaft/VSS pulse counting. Configurable tooth count, no missing-tooth gap detection.

6. **Shielded cable spec:** Determine minimum cable spec for VR sensor to module connection. Twisted pair with drain wire should be sufficient for 6-12" runs.

7. **Timeout at cranking:** MAX9926 adaptive threshold has ~85ms timeout. At cranking (150 RPM), the missing tooth gap is 22ms — well within timeout. Verify no issues at very low cranking speeds (<50 RPM) where gap could approach 60ms.
