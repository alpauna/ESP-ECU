# CAN Bus Analysis — ESP-ECU Integration and Module Backbone

**Date:** 2026-02-18
**Context:** Adding CAN 2.0B bus to ESP-ECU for sensor module feedback (wideband O2, EGT, knock, boost), OBD-II diagnostics, and future expansion
**References:** ISO 11898-1/2, ESP32-S3 Technical Reference (TWAI), SN65HVD230 (TI), MCP2562FD (Microchip), TJA1051 (NXP), PESD1CAN (Nexperia), SAE J2284, NXP TR1135

---

## 1. Purpose

Add a CAN bus interface to the ESP-ECU main board. The CAN bus serves as the high-speed feedback backbone for modular sensor nodes — wideband O2 controllers, exhaust gas temperature modules, knock sensors, boost controllers, and future expansion. It complements the existing RS-485 Modbus bus (which carries driver module diagnostics) with a faster, multi-master protocol suitable for real-time sensor data.

**Design goals:**
- Minimal ESP32 resource consumption (2 GPIO + 1 transceiver chip)
- Schematic-ready circuit with protection for engine bay environment
- Compatible with commercial CAN devices (Spartan 3 wideband, AEM UEGO, OBD-II tools)
- 5V I/O standard compliance — CAN transceiver handles all level translation
- Expandable to 30+ nodes without hardware changes

### 1.1 Why CAN, Not More RS-485

The ESP-ECU already has RS-485 Modbus RTU for driver module diagnostics. CAN is not a replacement — it fills a different role:

| Criterion | RS-485 Modbus RTU | CAN 2.0B |
|-----------|:-----------------:|:--------:|
| **Current use** | Driver module diagnostics | Sensor feedback (new) |
| **Topology** | Master-slave polling | Multi-master arbitration |
| **Latency** | 15-50ms (polling cycle) | 0.5-2ms (per frame) |
| **Bit rate** | 9600-115200 baud | 500kbps-1Mbps |
| **Protocol overhead** | Software (CRC, framing, polling) | Hardware (bit stuffing, CRC, ACK, arbitration) |
| **Error handling** | Software CRC-16 check | Hardware CRC-15 + ACK + error counters + bus-off recovery |
| **ESP32 peripheral** | UART (software protocol stack) | TWAI (dedicated hardware controller) |
| **Node addressing** | Explicit address (1-247) | Message-ID based (priority arbitration) |
| **Determinism** | Depends on poll order | Priority-based, worst-case bounded |
| **When critical** | Not time-critical (diagnostics only) | Moderate (sensor feedback, 20-50ms exhaust transport delay) |

**Coexistence:** Both buses operate simultaneously on independent peripherals. RS-485 uses UART1 (GPIO16/17). CAN uses TWAI (GPIO43/44). No resource conflicts.

### 1.2 Module Ecosystem — Bus Assignment

| Module | Bus | Reason |
|--------|-----|--------|
| Coil/injector driver modules | RS-485 Modbus | Control signals are direct-wired (no bus latency). Modbus carries only diagnostics. |
| Wideband O2 (lambda) | **CAN** | Primary feedback sensor — needs low latency, high update rate |
| EGT (exhaust gas temperature) | **CAN** | Per-cylinder temperature at 10-50Hz |
| Knock sensor module | **CAN** | Knock window timing-correlated data |
| Boost controller | **CAN** | Wastegate/BOV position + boost pressure |
| VR conditioner module | Direct wire | Timing-critical digital pulse train — cannot tolerate any bus latency |
| Data logger (high-speed capture) | **CAN** | Passive listener, logs all CAN traffic + internal ECU state |

---

## 2. CAN 2.0B Protocol — Key Specifications

### 2.1 Electrical Characteristics (ISO 11898-2)

CAN uses a differential signaling scheme on two wires (CAN_H and CAN_L):

```
        Dominant (logic 0)              Recessive (logic 1)
CAN_H:  3.5V (typ)                     2.5V (typ)
CAN_L:  1.5V (typ)                     2.5V (typ)
Vdiff:  +2.0V (typ, min 1.5V)          0V (typ, max 0.5V)
```

| Parameter | Value | Notes |
|-----------|-------|-------|
| Dominant differential | 1.5-3.0V (min-max) | Bus driven by transmitting node |
| Recessive differential | -120mV to +12mV | Bus floats via termination |
| Single-ended range | -2V to +7V (operating) | CAN_H and CAN_L individually |
| Bus termination | 120Ω at each end | 2 × 120Ω = 60Ω total |
| Nominal impedance | 120Ω characteristic | Twisted pair cable |
| Maximum nodes | 112 (ISO standard) | Transceiver dependent, some support 128+ |
| Maximum bus length | 40m at 1Mbps, 500m at 125kbps | Propagation delay limited |

