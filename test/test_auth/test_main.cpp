#include <unity.h>
#include "auth_logic.h"

void setUp(void) {}
void tearDown(void) {}

// Test rand: fill with incrementing counter (used across tasks)
static uint8_t g_randCounter = 0;
static void testRandFn(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) buf[i] = g_randCounter++;
}

void test_auth_logic_header_compiles(void) {
    // Verify struct is at least as large as its members (padding may make it larger)
    TEST_ASSERT_TRUE(sizeof(AuthSession) >= 65 + 33 + 17 + sizeof(uint32_t) + sizeof(bool));
    TEST_ASSERT_EQUAL(0, AUTH_SRC_NVS);
}

// ── Hex helpers ──────────────────────────────────────────────────────────
void test_bytes_to_hex(void) {
    uint8_t bytes[4] = {0x00, 0xFF, 0xAB, 0x12};
    char hex[9];
    authBytesToHex(bytes, 4, hex);
    TEST_ASSERT_EQUAL_STRING("00ffab12", hex);
}

void test_hex_to_bytes(void) {
    const char *hex = "00ffab12";
    uint8_t bytes[4];
    authHexToBytes(hex, bytes, 4);
    TEST_ASSERT_EQUAL_UINT8(0x00, bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(0xAB, bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(0x12, bytes[3]);
}

// ── Constant-time comparison ─────────────────────────────────────────────
void test_token_equal_same(void) {
    char a[65] = "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899";
    char b[65] = "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899";
    TEST_ASSERT_TRUE(authTokenEqual(a, b));
}

void test_token_equal_different(void) {
    char a[65] = "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899";
    char b[65] = "aabbccddeeff00112233445566778899aabbccddeeff001122334455667788FF";
    TEST_ASSERT_FALSE(authTokenEqual(a, b));
}

void test_token_equal_empty_vs_nonempty(void) {
    char a[65] = "";
    char b[65] = "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899";
    TEST_ASSERT_FALSE(authTokenEqual(a, b));
}

// TODO: uncomment in Task 4 after authIssueToken/authValidateToken are implemented
/*
void test_short_token_rejected(void) {
    // A token shorter than 64 chars must never match any stored session
    AuthSession sessions[AUTH_MAX_SESSIONS];
    memset(sessions, 0, sizeof(sessions));
    g_randCounter = 0;
    char validToken[65];
    authIssueToken(sessions, AUTH_MAX_SESSIONS, "alice", "admin", 1000, testRandFn, validToken);
    // Verify that passing a 10-char string doesn't match (length guard prevents OOB read)
    TEST_ASSERT_NULL(authValidateToken(sessions, AUTH_MAX_SESSIONS,
                                       "shorttoken", 2000, AUTH_TOKEN_TTL_MS));
}
*/

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_auth_logic_header_compiles);
    RUN_TEST(test_bytes_to_hex);
    RUN_TEST(test_hex_to_bytes);
    RUN_TEST(test_token_equal_same);
    RUN_TEST(test_token_equal_different);
    RUN_TEST(test_token_equal_empty_vs_nonempty);
    return UNITY_END();
}
