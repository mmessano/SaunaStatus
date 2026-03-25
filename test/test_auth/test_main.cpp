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

// ── Session helpers ───────────────────────────────────────────────────────
static AuthSession g_sessions[AUTH_MAX_SESSIONS];

static void clearSessions(void) {
    memset(g_sessions, 0, sizeof(g_sessions));
}

void test_issue_token_populates_slot(void) {
    clearSessions();
    g_randCounter = 0;
    char token[65];
    bool ok = authIssueToken(g_sessions, AUTH_MAX_SESSIONS,
                              "alice", "admin", 1000, testRandFn, token);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("alice", g_sessions[0].username);
    TEST_ASSERT_EQUAL_STRING("admin", g_sessions[0].role);
    TEST_ASSERT_TRUE(g_sessions[0].active);
    TEST_ASSERT_EQUAL(1000, g_sessions[0].issued_ms);
    TEST_ASSERT_EQUAL(64, (int)strlen(token));
}

void test_issued_token_validates(void) {
    clearSessions();
    g_randCounter = 0;
    char token[65];
    authIssueToken(g_sessions, AUTH_MAX_SESSIONS,
                   "alice", "admin", 1000, testRandFn, token);
    const AuthSession *s = authValidateToken(g_sessions, AUTH_MAX_SESSIONS,
                                              token, 2000, AUTH_TOKEN_TTL_MS);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_STRING("alice", s->username);
}

void test_wrong_token_rejected(void) {
    clearSessions();
    g_randCounter = 0;
    char token[65];
    authIssueToken(g_sessions, AUTH_MAX_SESSIONS,
                   "alice", "admin", 1000, testRandFn, token);
    token[0] = (token[0] == 'a') ? 'b' : 'a';
    const AuthSession *s = authValidateToken(g_sessions, AUTH_MAX_SESSIONS,
                                              token, 2000, AUTH_TOKEN_TTL_MS);
    TEST_ASSERT_NULL(s);
}

void test_expired_token_rejected(void) {
    clearSessions();
    g_randCounter = 0;
    char token[65];
    authIssueToken(g_sessions, AUTH_MAX_SESSIONS,
                   "alice", "admin", 0, testRandFn, token);
    // exactly at TTL — NOT expired (strict >)
    const AuthSession *s = authValidateToken(g_sessions, AUTH_MAX_SESSIONS,
                                              token, AUTH_TOKEN_TTL_MS, AUTH_TOKEN_TTL_MS);
    TEST_ASSERT_NOT_NULL(s);
    // one ms over — expired
    s = authValidateToken(g_sessions, AUTH_MAX_SESSIONS,
                          token, AUTH_TOKEN_TTL_MS + 1, AUTH_TOKEN_TTL_MS);
    TEST_ASSERT_NULL(s);
}

void test_expiry_across_millis_rollover(void) {
    clearSessions();
    g_randCounter = 0;
    char token[65];
    uint32_t issued = 0xFFFFFF00UL;
    authIssueToken(g_sessions, AUTH_MAX_SESSIONS,
                   "alice", "admin", issued, testRandFn, token);
    uint32_t now = 0x00000100UL;  // wrapped — elapsed = 0x100 = 256ms
    const AuthSession *s = authValidateToken(g_sessions, AUTH_MAX_SESSIONS,
                                              token, now, 1000UL);
    TEST_ASSERT_NOT_NULL(s);  // 256 < 1000
    s = authValidateToken(g_sessions, AUTH_MAX_SESSIONS, token, now, 200UL);
    TEST_ASSERT_NULL(s);      // 256 > 200
}

void test_logout_invalidates_token(void) {
    clearSessions();
    g_randCounter = 0;
    char token[65];
    authIssueToken(g_sessions, AUTH_MAX_SESSIONS,
                   "alice", "admin", 1000, testRandFn, token);
    authInvalidateToken(g_sessions, AUTH_MAX_SESSIONS, token);
    const AuthSession *s = authValidateToken(g_sessions, AUTH_MAX_SESSIONS,
                                              token, 2000, AUTH_TOKEN_TTL_MS);
    TEST_ASSERT_NULL(s);
}

