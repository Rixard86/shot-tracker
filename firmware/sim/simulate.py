#!/usr/bin/env python3
"""
simulate.py - Monte Carlo validation of firmware/src/shot_detect.c.

Builds libsd (shot_detect.c + sim_runner.c), generates synthetic archery
sessions at 3.3 kHz ground truth, runs them through the same IDLE/ACTIVE
pipeline and sensor model as the firmware, and scores detections. Also
builds and runs the unit tests of the other portable firmware modules
(test_capture.c, test_evlog.c).

    python simulate.py                 # default config, 60 sessions
    python simulate.py --sessions 200 --seed 7
    python simulate.py --stress        # also run weak-bow / hard-case sets

Synthetic signals are a model, not a measurement. They validate the logic
and its robustness margins; thresholds must be re-tuned on real captures
(see docs/VALIDATION.md).
"""
import argparse
import ctypes as C
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src")
FS = 3328.0
IDLE_HZ = 12.5
WAKE_LATENCY_MS = 5
CC = os.environ.get("CC", "gcc")
CFLAGS = ["-std=c99", "-O2", "-Wall", "-Wextra", "-Werror"]
G = 1000.0   # mg

# --------------------------------------------------------------------------
# C bindings
# --------------------------------------------------------------------------
class SdConfig(C.Structure):
    _fields_ = [("fs_hz", C.c_float), ("trig_hf_mg", C.c_float), ("move_mg", C.c_float),
                ("pre_still_min_ms", C.c_uint16), ("post_start_ms", C.c_uint16),
                ("post_end_ms", C.c_uint16), ("post_lf_min_mg", C.c_float),
                ("refractory_ms", C.c_uint16), ("wake_mg", C.c_float),
                ("idle_timeout_ms", C.c_uint32)]


class SdEvent(C.Structure):
    _fields_ = [("t_ms", C.c_uint32), ("peak_hf_mg", C.c_uint16), ("pre_still_ms", C.c_uint16),
                ("post_lf_mg", C.c_uint16), ("accepted", C.c_uint8), ("reason", C.c_uint8)]


class SimStats(C.Structure):
    _fields_ = [("active_ms", C.c_uint32), ("wakes", C.c_uint32), ("n_events", C.c_uint32)]


def build_lib():
    windows = sys.platform == "win32"
    so = os.path.join(HERE, "libsd.dll" if windows else "libsd.so")
    srcs = [os.path.join(SRC, "shot_detect.c"), os.path.join(HERE, "sim_runner.c")]
    if not os.path.exists(so) or any(os.path.getmtime(s) > os.path.getmtime(so) for s in srcs):
        pic = [] if windows else ["-fPIC"]
        subprocess.check_call([CC] + CFLAGS + ["-shared"] + pic + ["-o", so] + srcs + ["-lm"])
    lib = C.CDLL(so)
    lib.sd_default_config.argtypes = [C.POINTER(SdConfig)]
    fp = np.ctypeslib.ndpointer(dtype=np.float32, flags="C_CONTIGUOUS")
    lib.sim_run.argtypes = [fp, fp, fp, C.c_int, C.c_float, C.POINTER(SdConfig), C.c_float,
                            C.c_int, C.POINTER(SdEvent), C.c_int, C.POINTER(SimStats)]
    lib.sim_run.restype = C.c_int
    return lib


# --------------------------------------------------------------------------
# Signal building blocks (body frame, mg)
# --------------------------------------------------------------------------
def unit(v):
    v = np.asarray(v, float)
    return v / np.linalg.norm(v)


def rand_dir(rng):
    return unit(rng.normal(size=3))


def tilt(gdir, deg, rng):
    """Rotate unit vector by deg around a random perpendicular axis."""
    axis = unit(np.cross(gdir, rand_dir(rng)))
    th = np.radians(deg)
    return unit(gdir * np.cos(th) + np.cross(axis, gdir) * np.sin(th)
                + axis * np.dot(axis, gdir) * (1 - np.cos(th)))


def slerp(a, b, n):
    s = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, n))  # smooth ease in/out
    om = np.arccos(np.clip(np.dot(a, b), -1, 1))
    if om < 1e-6:
        return np.tile(a, (n, 1))
    return (np.sin((1 - s) * om)[:, None] * a + np.sin(s * om)[:, None] * b) / np.sin(om)


