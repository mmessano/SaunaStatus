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

// Test hash: XOR-fold all bytes into each output byte — deterministic, not secure
static void testHashFn(const uint8_t *data, size_t len, uint8_t *out32) {
    uint8_t acc = 0;
    for (size_t i = 0; i < len; i++) acc ^= data[i];
    memset(out32, acc, 32);
}

void test_generate_salt_length(void) {
    g_randCounter = 0;
    char salt[33];
    authGenerateSalt(salt, testRandFn);
    TEST_ASSERT_EQUAL(32, strlen(salt));  // 16 bytes → 32 hex chars
}

void test_hash_and_verify_correct_password(void) {
    char salt[33], hash[65];
    g_randCounter = 42;
    authGenerateSalt(salt, testRandFn);
    authHashPassword("mysecretpass", salt, hash, testHashFn);
    TEST_ASSERT_TRUE(authVerifyPassword("mysecretpass", salt, hash, testHashFn));
}

void test_wrong_password_rejected(void) {
    char salt[33], hash[65];
    g_randCounter = 0;
    authGenerateSalt(salt, testRandFn);
    authHashPassword("correctpass", salt, hash, testHashFn);
    TEST_ASSERT_FALSE(authVerifyPassword("wrongpass", salt, hash, testHashFn));
}

void test_empty_password_rejected_by_length(void) {
    TEST_ASSERT_FALSE(authPasswordLengthOk(""));
}

void test_password_below_min_len_rejected(void) {
    TEST_ASSERT_FALSE(authPasswordLengthOk("short"));   // 5 < 8
}

void test_password_at_min_len_accepted(void) {
    TEST_ASSERT_TRUE(authPasswordLengthOk("exactly8"));  // 8 == AUTH_MIN_PASS_LEN
}

void test_password_above_min_len_accepted(void) {
    TEST_ASSERT_TRUE(authPasswordLengthOk("longerpassword"));
}

void test_password_at_max_len_accepted(void) {
    // AUTH_MAX_PASS_LEN chars — boundary, should be accepted
    char pass[AUTH_MAX_PASS_LEN + 1];
    memset(pass, 'a', AUTH_MAX_PASS_LEN);
    pass[AUTH_MAX_PASS_LEN] = '\0';
    TEST_ASSERT_TRUE(authPasswordLengthOk(pass));
}

void test_password_above_max_len_rejected(void) {
    // AUTH_MAX_PASS_LEN + 1 chars — exceeds max, must be rejected to prevent hash buffer overflow
    char pass[AUTH_MAX_PASS_LEN + 2];
    memset(pass, 'a', AUTH_MAX_PASS_LEN + 1);
    pass[AUTH_MAX_PASS_LEN + 1] = '\0';
    TEST_ASSERT_FALSE(authPasswordLengthOk(pass));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_auth_logic_header_compiles);
    RUN_TEST(test_bytes_to_hex);
    RUN_TEST(test_hex_to_bytes);
    RUN_TEST(test_token_equal_same);
    RUN_TEST(test_token_equal_different);
    RUN_TEST(test_token_equal_empty_vs_nonempty);
    RUN_TEST(test_generate_salt_length);
    RUN_TEST(test_hash_and_verify_correct_password);
    RUN_TEST(test_wrong_password_rejected);
    RUN_TEST(test_empty_password_rejected_by_length);
    RUN_TEST(test_password_below_min_len_rejected);
    RUN_TEST(test_password_at_min_len_accepted);
    RUN_TEST(test_password_above_min_len_accepted);
    RUN_TEST(test_password_at_max_len_accepted);
    RUN_TEST(test_password_above_max_len_rejected);
    return UNITY_END();
}
