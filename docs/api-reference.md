# Sauna Controller — API Reference

All API values are derived directly from `src/main.cpp` and `src/sauna_logic.h`.

---

## 1. Device Info

| Property       | Value                  |
|----------------|------------------------|
| Static IP      | `192.168.1.200`        |
| Gateway        | `192.168.1.100`        |
| Subnet         | `255.255.255.0`        |
| DNS            | `8.8.8.8`              |
| HTTP port      | `80`                   |
| WebSocket port | `81`                   |
| MQTT port      | `1883` (from `secrets.h` `MQTT_PORT`) |
| Time zone      | `CST6CDT,M3.2.0,M11.1.0` |

Credentials (WiFi SSID/password, InfluxDB URL/token/org/bucket, MQTT broker/user/pass) are defined in `src/secrets.h`, which is not committed to version control.

---

## 2. HTTP REST API

All endpoints are registered on port 80. State-mutating routes require `Authorization: Bearer <token>` (see `src/auth.h`). Unauthenticated requests return `401 application/json`. The `Cache-Control: no-store` header is sent on all HTML page responses.

### HTTP Response Conventions

| Outcome | Status | Body pattern |
|---|---|---|
| Success (mutation) | 200 | `{"ok":true}` or `{"ok":true,"restart_required":false}` |
| Success (data) | 200 | `application/json` with data fields |
| Bad request / validation | 400 | `{"ok":false,"error":"<message>"}` or `{"error":"<message>"}` |
| Unauthorized | 401 | `{"error":"unauthorized"}` or `{"error":"token_invalid"}` |
| Forbidden (slot 0) | 403 | `{"error":"cannot delete emergency admin"}` |
| Not found | 404 | `{"error":"user not found"}` |
| Conflict | 409 | `{"error":"user limit reached"}` or `{"error":"username taken"}` |
| Upstream failure | 502 | `{"ok":false,"error":"..."}` or plain text |
| Server error | 500 | `{"ok":false,"error":"..."}` or plain text |

`handleConfigSave()` uses `goto send_error` to jump from any validation failure to a single error-emit block, ensuring no partial state is applied.

---

### `GET /`

Serves the web UI from LittleFS.

#### Response

| Status | Content-Type  | Body                    |
|--------|---------------|-------------------------|
| `200`  | `text/html`   | `/index.html` from LittleFS (streamed) |
| `500`  | `text/plain`  | `index.html not found`  |

#### Example

```
GET http://192.168.1.200/
```

---

### `GET /log`

Triggers an immediate write of all current sensor and control state to InfluxDB (both `sauna_status` and `sauna_control` measurements). Normally writes happen on a 60-second timer; this forces an out-of-cycle write.

#### Response

| Status | Content-Type | Body                               |
|--------|--------------|------------------------------------|
| `200`  | `text/plain` | `OK`                               |
| `500`  | `text/plain` | InfluxDB error message string      |

#### Example

```
GET http://192.168.1.200/log
```

---

### `GET /history`

Proxies a Flux query to InfluxDB and returns the result as CSV. The InfluxDB token never leaves the device. Returns 5-minute mean aggregates for `ceiling_temp`, `bench_temp`, and `stove_temp` from the `sauna_status` measurement.

#### Query Parameters

| Name    | Type   | Required | Validation                                      | Description                        |
|---------|--------|----------|-------------------------------------------------|------------------------------------|
| `range` | string | No       | 1–8 characters, `[0-9a-zA-Z]` only; defaults to `1h` if missing or invalid | Flux range string (e.g., `1h`, `3h`, `24h`, `7d`) |

#### Response

| Status | Content-Type | Body                         |
|--------|--------------|------------------------------|
| `200`  | `text/csv`   | Flux query result as CSV     |
| `502`  | `text/plain` | `InfluxDB query failed`      |

#### Flux Query Executed

