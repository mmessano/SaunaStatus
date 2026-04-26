#include <unity.h>
#include "ota_logic.h"
#include <cstring>

void setUp(void) {}
void tearDown(void) {}

// =============================================================================
// Version parsing
// =============================================================================

void test_parse_version_valid(void) {
    FirmwareVersion v = parseVersion("1.2.3");
    TEST_ASSERT_TRUE(v.valid);
    TEST_ASSERT_EQUAL_UINT8(1, v.major);
    TEST_ASSERT_EQUAL_UINT8(2, v.minor);
    TEST_ASSERT_EQUAL_UINT8(3, v.patch);
}

void test_parse_version_empty_invalid(void) {
    FirmwareVersion v = parseVersion("");
    TEST_ASSERT_FALSE(v.valid);
}

void test_parse_version_null_invalid(void) {
    FirmwareVersion v = parseVersion(nullptr);
    TEST_ASSERT_FALSE(v.valid);
}

void test_parse_version_malformed_invalid(void) {
    TEST_ASSERT_FALSE(parseVersion("abc").valid);
    TEST_ASSERT_FALSE(parseVersion("1.2").valid);
    TEST_ASSERT_FALSE(parseVersion("1.2.").valid);
    TEST_ASSERT_FALSE(parseVersion("1..3").valid);
}

void test_parse_version_zeros_valid(void) {
    FirmwareVersion v = parseVersion("0.0.0");
    TEST_ASSERT_TRUE(v.valid);
    TEST_ASSERT_EQUAL_UINT8(0, v.major);
    TEST_ASSERT_EQUAL_UINT8(0, v.minor);
    TEST_ASSERT_EQUAL_UINT8(0, v.patch);
}

// =============================================================================
// Version comparison
// =============================================================================

void test_version_compare_newer_patch(void) {
    FirmwareVersion a = parseVersion("1.2.4");
    FirmwareVersion b = parseVersion("1.2.3");
    TEST_ASSERT_EQUAL_INT(1, compareVersion(a, b));
}

void test_version_compare_older_patch(void) {
    FirmwareVersion a = parseVersion("1.2.3");
    FirmwareVersion b = parseVersion("1.2.4");
    TEST_ASSERT_EQUAL_INT(-1, compareVersion(a, b));
}

void test_version_compare_equal(void) {
    FirmwareVersion a = parseVersion("1.2.3");
    FirmwareVersion b = parseVersion("1.2.3");
    TEST_ASSERT_EQUAL_INT(0, compareVersion(a, b));
}

void test_version_compare_newer_minor(void) {
    FirmwareVersion a = parseVersion("1.3.0");
    FirmwareVersion b = parseVersion("1.2.9");
    TEST_ASSERT_EQUAL_INT(1, compareVersion(a, b));
}

void test_version_compare_newer_major(void) {
    FirmwareVersion a = parseVersion("2.0.0");
    FirmwareVersion b = parseVersion("1.9.9");
    TEST_ASSERT_EQUAL_INT(1, compareVersion(a, b));
}

// =============================================================================
// Manifest parsing
// =============================================================================

void test_manifest_valid(void) {
    const char *json = "{\"version\":\"1.2.3\",\"url\":\"http://server/fw.bin\","
                       "\"sha256\":\"abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789\"}";
    OtaManifest m = parseOtaManifest(json);
    TEST_ASSERT_TRUE(m.valid);
    TEST_ASSERT_EQUAL_STRING("1.2.3", m.version);
    TEST_ASSERT_EQUAL_STRING("http://server/fw.bin", m.url);
    TEST_ASSERT_EQUAL_STRING("abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
                             m.sha256);
}

void test_manifest_missing_url_invalid(void) {
    const char *json = "{\"version\":\"1.2.3\",\"sha256\":\"abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789\"}";
    OtaManifest m = parseOtaManifest(json);
    TEST_ASSERT_FALSE(m.valid);
}

void test_manifest_missing_version_invalid(void) {
    const char *json = "{\"url\":\"http://server/fw.bin\",\"sha256\":\"abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789\"}";
    OtaManifest m = parseOtaManifest(json);
    TEST_ASSERT_FALSE(m.valid);
}

void test_manifest_sha256_required(void) {
    // sha256 is now required — manifest without it is invalid
    const char *json = "{\"version\":\"1.0.0\",\"url\":\"http://server/fw.bin\"}";
    OtaManifest m = parseOtaManifest(json);
    TEST_ASSERT_FALSE(m.valid);
}

void test_manifest_md5_alone_not_sufficient(void) {
    // md5 alone does not satisfy the hash requirement
    const char *json = "{\"version\":\"1.0.0\",\"url\":\"http://server/fw.bin\",\"md5\":\"abc123\"}";
    OtaManifest m = parseOtaManifest(json);
    TEST_ASSERT_FALSE(m.valid);
}

