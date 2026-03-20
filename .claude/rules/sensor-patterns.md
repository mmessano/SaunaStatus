# Sensor Patterns — Rules for Future Sessions

Lessons derived from bugs fixed during ESP32 SaunaStatus development.
Apply these rules whenever touching sensor reading, staleness, or timing code.

---

## Rule 1: Never retain stale values — clear to NaN on failure

When a sensor read fails or times out, immediately set its output variables to `NAN`. Do not leave the previous valid reading in place.

```cpp
// WRONG — stale value silently persists
if (dht.read()) {
    ceiling_temp = dht.readTemperature();
}

// RIGHT — failure path clears to NaN
float t = dhtCeiling.readTemperature();
float h = dhtCeiling.readHumidity();
if (!isnan(t) || !isnan(h)) {
    ceiling_temp = t;
    ceiling_hum  = h;
    ceiling_last_ok_ms = millis();
} else {
    ceiling_temp = NAN;
    ceiling_hum  = NAN;
}
```

---

## Rule 2: Use `||` not `&&` to gate `last_ok_ms` updates

A DHT sensor returns temperature and humidity as separate readings, either of which can be NaN independently. Gate the timestamp update on **either** reading succeeding, not both. Using `&&` falsely declares the sensor dead when only one channel fails, triggering stale detection and unintended vent closure.

```cpp
// WRONG — both must succeed; a failed humidity channel kills the sensor
if (!isnan(t) && !isnan(h))
    ceiling_last_ok_ms = millis();

// RIGHT — either succeeding means the sensor is alive
if (!isnan(t) || !isnan(h))
    ceiling_last_ok_ms = millis();
```

---

## Rule 3: Each sensor must fail independently

Sensor A failing must never affect the reported state or computed outputs of Sensor B. Do not average, share, or cross-pollute per-sensor variables.

- Separate `last_ok_ms` timestamps per sensor
- Separate NaN checks per sensor
- Separate stale flags (`cst` / `bst`) per sensor
- Staleness of ceiling sensor does not force bench sensor values to null, and vice versa

---

## Rule 4: Apply validity checks to ALL consumers, not just the display path

When a new validity condition is introduced (NaN guard, staleness, range check), audit every consumer of that sensor value:

| Consumer | Check required |
|---|---|
| WebSocket JSON | ✅ null on NaN or stale |
| MQTT publish | ✅ null or omit on NaN |
| InfluxDB write | ✅ omit field on NaN |
| PID input | ✅ skip computation on NaN |
| Serial log | ✅ print "---" or similar on NaN |
| Overheat check | ✅ ignore NaN readings (`!isnan(x) &&`) |

The stale-detection bug was added to `buildJsonFull()` for display but PID controllers only checked `!isnan()` — a stale-but-non-NaN reading continued driving motors. Never assume "display is fixed = feature is complete."

---

## Rule 5: Expose all timing parameters as `#ifndef`-guarded `#define`s

Any millisecond interval, threshold, or timeout must be a named constant, not a magic number. Use `#ifndef` guards so values can be overridden at build time via `platformio.ini` without touching source.

```cpp
// WRONG
if (millis() - last_ok_ms > 10000) { ... }

// RIGHT
#ifndef STALE_THRESHOLD_MS
#define STALE_THRESHOLD_MS 10000UL
#endif
if (millis() - last_ok_ms > STALE_THRESHOLD_MS) { ... }
```

Define validation bounds the same way:

```cpp
#ifndef SENSOR_READ_INTERVAL_MIN_MS
#define SENSOR_READ_INTERVAL_MIN_MS 500UL
#endif
#ifndef SENSOR_READ_INTERVAL_MAX_MS
#define SENSOR_READ_INTERVAL_MAX_MS 10000UL
#endif
```

Document every tunable define in the Build-Flag Overrides table in `CLAUDE.md`.

---

## Rule 6: Verify all JSON schema consumers when adding a new field

When adding a field to the WebSocket JSON schema:

1. Add it to `buildJsonFull()` in `sauna_logic.h`
2. Add it to the schema table in `CLAUDE.md`
3. Add it to `docs/api-reference.md`
4. Check the dashboard JS (`data/index.html`) actually reads and renders it
5. Add a unit test in `test/test_websocket/` asserting its presence and value

The `cst`/`bst` stale flags were computed and transmitted correctly for multiple sessions before the UI was found to silently ignore them. "It's in the JSON" is not the same as "it's being used."

---

## Rule 7: Stale detection and NaN checking are separate concerns

- **NaN** — the sensor returned a bad floating-point value on the last read attempt
- **Stale** — the sensor has not returned any successful reading within `STALE_THRESHOLD_MS`

A sensor can be non-NaN but stale (value is valid but old). Stale detection lives in `buildJsonFull()` / `isSensorStale()` in `sauna_logic.h`. NaN checking happens at the point of use (PID, InfluxDB, serial). Both must be applied; one does not substitute for the other.

---

## Rule 8: De-energize stepper coils after positioning

After a motor reaches its target position, the coils should be released to avoid heat buildup in an already-hot sauna enclosure. The 28BYJ-48 is rated for ≤50 °C ambient; continuous energization adds unnecessary thermal load.

When adding motor control logic, include coil release after the target is reached unless continuous holding torque is explicitly required.
