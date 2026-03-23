# SaunaStatus Hardware Pinout

> **Board:** LB-ESP32S3-N16R8 "Lonely Binary" (ESP32-S3, 16 MB OPI Flash, 8 MB OPI PSRAM)
> Previous board: esp32_devkit_v1_doit (ESP32-WROOM-32, 4 MB flash, no PSRAM)
>
> **KiCad footprint:** WEMOS S3 / Lolin S3 — 2×20 through-hole, 2.54 mm pitch, ~26 mm × 65 mm.
> The board is pin-for-pin compatible with the WEMOS S3 / Lolin S3 form factor (`board = lolin_s3` in platformio.ini).
> Source footprint from SnapEDA or the WEMOS KiCad repo; do not adapt the DOIT DevKit footprint (wrong pin count: 2×15 vs 2×20).
>
> **Left header (physical order, top → bottom):** 3V3, 3V3, RST, GPIO3, GPIO4–7, GPIO8–9, GPIO10–14, GPIO15–18, GND
> **Right header (physical order, top → bottom):** GND, GPIO43, GPIO44, GPIO1, GPIO2, GPIO42–39, GPIO38–35, GPIO0, GPIO45, GPIO48, GPIO47, GPIO21, GPIO20, GPIO19, GND, GND

## MAX31865 — Stove PT1000 Thermocouple (Hardware SPI)

| MAX31865 Pin | ESP32-S3 GPIO | Notes |
|---|---|---|
| VIN | 3.3V | |
| GND | GND | |
| CS | GPIO 42 | Right header, consecutive group 39–42 |
| SCK | GPIO 41 | Right header |
| SDO | GPIO 40 | Right header (MISO) |
| SDI | GPIO 39 | Right header (MOSI) |

- Sensor type: PT1000, 3-wire configuration
- Solder jumper on breakout must be set to 3-wire mode
- RREF = 4300 Ω, RNOMINAL = 1000 Ω
- **ESP32-S3 has no fixed default SPI pins.** `SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN)` must be called in `setup()` before `stove_thermo.begin()`.
- Constructor: `Adafruit_MAX31865(SPI_CS_PIN, &SPI)`

---

## DHT21 / AM2301 — Temperature & Humidity Sensors (External, via J4/J5)

The DHT21 sensors are **external packages** connected to the PCB via 4-pin headers (J4 = Ceiling, J5 = Bench).

| Connector | Sensor | Pin 1 | Pin 2 | Pin 3 | Pin 4 |
|---|---|---|---|---|---|
| J4 | Ceiling | +3V3 (VDD) | GPIO 8 (DATA) | NC | GND |
| J5 | Bench | +3V3 (VDD) | GPIO 9 (DATA) | NC | GND |

Connector footprint: `Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical` (2.54 mm pitch, matches DHT21 pinout directly).

- 10 kΩ pull-up DATA → VCC required on each sensor
- Pin numbering matches AM2301 physical pinout: Pin 1=VDD, Pin 2=DATA, Pin 3=NC, Pin 4=GND
- GPIO 8 and GPIO 9 are adjacent on the left header — wires terminate at consecutive pins

---

## Outflow Stepper — Upper Vent (28BYJ-48 via ULN2003)

| ULN2003 Pin | ESP32-S3 GPIO | Header position |
|---|---|---|
| IN1 | GPIO 4 | Left header (consecutive group 4–7) |
| IN2 | GPIO 5 | Left header |
| IN3 | GPIO 6 | Left header |
| IN4 | GPIO 7 | Left header |

- IN1 < IN2 < IN3 < IN4 (ascending) — required for correct 28BYJ-48 step direction with CheapStepper

---

## Inflow Stepper — Lower Vent (28BYJ-48 via ULN2003)

| ULN2003 Pin | ESP32-S3 GPIO | Header position |
|---|---|---|
| IN1 | GPIO 15 | Left header (consecutive group 15–18) |
| IN2 | GPIO 16 | Left header |
| IN3 | GPIO 17 | Left header |
| IN4 | GPIO 18 | Left header |

- IN1 < IN2 < IN3 < IN4 (ascending) — required for correct 28BYJ-48 step direction with CheapStepper

---

## ULN2003 Driver Boards (both)

| ULN2003 Pin | Connection |
|---|---|
| VCC (motor supply) | 5V |
| GND | GND |

Motor connects via standard 28BYJ-48 5-wire JST connector to ULN2003 output header.

---

## INA260 — Power Monitor (I2C)

| INA260 Pin | ESP32-S3 GPIO | Notes |
|---|---|---|
| SDA | GPIO 1 | Right header, adjacent group 1–2 |
| SCL | GPIO 2 | Right header |
| VCC | 3.3V | |
| GND | GND | |
| A0 | GND | I2C address bit 0 = 0 → address 0x40 |
| A1 | GND | I2C address bit 1 = 0 → address 0x40 |

- Integrated 2 mΩ shunt; no external shunt resistor required
- I2C address: 0x40 (A0=GND, A1=GND)
- `Wire.begin(INA260_SDA, INA260_SCL)` — GPIO 1 (SDA), GPIO 2 (SCL)
- API: `readBusVoltage()` (V), `readCurrent()` (mA), `readPower()` (mW)

---

## ESP32-S3 Power Distribution

| ESP32-S3 Pin | Supplies |
|---|---|
| 3V3 | MAX31865 VIN, DHT21 VCC, INA260 VCC |
| 5V (VIN) | ULN2003 motor VCC (both drivers) |
| GND | All GND (common ground) |

---

## Quick Reference — All GPIO Assignments

Sorted by GPIO number. All pins verified conflict-free by `pio test -e native` (test_gpio_config).

| GPIO | Function | Header | Group |
|---|---|---|---|
| 1 | INA260 SDA (I2C) | Right | I2C pair |
| 2 | INA260 SCL (I2C) | Right | I2C pair |
| 4 | Outflow stepper IN1 | Left | Outflow motor |
| 5 | Outflow stepper IN2 | Left | Outflow motor |
| 6 | Outflow stepper IN3 | Left | Outflow motor |
| 7 | Outflow stepper IN4 | Left | Outflow motor |
| 8 | DHT21 Ceiling DATA | Left | DHT sensors |
| 9 | DHT21 Bench DATA | Left | DHT sensors |
| 15 | Inflow stepper IN1 | Left | Inflow motor |
| 16 | Inflow stepper IN2 | Left | Inflow motor |
| 17 | Inflow stepper IN3 | Left | Inflow motor |
| 18 | Inflow stepper IN4 | Left | Inflow motor |
| 39 | MAX31865 MOSI (SDI) | Right | SPI bus |
| 40 | MAX31865 MISO (SDO) | Right | SPI bus |
| 41 | MAX31865 SCK | Right | SPI bus |
| 42 | MAX31865 CS | Right | SPI bus |

## Reserved / Do Not Use

| GPIO | Reason |
|---|---|
| 0 | BOOT strapping pin — LOW at power-on forces download mode |
| 19 | USB D- (native USB OTG) |
| 20 | USB D+ (native USB OTG) |
| 26–32 | OPI PSRAM (internal, N16R8) |
| 33–34 | OPI Flash CS (internal) |
| 35–38 | On header per silkscreen (SPIIO6/7, SPIDQS, FSPIWP) but likely tied to OPI flash interface on N16R8 — treat as reserved until verified with hardware |
| 43 | UART0 TX — serial monitor / upload |
| 44 | UART0 RX — serial monitor / upload |
| 45 | Strapping pin (VDD_SPI voltage selection) |
| 46 | Strapping pin (ROM log enable) |
| 48 | On-board RGB LED |
