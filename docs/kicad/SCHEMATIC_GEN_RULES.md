# Rules for Programmatic KiCad Schematic Generation

Lessons extracted from iterative ERC debugging of `generate_schematic.py`.
Apply these rules when generating `.kicad_sch` files from Python (or any code).

---

## 1. Grid Alignment

**All coordinates must be integer multiples of 2.54 mm.**

- Use constants: `GRID = 2.54`
- Snap every position: `round(x / 2.54) * 2.54`
- Common multiples: 5.08 (2×), 7.62 (3×), 10.16 (4×), 12.70 (5×), 15.24 (6×)
- Violation produces `endpoint_off_grid` ERC warnings for every affected pin

---

## 2. Coordinate Frame Convention

KiCad uses **Y-down** in the schematic (y increases downward on screen).
Library symbols use **Y-up** (y increases upward in the symbol editor).

```
abs_x = symbol_origin_x + lib_pin_x
abs_y = symbol_origin_y - lib_pin_y   ← sign flip!
```

Getting this wrong produces off-grid endpoints, unconnected wires, and dangling labels.
Verify every pin group by cross-checking one known pin against a library `(pin ... (at x y))` entry.

---

## 3. KiCad 9 Annotation — Instances Block

KiCad 9 will **re-annotate** (assign `U?`, `J?`, `#PWR?`) any symbol that lacks an
`instances` block, then flag all resulting duplicates as `duplicate_reference` errors.

Every symbol — components, power symbols, PWR_FLAGs — needs:

```scheme
(instances
  (project "ProjectName"
    (path "/SCHEMATIC_UUID"
      (reference "U1")
      (unit 1)
    )
  )
)
```

Rules:
- Generate one `SCHEMATIC_UUID = str(uuid.uuid4())` per script run.
- Embed the same UUID in `(uuid "...")` in the schematic header.
- Use a monotonically incrementing counter for `#PWR001`, `#FLG001`, etc.
- The `(property "Reference" "U1" ...)` field alone is **not sufficient** for KiCad 9.

---

## 4. PWR_FLAG Placement

`power:PWR_FLAG` silences `power_pin_not_driven` ERC errors.
Its single pin is at `(0, 0)` in library coordinates (no offset).

**Place PWR_FLAG at the exact same `(x, y)` as the power symbol it certifies.**

```python
elements.append(power_sym("+3V3", x, y))
elements.append(pwr_flag(x, y))   # same x, y — pin coincides
```

- One PWR_FLAG per distinct power net (`+3V3`, `GND`, `+5V`, …)
- Offset by even 2.54 mm causes `pin_not_connected` on the flag

---

## 5. Power Symbol Collision Check

Two power symbols with **different net names** at the **same coordinate** create a
short circuit (`multiple_net_names` warning + cascading `pin_to_pin` error).

When placing power symbols at offsets from component origins, check for collision:

```python
# Example: two stacked DHT sensors with ±offset power symbols
# D1 GND at D1_Y + offset  must NOT equal  D2 VDD at D2_Y - offset
# Collision when:  D2_Y - D1_Y == 2 * offset
# Fix: increase spacing or decrease offset
assert (D2_Y - D1_Y) > 2 * power_offset
```

Use the smallest offset that still clears the component body (typically 10.16 mm = 4×2.54).

---

## 6. Net Labels — Connection Rules

A net label connects to a wire only when its anchor point (the `(at x y)` position)
**exactly coincides** with a wire endpoint or component pin endpoint.

- Labels at `angle=0` extend text rightward; anchor is at `(x, y)` (left end).
- Labels at `angle=180` extend text leftward; anchor is still at `(x, y)` (right end).
- **Never place two labels at the same coordinate** — KiCad may report both as
  dangling even when wires touch that point.

### Preferred pattern for connecting two components via a named net:

```
Component A pin ──[stub wire]──[label]   ← one label, one wire
Component B pin ──[bridge wire]──────┘   ← wire only, no second label
```

