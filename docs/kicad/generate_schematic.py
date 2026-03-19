#!/usr/bin/env python3
"""
Generate a KiCad 7/9 schematic for SaunaStatus with real component symbols.

Run from docs/kicad/:
    python3 generate_schematic.py

Produces SaunaStatus.kicad_sch.  Open SaunaStatus.kicad_pro in KiCad.
"""

import uuid, os, re

OUT = os.path.join(os.path.dirname(__file__), "SaunaStatus.kicad_sch")
KICAD_SYM = "/usr/share/kicad/symbols"

def uid(): return str(uuid.uuid4())

# One UUID for the whole schematic file — used in symbol instances blocks so
# KiCad 9 treats all symbols as already-annotated instead of auto-annotating.
SCHEMATIC_UUID = uid()

PROJECT_NAME = "SaunaStatus"

# ---------------------------------------------------------------------------
# Extract a full (symbol "NAME" ...) block from a .kicad_sym library file
# ---------------------------------------------------------------------------
def extract_symbol(lib_file, name):
    with open(lib_file) as f:
        text = f.read()
    pattern = f'(symbol "{name}"'
    start = text.find(pattern)
    if start == -1:
        raise ValueError(f"Symbol {name!r} not found in {lib_file}")
    depth = 0
    for i, ch in enumerate(text[start:], start):
        if ch == '(':  depth += 1
        elif ch == ')':
            depth -= 1
            if depth == 0:
                return text[start:i+1]
    raise ValueError(f"Unbalanced parens for {name!r}")

# ---------------------------------------------------------------------------
# Schematic primitives
# ---------------------------------------------------------------------------

def _instances_block(ref, unit=1):
    """KiCad 9 requires this to treat a symbol as annotated."""
    return (f'    (instances\n'
            f'      (project "{PROJECT_NAME}"\n'
            f'        (path "/{SCHEMATIC_UUID}"\n'
            f'          (reference "{ref}")\n'
            f'          (unit {unit})\n'
            f'        )\n'
            f'      )\n'
            f'    )')

def sym_instance(lib_id, ref, value, x, y, angle, pins, ref_dx=0, ref_dy=-4, footprint=""):
    """Place a component symbol instance."""
    pin_lines = "\n".join(f'    (pin "{p}" (uuid "{uid()}"))' for p in pins)
    rx, ry = x + ref_dx, y + ref_dy
    return f"""\
  (symbol (lib_id "{lib_id}") (at {x:.3f} {y:.3f} {angle}) (unit 1)
    (in_bom yes) (on_board yes) (dnp no)
    (uuid "{uid()}")
    (property "Reference" "{ref}" (at {rx:.3f} {ry:.3f} 0)
      (effects (font (size 1.27 1.27)))
    )
    (property "Value" "{value}" (at {x + ref_dx + 15:.3f} {ry:.3f} 0)
      (effects (font (size 1.27 1.27)))
    )
    (property "Footprint" "{footprint}" (at {x:.3f} {y:.3f} 0)
      (effects (font (size 1.27 1.27)) hide)
    )
    (property "Datasheet" "" (at {x:.3f} {y:.3f} 0)
      (effects (font (size 1.27 1.27)) hide)
    )
{pin_lines}
{_instances_block(ref)}
  )"""

def wire(x1, y1, x2, y2):
    return (f'  (wire (pts (xy {x1:.3f} {y1:.3f}) (xy {x2:.3f} {y2:.3f}))\n'
            f'    (stroke (width 0) (type default))\n'
            f'    (uuid "{uid()}")\n  )')

def net_label(name, x, y, angle=0):
    justify = "right mirror" if angle == 180 else "left"
    return (f'  (label "{name}" (at {x:.3f} {y:.3f} {angle})\n'
            f'    (fields_autoplaced)\n'
            f'    (effects (font (size 1.27 1.27)) (justify {justify}))\n'
            f'    (uuid "{uid()}")\n  )')

_pwr_counter = [1]