```flux
from(bucket:"<BUCKET>")
  |> range(start: -<range>)
  |> filter(fn: (r) => r._measurement == "sauna_status")
  |> filter(fn: (r) =>
       r._field == "ceiling_temp" or
       r._field == "bench_temp"   or
       r._field == "stove_temp")
  |> aggregateWindow(every: 5m, fn: mean, createEmpty: false)
  |> pivot(rowKey:["_time"], columnKey:["_field"], valueColumn:"_value")
  |> keep(columns:["_time","ceiling_temp","bench_temp","stove_temp"])
```

CSV dialect: no annotations, header row included, comma delimiter.

#### Examples

```
GET http://192.168.1.200/history
GET http://192.168.1.200/history?range=3h
GET http://192.168.1.200/history?range=24h
```

---

### `GET /setpoint`

Sets one or both PID temperature setpoints. Accepts values in degrees Fahrenheit. Values outside 32–300 °F are silently ignored (the existing setpoint is unchanged). Persists to NVS immediately.

#### Query Parameters

| Name      | Type  | Required | Validation          | Description                       |
|-----------|-------|----------|---------------------|-----------------------------------|
| `ceiling` | float | No       | 32.0 ≤ value ≤ 300.0 °F | Ceiling PID setpoint in °F   |
| `bench`   | float | No       | 32.0 ≤ value ≤ 300.0 °F | Bench PID setpoint in °F     |

At least one parameter should be provided; providing neither is accepted but has no effect.

#### Response

| Status | Content-Type | Body |
|--------|--------------|------|
| `200`  | `text/plain` | `OK` |

#### Side Effects

Saves `csp` (ceiling setpoint, in °C) and `bsp` (bench setpoint, in °C) to NVS namespace `sauna`.

#### Examples

```
GET http://192.168.1.200/setpoint?ceiling=160
GET http://192.168.1.200/setpoint?bench=120
GET http://192.168.1.200/setpoint?ceiling=160&bench=120
```

---

### `GET /pid`

Enables or disables one or both PID controllers. Persists to NVS immediately.

#### Query Parameters

| Name      | Type   | Required | Accepted Values | Description              |
|-----------|--------|----------|-----------------|--------------------------|
| `ceiling` | string | No       | `1` (enable) or `0` (disable) | Ceiling PID enable state |
| `bench`   | string | No       | `1` (enable) or `0` (disable) | Bench PID enable state   |

Any value other than `"1"` is treated as disable.

#### Response

| Status | Content-Type | Body |
|--------|--------------|------|
| `200`  | `text/plain` | `OK` |

#### Side Effects

Saves `cen` and `ben` booleans to NVS namespace `sauna`.

#### Examples

```
GET http://192.168.1.200/pid?ceiling=1
GET http://192.168.1.200/pid?bench=0
GET http://192.168.1.200/pid?ceiling=1&bench=1
```

---

### `GET /motor`

Commands a stepper motor. All commands respond immediately; motor movement runs asynchronously in the main loop.

#### Query Parameters

| Name    | Type    | Required | Validation                             | Description                          |
|---------|---------|----------|----------------------------------------|--------------------------------------|
| `motor` | string  | Yes      | `outflow` or `inflow`                  | Which motor to command               |
| `cmd`   | string  | Yes      | See command table below                | Command to execute                   |
| `steps` | integer | No       | 1 ≤ value ≤ 4096 (`VENT_STEPS * 4`); defaults to `64` if absent or out of range | Step count for `cw`/`ccw` commands |

#### Motor Names

| Value     | Physical Vent         | GPIO Pins              |
|-----------|-----------------------|------------------------|
| `outflow` | Upper (exhaust) vent  | 4, 5, 6, 7             |
| `inflow`  | Lower (intake) vent   | 15, 16, 17, 18         |

#### Command Table