### 2.2 Frame Format

CAN 2.0B supports standard (11-bit ID) and extended (29-bit ID) frames:

```
Standard Frame (11-bit ID):
┌─────┬──────────┬───┬───┬─────┬──────────┬───────┬─────┬───────┬─────┬───┐
│ SOF │ 11-bit ID│RTR│IDE│ r0  │ DLC(4b)  │ Data  │ CRC │  ACK  │ EOF │IFS│
│  1  │    11    │ 1 │ 1 │  1  │    4     │ 0-64  │ 16  │   2   │  7  │ 3 │
└─────┴──────────┴───┴───┴─────┴──────────┴───────┴─────┴───────┴─────┴───┘
                                            0-8 bytes
```

| Parameter | Value |
|-----------|-------|
| Data payload | 0-8 bytes per frame |
| CRC | 15-bit + delimiter |
| Bit stuffing | After 5 identical bits, 1 complement bit inserted |
| Acknowledgment | Hardware — receiving node drives ACK slot dominant |
| Error detection | CRC error, bit error, stuff error, form error, ACK error |
| Bus-off recovery | Automatic after 128 × 11 recessive bits |

### 2.3 Bit Rate and Bus Length

| Bit Rate | Max Bus Length | Bit Time | Use Case |
|----------|---------------|----------|----------|
| 1 Mbps | 40m | 1μs | Short bus, module cluster |
| 500 kbps | 100m | 2μs | **Standard automotive (recommended)** |
| 250 kbps | 250m | 4μs | OBD-II, J1939 truck networks |
| 125 kbps | 500m | 8μs | Long bus runs |

**Recommendation:** 500kbps — standard automotive bit rate, supports 100m bus length (more than enough for engine bay), compatible with virtually all commercial CAN devices and OBD-II tools.

### 2.4 Bus Utilization Calculation

At 500kbps, each standard 8-byte CAN frame takes approximately:

```
Frame bits = 1(SOF) + 11(ID) + 1(RTR) + 1(IDE) + 1(r0) + 4(DLC) + 64(data) + 15(CRC) + 1(delim) + 1(ACK) + 1(delim) + 7(EOF) + 3(IFS)
           = 111 bits (worst case with bit stuffing: ~130 bits)

Time per frame at 500kbps = 130 / 500000 = 260μs

Frames per second (theoretical max) = 1 / 260μs ≈ 3846 frames/sec
```

**ESP-ECU module traffic estimate:**

| Module | Frames/sec | Utilization |
|--------|-----------|-------------|
| 2× wideband O2 (50Hz each) | 100 | 2.6% |
| 8× EGT (10Hz each) | 80 | 2.1% |
| 4× knock (100Hz each) | 400 | 10.4% |
| 1× boost (20Hz) | 20 | 0.5% |
| 1× data logger (passive) | 0 | 0% |
| **Total** | **600** | **15.6%** |

Comfortable margin. Industry guideline is to keep CAN bus utilization below 70%.

---

## 3. ESP32-S3 TWAI Peripheral

### 3.1 Overview

The ESP32-S3 includes a dedicated TWAI (Two-Wire Automotive Interface) controller — Espressif's ISO 11898-1 compatible CAN 2.0B implementation.

| Parameter | Value | Notes |
|-----------|-------|-------|
| Controller count | 1 | Single TWAI instance |
| Protocol | ISO 11898-1 (CAN 2.0B) | Standard + extended frames |
| CAN FD support | **No** | FD frames interpreted as errors |
| Maximum bit rate | 1 Mbps | Software configurable |
| TX buffer | 1 frame (hardware) | Software TX queue in IDF driver |
| RX buffer | 1 frame (hardware) | Software RX queue configurable (default 32) |
| Acceptance filter | 1 mask filter | Single/dual filter mode |
| GPIO | Any 2 GPIO via matrix | TX and RX fully flexible |
| Interrupt sources | TX complete, RX available, error, bus-off, arbitration lost |
| Error counters | TEC and REC (automatic) | Bus-off at TEC > 255 |
| Operating modes | Normal, no-ack (self-test), listen-only | Listen-only for sniffing |

### 3.2 Filter Configuration

The TWAI peripheral has a single hardware acceptance filter that can operate in two modes:

**Single filter mode:** One 32-bit filter — matches against the full 29-bit extended ID or 11-bit standard ID + first 2 data bytes.

**Dual filter mode:** Two independent 16-bit filters — each matches against the 11-bit standard ID + RTR + first data byte.

For ESP-ECU, **single filter mode** with a mask accepting IDs 0x600-0x7FF covers all planned module traffic:
- Filter ID: `0x600` (0b 110 0000 0000)
- Filter mask: `0x1FF` (ignore lower 9 bits) → accepts `0x600-0x7FF`

