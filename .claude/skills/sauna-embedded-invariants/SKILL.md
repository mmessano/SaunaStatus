# sauna-embedded-invariants

Evolved from 3 instincts (avg confidence: 92%)

## When to Apply

Trigger: when writing or modifying embedded firmware code in SaunaStatus

## Actions

- After adding any field to `buildJsonFull()` (or `buildJson()`), audit ALL six consumers:
- Every NVS read must be preceded by `prefs.isKey("<key>")`. A missing key must never
silently revert a fleet config value to a default.
- - On any read failure: set `sensor_temp = NAN; sensor_hum = NAN;`
- Gate `last_ok_ms = millis()` on `!isnan(t) || !isnan(h)` (not `&&`)
- Apply NaN checks to ALL consumers: JSON, MQTT, InfluxDB, PID, serial log
- See `.claude/rules/sensor-patterns.md` for full rule set
