# Design: Replace MAX31865 Bare IC with Adafruit Breakout Connector

**Date:** 2026-03-20
**Status:** Approved

---

## Background

The SaunaStatus PCB currently includes the MAX31865 resistance-to-digital converter (U2) as a bare 20-pin SSOP IC, wired directly on-board. The stove sensor's analog connections (REFIN+/-, FORCE+/-, RTDIN+/-) route through a dedicated 6-pin connector (J1, `PT1000-Connector`).

The Adafruit MAX31865 breakout board (product #3328) integrates the MAX31865 IC, a 4300Ω reference resistor, decoupling capacitors, and a screw terminal for the RTD sensor onto a single module. Replacing the bare IC with this breakout board eliminates the need to source and solder the SSOP-20 IC and supporting passives, and provides a pre-tested, compact module.

---

## Scope

- **Remove** U2 (`Sensor_Temperature:MAX31865xAP`, 20-pin SSOP) from the schematic and PCB.
- **Remove** J1 (`Connector_Generic:Conn_01x06`, 6-pin PT1000 connector) from the schematic and PCB.
- **Add** a new J1 (`Connector_Generic:Conn_01x08`, 8-pin header) representing the Adafruit breakout's pin header.
- Fix all ERC and DRC violations resulting from the change.
- Verify all native unit tests continue to pass with no regressions.

**Out of scope:** Adding the Adafruit Eagle/KiCad source files to the SaunaStatus project.

---

## Source Information

The Adafruit MAX31865 breakout board exposes an 8-pin 0.1" (2.54mm) header with the following pinout (left to right, component side up):

| Pin | Label | Direction | Description |
|-----|-------|-----------|-------------|
| 1 | VIN | Input | Power supply, 3.3–5V |
| 2 | 3Vo | Output | 3.3V regulated output from on-board regulator |
| 3 | GND | Ground | Common ground |
| 4 | CLK | Input | SPI clock |
| 5 | SDO | Output | SPI MISO (chip → host) |
| 6 | SDI | Input | SPI MOSI (host → chip) |
| 7 | CS | Input | SPI chip select, active low |
| 8 | RDY | Output | Data ready, active low (~DRDY) |

Source: Extracted from `Adafruit MAX31865-eagle-import.kicad_sym` (HEADER-1X8 and TEMP_MAX31865 symbols from the Adafruit Eagle import, archived in `docs/kicad/Adafruit MAX31865-backups/Adafruit MAX31865-2026-03-20_172613.zip`).

### Hardware prerequisite — 3-wire solder jumper

The firmware initializes the MAX31865 in **3-wire mode** (`stove_thermo.begin(MAX31865_3WIRE)` in `main.cpp`). The Adafruit MAX31865 breakout (#3328) ships configured for **2-wire/4-wire** by default. Before installation, the on-board solder jumper labeled **"2/3 Wire"** must be bridged to select 3-wire mode. Failure to do so will cause the chip to report RTD fault errors. See [Adafruit MAX31865 product documentation](https://learn.adafruit.com/adafruit-max31865-rtd-pt100-amplifier) for the jumper location.

### RREF compatibility

The firmware defines `RREF 4300.0` (Ω). The Adafruit MAX31865 breakout (#3328) includes a 4300Ω on-board reference resistor. These values are consistent — no firmware change is required. The SaunaStatus board supplies 3.3V on the `+3V3` rail; connecting J1 pin 1 (VIN) to `+3V3` is correct. At 3.3V the Adafruit regulator is bypassed and pin 2 (3Vo) tracks VIN; the `no_connect` on 3Vo is appropriate in all cases.

---

## Schematic Changes

### Remove

- **U2** — `Sensor_Temperature:MAX31865xAP` at position `(149.86, 88.90)` mm. No passive support components present (wired bare).
- **J1** — `Connector_Generic:Conn_01x06` at position `(231.14, 85.09)` mm, value `PT1000-Connector`.
- All wires and net labels associated exclusively with the removed components: `/REFIN+`, `/REFIN-`, `/FORCE+`, `/FORCE-`, `/RTDIN+`, `/RTDIN-`.

### Add

New **J1** connector:
- Symbol: `Connector_Generic:Conn_01x08`
- Value: `Adafruit-MAX31865-Breakout`
- Footprint: `Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Vertical`
- Net connections:

| Pin | Net | Note |
|-----|-----|------|
| 1 (VIN) | `+3V3` | Powers the Adafruit regulator |
| 2 (3Vo) | no_connect | Output from breakout, unused on host board |
| 3 (GND) | `GND` | |
| 4 (CLK) | `/SPI_SCK` | ESP32 GPIO18 |
| 5 (SDO) | `/SPI_MISO` | ESP32 GPIO19 |
| 6 (SDI) | `/SPI_MOSI` | ESP32 GPIO23 |
| 7 (CS) | `/SPI_CS` | ESP32 GPIO5 |
| 8 (RDY) | no_connect | DRDY unused in firmware (polled SPI) |

### Preserved nets

The following nets are preserved unchanged; only their U2 endpoints move to J1:

| Net | From | To |
|-----|------|----|
| `/SPI_SCK` | ESP32 U1 | J1 pin 4 |
| `/SPI_MISO` | ESP32 U1 | J1 pin 5 |
| `/SPI_MOSI` | ESP32 U1 | J1 pin 6 |
| `/SPI_CS` | ESP32 U1 | J1 pin 7 |
| `+3V3` | Power net | J1 pin 1 |
| `GND` | Power net | J1 pin 3 |

---

## PCB Changes

- Remove U2 footprint (`Package_SO:SSOP-20_5.3x7.2mm_P0.65mm`) from the board.
- Replace J1 footprint from `PinHeader_1x06_P2.54mm_Vertical` to `PinHeader_1x08_P2.54mm_Vertical`.
- Re-route any affected traces; run DRC and resolve all violations.

---

## ERC/DRC Considerations

After the schematic change:
- No unconnected mandatory pins should remain (all pins connected or explicitly no_connect).
- `Conn_01x08` passive pins do not generate ERC errors for no_connect markers.
- PCB DRC: the 8-pin footprint must fit within the board outline; adjust placement if needed.

---

## Testing

### Existing tests (must continue to pass)

- All native unit tests (`pio test -e native`) must pass with no regressions. These do not touch KiCad files. Suites include: sensor, config, websocket, auth, and OTA.

### New test

No new C++ unit test is required. This is a schematic-only change; the SPI GPIO constants in `main.cpp` (CS=5, SCK=18, MISO=19, MOSI=23) are unchanged. Implementation verification: grep `main.cpp` to confirm the GPIO values still match the schematic net annotations before closing the task.

---

## Acceptance Criteria

1. U2 (`MAX31865xAP`) is absent from schematic and PCB.
2. Old J1 (6-pin) is absent from schematic and PCB.
3. New J1 (8-pin `Conn_01x08`) is present in schematic with the pin-to-net mapping above.
4. No dangling net labels remain from removed components (`/REFIN+`, `/REFIN-`, `/FORCE+`, `/FORCE-`, `/RTDIN+`, `/RTDIN-`).
5. ERC reports zero errors.
6. DRC reports zero errors (or only pre-existing informational items).
7. `pio test -e native` passes all native tests with no regressions.
8. No firmware changes required (SPI GPIOs CS=5, SCK=18, MISO=19, MOSI=23 unchanged).
9. Hardware prerequisite documented: Adafruit breakout 3-wire solder jumper must be set before installation.
