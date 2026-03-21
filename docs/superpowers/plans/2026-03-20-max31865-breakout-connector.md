# MAX31865 Breakout Connector Replacement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the bare MAX31865 IC (U2) and 6-pin PT1000 connector (J1) in the SaunaStatus schematic and PCB with a single 8-pin connector (J1) for the Adafruit MAX31865 breakout board, preserving all SPI GPIO assignments.

**Architecture:** Edit the KiCad schematic text file directly using Python scripts, removing 42 elements (symbol instances, wires, labels, no_connect markers, power symbols) by UUID and inserting the new connector symbol, wires, net labels, and no_connect markers. Update the PCB via KiCad's "Update PCB from Schematic" workflow. Verify with ERC, DRC, and the native unit test suite.

**Tech Stack:** Python 3, KiCad 9 (.kicad_sch S-expression format), PlatformIO (pio test -e native), KiCad MCP server (ERC/DRC verification).

---

## Reference: Elements to Remove

All elements belong to U2 (MAX31865xAP) or old J1 (Conn_01x06) and their exclusive connections.

### Symbol instances
| UUID | What |
|------|------|
| `2da7b3a7-1a8e-453e-881f-7d15e71429bc` | U2 (MAX31865xAP) |
| `751101c5-e1bb-4ea9-93f3-dd5c2c20c183` | J1 (Conn_01x06) |
| `06e4dd75-2b70-4a5a-a7b0-ff358c733574` | +3V3 at (152.4, 63.5) — U2 DVDD supply |
| `06f7d7a1-81b8-442d-96f3-daccb1eaf590` | GND at (152.4, 111.76) — U2 GND supply |
| `8ba60050-ecbf-4d29-8ce5-c2120da22408` | +3V3 at (147.32, 63.5) — U2 VDD supply |
| `98c3a802-ab95-4d3f-bd69-15e3973bbed3` | GND at (147.32, 111.76) — U2 AGND supply |

### Net labels
| UUID | Label | Position |
|------|-------|----------|
| `53d43e4f-cc0d-465f-bfb9-564d3cd9fd45` | SPI_MOSI | (124.46, 76.2, 180°) — U2 left |
| `61941e15-187f-4d35-af78-44c68c55c8da` | SPI_SCK | (124.46, 78.74, 180°) — U2 left |
| `e13f319c-6931-4e34-919e-6f92bf84646d` | SPI_CS | (124.46, 81.28, 180°) — U2 left |
| `f7450236-a6b3-4776-971b-d0bdb4824ee4` | SPI_MISO | (124.46, 83.82, 180°) — U2 left |
| `06f3fc3b-c007-4dab-af4d-f0fbee37a155` | FORCE- | (175.26, 101.6, 0°) — U2 right |
| `161085e1-4475-4697-b04e-9cbb6ef4c71e` | FORCE+ | (175.26, 88.9, 0°) — U2 right |
| `7fad809c-3f1f-475d-b1aa-ff9440c47c5e` | REFIN- | (175.26, 81.28, 0°) — U2 right |
| `a88d9e11-7f5d-4c83-b270-ccf4933c9595` | REFIN+ | (175.26, 76.2, 0°) — U2 right |
| `c80500a1-81ad-4939-8190-217e11909cbd` | RTDIN+ | (175.26, 93.98, 0°) — U2 right |
| `cc219411-1b96-49ba-a748-ddb378b45457` | RTDIN- | (175.26, 99.06, 0°) — U2 right |
| `30014368-d7f1-413d-a50f-761430f4a9f4` | FORCE- | (215.9, 90.17, 180°) — J1 left |
| `6d306804-697a-4f01-af23-bf7b02153329` | RTDIN+ | (215.9, 85.09, 180°) — J1 left |
| `7d934810-dbb1-4efd-8d4a-4e08f31835e2` | REFIN+ | (215.9, 80.01, 180°) — J1 left |
| `8a65a225-b092-4537-ae0a-60c08a1a3444` | FORCE+ | (215.9, 82.55, 180°) — J1 left |
| `95f02c83-52c8-4f68-9645-159de6c85487` | RTDIN- | (215.9, 87.63, 180°) — J1 left |
| `9f446976-103e-42ea-aa15-e46ac0f0606f` | REFIN- | (215.9, 92.71, 180°) — J1 left |

