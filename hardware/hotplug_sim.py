import itertools
import sys
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass

import numpy as np
from scipy.integrate import solve_ivp
from scipy.special import wrightomega

MICRO = 1e-6
NANO = 1e-9
MILLI = 1e-3

D1_IF_LOW = 1 * MILLI
D1_IF_MID = 10 * MILLI
D1_IF_HIGH = 100 * MILLI
D1_VF_AT_LOW = 0.320
D1_VF_AT_MID = 0.400
D1_VF_AT_HIGH = 0.800
D1_N_VT = (D1_VF_AT_MID - D1_VF_AT_LOW) / np.log(D1_IF_MID / D1_IF_LOW)
D1_LOG_IS = np.log(D1_IF_LOW) - D1_VF_AT_LOW / D1_N_VT
D1_RS_DATASHEET_MAX = (D1_VF_AT_HIGH - D1_N_VT * (np.log(D1_IF_HIGH) - D1_LOG_IS)) / D1_IF_HIGH
D1_RS_STRONG = 0.5
D1_RS_TYPICAL = 1.2
D1_IFSM = 0.6
D1_IFSM_PULSE = 10 * MILLI
D1_IFSM_I2T = D1_IFSM ** 2 * D1_IFSM_PULSE

TVS_TEST_CURRENT = 10 * MILLI
TVS_VBR_MIN = 6.40
TVS_VBR_MAX = 7.07
TVS_CLAMP_VOLTAGE = 9.2
TVS_PEAK_PULSE_CURRENT = 21.7
TVS_KNEE_VOLTS_PER_E = 0.1
TVS_DYNAMIC_RESISTANCE = (TVS_CLAMP_VOLTAGE - TVS_VBR_MAX - TVS_KNEE_VOLTS_PER_E
                          * np.log(TVS_PEAK_PULSE_CURRENT / TVS_TEST_CURRENT)) / TVS_PEAK_PULSE_CURRENT
TVS_CAPACITANCE = 1 * NANO

R9_OHMS = 1000.0
C9_FARADS = 100 * NANO
C1_NOMINAL = 4.7 * MICRO
C1_DERATED = 2.0 * MICRO
CHARGER_IDLE_CURRENT = 50 * MICRO
NRF_VBUS_CURRENT = 24 * MICRO
NOMINAL_VOLTS = 5.0
CHARGER_LOAD_OHMS = NOMINAL_VOLTS / CHARGER_IDLE_CURRENT
NRF_VBUS_LOAD_OHMS = NOMINAL_VOLTS / NRF_VBUS_CURRENT
TYPICAL_CABLE_HENRY = 1 * MICRO
TYPICAL_CABLE_OHMS = 0.2

SUPPLY_SWEEP = [5.0, 5.25, 5.5]
CABLE_HENRY_SWEEP = [0.5 * MICRO, 1 * MICRO, 2 * MICRO]
CABLE_OHMS_SWEEP = [0.05, 0.2, 0.5]
C1_SWEEP = [C1_DERATED, C1_NOMINAL]
D1_RS_SWEEP = [D1_RS_STRONG, D1_RS_TYPICAL, D1_RS_DATASHEET_MAX]
TVS_VBR_SWEEP = [TVS_VBR_MIN, TVS_VBR_MAX]

MCP73831_VDD_ABS_MAX = 7.0
NRF_VBUS_ABS_MAX = 5.8
SIM_END = 2 * MILLI
MAX_STEP = 0.05 * MICRO
REL_TOL = 1e-7
ABS_TOL = 1e-10
WORST_ROWS_SHOWN = 12
REV_0A_FLAG = "--rev-0a"


@dataclass(frozen=True)
class Case:
    supply_volts: float = NOMINAL_VOLTS
    cable_henry: float = TYPICAL_CABLE_HENRY
    cable_ohms: float = TYPICAL_CABLE_OHMS
    c1_farads: float = C1_NOMINAL
    d1_series_ohms: float = D1_RS_TYPICAL
    tvs_vbr: float = TVS_VBR_MAX
    r9_from_raw: bool = True


def omega_current(volts, branch):
    knee, log_i0, series = branch
    i0 = np.exp(log_i0)
    arg = np.log(i0 * series / knee) + (volts + i0 * series) / knee
    return knee / series * np.real(wrightomega(arg)) - i0


def d1_current(volts, series_ohms):
    return omega_current(volts, (D1_N_VT, D1_LOG_IS, series_ohms))


def tvs_current(volts, vbr):
    log_i0 = np.log(TVS_TEST_CURRENT) - vbr / TVS_KNEE_VOLTS_PER_E
    branch = (TVS_KNEE_VOLTS_PER_E, log_i0, TVS_DYNAMIC_RESISTANCE)
    return np.sign(volts) * omega_current(np.abs(volts), branch)


def make_rhs(case):
    def rhs(_t, y):
        i_cable, v_raw, v_in, v_sns = y
        i_d1 = d1_current(v_raw - v_in, case.d1_series_ohms)
        i_r9 = ((v_raw if case.r9_from_raw else v_in) - v_sns) / R9_OHMS
        i_r9_raw, i_r9_in = (i_r9, 0.0) if case.r9_from_raw else (0.0, i_r9)
        di = (case.supply_volts - case.cable_ohms * i_cable - v_raw) / case.cable_henry
        dv_raw = (i_cable - tvs_current(v_raw, case.tvs_vbr) - i_d1 - i_r9_raw) / TVS_CAPACITANCE
        dv_in = (i_d1 - v_in / CHARGER_LOAD_OHMS - i_r9_in) / case.c1_farads
        dv_sns = (i_r9 - v_sns / NRF_VBUS_LOAD_OHMS) / C9_FARADS
        return [di, dv_raw, dv_in, dv_sns]
    return rhs


