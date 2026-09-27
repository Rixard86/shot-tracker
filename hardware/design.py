"""
design.py - ShotPuck circuit, single source of truth.

gen_sch.py turns this into shotpuck.kicad_sch and gen_pcb.py into
shotpuck.kicad_pcb. Change the circuit here, then re-run both.

Power path
  J1 Samzo PR5L4015 magnetic receptacle (VIN_RAW) -> D1 BAT54J (reverse polarity) -> VIN
  VIN -> U3 MCP73831-2 (4.20 V, 21 mA via R1 = 47k) -> VBAT (LIR1254, 45-65 mAh)
  VBAT -> U1 VDDH (nRF52840 high-voltage mode, REG0 -> VDD 3.0 V)
  VDD  -> U2 LSM6DSO32 (VDD + VDDIO), LED, SWD VCC sense
IMU
  U2 LSM6DSO32 on I2C at 0x6A (SA0 = GND, CS = VDD selects I2C). Aux master
  pins SDx/SCx to GND; INT2, OCS_Aux, SDO_Aux unconnected.
Charge inhibit (temperature, firmware)
  PROG -> R1 -> Q1A drain; Q1A gate pulled to VIN by R2 (charging allowed
  whenever a cable is present). Q1B (gate = CHG_INH, R3 pull-down) pulls
  Q1A gate low -> PROG floats -> MCP73831 charge disabled.
Cell protection (PCM, required by VARTA for CoinPower cells)
  U4 BQ29700 + Q2 DMN2004DWK in the cell's negative lead: BAT_N -> Q2A (DOUT,
  discharge) -> PCM_D -> Q2B (COUT, charge) -> GND. OVP 4.275 V, UVP 2.80 V,
  OCD/OCC +-100 mV across ~1 ohm of FETs (~0.1 A), SCD 0.5 V. R7 330R + C8
  100n filter the BAT supply, R8 2k2 on V-. Standby 4 uA.
Current measurement
  JP1 (bridged solder jumper) in series with the cell: cut, insert an
  ammeter, re-bridge with solder.
Charge status
  STAT -> D2 (cathode) ; D2 anode -> CHG_STAT GPIO with pull-up enabled only
  while VBUS is present (no standby leakage).
"""

# (ref, lib_id, footprint, value, {pin: net}, extra)
PARTS = [
    ("U1", "RF_Module:MDBT50Q-1MV2", "RF_Module:Raytac_MDBT50Q", "MDBT50Q-1MV2",
     {"1": "GND", "2": "GND", "15": "GND", "33": "GND", "55": "GND",
      "28": "VDD", "30": "VBAT", "32": "VIN",
      "20": "SDA", "22": "SCL", "24": "ACC_INT1", "26": "CHG_STAT", "16": "CHG_INH",
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
    ("R1", "Device:R_Small", "Resistor_SMD:R_0402_1005Metric", "47k 1%",
     {"1": "PROG", "2": "PROG_SW"}, {}),
    ("R2", "Device:R_Small", "Resistor_SMD:R_0402_1005Metric", "1M",
     {"1": "VIN", "2": "Q1_GATE"}, {}),
    ("R3", "Device:R_Small", "Resistor_SMD:R_0402_1005Metric", "100k",
     {"1": "CHG_INH", "2": "GND"}, {}),
    ("R4", "Device:R_Small", "Resistor_SMD:R_0402_1005Metric", "1k",
     {"1": "LED_IO", "2": "LED_A"}, {}),
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
    ("BT1", "Device:Battery_Cell", "ShotPuck:LIR1254_Contacts", "LIR1254 (plain, spring contacts)",
     {"1": "BAT_P", "2": "BAT_N"}, {}),
    ("U4", "Battery_Management:BQ297xy", "Package_SON:WSON-6_1.5x1.5mm_P0.5mm", "BQ29700DSER",
     {"2": "PCM_COUT", "3": "PCM_DOUT", "4": "BAT_N", "5": "PCM_BAT", "6": "PCM_VM"}, {}),
    ("Q2", "Transistor_FET:Q_Dual_NMOS_S1G1D2S2G2D1", "Package_TO_SOT_SMD:SOT-363_SC-70-6", "DMN2004DWK",
     {"1": "BAT_N", "2": "PCM_DOUT", "6": "PCM_D", "4": "GND", "5": "PCM_COUT", "3": "PCM_D"},
     {"units": 2}),
    ("R7", "Device:R_Small", "Resistor_SMD:R_0402_1005Metric", "330R",
     {"1": "VBAT", "2": "PCM_BAT"}, {}),
    ("R8", "Device:R_Small", "Resistor_SMD:R_0402_1005Metric", "2.2k",
     {"1": "PCM_VM", "2": "GND"}, {}),
    ("C8", "Device:C_Small", "Capacitor_SMD:C_0402_1005Metric", "100n",
     {"1": "PCM_BAT", "2": "BAT_N"}, {}),
    ("JP1", "Jumper:SolderJumper_2_Bridged", "Jumper:SolderJumper-2_P1.3mm_Bridged_Pad1.0x1.5mm",
     "I_MEAS (cut to measure)", {"1": "BAT_P", "2": "VBAT"}, {}),
    ("J1", "Connector_Generic:Conn_01x02", "ShotPuck:MagPogo_Samzo_2P", "Samzo PR5L4015-2P-C-F",
     {"1": "VIN_RAW", "2": "GND"}, {}),
    ("J2", "Connector_Generic:Conn_01x04", "ShotPuck:SWD_Pads_1x4_P2.54", "SWD pads",
     {"1": "VDD", "2": "SWDIO", "3": "SWDCLK", "4": "GND"}, {}),
    ("H1", "Mechanical:MountingHole", "ShotPuck:MountingHole_2.4mm_NPTH", "M2", {}, {}),
    ("H2", "Mechanical:MountingHole", "ShotPuck:MountingHole_2.4mm_NPTH", "M2", {}, {}),
    ("H3", "Mechanical:MountingHole", "ShotPuck:MountingHole_2.4mm_NPTH", "M2", {}, {}),
]

POWER_FLAG_NETS = ["GND", "VDD", "VIN", "VIN_RAW", "BAT_N"]

# Nets that carry charge current / supply: wider tracks on the PCB
POWER_NETS = {"BAT_P": 0.4, "BAT_N": 0.4, "PCM_D": 0.4, "VIN_RAW": 0.4, "VIN": 0.4, "VBAT": 0.4,
              "GND": 0.4, "VDD": 0.3}


def nets():
    out = {}
    for ref, _, _, _, pins, _ in PARTS:
        for p, n in pins.items():
            out.setdefault(n, []).append((ref, p))
    return out