| `cmd`       | Description                                                                                                           | NVS Side Effect     |
|-------------|-----------------------------------------------------------------------------------------------------------------------|---------------------|
| `cw`        | Move `steps` steps clockwise (opens vent). No upper bound enforced on target step count.                              | None                |
| `ccw`       | Move `steps` steps counter-clockwise (closes vent). Floored at 0; will not drive below closed.                        | None                |
| `open`      | Move to calibrated fully-open position (`outflow_max_steps` or `inflow_max_steps`).                                   | None                |
| `close`     | Move to step 0 (fully closed).                                                                                        | None                |
| `third`     | Move to one-third of calibrated max steps.                                                                            | None                |
| `twothird`  | Move to two-thirds of calibrated max steps.                                                                           | None                |
| `stop`      | Halt motor immediately.                                                                                                | None                |
| `zero`      | Calibration: mark current physical position as step 0 (closed). Stops motor, sets target to 0.                        | None (use `setopen` to persist) |
| `setopen`   | Calibration: mark current physical position as the fully-open endpoint. Saves calibrated max steps to NVS. Only applied if current target > 0. | Saves `omx` (outflow) or `imx` (inflow) to NVS namespace `sauna` |

#### Response

| Status | Content-Type | Body         |
|--------|--------------|--------------|
| `200`  | `text/plain` | `OK`         |
| `400`  | `text/plain` | `Bad motor`  |
| `400`  | `text/plain` | `Bad cmd`    |

#### Examples

```
GET http://192.168.1.200/motor?motor=outflow&cmd=open
GET http://192.168.1.200/motor?motor=inflow&cmd=cw&steps=128
GET http://192.168.1.200/motor?motor=outflow&cmd=zero
GET http://192.168.1.200/motor?motor=outflow&cmd=setopen
GET http://192.168.1.200/motor?motor=inflow&cmd=close
```

---

### `GET /config`

Serves the configuration portal UI from LittleFS.

#### Response

| Status | Content-Type | Body |
|--------|--------------|------|
| `200`  | `text/html`  | `/config.html` from LittleFS (streamed) |
| `500`  | `text/plain` | `config.html not found — upload filesystem image` |

---

### `GET /config/get`

Returns the current runtime configuration as JSON.

#### Response

| Status | Content-Type       | Body |
|--------|--------------------|------|
| `200`  | `application/json` | JSON object (see below) |

#### Response Schema

```json
{
  "ceiling_setpoint_f": 160.0,
  "bench_setpoint_f": 120.0,
  "ceiling_pid_en": false,
  "bench_pid_en": false,
  "sensor_read_interval_ms": 2000,
  "serial_log_interval_ms": 10000,
  "static_ip": "192.168.1.200",
  "device_name": "ESP32"
}
```

| Field | Type | Description |
|---|---|---|
| `ceiling_setpoint_f` | float | Ceiling PID setpoint (°F) |
| `bench_setpoint_f` | float | Bench PID setpoint (°F) |
| `ceiling_pid_en` | bool | Ceiling PID enabled |
| `bench_pid_en` | bool | Bench PID enabled |
| `sensor_read_interval_ms` | uint | Sensor read / WebSocket broadcast interval (ms) |
| `serial_log_interval_ms` | uint | Serial status log interval (ms) |
| `static_ip` | string | Current static IP (active after restart) |
| `device_name` | string | Current device name (active after restart) |

---

### `POST /config/save`

Validates and applies runtime configuration changes. All fields are optional; only provided fields are changed. Validation is performed atomically — if any field fails, no state is modified.

**Content-Type:** `application/x-www-form-urlencoded`

#### Parameters

| Name | Type | Validation | Description |
|---|---|---|---|
| `ceiling_setpoint_f` | float | 32.0–300.0 °F | Ceiling PID setpoint |
| `bench_setpoint_f` | float | 32.0–300.0 °F | Bench PID setpoint |
| `ceiling_pid_en` | string | `1`/`true`/`on` or `0`/`false`/`off` | Enable/disable ceiling PID |
| `bench_pid_en` | string | `1`/`true`/`on` or `0`/`false`/`off` | Enable/disable bench PID |
| `sensor_read_interval_ms` | int | 500–10000 | Sensor read interval (ms) |
| `serial_log_interval_ms` | int | 1000–60000 | Serial log interval (ms) |
| `static_ip` | string | valid IPv4 address | New static IP (restart required) |
| `device_name` | string | 1–24 chars, `[A-Za-z0-9_-]` | New device name (restart required) |

#### Response