def power_sym(name, x, y, angle=0):
    """Place a KiCad power symbol (power:GND / power:+3V3 / power:+5V)."""
    ref = f"#PWR{_pwr_counter[0]:03d}"
    _pwr_counter[0] += 1
    return (f'  (symbol (lib_id "power:{name}") (at {x:.3f} {y:.3f} {angle}) (unit 1)\n'
            f'    (in_bom yes) (on_board yes) (dnp no)\n'
            f'    (uuid "{uid()}")\n'
            f'    (property "Reference" "{ref}" (at {x:.3f} {y:.3f} 0)\n'
            f'      (effects (font (size 1.27 1.27)) hide)\n    )\n'
            f'    (property "Value" "{name}" (at {x:.3f} {y:.3f} 0)\n'
            f'      (effects (font (size 1.27 1.27)))\n    )\n'
            f'    (property "Footprint" "" (at {x:.3f} {y:.3f} 0)\n'
            f'      (effects (font (size 1.27 1.27)) hide)\n    )\n'
            f'    (property "Datasheet" "" (at {x:.3f} {y:.3f} 0)\n'
            f'      (effects (font (size 1.27 1.27)) hide)\n    )\n'
            f'    (pin "1" (uuid "{uid()}"))\n'
            f'{_instances_block(ref)}\n'
            f'  )')

_flg_counter = [1]

def pwr_flag(x, y):
    """PWR_FLAG — tells ERC the power net is intentionally driven.
    Place at the EXACT position of an existing power symbol on the net."""
    ref = f"#FLG{_flg_counter[0]:03d}"
    _flg_counter[0] += 1
    return (f'  (symbol (lib_id "power:PWR_FLAG") (at {x:.3f} {y:.3f} 0) (unit 1)\n'
            f'    (in_bom yes) (on_board yes) (dnp no)\n'
            f'    (uuid "{uid()}")\n'
            f'    (property "Reference" "{ref}" (at {x:.3f} {y:.3f} 0)\n'
            f'      (effects (font (size 1.27 1.27)) hide)\n    )\n'
            f'    (property "Value" "PWR_FLAG" (at {x:.3f} {y:.3f} 0)\n'
            f'      (effects (font (size 1.27 1.27)))\n    )\n'
            f'    (property "Footprint" "" (at {x:.3f} {y:.3f} 0)\n'
            f'      (effects (font (size 1.27 1.27)) hide)\n    )\n'
            f'    (property "Datasheet" "" (at {x:.3f} {y:.3f} 0)\n'
            f'      (effects (font (size 1.27 1.27)) hide)\n    )\n'
            f'    (pin "1" (uuid "{uid()}"))\n'
            f'{_instances_block(ref)}\n'
            f'  )')

def no_connect(x, y):
    return f'  (no_connect (at {x:.3f} {y:.3f}) (uuid "{uid()}"))'

def stub(pin_x, pin_y, length, label_name, right_side=True):
    """Wire stub + net label at a component pin."""
    if right_side:
        lx = pin_x + length
        return "\n".join([wire(pin_x, pin_y, lx, pin_y),
                          net_label(label_name, lx, pin_y, angle=0)])
    else:
        lx = pin_x - length
        return "\n".join([wire(pin_x, pin_y, lx, pin_y),
                          net_label(label_name, lx, pin_y, angle=180)])

# ---------------------------------------------------------------------------
# Component positions — all on 2.54 mm grid
# ---------------------------------------------------------------------------

E_X,  E_Y  = 127.0,  127.0    # ESP32  (50×2.54, 50×2.54)
M_X,  M_Y  = 241.3,   76.2    # MAX31865  (95×2.54, 30×2.54)
D1_X, D1_Y =  45.72,  76.2    # DHT21 ceiling  (18×2.54, 30×2.54)
D2_X, D2_Y =  45.72, 101.6    # DHT21 bench    (18×2.54, 40×2.54)
U1_X, U1_Y = 210.82, 160.02   # ULN2003 outflow (83×2.54, 63×2.54)
U2_X, U2_Y = 279.40, 160.02   # ULN2003 inflow  (110×2.54, 63×2.54)

STUB = 10.16   # 4 × 2.54 mm

# ---------------------------------------------------------------------------
# Pin endpoint helpers
# Library frame: y increases upward (opposite to schematic y-down).
# When symbol placed at (ox, oy): abs_x = ox + lib_x, abs_y = oy - lib_y
# ---------------------------------------------------------------------------

E_R = E_X + 15.24   # ESP32 right-side x endpoint
E_L = E_X - 15.24   # ESP32 left-side x endpoint
def esp_r(dy): return E_R, E_Y + dy    # dy = -lib_pin_y
def esp_l(dy): return E_L, E_Y + dy

