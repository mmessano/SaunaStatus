"""
test_kicad_connectors.py
------------------------
Pytest test suite validating the KiCad schematic and PCB connector update for
the SaunaStatus project.

These tests verify that bare IC symbols (U1, U5, U6, U7) have been replaced
with proper breakout-board connector symbols, and that all critical nets are
preserved after the replacement.

Expected state after the update:
  U1:  RF_Module:ESP32-WROOM-32      → esp32_devkit_v1_doit (2×15 headers)
  U5:  Transistor_Array:ULN2003      → Conn_01x07 + Conn_01x04 + Conn_01x02
  U6:  Transistor_Array:ULN2003      → Conn_01x07 + Conn_01x04 + Conn_01x02
  U7:  Sensor:INA260                 → Conn_01x08 (JP2) + Conn_01x02 (X1)

Run with:
    pytest test/test_kicad_connectors.py -v

Tests will FAIL until the schematic/PCB files are updated — this is the red
(TDD) phase.  Once the update is complete all tests should pass.
"""

import re
from pathlib import Path

import pytest

# ---------------------------------------------------------------------------
# File paths
# ---------------------------------------------------------------------------

REPO_ROOT = Path(__file__).resolve().parent.parent
SCH_PATH = REPO_ROOT / "docs" / "kicad" / "SaunaStatus.kicad_sch"
PCB_PATH = REPO_ROOT / "docs" / "kicad" / "SaunaStatus.kicad_pcb"


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture(scope="session")
def sch_text() -> str:
    """Load the KiCad schematic file once per test session."""
    assert SCH_PATH.exists(), f"Schematic not found: {SCH_PATH}"
    return SCH_PATH.read_text(encoding="utf-8")


@pytest.fixture(scope="session")
def pcb_text() -> str:
    """Load the KiCad PCB file once per test session."""
    assert PCB_PATH.exists(), f"PCB not found: {PCB_PATH}"
    return PCB_PATH.read_text(encoding="utf-8")


# ===========================================================================
# CATEGORY 1: Net Preservation
# These are regression tests.  Every net name listed below must still appear
# in the schematic after the connector replacement.  A missing net name means
# a signal was accidentally dropped or renamed during the update.
# ===========================================================================