def simulate(case):
    sol = solve_ivp(make_rhs(case), (0, SIM_END), [0.0, 0.0, 0.0, 0.0], method="LSODA",
                    rtol=REL_TOL, atol=ABS_TOL, max_step=MAX_STEP)
    _i_cable, v_raw, v_in, v_sns = sol.y
    return {"t": sol.t, "v_raw": v_raw, "v_in": v_in, "v_sns": v_sns,
            "i_d1": d1_current(v_raw - v_in, case.d1_series_ohms),
            "i_tvs": tvs_current(v_raw, case.tvs_vbr), "ok": sol.success}


def summarize(case, wave):
    i_d1_forward = np.clip(wave["i_d1"], 0, None)
    return {"vraw_max": wave["v_raw"].max(), "vin_max": wave["v_in"].max(),
            "vsns_max": wave["v_sns"].max(), "id1_max": wave["i_d1"].max(),
            "itvs_max": wave["i_tvs"].max(), "ok": wave["ok"],
            "i2t_d1": np.trapezoid(i_d1_forward ** 2, wave["t"])}


def run_case(case):
    return case, summarize(case, simulate(case))


def sweep_cases(r9_from_raw):
    grid = itertools.product(SUPPLY_SWEEP, CABLE_HENRY_SWEEP, CABLE_OHMS_SWEEP,
                             C1_SWEEP, D1_RS_SWEEP, TVS_VBR_SWEEP)
    return [Case(*values, r9_from_raw) for values in grid]


def row_text(case, result):
    return (f"{case.supply_volts:5.2f} {case.cable_henry / MICRO:4.1f} {case.cable_ohms:5.2f} "
            f"{case.c1_farads / MICRO:4.1f} {case.d1_series_ohms:4.1f} {case.tvs_vbr:5.2f} | "
            f"{result['vraw_max']:5.2f} {result['vin_max']:5.2f} {result['vsns_max']:5.2f} "
            f"{result['id1_max']:5.2f} {result['itvs_max'] / MILLI:7.1f} "
            f"{result['i2t_d1'] / MICRO:6.1f}")


HEADER = ("  Vs    L     R     C1  Rs_D1  VBR  | VRAWpk VINpk VSNSpk ID1pk ITVSpk  I2T_D1\n"
          "  V     uH   ohm   uF   ohm    V   |   V      V     V      A     mA   uA2s")


def print_table(title, rows):
    print(f"\n{title}\n{HEADER}")
    for case, result in rows:
        print(row_text(case, result))


def run_sweep(args):
    r9_from_raw = REV_0A_FLAG not in args
    print(f"R9 fed from {'VIN_RAW (rev 0B)' if r9_from_raw else 'VIN (rev 0A)'}")
    with ProcessPoolExecutor() as pool:
        rows = list(pool.map(run_case, sweep_cases(r9_from_raw)))
    failed = [case for case, result in rows if not result["ok"]]
    by_sns = sorted(rows, key=lambda row: row[1]["vsns_max"], reverse=True)
    print(f"D1 fit: nVt {D1_N_VT / MILLI:.1f} mV, Rs(datasheet max) {D1_RS_DATASHEET_MAX:.2f} ohm; "
          f"TVS Rd {TVS_DYNAMIC_RESISTANCE / MILLI:.0f} mohm; solver failures: {len(failed)}")
    print_table(f"Worst {WORST_ROWS_SHOWN} by VBUS_SNS peak", by_sns[:WORST_ROWS_SHOWN])
    print_table("Typical (5.0 V, 1 uH, 0.2 ohm, 4.7 uF nominal, Rs 1.2)",
                [row for row in rows if row[0] == Case(r9_from_raw=r9_from_raw)])
    over_sns = sum(result["vsns_max"] > NRF_VBUS_ABS_MAX for _c, result in rows)
    over_vin = sum(result["vin_max"] > MCP73831_VDD_ABS_MAX for _c, result in rows)
    print(f"\n{len(rows)} cases: VBUS_SNS > {NRF_VBUS_ABS_MAX} V in {over_sns}, "
          f"VIN > {MCP73831_VDD_ABS_MAX} V in {over_vin}")
    worst_i2t = max(result["i2t_d1"] for _c, result in rows)
    print(f"D1 pulse: worst I2t {worst_i2t / MICRO:.1f} uA2s against {D1_IFSM_I2T / MICRO:.0f} uA2s "
          f"for the {D1_IFSM} A / {D1_IFSM_PULSE / MILLI:.0f} ms IFSM rating")
    print_breakdown(rows)


def print_breakdown(rows):
    print(f"\nShare of cases with VBUS_SNS > {NRF_VBUS_ABS_MAX} V, by parameter:")
    for field in Case.__dataclass_fields__:
        parts = []
        for value in sorted({getattr(case, field) for case, _r in rows}):
            subset = [result for case, result in rows if getattr(case, field) == value]
            over = sum(result["vsns_max"] > NRF_VBUS_ABS_MAX for result in subset)
            peak = max(result["vsns_max"] for result in subset)
            parts.append(f"{value:.3g}: {over}/{len(subset)} (max {peak:.2f})")
        print(f"  {field:15s} " + "   ".join(parts))


if __name__ == "__main__":
    run_sweep(sys.argv[1:])