# ---------------------------------------------------------------------------
elements = []

# ── ESP32-WROOM-32 ──────────────────────────────────────────────────────────
esp_pins = [str(p) for p in range(1, 40)]
elements.append(sym_instance(
    "RF_Module:ESP32-WROOM-32", "U1", "ESP32-WROOM-32",
    E_X, E_Y, 0, esp_pins, ref_dx=-12, ref_dy=-37,
    footprint="RF_Module:ESP32-WROOM-32"))

# Right-side GPIO stubs — dy = -lib_pin_y
right_pins = [
    (-17.78, "SPI_CS"),       # IO5
    (-10.16, "OUTFLOW_I4"),   # IO14
    ( -5.08, "DHT_CEILING"),  # IO16
    ( -2.54, "DHT_BENCH"),    # IO17
    (  0.00, "SPI_SCK"),      # IO18
    (  2.54, "SPI_MISO"),     # IO19
    (  5.08, "OUTFLOW_I1"),   # IO21
    (  7.62, "INFLOW_I1"),    # IO22
    ( 10.16, "SPI_MOSI"),     # IO23
    ( 12.70, "OUTFLOW_I2"),   # IO25
    ( 15.24, "OUTFLOW_I3"),   # IO26
    ( 17.78, "INFLOW_I2"),    # IO27
    ( 20.32, "INFLOW_I3"),    # IO32
    ( 22.86, "INFLOW_I4"),    # IO33
]
for dy, net in right_pins:
    px, py = esp_r(dy)
    elements.append(stub(px, py, STUB, net, right_side=True))

# Right-side unused GPIOs — no-connect (dy = -lib_pin_y)
right_nc_dy = [
    -7.62,   # IO15
   -12.70,   # IO13
   -15.24,   # IO12
   -20.32,   # IO4
   -22.86,   # RXD0/IO3
   -25.40,   # IO2
   -27.94,   # TXD0/IO1
   -30.48,   # IO0
    25.40,   # IO34
    27.94,   # IO35
]
for dy in right_nc_dy:
    px, py = esp_r(dy)
    elements.append(no_connect(px, py))

# Left-side no-connects (flash + unused analog)
# dy = -lib_pin_y:
#   SDO/SD0 lib_y=0   → dy=0
#   SDI/SD1 lib_y=-2.54 → dy=2.54
#   SHD/SD2 lib_y=-5.08 → dy=5.08
#   SWP/SD3 lib_y=-7.62 → dy=7.62
#   SCK/CLK lib_y=-10.16 → dy=10.16
#   SCS/CMD lib_y=-12.70 → dy=12.70
#   SENSOR_VN lib_y=22.86 → dy=-22.86
#   SENSOR_VP lib_y=25.40 → dy=-25.40
#   EN       lib_y=30.48 → dy=-30.48
left_nc_dy = [0.00, 2.54, 5.08, 7.62, 10.16, 12.70, -22.86, -25.40, -30.48]
for dy in left_nc_dy:
    px, py = esp_l(dy)
    elements.append(no_connect(px, py))

# ESP32 power — VDD at lib_y=+35.56 (dy=-35.56), GND at lib_y=-35.56 (dy=+35.56)
vdd_x, vdd_y = E_X, E_Y - 35.56
gnd_x, gnd_y = E_X, E_Y + 35.56
elements.append(wire(vdd_x, vdd_y, vdd_x, vdd_y - 5.08))
elements.append(power_sym("+3V3", vdd_x, vdd_y - 5.08, angle=270))
elements.append(wire(gnd_x, gnd_y, gnd_x, gnd_y + 5.08))
elements.append(power_sym("GND", gnd_x, gnd_y + 5.08, angle=90))

# PWR_FLAG — place at the EXACT same position as the power symbol so the
# flag's pin coincides with the power symbol's pin (same net point).
elements.append(pwr_flag(vdd_x, vdd_y - 5.08))   # on +3V3 net
elements.append(pwr_flag(gnd_x, gnd_y + 5.08))   # on GND net

# ── MAX31865xAP ──────────────────────────────────────────────────────────────
max_pins = [str(p) for p in range(1, 21)]
elements.append(sym_instance(
    "Sensor_Temperature:MAX31865xAP", "U2", "MAX31865xAP",
    M_X, M_Y, 0, max_pins, ref_dx=-11, ref_dy=-22,
    footprint="Package_SO:SSOP-20_5.3x7.2mm_P0.65mm"))