### Wires
| UUID | From → To | Net |
|------|-----------|-----|
| `d1b839c6-6a15-4b84-b9d9-572fc8cd4cc0` | (134.62,76.2)→(124.46,76.2) | SPI_MOSI |
| `019e015b-8a52-4c21-a958-589ffb6d620e` | (134.62,78.74)→(124.46,78.74) | SPI_SCK |
| `62531def-d7dd-486e-8023-43c38b98b5a4` | (134.62,81.28)→(124.46,81.28) | SPI_CS |
| `851c38eb-411b-46a3-96c9-b975d8ffde3c` | (134.62,83.82)→(124.46,83.82) | SPI_MISO |
| `899aa009-c48d-4820-ad37-ca2d33711a3c` | (165.1,76.2)→(175.26,76.2) | REFIN+ |
| `f5e59760-c2cf-424c-8dd9-c40222f428bf` | (165.1,81.28)→(175.26,81.28) | REFIN- |
| `d8fc72be-af30-49ee-b869-3c7529d1527b` | (165.1,88.9)→(175.26,88.9) | FORCE+ |
| `fdf239c5-3644-467e-ade5-7ae248d7e657` | (165.1,93.98)→(175.26,93.98) | RTDIN+ |
| `e5c83c8e-dc49-402e-b249-efd7472292ed` | (165.1,99.06)→(175.26,99.06) | RTDIN- |
| `69fc53d6-c201-4de6-b76f-565983d0f354` | (165.1,101.6)→(175.26,101.6) | FORCE- |
| `063fe94c-e58b-467a-b729-4acdd005eb3e` | (152.4,68.58)→(152.4,63.5) | +3V3 DVDD |
| `a692dbb5-79b1-473e-b411-25858e3390db` | (147.32,68.58)→(147.32,63.5) | +3V3 VDD |
| `068786b2-aee4-4a33-831a-24026ee20c46` | (147.32,106.68)→(147.32,111.76) | GND AGND |
| `b1f09643-82c4-4329-841a-4993a57102d3` | (152.4,106.68)→(152.4,111.76) | GND DGND |
| `3af109e1-5b21-4efb-ac47-a2320482b11e` | (226.06,82.55)→(215.9,82.55) | FORCE+ |
| `51d06e50-c85e-40bf-b8a6-b36f6bd445a9` | (226.06,87.63)→(215.9,87.63) | RTDIN- |
| `8a82ec28-92c4-4be4-8ead-f50a493939db` | (226.06,85.09)→(215.9,85.09) | RTDIN+ |
| `8f6df7a1-1e76-4160-acbb-f6b4d77883b8` | (226.06,92.71)→(215.9,92.71) | REFIN- |
| `e84c9f09-4f78-4edd-ab70-d78eebc19b42` | (226.06,80.01)→(215.9,80.01) | REFIN+ |
| `ec744f26-b346-4d1c-8996-d788a013e180` | (226.06,90.17)→(215.9,90.17) | FORCE- |

### No-connect markers
| UUID | Position | U2 pin |
|------|----------|--------|
| `7f780edc-4ebc-4bef-b220-0926bc83ac65` | (134.62, 73.66) | ~{DRDY} (left side) |
| `5433a945-2878-451f-8b5b-ce1b40bcbd1c` | (165.1, 73.66) | BIAS (right side) |
| `cf74f2ac-c8d1-4130-b30d-0b2a24895eb7` | (165.1, 83.82) | FORCE2 (right side) |
| `d493eed5-e284-438e-978e-837d05058bb6` | (165.1, 91.44) | NC (right side) |

### lib_symbols to replace
- Remove: `Sensor_Temperature:MAX31865xAP` (used only by U2)
- Replace: `Connector_Generic:Conn_01x06` → `Connector_Generic:Conn_01x08` (J1 is the only user)

---

## Reference: New J1 Connector

**New J1:** `Connector_Generic:Conn_01x08`, placed at **(231.14, 85.09)** (same origin as old J1).
**Value:** `Adafruit-MAX31865-Breakout`
**Footprint:** `Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Vertical`

Pin absolute coordinates (lib_x = −5.08, lib_y per pin):

| Pin | Function | abs_x | abs_y | Connection |
|-----|----------|-------|-------|------------|
| 1 | VIN | 226.06 | 77.47 | net label `+3V3` at (215.9, 77.47) via wire |
| 2 | 3Vo | 226.06 | 80.01 | no_connect |
| 3 | GND | 226.06 | 82.55 | net label `GND` at (215.9, 82.55) via wire |
| 4 | CLK | 226.06 | 85.09 | net label `SPI_SCK` at (215.9, 85.09) via wire |
| 5 | SDO | 226.06 | 87.63 | net label `SPI_MISO` at (215.9, 87.63) via wire |
| 6 | SDI | 226.06 | 90.17 | net label `SPI_MOSI` at (215.9, 90.17) via wire |
| 7 | CS | 226.06 | 92.71 | net label `SPI_CS` at (215.9, 92.71) via wire |
| 8 | RDY | 226.06 | 95.25 | no_connect |

All wires run horizontally from (226.06, y) to (215.9, y). Net labels at angle 180°, justify right.

---

## Task 1: Back Up Schematic and PCB