| Status | Content-Type | Body |
|--------|--------------|------|
| `200`  | `application/json` | `{"ok":true,"restart_required":<bool>}` |
| `400`  | `application/json` | `{"ok":false,"error":"<message>"}` |

`restart_required` is `true` when `static_ip` or `device_name` was changed (the new value takes effect after reboot).

All validated fields are persisted to NVS immediately.

#### Example

```
POST http://192.168.1.200/config/save
Content-Type: application/x-www-form-urlencoded

ceiling_setpoint_f=170&sensor_read_interval_ms=3000
```

---

### `GET /ota/status`

Returns current firmware version and OTA partition information.

#### Response

| Status | Content-Type | Body |
|--------|--------------|------|
| `200`  | `application/json` | JSON object (see below) |

#### Response Schema

```json
{
  "version": "2.0.0",
  "partition": "ota_0",
  "boot_failures": 0
}
```

| Field | Type | Description |
|---|---|---|
| `version` | string | Running firmware version (from `FIRMWARE_VERSION` build flag) |
| `partition` | string | ESP32 OTA partition label (`ota_0`, `ota_1`, or `unknown`) |
| `boot_failures` | int | Consecutive failed boots tracked in NVS (`sauna`/`boot_fail`) |

---

### `POST /ota/update`

Initiates an OTA firmware update by downloading and applying a firmware binary described by a JSON manifest URL. Refuses same-version re-flashes and downgrades. Partial-download state is persisted to NVS so an interrupted transfer can be detected on the next boot.

#### Query Parameters

| Name | Type | Required | Description |
|---|---|---|---|
| `manifest` | string | Yes | URL of the OTA manifest JSON file |

#### Manifest JSON Format

```json
{
  "version": "1.1.0",
  "url": "http://server/firmware.bin"
}
```

#### Response

| Status | Content-Type | Body |
|--------|--------------|------|
| `200`  | `application/json` | `{"ok":true,"updated":true}` — update applied; device will reboot |
| `200`  | `application/json` | `{"ok":true,"updated":false,"reason":"current 1.0.0 >= manifest 1.0.0"}` — no update needed |
| `400`  | `application/json` | `{"ok":false,"error":"missing manifest param"}` |
| `400`  | `application/json` | `{"ok":false,"error":"invalid manifest: missing version or url"}` |
| `502`  | `application/json` | `{"ok":false,"error":"manifest fetch failed: HTTP <code>"}` |
| `500`  | `application/json` | `{"ok":false,"error":"<firmware download/flash error>"}` |

#### Example

```
POST http://192.168.1.200/ota/update?manifest=http://192.168.1.10/firmware/manifest.json
```

---

### `GET /delete/status`

Deletes all data in the `sauna_status` measurement from InfluxDB (time range `1970-01-01` to `2099-12-31`).

#### Response

| Status | Content-Type | Body            |
|--------|--------------|-----------------|
| `200`  | `text/plain` | `OK`            |
| `500`  | `text/plain` | `Delete failed` |

#### Example

```
GET http://192.168.1.200/delete/status
```

---

### `GET /delete/control`

Deletes all data in the `sauna_control` measurement from InfluxDB (same time range as `/delete/status`).

#### Response

| Status | Content-Type | Body            |
|--------|--------------|-----------------|
| `200`  | `text/plain` | `OK`            |
| `500`  | `text/plain` | `Delete failed` |

#### Example

```
GET http://192.168.1.200/delete/control
```

---

## 3. WebSocket API (port 81)

#### Connection

```
ws://192.168.1.200:81/
```

#### Message Direction

Server → client only. The client never sends messages; the server does not process any received frames.

#### Trigger Conditions

| Event                          | Behavior                                              |
|--------------------------------|-------------------------------------------------------|
| Client connects                | Server immediately sends one JSON message to that client |
| Every 2 seconds (main loop)    | Server broadcasts one JSON message to all connected clients |

#### JSON Schema

