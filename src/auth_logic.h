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
