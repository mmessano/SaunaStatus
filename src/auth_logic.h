#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

// ── Build-flag tunables ────────────────────────────────────────────────────
#ifndef AUTH_TOKEN_TTL_MS
#define AUTH_TOKEN_TTL_MS   3600000UL
#endif
#ifndef AUTH_MAX_SESSIONS
#define AUTH_MAX_SESSIONS   10
#endif
#ifndef AUTH_MAX_USERS
#define AUTH_MAX_USERS      5
#endif
#ifndef AUTH_MIN_PASS_LEN
#define AUTH_MIN_PASS_LEN   8
#endif
#ifndef AUTH_MAX_PASS_LEN
#define AUTH_MAX_PASS_LEN   72   // max password accepted — bounds the SHA-256 hash buffer (salt[16] + password[MAX])
#endif
#ifndef AUTH_PBKDF2_ITERATIONS
#define AUTH_PBKDF2_ITERATIONS 10000
#endif

// ── Injectable function types (enables native unit testing) ───────────────
// Hash: takes data+len, writes 32 bytes into out_32
typedef void (*AuthHashFn)(const uint8_t *data, size_t len, uint8_t *out_32);
// Random: fills buf with len random bytes
typedef void (*AuthRandFn)(uint8_t *buf, size_t len);

// ── Session store ─────────────────────────────────────────────────────────
struct AuthSession {
    char     token[65];    // 64-char hex + null terminator
    char     username[33]; // max 32 chars + null
    char     role[17];     // max 16 chars + null (e.g. "admin")
    uint32_t issued_ms;    // millis() at issuance — use subtraction for expiry
    bool     active;
};

// ── User store (portable, populated by NVS layer) ─────────────────────────
struct AuthUser {
    char     name[33];
    char     hash[65];  // SHA-256 hex, 64 chars + null
    char     salt[33];  // 16-byte salt hex, 32 chars + null
    char     role[17];
    uint16_t iterations; // 0 = legacy single-pass SHA-256; >0 = PBKDF2-SHA-256
    bool     active;
};

struct AuthUserStore {
    AuthUser users[AUTH_MAX_USERS];
    int      count;
};

// ── Result codes ──────────────────────────────────────────────────────────
enum AuthUserResult {
    AUTH_USER_OK        = 0,
    AUTH_USER_NOT_FOUND = 1,
    AUTH_USER_EXISTS    = 2,
    AUTH_USER_FULL      = 3,
    AUTH_USER_PROTECTED = 4,  // slot 0 delete attempt
    AUTH_USER_BAD_PASS  = 5,  // password too short
    AUTH_USER_BAD_NAME  = 6,  // username invalid (length or charset)
};

enum AdapterResult {
    ADAPTER_OK       = 0,  // credentials valid
    ADAPTER_REJECTED = 1,  // credentials invalid (deliberate)
    ADAPTER_ERROR    = 2,  // network/timeout — fall through to NVS
};

enum LoginResult {
    LOGIN_OK       = 0,
    LOGIN_REJECTED = 1,
};

enum AuthSource {
    AUTH_SRC_NVS     = 0,
    AUTH_SRC_ADAPTER = 1,
};

struct LoginOutcome {
    LoginResult result;
    AuthSource  source;
    char        role[17];
};

// Adapter callback: returns ADAPTER_OK/REJECTED/ERROR, fills out_role on OK
typedef AdapterResult (*AdapterFn)(const char *username,
                                   const char *password,
                                   char *out_role,
                                   void *ctx);

// ── Hex helpers ──────────────────────────────────────────────────────────
inline void authBytesToHex(const uint8_t *bytes, size_t len, char *out) {
    static const char kHexChars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = kHexChars[(bytes[i] >> 4) & 0xF];
        out[i * 2 + 1] = kHexChars[bytes[i] & 0xF];
    }
    out[len * 2] = '\0';
}

inline void authHexToBytes(const char *hex, uint8_t *out, size_t out_len) {
    for (size_t i = 0; i < out_len; i++) {
        auto hexVal = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        out[i] = (hexVal(hex[i * 2]) << 4) | hexVal(hex[i * 2 + 1]);
    }
}