**Files:**
- Read/Write: `docs/kicad/SaunaStatus.kicad_sch`
- Read/Write: `docs/kicad/SaunaStatus.kicad_pcb`

- [ ] **Step 1: Create backups**

```bash
cp docs/kicad/SaunaStatus.kicad_sch docs/kicad/SaunaStatus.kicad_sch.bak
cp docs/kicad/SaunaStatus.kicad_pcb docs/kicad/SaunaStatus.kicad_pcb.bak
```

- [ ] **Step 2: Verify backups exist**

```bash
ls -lh docs/kicad/SaunaStatus.kicad_sch.bak docs/kicad/SaunaStatus.kicad_pcb.bak
```

Expected: both files present with non-zero size.

---

## Task 2: Remove All U2 and J1 Elements from Schematic

**Files:**
- Modify: `docs/kicad/SaunaStatus.kicad_sch`
- Create: `docs/kicad/remove_u2_j1.py` (temporary script)

This task uses a Python script that finds and removes S-expression blocks by UUID. Each block starts at a `(` that contains the target UUID and ends at the matching `)`.

- [ ] **Step 1: Write the removal script**

```python
#!/usr/bin/env python3
"""Remove U2, old J1, and all their exclusive connections from SaunaStatus.kicad_sch."""
import re
import sys

SCH = "docs/kicad/SaunaStatus.kicad_sch"

# All UUIDs to remove — symbol instances, labels, wires, no_connect markers
REMOVE_UUIDS = {
    # Symbol instances
    "2da7b3a7-1a8e-453e-881f-7d15e71429bc",  # U2 MAX31865xAP
    "751101c5-e1bb-4ea9-93f3-dd5c2c20c183",  # J1 Conn_01x06
    "06e4dd75-2b70-4a5a-a7b0-ff358c733574",  # +3V3 at (152.4, 63.5)
    "06f7d7a1-81b8-442d-96f3-daccb1eaf590",  # GND at (152.4, 111.76)
    "8ba60050-ecbf-4d29-8ce5-c2120da22408",  # +3V3 at (147.32, 63.5)
    "98c3a802-ab95-4d3f-bd69-15e3973bbed3",  # GND at (147.32, 111.76)
    # SPI labels on U2 left side
    "53d43e4f-cc0d-465f-bfb9-564d3cd9fd45",  # SPI_MOSI (124.46, 76.2)
    "61941e15-187f-4d35-af78-44c68c55c8da",  # SPI_SCK  (124.46, 78.74)
    "e13f319c-6931-4e34-919e-6f92bf84646d",  # SPI_CS   (124.46, 81.28)
    "f7450236-a6b3-4776-971b-d0bdb4824ee4",  # SPI_MISO (124.46, 83.82)
    # Analog labels on U2 right side
    "06f3fc3b-c007-4dab-af4d-f0fbee37a155",  # FORCE-   (175.26, 101.6)
    "161085e1-4475-4697-b04e-9cbb6ef4c71e",  # FORCE+   (175.26, 88.9)
    "7fad809c-3f1f-475d-b1aa-ff9440c47c5e",  # REFIN-   (175.26, 81.28)
    "a88d9e11-7f5d-4c83-b270-ccf4933c9595",  # REFIN+   (175.26, 76.2)
    "c80500a1-81ad-4939-8190-217e11909cbd",  # RTDIN+   (175.26, 93.98)
    "cc219411-1b96-49ba-a748-ddb378b45457",  # RTDIN-   (175.26, 99.06)
    # Analog labels on J1 left side
    "30014368-d7f1-413d-a50f-761430f4a9f4",  # FORCE-   (215.9, 90.17)
    "6d306804-697a-4f01-af23-bf7b02153329",  # RTDIN+   (215.9, 85.09)
    "7d934810-dbb1-4efd-8d4a-4e08f31835e2",  # REFIN+   (215.9, 80.01)
    "8a65a225-b092-4537-ae0a-60c08a1a3444",  # FORCE+   (215.9, 82.55)
    "95f02c83-52c8-4f68-9645-159de6c85487",  # RTDIN-   (215.9, 87.63)
    "9f446976-103e-42ea-aa15-e46ac0f0606f",  # REFIN-   (215.9, 92.71)
    # Wires — SPI left side of U2
    "d1b839c6-6a15-4b84-b9d9-572fc8cd4cc0",  # SPI_MOSI wire
    "019e015b-8a52-4c21-a958-589ffb6d620e",  # SPI_SCK  wire
    "62531def-d7dd-486e-8023-43c38b98b5a4",  # SPI_CS   wire
    "851c38eb-411b-46a3-96c9-b975d8ffde3c",  # SPI_MISO wire
    # Wires — analog right side of U2
    "899aa009-c48d-4820-ad37-ca2d33711a3c",  # REFIN+   wire
    "f5e59760-c2cf-424c-8dd9-c40222f428bf",  # REFIN-   wire
    "d8fc72be-af30-49ee-b869-3c7529d1527b",  # FORCE+   wire
    "fdf239c5-3644-467e-ade5-7ae248d7e657",  # RTDIN+   wire
    "e5c83c8e-dc49-402e-b249-efd7472292ed",  # RTDIN-   wire
    "69fc53d6-c201-4de6-b76f-565983d0f354",  # FORCE-   wire
    # Wires — power supply to U2
    "063fe94c-e58b-467a-b729-4acdd005eb3e",  # +3V3 DVDD wire
    "a692dbb5-79b1-473e-b411-25858e3390db",  # +3V3 VDD  wire
    "068786b2-aee4-4a33-831a-24026ee20c46",  # GND AGND  wire
    "b1f09643-82c4-4329-841a-4993a57102d3",  # GND DGND  wire
    # Wires — old J1 left side
    "3af109e1-5b21-4efb-ac47-a2320482b11e",  # FORCE+  wire
    "51d06e50-c85e-40bf-b8a6-b36f6bd445a9",  # RTDIN-  wire
    "8a82ec28-92c4-4be4-8ead-f50a493939db",  # RTDIN+  wire
    "8f6df7a1-1e76-4160-acbb-f6b4d77883b8",  # REFIN-  wire
    "e84c9f09-4f78-4edd-ab70-d78eebc19b42",  # REFIN+  wire
    "ec744f26-b346-4d1c-8996-d788a013e180",  # FORCE-  wire
    # No-connect markers on U2 pins
    "7f780edc-4ebc-4bef-b220-0926bc83ac65",  # ~{DRDY} (134.62, 73.66)
    "5433a945-2878-451f-8b5b-ce1b40bcbd1c",  # BIAS    (165.1, 73.66)
    "cf74f2ac-c8d1-4130-b30d-0b2a24895eb7",  # FORCE2  (165.1, 83.82)
    "d493eed5-e284-438e-978e-837d05058bb6",  # NC      (165.1, 91.44)
}

def remove_blocks_by_uuid(content: str, uuids: set) -> tuple[str, list]:
    """
    Remove top-level S-expression blocks that contain a given UUID.
    A top-level block starts with a tab + '(' on a line by itself,
    and ends at the matching closing ')' + newline.
    Returns (new_content, list_of_removed_uuids).
    """
    removed = []
    for uuid in uuids:
        pattern = rf'\t\([^\n]*\n(?:[^\t].*\n|\t\t[^\n]*\n)*\t\t\(uuid "{re.escape(uuid)}"\)\n\t\)\n'
        match = re.search(pattern, content)
        if match:
            content = content[:match.start()] + content[match.end():]
            removed.append(uuid)
        else:
            # Try a simpler approach: find the uuid, then walk back to the
            # enclosing top-level block and remove it.
            uuid_pos = content.find(f'(uuid "{uuid}")')
            if uuid_pos == -1:
                print(f"WARNING: UUID not found: {uuid}", file=sys.stderr)
                continue
            # Walk backwards to find the start of the enclosing top-level block
            # Top-level blocks start with '\t(' at the beginning of a line
            block_start = content.rfind('\n\t(', 0, uuid_pos)
            if block_start == -1:
                print(f"WARNING: block start not found for: {uuid}", file=sys.stderr)
                continue
            block_start += 1  # skip the \n, keep the \t
            # Walk forward to find matching closing paren
            depth = 0
            i = block_start
            while i < len(content):
                if content[i] == '(':
                    depth += 1
                elif content[i] == ')':
                    depth -= 1
                    if depth == 0:
                        block_end = i + 1
                        # consume trailing newline
                        if block_end < len(content) and content[block_end] == '\n':
                            block_end += 1
                        break
                i += 1
            else:
                print(f"WARNING: block end not found for: {uuid}", file=sys.stderr)
                continue
            content = content[:block_start] + content[block_end:]
            removed.append(uuid)
    return content, removed


def remove_lib_symbol(content: str, lib_name: str) -> str:
    """Remove an embedded lib_symbol entry by name from the lib_symbols section."""
    # Find the symbol definition inside lib_symbols
    # Pattern: \t\t(symbol "lib_name" ... ) where the block is indented with \t\t
    pattern = rf'\t\t\(symbol "{re.escape(lib_name)}"'
    start = content.find(pattern)
    if start == -1:
        print(f"WARNING: lib_symbol not found: {lib_name}", file=sys.stderr)
        return content
    # Walk forward to find the matching closing paren at \t\t depth
    depth = 0
    i = start
    while i < len(content):
        if content[i] == '(':
            depth += 1
        elif content[i] == ')':
            depth -= 1
            if depth == 0:
                end = i + 1
                if end < len(content) and content[end] == '\n':
                    end += 1
                break
        i += 1
    else:
        print(f"WARNING: lib_symbol end not found: {lib_name}", file=sys.stderr)
        return content
    return content[:start] + content[end:]


with open(SCH) as f:
    content = f.read()

original_len = len(content)

# 1. Remove all blocks by UUID
content, removed = remove_blocks_by_uuid(content, REMOVE_UUIDS)
print(f"Removed {len(removed)}/{len(REMOVE_UUIDS)} UUID blocks")
missing = REMOVE_UUIDS - set(removed)
if missing:
    print("MISSING UUIDs (not found in file):")
    for u in sorted(missing):
        print(f"  {u}")

# 2. Remove lib_symbol definitions no longer used
content = remove_lib_symbol(content, "Sensor_Temperature:MAX31865xAP")
content = remove_lib_symbol(content, "Connector_Generic:Conn_01x06")
print("Removed lib_symbols: MAX31865xAP, Conn_01x06")

print(f"Content size: {original_len} → {len(content)} bytes (removed {original_len - len(content)} bytes)")

with open(SCH, 'w') as f:
    f.write(content)
print("Schematic written.")
```

