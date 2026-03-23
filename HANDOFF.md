# Handoff: ESP32-S3 Board Migration — Hardware Verification Remaining

**Generated**: 2026-03-23
**Branch**: master
**Status**: In Progress — firmware/schematic/PCB complete, hardware verification pending

## Goal

Fully migrate the SaunaStatus sauna controller from the old ESP32-DevKit-V1-DOIT
to the LB-ESP32S3-N16R8 (Lolin S3 form factor, ESP32-S3 N16R8 — 16 MB OPI flash,
8 MB PSRAM). All software/schematic/PCB work is committed; what remains is physical
hardware testing.

## Completed

- [x] GPIO assignments migrated to ESP32-S3 in `src/gpio_config.h`
- [x] `platformio.ini` — `lb_esp32s3` environment added (lolin_s3 board, OPI memory, 16 MB partition table)
- [x] `partitions_ota_16mb.csv` — 16 MB OTA partition table (spiffs named correctly for LittleFS)
- [x] 21 native unit tests for GPIO config (`test/test_gpio_config/`) — all passing
- [x] KiCad schematic — U1 ESP32-DevKit replaced with J_LEFT / J_RIGHT Conn_01x20 connectors, all nets verified
- [x] KiCad schematic — root scope corruption fixed (spurious `)` at line 3626 caused all connectors to be outside the kicad_sch scope, invisible to ERC/netlist export)
- [x] KiCad PCB — J_LEFT and J_RIGHT footprints (PinHeader_1x20) added with correct pad/net assignments
- [x] KiCad PCB — connector stub footprints (J_U5/J_U6/J_U7) populated with pads and net assignments
- [x] `pcb import` succeeds (exit 0) — SaunaStatus.zen generated; 0 blocking parity violations
- [x] `docs/pinout.md` updated for ESP32-S3
- [x] `CLAUDE.md` updated (build commands, unit test table, UART vs USB-C warning)
- [x] Boot confirmed — device boots on new hardware

## Not Yet Done

- [ ] **Hardware verification** — sensors and motors not yet tested on new hardware after boot
- [ ] **PCB footprint row spacing** — confirm center-to-center pin row spacing on physical LB board matches PCB layout (see Warnings)
- [ ] **Commit `.claude/settings.local.json`** — one-line permission addition for `sync-repos.sh` not yet staged

## Failed Approaches (Don't Repeat These)

**kicad-skip Python library** — used to edit the KiCad schematic programmatically.
Fails with `sexpdata.ExpectNothing: Too many closing brackets` on KiCad 9 schematics
(version 20250114). The library's sexpdata parser is incompatible. Workaround: direct
Python text manipulation using paren-counting to find block boundaries. See
`~/.claude/skills/learned/kicad-schematic-text-edit.md`.

**`mcp__kicad__run_erc`** — returns "Failed to load schematic" on this project (missing
`.kicad_pro` file or KiCad CLI not available). Workaround: use `kicad-cli sch erc` directly
or use `mcp__kicad__list_schematic_components` + `mcp__kicad__list_schematic_nets` to verify
schematic validity instead.

**`board_build.arduino.memory_type = qio_opi` without USB CDC flags** — caused boot
failure on first flash. The LB-ESP32S3-N16R8 requires both
`-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=0` in build_flags to boot from
the UART connector. Without these, the device appears dead. Resolution: chip erase
+ reflash with correct flags.

**`pcb import` net insertion inside pad block** — when adding new nets to the PCB's
global net table, `re.match(r'\s+(net \d+ "', l)` matches BOTH global net table entries
AND pad-internal net references (which have 3 tabs vs 1 tab). Fix: use `^\t(net` (exactly
1 tab) to find only global-scope net entries.

## Key Decisions

| Decision | Rationale |
|----------|-----------|
| `board = lolin_s3` | LB-ESP32S3-N16R8 is pin-for-pin compatible; confirmed by silkscreen |
| `src/gpio_config.h` as pure `#define` header | Allows native testing without Arduino toolchain |
| LittleFS partition named `spiffs` | `LittleFS.begin()` searches for partition named `spiffs` — any other name causes mount failure at boot |
| GPIO35–38 marked reserved | Appear on header (SPIIO6/7, SPIDQS, FSPIWP) but may be connected to OPI flash interface; untested |
| J_LEFT / J_RIGHT Conn_01x20 in schematic | Replaces bare DevKit IC symbol with header connectors matching physical board |
| U1 left in PCB as `extra_footprint` | `pcb import` tolerates extra footprints; removing U1 tracks/silkscreen would require PCB layout work |
| Unconnected-* nets added to PCB global table | KiCad DRC parity check requires pad nets to match schematic even for no_connect pins |

## Current State

**Working**:
- Device boots on LB-ESP32S3-N16R8
- Firmware compiles clean: `pio run -t build`
- All 21 GPIO unit tests pass: `pio test -e native -f test_gpio_config`
- KiCad schematic: 0 ERC errors (86 warnings, non-blocking)
- KiCad PCB DRC: 0 blocking parity violations
- `pcb import`: exit 0, SaunaStatus.zen generated

**Not yet verified on hardware**:
- DHT21 ceiling (GPIO8) and bench (GPIO9) temperature/humidity readings
- MAX31865 stove sensor SPI communication (GPIO39–42, CS=GPIO42)
- Outflow stepper motor (GPIO4–7) and inflow stepper (GPIO15–18)
- INA260 power monitor I2C (SDA=GPIO1, SCL=GPIO2)