Any finer filtering is done in software after the hardware filter passes the frame.

### 3.3 IDF API

The ESP-IDF TWAI driver provides a clean API:

```cpp
#include "driver/twai.h"

// Configuration
twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_43, GPIO_NUM_44, TWAI_MODE_NORMAL);
twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

// Lifecycle
twai_driver_install(&g_config, &t_config, &f_config);
twai_start();

// Transmit
twai_message_t tx_msg = { .identifier = 0x601, .data_length_code = 8, .data = {...} };
twai_transmit(&tx_msg, pdMS_TO_TICKS(10));

// Receive (blocking with timeout)
twai_message_t rx_msg;
twai_receive(&rx_msg, pdMS_TO_TICKS(100));

// Status
twai_status_info_t status;
twai_get_status_info(&status);
// status.msgs_to_rx, status.msgs_to_tx, status.tx_error_counter, status.rx_error_counter, status.state

// Cleanup
twai_stop();
twai_driver_uninstall();
```

### 3.4 GPIO Assignment

**Recommended pins: GPIO43 (TX) + GPIO44 (RX)**

These are the default UART0 (Serial) TX/RX pins. On the ESP32-S3, UART0 is used for USB-JTAG/Serial by default (via the internal USB-Serial bridge on GPIO19/20 or the native USB on GPIO19/20). GPIO43/44 are the legacy UART0 pins — **they are not used by current firmware** and are completely free.

| GPIO | TWAI Function | Board Signal | Direction |
|------|--------------|--------------|-----------|
| 43 | TWAI_TX | → SN65HVD230 TXD (pin 1) | ESP32 → Transceiver |
| 44 | TWAI_RX | ← SN65HVD230 RXD (pin 4) | Transceiver → ESP32 |

**Alternative pins (if GPIO43/44 are needed for other purposes):** Any GPIO that supports output (TX) and input (RX) works via the ESP32-S3 GPIO matrix. GPIO14 (free) and GPIO18 (free) are alternate candidates if needed.

---

## 4. CAN Transceiver Selection

### 4.1 Comparison Table

| Parameter | SN65HVD230 | MCP2562FD | TJA1051 | TJA1050 | MCP2551 | TCAN330 |
|-----------|:----------:|:---------:|:-------:|:-------:|:-------:|:-------:|
| **Manufacturer** | TI | Microchip | NXP | NXP | Microchip | TI |
| **VCC** | 3.3V | 5V (SPLIT: VIO=1.8-5.5V) | 4.75-5.25V | 4.75-5.25V | 4.5-5.5V | 3.3V |
| **Logic I/O** | 3.3V native | 1.8-5.5V (VIO pin) | 5V | 5V | 5V | 3.3V native |
| **CAN FD** | No | Yes (8Mbps) | No | No | No | No |
| **Max bitrate** | 1 Mbps | 8 Mbps (FD) / 1Mbps (classic) | 1 Mbps | 1 Mbps | 1 Mbps | 1 Mbps |
| **ESD (HBM, bus pins)** | ±16kV | ±8kV | ±8kV (AEC-Q100) | ±2kV | ±6kV | ±25kV |
| **Common-mode range** | -2V to +7V | -58V to +58V | -12V to +12V | -12V to +12V | -2V to +7V | -2V to +7V |
| **Standby current** | 370μA (Rs=HIGH) | 5μA (standby) | 15μA (silent) | None (no standby) | 1μA (standby) | 5μA (standby) |
| **Propagation delay** | ~120ns typ | ~120ns typ | ~120ns typ | ~150ns typ | ~150ns typ | ~55ns typ |
| **Package** | SOIC-8 | SOIC-8, DFN-8 | SOT-23-8, SOIC-8 | SOIC-8 | PDIP-8, SOIC-8 | SOT-23-6 |
| **AEC-Q100** | No | Yes | Yes | Yes | No | No |
| **Price (approx)** | ~$1.50 | ~$1.80 | ~$1.20 | ~$0.80 | ~$1.50 | ~$1.00 |
| **Level shift needed** | **No** | No (VIO pin) | Yes (5V logic) | Yes (5V logic) | Yes (5V logic) | **No** |

### 4.2 Recommendation: SN65HVD230DR (Primary) or MCP2562FD (Future-Proof)

**Primary choice: SN65HVD230DR (TI)**

| Reason | Detail |
|--------|--------|
| 3.3V native | Connects directly to ESP32-S3 GPIO — no level shifting required |
| Proven | Most widely used 3.3V CAN transceiver in hobbyist and industrial applications |
| Excellent ESD | ±16kV HBM on bus pins — best in class, no external TVS strictly needed (but recommended) |
| Slope control | RS pin for slope rate control: GND = high speed, 10kΩ = slope control (~40ns), 100kΩ = standby |
| Low pin count | 8-pin SOIC, simple circuit — VCC, GND, TXD, RXD, CAN_H, CAN_L, RS, Vref |
| Available | Multi-source (TI, on-module boards), well-stocked at all distributors |