def colored_noise(rng, n, rms, fc):
    """Band-limited noise with approx cutoff fc (Hz), per axis."""
    w = rng.normal(size=(n, 3))
    a = 1 - np.exp(-2 * np.pi * fc / FS)
    out = np.empty_like(w)
    acc = np.zeros(3)
    for _ in range(2):  # 2-pole
        for i in range(n):
            acc += a * (w[i] - acc)
            out[i] = acc
        w = out.copy()
    s = out.std()
    return out * (rms / s if s > 0 else 0)


def fast_noise(rng, n, rms, fc):
    """Vectorised 1-pole-ish band-limited noise (for long segments)."""
    from scipy.signal import lfilter
    a = 1 - np.exp(-2 * np.pi * fc / FS)
    w = rng.normal(size=(n, 3))
    y = lfilter([a], [1, -(1 - a)], w, axis=0)
    y = lfilter([a], [1, -(1 - a)], y, axis=0)
    s = y.std()
    return y * (rms / s if s > 0 else 0)


def ringdown(rng, n, peak_g, f_lo, f_hi, tau_lo, tau_hi, modes=(3, 6)):
    """Sum of damped sinusoids in random directions, scaled to peak_g."""
    t = np.arange(n) / FS
    sig = np.zeros((n, 3))
    for _ in range(rng.integers(modes[0], modes[1] + 1)):
        f = rng.uniform(f_lo, f_hi)
        tau = rng.uniform(tau_lo, tau_hi)
        ph = rng.uniform(0, 2 * np.pi)
        sig += np.outer(np.exp(-t / tau) * np.sin(2 * np.pi * f * t + ph), rand_dir(rng)) \
            * rng.uniform(0.3, 1.0)
    pk = np.abs(sig).max()
    return sig * (peak_g * G / pk if pk > 0 else 0)


def pulse(n, peak_mg, direction):
    """Smooth half-sine linear acceleration pulse."""
    return np.outer(np.sin(np.linspace(0, np.pi, n)) * peak_mg, direction)