**Uncommitted Changes**:
- `.claude/settings.local.json` — added `Bash(bash ~/bin/sync-repos.sh --dry-run)` permission

## Files to Know

| File | Why It Matters |
|------|----------------|
| `src/gpio_config.h` | All ESP32-S3 GPIO `#define`s — single source of truth for pin assignments |
| `platformio.ini` | `lb_esp32s3` is default env; `upesy_wroom` is legacy reference only — **do not flash** |
| `partitions_ota_16mb.csv` | Custom 16 MB OTA partition table; `spiffs` partition name is mandatory |
| `test/test_gpio_config/test_main.cpp` | 21 native GPIO tests; run with `pio test -e native -f test_gpio_config` |
| `docs/kicad/SaunaStatus.kicad_sch` | Updated schematic with J_LEFT/J_RIGHT replacing U1; root scope fixed |
| `docs/kicad/SaunaStatus.kicad_pcb` | Updated PCB with J_LEFT/J_RIGHT footprints and connector stub pads |
| `src/secrets.h` | Not committed; must define WiFi, InfluxDB, MQTT, and auth credentials |

## GPIO Assignments (ESP32-S3)

```cpp
// SPI (MAX31865 stove sensor)
#define PIN_SPI_SCK   41
#define PIN_SPI_MISO  40
#define PIN_SPI_MOSI  39
#define PIN_SPI_CS    42

// I2C (INA260 power monitor)
#define PIN_I2C_SDA   1
#define PIN_I2C_SCL   2

// DHT21 sensors
#define PIN_DHT_CEILING  8
#define PIN_DHT_BENCH    9

// Outflow stepper (ULN2003 -> 28BYJ-48)
#define PIN_OUTFLOW_IN1  4
#define PIN_OUTFLOW_IN2  5
#define PIN_OUTFLOW_IN3  6
#define PIN_OUTFLOW_IN4  7

// Inflow stepper (ULN2003 -> 28BYJ-48)
#define PIN_INFLOW_IN1  15
#define PIN_INFLOW_IN2  16
#define PIN_INFLOW_IN3  17
#define PIN_INFLOW_IN4  18
```

## Resume Instructions

1. **Build and upload firmware** (use UART connector — GPIO43/44, NOT USB-C OTG):
   ```bash
   pio run -t upload    # firmware only
   pio run -t uploadfs  # web UI + config.json
   ```

2. **Open serial monitor** and confirm boot messages:
   ```bash
   pio device monitor   # 115200 baud
   ```
   Expected: WiFi connected, NTP synced, sensors initializing

3. **Verify each sensor** on the serial output:
   - `ceiling_temp` / `ceiling_hum` — DHT21 ceiling (GPIO8)
   - `bench_temp` / `bench_hum` — DHT21 bench (GPIO9)
   - `stove_temp` — MAX31865 PT1000 (SPI)
   - `bus_voltage_V` / `current_mA` — INA260 (if present)
   - If sensor shows `NAN` at startup: check wiring; DHT sensors need 10kΩ pull-up DATA→VCC

4. **Verify motors** via HTTP (device at `192.168.1.200`):
   ```bash
   curl "http://192.168.1.200/motor?motor=outflow&cmd=cw&steps=64" \
        -H "Authorization: Bearer <token>"
   ```
   Expected: outflow motor steps CW

5. **Commit the settings file**:
   ```bash
   git add .claude/settings.local.json
   git commit -m "chore(claude): add sync-repos dry-run permission"
   ```

## pcb Toolchain Usage

```bash
# Validate schematic and PCB (runs ERC + DRC parity)
/home/mmessano/Documents/repos/pcb/target/release/pcb import \
    docs/kicad/SaunaStatus.kicad_pro /tmp/pcb_out

# Expected: exit 0, warns about ERC/DRC non-blocking errors
# Generates: /tmp/pcb_out/boards/SaunaStatus/SaunaStatus.zen
```

## Warnings

**PCB footprint row spacing**: The PCB file was updated with the schematic but the
physical pin row spacing of the LB-ESP32S3-N16R8 has not been measured. The board
uses 2x20 through-hole at 2.54 mm pitch, but the center-to-center distance between
the two header rows (left and right) may differ from the old DevKit footprint. If the
PCB footprint doesn't match, the board won't seat. **Measure the physical board before
ordering PCBs.**

**GPIO35–38 are on the header silkscreen** (labeled SPIIO6, SPIIO7/SPIID7, SPIDQS,
FSPIWP) but the N16R8 module may use them internally for OPI flash. Treat as reserved
— the unit tests assert they are excluded from all motor/sensor assignments.

**Two USB connectors on the board**: Use the UART connector (GPIO43/44) for `pio device monitor`
and firmware upload. The USB-C OTG port (GPIO19/20) is disabled in firmware
(`-DARDUINO_USB_CDC_ON_BOOT=0`).

**`pio run` uploads both firmware AND filesystem** (targets = upload, uploadfs in
platformio.ini). Use explicit targets: `pio run -t upload` or `pio run -t uploadfs`.

**86 ERC warnings remain** (non-blocking for `pcb import`): unconnected_wire_endpoint,
label_dangling, no_connect_dangling, endpoint_off_grid, lib_symbol_mismatch. These
are schematic design issues (wire stubs, off-grid pins near J_LEFT/J_RIGHT) that
predate the migration and don't affect functionality.