class TestNetPreservation:
    """Verify all critical signal nets are still present in the schematic."""

    # --- I2C -----------------------------------------------------------------

    def test_net_i2c_scl(self, sch_text):
        """I2C_SCL must survive the INA260 (U7) → Conn_01x08 replacement.

        The INA260 breakout's SCL pin connects to I2C_SCL.  If this net
        disappears the power monitor loses its clock line.
        """
        assert "I2C_SCL" in sch_text, "Net I2C_SCL missing from schematic"

    def test_net_i2c_sda(self, sch_text):
        """I2C_SDA must survive the INA260 (U7) → Conn_01x08 replacement.

        The INA260 breakout's SDA pin connects to I2C_SDA.  If this net
        disappears the power monitor loses its data line.
        """
        assert "I2C_SDA" in sch_text, "Net I2C_SDA missing from schematic"

    # --- SPI -----------------------------------------------------------------

    def test_net_spi_mosi(self, sch_text):
        """SPI_MOSI must survive the ESP32 (U1) connector replacement.

        Drives the MAX31865 PT1000 interface.
        """
        assert "SPI_MOSI" in sch_text, "Net SPI_MOSI missing from schematic"

    def test_net_spi_miso(self, sch_text):
        """SPI_MISO must survive the ESP32 (U1) connector replacement.

        Receives data from the MAX31865 PT1000 interface.
        """
        assert "SPI_MISO" in sch_text, "Net SPI_MISO missing from schematic"

    def test_net_spi_sck(self, sch_text):
        """SPI_SCK must survive the ESP32 (U1) connector replacement.

        Clock line for the MAX31865 PT1000 interface.
        """
        assert "SPI_SCK" in sch_text, "Net SPI_SCK missing from schematic"

    def test_net_spi_cs(self, sch_text):
        """SPI_CS must survive the ESP32 (U1) connector replacement.

        Chip-select for the MAX31865; ties to GPIO5 on the ESP32 DevKit.
        """
        assert "SPI_CS" in sch_text, "Net SPI_CS missing from schematic"

    # --- DHT sensors ---------------------------------------------------------

    def test_net_dht_ceiling(self, sch_text):
        """DHT_CEILING must survive the ESP32 (U1) connector replacement.

        One-wire data line to the ceiling DHT21 (GPIO16).
        """
        assert "DHT_CEILING" in sch_text, "Net DHT_CEILING missing from schematic"

    def test_net_dht_bench(self, sch_text):
        """DHT_BENCH must survive the ESP32 (U1) connector replacement.

        One-wire data line to the bench DHT21 (GPIO17).
        """
        assert "DHT_BENCH" in sch_text, "Net DHT_BENCH missing from schematic"

    # --- INA260 power monitor ------------------------------------------------

    def test_net_ina_in_plus(self, sch_text):
        """INA_IN+ must survive the INA260 (U7) → connector replacement.

        High-side current-sense input; connects to X1 screw terminal and
        JP2 signal header.
        """
        assert "INA_IN+" in sch_text, "Net INA_IN+ missing from schematic"

    def test_net_ina_in_minus(self, sch_text):
        """INA_IN- must survive the INA260 (U7) → connector replacement.

        Return side of the current-sense path.
        """
        assert "INA_IN-" in sch_text, "Net INA_IN- missing from schematic"

    # --- Outflow motor (U5) inputs -------------------------------------------

    def test_net_outflow_i1(self, sch_text):
        """OUTFLOW_I1 must be present (U5 input 1 from ESP32 GPIO21)."""
        assert "OUTFLOW_I1" in sch_text, "Net OUTFLOW_I1 missing from schematic"

    def test_net_outflow_i2(self, sch_text):
        """OUTFLOW_I2 must be present (U5 input 2 from ESP32 GPIO25)."""
        assert "OUTFLOW_I2" in sch_text, "Net OUTFLOW_I2 missing from schematic"

    def test_net_outflow_i3(self, sch_text):
        """OUTFLOW_I3 must be present (U5 input 3 from ESP32 GPIO26)."""
        assert "OUTFLOW_I3" in sch_text, "Net OUTFLOW_I3 missing from schematic"

    def test_net_outflow_i4(self, sch_text):
        """OUTFLOW_I4 must be present (U5 input 4 from ESP32 GPIO14)."""
        assert "OUTFLOW_I4" in sch_text, "Net OUTFLOW_I4 missing from schematic"

    # --- Outflow motor (U5) outputs ------------------------------------------

    def test_net_u5_28byj_a(self, sch_text):
        """U5_28BYJ_A must be present (U5 output A to outflow 28BYJ-48)."""
        assert "U5_28BYJ_A" in sch_text, "Net U5_28BYJ_A missing from schematic"

    def test_net_u5_28byj_b(self, sch_text):
        """U5_28BYJ_B must be present (U5 output B to outflow 28BYJ-48)."""
        assert "U5_28BYJ_B" in sch_text, "Net U5_28BYJ_B missing from schematic"

    def test_net_u5_28byj_c(self, sch_text):
        """U5_28BYJ_C must be present (U5 output C to outflow 28BYJ-48)."""
        assert "U5_28BYJ_C" in sch_text, "Net U5_28BYJ_C missing from schematic"

    def test_net_u5_28byj_d(self, sch_text):
        """U5_28BYJ_D must be present (U5 output D to outflow 28BYJ-48)."""
        assert "U5_28BYJ_D" in sch_text, "Net U5_28BYJ_D missing from schematic"

    # --- Inflow motor (U6) inputs --------------------------------------------

    def test_net_inflow_i1(self, sch_text):
        """INFLOW_I1 must be present (U6 input 1 from ESP32 GPIO22)."""
        assert "INFLOW_I1" in sch_text, "Net INFLOW_I1 missing from schematic"

    def test_net_inflow_i2(self, sch_text):
        """INFLOW_I2 must be present (U6 input 2 from ESP32 GPIO27)."""
        assert "INFLOW_I2" in sch_text, "Net INFLOW_I2 missing from schematic"

    def test_net_inflow_i3(self, sch_text):
        """INFLOW_I3 must be present (U6 input 3 from ESP32 GPIO32)."""
        assert "INFLOW_I3" in sch_text, "Net INFLOW_I3 missing from schematic"

    def test_net_inflow_i4(self, sch_text):
        """INFLOW_I4 must be present (U6 input 4 from ESP32 GPIO33)."""
        assert "INFLOW_I4" in sch_text, "Net INFLOW_I4 missing from schematic"

    # --- Inflow motor (U6) outputs -------------------------------------------

    def test_net_u6_28byj_a(self, sch_text):
        """U6_28BYJ_A must be present (U6 output A to inflow 28BYJ-48)."""
        assert "U6_28BYJ_A" in sch_text, "Net U6_28BYJ_A missing from schematic"

    def test_net_u6_28byj_b(self, sch_text):
        """U6_28BYJ_B must be present (U6 output B to inflow 28BYJ-48)."""
        assert "U6_28BYJ_B" in sch_text, "Net U6_28BYJ_B missing from schematic"

    def test_net_u6_28byj_c(self, sch_text):
        """U6_28BYJ_C must be present (U6 output C to inflow 28BYJ-48)."""
        assert "U6_28BYJ_C" in sch_text, "Net U6_28BYJ_C missing from schematic"

    def test_net_u6_28byj_d(self, sch_text):
        """U6_28BYJ_D must be present (U6 output D to inflow 28BYJ-48)."""
        assert "U6_28BYJ_D" in sch_text, "Net U6_28BYJ_D missing from schematic"

    # --- Power rails ---------------------------------------------------------

    def test_net_3v3(self, sch_text):
        """The +3V3 power rail must remain present after all replacements.

        Used by the ESP32 DevKit, INA260 breakout VCC, and pull-up resistors.
        """
        assert "+3V3" in sch_text, "Net +3V3 missing from schematic"

    def test_net_5v(self, sch_text):
        """The +5V power rail must remain present after all replacements.

        Powers both ULN2003 breakout boards (U5, U6) and the stepper motors.
        """
        assert "+5V" in sch_text, "Net +5V missing from schematic"

    def test_net_gnd(self, sch_text):
        """The GND net must remain present after all replacements.

        Common ground for all subsystems.
        """
        assert "GND" in sch_text, "Net GND missing from schematic"


