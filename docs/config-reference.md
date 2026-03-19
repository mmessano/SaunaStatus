# Configuration Reference

All configurable values in the SaunaStatus firmware, organized by config tier.

---

## Tier 1: Compile-Time / Build Flags

Set via `-D` flags in `platformio.ini` under `build_flags`. Values in `src/main.cpp` are the fallback defaults if no build flag overrides them.

| Name | Default Value | Description | File |
|---|---|---|---|
| `DEFAULT_CEILING_SP_F` | `160.0f` | Ceiling PID setpoint at boot (°F) | `src/main.cpp`, `platformio.ini` |
| `DEFAULT_BENCH_SP_F` | `120.0f` | Bench PID setpoint at boot (°F) | `src/main.cpp`, `platformio.ini` |
| `TEMP_LIMIT_C` | `120.0f` | Overheat alarm threshold (°C / 248°F); triggers full vent open and suppresses PID | `src/main.cpp` |
| `SERIAL_LOG_INTERVAL_MS` | `10000` | Minimum ms between serial status log lines | `src/main.cpp` |
| `STALE_THRESHOLD_MS` | `10000UL` | DHT stale-reading timeout (ms); 0 disables stale detection entirely | `src/main.cpp` |

---

## Tier 2: Fleet Defaults — LittleFS `/config.json`

Uploaded with the filesystem image (`pio run -t uploadfs`). Overrides Tier 1 for all devices flashed with the same image. Missing file is silently ignored; parse errors skip the layer.

| JSON Key | Type | Description |
|---|---|---|
| `ceiling_setpoint_f` | float | Ceiling PID setpoint (°F) |
| `bench_setpoint_f` | float | Bench PID setpoint (°F) |
| `ceiling_pid_enabled` | bool | Enable ceiling PID at boot |
| `bench_pid_enabled` | bool | Enable bench PID at boot |

> Motor calibration (`omx`/`imx`) is intentionally excluded — it is device-specific.

---

## Tier 3: Per-Device NVS

Namespace: `sauna`. Written by HTTP `/setpoint`, `/pid`, `/motor?cmd=setopen` endpoints and MQTT subscription callbacks. Each key is guarded by `prefs.isKey()` so a missing NVS entry never silently reverts a Tier 2 value.

| NVS Key | Type | Description |
|---|---|---|
| `csp` | float | Ceiling setpoint (stored in °C) |
| `bsp` | float | Bench setpoint (stored in °C) |
| `cen` | bool | Ceiling PID enabled |
| `ben` | bool | Bench PID enabled |
| `omx` | int | Outflow motor calibrated full-open step count |
| `imx` | int | Inflow motor calibrated full-open step count |

> Setpoints are stored internally in °C; the HTTP/MQTT API accepts and returns °F.

---

## Hardware Constants

Fixed values tied to the physical hardware. Not intended to be overridden without a corresponding hardware change.

| Name | Value | Description | File |
|---|---|---|---|
| `DEVICE` | `"ESP32"` | InfluxDB/MQTT device tag | `src/main.cpp` |
| `TZ_INFO` | `"CST6CDT,M3.2.0,M11.1.0"` | POSIX timezone string (US Central) | `src/main.cpp` |
| `RREF` | `4300.0` | MAX31865 reference resistor value (Ω) | `src/main.cpp` |
| `RNOMINAL` | `1000.0` | PT1000 nominal resistance at 0°C (Ω) | `src/main.cpp` |
| `INA219_SDA` | `4` | I2C SDA GPIO for INA219 power monitor | `src/main.cpp` |
| `INA219_SCL` | `13` | I2C SCL GPIO for INA219 power monitor | `src/main.cpp` |
| `DHTPIN_CEILING` | `16` | GPIO for ceiling DHT21 sensor | `src/main.cpp` |
| `DHTPIN_BENCH` | `17` | GPIO for bench DHT21 sensor | `src/main.cpp` |
| `DHTTYPE` | `DHT21` | DHT sensor model (AM2301) | `src/main.cpp` |
| `VENT_STEPS` | `1024` | Default full-open step count (90° on 28BYJ-48); overridden at runtime by `omx`/`imx` from NVS | `src/main.cpp` |

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

> Static IP (`192.168.1.200`), gateway, and DNS are hardcoded in `src/main.cpp` `setup()` — not currently configurable without a code change.
