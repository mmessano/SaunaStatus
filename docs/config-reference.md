# Configuration Reference

All configurable values in the SaunaStatus firmware, organized by config tier.

---

## Tier 1: Compile-Time / Build Flags

Set via `-D` flags in `platformio.ini` under `build_flags`. Values in `src/main.cpp` are the fallback defaults if no build flag overrides them. All are `#ifndef`-guarded so a build flag always wins.

| Name | Default Value | Description |
|---|---|---|
| `DEFAULT_CEILING_SP_F` | `160.0f` | Ceiling PID setpoint at boot (°F) |
| `DEFAULT_BENCH_SP_F` | `120.0f` | Bench PID setpoint at boot (°F) |
| `TEMP_LIMIT_C` | `120.0f` | Overheat alarm threshold (°C / 248°F); triggers full vent open and suppresses PID |
| `SERIAL_LOG_INTERVAL_MS` | `10000` | Minimum ms between serial status log lines |
| `STALE_THRESHOLD_MS` | `10000UL` | DHT stale-reading timeout (ms); 0 disables stale detection entirely |
| `INFLUX_WRITE_INTERVAL_MS` | `60000UL` | InfluxDB write interval (ms) |
| `MQTT_RECONNECT_INTERVAL_MS` | `5000UL` | MQTT reconnect retry interval (ms) |
| `MOTOR_RPM` | `12` | Stepper motor speed (RPM) |
| `PID_MIN_STEP_DELTA` | `5` | Minimum PID output delta (steps) to actuate motor; suppresses jitter |
| `PID_CONSERVATIVE_THRESHOLD_C` | `10.0f` | Error threshold (°C) to switch PID from aggressive to conservative tuning |
| `SETPOINT_MIN_F` | `32.0f` | Minimum valid setpoint (°F); enforced in HTTP, MQTT, and LittleFS loading |
| `SETPOINT_MAX_F` | `300.0f` | Maximum valid setpoint (°F) |
| `DEFAULT_SENSOR_READ_INTERVAL_MS` | `2000UL` | Default sensor read interval (ms) |
| `DEFAULT_STATIC_IP` | `"192.168.1.200"` | Default device static IP address |
| `WS_JSON_BUF_SIZE` | `320` | WebSocket broadcast JSON output buffer (bytes) |
| `MQTT_BUF_SIZE` | `512` | MQTT client buffer size (bytes) |
| `NTP_SERVER_LOCAL` | `"192.168.1.100"` | Primary NTP server (router or local server) |
| `WIFI_GATEWAY_IP` | `192, 168, 1, 100` | WiFi gateway (IPAddress comma-separated initializer) |
| `WIFI_DNS_IP` | `8, 8, 8, 8` | Primary DNS (IPAddress comma-separated initializer) |
| `SENSOR_READ_INTERVAL_MIN_MS` | `500UL` | Minimum sensor read interval (ms); enforced in LittleFS loading and `/config/save` |
| `SENSOR_READ_INTERVAL_MAX_MS` | `10000UL` | Maximum sensor read interval (ms) |
| `SERIAL_LOG_INTERVAL_MIN_MS` | `1000UL` | Minimum serial log interval (ms) |
| `SERIAL_LOG_INTERVAL_MAX_MS` | `60000UL` | Maximum serial log interval (ms) |
| `RREF` | `4300.0` | MAX31865 reference resistor value (Ω) |
| `RNOMINAL` | `1000.0` | MAX31865 nominal resistance at 0°C (Ω) — PT1000 = 1000 |
| `AUTH_TOKEN_TTL_MS` | `3600000UL` (1 hour) | Session token time-to-live |
| `AUTH_MAX_SESSIONS` | `10` | Concurrent session slots |
| `AUTH_MAX_USERS` | `5` | Maximum stored users |
| `AUTH_MIN_PASS_LEN` | `8` | Minimum password length |
| `AUTH_MAX_PASS_LEN` | `72` | Maximum password length (bounds SHA-256 buffer) |
| `AUTH_MIN_USER_LEN` | `1` | Minimum username length |
| `AUTH_MAX_USER_LEN` | `32` | Maximum username length |
| `AUTH_PBKDF2_ITERATIONS` | `10000` | PBKDF2-SHA-256 iterations for newly created or updated passwords |
| `AUTH_RATE_LIMIT_MAX_FAILURES` | `5` | Failed login attempts allowed within the rate-limit window before lockout |
| `AUTH_RATE_LIMIT_WINDOW_MS` | `60000UL` | Sliding failure window for login rate limiting (ms) |
| `AUTH_RATE_LIMIT_LOCKOUT_MS` | `300000UL` | Login lockout duration after too many failures (ms) |
| `AUTH_RATE_LIMIT_SLOTS` | `8` | Distinct IP slots tracked by the in-memory login rate limiter |
| `OTA_ALLOWED_HOSTS` | `""` | Comma-separated OTA hostname allowlist; empty string disables OTA |
| `OTA_MAX_BOOT_FAILURES` | `3` | Consecutive boot failures before rollback |
| `OVERHEAT_CLEAR_HYSTERESIS_C` | `10.0f` | Clear band used by portable `tickOverheat()` below `TEMP_LIMIT_C`; note `checkOverheat()` in `sensors.cpp` does not apply this hysteresis |
| `FIRMWARE_VERSION` | `"2.0.0"` | Firmware version string for OTA comparison |