- [ ] **Step 2: Run the removal script**

```bash
cd /home/mmessano/Documents/PlatformIO/Projects/SaunaStatus
python3 docs/kicad/remove_u2_j1.py
```

Expected output:
```
Removed 46/46 UUID blocks
Removed lib_symbols: MAX31865xAP, Conn_01x06
Content size: XXXXX → YYYYY bytes (removed ZZZZZ bytes)
Schematic written.
```
If any UUID shows as MISSING, stop and investigate before continuing.

- [ ] **Step 3: Verify the schematic still parses as valid text (not corrupted)**

```bash
python3 -c "
content = open('docs/kicad/SaunaStatus.kicad_sch').read()
# Check structural integrity: balanced parens
depth = 0
for ch in content:
    if ch == '(': depth += 1
    elif ch == ')': depth -= 1
    if depth < 0:
        print('ERROR: unbalanced parens')
        exit(1)
print(f'Paren balance: {depth} (must be 0)')
assert depth == 0
# Confirm removed elements are gone
for name in ['MAX31865xAP', 'Conn_01x06', 'REFIN+', 'REFIN-', 'FORCE+', 'FORCE-', 'RTDIN+', 'RTDIN-']:
    count = content.count(name)
    print(f'{name}: {count} occurrences remaining')
"
```

