# 4-Channel Low-Side Driver Module — Design Specification

**Date:** 2026-02-18
**Rev:** 0.1
**Context:** Remote coil/injector driver with current sensing, Modbus RTU diagnostic feedback to ESP-ECU

---

## Overview

Universal 4-channel low-side MOSFET driver module for ignition coils or fuel injectors. Each module receives logic-level control signals directly from the ESP-ECU (MCP23S17 expander outputs) and switches 12V loads through discrete N-channel MOSFETs. On-board current sensing (INA180A1) feeds a local STM32 MCU that reports diagnostics back to the ESP-ECU via Modbus RTU over RS-485.

**Key design goals:**
- TO-220 MOSFETs in sockets for field replacement
- Same PCB for coil and injector variants (BOM options)
- 4 channels per module = 1 bank of a V8, or a complete 4-cylinder
- V8 configuration: 2 coil modules + 2 injector modules
- Control signals are direct-wired (no Modbus latency in the drive path)
- Modbus carries only diagnostic data (current, pulse width, faults)

---

## Signal Flow

```
ESP-ECU                          Driver Module                    Load
MCP23S17 ──[4× logic signals]──▶ UCC27524A ──▶ MOSFET ──▶ Coil/Injector
expander     (3.3V push-pull)    gate driver   (TO-220)    (12V switched)
                                                  │
                                            ┌─────▼──────┐
                                            │ INA180A1   │
                                            │ + shunt R  │
                                            └─────┬──────┘
                                                  │ (analog)
                                            ┌─────▼──────┐
                                            │ STM32F030  │
                                            │ ADC + UART │
                                            └─────┬──────┘
                                                  │ (UART)
                                            ┌─────▼──────┐
ESP-ECU ◀──[RS-485 Modbus RTU]────────────── │ SP3485     │
UART1 GPIO16/17                              └────────────┘
```

---

## Electrical Specifications

### Power

| Parameter | Value | Notes |
|-----------|-------|-------|
| Load input voltage | 10-16V (from ECU ISEN−, battery post-sense) | Protected by ECU fuse (F1) + SMBJ36A + per-module resettable PTC fuse |
| Electronics input | 12V (from RS-485 module TPS61089 bus) | Powers MCU, sense amps, gate drivers, VREF |
| Logic supply | 3.3V via AMS1117-3.3 | Powers MCU, INA180A1, SP3485 |
| Gate driver supply | 12V (direct from VIN) | UCC27524A VDD, full gate enhancement |
| Quiescent current | ~25mA | MCU + drivers + RS-485 idle |

### Control Inputs (4 channels + enable)

| Parameter | Value | Notes |
|-----------|-------|-------|
| Input voltage | 3.3V CMOS (from MCP23S17) | VIH >2.0V, VIL <0.8V |
| Input impedance | >10kΩ | UCC27524A input |
| Input pulldown | 10kΩ per channel + enable | Ensures OFF when disconnected |
| Enable pin | Active-HIGH, 10kΩ pulldown | Must be driven HIGH to allow any output |
| Input connector | 7-pin header: EN + CH1-CH4 + VCC + GND | Enable is mandatory for outputs |

### Enable Circuit (Misfire Protection)

The enable pin controls a P-channel MOSFET (DMP6023LE or equivalent) on the 12V output rail feeding all 4 power MOSFETs. This is a hard kill — regardless of gate driver state or MCU faults, no load current can flow without enable HIGH.

```
ISEN− (battery) ──▶ PTC fuse(s) ──▶ Q5 (P-FET, DMP6023LE) ──▶ +12V_OUT (to load connectors)
              │ gate
              ├── R_gate (10kΩ to +12V = default OFF)
              │
              └── Q6 (N-FET, 2N7002) gate ← EN pin (3.3V from ESP-ECU)
                   drain → Q5 gate (pulls low = P-FET ON)
                   source → GND

EN = LOW (default, pulldown):  Q6 OFF → Q5 gate pulled to +12V → P-FET OFF → no output
EN = HIGH (ESP-ECU active):    Q6 ON → Q5 gate pulled to GND → P-FET ON → outputs armed
```

**Failure modes — all safe:**
- ESP-ECU not powered → EN floats LOW → outputs OFF
- EN wire broken → pulldown holds LOW → outputs OFF
- ESP-ECU crash/watchdog → EN can be tied to a GPIO with watchdog-gated output
- Q5 P-FET fails open → no output (safe)
- Q5 P-FET fails short → outputs still controlled by individual MOSFETs (existing protection active)

### Power Outputs (4 channels)

| Parameter | Injector Variant | Coil Variant (w/ igniter) | Coil Variant (direct) |
|-----------|-----------------|--------------------------|----------------------|
| Max continuous current | 2A | 2A | 10A |
| Peak current | 5A | 5A | 15A |
| Max switching voltage | 55V | 55V | 100V |
| MOSFET Rds(on) | 22mΩ (IRLZ44N) | 22mΩ (IRLZ44N) | 3.7mΩ (IRFB4110) |
| Switching speed | <50ns | <50ns | <10ns |
| Flyback clamp | SMBJ40A (40V standoff) | SMBJ40A | SMBJ80A (80V standoff) |