**Future-proof alternative: MCP2562FD-E/MF (Microchip)**

| Reason | Detail |
|--------|--------|
| CAN FD ready | Supports 8Mbps data phase — future-proofs if ESP32 successor has CAN FD |
| Split supply | VIO pin accepts 1.8-5.5V for logic I/O — works with 3.3V ESP32 from 5V bus supply |
| AEC-Q100 | Automotive qualified |
| ±58V bus fault | Survives ±58V on CAN_H/CAN_L — extreme protection against miswiring |

**Note on 5V I/O standard:** The CAN transceiver inherently provides the voltage translation between the 5V-domain CAN bus and the 3.3V ESP32 GPIO. For the SN65HVD230, it operates entirely at 3.3V — the CAN_H/CAN_L bus is a differential signal (not 5V CMOS), so the 5V I/O standard doesn't apply to CAN bus wiring. The transceiver is the boundary.

**Also notable: TCAN330 (TI)**

| Reason | Detail |
|--------|--------|
| Best ESD | ±25kV HBM on bus pins — highest in class, no external TVS strictly needed |
| Tiny package | SOT-23-6 — smallest CAN transceiver available, saves board space |
| 3.3V native | Direct 3.3V logic, no level shifting |
| Dominant timeout | Built-in DTO prevents bus lockup if transmitter gets stuck dominant |
| CAN FD partial | TCAN330G variant adds CAN FD loop delay symmetry guarantees |

The TCAN330 is an excellent alternative to the SN65HVD230 if board space is constrained (SOT-23-6 vs SOIC-8). It lacks the RS slope control pin (fixed high-speed mode) and the Vref output, but neither is critical for this application.

### 4.3 Why Not TJA1050/TJA1051

The standard NXP TJA1050/TJA1051 require 5V logic I/O — the ESP32-S3's 3.3V GPIO cannot drive TXD directly without a level shifter. However, NXP offers a **TJA1051T/3 variant** with 3.0-3.6V logic I/O that connects directly to the ESP32-S3. This part is AEC-Q100 qualified with ±8kV ESD and CAN FD timing support at 5Mbps. It's a viable alternative if NXP sourcing or automotive qualification is preferred, though the SN65HVD230's ±16kV ESD and the TCAN330's ±25kV ESD still provide better bus pin protection.

---

## 5. Schematic — ESP-ECU CAN Interface

### 5.1 Circuit Diagram

```
ESP32-S3                          SN65HVD230 (SOIC-8)                  CAN Bus
                           ┌────────────────────────────┐
GPIO43 (CAN_TX) ──────────▶│ 1 TXD            VCC 8 │◀── 3.3V (via 100nF to GND)
                            │                          │
GPIO44 (CAN_RX) ◀──────────│ 4 RXD          CAN_H 7 │──┬── 22Ω ──┬──▶ CAN_H (to connector)
                            │                          │  │         │
                       ┌───▶│ 5 Vref         CAN_L 6 │──┼── 22Ω ──┼──▶ CAN_L (to connector)
                       │    │                          │  │         │
               10kΩ ──┤    │ 8 RS            GND  3 │──┼─────────┤
               to GND │    │                          │  │         │
                       │    └────────────────────────────┘  │         │
                       │                                    │         │
                       │    (Mode: RS to GND = high speed)  │         │
                       │                                    │         │
                       │    Protection:                     │         │
                       │    ┌───────────────────────────────┘         │
                       │    │                                         │
                       │    ├── PESD1CAN (CAN_H → CAN_L, bidirectional TVS)
                       │    │                                         │
                       │    ├── C_split: 2×60Ω + 4.7nF to GND (split termination)
                       │    │   ┌──60Ω──┐                            │
                       │    │   │       ├── 4.7nF ── GND             │
                       │    │   └──60Ω──┘                            │
                       │    │   (CAN_H)  (CAN_L)                     │
                       │    │                                         │
                       │    └── 120Ω termination (solder jumper selectable)
                       │        CAN_H ──[SJ1]── 120Ω ──[SJ1]── CAN_L
                       │
                       └── Vref: 0.5×VCC reference output (2.5V) — connect 100nF to GND
```

### 5.2 Split Termination

Standard CAN termination is a single 120Ω resistor between CAN_H and CAN_L at each end of the bus. **Split termination** replaces this with 2×60Ω + a capacitor to ground:

```
Standard:     CAN_H ──── 120Ω ──── CAN_L

Split:        CAN_H ── 60Ω ──┬── 60Ω ── CAN_L
                              │
                            4.7nF
                              │
                             GND
```

