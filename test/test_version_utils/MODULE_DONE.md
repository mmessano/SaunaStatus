STATUS: PASS
ITERATIONS: 2
FUNCTIONS_ADDED: formatVersion, isDowngrade, isSameVersion

NOTES:
- All three functions added to src/ota_logic.h after isUpdateAvailable.
- formatVersion: uses snprintf("") for invalid versions — produces empty string,
  null-terminated. A zero-length format string warning is emitted but harmless.
- isDowngrade: delegates to compareVersion(manifest, current) < 0; returns false
  if either version is invalid (no direction can be determined).
- isSameVersion: explicitly requires both valid before comparing; two invalid
  versions are NOT considered equal (both-invalid returns false).
- platformio.ini native build_flags required -Wno-narrowing to compile the test
  file's {0xFF, 0xFF, 0xFF} char array initializer under c++14 on GCC.