Expected: paren balance = 0; all analog net names have 0 occurrences.

---

## Task 3: Add Conn_01x08 lib_symbol to Embedded Symbols

**Files:**
- Modify: `docs/kicad/SaunaStatus.kicad_sch`
- Create: `docs/kicad/add_conn01x08_lib.py` (temporary)

The schematic embeds full symbol definitions in its `(lib_symbols ...)` section. We must add `Connector_Generic:Conn_01x08` there.

- [ ] **Step 1: Verify the Conn_01x08 symbol exists in the KiCad system library**

```bash
python3 -c "
import re
with open('/usr/share/kicad/symbols/Connector_Generic.kicad_sym') as f:
    content = f.read()
# Find Conn_01x08 block
start = content.find('\t(symbol \"Conn_01x08\"')
if start == -1:
    print('NOT FOUND — check KiCad installation')
    exit(1)
# Walk to end of block
depth = 0
i = start
while i < len(content):
    if content[i] == '(': depth += 1
    elif content[i] == ')':
        depth -= 1
        if depth == 0:
            end = i + 1
            break
    i += 1
block = content[start:end]
print(f'Found Conn_01x08 block: {len(block)} bytes')
print('First 3 lines:')
for line in block.splitlines()[:3]: print(' ', line)
"
```

- [ ] **Step 2: Write the lib_symbol insertion script**