void test_expired_slot_reclaimed_before_valid(void) {
    // Tests the first branch of authFindEvictSlot: an expired slot is chosen
    // before falling through to the oldest-eviction path.
    // Use authFindEvictSlot directly so we can supply a custom ttl_ms that
    // makes only slot 3 expired (authIssueToken hardcodes AUTH_TOKEN_TTL_MS).
    clearSessions();
    // All 10 slots active, issued at ms=1000. now=5000 → age=4000.
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        g_sessions[i].active    = true;
        g_sessions[i].issued_ms = 1000;
    }
    // Slot 3: issued at ms=0. now=5000, age=5000.
    // With ttl=4500: 5000 > 4500 → expired. All others: 4000 < 4500 → valid.
    g_sessions[3].issued_ms = 0;
    int slot = authFindEvictSlot(g_sessions, AUTH_MAX_SESSIONS, 5000, 4500UL);
    TEST_ASSERT_EQUAL(3, slot);  // expired slot chosen; valid sessions not displaced
}

void test_oldest_valid_evicted_when_all_full(void) {
    clearSessions();
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        g_randCounter = i;
        char t[65];
        char user[8]; user[0]='u'; user[1]='0'+i; user[2]='\0';
        authIssueToken(g_sessions, AUTH_MAX_SESSIONS,
                       user, "admin", (uint32_t)(i * 1000), testRandFn, t);
    }
    // slot 0 has issued_ms=0 → oldest → should be evicted
    g_randCounter = 99;
    char newToken[65];
    uint32_t now = AUTH_MAX_SESSIONS * 1000 + 500;
    authIssueToken(g_sessions, AUTH_MAX_SESSIONS,
                   "latecomer", "admin", now, testRandFn, newToken);
    TEST_ASSERT_EQUAL_STRING("latecomer", g_sessions[0].username);
}

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

// ── User store ────────────────────────────────────────────────────────────
static AuthUserStore g_store;

static void clearStore(void) {
    memset(&g_store, 0, sizeof(g_store));
}

void test_add_user_and_find(void) {
    clearStore();
    g_randCounter = 0;
    AuthUserResult r = authAddUser(&g_store, "alice", "password1", "admin",
                                    testRandFn, testHashFn);
    TEST_ASSERT_EQUAL(AUTH_USER_OK, r);
    TEST_ASSERT_EQUAL(1, g_store.count);
    const AuthUser *u = authFindUser(&g_store, "alice");
    TEST_ASSERT_NOT_NULL(u);
    TEST_ASSERT_EQUAL_STRING("alice", u->name);
}

void test_max_users_enforced(void) {
    clearStore();
    g_randCounter = 0;
    for (int i = 0; i < AUTH_MAX_USERS; i++) {
        char name[8]; name[0]='u'; name[1]='0'+i; name[2]='\0';
        AuthUserResult r = authAddUser(&g_store, name, "password1", "admin",
                                        testRandFn, testHashFn);
        TEST_ASSERT_EQUAL(AUTH_USER_OK, r);
    }
    // 6th user should fail
    AuthUserResult r = authAddUser(&g_store, "overflow", "password1", "admin",
                                    testRandFn, testHashFn);
    TEST_ASSERT_EQUAL(AUTH_USER_FULL, r);
    TEST_ASSERT_EQUAL(AUTH_MAX_USERS, g_store.count);
}

void test_delete_user(void) {
    clearStore();
    g_randCounter = 0;
    authAddUser(&g_store, "alice", "password1", "admin", testRandFn, testHashFn);
    // emergency_slot = 99 (no slot 0 protection in this test)
    AuthUserResult r = authDeleteUser(&g_store, "alice", 99);
    TEST_ASSERT_EQUAL(AUTH_USER_OK, r);
    TEST_ASSERT_NULL(authFindUser(&g_store, "alice"));
}

void test_slot0_delete_rejected(void) {
    clearStore();
    g_randCounter = 0;
    authAddUser(&g_store, "admin", "password1", "admin", testRandFn, testHashFn);
    // emergency_slot = 0 → delete attempt returns AUTH_USER_PROTECTED
    AuthUserResult r = authDeleteUser(&g_store, "admin", 0);
    TEST_ASSERT_EQUAL(AUTH_USER_PROTECTED, r);
    TEST_ASSERT_NOT_NULL(authFindUser(&g_store, "admin"));
}

