import os

import pcbnew

import sexp
from sexp import Str

NAME = "BMD-340"
FOOTPRINT = "u-blox_BMD-340"
DATASHEET = "https://content.u-blox.com/sites/default/files/BMD-340_DataSheet_UBX-19033353.pdf"
BODY_LENGTH_MM = 15.0
BODY_WIDTH_MM = 10.2
BODY_HEIGHT_MM = 1.9
ANTENNA_KEEPOUT_MM = 4.4
PAD_MM = 0.5
PITCH_MM = 1.1
COURTYARD_MARGIN_MM = 0.3
FAB_LINE_MM = 0.1
SILK_LINE_MM = 0.12
COURTYARD_LINE_MM = 0.05
PIN1_MARK_MM = 0.6
MODEL_PATH = "${KIPRJMOD}/ShotPuck.3dshapes/u-blox_BMD-340.step"
MODEL_ROTATION_DEG = -90.0
MODEL_OFFSET_MM = (BODY_LENGTH_MM / 2, -BODY_WIDTH_MM / 2, 0.0)

PAD_RUNS = (
    (1, 2, 8, (-2.85, 4.40), (PITCH_MM, 0.0)),
    (2, 2, 7, (-2.30, 3.50), (PITCH_MM, 0.0)),
    (16, 1, 1, (5.95, 4.40), (0.0, 0.0)),
    (17, 2, 7, (5.95, 3.30), (0.0, -PITCH_MM)),
    (18, 2, 6, (5.05, 2.75), (0.0, -PITCH_MM)),
    (30, 1, 1, (5.95, -4.40), (0.0, 0.0)),
    (31, 2, 8, (4.85, -4.40), (-PITCH_MM, 0.0)),
    (32, 2, 7, (4.30, -3.50), (-PITCH_MM, 0.0)),
    (46, 1, 2, (-2.85, -1.65), (0.0, 3.30)),
    (48, 1, 5, (-1.75, 2.60), (PITCH_MM, 0.0)),
    (53, 1, 3, (4.15, 1.10), (0.0, -PITCH_MM)),
    (56, 1, 5, (2.65, -2.60), (-PITCH_MM, 0.0)),
    (61, 1, 8, (6.85, 3.85), (0.0, -PITCH_MM)),
)

GND_PINS = (1, 2, 3, 4, 5, 16, 18, 29, 30, 45, 46, 47, 55)
POWER_PINS = {65: "VCCH", 17: "VCC", 66: "VBUS"}
OTHER_PINS = {68: "USB-D+", 67: "USB-D-", 44: "SWDIO", 43: "SWCLK"}
GPIO_PINS = {
    13: "P0.00", 14: "P0.01", 15: "P0.02", 19: "P0.03", 20: "P0.04", 21: "P0.05", 22: "P0.06",
    23: "P0.07", 24: "P0.08", 25: "P0.09", 26: "P0.10", 27: "P0.11", 28: "P0.12", 31: "P0.13",
    32: "P0.14", 33: "P0.15", 34: "P0.16", 35: "P0.17", 39: "P0.18", 37: "P0.19", 38: "P0.20",
    36: "P0.21", 40: "P0.22", 41: "P0.23", 42: "P0.24", 6: "P0.25", 7: "P0.26", 8: "P0.27",
    9: "P0.28", 10: "P0.29", 11: "P0.30", 12: "P0.31",
    56: "P1.00", 57: "P1.01", 58: "P1.02", 59: "P1.03", 60: "P1.04", 48: "P1.05", 49: "P1.06",
    50: "P1.07", 51: "P1.08", 52: "P1.09", 53: "P1.10", 54: "P1.11", 61: "P1.12", 62: "P1.13",
    63: "P1.14", 64: "P1.15",
}
PIN_TYPES = {"VCCH": "power_in", "VCC": "power_in", "VBUS": "power_in", "SWCLK": "input"}