```python
#!/usr/bin/env python3
"""Add Connector_Generic:Conn_01x08 lib_symbol to SaunaStatus.kicad_sch."""

SCH = "docs/kicad/SaunaStatus.kicad_sch"
KICAD_LIB = "/usr/share/kicad/symbols/Connector_Generic.kicad_sym"

# 1. Extract Conn_01x08 from system library
with open(KICAD_LIB) as f:
    lib_content = f.read()

start = lib_content.find('\t(symbol "Conn_01x08"')
if start == -1:
    raise RuntimeError("Conn_01x08 not found in system library")
depth = 0
i = start
while i < len(lib_content):
    if lib_content[i] == '(':
        depth += 1
    elif lib_content[i] == ')':
        depth -= 1
        if depth == 0:
            end = i + 1
            if end < len(lib_content) and lib_content[end] == '\n':
                end += 1
            break
    i += 1
conn_block = lib_content[start:end]

# 2. Rename the symbol to use the full qualified name for embedding
# In lib_symbols, the symbol is referenced as "Connector_Generic:Conn_01x08"
conn_block_embedded = conn_block.replace(
    '\t(symbol "Conn_01x08"',
    '\t\t(symbol "Connector_Generic:Conn_01x08"',
    1
)
# Also adjust inner sub-symbol references
conn_block_embedded = conn_block_embedded.replace(
    '\t(symbol "Conn_01x08_',
    '\t\t(symbol "Connector_Generic:Conn_01x08_',
)
# Fix indentation: the system lib uses \t, embedded symbols use \t\t
# Already done the first-level indentation above.
# Now add one extra tab to all lines (they start with \t, need \t\t)
lines = conn_block_embedded.splitlines()
indented_lines = []
for line in lines:
    if line.startswith('\t\t(symbol'):
        indented_lines.append(line)  # already fixed above
    elif line.startswith('\t'):
        indented_lines.append('\t' + line)  # add one level
    else:
        indented_lines.append(line)
conn_block_embedded = '\n'.join(indented_lines) + '\n'

# 3. Insert into schematic lib_symbols section (just before the closing paren)
with open(SCH) as f:
    sch = f.read()

# Find end of lib_symbols block
lib_sym_start = sch.find('\t(lib_symbols')
if lib_sym_start == -1:
    raise RuntimeError("lib_symbols section not found")
# Find its closing paren
depth = 0
i = lib_sym_start
while i < len(sch):
    if sch[i] == '(':
        depth += 1
    elif sch[i] == ')':
        depth -= 1
        if depth == 0:
            lib_sym_end = i  # position of closing )
            break
    i += 1

# Insert conn_block_embedded just before the closing )
sch = sch[:lib_sym_end] + conn_block_embedded + sch[lib_sym_end:]

with open(SCH, 'w') as f:
    f.write(sch)
print("Conn_01x08 lib_symbol added to schematic.")
print(f"Schematic size: {len(sch)} bytes")
```

- [ ] **Step 3: Run the lib_symbol insertion script**

```bash
python3 docs/kicad/add_conn01x08_lib.py
```

Expected: `Conn_01x08 lib_symbol added to schematic.`

- [ ] **Step 4: Verify Conn_01x08 is now embedded**

```bash
python3 -c "
content = open('docs/kicad/SaunaStatus.kicad_sch').read()
assert 'Connector_Generic:Conn_01x08' in content
print('Conn_01x08 found in schematic')
# Also verify parens still balanced
depth = sum(1 if c=='(' else -1 if c==')' else 0 for c in content)
print(f'Paren balance: {depth} (must be 0)')
assert depth == 0
"
```

---

## Task 4: Add New J1 Connector Symbol with All Connections

**Files:**
- Modify: `docs/kicad/SaunaStatus.kicad_sch`
- Create: `docs/kicad/add_new_j1.py` (temporary)

This script adds the new J1 `Conn_01x08` instance at (231.14, 85.09) plus all 8 wires, 6 net labels, and 2 no_connect markers.

- [ ] **Step 1: Write the J1 addition script**