void test_slot0_password_change_permitted(void) {
    clearStore();
    g_randCounter = 0;
    authAddUser(&g_store, "admin", "oldpassword", "admin", testRandFn, testHashFn);
    AuthUserResult r = authChangePassword(&g_store, "admin", "newpassword12",
                                           testRandFn, testHashFn);
    TEST_ASSERT_EQUAL(AUTH_USER_OK, r);
    const AuthUser *u = authFindUser(&g_store, "admin");
    TEST_ASSERT_TRUE(authVerifyPassword("newpassword12", u->salt, u->hash, testHashFn));
    TEST_ASSERT_FALSE(authVerifyPassword("oldpassword",  u->salt, u->hash, testHashFn));
}

void test_delete_non_slot0_preserves_slot0_protection(void) {
    clearStore();
    g_randCounter = 0;
    authAddUser(&g_store, "admin",  "password1", "admin", testRandFn, testHashFn);
    authAddUser(&g_store, "bob",    "password1", "admin", testRandFn, testHashFn);
    authAddUser(&g_store, "carol",  "password1", "admin", testRandFn, testHashFn);
    // Delete bob (slot 1) — carol shifts to slot 1, count becomes 2
    TEST_ASSERT_EQUAL(AUTH_USER_OK, authDeleteUser(&g_store, "bob", 0));
    TEST_ASSERT_EQUAL(2, g_store.count);
    // Slot 0 must still be "admin" and protected
    TEST_ASSERT_EQUAL_STRING("admin", g_store.users[0].name);
    TEST_ASSERT_EQUAL(AUTH_USER_PROTECTED, authDeleteUser(&g_store, "admin", 0));
    // Carol is now at slot 1 and deletable
    TEST_ASSERT_EQUAL(AUTH_USER_OK, authDeleteUser(&g_store, "carol", 0));
}

void test_password_below_min_rejected_on_add(void) {
    clearStore();
    g_randCounter = 0;
    AuthUserResult r = authAddUser(&g_store, "alice", "short", "admin",
                                    testRandFn, testHashFn);
    TEST_ASSERT_EQUAL(AUTH_USER_BAD_PASS, r);
    TEST_ASSERT_EQUAL(0, g_store.count);
}

// ── Login fallback logic ──────────────────────────────────────────────────
static int g_adapterCallCount;
static bool g_adapterShouldSucceed;
static bool g_adapterShouldError;

static AdapterResult mockAdapter(const char *username,
                                  const char *password,
                                  char *out_role, void *ctx) {
    g_adapterCallCount++;
    if (g_adapterShouldError)   return ADAPTER_ERROR;
    if (g_adapterShouldSucceed) { strncpy(out_role, "admin", 16); return ADAPTER_OK; }
    return ADAPTER_REJECTED;
}

void test_adapter_success_issues_token(void) {
    clearStore();
    g_adapterCallCount = 0; g_adapterShouldSucceed = true; g_adapterShouldError = false;
    LoginOutcome out = authAttemptLogin("alice", "any", true,
                                        mockAdapter, nullptr,
                                        &g_store, testHashFn);
    TEST_ASSERT_EQUAL(LOGIN_OK,         out.result);
    TEST_ASSERT_EQUAL(AUTH_SRC_ADAPTER, out.source);
    TEST_ASSERT_EQUAL(1, g_adapterCallCount);
}

void test_adapter_rejection_no_nvs_fallthrough(void) {
    clearStore();
    g_randCounter = 0;
    authAddUser(&g_store, "alice", "correctpass", "admin", testRandFn, testHashFn);
    g_adapterCallCount = 0; g_adapterShouldSucceed = false; g_adapterShouldError = false;
    LoginOutcome out = authAttemptLogin("alice", "correctpass", true,
                                        mockAdapter, nullptr,
                                        &g_store, testHashFn);
    // Adapter said REJECTED — must NOT fall through to NVS
    TEST_ASSERT_EQUAL(LOGIN_REJECTED, out.result);
    TEST_ASSERT_EQUAL(1, g_adapterCallCount);
}