### Current Sensing

| Parameter | Injector Variant | Coil Variant (direct) |
|-----------|-----------------|----------------------|
| Sense amplifier | INA180A1 (20V/V) | INA180A1 (20V/V) |
| Shunt resistor | 100mΩ ±1% (2512) | 10mΩ ±1% (2512) |
| Full-scale current | 1.65A | 16.5A |
| ADC resolution | 12-bit (STM32) | 12-bit (STM32) |
| Current resolution | 0.4mA | 4.0mA |
| Bandwidth | 35kHz (-3dB) | 35kHz (-3dB) |

### Communication

| Parameter | Value |
|-----------|-------|
| Protocol | Modbus RTU slave |
| Physical | RS-485 half-duplex (SP3485) |
| Baud rate | 9600 (default), configurable |
| Address | 1-8, set by solder jumpers or DIP switch |
| Connector | 4-pin header: A, B, VCC, GND |
| Termination | 120Ω optional (solder jumper) |

---

## Component Selection

### U1: MCU — STM32F030F4P6 (TSSOP-20)

- ARM Cortex-M0, 48MHz, 16KB Flash, 4KB RAM
- 4× ADC channels (PA0-PA3) for INA180A1 outputs
- 1× UART (PA2/PA3 remap or PA9/PA10) for Modbus RTU
- 1× GPIO for RS-485 DE/RE direction (PA4)
- Internal 8MHz HSI oscillator — no external crystal needed (±1% accuracy is fine for 9600 baud)
- Boot0 pin for firmware programming via UART
- Operating voltage: 2.4-3.6V

**Pin assignment:**

| Pin | Function | Connection |
|-----|----------|------------|
| PA0 | ADC_IN0 | INA180A1 CH1 output |
| PA1 | ADC_IN1 | INA180A1 CH2 output |
| PA2 | ADC_IN2 | INA180A1 CH3 output |
| PA3 | ADC_IN3 | INA180A1 CH4 output |
| PA4 | GPIO out | SP3485 DE/RE |
| PA5 | GPIO in | Board temp (NTC via ADC_IN5, optional) |
| PA9 | USART1_TX | SP3485 DI |
| PA10 | USART1_RX | SP3485 RO |
| PA13 | SWDIO | Programming/debug |
| PA14 | SWCLK | Programming/debug |
| PB1 | GPIO out | Status LED |
| NRST | Reset | 100nF to GND + 10kΩ pullup |
| BOOT0 | Boot select | 10kΩ pulldown (normal), jumper to VDD for UART programming |

### U2, U3: Gate Driver — UCC27524ADBVR (SOT-23-6)

- Dual 5A low-side gate driver
- 4.5-18V VDD (run at VIN ~12V)
- TTL/CMOS compatible inputs: VIH=2.0V, VIL=0.8V
- 13ns rise / 12ns fall time
- Non-inverting: input HIGH = output HIGH = MOSFET ON
- 2 ICs × 2 channels = 4 channels

### Q1-Q4: Power MOSFET — TO-220

**Injector / COP-with-igniter variant: IRLZ44NPBF**
- 55V, 47A, 22mΩ Rds(on) at Vgs=10V
- Logic-level gate: Vgs(th) = 1.0-2.0V, fully enhanced at 5V
- Qg = 42nC — switching time with 5A driver: ~8ns
- TO-220AB — fits standard sockets or PCB mount

**Direct coil drive variant: IRFB4110PBF**
- 100V, 180A, 3.7mΩ Rds(on) at Vgs=10V
- Standard gate: Vgs(th) = 2.0-4.0V, fully enhanced at 10V (hence 12V VDD on driver)
- Qg = 150nC — switching time with 5A driver: ~30ns
- TO-220AB

### U4-U7: Current Sense — INA180A1IDBVR (SOT-23-5)

- Gain: 20V/V fixed
- Common-mode: -0.2V to +26V (handles 12V battery rail)
- Bandwidth: 35kHz at gain=20
- Offset: ±500μV max
- Supply: 2.7-5.5V (run at 3.3V)
- Output swing: 5mV to (VCC-5mV)

### U8: RS-485 — SP3485EN-L/TR (SOIC-8)

- 3.3V supply, half-duplex
- 10Mbps max — plenty for 9600-115200 baud
- DE/RE controlled by MCU GPIO
- Low power: 300μA quiescent
- ESD: ±15kV HBM on bus pins

### U9: Voltage Regulator — AMS1117-3.3 (SOT-223)

- 3.3V output, 1A max (only need ~50mA)
- Input: 4.5-12V (from vehicle battery)
- Dropout: 1.3V typical at 1A
- Input protection: reverse polarity diode + TVS

---

## Protection

### Per-Channel