All fields are always present. Temperature fields are in **degrees Fahrenheit**. Fields sourced from DHT sensors become JSON `null` when the sensor reading is `NaN` *or* when the sensor is stale (no valid read in the last 10,000 ms, or never read since boot). The stove temperature (`tct`) becomes `null` on MAX31865 fault or out-of-range reading (-200 to 900 °C); it is never affected by the stale-threshold logic.

| Key     | JSON Type       | Units   | Description                                                                 | Null condition                                          |
|---------|-----------------|---------|-----------------------------------------------------------------------------|---------------------------------------------------------|
| `clt`   | number \| null  | °F      | Ceiling temperature (DHT21, GPIO 8)                                         | Sensor NaN or stale (age > 10,000 ms or never read)     |
| `clh`   | number \| null  | %RH     | Ceiling relative humidity (DHT21, GPIO 8)                                   | Same as `clt`                                           |
| `d5t`   | number \| null  | °F      | Bench temperature (DHT21, GPIO 9)                                           | Sensor NaN or stale (age > 10,000 ms or never read)     |
| `d5h`   | number \| null  | %RH     | Bench relative humidity (DHT21, GPIO 9)                                     | Same as `d5t`                                           |
| `tct`   | number \| null  | °F      | Stove temperature (PT1000 via MAX31865, GPIO 5 CS)                          | MAX31865 fault bit set, or raw reading outside -200–900 °C |
| `ofs`   | number          | %       | Outflow (upper) vent position, 0–100                                        | Never null                                              |
| `ofd`   | number          | —       | Outflow motor direction: `1`=CW (opening), `-1`=CCW (closing), `0`=stopped | Never null                                              |
| `ifs`   | number          | %       | Inflow (lower) vent position, 0–100                                         | Never null                                              |
| `ifd`   | number          | —       | Inflow motor direction: `1`=CW, `-1`=CCW, `0`=stopped                      | Never null                                              |
| `csp`   | number \| null  | °F      | Ceiling PID setpoint                                                        | Null only if internal Ceilingpoint is NaN (should not occur) |
| `cop`   | number \| null  | raw 0–255 | Ceiling PID raw output (QuickPID output, 0–255 scale)                     | Null only if NaN (should not occur)                     |
| `ctm`   | number (0/1)    | —       | Ceiling PID tuning mode: `1`=conservative (error < 10 °C), `0`=aggressive  | Never null                                              |
| `cen`   | number (0/1)    | —       | Ceiling PID enabled: `1`=enabled, `0`=disabled                             | Never null                                              |
| `bsp`   | number \| null  | °F      | Bench PID setpoint                                                          | Null only if internal Benchpoint is NaN (should not occur) |
| `bop`   | number \| null  | raw 0–255 | Bench PID raw output (QuickPID output, 0–255 scale)                       | Null only if NaN (should not occur)                     |
| `btm`   | number (0/1)    | —       | Bench PID tuning mode: `1`=conservative (error < 10 °C), `0`=aggressive    | Never null                                              |
| `ben`   | number (0/1)    | —       | Bench PID enabled: `1`=enabled, `0`=disabled                               | Never null                                              |
| `pvolt` | number \| null  | V       | INA260 bus voltage                                                          | Null if INA260 not detected at boot, or reading is NaN  |
| `pcurr` | number \| null  | mA      | INA260 current                                                              | Same as `pvolt`                                         |
| `pmw`   | number \| null  | mW      | INA260 power                                                                | Same as `pvolt`                                         |
| `oa`    | number (0/1)    | —       | Overheat alarm: `1`=active (any air sensor ≥ 120 °C; vents forced open)    | Never null                                              |
| `cst`   | number (0/1)    | —       | Ceiling sensor stale flag: `1`=stale (no valid read in > 10,000 ms or never read) | Never null                                        |
| `bst`   | number (0/1)    | —       | Bench sensor stale flag: `1`=stale                                          | Never null                                              |

#### Stale Detection Details

The stale threshold is `STALE_THRESHOLD_MS = 10000` ms (10 seconds). A sensor is stale if:
- `last_ok_ms == 0` (device has not received a valid reading since boot), OR
- `millis() - last_ok_ms > 10000`

