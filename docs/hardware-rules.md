# SaunaStatus Hardware Design Reference

Derived from `src/main.cpp`, `docs/pinout.md`, `docs/kicad/SaunaStatus.kicad_sch`,
`docs/kicad/report.txt`, and `docs/kicad/SCHEMATIC_GEN_RULES.md`.

---

## 1. Pin Assignment Table

Every GPIO currently allocated. Do not assign these to any other function.

| GPIO | Net / Label          | Direction | Connected To                       | Constraint                         |
|------|----------------------|-----------|------------------------------------|------------------------------------|
| 4    | SDA (I2C)            | Bidirec.  | INA260 SDA                         | Non-default I2C bus; call `Wire.begin(4, 13)` |
| 5    | SPI_CS               | Output    | MAX31865 CS                        | VSPI CS; hardware SPI              |
| 13   | SCL (I2C)            | Output    | INA260 SCL                         | Non-default I2C bus; call `Wire.begin(4, 13)` |
| 14   | OUTFLOW_I4           | Output    | ULN2003 (U5) IN4 → outflow stepper | Boot: outputs PWM; no conflict here |
| 16   | DHT_CEILING          | Input     | DHT21/AM2301 ceiling DATA          | 10 kΩ pull-up to 3.3 V required    |
| 17   | DHT_BENCH            | Input     | DHT21/AM2301 bench DATA            | 10 kΩ pull-up to 3.3 V required    |
| 18   | SPI_SCK              | Output    | MAX31865 SCK                       | VSPI SCK — reserved, do not share  |
| 19   | SPI_MISO             | Input     | MAX31865 SDO                       | VSPI MISO — reserved, do not share |
| 21   | OUTFLOW_I1           | Output    | ULN2003 (U5) IN1 → outflow stepper |                                    |
| 22   | INFLOW_I1            | Output    | ULN2003 (U6) IN1 → inflow stepper  |                                    |
| 23   | SPI_MOSI             | Output    | MAX31865 SDI                       | VSPI MOSI — reserved, do not share |
| 25   | OUTFLOW_I2           | Output    | ULN2003 (U5) IN2 → outflow stepper |                                    |
| 26   | OUTFLOW_I3           | Output    | ULN2003 (U5) IN3 → outflow stepper |                                    |
| 27   | INFLOW_I2            | Output    | ULN2003 (U6) IN2 → inflow stepper  |                                    |
| 32   | INFLOW_I3            | Output    | ULN2003 (U6) IN3 → inflow stepper  |                                    |
| 33   | INFLOW_I4            | Output    | ULN2003 (U6) IN4 → inflow stepper  |                                    |

---

## 2. Reserved Pins — Must Not Be Reused

### VSPI Bus (MAX31865)

| GPIO | VSPI Function | Net Label  |
|------|---------------|------------|
| 18   | SCK           | SPI_SCK    |
| 19   | MISO          | SPI_MISO   |
| 23   | MOSI          | SPI_MOSI   |
| 5    | CS            | SPI_CS     |

These four pins constitute the complete VSPI bus. Adding another SPI device requires a dedicated CS pin on a free GPIO; the SCK/MISO/MOSI lines can be shared.

### Flash-Connected Pins — Absolutely Off-Limits

GPIO 6, 7, 8, 9, 10, 11 are connected to the internal SPI flash on the ESP32-WROOM-32 module. Driving or reading these pins will corrupt flash and crash the device. They are not broken out on the esp32doit-devkit-v1/WROOM-32 header.

The `SHD/SD2` (pad 17) and `SWP/SD3` (pad 18) pins visible in the KiCad schematic are flash bus lines on the module; both are marked no-connect in `report.txt` and must stay unconnected.

### Input-Only Pins

GPIO 34, 35, 36 (SENSOR_VP), 39 (SENSOR_VN) are input-only with no internal pull-up or pull-down. They cannot be used as outputs. Both SENSOR_VP (pad 4) and SENSOR_VN (pad 5) are unconnected in the current schematic.

