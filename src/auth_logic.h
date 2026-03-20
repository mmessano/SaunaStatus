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
    char name[33];
    char hash[65];  // SHA-256 hex, 64 chars + null
    char salt[33];  // 16-byte salt hex, 32 chars + null
    char role[17];
    bool active;
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
    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = HEX[(bytes[i] >> 4) & 0xF];
        out[i * 2 + 1] = HEX[bytes[i] & 0xF];
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

// Hashes (salt_hex decoded bytes) || (password bytes) using hash_fn
// Stores result as hex in out_hash[65]
inline void authHashPassword(const char *password,
                              const char *salt_hex,
                              char *out_hash,
                              AuthHashFn hash_fn) {
    uint8_t salt_bytes[16];
    authHexToBytes(salt_hex, salt_bytes, 16);
    size_t pass_len = strlen(password);
    // Concatenate salt_bytes + password bytes into a buffer
    uint8_t buf[16 + AUTH_MAX_PASS_LEN];  // salt(16) + password bytes; authPasswordLengthOk enforces max
    size_t buf_len = 16 + pass_len;
    memcpy(buf, salt_bytes, 16);
    memcpy(buf + 16, password, pass_len);
    uint8_t digest[32];
    hash_fn(buf, buf_len, digest);
    authBytesToHex(digest, 32, out_hash);
}

// Returns true if hash_fn(salt || password) matches stored_hash_hex
inline bool authVerifyPassword(const char *password,
                                const char *salt_hex,
                                const char *stored_hash_hex,
                                AuthHashFn hash_fn) {
    char computed[65];
    authHashPassword(password, salt_hex, computed, hash_fn);
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
    if (!authPasswordLengthOk(password)) return AUTH_USER_BAD_PASS;
    if (store->count >= AUTH_MAX_USERS)  return AUTH_USER_FULL;
    if (authFindUser(store, username))   return AUTH_USER_EXISTS;
    AuthUser &u = store->users[store->count];
    memset(&u, 0, sizeof(AuthUser));
    strncpy(u.name, username, 32); u.name[32] = '\0';
    strncpy(u.role, role,     16); u.role[16] = '\0';
    authGenerateSalt(u.salt, rand_fn);
    authHashPassword(password, u.salt, u.hash, hash_fn);
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
        authHashPassword(new_password, store->users[i].salt,
                         store->users[i].hash, hash_fn);
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
        if (ar == ADAPTER_OK)       { out.result = LOGIN_OK;       out.source = AUTH_SRC_ADAPTER; return out; }
        if (ar == ADAPTER_REJECTED) { out.result = LOGIN_REJECTED; out.source = AUTH_SRC_ADAPTER; return out; }
        // ADAPTER_ERROR — fall through to NVS
    }
    const AuthUser *u = authFindUser(store, username);
    if (u && authVerifyPassword(password, u->salt, u->hash, hash_fn)) {
        out.result = LOGIN_OK;
        out.source = AUTH_SRC_NVS;
        strncpy(out.role, u->role, 16); out.role[16] = '\0';
    } else {
        out.result = LOGIN_REJECTED;
        out.source = AUTH_SRC_NVS;
    }
    return out;
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