M_L = M_X - 15.24
M_R = M_X + 15.24

# Left-side SPI pins — abs_y = M_Y - lib_y
spi_left = [
    (15.24, "DRDY"),      # pin 1  ~{DRDY} — no-connect (output, optional)
    (12.70, "SPI_MOSI"),  # pin 14 SDI
    (10.16, "SPI_SCK"),   # pin 15 SCLK
    ( 7.62, "SPI_CS"),    # pin 16 ~{CS}
    ( 5.08, "SPI_MISO"),  # pin 17 SDO
]
for lib_y, net in spi_left:
    py = M_Y - lib_y
    if net == "DRDY":
        elements.append(no_connect(M_L, py))
    else:
        elements.append(stub(M_L, py, STUB, net, right_side=False))

# NC for left-side NC pin (pin 20, lib x=-12.70, lib y=-2.54 → abs_y=M_Y+2.54)
elements.append(no_connect(M_X - 12.70, M_Y + 2.54))

# Right-side RTD connections — abs_y = M_Y - lib_y
rtd_right = [
    ( 12.70, "REFIN+"),  # pin 5
    (  7.62, "REFIN-"),  # pin 6
    (  0.00, "FORCE+"),  # pin 8
    ( -5.08, "RTDIN+"),  # pin 10
    (-10.16, "RTDIN-"),  # pin 11
    (-12.70, "FORCE-"),  # pin 12
]
for lib_y, net in rtd_right:
    py = M_Y - lib_y
    elements.append(stub(M_R, py, STUB, net, right_side=True))

# NC for unused right-side pins (BIAS pin 4 lib_y=15.24, FORCE2 pin 9 lib_y=-2.54,
# ISENSOR pin 7 lib_y=5.08)
for lib_y in [15.24, -2.54, 5.08]:
    elements.append(no_connect(M_R, M_Y - lib_y))

# MAX31865 power — VDD/DVDD at lib_y=+20.32, GND/DGND at lib_y=-17.78
for dx in [2.54, -2.54]:
    vx, vy = M_X + dx, M_Y - 20.32
    elements.append(wire(vx, vy, vx, vy - 5.08))
    elements.append(power_sym("+3V3", vx, vy - 5.08, angle=270))
for dx in [2.54, -2.54]:
    gx, gy = M_X + dx, M_Y + 17.78
    elements.append(wire(gx, gy, gx, gy + 5.08))
    elements.append(power_sym("GND", gx, gy + 5.08, angle=90))

# ── J1 — PT1000 / RTD connector (Conn_01x06) ────────────────────────────────
# Placed to the right of MAX31865. Net labels on J1's left side match the
# RTD net labels on MAX31865's right side → same named net = ERC closed.
# Conn_01x06 pins: lib x=-5.08, lib y: pin1=5.08 pin2=2.54 ... pin6=-7.62
# Connection endpoint abs coords: x = J1_X - 5.08, y = J1_Y - lib_y
J1_X = M_X + 50.80    # 292.10 mm (20×2.54 right of MAX31865)
J1_Y = M_Y + 2.54     # 78.74  mm (31×2.54)

elements.append(sym_instance(
    "Connector_Generic:Conn_01x06", "J1", "PT1000-Connector",
    J1_X, J1_Y, 0, ["1","2","3","4","5","6"],
    ref_dx=-4, ref_dy=-10,
    footprint="Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical"))

J1_EP_X = J1_X - 5.08   # 287.02 mm — left-pin endpoint x

# abs_y = J1_Y - lib_pin_y  (corrected sign: library y is up-positive)
j1_pins = [
    ( 5.08, "REFIN+"),   # pin 1: abs_y = 78.74 - 5.08 = 73.66
    ( 2.54, "FORCE+"),   # pin 2: abs_y = 78.74 - 2.54 = 76.20  (= M_Y)
    ( 0.00, "RTDIN+"),   # pin 3: abs_y = 78.74
    (-2.54, "RTDIN-"),   # pin 4: abs_y = 78.74 + 2.54 = 81.28
    (-5.08, "FORCE-"),   # pin 5: abs_y = 78.74 + 5.08 = 83.82
    (-7.62, "REFIN-"),   # pin 6: abs_y = 78.74 + 7.62 = 86.36
]
for lib_y, net in j1_pins:
    py = J1_Y - lib_y
    elements.append(stub(J1_EP_X, py, STUB, net, right_side=False))