```python
#!/usr/bin/env python3
"""Add new J1 (Conn_01x08 Adafruit breakout connector) to SaunaStatus.kicad_sch."""
import uuid

SCH = "docs/kicad/SaunaStatus.kicad_sch"
SCH_UUID = "d19a36c6-5583-4c95-873e-3cdaa80a72d5"  # schematic root UUID

def u():
    return str(uuid.uuid4())

# New J1 symbol instance
# Position: (231.14, 85.09), Conn_01x08
# Pin 1 lib_y=7.62  → abs_y = 85.09 - 7.62 = 77.47
# Pin k: abs_y = 85.09 - (7.62 - (k-1)*2.54)
pin_y = [round(85.09 - (7.62 - (k-1)*2.54), 2) for k in range(1, 9)]
pin_x = 226.06  # 231.14 - 5.08

NEW_J1 = f"""\t(symbol
\t\t(lib_id "Connector_Generic:Conn_01x08")
\t\t(at 231.14 85.09 0)
\t\t(unit 1)
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(dnp no)
\t\t(uuid "{u()}")
\t\t(property "Reference" "J1"
\t\t\t(at 227.14 74.47 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(property "Value" "Adafruit-MAX31865-Breakout"
\t\t\t(at 242.14 74.47 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(property "Footprint" "Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Vertical"
\t\t\t(at 231.14 85.09 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(hide yes)
\t\t\t)
\t\t)
\t\t(property "Datasheet" ""
\t\t\t(at 231.14 85.09 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(hide yes)
\t\t\t)
\t\t)
\t\t(property "Description" "Adafruit MAX31865 RTD breakout board 8-pin header"
\t\t\t(at 231.14 85.09 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(pin "1" (uuid "{u()}"))
\t\t(pin "2" (uuid "{u()}"))
\t\t(pin "3" (uuid "{u()}"))
\t\t(pin "4" (uuid "{u()}"))
\t\t(pin "5" (uuid "{u()}"))
\t\t(pin "6" (uuid "{u()}"))
\t\t(pin "7" (uuid "{u()}"))
\t\t(pin "8" (uuid "{u()}"))
\t\t(instances
\t\t\t(project "SaunaStatus"
\t\t\t\t(path "/{SCH_UUID}"
\t\t\t\t\t(reference "J1")
\t\t\t\t\t(unit 1)
\t\t\t\t)
\t\t\t)
\t\t)
\t)
"""

# Net connections: 6 wires + labels, 2 no_connect markers
# Labels at x=215.9, wires from 226.06 to 215.9
LABEL_X = 215.9
PIN_X = 226.06

def wire(y):
    return f"""\t(wire
\t\t(pts
\t\t\t(xy {PIN_X} {y}) (xy {LABEL_X} {y})
\t\t)
\t\t(stroke
\t\t\t(width 0)
\t\t\t(type default)
\t\t)
\t\t(uuid "{u()}")
\t)
"""

def label(name, y):
    return f"""\t(label "{name}"
\t\t(at {LABEL_X} {y} 180)
\t\t(effects
\t\t\t(font
\t\t\t\t(size 1.27 1.27)
\t\t\t)
\t\t\t(justify right)
\t\t)
\t\t(uuid "{u()}")
\t)
"""

def no_connect(x, y):
    return f"""\t(no_connect
\t\t(at {x} {y})
\t\t(uuid "{u()}")
\t)
"""

# pin_y indices: pin_y[0]=77.47, pin_y[1]=80.01, ..., pin_y[7]=95.25
connections = (
    wire(pin_y[0]) + label("+3V3", pin_y[0]) +   # Pin 1 VIN
    no_connect(PIN_X, pin_y[1]) +                  # Pin 2 3Vo
    wire(pin_y[2]) + label("GND", pin_y[2]) +      # Pin 3 GND
    wire(pin_y[3]) + label("SPI_SCK", pin_y[3]) +  # Pin 4 CLK
    wire(pin_y[4]) + label("SPI_MISO", pin_y[4]) + # Pin 5 SDO
    wire(pin_y[5]) + label("SPI_MOSI", pin_y[5]) + # Pin 6 SDI
    wire(pin_y[6]) + label("SPI_CS", pin_y[6]) +   # Pin 7 CS
    no_connect(PIN_X, pin_y[7])                     # Pin 8 RDY
)

new_elements = NEW_J1 + connections

# Insert before the sheet_instances section (near end of file)
with open(SCH) as f:
    sch = f.read()

INSERT_BEFORE = "\t(sheet_instances"
pos = sch.rfind(INSERT_BEFORE)
if pos == -1:
    raise RuntimeError("sheet_instances not found — cannot determine insert position")

sch = sch[:pos] + new_elements + sch[pos:]

with open(SCH, 'w') as f:
    f.write(sch)

print("New J1 (Conn_01x08) added to schematic.")
print(f"Pin Y coordinates: {pin_y}")
print(f"Schematic size: {len(sch)} bytes")
```

- [ ] **Step 2: Run the J1 addition script**

```bash
python3 docs/kicad/add_new_j1.py
```

Expected:
```
New J1 (Conn_01x08) added to schematic.
Pin Y coordinates: [77.47, 80.01, 82.55, 85.09, 87.63, 90.17, 92.71, 95.25]
Schematic size: XXXXX bytes
```

- [ ] **Step 3: Verify J1 and connections are present**

```bash
python3 -c "
content = open('docs/kicad/SaunaStatus.kicad_sch').read()
checks = [
    ('Connector_Generic:Conn_01x08', 'J1 lib_id'),
    ('Adafruit-MAX31865-Breakout', 'J1 value'),
    ('PinHeader_1x08_P2.54mm', 'J1 footprint'),
    ('SPI_SCK', 'SPI_SCK label'),
    ('SPI_MISO', 'SPI_MISO label'),
    ('SPI_MOSI', 'SPI_MOSI label'),
    ('SPI_CS', 'SPI_CS label'),
    ('226.06 95.25', 'pin 8 RDY no_connect coord'),
    ('226.06 80.01', 'pin 2 3Vo no_connect coord'),
]
for text, desc in checks:
    assert text in content, f'MISSING: {desc} ({text!r})'
    print(f'OK: {desc}')
depth = sum(1 if c=='(' else -1 if c==')' else 0 for c in content)
print(f'Paren balance: {depth} (must be 0)')
assert depth == 0
"
```

---

## Task 5: Run ERC and Fix Any Violations