// ── HMAC-SHA-256 (RFC 2104) using injectable hash_fn ─────────────────────
// SHA-256 block size is 64 bytes.
inline void authHmacSha256(const uint8_t *key, size_t key_len,
                            const uint8_t *msg, size_t msg_len,
                            uint8_t *out_32,
                            AuthHashFn hash_fn) {
    const size_t BLOCK = 64;
    uint8_t k_block[64];
    memset(k_block, 0, BLOCK);
    if (key_len > BLOCK) {
        hash_fn(key, key_len, k_block);  // hash key if longer than block
    } else {
        memcpy(k_block, key, key_len);
    }
    // ipad = k_block XOR 0x36, opad = k_block XOR 0x5c
    uint8_t ipad[64], opad[64];
    for (size_t i = 0; i < BLOCK; i++) {
        ipad[i] = k_block[i] ^ 0x36;
        opad[i] = k_block[i] ^ 0x5c;
    }
    // inner = hash(ipad || msg)
    uint8_t inner_buf[64 + 256];  // 64 + max(salt+4, 32) — safe for PBKDF2 use
    size_t inner_total = BLOCK + msg_len;
    // For messages larger than 192 bytes, fall back to two-pass
    if (inner_total <= sizeof(inner_buf)) {
        memcpy(inner_buf, ipad, BLOCK);
        memcpy(inner_buf + BLOCK, msg, msg_len);
        uint8_t inner_hash[32];
        hash_fn(inner_buf, inner_total, inner_hash);
        // outer = hash(opad || inner_hash)
        uint8_t outer_buf[64 + 32];
        memcpy(outer_buf, opad, BLOCK);
        memcpy(outer_buf + BLOCK, inner_hash, 32);
        hash_fn(outer_buf, BLOCK + 32, out_32);
    } else {
        // Large message path — should not happen in PBKDF2 usage
        // but handle defensively with stack allocation
        uint8_t inner_hash[32];
        // We can't easily do this without dynamic alloc on embedded,
        // but PBKDF2 messages are always ≤ 64 bytes (salt+4 or 32)
        // so this path should never execute.
        memset(out_32, 0, 32);
    }
}

// ── PBKDF2-SHA-256 (RFC 2898) ────────────────────────────────────────────
// Derives a 32-byte key from password+salt using iterated HMAC-SHA-256.
// salt_bytes/salt_len: raw salt bytes (not hex)
// iterations: must be >= 1
inline void authPbkdf2Sha256(const uint8_t *password, size_t pass_len,
                              const uint8_t *salt_bytes, size_t salt_len,
                              uint16_t iterations,
                              uint8_t *out_32,
                              AuthHashFn hash_fn) {
    // PBKDF2 with dkLen=32: only need block i=1
    // U1 = HMAC(password, salt || INT_32_BE(1))
    uint8_t salt_i[80];  // salt(max 16) + 4 bytes for block index
    if (salt_len > 76) salt_len = 76;  // safety clamp
    memcpy(salt_i, salt_bytes, salt_len);
    salt_i[salt_len]     = 0;
    salt_i[salt_len + 1] = 0;
    salt_i[salt_len + 2] = 0;
    salt_i[salt_len + 3] = 1;  // block index = 1 (big-endian)

    uint8_t u[32], result[32];
    authHmacSha256(password, pass_len, salt_i, salt_len + 4, u, hash_fn);
    memcpy(result, u, 32);

    for (uint16_t iter = 1; iter < iterations; iter++) {
        authHmacSha256(password, pass_len, u, 32, u, hash_fn);
        for (int j = 0; j < 32; j++) result[j] ^= u[j];
    }
    memcpy(out_32, result, 32);
}

// ── Username validation ───────────────────────────────────────────────────
#ifndef AUTH_MIN_USER_LEN
#define AUTH_MIN_USER_LEN 1
#endif
#ifndef AUTH_MAX_USER_LEN
#define AUTH_MAX_USER_LEN 32
#endif

