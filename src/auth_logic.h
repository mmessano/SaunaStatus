#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

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
#define AUTH_MAX_PASS_LEN   72   // max password accepted — prevents stack overflow in hash buffer
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
    char     role[17];     // "admin" + null
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