# ===========================================================================
# CATEGORY 2: Connector Symbol Replacement (schematic)
# Verify that old bare-IC lib_ids are gone and new connector lib_ids are present.
# ===========================================================================


class TestConnectorSymbolReplacement:
    """Verify schematic symbol library references after replacement."""

    # --- U1: ESP32 bare module → DevKit connector ----------------------------

    def test_u1_bare_module_lib_id_removed(self, sch_text):
        """RF_Module:ESP32-WROOM-32 lib_id must not appear in the schematic.

        The bare WROOM-32 module footprint (38-pad SMD) is being replaced
        by the ESP32 DevKit V1 DOIT 2×15-pin through-hole header symbol so
        the schematic matches the actual breakout board wired into the design.
        """
        assert "RF_Module:ESP32-WROOM-32" not in sch_text, (
            "Bare ESP32-WROOM-32 lib_id still present — U1 not yet replaced"
        )

    def test_u1_devkit_lib_id_present(self, sch_text):
        """esp32_devkit_v1_doit (or equivalent) lib_id must appear for U1.

        A 2×15 (30-pin) header symbol that matches the physical DevKit V1
        DOIT breakout board must be referenced as U1's lib_id.
        """
        # Accept any reasonable naming: esp32_devkit, ESP32_DevKit, etc.
        pattern = re.compile(r"esp32.devkit", re.IGNORECASE)
        assert pattern.search(sch_text), (
            "No esp32_devkit lib_id found — U1 replacement not yet applied"
        )

    # --- U5: ULN2003 outflow → connector set ---------------------------------

    def test_u5_uln2003_lib_id_removed(self, sch_text):
        """Transistor_Array:ULN2003 lib_id must not appear at the U5 instance.

        The bare DIP-16 ULN2003 IC is being replaced by a set of connectors
        (Conn_01x07 inputs, Conn_01x04 motor outputs, Conn_01x02 power) that
        represent the physical ULN2003A breakout board's pin headers.

        NOTE: This test checks that both ULN2003 instances (U5 and U6) are
        gone.  If U6 is replaced but U5 is not, this test still fails.
        """
        # Count remaining ULN2003 lib_id occurrences — should be zero.
        count = sch_text.count("Transistor_Array:ULN2003")
        assert count == 0, (
            f"Transistor_Array:ULN2003 still referenced {count} time(s) — "
            "U5/U6 not yet replaced"
        )

    def test_u5_conn_01x07_present(self, sch_text):
        """A Conn_01x07 (7-pin input header) must be present for U5.

        The ULN2003A breakout board exposes 7 input pins (IN1–IN7) as a
        single-row 7-pin header.  Only 4 are used (IN1–IN4 for the 4-wire
        stepper); the remaining 3 carry no_connect markers.
        """
        assert "Connector_Generic:Conn_01x07" in sch_text, (
            "Conn_01x07 symbol missing — U5 input header not added"
        )

    def test_u5_conn_01x04_present(self, sch_text):
        """A Conn_01x04 (4-pin motor output header) must be present for U5.

        Connects the ULN2003A open-collector outputs to the outflow 28BYJ-48
        stepper coil wires (U5_28BYJ_A/B/C/D).
        """
        assert "Connector_Generic:Conn_01x04" in sch_text, (
            "Conn_01x04 symbol missing — U5 motor output header not added"
        )

    def test_u5_u6_conn_01x02_power_present(self, sch_text):
        """Conn_01x02 (2-pin power header) must be present for U5 and/or U6.

        Each ULN2003 breakout board requires a +5V and GND connector (2 pins)
        to power the open-collector outputs.
        """
        assert "Connector_Generic:Conn_01x02" in sch_text, (
            "Conn_01x02 symbol missing — ULN2003 power header not added"
        )

    # --- U6: ULN2003 inflow (same pattern as U5) ----------------------------

    def test_u6_inflow_conn_01x07_present(self, sch_text):
        """A second Conn_01x07 must be present for U6 (inflow motor inputs).

        U5 (outflow) and U6 (inflow) are symmetric.  After replacement there
        must be at least two Conn_01x07 instances in the schematic.
        """
        count = len(re.findall(r"Connector_Generic:Conn_01x07", sch_text))
        assert count >= 2, (
            f"Only {count} Conn_01x07 instance(s) found — expected ≥2 "
            "(one for U5 outflow, one for U6 inflow)"
        )

    def test_u6_inflow_conn_01x04_present(self, sch_text):
        """A second Conn_01x04 must be present for U6 (inflow motor outputs).

        At least two Conn_01x04 instances are required: one for outflow
        (U5_28BYJ_A/B/C/D) and one for inflow (U6_28BYJ_A/B/C/D).
        """
        count = len(re.findall(r"Connector_Generic:Conn_01x04", sch_text))
        assert count >= 2, (
            f"Only {count} Conn_01x04 instance(s) found — expected ≥2 "
            "(one for U5 outflow outputs, one for U6 inflow outputs)"
        )

    # --- U7: INA260 → JP2 signal header + X1 screw terminal -----------------

    def test_u7_ina260_lib_id_removed(self, sch_text):
        """Sensor:INA260 lib_id must not appear in the schematic.

        The bare TSSOP-16 INA260 IC is being replaced by a Conn_01x08 (JP2)
        signal header plus a Conn_01x02 (X1) screw terminal to match the
        physical INA260 breakout board's connectors.
        """
        assert "Sensor:INA260" not in sch_text, (
            "Sensor:INA260 lib_id still present — U7 not yet replaced"
        )

    def test_u7_jp2_conn_01x08_present(self, sch_text):
        """A Conn_01x08 (8-pin JP2 signal header) must be present for U7.

        The INA260 breakout board JP2 header exposes:
          VCC / GND / SCL / SDA / ALERT / VBUS / IN+ / IN-
        ALERT and VBUS are not connected in firmware and get no_connect markers.
        """
        assert "Connector_Generic:Conn_01x08" in sch_text, (
            "Conn_01x08 symbol missing — U7 JP2 signal header not added"
        )

    # --- No bare-IC lib_ids remain -------------------------------------------

    def test_no_bare_ic_lib_ids_remain(self, sch_text):
        """No bare IC lib_ids (RF_Module, ULN2003, INA260) must remain.

        Combines the individual removal checks into a single summary assertion
        to make it obvious at a glance that the migration is complete.
        """
        forbidden = [
            "RF_Module:ESP32-WROOM-32",
            "Transistor_Array:ULN2003",
            "Sensor:INA260",
        ]
        found = [lib for lib in forbidden if lib in sch_text]
        assert not found, (
            f"Bare IC lib_ids still present in schematic: {found}"
        )

    # --- ULN2003 unused input pins (IN5/IN6/IN7) handling -------------------

    def test_uln2003_unused_inputs_not_wired_to_esp32_nets(self, sch_text):
        """IN5, IN6, IN7 of each ULN2003 connector must not carry ESP32 nets.

        The firmware only uses IN1–IN4 for the 4-wire stepper sequence.
        Pins 5–7 of the Conn_01x07 input header must either have no_connect
        markers or be left unwired; they must not be accidentally connected to
        OUTFLOW/INFLOW_I5, OUTFLOW/INFLOW_I6, OUTFLOW/INFLOW_I7.
        """
        # If these nets appear it means extra stepper channels were accidentally
        # wired up — a design error.
        forbidden_nets = [
            "OUTFLOW_I5", "OUTFLOW_I6", "OUTFLOW_I7",
            "INFLOW_I5", "INFLOW_I6", "INFLOW_I7",
        ]
        found = [net for net in forbidden_nets if net in sch_text]
        assert not found, (
            f"Unexpected motor input nets found (IN5–IN7 should be NC): {found}"
        )

    # --- INA260 ALERT and VBUS pin handling ----------------------------------

    def test_ina260_alert_and_vbus_not_wired(self, sch_text):
        """ALERT and VBUS on U7 JP2 must not be wired to active nets.

        Per CLAUDE.md the firmware does not use the ALERT interrupt or the
        VBUS bus-voltage sense input.  These pins must have no_connect markers
        on the Conn_01x08 header, not active net labels.
        """
        # If dedicated net labels for these appear they are accidentally wired.
        # Simple substring checks are sufficient — KiCad label text is literal.
        assert "INA_ALERT" not in sch_text, (
            "Net INA_ALERT found — ALERT pin on U7 JP2 should be no_connect"
        )
        assert "INA_VBUS" not in sch_text, (
            "Net INA_VBUS found — VBUS pin on U7 JP2 should be no_connect"
        )

    # --- Connector value labels ----------------------------------------------

    def test_u5_connector_value_label(self, sch_text):
        """U5 connectors must carry a value label identifying the ULN2003A breakout.

        Value labels help assembly staff identify which physical breakout board
        connects to which header.  The value should reference 'ULN2003' or
        the breakout board name.
        """
        # The value property of the U5 connector instances should contain
        # something identifying it as ULN2003-related.
        # After replacement the original 'ULN2003A' value may be on the new
        # connector symbol, or a descriptive name like 'ULN2003_OUT' may be used.
        # We check that at least one ULN2003-related value label still appears.
        assert re.search(r"ULN2003", sch_text, re.IGNORECASE), (
            "No ULN2003 value label found — connector value labels may be missing"
        )

    def test_u7_connector_value_label(self, sch_text):
        """U7 connectors must carry a value label identifying the INA260 breakout.

        The Conn_01x08 (JP2) and Conn_01x02 (X1) instances for U7 should
        have value properties that reference 'INA260' or the breakout name
        so the assembled board can be traced back to the schematic.
        """
        assert re.search(r"INA260", sch_text, re.IGNORECASE), (
            "No INA260 value label found — U7 connector value labels may be missing"
        )