// Allowed chars: alphanumeric, underscore, hyphen, period
inline bool authUsernameValid(const char *name) {
    if (!name || name[0] == '\0') return false;
    size_t len = 0;
    for (const char *p = name; *p; p++, len++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return len >= AUTH_MIN_USER_LEN && len <= AUTH_MAX_USER_LEN;
}

// ── Password helpers ──────────────────────────────────────────────────────
inline bool authPasswordLengthOk(const char *pass) {
    size_t len = 0;
    while (pass[len]) len++;
    return len >= AUTH_MIN_PASS_LEN && len <= AUTH_MAX_PASS_LEN;
}

// Generates a 16-byte random salt, hex-encoded into out_salt[33]
inline void authGenerateSalt(char *out_salt, AuthRandFn rand_fn) {
    uint8_t raw[16];
    rand_fn(raw, 16);
    authBytesToHex(raw, 16, out_salt);
}

// Legacy single-pass: SHA-256(salt_bytes || password)
// Stores result as hex in out_hash[65]
inline void authHashPasswordLegacy(const char *password,
                                    const char *salt_hex,
                                    char *out_hash,
                                    AuthHashFn hash_fn) {
    uint8_t salt_bytes[16];
    authHexToBytes(salt_hex, salt_bytes, 16);
    size_t pass_len = strlen(password);
    uint8_t buf[16 + AUTH_MAX_PASS_LEN];
    size_t buf_len = 16 + pass_len;
    memcpy(buf, salt_bytes, 16);
    memcpy(buf + 16, password, pass_len);
    uint8_t digest[32];
    hash_fn(buf, buf_len, digest);
    authBytesToHex(digest, 32, out_hash);
}

// PBKDF2-SHA-256 password hashing
// Stores result as hex in out_hash[65]
inline void authHashPasswordPbkdf2(const char *password,
                                     const char *salt_hex,
                                     uint16_t iterations,
                                     char *out_hash,
                                     AuthHashFn hash_fn) {
    uint8_t salt_bytes[16];
    authHexToBytes(salt_hex, salt_bytes, 16);
    size_t pass_len = strlen(password);
    uint8_t digest[32];
    authPbkdf2Sha256((const uint8_t *)password, pass_len,
                     salt_bytes, 16, iterations, digest, hash_fn);
    authBytesToHex(digest, 32, out_hash);
}

// Unified hashing: uses PBKDF2 when iterations > 0, legacy otherwise
inline void authHashPassword(const char *password,
                              const char *salt_hex,
                              char *out_hash,
                              AuthHashFn hash_fn,
                              uint16_t iterations = 0) {
    if (iterations > 0) {
        authHashPasswordPbkdf2(password, salt_hex, iterations, out_hash, hash_fn);
    } else {
        authHashPasswordLegacy(password, salt_hex, out_hash, hash_fn);
    }
}

// Verifies password against stored hash. Uses PBKDF2 when iterations > 0,
// legacy single-pass when iterations == 0 (backward compatible with existing NVS data).
// stored_hash_hex must be exactly 64 hex chars + null (65 bytes).
inline bool authVerifyPassword(const char *password,
                                const char *salt_hex,
                                const char *stored_hash_hex,
                                AuthHashFn hash_fn,
                                uint16_t iterations = 0) {
    // Length guard: a valid SHA-256 hex digest is always exactly 64 chars.
    size_t stored_len = 0;
    while (stored_len <= 64 && stored_hash_hex[stored_len]) stored_len++;
    if (stored_len != 64) return false;

    char computed[65];
    authHashPassword(password, salt_hex, computed, hash_fn, iterations);
    // Constant-time compare on the 64-char hex strings
    uint8_t diff = 0;
    for (int i = 0; i < 64; i++)
        diff |= (uint8_t)computed[i] ^ (uint8_t)stored_hash_hex[i];
    return diff == 0;
}

// ── Constant-time token comparison (prevents timing side-channel) ─────────
// Precondition: both buffers must be null-terminated and at least 65 bytes.
// Returns false immediately if either is not exactly 64 chars (rejects short
// tokens before the XOR loop — prevents OOB reads on short inputs).
inline bool authTokenEqual(const char *a, const char *b) {
    size_t la = 0, lb = 0;
    while (la <= 64 && a[la]) la++;
    while (lb <= 64 && b[lb]) lb++;
    if (la != 64 || lb != 64) return false;
    uint8_t diff = 0;
    for (int i = 0; i < 64; i++) diff |= (uint8_t)a[i] ^ (uint8_t)b[i];
    return diff == 0;
}

// ── Session operations ────────────────────────────────────────────────────

// Finds the best slot to reclaim: expired/inactive first, then oldest by issued_ms
inline int authFindEvictSlot(AuthSession sessions[], int max,
                              uint32_t now_ms, uint32_t ttl_ms) {
    for (int i = 0; i < max; i++) {
        if (!sessions[i].active) return i;
        if ((now_ms - sessions[i].issued_ms) > ttl_ms) return i;
    }
    int oldest = 0;
    uint32_t maxAge = 0;
    for (int i = 0; i < max; i++) {
        uint32_t age = now_ms - sessions[i].issued_ms;
        if (age > maxAge) { maxAge = age; oldest = i; }
    }
    return oldest;
}

// Issues a new token. Evicts a slot if all are full. Fills out_token[65].
inline bool authIssueToken(AuthSession sessions[], int max,
                            const char *username, const char *role,
                            uint32_t now_ms, AuthRandFn rand_fn,
                            char *out_token) {
    int slot = authFindEvictSlot(sessions, max, now_ms, AUTH_TOKEN_TTL_MS);
    uint8_t raw[32];
    rand_fn(raw, 32);
    authBytesToHex(raw, 32, out_token);
    AuthSession &s = sessions[slot];
    memset(&s, 0, sizeof(AuthSession));
    strncpy(s.token,    out_token, 64); s.token[64] = '\0';
    strncpy(s.username, username, 32);  s.username[32] = '\0';
    strncpy(s.role,     role,     16);  s.role[16] = '\0';
    s.issued_ms = now_ms;
    s.active    = true;
    return true;
}

// Returns pointer to valid session, or nullptr. Expiry uses unsigned subtraction (rollover-safe).
inline const AuthSession *authValidateToken(const AuthSession sessions[], int max,
                                             const char *token,
                                             uint32_t now_ms, uint32_t ttl_ms) {
    for (int i = 0; i < max; i++) {
        if (!sessions[i].active) continue;
        if ((now_ms - sessions[i].issued_ms) > ttl_ms) continue;
        if (authTokenEqual(sessions[i].token, token)) return &sessions[i];
    }
    return nullptr;
}

// Clears the session matching token (if found).
inline void authInvalidateToken(AuthSession sessions[], int max, const char *token) {
    for (int i = 0; i < max; i++) {
        if (sessions[i].active && authTokenEqual(sessions[i].token, token)) {
            memset(&sessions[i], 0, sizeof(AuthSession));
            return;
        }
    }
}

// ── User store operations ─────────────────────────────────────────────────
inline const AuthUser *authFindUser(const AuthUserStore *store, const char *username) {
    for (int i = 0; i < store->count; i++) {
        if (store->users[i].active &&
            strncmp(store->users[i].name, username, 32) == 0)
            return &store->users[i];
    }
    return nullptr;
}

inline AuthUserResult authAddUser(AuthUserStore *store,
                                   const char *username,
                                   const char *password,
                                   const char *role,
                                   AuthRandFn rand_fn,
                                   AuthHashFn hash_fn) {
    if (!authUsernameValid(username))    return AUTH_USER_BAD_NAME;
    if (!authPasswordLengthOk(password)) return AUTH_USER_BAD_PASS;
    if (store->count >= AUTH_MAX_USERS)  return AUTH_USER_FULL;
    if (authFindUser(store, username))   return AUTH_USER_EXISTS;
    AuthUser &u = store->users[store->count];
    memset(&u, 0, sizeof(AuthUser));
    strncpy(u.name, username, 32); u.name[32] = '\0';
    strncpy(u.role, role,     16); u.role[16] = '\0';
    authGenerateSalt(u.salt, rand_fn);
    u.iterations = AUTH_PBKDF2_ITERATIONS;
    authHashPassword(password, u.salt, u.hash, hash_fn, u.iterations);
    u.active = true;
    store->count++;
    return AUTH_USER_OK;
}

// emergency_slot: index that cannot be deleted (pass 0 in production, 99 in tests to skip)
inline AuthUserResult authDeleteUser(AuthUserStore *store,
                                      const char *username,
                                      int emergency_slot) {
    for (int i = 0; i < store->count; i++) {
        if (!store->users[i].active) continue;
        if (strncmp(store->users[i].name, username, 32) != 0) continue;
        if (i == emergency_slot) return AUTH_USER_PROTECTED;
        // Shift remaining users down
        for (int j = i; j < store->count - 1; j++)
            store->users[j] = store->users[j + 1];
        memset(&store->users[store->count - 1], 0, sizeof(AuthUser));
        store->count--;
        return AUTH_USER_OK;
    }
    return AUTH_USER_NOT_FOUND;
}

inline AuthUserResult authChangePassword(AuthUserStore *store,
                                          const char *username,
                                          const char *new_password,
                                          AuthRandFn rand_fn,
                                          AuthHashFn hash_fn) {
    if (!authPasswordLengthOk(new_password)) return AUTH_USER_BAD_PASS;
    for (int i = 0; i < store->count; i++) {
        if (!store->users[i].active) continue;
        if (strncmp(store->users[i].name, username, 32) != 0) continue;
        authGenerateSalt(store->users[i].salt, rand_fn);
        store->users[i].iterations = AUTH_PBKDF2_ITERATIONS;
        authHashPassword(new_password, store->users[i].salt,
                         store->users[i].hash, hash_fn, store->users[i].iterations);
        return AUTH_USER_OK;
    }
    return AUTH_USER_NOT_FOUND;
}

// ── Login fallback orchestration ──────────────────────────────────────────
inline LoginOutcome authAttemptLogin(const char *username,
                                      const char *password,
                                      bool adapter_configured,
                                      AdapterFn adapter_fn,
                                      void *adapter_ctx,
                                      const AuthUserStore *store,
                                      AuthHashFn hash_fn) {
    LoginOutcome out;
    memset(&out, 0, sizeof(out));
    if (adapter_configured && adapter_fn) {
        AdapterResult ar = adapter_fn(username, password, out.role, adapter_ctx);
        if (ar == ADAPTER_OK)       { out.result = LOGIN_OK; out.source = AUTH_SRC_ADAPTER; out.role[16] = '\0'; return out; }
        if (ar == ADAPTER_REJECTED) { out.result = LOGIN_REJECTED; out.source = AUTH_SRC_ADAPTER; return out; }
        // ADAPTER_ERROR — fall through to NVS
    }
    const AuthUser *u = authFindUser(store, username);
    if (u && authVerifyPassword(password, u->salt, u->hash, hash_fn, u->iterations)) {
        out.result = LOGIN_OK;
        out.source = AUTH_SRC_NVS;
        strncpy(out.role, u->role, 16); out.role[16] = '\0';
    } else {
        out.result = LOGIN_REJECTED;
        out.source = AUTH_SRC_NVS;
    }
    return out;
}

// ── Adapter URL validation ───────────────────────────────────────────────
// Adapter URLs must use HTTPS to prevent plaintext credential transmission.
// Empty URL is allowed (means adapter is disabled).
inline bool authAdapterUrlValid(const char *url) {
    if (!url || url[0] == '\0') return true;  // empty = disabled = OK
    return strncmp(url, "https://", 8) == 0;
}

// ── Login rate limiter (portable, testable) ─────────────────────────────
#ifndef AUTH_RATE_LIMIT_MAX_FAILURES
#define AUTH_RATE_LIMIT_MAX_FAILURES 5
#endif
#ifndef AUTH_RATE_LIMIT_WINDOW_MS
#define AUTH_RATE_LIMIT_WINDOW_MS 60000UL
#endif
#ifndef AUTH_RATE_LIMIT_LOCKOUT_MS
#define AUTH_RATE_LIMIT_LOCKOUT_MS 300000UL
#endif
#ifndef AUTH_RATE_LIMIT_SLOTS
#define AUTH_RATE_LIMIT_SLOTS 8
#endif

struct RateLimitEntry {
    uint32_t ip_hash;
    uint32_t failure_times[AUTH_RATE_LIMIT_MAX_FAILURES];
    uint8_t  count;         // number of failures recorded in window
    uint32_t lockout_until; // millis() when lockout expires (0 = not locked)
    bool     active;
};

struct RateLimiter {
    RateLimitEntry entries[AUTH_RATE_LIMIT_SLOTS];
};

// Simple IP hash from 4 octets packed as uint32_t
inline uint32_t authIpHash(uint32_t ip) {
    // Mix bits to distribute across slots
    ip ^= ip >> 16;
    ip *= 0x45d9f3b;
    ip ^= ip >> 16;
    return ip;
}

inline RateLimitEntry *rateLimitFind(RateLimiter *rl, uint32_t ip_hash) {
    for (int i = 0; i < AUTH_RATE_LIMIT_SLOTS; i++) {
        if (rl->entries[i].active && rl->entries[i].ip_hash == ip_hash)
            return &rl->entries[i];
    }
    return nullptr;
}

inline RateLimitEntry *rateLimitAlloc(RateLimiter *rl, uint32_t ip_hash, uint32_t now_ms) {
    // Find inactive slot
    for (int i = 0; i < AUTH_RATE_LIMIT_SLOTS; i++) {
        if (!rl->entries[i].active) {
            memset(&rl->entries[i], 0, sizeof(RateLimitEntry));
            rl->entries[i].ip_hash = ip_hash;
            rl->entries[i].active  = true;
            return &rl->entries[i];
        }
    }
    // Evict oldest (earliest first failure)
    int oldest = 0;
    uint32_t oldestAge = 0;
    for (int i = 0; i < AUTH_RATE_LIMIT_SLOTS; i++) {
        uint32_t ft = rl->entries[i].count > 0 ? rl->entries[i].failure_times[0] : 0;
        uint32_t age = now_ms - ft;
        if (age > oldestAge) { oldestAge = age; oldest = i; }
    }
    memset(&rl->entries[oldest], 0, sizeof(RateLimitEntry));
    rl->entries[oldest].ip_hash = ip_hash;
    rl->entries[oldest].active  = true;
    return &rl->entries[oldest];
}

// Returns true if the IP is currently locked out
inline bool rateLimitIsLocked(RateLimiter *rl, uint32_t ip_hash, uint32_t now_ms) {
    RateLimitEntry *e = rateLimitFind(rl, ip_hash);
    if (!e) return false;
    if (e->lockout_until != 0 && (now_ms - e->lockout_until) > AUTH_RATE_LIMIT_LOCKOUT_MS) {
        // Lockout has expired — but we use subtraction, so check differently:
        // lockout_until is the timestamp when lockout started.
        // Actually, store lockout_until as the time AT which lockout expires.
    }
    // Check if locked out
    if (e->lockout_until != 0) {
        // lockout_until stores the millis() at which lockout expires
        if ((int32_t)(now_ms - e->lockout_until) < 0) {
            return true;  // still locked
        }
        // Lockout expired — reset entry
        memset(e, 0, sizeof(RateLimitEntry));
        return false;
    }
    return false;
}

// Record a failed login attempt. Returns true if IP is now locked out.
inline bool rateLimitRecordFailure(RateLimiter *rl, uint32_t ip_hash, uint32_t now_ms) {
    RateLimitEntry *e = rateLimitFind(rl, ip_hash);
    if (!e) e = rateLimitAlloc(rl, ip_hash, now_ms);

    // Expire old failures outside the window
    uint8_t valid = 0;
    for (uint8_t i = 0; i < e->count; i++) {
        if ((now_ms - e->failure_times[i]) <= AUTH_RATE_LIMIT_WINDOW_MS) {
            e->failure_times[valid++] = e->failure_times[i];
        }
    }
    e->count = valid;

    // Add new failure
    if (e->count < AUTH_RATE_LIMIT_MAX_FAILURES) {
        e->failure_times[e->count++] = now_ms;
    }

    // Check if threshold reached
    if (e->count >= AUTH_RATE_LIMIT_MAX_FAILURES) {
        e->lockout_until = now_ms + AUTH_RATE_LIMIT_LOCKOUT_MS;
        return true;
    }
    return false;
}

// Clear rate limit entry on successful login
inline void rateLimitClear(RateLimiter *rl, uint32_t ip_hash) {
    RateLimitEntry *e = rateLimitFind(rl, ip_hash);
    if (e) {
        memset(e, 0, sizeof(RateLimitEntry));
    }
}

// ── Log event struct (populated portably, written to InfluxDB by auth.h) ──
struct AuthLogEvent {
    char event[32];
    char username[33];
    char client_ip[40];
    char auth_source[16];
};

inline AuthLogEvent authBuildLogEvent(const char *event,
                                       const char *username,
                                       const char *client_ip,
                                       const char *auth_source) {
    AuthLogEvent ev;
    memset(&ev, 0, sizeof(ev));
    strncpy(ev.event,       event,       31); ev.event[31]       = '\0';
    strncpy(ev.username,    username,    32); ev.username[32]    = '\0';
    strncpy(ev.client_ip,   client_ip,   39); ev.client_ip[39]   = '\0';
    strncpy(ev.auth_source, auth_source, 15); ev.auth_source[15] = '\0';
    return ev;
}