SYMBOL_PIN_PITCH = 2.54
SYMBOL_PIN_LENGTH = 2.54
SYMBOL_HALF_WIDTH = 17.78
SYMBOL_HALF_HEIGHT = 43.18
SYMBOL_FONT = 1.27
SYMBOL_LINE = 0.254
LEFT_SIDE_GAP_ROWS = 1
KICAD_SYMBOL_VERSION = 20251024


def pad_positions():
    positions = {}
    for first, step, count, (x0, y0), (dx, dy) in PAD_RUNS:
        for k in range(count):
            positions[first + k * step] = (round(x0 + k * dx, 3), round(y0 + k * dy, 3))
    return positions


def _fp_line(fp, spec):
    layer, (x1, y1), (x2, y2), width = spec
    shape = pcbnew.PCB_SHAPE(fp, pcbnew.SHAPE_T_SEGMENT)
    shape.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(x1), pcbnew.FromMM(y1)))
    shape.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(x2), pcbnew.FromMM(y2)))
    shape.SetLayer(layer)
    shape.SetWidth(pcbnew.FromMM(width))
    fp.Add(shape)


def _fp_rect(fp, spec):
    layer, half_x, half_y, width = spec
    corners = [(-half_x, -half_y), (half_x, -half_y), (half_x, half_y), (-half_x, half_y)]
    for a, b in zip(corners, corners[1:] + corners[:1]):
        _fp_line(fp, (layer, a, b, width))


def _add_pads(fp):
    size = pcbnew.VECTOR2I(pcbnew.FromMM(PAD_MM), pcbnew.FromMM(PAD_MM))
    for number, (x, y) in sorted(pad_positions().items()):
        pad = pcbnew.PAD(fp)
        pad.SetNumber(str(number))
        pad.SetShape(pcbnew.PAD_SHAPE_RECT)
        pad.SetSize(size)
        pad.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
        pad.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        pad.SetLayerSet(pad.SMDMask())
        fp.Add(pad)


def _add_graphics(fp):
    half_x, half_y = BODY_LENGTH_MM / 2, BODY_WIDTH_MM / 2
    _fp_rect(fp, (pcbnew.F_Fab, half_x, half_y, FAB_LINE_MM))
    _fp_rect(fp, (pcbnew.F_CrtYd, half_x + COURTYARD_MARGIN_MM, half_y + COURTYARD_MARGIN_MM,
                  COURTYARD_LINE_MM))
    _fp_rect(fp, (pcbnew.F_SilkS, half_x, half_y, SILK_LINE_MM))
    antenna_edge = -half_x + ANTENNA_KEEPOUT_MM
    _fp_line(fp, (pcbnew.F_Fab, (antenna_edge, -half_y), (antenna_edge, half_y), FAB_LINE_MM))
    pin1_x = pad_positions()[1][0]
    _fp_line(fp, (pcbnew.F_SilkS, (pin1_x, half_y), (pin1_x, half_y + PIN1_MARK_MM), SILK_LINE_MM))


def save_footprint(lib_dir):
    fp = pcbnew.FOOTPRINT(None)
    fp.SetFPID(pcbnew.LIB_ID("ShotPuck", FOOTPRINT))
    fp.SetLibDescription("u-blox BMD-340 nRF52840 module, LGA-68, 15.0 x 10.2 x 1.9 mm (UBX-19033353 "
                         "Figure 12). Antenna at -x: keep the last 4.4 mm of the module copper-free.")
    fp.Reference().SetLayer(pcbnew.F_Fab)
    fp.Value().SetLayer(pcbnew.F_Fab)
    _add_pads(fp)
    _add_graphics(fp)
    fp.Add3DModel(model())
    pcbnew.FootprintSave(lib_dir, fp)


def model():
    m = pcbnew.FP_3DMODEL()
    m.m_Filename = MODEL_PATH
    m.m_Offset = pcbnew.VECTOR3D(*MODEL_OFFSET_MM)
    m.m_Rotation = pcbnew.VECTOR3D(0.0, 0.0, MODEL_ROTATION_DEG)
    return m


