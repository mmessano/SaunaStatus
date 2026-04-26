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

SOURCE_FILES = (
    REPO_ROOT / "src" / "main.cpp",
    REPO_ROOT / "src" / "auth_logic.h",
    REPO_ROOT / "src" / "ota_logic.h",
    REPO_ROOT / "src" / "sauna_logic.h",
)

# Keep this list intentionally narrow: these are the values most likely to drift
# because they are security- or recovery-sensitive and explicitly documented.
CONSTANTS_TO_VERIFY = (
    "AUTH_TOKEN_TTL_MS",
    "AUTH_MAX_SESSIONS",
    "AUTH_MAX_USERS",
    "AUTH_MIN_PASS_LEN",
    "AUTH_MAX_PASS_LEN",
    "AUTH_MIN_USER_LEN",
    "AUTH_MAX_USER_LEN",
    "AUTH_PBKDF2_ITERATIONS",
    "AUTH_RATE_LIMIT_MAX_FAILURES",
    "AUTH_RATE_LIMIT_WINDOW_MS",
    "AUTH_RATE_LIMIT_LOCKOUT_MS",
    "AUTH_RATE_LIMIT_SLOTS",
    "SENSOR_READ_INTERVAL_MIN_MS",
    "SENSOR_READ_INTERVAL_MAX_MS",
    "SERIAL_LOG_INTERVAL_MIN_MS",
    "SERIAL_LOG_INTERVAL_MAX_MS",
    "OTA_ALLOWED_HOSTS",
    "OTA_MAX_BOOT_FAILURES",
    "OVERHEAT_CLEAR_HYSTERESIS_C",
)

ROUTE_RE = re.compile(
    r'server\.on\("(?P<path>[^"]+)"(?:,\s*(?P<method>HTTP_[A-Z]+))?'
)
DOC_ROUTE_RE = re.compile(r"^###\s+`(?P<method>[A-Z]+)\s+(?P<path>/[^`]*)`\s*$")
DEFINE_RE = re.compile(r"^\s*#define\s+(?P<name>[A-Z0-9_]+)\s+(?P<value>.+?)\s*$")
DOC_NAME_RE = re.compile(r"^\s*`(?P<name>[A-Z0-9_]+)`\s*$")
DOC_VALUE_RE = re.compile(r"`(?P<value>[^`]+)`")


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

    for name in CONSTANTS_TO_VERIFY:
        source_value = source_defines.get(name)
        if source_value is None:
            errors.append(f"Missing source definition for {name}")
            continue
        documented_value = documented_constants.get(name)
        if documented_value is None:
            errors.append(f"Missing {name} row in docs/config-reference.md")
            continue
        if documented_value != source_value:
            errors.append(
                f"Value mismatch for {name}: docs={documented_value} source={source_value}"
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