# ── DHT11 × 2 ────────────────────────────────────────────────────────────────
# DHT11 pins: DATA lib_y=0 (abs_y=oy), VDD lib_y=7.62 (abs_y=oy-7.62),
#             GND lib_y=-7.62 (abs_y=oy+7.62)
for ref, net, ox, oy in [("U3", "DHT_CEILING", D1_X, D1_Y),
                           ("U4", "DHT_BENCH",   D2_X, D2_Y)]:
    elements.append(sym_instance(
        "Sensor:DHT11", ref, "DHT21/AM2301",
        ox, oy, 0, ["1","2","3","4"], ref_dx=-4, ref_dy=-8,
        footprint="Sensor:Aosong_DHT11_5.5x12.0_P2.54mm"))
    elements.append(stub(ox + 7.62, oy, STUB, net, right_side=True))
    elements.append(wire(ox, oy - 7.62, ox, oy - 10.16))
    elements.append(power_sym("+3V3", ox, oy - 10.16, angle=270))
    elements.append(wire(ox, oy + 7.62, ox, oy + 10.16))
    elements.append(power_sym("GND", ox, oy + 10.16, angle=90))

# ── ULN2003A × 2 ─────────────────────────────────────────────────────────────
# ULN2003 pin layout (abs_y = oy - lib_y):
#   Inputs  I1-I7: lib x=-10.16, lib_y=5.08/2.54/0/-2.54/-5.08/-7.62/-10.16
#   Outputs O1-O7: lib x=+10.16, lib_y=5.08/2.54/0/-2.54/-5.08/-7.62/-10.16
#   GND: lib x=0, lib_y=-15.24 → abs_y=oy+15.24
#   COM: lib x=+10.16, lib_y=10.16 → abs_y=oy-10.16

outflow_nets = ["OUTFLOW_I1", "OUTFLOW_I2", "OUTFLOW_I3", "OUTFLOW_I4"]
inflow_nets  = ["INFLOW_I1",  "INFLOW_I2",  "INFLOW_I3",  "INFLOW_I4"]

for ref, nets, ox, oy, jref in [("U5", outflow_nets, U1_X, U1_Y, "J2"),
                                  ("U6", inflow_nets,  U2_X, U2_Y, "J3")]:
    elements.append(sym_instance(
        "Transistor_Array:ULN2003", ref, "ULN2003A",
        ox, oy, 0,
        [str(p) for p in range(1, 17)],
        ref_dx=-7, ref_dy=-15,
        footprint="Package_DIP:DIP-16_W7.62mm"))

    # Input stubs I1-I4 (lib_y: 5.08/2.54/0/-2.54 → abs_y = oy - lib_y)
    in_lib_y = [5.08, 2.54, 0.0, -2.54]
    for lib_y, net in zip(in_lib_y, nets):
        elements.append(stub(ox - 10.16, oy - lib_y, STUB, net, right_side=False))

    # Unused inputs I5-I7: no-connect (lib_y=-5.08/-7.62/-10.16 → abs_y=oy+5.08/7.62/10.16)
    for lib_y in [-5.08, -7.62, -10.16]:
        elements.append(no_connect(ox - 10.16, oy - lib_y))

    # COM (motor VCC): lib x=+10.16, lib_y=+10.16 → abs_y = oy-10.16
    com_x, com_y = ox + 10.16, oy - 10.16
    elements.append(wire(com_x, com_y, com_x + 5.08, com_y))
    elements.append(power_sym("+5V", com_x + 5.08, com_y, angle=0))

    # Output stubs O1-O4 (lib_y: 5.08/2.54/0/-2.54 → abs_y = oy - lib_y)
    out_labels = ["28BYJ_A", "28BYJ_B", "28BYJ_C", "28BYJ_D"]
    out_lib_y  = [5.08, 2.54, 0.0, -2.54]
    for lib_y, lbl in zip(out_lib_y, out_labels):
        px, py = ox + 10.16, oy - lib_y
        elements.append(stub(px, py, STUB, f"{ref}_{lbl}", right_side=True))

    # Unused outputs O5-O7: no-connect (lib_y=-5.08/-7.62/-10.16 → abs_y=oy+5.08/7.62/10.16)
    for lib_y in [-5.08, -7.62, -10.16]:
        elements.append(no_connect(ox + 10.16, oy - lib_y))

    # GND: lib x=0, lib_y=-15.24 → abs_y = oy+15.24
    elements.append(wire(ox, oy + 15.24, ox, oy + 20.32))
    elements.append(power_sym("GND", ox, oy + 20.32, angle=90))

    # ── Stepper motor connector (Conn_01x04) ──────────────────────────────
    # Place to the right of ULN2003 output stubs.
    # Conn_01x04 pins: lib x=-5.08, lib_y=2.54/0/-2.54/-5.08 (pins 1-4)
    # Connection endpoint: x = Jx-5.08, abs_y = Jy - lib_pin_y
    #
    # Want J pin 1 at abs_y = oy-5.08 (same as O1 label):
    #   Jy - 2.54 = oy - 5.08  →  Jy = oy - 2.54  (62×2.54 for oy=160.02)
    Jx = ox + 10.16 + STUB + 15.24   # past output stubs, room for connector body
    Jy = oy - 2.54                    # 62×2.54 grid-aligned

    elements.append(sym_instance(
        "Connector_Generic:Conn_01x04", jref, "28BYJ-48",
        Jx, Jy, 0, ["1","2","3","4"],
        ref_dx=-4, ref_dy=-8,
        footprint="Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical"))

    Jp_x = Jx - 5.08   # left-pin endpoint x

    # Connect J2/J3 pins directly to ULN output stub endpoints via plain wires.
    # ULN output label sits at (ox+10.16+STUB, py) = (Jp_x-STUB, py).
    # A wire from Jp_x to Jp_x-STUB bridges the connector pin to that endpoint,
    # so the single ULN net label connects both sides — no duplicate label needed.
    for lib_y in [2.54, 0.0, -2.54, -5.08]:   # Conn_01x04 pins 1-4
        py = Jy - lib_y
        elements.append(wire(Jp_x, py, Jp_x - STUB, py))