def pin_names():
    names = {n: "GND" for n in GND_PINS}
    for table in (POWER_PINS, OTHER_PINS, GPIO_PINS):
        names.update(table)
    return names


def _font():
    return ["effects", ["font", ["size", SYMBOL_FONT, SYMBOL_FONT]]]


def _pin(number, spec):
    name, (x, y, angle), hidden = spec
    kind = "passive" if hidden else PIN_TYPES.get(name, "power_in" if name == "GND" else "bidirectional")
    pin = ["pin", kind, "line", ["at", x, y, angle], ["length", SYMBOL_PIN_LENGTH]]
    if hidden:
        pin.append(["hide", "yes"])
    return pin + [["name", Str(name), _font()], ["number", Str(str(number)), _font()]]


def _side_pins(numbers, side_x):
    angle = 0 if side_x < 0 else 180
    top = SYMBOL_HALF_HEIGHT - SYMBOL_PIN_PITCH
    names = pin_names()
    pins = []
    for row, number in enumerate(numbers):
        if number is not None:
            pins.append(_pin(number, (names[number], (side_x, top - row * SYMBOL_PIN_PITCH, angle), False)))
    return pins


def _symbol_pins():
    by_name = {v: k for k, v in GPIO_PINS.items()}
    port0 = [by_name[f"P0.{i:02d}"] for i in range(32)]
    port1 = [by_name[f"P1.{i:02d}"] for i in range(16)]
    left = [65, 17, 66, 68, 67, 44, 43] + [None] * LEFT_SIDE_GAP_ROWS + port1
    pin_x = SYMBOL_HALF_WIDTH + SYMBOL_PIN_LENGTH
    pins = _side_pins(left, -pin_x) + _side_pins(port0, pin_x)
    gnd_at = (0, -SYMBOL_HALF_HEIGHT - SYMBOL_PIN_LENGTH, 90)
    for k, number in enumerate(GND_PINS):
        pins.append(_pin(number, ("GND", gnd_at, k > 0)))
    return pins


def _property(key, spec):
    value, y, hidden = spec
    prop = ["property", Str(key), Str(value), ["at", 0, y, 0]]
    effects = _font()
    if hidden:
        effects.append(["hide", "yes"])
    return prop + [effects]


def symbol():
    body = ["rectangle", ["start", -SYMBOL_HALF_WIDTH, SYMBOL_HALF_HEIGHT],
            ["end", SYMBOL_HALF_WIDTH, -SYMBOL_HALF_HEIGHT],
            ["stroke", ["width", SYMBOL_LINE], ["type", "default"]], ["fill", ["type", "background"]]]
    label_y = SYMBOL_HALF_HEIGHT + SYMBOL_PIN_PITCH
    return ["symbol", Str(NAME), ["exclude_from_sim", "no"], ["in_bom", "yes"], ["on_board", "yes"],
            _property("Reference", ("U", label_y, False)),
            _property("Value", (NAME, -label_y, False)),
            _property("Footprint", ("ShotPuck:" + FOOTPRINT, 0, True)),
            _property("Datasheet", (DATASHEET, 0, True)),
            ["symbol", Str(NAME + "_0_1"), body],
            ["symbol", Str(NAME + "_1_1")] + _symbol_pins()]


def save_symbol(lib_dir):
    lib = ["kicad_symbol_lib", ["version", KICAD_SYMBOL_VERSION], ["generator", Str("shotpuck")], symbol()]
    path = os.path.join(lib_dir, "ShotPuck.kicad_sym")
    with open(path, "w") as f:
        f.write(sexp.dump(lib) + "\n")
    with open(os.path.join(lib_dir, "sym-lib-table"), "w") as f:
        f.write('(sym_lib_table\n  (lib (name "ShotPuck")(type "KiCad")(uri "${KIPRJMOD}/ShotPuck.kicad_sym")'
                '(options "")(descr "ShotPuck custom symbols"))\n)\n')
    return path