Commented-out override examples are available in `platformio.ini` under `build_flags`.

---

## Tier 2: Fleet Defaults — LittleFS `/config.json`

Uploaded with the filesystem image (`pio run -t uploadfs`). Overrides Tier 1 for all devices flashed with the same image. Missing file is silently ignored; parse errors skip the layer. Motor calibration (`omx`/`imx`) is intentionally excluded — it is device-specific.

| JSON Key | Type | Valid Range | Description |
|---|---|---|---|
| `ceiling_setpoint_f` | float | 32.0–300.0 °F | Ceiling PID setpoint (°F) |
| `bench_setpoint_f` | float | 32.0–300.0 °F | Bench PID setpoint (°F) |
| `ceiling_pid_enabled` | bool | `true`/`false` | Enable ceiling PID at boot |
| `bench_pid_enabled` | bool | `true`/`false` | Enable bench PID at boot |
| `sensor_read_interval_ms` | uint | 500–10000 | Sensor read and WebSocket broadcast interval (ms) |
| `serial_log_interval_ms` | uint | 1000–60000 | Serial status log interval (ms) |
| `static_ip` | string | valid IPv4 | Device static IP (requires restart to take effect) |
| `device_name` | string | 1–24 chars | Device name used as MQTT client ID prefix (requires restart) |

Validation semantics:

- If LittleFS is unavailable at boot, the entire Tier 2 layer is skipped.
- If `/config.json` is missing, the entire Tier 2 layer is skipped.
- If `/config.json` is malformed, the entire Tier 2 layer is skipped.
- If a specific key is present but invalid, only that field is ignored; other valid fields in the same file still apply.
- Tier 3 NVS values still override any valid Tier 2 values later in boot.
- Tier 2 `device_name` only checks non-empty length `< 25`; the stricter `[A-Za-z0-9_-]` validation is only enforced by Tier 3 `/config/save`.

#### Example `/config.json`:

```json
{
  "ceiling_setpoint_f": 160.0,
  "bench_setpoint_f": 120.0,
  "ceiling_pid_enabled": false,
  "bench_pid_enabled": false,
  "sensor_read_interval_ms": 2000,
  "serial_log_interval_ms": 10000,
  "static_ip": "192.168.1.200",
  "device_name": "sauna"
}
```

---

## Tier 3: Per-Device NVS

Namespace: `sauna`. Written by HTTP `POST /setpoint`, `POST /pid`, `POST /motor?cmd=setopen`, and `POST /config/save` endpoints, and MQTT subscription callbacks. Each key is guarded by `prefs.isKey()` so a missing NVS entry never silently reverts a Tier 2 value.

| NVS Key | Type | Units | Description | Written By |
|---|---|---|---|---|
| `csp` | float | °C | Ceiling setpoint (stored in °C, API accepts/returns °F) | `POST /setpoint?ceiling=`, `POST /config/save`, MQTT `sauna/ceiling_setpoint/set` |
| `bsp` | float | °C | Bench setpoint | `POST /setpoint?bench=`, `POST /config/save`, MQTT `sauna/bench_setpoint/set` |
| `cen` | bool | — | Ceiling PID enabled | `POST /pid?ceiling=`, `POST /config/save`, MQTT `sauna/ceiling_pid/set` |
| `ben` | bool | — | Bench PID enabled | `POST /pid?bench=`, `POST /config/save`, MQTT `sauna/bench_pid/set` |
| `omx` | int | steps | Outflow motor calibrated full-open step count | `POST /motor?motor=outflow&cmd=setopen` |
| `imx` | int | steps | Inflow motor calibrated full-open step count | `POST /motor?motor=inflow&cmd=setopen` |
| `sri` | uint | ms | Sensor read interval | `/config/save` |
| `slg` | uint | ms | Serial log interval | `/config/save` |
| `sip` | string | — | Static IP address (requires restart) | `/config/save` |
| `dn` | string | — | Device name (requires restart) | `/config/save` |
| `boot_fail` | int | — | Consecutive boot failure count (OTA rollback) | `otaCheckBootHealth()` on every boot |
| `ota_ip` | bool | — | OTA download in-progress flag | `handleOtaUpdate()` |
| `ota_exp` | uint | bytes | Expected firmware size (OTA) | `handleOtaUpdate()` |
| `ota_wrt` | uint | bytes | Bytes written so far (OTA) | `handleOtaUpdate()` |

