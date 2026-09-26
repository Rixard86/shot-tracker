"""
design.py - ShotPuck circuit, single source of truth.

gen_sch.py turns this into shotpuck.kicad_sch and gen_pcb.py into
shotpuck.kicad_pcb. Change the circuit here, then re-run both.

Power path
  J1 magnetic pogo (VIN_RAW) -> D1 BAT54J (reverse polarity) -> VIN
  VIN -> U3 MCP73831-2 (4.20 V, 50 mA via R1 = 20k) -> VBAT (CP1654)
  VBAT -> U1 VDDH (nRF52833 high-voltage mode, REG0 -> VDD 3.0 V)
  VDD  -> U2 LSM6DSO32 (VDD + VDDIO), LED, SWD VCC sense
IMU
  U2 LSM6DSO32 on I2C at 0x6A (SA0 = GND, CS = VDD selects I2C). Aux master
  pins SDx/SCx to GND; INT2, OCS_Aux, SDO_Aux unconnected.
Charge inhibit (temperature, firmware)
  PROG -> R1 -> Q1A drain; Q1A gate pulled to VIN by R2 (charging allowed
  whenever a cable is present). Q1B (gate = CHG_INH, R3 pull-down) pulls
  Q1A gate low -> PROG floats -> MCP73831 charge disabled.
Current measurement
  JP1 (bridged solder jumper) in series with the cell: cut, insert an
  ammeter, re-bridge with solder.
Charge status
  STAT -> D2 (cathode) ; D2 anode -> CHG_STAT GPIO with pull-up enabled only
  while VBUS is present (no standby leakage).
"""

