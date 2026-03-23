// test/test_gpio_config/test_main.cpp
// TDD tests for ESP32-S3 GPIO pin configuration (gpio_config.h).
//
// These tests enforce:
//   1. Each signal maps to its expected GPIO number
//   2. Device pins are physically adjacent on the board header (for clean wiring)
//   3. No two signals share the same GPIO (no conflicts)
//   4. No restricted pins are used (boot strapping, USB D+/D-, OPI flash/PSRAM, on-board LED)
//
// Run with: pio test -e native

#include <unity.h>
#include <cstdio>
#include "gpio_config.h"

// Collect all 16 signal pins into an array for uniqueness checks
static const int ALL_SIGNAL_PINS[] = {
    OUTFLOW_IN1, OUTFLOW_IN2, OUTFLOW_IN3, OUTFLOW_IN4,
    INFLOW_IN1,  INFLOW_IN2,  INFLOW_IN3,  INFLOW_IN4,
    DHTPIN_CEILING, DHTPIN_BENCH,
    INA260_SDA, INA260_SCL,
    SPI_CS_PIN, SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN,
};
static const int PIN_COUNT = 16;

void setUp(void) {}
void tearDown(void) {}

// =============================================================================
// Pin Value Tests — Normal Operation
// Each device's GPIO assignments are verified to match the planned mapping.
// Catches copy-paste errors during board migration.
// =============================================================================

void test_outflow_motor_pins_match_expected(void) {
    // Outflow (upper vent) stepper: left header GPIO4–7 (consecutive)
    TEST_ASSERT_EQUAL(4, OUTFLOW_IN1);
    TEST_ASSERT_EQUAL(5, OUTFLOW_IN2);
    TEST_ASSERT_EQUAL(6, OUTFLOW_IN3);
    TEST_ASSERT_EQUAL(7, OUTFLOW_IN4);
}

void test_inflow_motor_pins_match_expected(void) {
    // Inflow (lower vent) stepper: left header GPIO15–18 (consecutive)
    TEST_ASSERT_EQUAL(15, INFLOW_IN1);
    TEST_ASSERT_EQUAL(16, INFLOW_IN2);
    TEST_ASSERT_EQUAL(17, INFLOW_IN3);
    TEST_ASSERT_EQUAL(18, INFLOW_IN4);
}

void test_dht_sensor_pins_match_expected(void) {
    // DHT21 sensors: adjacent left-header pins GPIO8, GPIO9
    TEST_ASSERT_EQUAL(8,  DHTPIN_CEILING);
    TEST_ASSERT_EQUAL(9,  DHTPIN_BENCH);
}

void test_i2c_pins_match_expected(void) {
    // INA260 I2C: right-header adjacent GPIO1 (SDA), GPIO2 (SCL)
    TEST_ASSERT_EQUAL(1, INA260_SDA);
    TEST_ASSERT_EQUAL(2, INA260_SCL);
}

void test_spi_pins_match_expected(void) {
    // MAX31865 SPI: right-header consecutive GPIO39–42
    TEST_ASSERT_EQUAL(42, SPI_CS_PIN);
    TEST_ASSERT_EQUAL(41, SPI_SCK_PIN);
    TEST_ASSERT_EQUAL(40, SPI_MISO_PIN);
    TEST_ASSERT_EQUAL(39, SPI_MOSI_PIN);
}

// =============================================================================
// Adjacency Tests — Configuration Change / Physical Grouping
// Pins for the same device must be consecutive on the board header.
// Catches scattered assignments that would require messy cross-wiring.
// =============================================================================

void test_outflow_motor_pins_are_consecutive(void) {
    // All 4 outflow pins fit in a span of 3 (4 pins, no gaps)
    int mn = OUTFLOW_IN1, mx = OUTFLOW_IN1;
    if (OUTFLOW_IN2 < mn) mn = OUTFLOW_IN2;
    if (OUTFLOW_IN3 < mn) mn = OUTFLOW_IN3;
    if (OUTFLOW_IN4 < mn) mn = OUTFLOW_IN4;
    if (OUTFLOW_IN2 > mx) mx = OUTFLOW_IN2;
    if (OUTFLOW_IN3 > mx) mx = OUTFLOW_IN3;
    if (OUTFLOW_IN4 > mx) mx = OUTFLOW_IN4;
    TEST_ASSERT_EQUAL_MESSAGE(3, mx - mn,
        "Outflow motor pins must be 4 consecutive GPIOs (span = 3)");
}

void test_inflow_motor_pins_are_consecutive(void) {
    int mn = INFLOW_IN1, mx = INFLOW_IN1;
    if (INFLOW_IN2 < mn) mn = INFLOW_IN2;
    if (INFLOW_IN3 < mn) mn = INFLOW_IN3;
    if (INFLOW_IN4 < mn) mn = INFLOW_IN4;
    if (INFLOW_IN2 > mx) mx = INFLOW_IN2;
    if (INFLOW_IN3 > mx) mx = INFLOW_IN3;
    if (INFLOW_IN4 > mx) mx = INFLOW_IN4;
    TEST_ASSERT_EQUAL_MESSAGE(3, mx - mn,
        "Inflow motor pins must be 4 consecutive GPIOs (span = 3)");
}