### Strapping Pins — Handle With Care

| GPIO | Strapping Function                              | Current Use  |
|------|-------------------------------------------------|--------------|
| 0    | Boot mode (must be HIGH for normal boot)        | Not assigned |
| 2    | Boot mode (must be LOW or floating for download)| Not assigned |
| 5    | VSPI CS / strapping pin (HIGH at boot = SDIO)   | MAX31865 CS  |
| 12   | Flash voltage select (LOW = 3.3 V, HIGH = 1.8 V)| Not assigned |
| 15   | SDIO slave / U0TXD log silencer                 | Not assigned |

GPIO 5 is both a strapping pin and the MAX31865 CS line. The MAX31865 CS is active-low; the line idles HIGH at boot, which is the correct strapping state (SDIO timing). No conflict exists, but this must be preserved in any revision — do not use GPIO 5 for a device that pulls it low at power-on.

GPIO 12 must be LOW or floating at boot to select 3.3 V flash. The ESP32-WROOM-32 module already handles this internally; the pin appears as a no-connect in `report.txt`.

---

## 3. I2C Bus

Non-default I2C assignment; the firmware calls `Wire.begin(INA260_SDA, INA260_SCL)` to override the hardware defaults.

| Signal | GPIO | INA260 Pin |
|--------|------|------------|
| SDA    | 4    | SDA        |
| SCL    | 13   | SCL        |

**Device address:** INA260 default I2C address is 0x40 (A0 and A1 tied to GND). The firmware uses `Adafruit_INA260 ina260` with no address argument, confirming 0x40.

**Pull-ups:** I2C lines require pull-up resistors to 3.3 V. Typical value 4.7 kΩ. If using the Adafruit INA260 breakout, pull-ups are on the breakout board.

**Note:** The INA260 (U7) is present in the KiCad schematic (SaunaStatus.kicad_sch) but is not yet wired. The I2C connections (SDA=GPIO4, SCL=GPIO13) and address pins (A0=GND, A1=GND) must be routed before the next PCB revision. The `ina260_ok` runtime flag guards all INA260 reads, so the firmware degrades gracefully if the device is absent.

---

## 4. SPI Bus

Hardware VSPI peripheral. The MAX31865 is the only device on this bus.

| Signal | GPIO | MAX31865 Pin |
|--------|------|--------------|
| CS     | 5    | CS (active LOW) |
| SCK    | 18   | SCK          |
| MISO   | 19   | SDO          |
| MOSI   | 23   | SDI          |

Instantiation: `Adafruit_MAX31865 stove_thermo = Adafruit_MAX31865(5)` — hardware SPI, CS=GPIO5.

Configuration: 3-wire mode. The solder jumper on the MAX31865 breakout board **must** be set to 3-wire. Incorrect jumper setting produces plausible but wrong temperature readings.

---

## 5. Stepper Motor Wiring

Both motors are 28BYJ-48 (unipolar, 5 V, geared). Each is driven by a ULN2003A Darlington array (U5 = outflow, U6 = inflow).

### Outflow Stepper — Upper Vent (U5 / ULN2003A)

| ULN2003 Input | GPIO | Net Label   | 28BYJ-48 coil |
|---------------|------|-------------|---------------|
| IN1           | 21   | OUTFLOW_I1  | A             |
| IN2           | 25   | OUTFLOW_I2  | B             |
| IN3           | 26   | OUTFLOW_I3  | C             |
| IN4           | 14   | OUTFLOW_I4  | D             |

### Inflow Stepper — Lower Vent (U6 / ULN2003A)

| ULN2003 Input | GPIO | Net Label  | 28BYJ-48 coil |
|---------------|------|------------|---------------|
| IN1           | 22   | INFLOW_I1  | A             |
| IN2           | 27   | INFLOW_I2  | B             |
| IN3           | 32   | INFLOW_I3  | C             |
| IN4           | 33   | INFLOW_I4  | D             |