**Files:**
- Read: `docs/kicad/SaunaStatus.kicad_sch` (via MCP tool)

- [ ] **Step 1: Run ERC via MCP server**

Use the `mcp__kicad__run_erc` tool with `project_path = "docs/kicad/SaunaStatus.kicad_pro"`.

- [ ] **Step 2: Inspect results**

Expected: 0 errors. If violations are reported, follow the `kicad-erc-drc-workflow` skill to diagnose and fix them.

Common issues to watch for:
- `pin_not_connected`: a connector pin has no wire/label/no_connect → add the missing element
- `label_dangling`: a net label's anchor does not land exactly on a pin endpoint → verify pin_y coordinates match the Conn_01x08 pin layout
- `endpoint_off_grid`: any coordinate not a multiple of 2.54 → re-check the arithmetic in Task 4 step 1

- [ ] **Step 3: If ERC passes, open schematic in KiCad GUI to confirm visual layout looks correct**

Open `docs/kicad/SaunaStatus.kicad_pro` in KiCad and visually confirm:
- New J1 (8-pin) appears where old J1 was
- U2 (bare IC) is gone
- Pin labels VIN/3Vo/GND/CLK/SDO/SDI/CS/RDY visible

---

## Task 6: Update PCB

**Files:**
- Read/Modify: `docs/kicad/SaunaStatus.kicad_pcb`

This step must be done in the KiCad GUI. PCB layout cannot be reliably edited as text for footprint placement.

- [ ] **Step 1: Open PCB in KiCad**

Open `docs/kicad/SaunaStatus.kicad_pro`, then switch to the PCB editor.

- [ ] **Step 2: Run "Update PCB from Schematic"**

Menu: `Tools → Update PCB from Schematic` (or Ctrl+F8).

Expected changes:
- U2 (`SSOP-20_5.3x7.2mm`) is marked for removal (or already absent if it was never placed)
- J1 footprint changes from `PinHeader_1x06` to `PinHeader_1x08`

Accept all changes.

- [ ] **Step 3: Place/route new J1 footprint**

The 8-pin J1 footprint is 2 pins taller than the old 6-pin. Adjust its position on the board to fit within the board outline.

- [ ] **Step 4: Delete U2 footprint if present**

If U2 still appears as an unmatched footprint, delete it manually.

- [ ] **Step 5: Save the PCB file**

File → Save (Ctrl+S).

---

## Task 7: Run DRC and Fix Any Violations

**Files:**
- Read: `docs/kicad/SaunaStatus.kicad_pcb`

- [ ] **Step 1: Run DRC via MCP server**

Use `mcp__kicad__run_drc` with `project_path = "docs/kicad/SaunaStatus.kicad_pro"`.

- [ ] **Step 2: Inspect and fix**

Expected: 0 errors (or only pre-existing informational items that existed before this change).

If J1's new 8-pin footprint extends outside the board outline, move it within bounds.

---

## Task 8: Run Unit Tests

**Files:**
- Read: `test/` (all test suites)

- [ ] **Step 1: Run all native tests**

```bash
pio test -e native
```

- [ ] **Step 2: Confirm all pass**

Expected: all suites pass (sensor, config, websocket, auth, OTA). Zero failures.

If failures occur: this is a pure schematic change with no firmware changes — failures indicate a pre-existing issue, not caused by this task.

---

## Task 9: Clean Up Temporary Scripts and Commit

**Files:**
- Delete: `docs/kicad/remove_u2_j1.py`
- Delete: `docs/kicad/add_conn01x08_lib.py`
- Delete: `docs/kicad/add_new_j1.py`

- [ ] **Step 1: Remove temporary scripts**

```bash
rm docs/kicad/remove_u2_j1.py docs/kicad/add_conn01x08_lib.py docs/kicad/add_new_j1.py
rm -f docs/kicad/SaunaStatus.kicad_sch.bak docs/kicad/SaunaStatus.kicad_pcb.bak
```

- [ ] **Step 2: Stage changed files**

```bash
git add docs/kicad/SaunaStatus.kicad_sch docs/kicad/SaunaStatus.kicad_pcb
git add docs/superpowers/plans/2026-03-20-max31865-breakout-connector.md
```

- [ ] **Step 3: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(kicad): replace MAX31865 bare IC with Adafruit breakout connector

Remove U2 (MAX31865xAP SSOP-20) and J1 (6-pin PT1000 connector);
replace with J1 (Conn_01x08) for Adafruit MAX31865 breakout board.
SPI GPIO assignments unchanged (CS=5, SCK=18, MISO=19, MOSI=23).

Hardware prerequisite: set Adafruit breakout 3-wire solder jumper
before installation (ships in 2-wire/4-wire mode by default).

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 4: Verify clean working tree**

```bash
git status
```

Expected: `nothing to commit, working tree clean`.