# (ref, lib_id, footprint, value, {pin: net}, extra)
PARTS = [
    ("U1", "RF_Module:MDBT50Q-512K", "RF_Module:Raytac_MDBT50Q", "MDBT50Q-512K",
     {"1": "GND", "2": "GND", "15": "GND", "33": "GND", "55": "GND",
      "28": "VDD", "30": "VBAT", "32": "VIN",
      "20": "SCL", "22": "SDA", "24": "ACC_INT1", "26": "CHG_STAT", "16": "CHG_INH",
      "37": "LED_IO", "51": "SWDIO", "53": "SWDCLK"}, {}),
    ("U2", "Sensor_Motion:LSM6DSL", "Package_LGA:LGA-14_3x2.5mm_P0.5mm_LayoutBorder3x4y",
     "LSM6DSO32TR",
     {"1": "GND", "2": "GND", "3": "GND", "4": "ACC_INT1", "5": "VDD", "6": "GND", "7": "GND",
      "8": "VDD", "12": "VDD", "13": "SCL", "14": "SDA"}, {}),
    ("U3", "Battery_Management:MCP73831-2-OT", "Package_TO_SOT_SMD:SOT-23-5", "MCP73831T-2ACI/OT",
     {"1": "STAT", "2": "GND", "3": "VBAT", "4": "VIN", "5": "PROG"}, {}),
    ("Q1", "Transistor_FET:Q_Dual_NMOS_S1G1D2S2G2D1", "Package_TO_SOT_SMD:SOT-363_SC-70-6", "DMN63D8LDW",
     {"1": "GND", "2": "Q1_GATE", "6": "PROG_SW", "4": "GND", "5": "CHG_INH", "3": "Q1_GATE"},
     {"units": 2}),
    ("R1", "Device:R_Small", "Resistor_SMD:R_0402_1005Metric", "20k 1%",
     {"1": "PROG", "2": "PROG_SW"}, {}),
    ("R2", "Device:R_Small", "Resistor_SMD:R_0402_1005Metric", "1M",
     {"1": "VIN", "2": "Q1_GATE"}, {}),
    ("R3", "Device:R_Small", "Resistor_SMD:R_0402_1005Metric", "100k",
     {"1": "CHG_INH", "2": "GND"}, {}),
    ("R4", "Device:R_Small", "Resistor_SMD:R_0402_1005Metric", "1k",
     {"1": "LED_IO", "2": "LED_A"}, {}),
    ("R5", "Device:R_Small", "Resistor_SMD:R_0402_1005Metric", "4.7k DNP",
     {"1": "VDD", "2": "SCL"}, {"dnp": True}),
    ("R6", "Device:R_Small", "Resistor_SMD:R_0402_1005Metric", "4.7k DNP",
     {"1": "VDD", "2": "SDA"}, {"dnp": True}),
    ("D1", "Diode:BAT54J", "Diode_SMD:D_SOD-323F", "BAT54J",
     {"1": "VIN", "2": "VIN_RAW"}, {}),
    ("D2", "Diode:BAT54J", "Diode_SMD:D_SOD-323F", "BAT54J",
     {"1": "STAT", "2": "CHG_STAT"}, {}),
    ("D3", "Device:D_TVS", "Diode_SMD:D_SOD-523", "PESD5V0S1BB",
     {"1": "GND", "2": "VIN_RAW"}, {}),
    ("D4", "Device:LED", "LED_SMD:LED_0402_1005Metric", "LED green 0402",
     {"1": "GND", "2": "LED_A"}, {}),
    ("C1", "Device:C_Small", "Capacitor_SMD:C_0603_1608Metric", "4.7u 10V",
     {"1": "VIN", "2": "GND"}, {}),
    ("C2", "Device:C_Small", "Capacitor_SMD:C_0603_1608Metric", "4.7u 10V",
     {"1": "VBAT", "2": "GND"}, {}),
    ("C3", "Device:C_Small", "Capacitor_SMD:C_0402_1005Metric", "4.7u 6.3V",
     {"1": "VBAT", "2": "GND"}, {}),
    ("C4", "Device:C_Small", "Capacitor_SMD:C_0402_1005Metric", "4.7u 6.3V",
     {"1": "VDD", "2": "GND"}, {}),
    ("C5", "Device:C_Small", "Capacitor_SMD:C_0402_1005Metric", "100n",
     {"1": "VDD", "2": "GND"}, {}),
    ("C6", "Device:C_Small", "Capacitor_SMD:C_0402_1005Metric", "100n",
     {"1": "VDD", "2": "GND"}, {}),
    ("C7", "Device:C_Small", "Capacitor_SMD:C_0402_1005Metric", "100n",
     {"1": "VDD", "2": "GND"}, {}),
    ("BT1", "Device:Battery_Cell", "ShotPuck:CP1654_Tabbed", "Varta CP1654 A3 (tabbed)",
     {"1": "BAT_P", "2": "GND"}, {}),
    ("JP1", "Jumper:SolderJumper_2_Bridged", "Jumper:SolderJumper-2_P1.3mm_Bridged_Pad1.0x1.5mm",
     "I_MEAS (cut to measure)", {"1": "BAT_P", "2": "VBAT"}, {}),
    ("J1", "Connector_Generic:Conn_01x02", "ShotPuck:MagPogo_2P", "Magnetic pogo 2P (female)",
     {"1": "VIN_RAW", "2": "GND"}, {}),
    ("J2", "Connector:Conn_ARM_SWD_TagConnect_TC2030-NL",
     "Connector:Tag-Connect_TC2030-IDC-NL_2x03_P1.27mm_Vertical", "TC2030-NL",
     {"1": "VDD", "2": "SWDIO", "4": "SWDCLK", "5": "GND"}, {}),
    ("H1", "Mechanical:MountingHole", "ShotPuck:MountingHole_1.8mm_NPTH", "M1.6", {}, {}),
    ("H2", "Mechanical:MountingHole", "ShotPuck:MountingHole_1.8mm_NPTH", "M1.6", {}, {}),
    ("H3", "Mechanical:MountingHole", "ShotPuck:MountingHole_1.8mm_NPTH", "M1.6", {}, {}),
]

POWER_FLAG_NETS = ["GND", "VDD", "VIN", "VIN_RAW"]

# Nets that carry charge current / supply: wider tracks on the PCB
POWER_NETS = {"BAT_P": 0.4, "VIN_RAW": 0.4, "VIN": 0.4, "VBAT": 0.4, "GND": 0.4, "VDD": 0.3}


def nets():
    out = {}
    for ref, _, _, _, pins, _ in PARTS:
        for p, n in pins.items():
            out.setdefault(n, []).append((ref, p))
    return out