class Session:
    """Accumulates body-frame acceleration and ground-truth labels."""

    def __init__(self, rng):
        self.rng = rng
        self.chunks = []
        self.n = 0
        self.g = unit([0.1, -0.2, 1.0])  # current gravity direction (body frame)
        self.shots = []      # release times (s)
        self.shot_kw = {}    # overrides for shot() (stress sets)
        self.nonshots = []   # (time_s, kind) of HF events that must NOT count

    @property
    def t(self):
        return self.n / FS

    def add(self, acc):
        self.chunks.append(acc.astype(np.float32))
        self.n += len(acc)

    def grav(self, gdirs):
        return gdirs * G

    # ---- primitives -------------------------------------------------------
    def rest(self, dur):
        n = int(dur * FS)
        self.add(self.grav(np.tile(self.g, (n, 1))) + self.rng.normal(0, 4, (n, 3)))

    def move_to(self, gnew, dur, lin_rms, fc=3.0):
        n = int(dur * FS)
        acc = self.grav(slerp(self.g, gnew, n)) + fast_noise(self.rng, n, lin_rms, fc)
        self.add(acc)
        self.g = gnew

    def hold(self, dur, tremor_mg):
        n = int(dur * FS)
        acc = self.grav(np.tile(self.g, (n, 1))) + fast_noise(self.rng, n, tremor_mg, 8.0) \
            + fast_noise(self.rng, n, tremor_mg * 0.6, 1.0)
        self.add(acc)

    def impact(self, peak_g, tau=(0.008, 0.04), f=(80, 900), lf_mg=0.0, kind=None, dur=0.3):
        n = int(dur * FS)
        acc = self.grav(np.tile(self.g, (n, 1))) + ringdown(self.rng, n, peak_g, f[0], f[1], *tau)
        if lf_mg:
            m = int(0.05 * FS)
            acc[:m] += pulse(m, lf_mg, rand_dir(self.rng))
        if kind:
            self.nonshots.append((self.t, kind))
        self.add(acc)

    # ---- composite behaviours ------------------------------------------
    def walk(self, dur, knock_rate=0.05):
        """Carrying the bow while walking: 2 Hz steps + occasional knocks."""
        n = int(dur * FS)
        t = np.arange(n) / FS
        g0 = self.g
        sway = np.stack([np.sin(2 * np.pi * 0.5 * t), np.cos(2 * np.pi * 0.35 * t),
                         np.zeros(n)], 1) * 0.25
        gd = np.array([unit(g0 + s) for s in sway[::64]])
        gd = np.repeat(gd, 64, axis=0)[:n]
        step = (np.maximum(0, np.sin(2 * np.pi * 1.9 * t)) ** 8)[:, None] * \
            self.rng.uniform(300, 1200) * unit([0.3, 0.2, 1])
        acc = self.grav(gd) + step + fast_noise(self.rng, n, 120, 6.0)
        self.add(acc)
        self.g = gd[-1]
        # knocks against leg/quiver while walking
        k = self.rng.poisson(knock_rate * dur)
        for _ in range(k):
            self.impact(self.rng.uniform(2, 10), kind="walk_knock", dur=0.15)
            self.walk_tail(self.rng.uniform(0.5, 2))

    def walk_tail(self, dur):
        n = int(dur * FS)
        acc = self.grav(np.tile(self.g, (n, 1))) + fast_noise(self.rng, n, 250, 4.0)
        self.add(acc)

    def shot(self, peak_g=(8, 40), rot_deg=(15, 80), tau=(0.02, 0.10), f=(40, 700),
             modes=(3, 6), recoil_g=(1.0, 3.0)):
        rng = self.rng
        self.shots.append(self.t)
        n = int(0.8 * FS)
        # gravity: bow rotates forward into the sling / follow-through
        g_after = tilt(self.g, rng.uniform(*rot_deg), rng)
        d_rot = rng.uniform(0.25, 0.6)
        nr = int(d_rot * FS)
        gd = np.vstack([slerp(self.g, g_after, nr), np.tile(g_after, (n - nr, 1))])
        acc = self.grav(gd)
        acc += ringdown(rng, n, rng.uniform(*peak_g), f[0], f[1], tau[0], tau[1], modes)
        # recoil: forward jump, then sling catch (opposite)
        fwd = rand_dir(rng)
        m1, m2 = int(rng.uniform(0.03, 0.08) * FS), int(rng.uniform(0.08, 0.2) * FS)
        rg = rng.uniform(*recoil_g) * G
        acc[:m1] += pulse(m1, rg, fwd)
        acc[m1 + int(0.05 * FS):m1 + int(0.05 * FS) + m2] += pulse(m2, -0.5 * rg, fwd)
        acc += fast_noise(rng, n, 150, 5.0)
        self.add(acc)
        self.g = g_after

    # ---- one end of arrows ----------------------------------------------
    def shoot_end(self, n_arrows, hard=False):
        rng = self.rng
        g_hold = unit([0.05, 1.0, 0.1]) if rng.random() < 0.5 else unit([0.0, 0.9, -0.3])
        # pick up from stand
        self.move_to(g_hold, rng.uniform(0.8, 1.6), 400)
        for _ in range(n_arrows):
            # nock arrow; small taps on rest/riser
            self.hold(rng.uniform(2, 6), 120)
            for _ in range(rng.integers(0, 3)):
                self.impact(rng.uniform(0.3, 2.5), tau=(0.003, 0.012), kind="nock_tap", dur=0.1)
                self.hold(rng.uniform(0.3, 1.0), 120)
            # raise and draw
            g_aim = tilt(g_hold, rng.uniform(5, 25), rng)
            self.move_to(g_aim, rng.uniform(1.5, 3.0), 500)
            aim = rng.uniform(0.7, 8.0)
            if rng.random() < 0.08:   # let-down, then redraw
                self.hold(aim, rng.uniform(15, 50))
                self.move_to(g_hold, 1.5, 400)
                self.move_to(g_aim, 2.0, 500)
                aim = rng.uniform(0.7, 6.0)
            if hard and rng.random() < 0.15:  # finger bumps puck while aiming
                self.hold(rng.uniform(0.6, 2), rng.uniform(15, 50))
                self.impact(rng.uniform(2, 8), tau=(0.004, 0.02), kind="aim_tap", dur=0.15)
            self.hold(aim, rng.uniform(15, 50))
            self.shot(**self.shot_kw)
            self.walk_tail(rng.uniform(1.0, 2.5))   # follow-through, lower bow
            self.move_to(g_hold, 1.0, 300)
        # set bow down on stand (thud), maybe a knock later
        g_stand = tilt(unit([0.1, -0.2, 1.0]), rng.uniform(0, 20), rng)
        self.move_to(g_stand, rng.uniform(0.8, 1.5), 400)
        self.impact(rng.uniform(1.5, 12), kind="set_down", dur=0.2)
        self.rest(rng.uniform(3, 20))
        if rng.random() < 0.5:   # someone knocks the bow on the stand
            self.impact(rng.uniform(2, 15), tau=(0.01, 0.06), lf_mg=rng.uniform(0, 150),
                        kind="stand_knock", dur=0.4)
            self.rest(rng.uniform(3, 10))
        if hard and rng.random() < 0.3:  # hard grab from stand (slap + lift)
            self.impact(rng.uniform(1.5, 5), tau=(0.004, 0.015), lf_mg=2000, kind="grab", dur=0.1)
            self.move_to(g_hold, 0.6, 1200)
            self.move_to(g_stand, 1.0, 400)
            self.rest(2)