void test_dht_sensor_pins_are_adjacent(void) {
    int diff = DHTPIN_CEILING - DHTPIN_BENCH;
    if (diff < 0) diff = -diff;
    TEST_ASSERT_EQUAL_MESSAGE(1, diff,
        "DHT ceiling and bench pins must be adjacent GPIOs");
}

void test_i2c_pins_are_adjacent(void) {
    int diff = INA260_SDA - INA260_SCL;
    if (diff < 0) diff = -diff;
    TEST_ASSERT_EQUAL_MESSAGE(1, diff,
        "I2C SDA and SCL pins must be adjacent GPIOs");
}

void test_spi_bus_pins_are_consecutive(void) {
    // 4 SPI bus pins occupy 4 consecutive GPIOs (span = 3)
    int mn = SPI_CS_PIN, mx = SPI_CS_PIN;
    if (SPI_SCK_PIN  < mn) mn = SPI_SCK_PIN;
    if (SPI_MISO_PIN < mn) mn = SPI_MISO_PIN;
    if (SPI_MOSI_PIN < mn) mn = SPI_MOSI_PIN;
    if (SPI_SCK_PIN  > mx) mx = SPI_SCK_PIN;
    if (SPI_MISO_PIN > mx) mx = SPI_MISO_PIN;
    if (SPI_MOSI_PIN > mx) mx = SPI_MOSI_PIN;
    TEST_ASSERT_EQUAL_MESSAGE(3, mx - mn,
        "SPI CS/SCK/MISO/MOSI must be 4 consecutive GPIOs (span = 3)");
}

// =============================================================================
// Conflict Tests — Edge Cases
// All 16 signals must use distinct GPIO numbers.
// Catches situations where two devices are wired to the same physical pin.
// =============================================================================

void test_all_signal_pins_are_unique(void) {
    for (int i = 0; i < PIN_COUNT; i++) {
        for (int j = i + 1; j < PIN_COUNT; j++) {
            char msg[64];
            snprintf(msg, sizeof(msg),
                     "GPIO%d is assigned to two different signals (index %d and %d)",
                     ALL_SIGNAL_PINS[i], i, j);
            TEST_ASSERT_NOT_EQUAL_MESSAGE(ALL_SIGNAL_PINS[i], ALL_SIGNAL_PINS[j], msg);
        }
    }
}

// =============================================================================
// Restricted Pin Tests — Edge Cases
// ESP32-S3 LB-N16R8 has several pins that must NOT be used:
//   GPIO 0  — BOOT strapping pin (LOW = download mode)
//   GPIO 19 — USB D- (native USB)
//   GPIO 20 — USB D+ (native USB)
//   GPIO 26–38 — OPI Flash (GPIO35–37) and OPI PSRAM (GPIO26–32) on N16R8;
//                also includes FSPI pins GPIO38; entire range is off-limits
//   GPIO 45 — Strapping pin (VDD_SPI voltage selection)
//   GPIO 46 — Strapping pin (ROM log message enable)
//   GPIO 48 — On-board RGB LED (already in use)
//   GPIO 43 — UART0 TX (used for serial monitor)
//   GPIO 44 — UART0 RX (used for serial monitor)
// =============================================================================

void test_no_pin_uses_gpio0_boot_strap(void) {
    for (int i = 0; i < PIN_COUNT; i++)
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, ALL_SIGNAL_PINS[i],
            "GPIO0 is the BOOT strapping pin — pulling it LOW at power-on enters download mode");
}

void test_no_pin_uses_gpio19_usb_dm(void) {
    for (int i = 0; i < PIN_COUNT; i++)
        TEST_ASSERT_NOT_EQUAL_MESSAGE(19, ALL_SIGNAL_PINS[i],
            "GPIO19 is USB D- — using it for GPIO disables native USB");
}

void test_no_pin_uses_gpio20_usb_dp(void) {
    for (int i = 0; i < PIN_COUNT; i++)
        TEST_ASSERT_NOT_EQUAL_MESSAGE(20, ALL_SIGNAL_PINS[i],
            "GPIO20 is USB D+ — using it for GPIO disables native USB");
}

void test_no_pin_in_opi_flash_psram_range(void) {
    // GPIO26–38 are internally used by OPI PSRAM (26–32) and OPI Flash (35–37)
    // on the N16R8 module. GPIO33/34 are also internal Flash CS.
    // Avoid the entire range 26–38 to be safe.
    for (int i = 0; i < PIN_COUNT; i++) {
        char msg[80];
        snprintf(msg, sizeof(msg),
                 "GPIO%d is in the OPI Flash/PSRAM range (26–38) reserved on N16R8",
                 ALL_SIGNAL_PINS[i]);
        TEST_ASSERT_FALSE_MESSAGE(
            ALL_SIGNAL_PINS[i] >= 26 && ALL_SIGNAL_PINS[i] <= 38, msg);
    }
}