The bridge wire's endpoint meets the stub wire's endpoint (where the label sits).
This avoids duplicate labels and is unambiguous to KiCad's ERC.

---

## 7. Connector Pin Coordinates (Conn_01xN)

KiCad `Connector_Generic:Conn_01xN` pin layout (all on the left side):

| Symbol | lib_x | lib_y (pin 1 → pin N) |
|--------|-------|----------------------|
| Conn_01x04 | −5.08 | 2.54, 0, −2.54, −5.08 |
| Conn_01x06 | −5.08 | 5.08, 2.54, 0, −2.54, −5.08, −7.62 |

Pin endpoint (wire connection point):
```
abs_x = symbol_x + lib_x   (= symbol_x − 5.08)
abs_y = symbol_y − lib_y
```

---

## 8. Footprints Are Required for PCB Export

Every component symbol must have a non-empty `Footprint` property or
"Update PCB from Schematic" will fail with `Cannot add X (no footprint assigned)`.

Set footprints at symbol generation time:

```python
(property "Footprint" "Library:FootprintName" ...)
```

Common footprints used in this project:

| Component | Footprint |
|-----------|-----------|
| ESP32-WROOM-32 | `RF_Module:ESP32-WROOM-32` |
| MAX31865xAP (SSOP-20) | `Package_SO:SSOP-20_5.3x7.2mm_P0.65mm` |
| DHT21/AM2301 | `Sensor:Aosong_DHT11_5.5x12.0_P2.54mm` |
| ULN2003A (DIP-16) | `Package_DIP:DIP-16_W7.62mm` |
| 1×4 pin header | `Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical` |
| 1×6 pin header | `Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical` |

Verify footprint library paths with:
```bash
find /usr/share/kicad/footprints -name "*PartName*"
```

---

## 9. Schematic Header

Minimum valid KiCad 9 schematic header:

```scheme
(kicad_sch (version 20230121) (generator "your_script_name")

  (uuid "SCHEMATIC_UUID_HERE")

  (paper "A2")

  (lib_symbols
    ... embedded symbol definitions ...
  )

  ... wires, labels, symbols ...

  (sheet_instances
    (path "/" (page "1"))
  )
)
```

- `version 20230121` — KiCad 7/9 compatible
- `uuid` — must match the UUID used in all `instances` blocks
- `lib_symbols` — embed full symbol S-expressions extracted from `.kicad_sym` files;
  prefix the symbol name with `LibraryName:` (e.g., `power:GND`)

---

## 10. ERC Iteration Workflow

1. Run script → open `.kicad_pro` in KiCad → Inspect → Electrical Rules Checker → Run
2. Save report as `ERC.rpt`
3. For each error category, trace to a coordinate or naming issue in the generator
4. Fix in the Python script; regenerate; reload schematic in KiCad (File → Revert)
5. Repeat until `ERC messages: 0  Errors 0  Warnings 0`

### Common ERC → Root Cause mapping

| ERC error | Typical cause |
|-----------|---------------|
| `endpoint_off_grid` | Coordinate not a multiple of 2.54 mm |
| `pin_not_connected` | Wrong sign on `abs_y = oy − lib_y` (Y-axis flip missed) |
| `label_dangling` | Label anchor not touching a wire endpoint; or duplicate labels at same position |
| `power_pin_not_driven` | Missing PWR_FLAG on a power net |
| `power_pin_not_driven` | PWR_FLAG placed at wrong position (not coincident with power sym) |
| `pin_to_pin` (power out + power out) | Two PWR_FLAGs on the same net — caused by a `multiple_net_names` short |
| `multiple_net_names` | Two power symbols with different net names at the same coordinate |
| `duplicate_reference` | Missing `instances` block (KiCad 9 re-annotated all symbols) |
| `no_connect_dangling` | No-connect placed at wrong (x, y) due to Y-axis sign error |