**Benefits of split termination:**
- Creates a common-mode filter — the 4.7nF cap shunts common-mode noise to ground
- Reduces EMI emissions by 10-20dB in the 1-100MHz range
- Same DC termination (60Ω + 60Ω = 120Ω) — no effect on differential signaling
- Required by some automotive EMC standards (ISO 11452-4)

**Recommendation:** Use split termination on the ESP-ECU board. Include a solder jumper to enable/disable (not all bus configurations need termination at the ECU — only the two physical endpoints of the bus).

### 5.3 Bus Series Resistors

The 22Ω series resistors between the transceiver CAN_H/CAN_L pins and the bus connector serve two purposes:
- **EMI reduction**: Slow down switching edges slightly, reducing high-frequency emissions
- **ESD current limiting**: Additional protection for transceiver bus pins during ESD events

Some designs omit these for maximum speed. At 500kbps they have negligible effect on signal integrity. Include them for engine bay robustness.

### 5.4 Component Values

| Component | Value | Package | Purpose |
|-----------|-------|---------|---------|
| U_CAN | SN65HVD230DR | SOIC-8 | CAN transceiver |
| C_CAN1 | 100nF X7R | 0402 | VCC decoupling (close to pin 8) |
| C_CAN2 | 100nF X7R | 0402 | Vref decoupling (close to pin 5) |
| R_RS | 10kΩ | 0402 | RS pin to GND (high-speed mode) |
| R_CANH | 22Ω | 0402 | CAN_H series (EMI/ESD) |
| R_CANL | 22Ω | 0402 | CAN_L series (EMI/ESD) |
| R_TERM_H | 60Ω 1% | 0402 | Split termination (CAN_H side) |
| R_TERM_L | 60Ω 1% | 0402 | Split termination (CAN_L side) |
| C_TERM | 4.7nF C0G | 0402 | Split termination common-mode filter |
| SJ_TERM | Solder jumper | — | Enable/disable termination |
| D_CAN | PESD1CAN | SOT-23 | CAN bus TVS (bidirectional) |

### 5.5 RS Pin Modes (SN65HVD230)

The RS (slope control) pin selects the transceiver operating mode:

| RS Connection | Mode | Slope Rate | Use Case |
|--------------|------|------------|----------|
| GND (direct) | High speed | ~25ns | 500kbps-1Mbps, short bus |
| 10kΩ to GND | Slope control | ~40ns | Reduced EMI, recommended default |
| 100kΩ to GND | Low-power standby | — | Receiver active, transmitter off |
| VCC | Shutdown | — | <10μA, bus released |

**Recommendation:** 10kΩ to GND (slope control) for engine bay EMI. At 500kbps, the ~40ns slope rate is more than adequate (bit time = 2μs, so 40ns slope is 2% of bit time).

---

## 6. Protection

### 6.1 TVS Diode — PESD1CAN (Nexperia)

The PESD1CAN is a purpose-built bidirectional ESD protection diode specifically designed for CAN bus applications.

| Parameter | Value | Notes |
|-----------|-------|-------|
| Configuration | CAN_H to CAN_L (bidirectional) | Single SOT-23 package protects both lines |
| Working voltage | -24V to +24V differential | Well above CAN ±12V common mode |
| Clamping voltage | ±40V at 1A | Limits transient peaks |
| ESD rating | ±30kV (contact), ±30kV (air) | IEC 61000-4-2 |
| Capacitance | 5pF (typical) | Negligible at 500kbps |
| Leakage | <100nA | No effect on bus idle state |

**Alternatives:**

| Part | Manufacturer | ESD (IEC) | Capacitance | Notes |
|------|-------------|-----------|-------------|-------|
| PESD1CAN | Nexperia | ±23kV | 12pF max | **Recommended** — lowest capacitance |
| CDSOT23-T24CAN | Bourns | ±30kV | 22pF | Highest ESD, AEC-Q101 variant available (-Q suffix) |
| NUP2105L | onsemi | ±15kV | ~50pF | Highest surge power (350W), but high capacitance |

For classic CAN at 500kbps, all three are suitable. The PESD1CAN is preferred for lowest capacitance (best signal integrity). If AEC-Q101 automotive qualification is required, use the CDSOT23-T24CAN-Q.

**Placement:** As close to the CAN bus connector as possible. Before the 22Ω series resistors (between connector and resistors).

### 6.2 Common-Mode Choke (Optional)

For extreme EMI environments, a common-mode choke can be added between the transceiver and connector:

| Part | Value | Package | Notes |
|------|-------|---------|-------|
| DLW21HN900SQ2 (Murata) | 90Ω @ 100MHz | 0805 | Dual-line common-mode filter |
| ACM2012-900-2P (TDK) | 90Ω @ 100MHz | 0805 | Alternative |

