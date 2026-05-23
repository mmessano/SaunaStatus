#!/usr/bin/env python3
"""Verify route and constant documentation against the current source tree."""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
MAIN_CPP = REPO_ROOT / "src" / "main.cpp"
CONFIG_DOC = REPO_ROOT / "docs" / "config-reference.md"
API_DOC = REPO_ROOT / "docs" / "api-reference.md"
PLATFORMIO_INI = REPO_ROOT / "platformio.ini"

SOURCE_FILES = (
    REPO_ROOT / "src" / "main.cpp",
    REPO_ROOT / "src" / "auth_logic.h",
    REPO_ROOT / "src" / "ota_logic.h",
    REPO_ROOT / "src" / "sauna_logic.h",
)

# Source files scanned for prefs.* NVS key usage. Anything that actually
# touches the Preferences API lives in a .cpp; the headers carry only types
# and externs.
NVS_SOURCE_FILES = tuple(REPO_ROOT.glob("src/*.cpp")) + tuple(REPO_ROOT.glob("src/*.h"))

# NVS keys that aren't in the Tier 3 table: namespace strings and the
# sauna_auth-namespace keys (documented as prose in config-reference.md
# because the user store keys are templated u<N>_name / u<N>_hash / …).
NVS_KEYS_OUTSIDE_TABLE = frozenset({
    # Namespace identifiers passed to prefs.begin()
    "sauna", "sauna_auth",
    # External-auth adapter config (sauna_auth namespace, documented in prose)
    "db_url", "db_key",
    # User-store template key — only u0_name appears as a literal in source
    # (used to detect the "first-boot, no users" state); u1..u4 are formed
    # at runtime via snprintf.
    "u0_name",
})

ROUTE_RE = re.compile(
    r'server\.on\("(?P<path>[^"]+)"(?:,\s*(?P<method>HTTP_[A-Z]+))?'
)
DOC_ROUTE_RE = re.compile(r"^###\s+`(?P<method>[A-Z]+)\s+(?P<path>/[^`]*)`\s*$")
DEFINE_RE = re.compile(r"^\s*#define\s+(?P<name>[A-Z0-9_]+)\s+(?P<value>.+?)\s*$")
DOC_NAME_RE = re.compile(r"^\s*`(?P<name>[A-Z0-9_]+)`\s*$")
DOC_VALUE_RE = re.compile(r"`(?P<value>[^`]+)`")
NVS_CALL_RE = re.compile(r'\bprefs\.[A-Za-z]+\(\s*"(?P<key>[A-Za-z0-9_]+)"')
DOC_NVS_KEY_RE = re.compile(r"^`(?P<key>[a-z][a-z0-9_]*)`$")
# Captures active (uncommented) -DNAME=value build flags from platformio.ini.
# Escaped quotes (\") in the value become real quotes for comparison with the
# documented `"2.0.0"` style.
BUILD_FLAG_RE = re.compile(r'^\s*-D(?P<name>[A-Z0-9_]+)=(?P<value>\S+)\s*$')


def _read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def extract_source_routes() -> set[tuple[str, str]]:
    routes: set[tuple[str, str]] = set()
    for raw_line in _read_text(MAIN_CPP).splitlines():
        match = ROUTE_RE.search(raw_line)
        if not match:
            continue
        method = match.group("method")
        # Omitted method registrations are the read-only handlers in this repo.
        normalized_method = method.removeprefix("HTTP_") if method else "GET"
        routes.add((normalized_method, match.group("path")))
    return routes


def extract_documented_routes() -> set[tuple[str, str]]:
    routes: set[tuple[str, str]] = set()
    for raw_line in _read_text(API_DOC).splitlines():
        match = DOC_ROUTE_RE.match(raw_line.strip())
        if match:
            routes.add((match.group("method"), match.group("path")))
    return routes


def extract_source_defines() -> dict[str, str]:
    defines: dict[str, str] = {}
    for path in SOURCE_FILES:
        for raw_line in _read_text(path).splitlines():
            match = DEFINE_RE.match(raw_line)
            if not match:
                continue
            value = match.group("value").split("//", 1)[0].strip()
            defines.setdefault(match.group("name"), value)
    # Active build-flag -D NAME=VALUE entries in platformio.ini are also a
    # legitimate source of compile-time constants (FIRMWARE_VERSION lives
    # there, by design). Commented-out lines (starting with ; or #) are skipped.
    for raw_line in _read_text(PLATFORMIO_INI).splitlines():
        stripped = raw_line.lstrip()
        if stripped.startswith(';') or stripped.startswith('#'):
            continue
        match = BUILD_FLAG_RE.match(raw_line)
        if not match:
            continue
        value = match.group("value").replace('\\"', '"')
        defines.setdefault(match.group("name"), value)
    return defines


