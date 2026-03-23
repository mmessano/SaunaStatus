// src/gpio_config.h
// GPIO pin assignments for the LB-ESP32S3-N16R8-Pinout-Modified board.
//
// All signals are grouped by physical adjacency on the board header so each
// device's wires arrive at consecutive pins — no cross-wiring required.
//
// Left header (top → bottom):  3V3, 3V3, RST, GPIO3–18 (sequential), GND
// Right header (top → bottom): GND, GPIO43, GPIO44, GPIO1, GPIO2, GPIO42–39,
//                               GPIO38–35, GPIO0, GPIO45, GPIO48, GPIO47,
//                               GPIO21, GPIO20, GPIO19, GND, GND
//
// Previous board: esp32_devkit_v1_doit (ESP32-WROOM-32, 4MB flash, no PSRAM)
// This board:     LB-ESP32S3-N16R8     (ESP32-S3, 16MB flash, 8MB OPI PSRAM)
//
// GPIO restrictions on N16R8 — DO NOT USE:
//   GPIO 0       : BOOT strapping pin (LOW → download mode)
//   GPIO 19      : USB D-  (native USB OTG)
//   GPIO 20      : USB D+  (native USB OTG)
//   GPIO 26–38   : OPI PSRAM (26–32), OPI Flash (35–37), FSPI (38) — internal
//   GPIO 43      : UART0 TX (serial monitor / upload)
//   GPIO 44      : UART0 RX (serial monitor / upload)
//   GPIO 45      : Strapping pin (VDD_SPI voltage selection)
//   GPIO 46      : Strapping pin (ROM log message enable)
//   GPIO 48      : On-board RGB LED
//
// No Arduino dependencies — this header compiles cleanly in native test builds.
#pragma once

// ─── Outflow (upper vent) stepper ─── left header, GPIO4–7 ──────────────────
// Physically adjacent pins 4–7 on the left header. IN1<IN2<IN3<IN4 matches
// CheapStepper's expected coil sequence for correct 28BYJ-48 step direction.
// Previous: IN1=GPIO21, IN2=GPIO25, IN3=GPIO26, IN4=GPIO14 (scattered)
#define OUTFLOW_IN1 4
#define OUTFLOW_IN2 5
#define OUTFLOW_IN3 6
#define OUTFLOW_IN4 7

// ─── Inflow (lower vent) stepper ─── left header, GPIO15–18 ─────────────────
// Four consecutive pins immediately after the outflow group on the left header.
// Previous: IN1=GPIO22, IN2=GPIO27, IN3=GPIO32, IN4=GPIO33 (scattered)
#define INFLOW_IN1 15
#define INFLOW_IN2 16
#define INFLOW_IN3 17
#define INFLOW_IN4 18

// ─── DHT21 temperature/humidity sensors ─── left header, GPIO8–9 ────────────
// Two adjacent pins for the two external sensor wires.
// Previous: CEILING=GPIO16, BENCH=GPIO17
#define DHTPIN_CEILING 8
#define DHTPIN_BENCH   9
#define DHTTYPE DHT21

// ─── INA260 power monitor (I2C) ─── right header, GPIO1–2 ───────────────────
// Two adjacent pins at the top of the right header (after the UART0 pair).
// Previous: SDA=GPIO4, SCL=GPIO13
#define INA260_SDA 1
#define INA260_SCL 2

// ─── MAX31865 stove thermocouple (SPI) ─── right header, GPIO39–42 ──────────
// Four consecutive right-header pins. Must call SPI.begin(SCK, MISO, MOSI)
// before stove_thermo.begin() in setup() — ESP32-S3 has no fixed default SPI pins.
// Previous: CS=GPIO5, SCK=GPIO18, MISO=GPIO19, MOSI=GPIO23 (VSPI defaults)
#define SPI_CS_PIN   42
#define SPI_SCK_PIN  41
#define SPI_MISO_PIN 40
#define SPI_MOSI_PIN 39
