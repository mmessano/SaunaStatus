// src/web_ota.cpp — OTA HTTP handlers extracted from web.cpp
// This file is Arduino-only; it must not be compiled in native test builds.
#ifdef ARDUINO

#include "secrets.h"
#include "web.h"
#include "web_internal.h"
#include "auth.h"
#include "ota_logic.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <Preferences.h>

void handleOtaStatus()
{
  if (!requireAdmin()) return;
  const esp_partition_t *running = esp_ota_get_running_partition();
  char buf[192];
  snprintf(buf, sizeof(buf),
           "{\"version\":\"%s\",\"partition\":\"%s\",\"boot_failures\":%d}",
           FIRMWARE_VERSION,
           running ? running->label : "unknown",
           []() {
             prefs.begin("sauna", true);
             int f = prefs.getInt("boot_fail", 0);
             prefs.end();
             return f;
           }());
  server.send(200, "application/json", buf);
}

// POST /ota/update?manifest=<url>
// Downloads the JSON manifest, checks version, then streams the firmware binary.
// The partial-download state is persisted to NVS so a power failure is detectable.
void handleOtaUpdate()
{
  if (!requireAdmin()) return;
  if (!server.hasArg("manifest")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing manifest param\"}");
    return;
  }
  String manifestUrl = server.arg("manifest");

  // Validate manifest URL: must be HTTPS and on an allowed host
  if (!otaValidateUrl(manifestUrl.c_str())) {
    server.send(403, "application/json",
                "{\"ok\":false,\"error\":\"manifest URL rejected: must be HTTPS on an allowed host\"}");
    return;
  }

  // Fetch manifest
  HTTPClient http;
  http.begin(manifestUrl);
  int code = http.GET();
  if (code != 200) {
    http.end();
    char err[80];
    snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"manifest fetch failed: HTTP %d\"}", code);
    server.send(502, "application/json", err);
    return;
  }
  String body = http.getString();
  http.end();

  OtaManifest manifest = parseOtaManifest(body.c_str());
  if (!manifest.valid) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"invalid manifest: missing version or url\"}");
    return;
  }

  // Version check — refuse downgrades and same-version re-flashes
  FirmwareVersion current  = parseVersion(FIRMWARE_VERSION);
  FirmwareVersion incoming = parseVersion(manifest.version);
  if (!isUpdateAvailable(current, incoming)) {
    char msg[96];
    snprintf(msg, sizeof(msg),
             "{\"ok\":true,\"updated\":false,\"reason\":\"current %s >= manifest %s\"}",
             FIRMWARE_VERSION, manifest.version);
    server.send(200, "application/json", msg);
    return;
  }

  // Validate firmware binary URL from manifest
  if (!otaValidateUrl(manifest.url)) {
    server.send(403, "application/json",
                "{\"ok\":false,\"error\":\"firmware URL rejected: must be HTTPS on an allowed host\"}");
    return;
  }

  // Fetch firmware binary
  http.begin(manifest.url);
  int fwCode = http.GET();
  if (fwCode != 200) {
    http.end();
    char err[80];
    snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"firmware fetch failed: HTTP %d\"}", fwCode);
    server.send(502, "application/json", err);
    return;
  }

  int fwSize = http.getSize();
  if (fwSize <= 0) {
    http.end();
    server.send(502, "application/json",
                "{\"ok\":false,\"error\":\"firmware size unknown\"}");
    return;
  }

  if (!Update.begin(fwSize)) {
    http.end();
    char err[96];
    snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"Update.begin failed: %s\"}",
             Update.errorString());
    server.send(500, "application/json", err);
    return;
  }

  // SHA-256 integrity check: compute hash during streaming, verify after download.
  // MD5 is deprecated and ignored when sha256 is present.
  mbedtls_sha256_context sha_ctx;
  mbedtls_sha256_init(&sha_ctx);
  mbedtls_sha256_starts(&sha_ctx, 0);  // 0 = SHA-256

  // Mark download in progress in NVS so a power failure is detectable at next boot
  prefs.begin("sauna", false);
  prefs.putBool("ota_ip", true);
  prefs.putUInt("ota_exp", (unsigned int)fwSize);
  prefs.putUInt("ota_wrt", 0);
  prefs.end();

  Serial.printf("OTA: writing %d bytes from %s\n", fwSize, manifest.url);

  // Stream firmware through SHA-256 hash while writing to Update partition
  WiFiClient *stream = http.getStreamPtr();
  uint8_t otaBuf[512];
  size_t written = 0;
  while (written < (size_t)fwSize) {
    size_t toRead = sizeof(otaBuf);
    if (toRead > (size_t)fwSize - written) toRead = (size_t)fwSize - written;
    int bytesRead = stream->readBytes(otaBuf, toRead);
    if (bytesRead <= 0) break;
    mbedtls_sha256_update(&sha_ctx, otaBuf, bytesRead);
    Update.write(otaBuf, bytesRead);
    written += bytesRead;
  }
  http.end();

  // Update NVS with bytes written (best-effort — power may fail here)
  prefs.begin("sauna", false);
  prefs.putUInt("ota_wrt", (unsigned int)written);
  prefs.end();

  // Verify SHA-256 before finalizing
  uint8_t sha_digest[32];
  mbedtls_sha256_finish(&sha_ctx, sha_digest);
  mbedtls_sha256_free(&sha_ctx);
  char computed_sha[65];
  for (int i = 0; i < 32; i++) {
    snprintf(computed_sha + i * 2, 3, "%02x", sha_digest[i]);
  }
  if (strncmp(computed_sha, manifest.sha256, 64) != 0) {
    Update.abort();
    Serial.printf("OTA: SHA-256 mismatch — expected %.16s... got %.16s...\n",
                  manifest.sha256, computed_sha);
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"SHA-256 verification failed\"}");
    return;
  }

  if (!Update.end(true) || !Update.isFinished()) {
    char err[96];
    snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"Update.end failed: %s\"}",
             Update.errorString());
    server.send(500, "application/json", err);
    return;
  }

  // Clear in-progress flag — download completed and verified successfully
  prefs.begin("sauna", false);
  prefs.putBool("ota_ip", false);
  prefs.end();

  Serial.printf("OTA: success (%zu bytes, SHA-256 verified), rebooting to %s\n",
                written, manifest.version);
  server.send(200, "application/json",
              "{\"ok\":true,\"updated\":true,\"rebooting\":true}");
  delay(500);
  esp_restart();
}

#endif // ARDUINO
