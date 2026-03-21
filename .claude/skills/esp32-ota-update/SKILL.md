---
name: esp32-ota-update
description: Implement and extend the OTA firmware update system — manifest-based updates, boot health tracking, rollback, and partial download detection.
---

# ESP32 OTA Update System

## When to Use

Use this skill when touching the OTA update flow, adjusting rollback thresholds, modifying the firmware version string, or adding OTA-related NVS keys. Logic is in `src/ota_logic.h` (portable, testable natively) and `src/main.cpp` (`handleOtaUpdate`, `handleOtaStatus`, `otaCheckBootHealth`, `otaMarkBootSuccessful`, `otaCheckPartialDownload`).

## Pattern / Rules

1. **Manifest format**: JSON `{"version":"X.Y.Z","url":"http://...","md5":"<hex>"}`. MD5 is optional. Version must be strictly newer than `FIRMWARE_VERSION` — same-version re-flashes are refused by `isUpdateAvailable()`.

2. **Firmware version string**: Defined as `-DFIRMWARE_VERSION=\"X.Y.Z\"` in `platformio.ini`. Increment for every release. The OTA handler compares this against the manifest version to decide whether to proceed.

3. **Boot health**: `otaCheckBootHealth()` runs FIRST in `setup()`, before LittleFS or NVS config load. It increments `boot_fail` in NVS on every boot. Call `otaMarkBootSuccessful()` only after WiFi connects — this is the signal that the firmware is functional. If `boot_fail >= OTA_MAX_BOOT_FAILURES` (default 3), calls `esp_ota_mark_app_invalid_rollback_and_reboot()`.

4. **Rollback counter reset**: Reset `boot_fail` to 0 BEFORE calling `esp_ota_mark_app_invalid_rollback_and_reboot()` so a device without a previous OTA slot doesn't loop.

5. **Partial download detection**: `otaCheckPartialDownload()` reads `ota_ip`/`ota_exp`/`ota_wrt` from NVS. The `ota_ip` flag is set before `Update.writeStream()` and cleared only after `Update.end(true)` succeeds. A power failure leaves `ota_ip=true` — log it and clear on next boot.

6. **Partition table**: Uses `partitions_ota.csv` — must have two app partitions for OTA rollback to work. Without dual partitions, `esp_ota_mark_app_invalid_rollback_and_reboot()` logs "rollback unavailable" and continues.

7. **OTA NVS keys** (namespace `sauna`):
   - `boot_fail` (int) — consecutive boot failure count
   - `ota_ip` (bool) — download in-progress flag
   - `ota_exp` (uint) — expected firmware bytes
   - `ota_wrt` (uint) — bytes written so far

8. **Auth required**: Both `/ota/status` and `/ota/update` require `requireAdmin()` — never expose OTA to unauthenticated callers.

9. **Downgrade refused**: `isUpdateAvailable()` returns false if manifest version is equal to or older than current. The handler returns `{"ok":true,"updated":false,"reason":"current X >= manifest Y"}` — not an error.

10. **ota_logic.h has no Arduino deps**: `parseVersion`, `parseOtaManifest`, `isOtaIncomplete`, `shouldRollback` all use only `<cstdint>/<cstdio>/<cstring>`. Keep it that way for native testability.

## Code Template

```cpp
// Checking whether to trigger OTA from a scheduled task:
FirmwareVersion current  = parseVersion(FIRMWARE_VERSION);
FirmwareVersion incoming = parseVersion(manifest.version);
if (!isUpdateAvailable(current, incoming)) {
    // log "already up to date" and return
    return;
}
// proceed with Update.begin() / writeStream() / Update.end()
```

## Testing

`test/test_ota/` — 17 native tests covering: version parsing (valid, empty, null, malformed, zeros), comparison (patch/minor/major), manifest (valid, missing fields, optional md5), update decision (same/older/newer), rollback threshold, and partial download state. Run with `pio test -e native`.