When `cst=1`, `clt` and `clh` are forced to `null` in the JSON regardless of the raw sensor value. Same applies to `bst` for `d5t`/`d5h`.

#### Example JSON Payload

```json
{
  "clt": 175.4,
  "clh": 18.2,
  "d5t": 148.1,
  "d5h": 22.7,
  "tct": 392.0,
  "ofs": 72,
  "ofd": 0,
  "ifs": 55,
  "ifd": 1,
  "csp": 160.0,
  "cop": 183.0,
  "ctm": 0,
  "cen": 1,
  "bsp": 120.0,
  "bop": 140.0,
  "btm": 1,
  "ben": 1,
  "pvolt": 12.14,
  "pcurr": 341.5,
  "pmw": 4145.0,
  "oa": 0,
  "cst": 0,
  "bst": 0
}
```

---

## 4. MQTT API

#### Broker

Address and port are defined in `secrets.h` as `MQTT_BROKER` and `MQTT_PORT` (default port 1883).

#### Client ID

`sauna_esp32`

#### Authentication

If `MQTT_USER` in `secrets.h` is a non-empty string, connects with username and password (`MQTT_USER`/`MQTT_PASS`). If empty, connects without credentials.

#### Security: Broker ACL Requirement

**The MQTT broker must enforce ACLs for the `sauna/#` topic tree.** The firmware authenticates to the broker with `MQTT_USER`/`MQTT_PASS`, but control topics (`sauna/ceiling_setpoint/set`, `sauna/bench_setpoint/set`, `sauna/ceiling_pid/set`, `sauna/bench_pid/set`) accept commands from any publisher without per-message authentication. Any client with broker access can modify PID setpoints that directly control physical hardware. Configure broker ACLs to restrict publish access on `sauna/+/set` topics to trusted clients only.

#### Reconnect Behavior

If disconnected, the firmware retries every 5,000 ms. On reconnect, HA Discovery payloads are re-published and all subscriptions are re-established.

#### Buffer Size

512 bytes (set via `mqttClient.setBufferSize(512)`).

---

### Published Topic: `sauna/state`

Published every 2 seconds (synchronized with the sensor read cycle). QoS 0, not retained.

**Payload Format** — JSON object, temperatures in **degrees Fahrenheit**:

```json
{
  "ceiling_temp": 175.4,
  "ceiling_hum": 18.2,
  "bench_temp": 148.1,
  "bench_hum": 22.7,
  "stove_temp": 392.0,
  "outflow_pos": 72,
  "inflow_pos": 55,
  "ceiling_pid_out": 71.8,
  "bench_pid_out": 54.9,
  "ceiling_pid_en": "ON",
  "bench_pid_en": "ON",
  "ceiling_setpoint": 160.0,
  "bench_setpoint": 120.0,
  "bus_voltage": 12.14,
  "current_mA": 341.5,
  "power_mW": 4145.0
}
```

#### Field Descriptions

| Field              | JSON Type      | Units | Description                                                                |
|--------------------|----------------|-------|----------------------------------------------------------------------------|
| `ceiling_temp`     | number \| null | °F    | Ceiling DHT21 temperature; `null` if NaN                                   |
| `ceiling_hum`      | number \| null | %RH   | Ceiling DHT21 humidity; `null` if NaN                                      |
| `bench_temp`       | number \| null | °F    | Bench DHT21 temperature; `null` if NaN                                     |
| `bench_hum`        | number \| null | %RH   | Bench DHT21 humidity; `null` if NaN                                        |
| `stove_temp`       | number \| null | °F    | PT1000 stove temp (falls back to ceiling+bench average if PT1000 is NaN); `null` if all sources NaN |
| `outflow_pos`      | number         | %     | Outflow vent position, 0–100                                               |
| `inflow_pos`       | number         | %     | Inflow vent position, 0–100                                                |
| `ceiling_pid_out`  | number         | %     | Ceiling PID output scaled to 0–100% (`raw_output / 255 * 100`)             |
| `bench_pid_out`    | number         | %     | Bench PID output scaled to 0–100%                                          |
| `ceiling_pid_en`   | string         | —     | `"ON"` or `"OFF"`                                                          |
| `bench_pid_en`     | string         | —     | `"ON"` or `"OFF"`                                                          |
| `ceiling_setpoint` | number         | °F    | Ceiling PID setpoint                                                       |
| `bench_setpoint`   | number         | °F    | Bench PID setpoint                                                         |
| `bus_voltage`      | number \| null | V     | INA260 bus voltage; `null` if INA260 absent or NaN                         |
| `current_mA`       | number \| null | mA    | INA260 current; `null` if INA260 absent or NaN                             |
| `power_mW`         | number \| null | mW    | INA260 power; `null` if INA260 absent or NaN                               |