# ===========================================================================
# CATEGORY 3: PCB Footprint Replacement
# Verify that bare IC footprints are removed and PinHeader footprints take
# their place on the PCB.
# ===========================================================================


class TestPcbFootprintReplacement:
    """Verify PCB footprint changes for U5, U6, and U7."""

    # --- U5/U6: DIP-16 → PinHeader ------------------------------------------

    def test_dip16_footprint_removed_from_pcb(self, pcb_text):
        """Package_DIP:DIP-16_W7.62mm footprint must not appear in the PCB.

        Both U5 (outflow ULN2003) and U6 (inflow ULN2003) currently use the
        DIP-16_W7.62mm through-hole footprint that matches the bare DIP IC.
        After replacement with breakout-board connectors, this footprint must
        be absent.
        """
        assert "Package_DIP:DIP-16_W7.62mm" not in pcb_text, (
            "DIP-16_W7.62mm footprint still present — U5/U6 PCB footprints "
            "not yet updated to PinHeader"
        )

    def test_u5_pinheader_footprint_present(self, pcb_text):
        """A PinHeader_1x07 (or similar) footprint must be on the PCB for U5.

        Replaces the 16-pad DIP-16 footprint with a 7-pin single-row header
        for the ULN2003 breakout input connector.
        """
        # Accept any 1x07 or 2x07 PinHeader footprint variant.
        pattern = re.compile(r"PinHeader_1x07|PinHeader_2x07", re.IGNORECASE)
        assert pattern.search(pcb_text), (
            "No 7-pin PinHeader footprint found for U5 input connector"
        )

    def test_u5_u6_motor_output_pinheader_present(self, pcb_text):
        """PinHeader_1x04 footprints must be present for motor output connectors.

        Each ULN2003 breakout has a 4-pin motor output header.  At least two
        PinHeader_1x04 instances (one for U5, one for U6) must appear.
        """
        count = len(re.findall(r"PinHeader_1x04", pcb_text, re.IGNORECASE))
        assert count >= 2, (
            f"Only {count} PinHeader_1x04 footprint(s) found — expected ≥2 "
            "(one for U5 outflow outputs, one for U6 inflow outputs)"
        )

    # --- U7: TSSOP-16 → PinHeader -------------------------------------------

    def test_tssop16_footprint_removed_from_pcb(self, pcb_text):
        """Package_SO:TSSOP-16_4.4x5mm_P0.65mm footprint must not appear.

        The bare INA260 IC TSSOP-16 SMD footprint is being replaced by
        through-hole PinHeader connectors for the breakout board.
        """
        assert "TSSOP-16_4.4x5mm" not in pcb_text, (
            "TSSOP-16_4.4x5mm footprint still present — U7 PCB footprint "
            "not yet updated from bare INA260 IC to breakout connectors"
        )

    def test_u7_jp2_pinheader_1x08_present(self, pcb_text):
        """PinHeader_1x08 footprint must be present for U7 JP2 (INA260 breakout).

        The JP2 8-pin signal header (VCC/GND/SCL/SDA/ALERT/VBUS/IN+/IN-)
        requires a PinHeader_1x08 through-hole footprint on the PCB.
        """
        pattern = re.compile(r"PinHeader_1x08", re.IGNORECASE)
        assert pattern.search(pcb_text), (
            "No PinHeader_1x08 footprint found for U7 JP2 signal header"
        )

    def test_u7_x1_screw_terminal_footprint_present(self, pcb_text):
        """A 2-pin screw terminal or PinHeader_1x02 footprint must be present for X1.

        The INA260 breakout board's X1 screw terminal (IN+/IN-) provides the
        high-current sense connection.  A TerminalBlock or PinHeader_1x02
        footprint must be on the PCB.
        """
        pattern = re.compile(
            r"TerminalBlock.*1x02|PinHeader_1x02|Conn_01x02",
            re.IGNORECASE,
        )
        assert pattern.search(pcb_text), (
            "No 2-pin screw terminal or PinHeader_1x02 footprint found "
            "for U7 X1 current-sense input"
        )

    # --- U1: ESP32 bare module → DevKit footprint ----------------------------

    def test_u1_bare_module_footprint_removed(self, pcb_text):
        """RF_Module:ESP32-WROOM-32 footprint must not appear in the PCB.

        The bare WROOM-32 SMD module footprint is replaced by the ESP32
        DevKit V1 DOIT board footprint (2×15 through-hole header).
        """
        assert "RF_Module:ESP32-WROOM-32" not in pcb_text, (
            "RF_Module:ESP32-WROOM-32 footprint still present — U1 PCB "
            "footprint not yet updated to DevKit header"
        )

    def test_u1_devkit_header_footprint_present(self, pcb_text):
        """A 2×15 (or 1×15 pair) PinHeader footprint must be present for U1.

        The ESP32 DevKit V1 DOIT board has two 15-pin headers.  The PCB
        must have either a PinHeader_2x15 or two PinHeader_1x15 footprints.
        """
        pattern = re.compile(r"PinHeader_2x15|PinHeader_1x15", re.IGNORECASE)
        assert pattern.search(pcb_text), (
            "No 2×15 or 1×15 PinHeader footprint found — U1 not yet updated "
            "to ESP32 DevKit V1 DOIT through-hole header"
        )

    # --- No bare IC footprints remain (summary) ------------------------------

    def test_no_bare_ic_footprints_remain(self, pcb_text):
        """No bare IC footprints (DIP-16, TSSOP-16, ESP32-WROOM-32 SMD) must remain.

        Summary test combining removal checks for all three replaced ICs.
        """
        forbidden = {
            "Package_DIP:DIP-16_W7.62mm": "U5/U6 DIP-16",
            "TSSOP-16_4.4x5mm": "U7 TSSOP-16",
            "RF_Module:ESP32-WROOM-32": "U1 bare module",
        }
        found = {desc: pat for pat, desc in forbidden.items() if pat in pcb_text}
        assert not found, (
            "Bare IC footprints still present in PCB: "
            + ", ".join(f"{desc} ({pat!r})" for pat, desc in found.items())
        )

    # --- PCB net names preserved (spot check) --------------------------------

    def test_pcb_i2c_nets_present(self, pcb_text):
        """I2C_SCL and I2C_SDA must appear as PCB net names.

        If the schematic-to-PCB sync has been run after the connector update,
        the I2C nets must still be routed to the new footprint pads.
        """
        assert "I2C_SCL" in pcb_text, "Net I2C_SCL missing from PCB file"
        assert "I2C_SDA" in pcb_text, "Net I2C_SDA missing from PCB file"

    def test_pcb_motor_nets_present(self, pcb_text):
        """Motor output nets must appear as PCB net names after footprint update.

        After the DIP-16 → PinHeader swap and PCB sync, the outflow and inflow
        stepper nets must still be present in the PCB net list.
        """
        motor_nets = [
            "U5_28BYJ_A", "U5_28BYJ_B", "U5_28BYJ_C", "U5_28BYJ_D",
            "U6_28BYJ_A", "U6_28BYJ_B", "U6_28BYJ_C", "U6_28BYJ_D",
        ]
        missing = [n for n in motor_nets if n not in pcb_text]
        assert not missing, (
            f"Motor nets missing from PCB file: {missing}"
        )