### ULN2003 Power

| ULN2003 Pin | Connection |
|-------------|------------|
| COM (motor VCC) | +5 V  |
| GND         | Common GND |

The 28BYJ-48 connects via its standard 5-wire JST-PH connector to the ULN2003 output header (OUT1–OUT4 + COM).

### Step Count

`VENT_STEPS = 1024` steps = full 90° damper rotation. Positions are tracked 0–100% and persisted to NVS (`omx` / `imx` keys for calibrated max steps).

ULN2003 inputs I5–I7 and outputs O5–O7 are unused and are no-connected in the schematic.

---

## 6. Sensor Wiring

### DHT21 / AM2301 (U3 = Ceiling, U4 = Bench)

| Signal | Requirement |
|--------|-------------|
| Power  | 3.3 V, 10 µF decoupling cap recommended near VCC |
| DATA pull-up | 10 kΩ resistor, DATA → VCC (3.3 V) |
| DATA line | Single-wire, open-drain protocol |

Footprint used in schematic: `Sensor:Aosong_DHT11_5.5x12.0_P2.54mm` (DHT11 body; mechanically compatible with DHT21/AM2301).

Sensor value timeout: the firmware marks readings stale after `STALE_THRESHOLD_MS = 10000 ms`. If both sensors are stale simultaneously, PID operates on last-known values.

### PT1000 via MAX31865 (U2)

| Parameter    | Value      |
|--------------|------------|
| Sensor type  | PT1000     |
| Wiring       | 3-wire     |
| RREF         | 4300 Ω     |
| RNOMINAL     | 1000 Ω (0 °C resistance) |
| Breakout     | Adafruit MAX31865 (SSOP-20); footprint `Package_SO:SSOP-20_5.3x7.2mm_P0.65mm` |

The RREF resistor value is critical. Using a 430 Ω reference (correct for PT100) with a PT1000 sensor produces readings that are off by approximately 10× magnitude. The firmware constants `RREF 4300.0` and `RNOMINAL 1000.0` must not be changed unless the hardware is also changed.

PT1000 connector (J1) is a 6-pin header (Conn_01x06) with nets: REFIN+, FORCE+, RTDIN+, RTDIN−, FORCE−, REFIN−.

---

## 7. Power Monitor (INA260)

| Parameter      | Value                                              |
|----------------|----------------------------------------------------|
| IC             | Adafruit INA260                                    |
| I2C address    | 0x40 (default; A0=GND, A1=GND)                    |
| SDA            | GPIO 4                                             |
| SCL            | GPIO 13                                            |
| Shunt resistor | Integrated 2 mΩ (internal to IC; no external resistor required) |
| Max current    | ±15 A (INA260 internal shunt; 1.25 mA LSB)        |

The firmware initializes the INA260 in `setup()` and sets `ina260_ok = false` if `ina260.begin()` fails. All subsequent reads are gated on this flag. The INA260 (U7, TSSOP-16) is present in the KiCad schematic but **not yet wired**; I2C connections must be routed before the next PCB revision.

---

## 8. ERC Lessons Learned

A netlist-generator report was previously located at `docs/kicad/report.txt` (now removed from the repo). It was produced by `generate_schematic.py` and recorded 9 errors of the same class:

```
Error: Cannot add U1 (no footprint assigned).
Error: Cannot add U2 (no footprint assigned).
... (U3–U6, J1–J3 same)
```

**Root cause:** The schematic was generated programmatically by `generate_schematic.py`. At the time this report was produced, footprint properties were either absent or not yet mapped. The `SCHEMATIC_GEN_RULES.md` documents the fix: every symbol must carry a non-empty `Footprint` property at generation time, or `Update PCB from Schematic` will refuse to place the component.

**Status:** The `.kicad_sch` file in the repo does include `Footprint` properties in the embedded lib symbols (e.g., `RF_Module:ESP32-WROOM-32`). The report reflected an intermediate state of the generator and has been removed. Verify footprints are present before attempting PCB export.