def extract_documented_constants() -> dict[str, str]:
    constants: dict[str, str] = {}
    in_tier_one = False
    for raw_line in _read_text(CONFIG_DOC).splitlines():
        line = raw_line.strip()
        if line == "## Tier 1: Compile-Time / Build Flags":
            in_tier_one = True
            continue
        if in_tier_one and line.startswith("## "):
            break
        if not in_tier_one:
            continue
        if not line.startswith("|"):
            continue
        columns = [col.strip() for col in line.strip("|").split("|")]
        if len(columns) < 2:
            continue
        name_match = DOC_NAME_RE.match(columns[0])
        value_match = DOC_VALUE_RE.search(columns[1])
        if name_match and value_match:
            constants[name_match.group("name")] = value_match.group("value").strip()
    return constants


def extract_source_nvs_keys() -> set[str]:
    keys: set[str] = set()
    for path in NVS_SOURCE_FILES:
        for raw_line in _read_text(path).splitlines():
            for match in NVS_CALL_RE.finditer(raw_line):
                keys.add(match.group("key"))
    return keys - NVS_KEYS_OUTSIDE_TABLE


def extract_documented_nvs_keys() -> set[str]:
    """Pull NVS keys out of the Tier 3 table in config-reference.md."""
    keys: set[str] = set()
    in_tier_three = False
    for raw_line in _read_text(CONFIG_DOC).splitlines():
        line = raw_line.strip()
        if line == "## Tier 3: Per-Device NVS":
            in_tier_three = True
            continue
        # Stop at the next top-level section OR at the start of the
        # sauna_auth sub-section, which lists its keys as prose, not a table.
        if in_tier_three and (line.startswith("## ") or line.startswith("### Namespace")):
            break
        if not in_tier_three or not line.startswith("|"):
            continue
        columns = [col.strip() for col in line.strip("|").split("|")]
        if not columns:
            continue
        match = DOC_NVS_KEY_RE.match(columns[0])
        if match:
            keys.add(match.group("key"))
    return keys


def run_checks() -> list[str]:
    errors: list[str] = []

    source_routes = extract_source_routes()
    documented_routes = extract_documented_routes()

    missing_routes = sorted(source_routes - documented_routes)
    extra_routes = sorted(documented_routes - source_routes)

    if missing_routes:
        formatted = ", ".join(f"{method} {path}" for method, path in missing_routes)
        errors.append(f"Undocumented routes in docs/api-reference.md: {formatted}")
    if extra_routes:
        formatted = ", ".join(f"{method} {path}" for method, path in extra_routes)
        errors.append(f"Stale routes in docs/api-reference.md: {formatted}")

    source_defines = extract_source_defines()
    documented_constants = extract_documented_constants()

    # Every constant the docs claim is checked against the source. Adding a
    # row to the Tier 1 table without a matching #define (or vice versa) is
    # caught automatically — no whitelist to maintain.
    for name, documented_value in sorted(documented_constants.items()):
        source_value = source_defines.get(name)
        if source_value is None:
            errors.append(f"Missing source definition for {name}")
            continue
        if documented_value != source_value:
            errors.append(
                f"Value mismatch for {name}: docs={documented_value} source={source_value}"
            )

    source_nvs_keys = extract_source_nvs_keys()
    documented_nvs_keys = extract_documented_nvs_keys()

    missing_nvs = sorted(source_nvs_keys - documented_nvs_keys)
    extra_nvs = sorted(documented_nvs_keys - source_nvs_keys)

    if missing_nvs:
        errors.append(
            "Undocumented NVS keys in docs/config-reference.md Tier 3: "
            + ", ".join(missing_nvs)
        )
    if extra_nvs:
        errors.append(
            "Stale NVS keys in docs/config-reference.md Tier 3 (no longer referenced in source): "
            + ", ".join(extra_nvs)
        )

    return errors


def main() -> int:
    errors = run_checks()
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    print("Doc drift verification passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