void test_adapter_error_falls_through_to_nvs_success(void) {
    clearStore();
    g_randCounter = 0;
    authAddUser(&g_store, "alice", "correctpass", "admin", testRandFn, testHashFn);
    g_adapterCallCount = 0; g_adapterShouldSucceed = false; g_adapterShouldError = true;
    LoginOutcome out = authAttemptLogin("alice", "correctpass", true,
                                        mockAdapter, nullptr,
                                        &g_store, testHashFn);
    TEST_ASSERT_EQUAL(LOGIN_OK,      out.result);
    TEST_ASSERT_EQUAL(AUTH_SRC_NVS,  out.source);
}

void test_adapter_error_falls_through_to_nvs_failure(void) {
    clearStore();
    g_randCounter = 0;
    authAddUser(&g_store, "alice", "correctpass", "admin", testRandFn, testHashFn);
    g_adapterCallCount = 0; g_adapterShouldSucceed = false; g_adapterShouldError = true;
    LoginOutcome out = authAttemptLogin("alice", "wrongpass", true,
                                        mockAdapter, nullptr,
                                        &g_store, testHashFn);
    TEST_ASSERT_EQUAL(LOGIN_REJECTED, out.result);
}

void test_no_adapter_configured_uses_nvs_directly(void) {
    clearStore();
    g_randCounter = 0;
    authAddUser(&g_store, "alice", "correctpass", "admin", testRandFn, testHashFn);
    g_adapterCallCount = 0;
    // adapter_configured = false — mockAdapter must never be called
    LoginOutcome out = authAttemptLogin("alice", "correctpass", false,
                                        mockAdapter, nullptr,
                                        &g_store, testHashFn);
    TEST_ASSERT_EQUAL(LOGIN_OK,     out.result);
    TEST_ASSERT_EQUAL(AUTH_SRC_NVS, out.source);
    TEST_ASSERT_EQUAL(0, g_adapterCallCount);
}

// ── M2 regression: auth layer must accept empty role string ──────────────────
// Guards the web.cpp handler default: new users created without a role field
// in the JSON body must receive "" (no role), never an implicit "admin".
void test_add_user_with_empty_role_stores_empty_role(void) {
    clearStore();
    g_randCounter = 0;
    AuthUserResult r = authAddUser(&g_store, "bob", "password1", "",
                                    testRandFn, testHashFn);
    TEST_ASSERT_EQUAL(AUTH_USER_OK, r);
    const AuthUser *u = authFindUser(&g_store, "bob");
    TEST_ASSERT_NOT_NULL(u);
    TEST_ASSERT_EQUAL_STRING("", u->role);  // must not be "admin"
}

// ── M9 defensive: authVerifyPassword with a corrupted (short) stored hash ──────
// A stored_hash_hex shorter than 64 chars (e.g. from NVS corruption) must
// never validate as correct. The fix adds a strlen pre-check before the
// constant-time XOR loop to prevent reading past the end of the string.
void test_verify_password_short_stored_hash_rejected(void) {
    char salt[33], full_hash[65], short_hash[65];
    g_randCounter = 42;
    authGenerateSalt(salt, testRandFn);
    authHashPassword("mypassword", salt, full_hash, testHashFn);
    // Simulate NVS corruption: only the first 32 of 64 hex chars survived
    memset(short_hash, 0, sizeof(short_hash));
    memcpy(short_hash, full_hash, 32);  // 32 chars + null terminator
    TEST_ASSERT_FALSE(authVerifyPassword("mypassword", salt, short_hash, testHashFn));
}

// ── M9 edge: empty stored hash must always reject ────────────────────────────
void test_verify_password_empty_stored_hash_rejected(void) {
    char salt[33], hash[65];
    g_randCounter = 5;
    authGenerateSalt(salt, testRandFn);
    authHashPassword("password1", salt, hash, testHashFn);
    char empty_hash[65] = "";
    TEST_ASSERT_FALSE(authVerifyPassword("password1", salt, empty_hash, testHashFn));
}

// ── Adapter URL validation tests ────────────────────────────────────────────

void test_adapter_url_empty_is_valid(void) {
    TEST_ASSERT_TRUE(authAdapterUrlValid(""));
    TEST_ASSERT_TRUE(authAdapterUrlValid(nullptr));
}

void test_adapter_url_https_is_valid(void) {
    TEST_ASSERT_TRUE(authAdapterUrlValid("https://auth.example.com/api"));
}

void test_adapter_url_http_rejected(void) {
    TEST_ASSERT_FALSE(authAdapterUrlValid("http://auth.example.com/api"));
}

