#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

// =============================================================================
// Firmware version — parse and compare
// =============================================================================

struct FirmwareVersion {
    uint8_t major = 0;
    uint8_t minor = 0;
    uint8_t patch = 0;
    bool    valid = false;
};

// Parse "major.minor.patch" string. Returns valid=false on any parse error.
inline FirmwareVersion parseVersion(const char *str) {
    FirmwareVersion v;
    if (!str || !*str) return v;
    int maj, min, pat;
    // Require exactly 3 dot-separated non-negative integers.
    // sscanf alone can't reject trailing garbage, so verify with a manual walk.
    if (sscanf(str, "%d.%d.%d", &maj, &min, &pat) != 3) return v;
    if (maj < 0 || min < 0 || pat < 0) return v;
    if (maj > 255 || min > 255 || pat > 255) return v;
    // Reject inputs like "1.2." or "1..3" by checking the character layout
    const char *p = str;
    while (*p && (*p == '.' || (*p >= '0' && *p <= '9'))) p++;
    if (*p != '\0') return v;  // trailing non-numeric garbage
    v.major = (uint8_t)maj;
    v.minor = (uint8_t)min;
    v.patch = (uint8_t)pat;
    v.valid = true;
    return v;
}

// Compare two versions. Returns: -1 (a < b), 0 (a == b), 1 (a > b).
// Behaviour is undefined if either version is !valid.
inline int compareVersion(const FirmwareVersion &a, const FirmwareVersion &b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

// Returns true if manifest version is strictly newer than current.
// Both versions must be valid; invalid versions are never considered updates.
inline bool isUpdateAvailable(const FirmwareVersion &current,
                               const FirmwareVersion &manifest) {
    if (!current.valid || !manifest.valid) return false;
    return compareVersion(manifest, current) > 0;
}

// =============================================================================
// OTA manifest — parsed from JSON served by the update server
// =============================================================================

struct OtaManifest {
    char version[16] = "";  // "major.minor.patch"
    char url[128]    = "";  // firmware binary URL
    char md5[33]     = "";  // optional MD5 hex digest (32 chars + NUL)
    bool valid       = false;
};

// Minimal JSON string-field extractor (no ArduinoJson dependency — testable natively).
// Finds `"key":"value"` and copies value into out[outLen]. Returns true on success.
static inline bool _otaExtractStr(const char *json, const char *key,
                                   char *out, size_t outLen) {
    char needle[40];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < outLen - 1) out[i++] = *p++;
    out[i] = '\0';
    return (*p == '"') && (i > 0);
}

// Parse JSON manifest: {"version":"1.2.3","url":"http://...","md5":"<hex>"}
// md5 is optional. Returns valid=false if version or url are missing/empty.
inline OtaManifest parseOtaManifest(const char *json) {
    OtaManifest m;
    if (!json || !*json) return m;
    bool hasVer = _otaExtractStr(json, "version", m.version, sizeof(m.version));
    bool hasUrl = _otaExtractStr(json, "url",     m.url,     sizeof(m.url));
    _otaExtractStr(json, "md5", m.md5, sizeof(m.md5));  // optional, ignore return
    m.valid = hasVer && hasUrl;
    return m;
}

// =============================================================================
// OTA URL validation — allowlist and scheme enforcement
// =============================================================================

// Build-time allowlist of permitted OTA hostnames (comma-separated in define).
// If OTA_ALLOWED_HOSTS is not defined, OTA URL validation rejects ALL URLs
// (safe default — must be explicitly configured to enable OTA).
#ifndef OTA_ALLOWED_HOSTS
#define OTA_ALLOWED_HOSTS ""
#endif

// Extracts the hostname from a URL. Returns false if URL is malformed.
// Supports http:// and https:// schemes. Writes hostname into out[outLen].
inline bool otaExtractHostname(const char *url, char *out, size_t outLen) {
    if (!url || !*url) return false;
    const char *p = url;
    // Skip scheme
    if (strncmp(p, "https://", 8) == 0) p += 8;
    else if (strncmp(p, "http://", 7) == 0) p += 7;
    else return false;  // unknown scheme
    // Extract hostname (up to '/', ':', or end)
    size_t i = 0;
    while (*p && *p != '/' && *p != ':' && i < outLen - 1) out[i++] = *p++;
    out[i] = '\0';
    return i > 0;
}

// Returns true if the URL uses HTTPS
inline bool otaIsHttps(const char *url) {
    return url && strncmp(url, "https://", 8) == 0;
}

// Returns true if hostname is in the comma-separated allowlist.
// Empty allowlist = nothing allowed (safe default).
inline bool otaHostAllowed(const char *hostname, const char *allowlist) {
    if (!hostname || !hostname[0] || !allowlist || !allowlist[0]) return false;
    size_t hlen = strlen(hostname);
    const char *p = allowlist;
    while (*p) {
        // Skip leading whitespace/commas
        while (*p == ',' || *p == ' ') p++;
        if (!*p) break;
        // Find end of this entry
        const char *end = p;
        while (*end && *end != ',') end++;
        size_t elen = (size_t)(end - p);
        // Trim trailing whitespace
        while (elen > 0 && p[elen - 1] == ' ') elen--;
        if (elen == hlen && strncmp(p, hostname, hlen) == 0) return true;
        p = end;
    }
    return false;
}

// Full OTA URL validation: HTTPS required + hostname must be in allowlist.
// Returns true if URL is acceptable for OTA download.
inline bool otaValidateUrl(const char *url) {
    if (!otaIsHttps(url)) return false;
    char hostname[64];
    if (!otaExtractHostname(url, hostname, sizeof(hostname))) return false;
    return otaHostAllowed(hostname, OTA_ALLOWED_HOSTS);
}

// =============================================================================
// Boot-failure rollback — pure threshold logic
// =============================================================================

// Returns true when consecutive boot failures have reached the rollback threshold.
// Storage of boot_failures is the caller's responsibility (NVS in firmware).
inline bool shouldRollback(int boot_failures, int max_failures) {
    return boot_failures >= max_failures;
}

// =============================================================================
// Partial download detection
// =============================================================================

struct OtaDownloadState {
    bool     in_progress    = false;
    uint32_t bytes_expected = 0;
    uint32_t bytes_written  = 0;
};

// Returns true if a download was started but not completed.
// bytes_expected == 0 means no size was negotiated — treat as not started.
inline bool isOtaIncomplete(const OtaDownloadState &s) {
    return s.in_progress && s.bytes_expected > 0 &&
           s.bytes_written < s.bytes_expected;
}
