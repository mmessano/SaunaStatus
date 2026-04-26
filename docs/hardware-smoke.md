# Hardware Smoke Test

Post-flash and recovery checklist for behaviors that the native test suite cannot cover.

Use this after:

- flashing new firmware
- uploading a new LittleFS image
- changing Wi-Fi, MQTT, OTA, or safety logic
- replacing ESP32-S3 hardware or sensor wiring

## Preconditions

- Firmware flashed to the target board
- LittleFS uploaded with `index.html`, `config.html`, `login.html`, and optional `config.json`
- Serial monitor attached at `115200`
- Web browser on the same network
- MQTT broker reachable
- If testing OTA: a reachable HTTPS manifest host present in `OTA_ALLOWED_HOSTS`

## 1. Boot And LittleFS

1. Power-cycle the device.
2. Watch serial output from boot through Wi-Fi connection.
3. Confirm one of these expected paths:
   - Healthy path: LittleFS mounts, `/config.json` is loaded or reported missing, and boot continues normally.
   - Degraded path: `LittleFS mount failed — preserving existing filesystem contents` is printed, followed by the degraded-mode message.
4. In the healthy path, open `/`, `/config`, and `/auth/login` in a browser and confirm each page renders.
5. In the degraded path, open `/`, `/config`, and `/auth/login` and confirm each returns the documented `503` filesystem-unavailable response instead of a blank page or silent fallback.

## 2. Fleet Config Boot Behavior

1. If a fleet `data/config.json` is present, verify the runtime values match it via `GET /config/get`.
2. Confirm valid values apply:
   - `ceiling_setpoint_f`
   - `bench_setpoint_f`
   - `ceiling_pid_en` / `bench_pid_en`
   - `sensor_read_interval_ms`
   - `serial_log_interval_ms`
3. If testing malformed or invalid fleet config:
   - malformed file: confirm serial reports a JSON parse error and Tier 1 defaults remain active
   - one bad field plus other good fields: confirm only the invalid field is ignored
4. If NVS already contains per-device overrides, confirm they still win over fleet defaults after boot.

## 3. Wi-Fi Boot And Web UI

1. Confirm the device joins Wi-Fi and the expected IP is shown or reachable.
2. Open the dashboard root page and verify WebSocket live updates begin.
3. Check `GET /config/get` and confirm:
   - `static_ip` is the expected active address
   - `device_name` is the expected runtime name
4. If `static_ip` or `device_name` was changed via config:
   - verify the old values remain until reboot if the change requires restart
   - reboot and confirm the new values take effect

## 4. Auth And RBAC Smoke

1. Log in as an admin account and confirm:
   - admin controls are visible
   - `/auth/status` returns authenticated state and role
2. Log in as a viewer account and confirm:
   - non-privileged dashboard remains usable
   - admin-only controls are hidden
   - privileged routes return `403` if called directly
3. Confirm logout works for both roles.

## 5. MQTT Connect And Reconnect

1. Verify the device connects to the configured MQTT broker after boot.
2. Confirm state publishing resumes on the expected topic tree.
3. Temporarily interrupt broker availability:
   - stop the broker or block access
   - wait for reconnect attempts
4. Restore broker availability and confirm:
   - the device reconnects without reboot
   - state publishes resume
   - no config, auth, or vent-control state is lost

## 6. OTA Happy Path

1. Call `GET /ota/status` and record current version and partition.
2. Trigger `POST /ota/update?manifest=<url>` with a manifest that points to a newer firmware.
3. Confirm expected behavior:
   - manifest fetch succeeds
   - version check rejects neither as downgrade nor same-version
   - download starts and completes
   - reboot occurs
4. After reboot, verify:
   - `GET /ota/status` reports the new version
   - the active partition changed as expected
   - LittleFS pages and auth still work
   - `boot_fail` was cleared by successful boot completion

## 7. OTA Failure And Recovery

1. Same-version manifest:
   - trigger OTA with the current version
   - confirm it is refused cleanly
2. Downgrade manifest:
   - trigger OTA with an older version
   - confirm it is refused cleanly
3. Interrupted download:
   - start an OTA update
   - interrupt power or network before completion
   - reboot
   - confirm the incomplete-download warning path is reported and the device remains recoverable
4. Host allowlist:
   - use a manifest URL whose host is not in `OTA_ALLOWED_HOSTS`
   - confirm OTA is rejected before flashing

## 8. Overheat Safety

1. Put the unit in a safe test setup where vent movement can be observed without heating the sauna dangerously.
2. Simulate or induce an overheat condition on ceiling or bench temperature input.
3. Confirm:
   - `overheat_alarm` becomes active
   - both vents drive fully open once on alarm onset
   - normal PID vent control is suppressed while alarmed
4. Return temperatures to safe values and confirm:
   - alarm clears
   - normal control resumes
5. Pay attention to current firmware behavior:
   - `tickOverheat()` test logic includes a clear hysteresis band
   - `checkOverheat()` hardware path currently clears as soon as temps fall below threshold

## 9. Result Recording

Capture:

- firmware version tested
- board identifier
- LittleFS image version or commit
- Wi-Fi and MQTT environment used
- OTA manifest URL used, if any
- pass/fail for each section
- serial log excerpts for any failure

If any step fails, note whether the failure is:

- firmware regression
- environment issue
- provisioning error
- expected degraded/recovery behavior