# PWR_FLAG on +5V — overlaid on U5's existing COM power symbol position.
# (same x,y as the power_sym("+5V", com_x+5.08, com_y) placed in the U5 loop)
flag5v_x = U1_X + 10.16 + 5.08   # = 226.06 mm (89×2.54)
flag5v_y = U1_Y - 10.16           # = 149.86 mm (59×2.54)
elements.append(pwr_flag(flag5v_x, flag5v_y))

# ---------------------------------------------------------------------------
# Embed library symbols
# ---------------------------------------------------------------------------
def embed(lib_file, *names):
    lib_name = os.path.splitext(os.path.basename(lib_file))[0]
    out = []
    for name in names:
        raw = extract_symbol(lib_file, name)
        prefixed = f"{lib_name}:{name}"
        raw = raw.replace(f'(symbol "{name}"', f'(symbol "{prefixed}"', 1)
        indented = "\n".join("    " + ln if ln.strip() else ln
                             for ln in raw.splitlines())
        out.append(indented)
    return "\n".join(out)

lib_symbols = "\n".join([
    embed(f"{KICAD_SYM}/RF_Module.kicad_sym",            "ESP32-WROOM-32"),
    embed(f"{KICAD_SYM}/Sensor_Temperature.kicad_sym",    "MAX31865xAP"),
    embed(f"{KICAD_SYM}/Transistor_Array.kicad_sym",      "ULN2003"),
    embed(f"{KICAD_SYM}/Sensor.kicad_sym",                "DHT11"),
    embed(f"{KICAD_SYM}/Connector_Generic.kicad_sym",     "Conn_01x04", "Conn_01x06"),
    embed(f"{KICAD_SYM}/power.kicad_sym",                 "GND", "+3V3", "+5V", "PWR_FLAG"),
])

body = "\n\n".join(elements)

schematic = f"""\
(kicad_sch (version 20230121) (generator "sauna_gen")

  (uuid "{SCHEMATIC_UUID}")

  (paper "A2")

  (lib_symbols
{lib_symbols}
  )

{body}

  (sheet_instances
    (path "/" (page "1"))
  )
)
"""

with open(OUT, "w") as f:
    f.write(schematic)

print(f"Written: {OUT}")
print("Open docs/kicad/SaunaStatus.kicad_pro in KiCad.")