Note: The MQTT payload does not include stale flags (`cst`/`bst`) or motor direction (`ofd`/`ifd`) — those are WebSocket-only fields. The MQTT payload also does not include the overheat alarm flag.

---

### Subscribed Topics

| Topic                         | Accepted Values           | Effect                                                                      |
|-------------------------------|---------------------------|-----------------------------------------------------------------------------|
| `sauna/ceiling_pid/set`       | `ON` / anything else      | `ON` enables ceiling PID; any other value disables it. Persists to NVS.    |
| `sauna/bench_pid/set`         | `ON` / anything else      | `ON` enables bench PID; any other value disables it. Persists to NVS.      |
| `sauna/ceiling_setpoint/set`  | Float string, 32.0–300.0  | Sets ceiling setpoint (°F). Values outside range are silently ignored. Persists to NVS. |
| `sauna/bench_setpoint/set`    | Float string, 32.0–300.0  | Sets bench setpoint (°F). Values outside range are silently ignored. Persists to NVS. |

Payloads longer than 31 characters are silently ignored.

---

### Home Assistant MQTT Discovery

Published once at connection (and on each reconnect) to the standard HA discovery prefix. All payloads are retained (`retain=true`). Device metadata: `identifiers=["sauna_esp32"]`, `name="Sauna"`, `model="ESP32"`, `manufacturer="Custom"`.

**Sensors** (topic prefix: `homeassistant/sensor/sauna_esp32/<id>/config`)

| Entity ID        | Name                  | `value_template`                                        | Unit   | `device_class` |
|------------------|-----------------------|---------------------------------------------------------|--------|----------------|
| `ceiling_temp`   | Ceiling Temperature   | `{{ value_json.ceiling_temp \| round(1) }}`             | °F     | `temperature`  |
| `ceiling_hum`    | Ceiling Humidity      | `{{ value_json.ceiling_hum \| round(1) }}`              | %      | `humidity`     |
| `bench_temp`     | Bench Temperature     | `{{ value_json.bench_temp \| round(1) }}`               | °F     | `temperature`  |
| `bench_hum`      | Bench Humidity        | `{{ value_json.bench_hum \| round(1) }}`                | %      | `humidity`     |
| `stove_temp`     | Stove Temperature     | `{{ value_json.stove_temp \| round(1) }}`               | °F     | `temperature`  |
| `outflow_pos`    | Outflow Position      | `{{ value_json.outflow_pos }}`                          | %      | _(none)_       |
| `inflow_pos`     | Inflow Position       | `{{ value_json.inflow_pos }}`                           | %      | _(none)_       |
| `ceiling_pid_out`| Ceiling PID Output    | `{{ value_json.ceiling_pid_out \| round(1) }}`          | %      | _(none)_       |
| `bench_pid_out`  | Bench PID Output      | `{{ value_json.bench_pid_out \| round(1) }}`            | %      | _(none)_       |
| `bus_voltage`    | Bus Voltage           | `{{ value_json.bus_voltage \| round(2) }}`              | V      | `voltage`      |
| `current_mA`     | Current               | `{{ value_json.current_mA \| round(1) }}`               | mA     | _(none)_       |
| `power_mW`       | Power                 | `{{ value_json.power_mW \| round(1) }}`                 | mW     | _(none)_       |

All sensors have `state_class: measurement` and `state_topic: sauna/state`.