void test_adapter_url_no_scheme_rejected(void) {
    TEST_ASSERT_FALSE(authAdapterUrlValid("auth.example.com/api"));
}

void test_adapter_url_ftp_rejected(void) {
    TEST_ASSERT_FALSE(authAdapterUrlValid("ftp://auth.example.com/api"));
}

// ── Rate limiter tests ──────────────────────────────────────────────────────

static RateLimiter g_rl;
static void clearRateLimiter(void) { memset(&g_rl, 0, sizeof(g_rl)); }

void test_rate_limit_not_locked_initially(void) {
    clearRateLimiter();
    TEST_ASSERT_FALSE(rateLimitIsLocked(&g_rl, 0x12345678, 1000));
}

void test_rate_limit_not_locked_after_few_failures(void) {
    clearRateLimiter();
    uint32_t ip = authIpHash(0xC0A80101);  // 192.168.1.1
    for (int i = 0; i < AUTH_RATE_LIMIT_MAX_FAILURES - 1; i++) {
        TEST_ASSERT_FALSE(rateLimitRecordFailure(&g_rl, ip, 1000 + i * 100));
    }
    TEST_ASSERT_FALSE(rateLimitIsLocked(&g_rl, ip, 1500));
}

void test_rate_limit_locked_after_max_failures(void) {
    clearRateLimiter();
    uint32_t ip = authIpHash(0xC0A80101);
    for (int i = 0; i < AUTH_RATE_LIMIT_MAX_FAILURES - 1; i++) {
        rateLimitRecordFailure(&g_rl, ip, 1000 + i * 100);
    }
    // The Nth failure should trigger lockout
    TEST_ASSERT_TRUE(rateLimitRecordFailure(&g_rl, ip, 1500));
    TEST_ASSERT_TRUE(rateLimitIsLocked(&g_rl, ip, 2000));
}

void test_rate_limit_lockout_expires(void) {
    clearRateLimiter();
    uint32_t ip = authIpHash(0xC0A80101);
    for (int i = 0; i < AUTH_RATE_LIMIT_MAX_FAILURES; i++) {
        rateLimitRecordFailure(&g_rl, ip, 1000 + i * 100);
    }
    TEST_ASSERT_TRUE(rateLimitIsLocked(&g_rl, ip, 2000));
    // After lockout period expires
    TEST_ASSERT_FALSE(rateLimitIsLocked(&g_rl, ip, 1000 + AUTH_RATE_LIMIT_LOCKOUT_MS + 1000));
}

void test_rate_limit_different_ips_independent(void) {
    clearRateLimiter();
    uint32_t ip1 = authIpHash(0xC0A80101);
    uint32_t ip2 = authIpHash(0xC0A80102);
    // Lock out IP1
    for (int i = 0; i < AUTH_RATE_LIMIT_MAX_FAILURES; i++) {
        rateLimitRecordFailure(&g_rl, ip1, 1000 + i * 100);
    }
    TEST_ASSERT_TRUE(rateLimitIsLocked(&g_rl, ip1, 2000));
    // IP2 should not be locked
    TEST_ASSERT_FALSE(rateLimitIsLocked(&g_rl, ip2, 2000));
}

void test_rate_limit_clear_on_success(void) {
    clearRateLimiter();
    uint32_t ip = authIpHash(0xC0A80101);
    // Record some failures (but not enough to lock out)
    for (int i = 0; i < AUTH_RATE_LIMIT_MAX_FAILURES - 1; i++) {
        rateLimitRecordFailure(&g_rl, ip, 1000 + i * 100);
    }
    // Successful login clears failures
    rateLimitClear(&g_rl, ip);
    // Should now take full MAX_FAILURES again to lock out
    for (int i = 0; i < AUTH_RATE_LIMIT_MAX_FAILURES - 1; i++) {
        TEST_ASSERT_FALSE(rateLimitRecordFailure(&g_rl, ip, 5000 + i * 100));
    }
    TEST_ASSERT_TRUE(rateLimitRecordFailure(&g_rl, ip, 5500));
}