### Namespace `sauna_auth`

Users stored as `u0_name`, `u0_hash`, `u0_salt`, `u0_role` … `u4_*`. `authNvsLoad()` breaks on first missing `u{i}_name`. `authNvsSave()` clears name keys for removed users to prevent orphans. External adapter config: `db_url` and `db_key`.

---

## Hardware Constants

Fixed values tied to the physical hardware. Not intended to be overridden without a corresponding hardware change.

| Name | Value | Description |
|---|---|---|
| `DEVICE` | `"ESP32"` | InfluxDB/MQTT device tag |
| `TZ_INFO` | `"CST6CDT,M3.2.0,M11.1.0"` | POSIX timezone string (US Central) |
| `RREF` | `4300.0` | MAX31865 reference resistor value (Ω) |
| `RNOMINAL` | `1000.0` | PT1000 nominal resistance at 0°C (Ω) |
| `INA260_SDA` | `1` | I2C SDA GPIO for INA260 power monitor |
| `INA260_SCL` | `2` | I2C SCL GPIO for INA260 power monitor |
| `DHTPIN_CEILING` | `8` | GPIO for ceiling DHT21 sensor |
| `DHTPIN_BENCH` | `9` | GPIO for bench DHT21 sensor |
| `DHTTYPE` | `DHT21` | DHT sensor model (AM2301) |
| `VENT_STEPS` | `1024` | Default full-open step count (90° on 28BYJ-48); overridden at runtime by `omx`/`imx` from NVS |

---

## Network / Credentials

Defined in `src/secrets.h` (not committed to version control). See `CLAUDE.md` for the required `#define` list.

| Name | Description |
|---|---|
| `WIFI_SSID` | WiFi network name |
| `WIFI_PASSWORD` | WiFi password |
| `INFLUXDB_URL` | InfluxDB HTTP endpoint |
| `INFLUXDB_TOKEN` | InfluxDB write token |
| `INFLUXDB_ORG` | InfluxDB organization ID |
| `INFLUXDB_BUCKET` | InfluxDB bucket name |
| `MQTT_BROKER` | MQTT broker IP address |
| `MQTT_PORT` | MQTT broker port (default 1883) |
| `MQTT_USER` | MQTT username (empty string = no auth) |
| `MQTT_PASS` | MQTT password |
| `AUTH_ADMIN_USER` | Emergency admin username (seeded on first boot) |
| `AUTH_ADMIN_PASS` | Emergency admin password (min 8 chars; change immediately after first boot) |

Both `AUTH_ADMIN_USER` and `AUTH_ADMIN_PASS` are enforced by `#error` guards at the top of `main.cpp` — omitting either causes a compile error.

#### Template

```cpp
#define WIFI_SSID        "..."
#define WIFI_PASSWORD    "..."
#define INFLUXDB_URL     "http://192.168.1.125:30115"
#define INFLUXDB_TOKEN   "..."
#define INFLUXDB_ORG     "..."
#define INFLUXDB_BUCKET  "..."
#define MQTT_BROKER      "192.168.1.125"
#define MQTT_PORT        1883
#define MQTT_USER        "..."   // set to "" to connect without credentials
#define MQTT_PASS        "..."
#define AUTH_ADMIN_USER  "admin"
#define AUTH_ADMIN_PASS  "..."   // min 8 chars; change immediately after first boot
```

Static IP, gateway, and DNS default values are set by build flags (`DEFAULT_STATIC_IP`, `WIFI_GATEWAY_IP`, `WIFI_DNS_IP`). Static IP can be overridden at runtime via Tier 2 `/config.json` or Tier 3 NVS via `POST /config/save`; a restart is required for the new IP to take effect.
