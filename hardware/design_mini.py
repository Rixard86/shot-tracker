"""
design_mini.py - ShotPuck mini: square board for a Picatinny-rail case, single source of truth.

gen_sch.py design_mini turns this into shotpuck-mini.kicad_sch and gen_mini_pcb.py
into the unrouted shotpuck-mini.kicad_pcb (placed and routed by hand).

Power
  BT1 CR2450 under a MYOUNG MY-2450-03 + clip on the back (hand-soldered), - face on a board pad
  -> BAT_P -> JP1 (bridged, cut to measure) -> VDD
  U1 BMD-340 in normal voltage mode: VCC and VCCH both on VDD; VBUS to GND (USB unused)
  C1 47u on VDD carries the radio's current peaks on an aging coin cell
IMU
  U2 LSM6DSV32X on I2C at 0x6A, on the same module pins as the puck
Service
  J2 SWD pads (bottom), JP2 reset pads (BTN), D4 status LED on LED_IO
"""

PROJECT = "shotpuck-mini"
SCH_TITLE = "ShotPuck mini - square board, CR2450"

PARTS = [
    ("U1", "ShotPuck:BMD-340", "ShotPuck:u-blox_BMD-340", "BMD-340-A-R",
     {"1": "GND", "2": "GND", "3": "GND", "4": "GND", "5": "GND", "16": "GND", "18": "GND",
      "29": "GND", "30": "GND", "45": "GND", "46": "GND", "47": "GND", "55": "GND",
      "17": "VDD", "65": "VDD", "66": "GND",
      "21": "SDA", "23": "SCL", "19": "ACC_INT1",
      "31": "LED_IO", "7": "BTN", "44": "SWDIO", "43": "SWDCLK"}, {}),
    ("U2", "Sensor_Motion:LSM6DSL", "Package_LGA:LGA-14_3x2.5mm_P0.5mm_LayoutBorder3x4y",
     "LSM6DSV32XTR",
     {"1": "GND", "2": "GND", "3": "GND", "4": "ACC_INT1", "5": "VDD", "6": "GND", "7": "GND",
      "8": "VDD", "12": "VDD", "13": "SCL", "14": "SDA"}, {}),
    ("C1", "Device:C_Small", "Capacitor_SMD:C_0805_2012Metric", "47u 6.3V",
     {"1": "VDD", "2": "GND"}, {}),
    ("C4", "Device:C_Small", "Capacitor_SMD:C_0402_1005Metric", "4.7u 6.3V",
     {"1": "VDD", "2": "GND"}, {}),
    ("C6", "Device:C_Small", "Capacitor_SMD:C_0402_1005Metric", "100n",
     {"1": "VDD", "2": "GND"}, {}),
    ("C7", "Device:C_Small", "Capacitor_SMD:C_0402_1005Metric", "100n",
     {"1": "VDD", "2": "GND"}, {}),
    ("R4", "Device:R_Small", "Resistor_SMD:R_0402_1005Metric", "1k",
     {"1": "LED_IO", "2": "LED_A"}, {}),
    ("D4", "Device:LED", "LED_SMD:LED_0402_1005Metric", "LED yellow-green 0402",
     {"1": "GND", "2": "LED_A"}, {}),
    ("BT1", "Device:Battery_Cell", "ShotPuck:BatteryHolder_MYOUNG_MY-2450-03_CR2450", "MY-2450-03 (CR2450)",
     {"1": "BAT_P", "2": "GND"}, {}),
    ("JP1", "Jumper:SolderJumper_2_Bridged", "Jumper:SolderJumper-2_P1.3mm_Bridged_Pad1.0x1.5mm",
     "I_MEAS (cut to measure)", {"1": "BAT_P", "2": "VDD"}, {}),
    ("J2", "Connector_Generic:Conn_01x04", "ShotPuck:SWD_Pads_1x4_P2.54", "SWD pads",
     {"1": "VDD", "2": "SWDIO", "3": "SWDCLK", "4": "GND"}, {}),
    ("JP2", "Jumper:SolderJumper_2_Open", "ShotPuck:TweezerPads_2x0.8mm_Gap0.4mm", "RESET pads",
     {"1": "BTN", "2": "GND"}, {}),
    ("H1", "Mechanical:MountingHole", "ShotPuck:MountingHole_2.4mm_NPTH", "M2", {}, {}),
    ("H2", "Mechanical:MountingHole", "ShotPuck:MountingHole_2.4mm_NPTH", "M2", {}, {}),
    ("H3", "Mechanical:MountingHole", "ShotPuck:MountingHole_2.4mm_NPTH", "M2", {}, {}),
]

SCH_GROUPS = [
    ("MCU / radio (nRF52840 normal voltage mode)", ["U1", "C1", "C4"], 22.86, 35.56, 190.5),
    ("Programming & status LED", ["J2", "R4", "D4", "JP2"], 22.86, 170.18, 190.5),
    ("Battery (CR2450, back side)", ["BT1", "JP1"], 203.2, 35.56, 405.0),
    ("IMU (accelerometer + gyroscope)", ["U2", "C6", "C7"], 203.2, 81.28, 405.0),
    ("Mechanical", ["H1", "H2", "H3"], 203.2, 124.46, 405.0),
]

POWER_FLAG_NETS = ["GND", "VDD"]