**Recommendation:** Omit for initial design. The SN65HVD230's ±16kV ESD + PESD1CAN TVS + split termination + slope control provides excellent protection. Add common-mode choke in Rev 2 if EMC testing reveals issues.

### 6.3 Connector ESD Path

```
External CAN bus
    │
    ▼
[Connector J_CAN]
    │
    ├── PESD1CAN (TVS, CAN_H↔CAN_L)
    │
    ├── 22Ω series (R_CANH)──▶ SN65HVD230 CAN_H
    │
    └── 22Ω series (R_CANL)──▶ SN65HVD230 CAN_L
```

ESD energy path: Connector → TVS clamps differential and shunts to GND → Series resistors limit residual current → Transceiver's internal ±16kV ESD clamps absorb remainder.

---

## 7. Connector and Wiring

### 7.1 CAN Bus Connector on ESP-ECU

**Connector: 4-pin header (2.54mm) or 4-pin Molex MX150**

| Pin | Signal | Color (SAE convention) | Notes |
|-----|--------|----------------------|-------|
| 1 | CAN_H | Yellow-Green | High line |
| 2 | CAN_L | Yellow-Orange | Low line |
| 3 | +12V | Red | Power for remote CAN modules |
| 4 | GND | Black | Signal and power ground |

**Why include +12V and GND:** Remote CAN modules (wideband O2, EGT) need power. Rather than requiring separate power wiring, the CAN connector carries 12V + GND alongside the signal pair. This matches the RS-485 connector pattern on the driver modules (J4: A, B, +12V, GND).

**12V supply sourcing:** Same as the RS-485 bus — from the TPS61089 boost converter's 12V electronics rail. Protected by a PTC fuse on the ESP-ECU board.

### 7.2 Cable Specification

| Parameter | Value | Standard |
|-----------|-------|----------|
| Wire gauge | 22 AWG stranded | SAE J2284 |
| Twist rate | 33-50 twists/meter (25±5mm pitch) | SAE J2284 |
| Characteristic impedance | 120Ω ±10% | ISO 11898-2 |
| Shielding | Braided shield (optional, ground at one end only) | SAE J1939/11 |
| Insulation | Automotive grade (125°C rated minimum) | SAE J1128 |
| Maximum stub length | 300mm at 500kbps | ISO 11898-2 |

**For engine bay:** Use shielded 22 AWG twisted pair. Ground shield at ECU end only (prevent ground loops). Maximum total bus length: 100m at 500kbps.

### 7.3 Bus Topology

```
                          ┌─── 120Ω termination (split)
                          │
ESP-ECU ─────────────┬────┤◀── CAN bus backbone (twisted pair)
(CAN connector)      │    │
                     │    ├── Stub: Wideband O2 Module #1 (<300mm)
                     │    │
                     │    ├── Stub: Wideband O2 Module #2 (<300mm)
                     │    │
                     │    ├── Stub: EGT Module (<300mm)
                     │    │
                     │    ├── Stub: Knock Module (<300mm)
                     │    │
                     │    └── 120Ω termination (at far end of bus)
                     │
                     └── (optional: OBD-II diagnostic port tee)
```

**Termination:** 120Ω at each physical end of the bus. The ESP-ECU is typically one end (hence the on-board split termination with solder jumper). The furthest module provides the other termination.

### 7.4 OBD-II Compatibility

If the CAN bus is exposed to an OBD-II diagnostic port (standard on all vehicles since 2008), use pins 6 (CAN_H) and 14 (CAN_L) per SAE J1962:

| OBD-II Pin | Signal | Notes |
|------------|--------|-------|
| 6 | CAN_H | ISO 15765-4 |
| 14 | CAN_L | ISO 15765-4 |
| 4 | Chassis GND | |
| 5 | Signal GND | |
| 16 | Battery +12V | Always on |

**Caution:** If connecting to a vehicle's existing OBD-II CAN bus, the ESP-ECU would need to use **listen-only mode** to avoid bus contention with the vehicle's factory ECU. For a standalone engine (no factory ECU), the ESP-ECU owns the bus.

---

## 8. CAN Message ID Allocation

### 8.1 ID Range Plan

CAN 2.0B standard frame IDs are 11 bits (0x000-0x7FF). Lower IDs have higher priority (win arbitration). Reserve lower IDs for time-critical data:

| ID Range | Priority | Module Type | Content |
|----------|----------|-------------|---------|
| 0x000-0x0FF | Highest | Reserved | OBD-II standard PIDs (if OBD connected) |
| 0x100-0x1FF | High | ESP-ECU outbound | RPM, MAP, TPS broadcast for modules |
| 0x200-0x2FF | High | ESP-ECU commands | Module configuration, enable/disable |
| 0x300-0x3FF | Medium | — | Reserved for future |
| 0x400-0x4FF | Medium | Knock modules | Knock intensity (time-critical) |
| 0x500-0x5FF | Medium | Boost controller | Boost pressure, wastegate position |
| 0x600-0x63F | Normal | Wideband O2 | Lambda, AFR, health (per WidebandO2Analysis.md) |
| 0x640-0x67F | Normal | EGT modules | Per-cylinder exhaust gas temperature |
| 0x680-0x6BF | Normal | — | Reserved for future sensor modules |
| 0x6C0-0x6FF | Normal | — | Reserved |
| 0x700-0x73F | Low | Data logger | Capture triggers, status |
| 0x740-0x77F | Low | Diagnostics | Module firmware version, serial, self-test |
| 0x780-0x7FF | Lowest | Heartbeat | Module alive/health beacons |

### 8.2 Wideband O2 Frame Format (from WidebandO2Analysis.md)

| CAN ID | Byte 0-1 | Byte 2-3 | Byte 4-5 | Byte 6 | Byte 7 |
|--------|----------|----------|----------|--------|--------|
| 0x600 + node | Lambda × 10000 | AFR × 1000 | Nernst temp (°C) | Heater state | Health % |

Node 0 = 0x600, Node 1 = 0x601, etc. Update rate: 50Hz.

### 8.3 ESP-ECU Broadcast Frame (Optional)

The ESP-ECU can periodically broadcast engine state for modules that need context (e.g., knock module needs RPM to window correctly):

| CAN ID | Byte 0-1 | Byte 2-3 | Byte 4 | Byte 5 | Byte 6 | Byte 7 |
|--------|----------|----------|--------|--------|--------|--------|
| 0x100 | RPM | MAP × 10 | TPS % | CLT °C + 40 | IAT °C + 40 | Flags |

Broadcast rate: 50Hz (20ms). Only transmitted if `canEnabled` and at least one module needs it.

---

## 9. PCB Layout

### 9.1 Placement

Place the SN65HVD230 in the **digital zone** of the ESP-ECU 6-layer board, near the CAN connector. Keep the analog path short:

```
ESP32-S3 ─── [GPIO43/44 traces, ~20mm max] ─── SN65HVD230 ─── [<10mm] ─── J_CAN connector
                                                     │
                                                   PESD1CAN (at connector)
                                                   R_TERM (at transceiver)
```

### 9.2 Differential Pair Routing

Route CAN_H and CAN_L as a **100Ω differential pair** on L1 (top signal layer) with L2 (solid ground) as reference:

| Parameter | Value | Notes |
|-----------|-------|-------|
| Trace width | 5 mil (0.127mm) | For 100Ω differential on 4-5 mil prepreg |
| Trace spacing | 5 mil (0.127mm) | Edge-coupled microstrip |
| Reference layer | L2 (GND plane) | Solid, unbroken under pair |
| Length matching | ±50 mil (1.27mm) | Not critical at 500kbps, but good practice |
| Keep-out | 3× trace width from other signals | Minimize crosstalk |

> **Cross-reference:** See [BoardDesignAnalysis.md](BoardDesignAnalysis.md) Section 2.3 for controlled impedance targets. CAN uses the same 100Ω differential spec as RS-485.

### 9.3 Ground Connections

- SN65HVD230 GND (pin 3): Connect to L2 ground plane via **2 vias** minimum (low impedance return path)
- PESD1CAN GND: Via to L2 as close to pad as possible
- CAN connector GND: Wide trace to L2 with multiple vias
- Split termination capacitor GND: Via to L2 near capacitor

### 9.4 Decoupling

- 100nF (C_CAN1) on VCC: Within 3mm of SN65HVD230 pin 8, via to L2 GND
- 100nF (C_CAN2) on Vref: Within 3mm of pin 5, via to L2 GND

---

## 10. BOM — CAN Interface Components

### 10.1 ESP-ECU Main Board (CAN Interface Section)

| Item | Qty | Reference | Value | Package | Part Number | Manufacturer | Description |
|------|-----|-----------|-------|---------|-------------|--------------|-------------|
| 1 | 1 | U_CAN | SN65HVD230 | SOIC-8 | SN65HVD230DR | TI | 3.3V CAN transceiver, ±16kV ESD |
| 2 | 1 | D_CAN | PESD1CAN | SOT-23 | PESD1CAN,215 | Nexperia | Bidirectional CAN TVS, ±30kV ESD |
| 3 | 2 | C_CAN1, C_CAN2 | 100nF X7R | 0402 | CL05B104KO5NNNC | Samsung | VCC and Vref decoupling |
| 4 | 1 | R_RS | 10kΩ | 0402 | 0402WGF1002TCE | UNI-ROYAL | RS pin slope control (10kΩ mode) |
| 5 | 2 | R_CANH, R_CANL | 22Ω | 0402 | 0402WGF220JTCE | UNI-ROYAL | Bus series resistors (EMI/ESD) |
| 6 | 2 | R_TERM_H, R_TERM_L | 60Ω 1% | 0402 | 0402WGF600KTCE | UNI-ROYAL | Split termination resistors |
| 7 | 1 | C_TERM | 4.7nF C0G | 0402 | CL05C472JB5NNNC | Samsung | Split termination common-mode cap |
| 8 | 1 | SJ_TERM | Solder jumper | — | — | — | Termination enable/disable |
| 9 | 1 | J_CAN | 4-pin header | 2.54mm | Header 1x4P | Generic | CAN_H, CAN_L, +12V, GND |

