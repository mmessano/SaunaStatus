STATUS: PASS
ITERATIONS: 1
FUNCTIONS_ADDED: buildConfigJson(const SaunaConfig& cfg, char* buf, size_t len) — added to src/sauna_logic.h after mergeConfigLayer, before SensorValues
NOTES: Setpoints are stored directly in °F in SaunaConfig, so no conversion needed. Used snprintf with %.1f for one-decimal-place float formatting and %d cast for bool flags. Output fits in 63 bytes (well under 64-byte buffer limit).