def make_session(rng, hard=False, drop=False, shot_kw=None):
    s = Session(rng)
    s.shot_kw = shot_kw or {}
    s.rest(rng.uniform(5, 45))            # long rest -> device goes IDLE
    for _ in range(rng.integers(2, 5)):   # ends
        s.shoot_end(int(rng.integers(3, 7)), hard=hard)
        if rng.random() < 0.6:            # walk to target carrying the bow
            s.walk(rng.uniform(10, 40), knock_rate=0.08)
            s.move_to(tilt(unit([0.1, -0.2, 1.0]), 5, rng), 1.0, 300)
            s.impact(rng.uniform(1.5, 8), kind="set_down", dur=0.2)
        s.rest(rng.uniform(20, 60))        # >30 s rest exercises IDLE path
    if drop:
        # bow dropped: free fall then hard impact
        n = int(rng.uniform(0.15, 0.35) * FS)
        s.add(fast_noise(rng, n, 40, 5.0))
        s.impact(rng.uniform(20, 50), tau=(0.01, 0.05), kind="drop", dur=0.4)
        s.g = rand_dir(rng)
        s.rest(5)
    return s


# --------------------------------------------------------------------------
# Scoring
# --------------------------------------------------------------------------
def score(sess, events, tol=0.06):
    shots = np.array(sess.shots)
    acc = [e for e in events if e.accepted]
    matched = set()
    tp = 0
    fps = []
    for e in acc:
        t = e.t_ms / 1000.0
        idx = np.where(np.abs(shots - t) <= tol)[0] if len(shots) else []
        if len(idx) and idx[0] not in matched:
            matched.add(idx[0])
            tp += 1
        else:
            kind = "other"
            for (tn, k) in sess.nonshots:
                if -0.05 <= t - tn <= 0.35:
                    kind = k
            fps.append(kind)
    fn_times = [shots[i] for i in range(len(shots)) if i not in matched]
    return tp, fps, fn_times


