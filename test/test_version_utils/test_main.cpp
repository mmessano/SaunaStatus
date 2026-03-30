// test/test_version_utils/test_main.cpp
// RED phase: tests for formatVersion(), isDowngrade(), isSameVersion().
// All three must be added to src/ota_logic.h (pure C++, no Arduino deps).
// Run: pio test -e native -f test_version_utils
//
// formatVersion(const FirmwareVersion& v, char* buf, size_t len)
//   Writes "major.minor.patch" into buf. Writes "" (empty string) if !v.valid.
//
// isDowngrade(const FirmwareVersion& current, const FirmwareVersion& manifest)
//   Returns true if manifest < current (strict). Both must be valid; returns
//   false if either is invalid.
//
// isSameVersion(const FirmwareVersion& a, const FirmwareVersion& b)
//   Returns true if both valid and equal. Returns false if either is invalid.
#include <unity.h>
#include "ota_logic.h"
#include <cstring>

void setUp(void) {}
void tearDown(void) {}

// =============================================================================
// formatVersion
// =============================================================================

void test_format_valid_version(void) {
    FirmwareVersion v = parseVersion("1.2.3");
    char buf[16];
    formatVersion(v, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1.2.3", buf);
}

void test_format_zero_version(void) {
    FirmwareVersion v = parseVersion("0.0.0");
    char buf[16];
    formatVersion(v, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("0.0.0", buf);
}

void test_format_max_byte_version(void) {
    FirmwareVersion v = parseVersion("255.255.255");
    char buf[16];
    formatVersion(v, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("255.255.255", buf);
}

void test_format_invalid_version_writes_empty(void) {
    FirmwareVersion v{};  // valid=false
    char buf[16] = "unchanged";
    formatVersion(v, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_format_firmware_version_string(void) {
    // Simulate the FIRMWARE_VERSION define "2.0.0"
    FirmwareVersion v = parseVersion("2.0.0");
    char buf[16];
    formatVersion(v, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2.0.0", buf);
}

// Buffer exactly sized to fit "1.2.3\0" (6 bytes)
void test_format_exact_fit_buffer(void) {
    FirmwareVersion v = parseVersion("1.2.3");
    char buf[6];
    formatVersion(v, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1.2.3", buf);
}

// Buffer too small — must not crash and must null-terminate
void test_format_truncated_buffer_does_not_crash(void) {
    FirmwareVersion v = parseVersion("1.2.3");
    char buf[3] = {0xFF, 0xFF, 0xFF};
    formatVersion(v, buf, sizeof(buf));
    // buf must be null-terminated (snprintf contract)
    TEST_ASSERT_EQUAL_CHAR('\0', buf[2]);
}

// =============================================================================
// isDowngrade
// =============================================================================

// manifest patch older → downgrade
void test_is_downgrade_older_patch(void) {
    FirmwareVersion current  = parseVersion("1.2.4");
    FirmwareVersion manifest = parseVersion("1.2.3");
    TEST_ASSERT_TRUE(isDowngrade(current, manifest));
}

// manifest minor older → downgrade
void test_is_downgrade_older_minor(void) {
    FirmwareVersion current  = parseVersion("1.3.0");
    FirmwareVersion manifest = parseVersion("1.2.9");
    TEST_ASSERT_TRUE(isDowngrade(current, manifest));
}

// manifest major older → downgrade
void test_is_downgrade_older_major(void) {
    FirmwareVersion current  = parseVersion("2.0.0");
    FirmwareVersion manifest = parseVersion("1.9.9");
    TEST_ASSERT_TRUE(isDowngrade(current, manifest));
}

// manifest newer patch → NOT a downgrade
void test_not_downgrade_newer_patch(void) {
    FirmwareVersion current  = parseVersion("1.2.3");
    FirmwareVersion manifest = parseVersion("1.2.4");
    TEST_ASSERT_FALSE(isDowngrade(current, manifest));
}

// same version → NOT a downgrade
void test_not_downgrade_same_version(void) {
    FirmwareVersion current  = parseVersion("1.2.3");
    FirmwareVersion manifest = parseVersion("1.2.3");
    TEST_ASSERT_FALSE(isDowngrade(current, manifest));
}

// invalid current → false (can't determine direction)
void test_not_downgrade_invalid_current(void) {
    FirmwareVersion current{};  // invalid
    FirmwareVersion manifest = parseVersion("1.2.3");
    TEST_ASSERT_FALSE(isDowngrade(current, manifest));
}

// invalid manifest → false
void test_not_downgrade_invalid_manifest(void) {
    FirmwareVersion current  = parseVersion("1.2.3");
    FirmwareVersion manifest{};  // invalid
    TEST_ASSERT_FALSE(isDowngrade(current, manifest));
}

// both invalid → false
void test_not_downgrade_both_invalid(void) {
    FirmwareVersion current{};
    FirmwareVersion manifest{};
    TEST_ASSERT_FALSE(isDowngrade(current, manifest));
}

// =============================================================================
// isSameVersion
// =============================================================================

void test_same_version_equal(void) {
    FirmwareVersion a = parseVersion("1.2.3");
    FirmwareVersion b = parseVersion("1.2.3");
    TEST_ASSERT_TRUE(isSameVersion(a, b));
}

void test_same_version_zeros(void) {
    FirmwareVersion a = parseVersion("0.0.0");
    FirmwareVersion b = parseVersion("0.0.0");
    TEST_ASSERT_TRUE(isSameVersion(a, b));
}

void test_same_version_different_patch(void) {
    FirmwareVersion a = parseVersion("1.2.3");
    FirmwareVersion b = parseVersion("1.2.4");
    TEST_ASSERT_FALSE(isSameVersion(a, b));
}

void test_same_version_different_minor(void) {
    FirmwareVersion a = parseVersion("1.2.0");
    FirmwareVersion b = parseVersion("1.3.0");
    TEST_ASSERT_FALSE(isSameVersion(a, b));
}

void test_same_version_different_major(void) {
    FirmwareVersion a = parseVersion("1.0.0");
    FirmwareVersion b = parseVersion("2.0.0");
    TEST_ASSERT_FALSE(isSameVersion(a, b));
}

void test_same_version_invalid_a(void) {
    FirmwareVersion a{};  // invalid
    FirmwareVersion b = parseVersion("1.2.3");
    TEST_ASSERT_FALSE(isSameVersion(a, b));
}

void test_same_version_invalid_b(void) {
    FirmwareVersion a = parseVersion("1.2.3");
    FirmwareVersion b{};  // invalid
    TEST_ASSERT_FALSE(isSameVersion(a, b));
}

void test_same_version_both_invalid(void) {
    FirmwareVersion a{};
    FirmwareVersion b{};
    // Two invalid versions are NOT considered equal
    TEST_ASSERT_FALSE(isSameVersion(a, b));
}

// Integration: formatVersion roundtrip with isSameVersion
void test_format_then_parse_roundtrip(void) {
    FirmwareVersion original = parseVersion("3.7.11");
    char buf[16];
    formatVersion(original, buf, sizeof(buf));
    FirmwareVersion reparsed = parseVersion(buf);
    TEST_ASSERT_TRUE(isSameVersion(original, reparsed));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    // formatVersion
    RUN_TEST(test_format_valid_version);
    RUN_TEST(test_format_zero_version);
    RUN_TEST(test_format_max_byte_version);
    RUN_TEST(test_format_invalid_version_writes_empty);
    RUN_TEST(test_format_firmware_version_string);
    RUN_TEST(test_format_exact_fit_buffer);
    RUN_TEST(test_format_truncated_buffer_does_not_crash);
    // isDowngrade
    RUN_TEST(test_is_downgrade_older_patch);
    RUN_TEST(test_is_downgrade_older_minor);
    RUN_TEST(test_is_downgrade_older_major);
    RUN_TEST(test_not_downgrade_newer_patch);
    RUN_TEST(test_not_downgrade_same_version);
    RUN_TEST(test_not_downgrade_invalid_current);
    RUN_TEST(test_not_downgrade_invalid_manifest);
    RUN_TEST(test_not_downgrade_both_invalid);
    // isSameVersion
    RUN_TEST(test_same_version_equal);
    RUN_TEST(test_same_version_zeros);
    RUN_TEST(test_same_version_different_patch);
    RUN_TEST(test_same_version_different_minor);
    RUN_TEST(test_same_version_different_major);
    RUN_TEST(test_same_version_invalid_a);
    RUN_TEST(test_same_version_invalid_b);
    RUN_TEST(test_same_version_both_invalid);
    RUN_TEST(test_format_then_parse_roundtrip);
    return UNITY_END();
}