**Switches** (topic prefix: `homeassistant/switch/sauna_esp32/<id>/config`)

| Entity ID      | Name          | `value_template`                        | `command_topic`           |
|----------------|---------------|-----------------------------------------|---------------------------|
| `ceiling_pid`  | Ceiling PID   | `{{ value_json.ceiling_pid_en }}`       | `sauna/ceiling_pid/set`   |
| `bench_pid`    | Bench PID     | `{{ value_json.bench_pid_en }}`         | `sauna/bench_pid/set`     |

**Numbers / Setpoints** (topic prefix: `homeassistant/number/sauna_esp32/<id>/config`)

| Entity ID          | Name              | `value_template`                                              | `command_topic`                  | min | max | step | Unit |
|--------------------|-------------------|---------------------------------------------------------------|----------------------------------|-----|-----|------|------|
| `ceiling_setpoint` | Ceiling Setpoint  | `{{ value_json.ceiling_setpoint \| round(0) \| int }}`        | `sauna/ceiling_setpoint/set`     | 32  | 250 | 1    | °F   |
| `bench_setpoint`   | Bench Setpoint    | `{{ value_json.bench_setpoint \| round(0) \| int }}`          | `sauna/bench_setpoint/set`       | 32  | 250 | 1    | °F   |

---

## 5. InfluxDB

**Endpoint:** `INFLUXDB_URL` from `secrets.h` (default `http://192.168.1.125:30115`)
**Org/Bucket/Token:** defined in `secrets.h`
**Write Interval:** every 60,000 ms (60 seconds), plus on-demand via `GET /log`

Both measurements receive the same two tags:

| Tag      | Value                        |
|----------|------------------------------|
| `device` | `ESP32`                      |
| `SSID`   | Connected WiFi network name  |

---

### Measurement: `sauna_status`

Sensor readings and motor positions.

| Field          | Type    | Units | Written When                                      |
|----------------|---------|-------|---------------------------------------------------|
| `ceiling_temp` | float   | °C    | Only when DHT21 ceiling reading is not NaN        |
| `ceiling_hum`  | float   | %RH   | Only when DHT21 ceiling reading is not NaN        |
| `bench_temp`   | float   | °C    | Only when DHT21 bench reading is not NaN          |
| `bench_hum`    | float   | %RH   | Only when DHT21 bench reading is not NaN          |
| `stove_temp`   | float   | °C    | PT1000 if valid; otherwise ceiling+bench average; omitted if all NaN |
| `bus_voltage_V`| float   | V     | Only when INA260 detected at boot and value is not NaN |
| `current_mA`   | float   | mA    | Only when INA260 detected at boot and value is not NaN |
| `power_mW`     | float   | mW    | Only when INA260 detected at boot and value is not NaN |

Note: InfluxDB temperatures are written in **degrees Celsius** (the raw sensor values), unlike the HTTP/MQTT/WebSocket APIs which use Fahrenheit.

---

### Measurement: `sauna_control`

PID controller state and motor output. All fields are always written.

| Field               | Type    | Units | Description                                    |
|---------------------|---------|-------|------------------------------------------------|
| `outflow_pos`       | int     | %     | Outflow vent position, 0–100                   |
| `inflow_pos`        | int     | %     | Inflow vent position, 0–100                    |
| `ceiling_setpoint`  | float   | °C    | Ceiling PID setpoint (stored internally in °C) |
| `ceiling_pid_out`   | float   | 0–255 | Ceiling PID raw output value                   |
| `bench_setpoint`    | float   | °C    | Bench PID setpoint (stored internally in °C)   |
| `bench_pid_out`     | float   | 0–255 | Bench PID raw output value                     |

---

### History Proxy Endpoint

See `GET /history` in Section 2. The device proxies Flux queries to avoid exposing the InfluxDB token to the browser. The query is hard-coded to `sauna_status` with 5-minute mean aggregation over `ceiling_temp`, `bench_temp`, and `stove_temp`.

---

## 6. Configuration

→ See `docs/config-reference.md` for the full 3-tier configuration system (build flags, LittleFS `/config.json`, NVS keys).
