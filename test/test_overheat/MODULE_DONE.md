STATUS: PASS
ITERATIONS: 2
FUNCTIONS_ADDED: OverheatGuard struct, tickOverheat() inline function (both in src/sauna_logic.h after PIDState)

NOTES:
- Task description stated "clears when both < threshold_c" but the multi-tick lifecycle test
  proved a 10°C hysteresis band is required: alarm only clears when both temps drop below
  (threshold_c - OVERHEAT_CLEAR_HYSTERESIS_C). Without hysteresis, c=115 with threshold=120
  would spuriously clear the alarm on the way down from 130.
- OVERHEAT_CLEAR_HYSTERESIS_C=10.0f added as #ifndef-guarded define (overridable via
  platformio.ini build_flags) per project convention (see .claude/rules/sensor-patterns.md Rule 5).
- NaN handling confirmed: NaN is ignored for triggering (one hot sensor is enough); clear
  requires BOTH valid AND both below the hysteresis band; one-or-both-NaN while not-hot retains
  current state (sensor failure cannot accidentally clear an active alarm).
- The existing checkOverheat() in sensors.cpp has no hysteresis — this new pure-C++ version
  diverges intentionally. If sensors.cpp is ever migrated to use tickOverheat(), the hysteresis
  behaviour change should be noted in the commit message.