| Component | Function | Placement |
|-----------|----------|-----------|
| TVS (SMBJ40A or SMBJ80A) | Flyback voltage clamp | Drain to source |
| Schottky diode (SS54) | Fast flyback freewheel | Across load connector (cathode to +12V) |
| 15V Zener | Gate overvoltage protection | Gate to source |
| 10kΩ resistor | Gate pulldown (ensures OFF at power-up) | Gate to source |
| 100nF cap | Gate driver decoupling | VDD to GND, close to UCC27524A |

### Module-Level

| Component | Function |
|-----------|----------|
| SMBJ16A TVS | Input voltage spike protection (16V standoff) |
| SS54 Schottky | Reverse polarity protection (or P-FET for low drop) |
| 100μF/25V electrolytic | Bulk input capacitor |
| 10μF/25V ceramic | Input decoupling |
| 100nF ceramics | Local decoupling on each IC VDD |
| 120Ω resistor | RS-485 bus termination (solder jumper selectable) |

---

## PCB Considerations

- **2-layer PCB** sufficient — no high-speed signals
- **MOSFET mounting**: TO-220 with sockets or soldered with heatsink pads. Keep source traces wide (high current path)
- **Current sense shunt**: 4-wire Kelvin connection to shunt resistor. Keep sense traces away from power traces
- **Ground plane**: Solid ground on bottom layer. Star ground from power MOSFETs and analog sense
- **RS-485**: Keep A/B traces as differential pair, short stubs to SP3485
- **Board size target**: ~60×80mm (fits in standard enclosure)
- **Conformal coating recommended** for engine bay environment

---

## Modbus Register Map

Same as defined in ESP-ECU ModbusManager — the module firmware implements the slave side.

### Holding Registers (Function 0x03, 18 registers)

| Register | Content | Units |
|----------|---------|-------|
| 0-3 | Ch1-4 peak current | uint16, 0.001A |
| 4-7 | Ch1-4 pulse width | uint16, 0.01ms |
| 8-11 | Ch1-4 energy per pulse | uint16, 0.1mJ |
| 12-15 | Ch1-4 fault flags | uint16, bit-packed |
| 16 | Health (high byte) + module type (low byte) | uint8+uint8 |
| 17 | Board temperature | int16, 0.1°C |

**Fault flag bits (per channel):**
- Bit 0: Overcurrent (peak > threshold)
- Bit 1: Open circuit (no current when driven)
- Bit 2: Short circuit (current without drive signal)
- Bit 3: Thermal fault (board temp > limit)

### Input Registers (Function 0x04, 4 registers, read-only)

| Register | Content |
|----------|---------|
| 0-1 | Firmware version (uint32, high word first) |
| 2-3 | Serial number (uint32, high word first) |

### Module Type Values

| Value | Type |
|-------|------|
| 0 | Coil driver (with igniter) |
| 1 | Injector driver |
| 2 | Coil driver (direct primary) |

---

## Connectors

| Connector | Pins | Function |
|-----------|------|----------|
| J1 — Load Power | 2-pin screw terminal | ISEN− (battery, post-sense), GND — via on-board PTC fuse(s) |
| J2 — Control inputs | 7-pin header | EN, CH1, CH2, CH3, CH4, +3.3V, GND |
| J3 — Outputs | 5-pin screw terminal | OUT1, OUT2, OUT3, OUT4, GND |
| J4 — RS-485 bus | 4-pin header (or RJ45) | A, B, +12V (electronics power from TPS61089), GND |
| J5 — Programming | 4-pin header | SWDIO, SWCLK, 3.3V, GND |
| J6 — UART boot | 3-pin header | TX, RX, GND (for STM32 UART bootloader) |

---

## Firmware Outline (STM32F030)

1. **ADC**: Continuous scan mode, 4 channels (PA0-PA3), DMA to buffer
2. **Current measurement**: Sample at 10kHz, track peak per pulse, compute pulse width and energy
3. **Fault detection**: Compare peak current vs threshold, detect open/short by correlating drive signal with measured current
4. **Modbus slave**: UART1 interrupt-driven, 9600 baud default, address from solder jumpers
5. **Health reporting**: Self-test on power-up (verify ADC, check shunt continuity), running health score
6. **Temperature**: Optional NTC on PA5, reported in register 17

---

## BOM Variants

| Variant | MOSFET | Shunt | TVS (per ch) | Use case |
|---------|--------|-------|--------------|----------|
| **A — Injector** | IRLZ44NPBF (55V) | 100mΩ 2512 | SMBJ40A | High-Z injectors (~1A) |
| **B — COP w/ igniter** | IRLZ44NPBF (55V) | 100mΩ 2512 | SMBJ40A | Factory COP trigger (~0.5A) |
| **C — Direct coil** | IRFB4110PBF (100V) | 10mΩ 2512 | SMBJ80A | Coil primary drive (~10A) |

Variants A and B are identical BOM. Variant C swaps 3 components per channel (MOSFET, shunt, TVS).