**Total component cost: ~$3.50**

### 10.2 Per-Module CAN Interface (For Wideband O2, EGT, etc.)

Each remote CAN module needs a matching CAN interface. For STM32-based modules with built-in CAN controller:

| Item | Qty | Value | Package | Part Number | Description |
|------|-----|-------|---------|-------------|-------------|
| 1 | 1 | SN65HVD230 | SOIC-8 | SN65HVD230DR | CAN transceiver (if module MCU is 3.3V) |
| 2 | 1 | PESD1CAN | SOT-23 | PESD1CAN,215 | Bus TVS protection |
| 3 | 1 | 100nF | 0402 | CL05B104KO5NNNC | Decoupling |
| 4 | 1 | 120Ω | 0402 | 0402WGF1200TCE | Termination (solder jumper, last node only) |
| 5 | 1 | 4-pin header | 2.54mm | Header 1x4P | CAN connector |

**Per-module CAN interface cost: ~$2.50**

---

## 11. Integration with Existing ESP-ECU

### 11.1 Config Fields (ProjectInfo)

New fields to add alongside existing `modbusEnabled` / `pinModbusTx` / `pinModbusRx` pattern:

```
canEnabled          bool        default: false
pinCanTx            uint8_t     default: 43
pinCanRx            uint8_t     default: 44
canBitrate          uint32_t    default: 500000
```

### 11.2 Software Manager Pattern

Follow the ModbusManager pattern — a `CANManager` class with:
- `begin(txPin, rxPin, bitrate)` — install and start TWAI driver
- `poll()` — called from 100ms task, reads all queued RX frames
- `transmit(id, data, len)` — queue a TX frame
- `getStatus()` — bus state, error counters, RX/TX counts
- Fault callback for MQTT integration
- Registered CAN frame handlers (by ID range) for specific module types

### 11.3 Lambda Source Priority

With CAN bus added, the lambda source priority chain becomes:

1. CAN wideband online → use CAN lambda (highest priority, lowest latency)
2. CAN timeout (500ms no frame) → fall back to on-board CJ125 if present
3. CJ125 not ready → fall back to narrowband 0-5V analog
4. No sensor → open-loop (VE table only)

---

## 12. Open Items

1. **GPIO43/44 vs GPIO14/18** — Confirm GPIO43/44 don't conflict with USB-JTAG debug on development boards. On production boards (no USB debug), they're guaranteed free. If conflict exists, use GPIO14 (TX) + GPIO18 (RX) as alternates.

2. **CAN bus power budget** — Calculate maximum current draw of all CAN-powered modules on the shared +12V rail. May need a dedicated PTC fuse for the CAN bus 12V supply separate from the RS-485 bus.

3. **CAN FD future** — The ESP32-S3 TWAI is CAN 2.0B only (no CAN FD). If CAN FD is needed in the future (e.g., for high-resolution data), a successor ESP32 variant or an external CAN FD controller (MCP2518FD) would be required. For now, CAN 2.0B at 500kbps is more than sufficient.

4. **OBD-II PID responses** — If the ESP-ECU is connected to an OBD-II port, decide whether to respond to standard OBD-II diagnostic requests (Mode 01 PIDs: RPM, coolant temp, MAF, etc.). This enables standard scan tools to read ECU data.

5. **CAN bus wake-up** — The SN65HVD230 supports a standby mode (RS = 100kΩ) where the receiver stays active but the transmitter is off. Consider whether the ESP-ECU should wake from light sleep on CAN bus activity.

6. **Shared vs separate connectors** — Consider whether CAN and RS-485 should share a single multi-pin connector or remain separate. Separate connectors are simpler for initial design; combined connectors reduce wiring but increase connector cost.

7. **Transceiver quantity** — Only 1 CAN transceiver is needed for the ESP32-S3 (single TWAI controller). If a second independent CAN bus is needed in the future (e.g., isolated OBD-II + module bus), an external CAN controller + transceiver (MCP2515 + SN65HVD230) can be added on SPI.

8. **Cross-reference to schematics** — When the CAN interface is added to the ESP-ECU schematic, update the power supply schematic to show the CAN bus 12V feed and PTC fuse.