**Other ERC issues documented in SCHEMATIC_GEN_RULES.md** (encountered during schematic generator development and now resolved):

| Error | Cause | Resolution |
|-------|-------|------------|
| `endpoint_off_grid` | Coordinates not multiples of 2.54 mm | Snap all positions to `round(x / 2.54) * 2.54` |
| `pin_not_connected` | Wrong Y-axis sign (`abs_y = oy + lib_y` instead of `oy − lib_y`) | Apply Y-down flip: `abs_y = origin_y − lib_pin_y` |
| `label_dangling` | Label anchor not coincident with wire endpoint | Place label anchor exactly at wire endpoint; avoid duplicate labels at same coordinate |
| `power_pin_not_driven` | Missing `PWR_FLAG` on a power net | Add one `PWR_FLAG` per distinct power net, coincident with the power symbol |
| `multiple_net_names` | Two power symbols with different net names at same coordinate | Check for collision when two components share a Y offset |
| `duplicate_reference` | Missing `instances` block (KiCad 9 re-annotates) | Include `(instances ...)` block in every symbol with the schematic UUID |

---

## 9. Design Constraints — Must Not Be Violated

1. **GPIO 6–11 are internal flash lines.** Never connect, probe, or drive them.
2. **GPIO 34–39 are input-only.** Never use as outputs or assign to stepper/PWM functions.
3. **GPIO 5 idles HIGH at boot.** Any device on GPIO 5 whose CS or enable is active-low must not hold the line low through the boot sequence or the module will enter SDIO mode.
4. **GPIO 12 must be LOW or undriven at boot.** Pulling GPIO 12 HIGH at power-on selects 1.8 V flash supply on bare dies; on WROOM-32 this is handled internally, but do not add external pull-ups to GPIO 12.
5. **VSPI bus (GPIO 18/19/23) is allocated to MAX31865.** Adding a second SPI device requires only a new CS line; SCK/MISO/MOSI may be shared, but the second device must tolerate SPI Mode 1 (CPOL=0, CPHA=1) as used by MAX31865, or the bus must be reconfigured between transactions.
6. **DHT21 DATA lines require 10 kΩ pull-ups.** Omitting pull-ups causes intermittent read failures that are indistinguishable from sensor faults.
7. **RREF = 4300 Ω for PT1000.** Substituting a 430 Ω reference (PT100 value) while retaining PT1000 will give grossly wrong temperature readings.
8. **ULN2003 COM must be connected to +5 V.** The 28BYJ-48 requires 5 V coil drive. Connecting COM to 3.3 V results in weak torque and missed steps.
9. **INA260 uses an integrated 2 mΩ shunt.** No external shunt resistor is required. Do not add an external shunt in series with the INA260 — the internal shunt is always active.
10. **NVS keys `omx` and `imx` store calibrated motor max-steps.** If the motor assembly is physically changed (gear ratio, damper throw), these values must be reset via the calibration API, not by changing `VENT_STEPS` alone.

---

## 10. ESP32-WROOM-32 / esp32doit-devkit-v1 Constraints

| Category | Detail |
|----------|--------|
| Supply voltage | 3.0–3.6 V (3.3 V nominal). Do not supply 5 V to VCC. |
| I/O voltage | 3.3 V logic. All GPIO are 3.3 V; not 5 V tolerant. |
| Flash-connected GPIO | 6, 7, 8, 9, 10, 11 — reserved for internal SPI flash |
| Input-only GPIO | 34, 35, 36 (SENSOR_VP), 39 (SENSOR_VN) — no output, no pull |
| Strapping pins | 0 (boot mode), 2 (boot mode), 5 (VSPI timing), 12 (flash voltage), 15 (SDIO) |
| ADC2 conflict | GPIO 0, 2, 4, 12–15, 25–27 are ADC2 channels. ADC2 cannot be used while WiFi is active. Do not add analog reads on these pins. |
| Touch-capable GPIO | 0, 2, 4, 12–15, 27, 32, 33 — capacitive touch peripheral shares these pins; no conflict with current firmware but note the overlap |
| UART0 | TX=GPIO1, RX=GPIO3 — used for serial monitor; not broken out to external connectors |
| Max GPIO source/sink | 40 mA per pin, 1200 mA total chip I/O. ULN2003 input is TTL-compatible and draws <1 mA per input; no concern. |
| Deep sleep wake | Only RTC-capable GPIO (0, 2, 4, 12–15, 25–27, 32–39) can wake from deep sleep. Current firmware does not use deep sleep. |

