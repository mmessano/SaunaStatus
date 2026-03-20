# SaunaStatus Hardware Pinout

## MAX31865 — Stove PT1000 Thermocouple (Hardware SPI / VSPI)

| MAX31865 Pin | ESP32 Pin | Notes |
|---|---|---|
| VIN | 3.3V | |
| GND | GND | |
| CS | GPIO 5 | VSPI CS |
| SCK | GPIO 18 | VSPI SCK |
| SDO | GPIO 19 | VSPI MISO |
| SDI | GPIO 23 | VSPI MOSI |

- Sensor type: PT1000, 3-wire configuration
- Solder jumper on breakout must be set to 3-wire mode
- RREF = 4300 Ω, RNOMINAL = 1000 Ω

---

## DHT21 / AM2301 — Temperature & Humidity Sensors (External, via J4/J5)

The DHT21 sensors are **external packages** connected to the PCB via 4-pin headers (J4 = Ceiling, J5 = Bench).

| Connector | Sensor | Pin 1 | Pin 2 | Pin 3 | Pin 4 |
|---|---|---|---|---|---|
| J4 | Ceiling | +3V3 (VDD) | GPIO 16 (DATA) | NC | GND |
| J5 | Bench | +3V3 (VDD) | GPIO 17 (DATA) | NC | GND |

Connector footprint: `Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical` (2.54 mm pitch, matches DHT21 pinout directly).

- 10 kΩ pull-up DATA → VCC required on each sensor
- Pin numbering matches AM2301 physical pinout: Pin 1=VDD, Pin 2=DATA, Pin 3=NC, Pin 4=GND

---

## Outflow Stepper — Upper Vent (28BYJ-48 via ULN2003)

| ULN2003 Pin | ESP32 Pin |
|---|---|
| IN1 | GPIO 21 |
| IN2 | GPIO 25 |
| IN3 | GPIO 26 |
| IN4 | GPIO 14 |

---

## Inflow Stepper — Lower Vent (28BYJ-48 via ULN2003)

| ULN2003 Pin | ESP32 Pin |
|---|---|
| IN1 | GPIO 22 |
| IN2 | GPIO 27 |
| IN3 | GPIO 32 |
| IN4 | GPIO 33 |

---

## ULN2003 Driver Boards (both)

| ULN2003 Pin | Connection |
|---|---|
| VCC (motor supply) | 5V |
| GND | GND |

Motor connects via standard 28BYJ-48 5-wire JST connector to ULN2003 output header.

---

## INA260 — Power Monitor (I2C)

| INA260 Pin | ESP32 Pin | Notes |
|---|---|---|
| SDA | GPIO 4 | Non-default I2C bus; `Wire.begin(4, 13)` |
| SCL | GPIO 13 | Non-default I2C bus; `Wire.begin(4, 13)` |
| VCC | 3.3V | |
| GND | GND | |
| A0 | GND | I2C address bit 0 = 0 → address 0x40 |
| A1 | GND | I2C address bit 1 = 0 → address 0x40 |

- Integrated 2 mΩ shunt; no external shunt resistor required
- I2C address: 0x40 (A0=GND, A1=GND)
- API: `readBusVoltage()` (V), `readCurrent()` (mA), `readPower()` (mW)

---

## ESP32 Power Distribution

| ESP32 Pin | Supplies |
|---|---|
| 3V3 | MAX31865 VIN, DHT21 VCC |
| 5V (VIN) | ULN2003 motor VCC (both drivers) |
| GND | All GND (common ground) |

---

## Quick Reference — All GPIO Assignments

| GPIO | Function |
|---|---|
| 4 | INA260 SDA (I2C) |
| 5 | MAX31865 CS |
| 14 | Outflow stepper IN4 |
| 16 | DHT21 Ceiling DATA |
| 17 | DHT21 Bench DATA |
| 13 | INA260 SCL (I2C) |
| 18 | VSPI SCK (MAX31865) |
| 19 | VSPI MISO (MAX31865 SDO) |
| 21 | Outflow stepper IN1 |
| 22 | Inflow stepper IN1 |
| 23 | VSPI MOSI (MAX31865 SDI) |
| 25 | Outflow stepper IN2 |
| 26 | Outflow stepper IN3 |
| 27 | Inflow stepper IN2 |
| 32 | Inflow stepper IN3 |
| 33 | Inflow stepper IN4 |