void test_manifest_md5_with_sha256_valid(void) {
    // Both md5 and sha256 present — valid (sha256 takes precedence)
    const char *json = "{\"version\":\"1.0.0\",\"url\":\"http://server/fw.bin\","
                       "\"md5\":\"abc123\","
                       "\"sha256\":\"abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789\"}";
    OtaManifest m = parseOtaManifest(json);
    TEST_ASSERT_TRUE(m.valid);
    TEST_ASSERT_EQUAL_STRING("abc123", m.md5);
}

void test_manifest_no_update_needed_same_version(void) {
    const char *json = "{\"version\":\"1.0.0\",\"url\":\"http://server/fw.bin\","
                       "\"sha256\":\"abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789\"}";
    OtaManifest m = parseOtaManifest(json);
    FirmwareVersion current  = parseVersion("1.0.0");
    FirmwareVersion manifest = parseVersion(m.version);
    TEST_ASSERT_TRUE(m.valid);
    TEST_ASSERT_FALSE(isUpdateAvailable(current, manifest));
}

void test_manifest_no_update_needed_older_version(void) {
    const char *json = "{\"version\":\"0.9.0\",\"url\":\"http://server/fw.bin\","
                       "\"sha256\":\"abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789\"}";
    OtaManifest m = parseOtaManifest(json);
    FirmwareVersion current  = parseVersion("1.0.0");
    FirmwareVersion manifest = parseVersion(m.version);
    TEST_ASSERT_TRUE(m.valid);
    TEST_ASSERT_FALSE(isUpdateAvailable(current, manifest));
}

void test_manifest_update_available_newer_version(void) {
    const char *json = "{\"version\":\"1.1.0\",\"url\":\"http://server/fw.bin\","
                       "\"sha256\":\"abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789\"}";
    OtaManifest m = parseOtaManifest(json);
    FirmwareVersion current  = parseVersion("1.0.0");
    FirmwareVersion manifest = parseVersion(m.version);
    TEST_ASSERT_TRUE(m.valid);
    TEST_ASSERT_TRUE(isUpdateAvailable(current, manifest));
}

// =============================================================================
// Rollback threshold
// =============================================================================

void test_no_rollback_below_threshold(void) {
    TEST_ASSERT_FALSE(shouldRollback(2, 3));
}

void test_rollback_at_threshold(void) {
    TEST_ASSERT_TRUE(shouldRollback(3, 3));
}

void test_rollback_above_threshold(void) {
    TEST_ASSERT_TRUE(shouldRollback(5, 3));
}

void test_no_rollback_zero_failures(void) {
    TEST_ASSERT_FALSE(shouldRollback(0, 3));
}

// =============================================================================
// Partial download / recovery detection
// =============================================================================

void test_ota_incomplete_detected(void) {
    OtaDownloadState s;
    s.in_progress     = true;
    s.bytes_expected  = 1024 * 1024;
    s.bytes_written   = 512 * 1024;  // half-written
    TEST_ASSERT_TRUE(isOtaIncomplete(s));
}

void test_ota_complete_not_incomplete(void) {
    OtaDownloadState s;
    s.in_progress    = true;
    s.bytes_expected = 1024 * 1024;
    s.bytes_written  = 1024 * 1024;  // fully written
    TEST_ASSERT_FALSE(isOtaIncomplete(s));
}

void test_ota_not_started_not_incomplete(void) {
    OtaDownloadState s;  // in_progress defaults false
    TEST_ASSERT_FALSE(isOtaIncomplete(s));
}

void test_ota_zero_expected_not_incomplete(void) {
    // in_progress=true but no size set — treat as not started
    OtaDownloadState s;
    s.in_progress    = true;
    s.bytes_expected = 0;
    s.bytes_written  = 0;
    TEST_ASSERT_FALSE(isOtaIncomplete(s));
}

// ── URL validation tests ─────────────────────────────────────────────────────

void test_ota_extract_hostname_https(void) {
    char host[64];
    TEST_ASSERT_TRUE(otaExtractHostname("https://firmware.example.com/path", host, sizeof(host)));
    TEST_ASSERT_EQUAL_STRING("firmware.example.com", host);
}

void test_ota_extract_hostname_http(void) {
    char host[64];
    TEST_ASSERT_TRUE(otaExtractHostname("http://192.168.1.100/fw.bin", host, sizeof(host)));
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", host);
}

void test_ota_extract_hostname_with_port(void) {
    char host[64];
    TEST_ASSERT_TRUE(otaExtractHostname("https://host.local:8443/manifest", host, sizeof(host)));
    TEST_ASSERT_EQUAL_STRING("host.local", host);
}

void test_ota_extract_hostname_no_scheme(void) {
    char host[64];
    TEST_ASSERT_FALSE(otaExtractHostname("ftp://bad.com/file", host, sizeof(host)));
}

void test_ota_extract_hostname_empty(void) {
    char host[64];
    TEST_ASSERT_FALSE(otaExtractHostname("", host, sizeof(host)));
    TEST_ASSERT_FALSE(otaExtractHostname(nullptr, host, sizeof(host)));
}

void test_ota_is_https(void) {
    TEST_ASSERT_TRUE(otaIsHttps("https://example.com/fw.bin"));
    TEST_ASSERT_FALSE(otaIsHttps("http://example.com/fw.bin"));
    TEST_ASSERT_FALSE(otaIsHttps("ftp://example.com/fw.bin"));
    TEST_ASSERT_FALSE(otaIsHttps(nullptr));
}