void test_rate_limit_old_failures_expire_from_window(void) {
    clearRateLimiter();
    uint32_t ip = authIpHash(0xC0A80101);
    // Record 4 failures at time 1000
    for (int i = 0; i < AUTH_RATE_LIMIT_MAX_FAILURES - 1; i++) {
        rateLimitRecordFailure(&g_rl, ip, 1000);
    }
    // Time advances past the window
    uint32_t afterWindow = 1000 + AUTH_RATE_LIMIT_WINDOW_MS + 1;
    // One more failure should NOT trigger lockout (old failures expired)
    TEST_ASSERT_FALSE(rateLimitRecordFailure(&g_rl, ip, afterWindow));
}

void test_rate_limit_slot_eviction(void) {
    clearRateLimiter();
    // Fill all slots
    for (int i = 0; i < AUTH_RATE_LIMIT_SLOTS; i++) {
        uint32_t ip = authIpHash(0xC0A80100 + i);
        rateLimitRecordFailure(&g_rl, ip, 1000 + i * 1000);
    }
    // Adding one more should evict oldest and still work
    uint32_t newIp = authIpHash(0xC0A80200);
    TEST_ASSERT_FALSE(rateLimitRecordFailure(&g_rl, newIp, 50000));
    // New IP should be tracked
    rateLimitRecordFailure(&g_rl, newIp, 50001);
    // Should not crash or lose state
}

void test_influx_log_event_fields(void) {
    AuthLogEvent ev = authBuildLogEvent("login_success", "alice",
                                         "192.168.1.50", "nvs");
    TEST_ASSERT_EQUAL_STRING("login_success", ev.event);
    TEST_ASSERT_EQUAL_STRING("alice",         ev.username);
    TEST_ASSERT_EQUAL_STRING("192.168.1.50",  ev.client_ip);
    TEST_ASSERT_EQUAL_STRING("nvs",           ev.auth_source);
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
    RUN_TEST(test_short_token_rejected);
    RUN_TEST(test_issue_token_populates_slot);
    RUN_TEST(test_issued_token_validates);
    RUN_TEST(test_wrong_token_rejected);
    RUN_TEST(test_expired_token_rejected);
    RUN_TEST(test_expiry_across_millis_rollover);
    RUN_TEST(test_logout_invalidates_token);
    RUN_TEST(test_expired_slot_reclaimed_before_valid);
    RUN_TEST(test_oldest_valid_evicted_when_all_full);
    RUN_TEST(test_add_user_and_find);
    RUN_TEST(test_max_users_enforced);
    RUN_TEST(test_delete_user);
    RUN_TEST(test_slot0_delete_rejected);
    RUN_TEST(test_slot0_password_change_permitted);
    RUN_TEST(test_delete_non_slot0_preserves_slot0_protection);
    RUN_TEST(test_password_below_min_rejected_on_add);
    RUN_TEST(test_adapter_success_issues_token);
    RUN_TEST(test_adapter_rejection_no_nvs_fallthrough);
    RUN_TEST(test_adapter_error_falls_through_to_nvs_success);
    RUN_TEST(test_adapter_error_falls_through_to_nvs_failure);
    RUN_TEST(test_no_adapter_configured_uses_nvs_directly);
    RUN_TEST(test_influx_log_event_fields);
    RUN_TEST(test_add_user_with_empty_role_stores_empty_role);
    RUN_TEST(test_verify_password_short_stored_hash_rejected);
    RUN_TEST(test_verify_password_empty_stored_hash_rejected);
    // Adapter URL validation
    RUN_TEST(test_adapter_url_empty_is_valid);
    RUN_TEST(test_adapter_url_https_is_valid);
    RUN_TEST(test_adapter_url_http_rejected);
    RUN_TEST(test_adapter_url_no_scheme_rejected);
    RUN_TEST(test_adapter_url_ftp_rejected);
    // Rate limiter tests
    RUN_TEST(test_rate_limit_not_locked_initially);
    RUN_TEST(test_rate_limit_not_locked_after_few_failures);
    RUN_TEST(test_rate_limit_locked_after_max_failures);
    RUN_TEST(test_rate_limit_lockout_expires);
    RUN_TEST(test_rate_limit_different_ips_independent);
    RUN_TEST(test_rate_limit_clear_on_success);
    RUN_TEST(test_rate_limit_old_failures_expire_from_window);
    RUN_TEST(test_rate_limit_slot_eviction);
    return UNITY_END();
}