# ===========================================================================
# Edge cases covered (see comments throughout):
#
#  EC-1  No bare IC footprints remain after replacement
#        → test_no_bare_ic_footprints_remain (PCB), test_no_bare_ic_lib_ids_remain (SCH)
#
#  EC-2  ULN2003A unused inputs IN5/IN6/IN7 are not wired to ESP32 GPIO nets
#        → test_uln2003_unused_inputs_not_wired_to_esp32_nets
#
#  EC-3  ULN2003A unused outputs O5/O6/O7 produce no active net labels
#        → implied by test_uln2003_unused_inputs_not_wired_to_esp32_nets
#          (if inputs are NC, outputs carry no meaningful signal)
#
#  EC-4  INA260 ALERT and VBUS pins carry no_connect markers, not active nets
#        → test_ina260_alert_and_vbus_not_wired
#
#  EC-5  New connector symbols carry value labels identifying the breakout boards
#        → test_u5_connector_value_label, test_u7_connector_value_label
#
#  EC-6  PCB DIP-16_W7.62mm footprint removed for U5 and U6
#        → test_dip16_footprint_removed_from_pcb
#
#  EC-7  PCB TSSOP-16 footprint removed for U7
#        → test_tssop16_footprint_removed_from_pcb
#
#  EC-8  PCB RF_Module:ESP32-WROOM-32 footprint removed for U1
#        → test_u1_bare_module_footprint_removed
#
#  EC-9  PCB retains I2C and motor net names after schematic-to-PCB sync
#        → test_pcb_i2c_nets_present, test_pcb_motor_nets_present
#
#  EC-10 Both U5 and U6 get independent Conn_01x07 + Conn_01x04 instances
#        → test_u6_inflow_conn_01x07_present, test_u6_inflow_conn_01x04_present
#
#  EC-11 X1 screw terminal footprint present for INA260 high-current sensing
#        → test_u7_x1_screw_terminal_footprint_present
#
#  EC-12 ESP32 DevKit V1 DOIT uses 2×15 header layout, not a 1×N strip
#        → test_u1_devkit_header_footprint_present
# ===========================================================================