void test_ota_host_allowed_single(void) {
    TEST_ASSERT_TRUE(otaHostAllowed("firmware.example.com", "firmware.example.com"));
    TEST_ASSERT_FALSE(otaHostAllowed("evil.com", "firmware.example.com"));
}

void test_ota_host_allowed_multiple(void) {
    const char *list = "fw1.example.com, fw2.example.com, 192.168.1.100";
    TEST_ASSERT_TRUE(otaHostAllowed("fw1.example.com", list));
    TEST_ASSERT_TRUE(otaHostAllowed("fw2.example.com", list));
    TEST_ASSERT_TRUE(otaHostAllowed("192.168.1.100", list));
    TEST_ASSERT_FALSE(otaHostAllowed("evil.com", list));
}

void test_ota_host_allowed_empty_list(void) {
    TEST_ASSERT_FALSE(otaHostAllowed("anything.com", ""));
    TEST_ASSERT_FALSE(otaHostAllowed("anything.com", nullptr));
}

void test_ota_host_allowed_empty_hostname(void) {
    TEST_ASSERT_FALSE(otaHostAllowed("", "example.com"));
    TEST_ASSERT_FALSE(otaHostAllowed(nullptr, "example.com"));
}

void test_ota_validate_url_rejects_http(void) {
    // otaValidateUrl requires HTTPS — always rejects http://
    TEST_ASSERT_FALSE(otaValidateUrl("http://192.168.1.100/manifest.json"));
}

void test_ota_validate_url_accepts_https_allowed_host(void) {
    TEST_ASSERT_TRUE(otaValidateUrlWithAllowlist(
        "https://fw1.example.com/manifest.json",
        "fw1.example.com, fw2.example.com"));
}

void test_ota_validate_url_rejects_https_with_empty_allowlist(void) {
    TEST_ASSERT_FALSE(otaValidateUrlWithAllowlist(
        "https://fw1.example.com/manifest.json", ""));
}

void test_ota_validate_url_rejects_unknown_host(void) {
    // Even with HTTPS, host must be in OTA_ALLOWED_HOSTS (default is empty)
    // With default empty allowlist, all URLs are rejected
    TEST_ASSERT_FALSE(otaValidateUrl("https://random.host.com/fw.bin"));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Version parsing
    RUN_TEST(test_parse_version_valid);
    RUN_TEST(test_parse_version_empty_invalid);
    RUN_TEST(test_parse_version_null_invalid);
    RUN_TEST(test_parse_version_malformed_invalid);
    RUN_TEST(test_parse_version_zeros_valid);

    // Version comparison
    RUN_TEST(test_version_compare_newer_patch);
    RUN_TEST(test_version_compare_older_patch);
    RUN_TEST(test_version_compare_equal);
    RUN_TEST(test_version_compare_newer_minor);
    RUN_TEST(test_version_compare_newer_major);

    // Manifest parsing
    RUN_TEST(test_manifest_valid);
    RUN_TEST(test_manifest_missing_url_invalid);
    RUN_TEST(test_manifest_missing_version_invalid);
    RUN_TEST(test_manifest_sha256_required);
    RUN_TEST(test_manifest_md5_alone_not_sufficient);
    RUN_TEST(test_manifest_md5_with_sha256_valid);
    RUN_TEST(test_manifest_no_update_needed_same_version);
    RUN_TEST(test_manifest_no_update_needed_older_version);
    RUN_TEST(test_manifest_update_available_newer_version);

    // Rollback
    RUN_TEST(test_no_rollback_below_threshold);
    RUN_TEST(test_rollback_at_threshold);
    RUN_TEST(test_rollback_above_threshold);
    RUN_TEST(test_no_rollback_zero_failures);

    // Partial download recovery
    RUN_TEST(test_ota_incomplete_detected);
    RUN_TEST(test_ota_complete_not_incomplete);
    RUN_TEST(test_ota_not_started_not_incomplete);
    RUN_TEST(test_ota_zero_expected_not_incomplete);

    // URL validation
    RUN_TEST(test_ota_extract_hostname_https);
    RUN_TEST(test_ota_extract_hostname_http);
    RUN_TEST(test_ota_extract_hostname_with_port);
    RUN_TEST(test_ota_extract_hostname_no_scheme);
    RUN_TEST(test_ota_extract_hostname_empty);
    RUN_TEST(test_ota_is_https);
    RUN_TEST(test_ota_host_allowed_single);
    RUN_TEST(test_ota_host_allowed_multiple);
    RUN_TEST(test_ota_host_allowed_empty_list);
    RUN_TEST(test_ota_host_allowed_empty_hostname);
    RUN_TEST(test_ota_validate_url_rejects_http);
    RUN_TEST(test_ota_validate_url_accepts_https_allowed_host);
    RUN_TEST(test_ota_validate_url_rejects_https_with_empty_allowlist);
    RUN_TEST(test_ota_validate_url_rejects_unknown_host);

    return UNITY_END();
}
