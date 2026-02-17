# Cranking Brown-out Analysis — ESP-ECU

**Date:** 2026-02-17
**Context:** What happens when battery voltage sags 60%+ during engine cranking?

---

## Rail Dropout Thresholds

Each rail has a different minimum VIN before it falls out of regulation:

| Rail | Regulator | Min VIN for regulation | Calculation |
|------|-----------|----------------------|-------------|
| 6.8VA (analog) | U4 LGS5145 | **~7.5V** | 6.9V / 0.94 (Dmax) + 0.2A × 0.6Ω (Rdson) = 7.46V |
| 5V (digital) | U1 LM2576 | **~6.0-6.5V** | 5.0V + 1.0-1.5V (Vsat switch) |
| 3.3V (ESP32) | U3 LGS5145 | **~3.7V** | 3.3V / 0.94 + 0.3A × 0.6Ω = 3.69V; UVLO at 3.5V |

**Dropout order:** 6.8VA dies first → 5V dies second → 3.3V dies last

---

## Voltage Sag Scenarios

### Battery at 60% (VIN = 7.2V) — Light cranking

| Rail | Min VIN | Status | Consequence |
|------|---------|--------|-------------|
| 6.8VA | ~7.5V | **DROPPING OUT** | REF5050s, AMS1117 losing input → ADS1115, mux, PCA9306 losing power |
| 5V | ~6.0V | Regulating | ESP32 peripherals, MCP23S17 expanders OK |
| 3.3V | ~3.7V | Regulating | ESP32 core OK |

Right at the edge of 6.8V dropout. Analog sensing unreliable, engine control still works.

### Battery at 50% (VIN = 6.0V) — Normal cranking

| Rail | Status | Consequence |
|------|--------|-------------|
| 6.8VA | **DEAD** | All analog sensing offline |
| 5V | **DROPPING OUT** | MCP23S17 expanders losing power → coil/injector outputs go high-Z |
| 3.3V | Regulating | ESP32 alive but can't control anything |

### Battery at 40% (VIN = 4.8V) — Heavy cranking / weak battery

| Rail | Status | Consequence |
|------|--------|-------------|
| 6.8VA | **DEAD** | |
| 5V | **DEAD** | All expanders, 5V peripherals offline |
| 3.3V | Regulating | ESP32 running but blind — zombie state |

### Battery at 30% (VIN = 3.5V) — Extreme sag

| Rail | Status | Consequence |
|------|--------|-------------|
| 6.8VA | **DEAD** | |
| 5V | **DEAD** | |
| 3.3V | **DEAD** (UVLO at 3.5V) | ESP32 resets, clean power-on restart when voltage recovers |

---

## Cascading Failure Sequence

The danger isn't just rails dropping — it's the **partial-power states** between thresholds:

1. **VIN < 7.5V** → 6.8V dies → ADS1115 VDD drops → I2C bus errors → AMS1117 output drops → mux/PCA9306 lose power. Sensor readings become garbage. If the code doesn't handle this, bad sensor data causes incorrect fuel/spark calculations.

2. **VIN < 6.0V** → 5V dies → MCP23S17 expanders reset → all expander outputs go to **default state (high-impedance inputs)**. Coil and injector driver inputs float. Depending on the driver circuit:
   - Coils fire uncontrolled (stuck dwell → **overheating coils, potential fire**)
   - Injectors stuck open (flooding, hydro-lock risk)
   - Or safely off (if driver circuit has pull-down to GND)

3. **VIN still > 3.7V** → 3.3V holds → ESP32 is alive but blind and unable to control outputs. May log I2C/SPI errors, try to recover, or enter undefined state. This "zombie state" can last hundreds of milliseconds during cranking.

4. **Voltage recovers** (engine catches) → Rails come back in reverse order: 3.3V (already up) → 5V → 6.8VA (last, highest threshold). There's a window where expanders are initializing but analog isn't ready yet — sensor data is invalid during this recovery window.

---

## Recommendations

### 1. SAFETY-CRITICAL: Pull-down resistors on coil/injector driver gates

When MCP23S17 expanders lose power, their outputs go high-impedance. The coil/injector MOSFET driver gate inputs must default to OFF (no fire, no injection).

**Fix:** Add **10k-47k pull-down resistors** on each coil and injector MOSFET gate to GND. This ensures:
- Coil dwell terminates immediately when expander loses power (no stuck-on dwell)
- Injectors close when expander loses power (no flooding)
- Safe state is guaranteed by passive hardware, not dependent on software

This is the single most important safety item in the entire power supply design. Without it, a cranking brown-out could cause uncontrolled ignition and fuel delivery.

### 2. Software: detect and handle brown-out gracefully

The ESP32 monitors battery voltage via GPIO9 (47k/10k divider → ADC). Add brown-out detection to the ECU update loop:

```
// In ECU::update() — check before processing sensor data
if (batteryVoltage < 8.0f) {
    // 6.8V analog rail is dead or dying — sensor data is garbage
    // Don't use MAP/TPS/AFR readings for fuel/spark calculations
    // Fall back to safe cranking defaults (fixed timing, cranking fuel)
    _state.cranking = true;
    _state.limpMode = true;
}

if (batteryVoltage < 6.5f) {
    // 5V rail is dying — expanders may be resetting
    // Disable all coil/injector outputs via native GPIO pins
    // Don't try to communicate with MCP23S17 (they're offline)
}

// Recovery: when voltage climbs back above 9V
if (batteryVoltage > 9.0f && previousBatteryVoltage < 8.0f) {
    // Re-initialize expanders (they've been reset)
    // Re-initialize I2C devices (ADS1115 lost power)
    // Resume normal sensor reading after one full scan confirms valid data
}
```

Key thresholds:
- **< 8.0V:** Don't trust analog sensor readings (6.8VA rail marginal)
- **< 6.5V:** Don't trust expander state (5V rail marginal)
- **> 9.0V recovery:** Re-initialize all peripherals before resuming control

### 3. Power 3.3V from 5V rail instead of VIN

Currently both LGS5145s (3.3V and 6.8V) run from VIN directly. This creates the zombie state where ESP32 (3.3V) is alive but everything it controls (5V) is dead.

**Fix:** Power U3 (3.3V LGS5145) from the 5V LM2576 output instead of VIN:

```
Current:   VIN → U3 (LGS5145) → 3.3V     (independent of 5V rail)
Proposed:  VIN → U1 (LM2576) → 5V → U3 (LGS5145) → 3.3V
```

Benefits:
- 5V dies → 3.3V dies simultaneously → ESP32 cleanly resets via brownout detector
- No zombie state — either the ECU is fully functional or it's in reset
- When power recovers, everything boots together with proper sequencing
- 5V→3.3V is within LGS5145 range (VIN min = 4.5V; 5.0V input provides 1.7V headroom)

Trade-off:
- Adds one more conversion stage in series (tiny efficiency loss)
- 3.3V now depends on LM2576 — if 5V has issues, 3.3V goes down too
- Slightly slower 3.3V recovery (must wait for 5V to stabilize first)

This is the cleanest solution — eliminates the partial-power zombie state entirely.

### 4. Fast boot priority after reset

If the ECU resets during cranking, minimize time to regain control:

**Boot priority order:**
1. **Immediate** (< 10ms): Configure native GPIO coil/injector pins as outputs (LOW = safe)
2. **Fast** (< 50ms): Initialize crank/cam ISRs — start timing the engine immediately
3. **Normal** (< 200ms): Initialize SPI bus, MCP23S17 expanders, set output states
4. **Normal** (< 500ms): Read SD card config, load tune tables
5. **Deferred** (< 2s): Initialize WiFi, MQTT, web server, diagnostics

The engine will likely catch and start running during the 1-2 second boot. Having crank/cam ISRs up within 50ms means the ECU can begin basic spark/fuel control before the full config is loaded, using hardcoded safe defaults.

### 5. Hold-up capacitance assessment

Can capacitors ride through the cranking sag?

| Capacitance on VIN | ΔV (12→7.5V) | Load | Hold-up time |
|-------------------|-------------|------|-------------|
| 560µF (current C5) | 4.5V | 500mA | **5ms** |
| 2200µF (add bulk) | 4.5V | 500mA | **20ms** |
| 0.1F supercap | 4.5V | 500mA | **900ms** |

Cranking sag lasts 100-500ms typically. Only a supercap provides meaningful ride-through, but at significant cost and size. **Software brown-out handling + driver pull-downs is more practical than trying to ride through with capacitors.**

---

## Summary

| Priority | Action | Why |
|----------|--------|-----|
| **SAFETY-CRITICAL** | Pull-down resistors on coil/injector driver gates | Prevents uncontrolled fire/injection during 5V brown-out |
| **Strong rec** | Software brown-out detection (VBAT < 8V → safe mode) | Prevents bad sensor data from causing wrong spark/fuel |
| **Strong rec** | Power 3.3V from 5V rail (not VIN) | Eliminates zombie state, clean reset on brown-out |
| **Recommended** | Fast-boot priority (crank ISR first, WiFi last) | Minimizes blind time after crank-caused reset |
| **Nice-to-have** | Supercap on VIN for ride-through | Prevents reset entirely, but expensive and large |

**The pull-down resistors on driver gates are the #1 safety item.** Everything else is damage mitigation — the pull-downs prevent physical damage (coil overheating, engine flooding) that no amount of software can fix.