def run_set(lib, cfg, name, n, seed, hard=False, drop_frac=0.0, shot_kw=None, verbose=True):
    rng = np.random.default_rng(seed)
    tot_shots = tot_tp = 0
    fp_kinds = {}
    fn_reasons = {}
    act = tot_t = 0.0
    wakes = 0
    ev_buf = (SdEvent * 4096)()
    feats = []
    for _ in range(n):
        s = make_session(rng, hard=hard, drop=rng.random() < drop_frac, shot_kw=shot_kw)
        a = np.concatenate(s.chunks).astype(np.float32)
        st = SimStats()
        ne = lib.sim_run(np.ascontiguousarray(a[:, 0]), np.ascontiguousarray(a[:, 1]),
                         np.ascontiguousarray(a[:, 2]), len(a), FS, C.byref(cfg), IDLE_HZ,
                         WAKE_LATENCY_MS,
                         ev_buf, 4096, C.byref(st))
        evs = [ev_buf[i] for i in range(ne)]
        tp, fps, fns = score(s, evs)
        tot_shots += len(s.shots)
        tot_tp += tp
        for k in fps:
            fp_kinds[k] = fp_kinds.get(k, 0) + 1
        for tfn in fns:
            near = [e for e in evs if abs(e.t_ms / 1000 - tfn) < 0.06]
            r = ("missed_trigger" if not near else
                 {1: "rej_not_still", 2: "rej_no_follow"}.get(near[0].reason, "?"))
            fn_reasons[r] = fn_reasons.get(r, 0) + 1
        for e in evs:
            t = e.t_ms / 1000
            is_shot = any(abs(t - x) <= 0.06 for x in s.shots)
            feats.append((is_shot, e.peak_hf_mg, e.pre_still_ms, e.post_lf_mg))
        act += st.active_ms / 1000
        tot_t += len(a) / FS
        wakes += st.wakes
    fp = sum(fp_kinds.values())
    res = dict(name=name, sessions=n, shots=tot_shots, tp=tot_tp, fn=tot_shots - tot_tp,
               fp=fp, fp_kinds=fp_kinds, fn_reasons=fn_reasons,
               recall=tot_tp / max(1, tot_shots), precision=tot_tp / max(1, tot_tp + fp),
               active_frac=act / tot_t, hours=tot_t / 3600, wakes=wakes, feats=feats)
    if verbose:
        print(f"[{name}] sessions={n} sim_time={res['hours']:.2f} h shots={tot_shots}  "
              f"recall={res['recall']*100:.2f}%  precision={res['precision']*100:.2f}%  "
              f"FN={res['fn']} {fn_reasons}  FP={fp} {fp_kinds}  "
              f"ACTIVE={res['active_frac']*100:.1f}% of time  wakes={wakes}")
    return res


def margins(res):
    f = np.array([(a, b, c, d) for a, b, c, d in res["feats"]], dtype=float)
    shots = f[f[:, 0] == 1]
    non = f[f[:, 0] == 0]
    print("  feature ranges over triggered candidates (5th/50th/95th pct):")
    for i, nm in [(1, "peak_hf_mg"), (2, "pre_still_ms"), (3, "post_lf_mg")]:
        ps = np.percentile(shots[:, i], [5, 50, 95]) if len(shots) else []
        pn = np.percentile(non[:, i], [5, 50, 95]) if len(non) else []
        print(f"    {nm:13s} shots {np.round(ps).astype(int)}   non-shots {np.round(pn).astype(int)}")


UNIT_TESTS = [("test_capture.c", "capture.c"), ("test_evlog.c", "evlog.c")]


def unit_test_passes(pair):
    test, module = pair
    exe = os.path.join(tempfile.mkdtemp(), test.replace(".c", ".exe"))
    srcs = [os.path.join(HERE, test), os.path.join(SRC, module)]
    subprocess.check_call([CC] + CFLAGS + ["-o", exe] + srcs)
    return subprocess.run([exe]).returncode == 0


def unit_tests_pass():
    return all([unit_test_passes(pair) for pair in UNIT_TESTS])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sessions", type=int, default=60)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--stress", action="store_true")
    args = ap.parse_args()

    lib = build_lib()
    cfg = SdConfig()
    lib.sd_default_config(C.byref(cfg))
    print(f"config: trig={cfg.trig_hf_mg:.0f} mg, move={cfg.move_mg:.0f} mg, "
          f"pre_still>={cfg.pre_still_min_ms} ms, post_lf>={cfg.post_lf_min_mg:.0f} mg "
          f"[{cfg.post_start_ms}-{cfg.post_end_ms} ms], fs={cfg.fs_hz:.0f} Hz")

    ok = unit_tests_pass()
    r = run_set(lib, cfg, "nominal", args.sessions, args.seed, drop_frac=0.1)
    margins(r)
    ok &= r["recall"] >= 0.99 and r["precision"] >= 0.99
    if args.stress:
        r2 = run_set(lib, cfg, "hard-cases", args.sessions, args.seed + 100, hard=True,
                     drop_frac=0.3)
        margins(r2)
        ok &= r2["recall"] >= 0.98 and r2["precision"] >= 0.98
        # Informational: weak bow / firm grip (small shock, little rotation).
        # Shows where the default thresholds stop working; not a pass gate.
        r3 = run_set(lib, cfg, "weak-bow (info)", args.sessions // 2, args.seed + 200,
                     shot_kw=dict(peak_g=(4, 12), rot_deg=(5, 30), recoil_g=(0.4, 1.5)))
        margins(r3)
    print("RESULT:", "PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