void test_no_pin_uses_gpio45_strap(void) {
    for (int i = 0; i < PIN_COUNT; i++)
        TEST_ASSERT_NOT_EQUAL_MESSAGE(45, ALL_SIGNAL_PINS[i],
            "GPIO45 is a strapping pin (VDD_SPI voltage) — avoid to prevent boot instability");
}

void test_no_pin_uses_gpio48_rgb_led(void) {
    for (int i = 0; i < PIN_COUNT; i++)
        TEST_ASSERT_NOT_EQUAL_MESSAGE(48, ALL_SIGNAL_PINS[i],
            "GPIO48 drives the on-board RGB LED — using it for another signal would conflict");
}

void test_no_pin_uses_uart0_tx_rx(void) {
    for (int i = 0; i < PIN_COUNT; i++) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(43, ALL_SIGNAL_PINS[i],
            "GPIO43 is UART0 TX — used for Serial monitor, do not repurpose");
        TEST_ASSERT_NOT_EQUAL_MESSAGE(44, ALL_SIGNAL_PINS[i],
            "GPIO44 is UART0 RX — used for Serial monitor, do not repurpose");
    }
}

// =============================================================================
// Motor Coil Order Test
// IN1 < IN2 < IN3 < IN4 on consecutive GPIOs ensures the stepper library
// drives coils in the correct sequence. Reversed or shuffled assignment
// causes the motor to stall or run backwards.
// =============================================================================

void test_outflow_motor_coil_order_is_ascending(void) {
    TEST_ASSERT_LESS_THAN(OUTFLOW_IN2, OUTFLOW_IN1);  // IN1 < IN2
    TEST_ASSERT_LESS_THAN(OUTFLOW_IN3, OUTFLOW_IN2);  // IN2 < IN3
    TEST_ASSERT_LESS_THAN(OUTFLOW_IN4, OUTFLOW_IN3);  // IN3 < IN4
}

void test_inflow_motor_coil_order_is_ascending(void) {
    TEST_ASSERT_LESS_THAN(INFLOW_IN2, INFLOW_IN1);
    TEST_ASSERT_LESS_THAN(INFLOW_IN3, INFLOW_IN2);
    TEST_ASSERT_LESS_THAN(INFLOW_IN4, INFLOW_IN3);
}

// =============================================================================
// Valid Range Test
// All GPIOs must be in the valid ESP32-S3 GPIO range 0–48.
// =============================================================================

void test_all_pins_in_valid_esp32s3_range(void) {
    for (int i = 0; i < PIN_COUNT; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg),
                 "GPIO%d is outside valid ESP32-S3 range 0–48", ALL_SIGNAL_PINS[i]);
        TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(0,  ALL_SIGNAL_PINS[i], msg);
        TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(48, ALL_SIGNAL_PINS[i], msg);
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Normal operation — pin value verification
    RUN_TEST(test_outflow_motor_pins_match_expected);
    RUN_TEST(test_inflow_motor_pins_match_expected);
    RUN_TEST(test_dht_sensor_pins_match_expected);
    RUN_TEST(test_i2c_pins_match_expected);
    RUN_TEST(test_spi_pins_match_expected);

    // Configuration change — physical adjacency grouping
    RUN_TEST(test_outflow_motor_pins_are_consecutive);
    RUN_TEST(test_inflow_motor_pins_are_consecutive);
    RUN_TEST(test_dht_sensor_pins_are_adjacent);
    RUN_TEST(test_i2c_pins_are_adjacent);
    RUN_TEST(test_spi_bus_pins_are_consecutive);

    // Edge cases — no pin conflicts
    RUN_TEST(test_all_signal_pins_are_unique);

    // Edge cases — restricted pins
    RUN_TEST(test_no_pin_uses_gpio0_boot_strap);
    RUN_TEST(test_no_pin_uses_gpio19_usb_dm);
    RUN_TEST(test_no_pin_uses_gpio20_usb_dp);
    RUN_TEST(test_no_pin_in_opi_flash_psram_range);
    RUN_TEST(test_no_pin_uses_gpio45_strap);
    RUN_TEST(test_no_pin_uses_gpio48_rgb_led);
    RUN_TEST(test_no_pin_uses_uart0_tx_rx);

    // Edge cases — motor coil order (wrong order stalls motor)
    RUN_TEST(test_outflow_motor_coil_order_is_ascending);
    RUN_TEST(test_inflow_motor_coil_order_is_ascending);

    // Valid GPIO range
    RUN_TEST(test_all_pins_in_valid_esp32s3_range);

    return UNITY_END();
}