---

## 11. Sauna Environment Notes

| Factor | Value / Implication |
|--------|---------------------|
| Normal operating range | Ceiling: up to ~160 °F (71 °C) target; Bench: up to ~120 °F (49 °C) target |
| Safety cutoff | `TEMP_LIMIT_C = 120.0 °C` (248 °F). If any DHT21 sensor reaches this threshold, both vents are driven fully open and PID is suppressed. Override at build time with `-DTEMP_LIMIT_C=115`. |
| Humidity | DHT21 sensors operate 0–100% RH; sauna humidity can approach 100% with steam. Sensors should be mounted with the grill facing down or protected from direct water splash. |
| Condensation | All PCB and connector assemblies should be potted or conformal-coated. The ESP32 module itself is not rated for condensing environments. |
| INA260 placement | The INA260 has an integrated 2 mΩ shunt; at 2 A load shunt dissipation is only 8 mW — negligible. No external shunt resistor is needed. The IC itself (TSSOP-16) has a typical operating temperature range of −40 to +125 °C; mount away from the stove sensor area if ambient may exceed 85 °C continuously. |
| Stepper motors | 28BYJ-48 motors are rated for continuous operation up to ~50 °C ambient. Damper positions are set and then held or released; the firmware should de-energize coils after positioning to limit heat buildup in an already-hot enclosure. |
| Cable routing | DHT21 DATA lines are single-wire and susceptible to noise. Keep DATA wiring away from stepper motor cables. Use twisted-pair or shielded wire for DATA + GND if cable runs exceed ~30 cm. |

---

## Schematic Component Reference

| Ref | Value / Part         | Footprint                                      | Notes |
|-----|----------------------|------------------------------------------------|-------|
| U1  | ESP32-WROOM-32       | RF_Module:ESP32-WROOM-32                       | Main MCU |
| U2  | MAX31865xAP          | Package_SO:SSOP-20_5.3x7.2mm_P0.65mm          | PT1000 ADC, VSPI |
| U3  | DHT21/AM2301         | Sensor:Aosong_DHT11_5.5x12.0_P2.54mm          | Ceiling sensor |
| U4  | DHT21/AM2301         | Sensor:Aosong_DHT11_5.5x12.0_P2.54mm          | Bench sensor |
| U5  | ULN2003A             | Package_DIP:DIP-16_W7.62mm                     | Outflow stepper driver |
| U6  | ULN2003A             | Package_DIP:DIP-16_W7.62mm                     | Inflow stepper driver |
| J1  | PT1000-Connector     | Connector_PinHeader_2.54mm:PinHeader_1x06_...  | 6-pin: REFIN+/−, FORCE+/−, RTDIN+/− |
| J2  | 28BYJ-48             | Connector_PinHeader_2.54mm:PinHeader_1x04_...  | Outflow motor JST header |
| J3  | 28BYJ-48             | Connector_PinHeader_2.54mm:PinHeader_1x04_...  | Inflow motor JST header |
| U7  | INA260               | Package_SO:TSSOP-16_4.4x5mm_P0.65mm           | Power monitor; I2C 0x40 (A0=A1=GND), GPIO 4 (SDA) / GPIO 13 (SCL); not yet wired in schematic |
