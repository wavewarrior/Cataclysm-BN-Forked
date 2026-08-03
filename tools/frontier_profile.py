#!/usr/bin/env python3
"""Gate the seen/remembered frontier band for a dark rim or a bright overshoot.

The sprite lighting composite is moving from `max(mem_tint, gpu_total)` ("brighter
wins") to `lerp(gpu_lit_result, memory_result, frontier_cov)` - a genuine monotonic
cross-fade. The two forms agree at BOTH endpoints of `frontier_cov` (0 and 1) but not
in between, so the frontier band changes ON PURPOSE and "no visual diff" is the WRONG
acceptance criterion there. The right one - and the one thing the categorical
light-mode debug view cannot check, because it shows CLASS and not brightness - is:

    across a seen/remembered boundary the luma ramp must be MONOTONIC between the two
    plateaus: no dip below the remembered side, no overshoot above the visible side.

So this tool reads a `debug_mode 15` capture (the vision-frontier view, where GREEN
carries `frontier_cov`, R-high/B-low marks a REMEMBERED tile and a flat G~0.55 marks
an ordinary drawn one) to LOCATE the boundary, and a normal `debug_mode 0` capture of
the same state to supply the LUMA. It never launches the game and never emits an
image - only a few numbered lines and a verdict, because every printed byte is a
token the caller pays for.

Two scales, because a real dark rim is 1-2 px wide and the reported 16-number ramp
averages ~1/3 of a tile per block, which would smear a -12 rim down to -2:
  - the block ramp catches a broad tilt and is what gets printed;
  - the raw per-position profile catches a 1 px rim, judged against a noise floor
    MEASURED from the two plateau regions rather than a constant, because tile art
    texture survives band-averaging and is the only thing that can fake a step there.
    The floor is printed, so a null result states its own detection limit.

Admissibility is checked before a single pixel is measured, because zoom is the
dominant nondeterminism in this project's scripted captures: dropped zoom keypresses
put two runs of the same scenario at 8.02 px vs 42.86 px tile pitch, a 19.7 luma swing
that manufactured a 67% "signal" out of an unchanged shader. Categorical hues survive
that; luma profiles do not.

Usage:
  python tools/frontier_profile.py --mode15 F15.png --scene SCENE.png
      [--mode15-b F15b.png --scene-b SCENEb.png]
      [--axis x|y] [--band 0.35,0.65] [--tol 2.0]

Exit: 0 gate passed, 1 gate failed, 2 usage/IO error.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import NamedTuple

import numpy as np
from PIL import Image

# Reuse the FFT pitch estimator instead of duplicating it. This tool exists because
# zoom drift silently invents signal; two independent copies of the measurement that
# detects it would be two things to keep honest.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from shadow_zoom_check import pitch  # noqa: E402

TILES_PER_SIDE = 3.0
MAX_RAMP = 16
PITCH_TOL_FRAC = 0.05
MIN_TILES_ACROSS = 8    # no playable zoom shows fewer; above this the FFT saturated


class Located(NamedTuple):
    """Where the seen/remembered boundary sits on the profiled axis."""

    pos: float          # sub-pixel crossing of remembered-fraction 0.5
    idx: int            # nearest integer position, used for windowing
    crossings: int      # how many 0.5 crossings the band contained
    rem_first: bool     # True when the remembered side is at the low end of the axis
    max_n: int          # largest half-window that still fits inside the frame


class Ramp(NamedTuple):
    """A luma profile across one boundary, oriented remembered plateau first."""

    raw: np.ndarray     # per-position band-averaged luma
    raw_at: np.ndarray  # axis coordinate of each raw position
    ramp: np.ndarray    # <= MAX_RAMP block means, remembered -> visible
    at: np.ndarray      # axis coordinate of each block centre
    rem: float          # remembered-side plateau mean
    vis: float          # visible-side plateau mean
    plateau_px: int


def load_rgb(path: str) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def size_of(rgb: np.ndarray) -> tuple[int, int]:
    return rgb.shape[1], rgb.shape[0]


def classify(rgb: np.ndarray) -> dict[str, np.ndarray]:
    """Categorical masks for the debug_mode 15 palette (8-bit, so 0.55 ~ 140)."""
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    return {
        "remembered": (r >= 200) & (b <= 80),
        "ordinary": (r <= 80) & (b <= 80) & (g >= 100) & (g <= 180),
        "overlay": (b >= 200) & (r <= 80),
        "low_sight": (r >= 200) & (b >= 200),
        "undrawn": (r <= 12) & (g <= 12) & (b <= 12),
    }


def luma(rgb: np.ndarray) -> np.ndarray:
    """ITU-R BT.601 luma."""
    return 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]


def band_of(a: np.ndarray, axis: str, band: tuple[float, float]) -> np.ndarray:
    """Slice the NON-profiled axis to `band` and orient the result as (band, L)."""
    n = a.shape[0] if axis == "x" else a.shape[1]
    lo = max(0, min(n - 1, int(round(n * band[0]))))
    hi = max(lo + 1, min(n, int(round(n * band[1]))))
    return a[lo:hi, :] if axis == "x" else a[:, lo:hi].T


def locate(m15: np.ndarray, axis: str,
           band: tuple[float, float]) -> Located | dict[str, float]:
    """Find the remembered-fraction 0.5 crossing, or return the class fractions."""
    cls = classify(m15)
    rem = band_of(cls["remembered"].astype(np.float32), axis, band).mean(axis=0)
    ordn = band_of(cls["ordinary"].astype(np.float32), axis, band).mean(axis=0)
    tot = rem + ordn
    ok = np.flatnonzero(tot > 0.0)
    frac = rem[ok] / tot[ok] - 0.5
    hits: list[float] = []
    for a in range(len(ok) - 1):
        fa, fb = float(frac[a]), float(frac[a + 1])
        ia, ib = int(ok[a]), int(ok[a + 1])
        if fa == 0.0:
            hits.append(float(ia))
        elif fa * fb < 0.0:
            hits.append(ia + (ib - ia) * fa / (fa - fb))
    if not hits:
        return {k: float(band_of(v.astype(np.float32), axis, band).mean())
                for k, v in cls.items()}
    pos = min(hits, key=lambda h: abs(h - len(rem) / 2.0))
    idx = int(round(pos))
    lo_side = float(rem[:idx].mean()) if idx else 0.0
    hi_side = float(rem[idx + 1:].mean()) if idx + 1 < len(rem) else 0.0
    return Located(pos, idx, len(hits), lo_side > hi_side, min(idx, len(rem) - 1 - idx))


def extract(scene: np.ndarray, axis: str, band: tuple[float, float], loc: Located,
            n: int, pitch_px: float) -> Ramp:
    """Window the band-averaged luma around the boundary, remembered side first."""
    prof = band_of(luma(scene), axis, band).mean(axis=0)
    lo, hi = loc.idx - n, loc.idx + n + 1
    raw = prof[lo:hi]
    at = np.arange(lo, hi, dtype=np.float32)
    if not loc.rem_first:
        raw, at = raw[::-1], at[::-1]
    nb = min(MAX_RAMP, len(raw))
    ramp = np.array([c.mean() for c in np.array_split(raw, nb)], dtype=np.float32)
    centre = np.array([c.mean() for c in np.array_split(at, nb)], dtype=np.float32)
    p = max(1, min(int(round(pitch_px)), len(raw) // 3))
    return Ramp(raw, at, ramp, centre, float(raw[:p].mean()), float(raw[-p:].mean()), p)


def check(r: Ramp, tol: float) -> tuple[list[str], list[str]]:
    """The verdict lines, and the subset of them that failed."""
    rising = r.vis >= r.rem
    dark, bright = (("remembered", r.rem), ("visible", r.vis)) if rising else \
        (("visible", r.vis), ("remembered", r.rem))
    sign = 1.0 if rising else -1.0
    p = r.plateau_px
    dark_raw, bright_raw = (r.raw[:p], r.raw[-p:]) if rising else (r.raw[-p:], r.raw[:p])
    # Self-calibrating floor: the plateaus are flat by construction, so their step
    # magnitude IS this scene's art-texture noise on the raw profile. Doubled, per
    # the harness convention that a threshold is a measured null x2 and never a
    # constant - art detail at the boundary can be locally busier than the plateaus,
    # and a gate that cries wolf gets ignored.
    flat = np.abs(np.concatenate([np.diff(r.raw[:p]), np.diff(r.raw[-p:])]))
    floor = 2.0 * float(flat.max()) if flat.size else 0.0
    lim = max(tol, floor)

    bstep = np.diff(r.ramp) * sign
    bk = int(np.argmin(bstep)) if bstep.size else 0
    bworst = float(bstep[bk]) if bstep.size else 0.0
    bat = (r.at[bk] + r.at[bk + 1]) / 2.0 if bstep.size else r.at[0]
    rstep = np.diff(r.raw) * sign
    rk = int(np.argmin(rstep)) if rstep.size else 0
    rworst = float(rstep[rk]) if rstep.size else 0.0
    rat = (r.raw_at[rk] + r.raw_at[rk + 1]) / 2.0 if rstep.size else r.raw_at[0]

    dip_b = float(r.ramp.min()) - dark[1]
    dip_r = float(r.raw.min()) - float(dark_raw.min())
    over_b = float(r.ramp.max()) - bright[1]
    over_r = float(r.raw.max()) - float(bright_raw.max())
    lines = [
        f"monotonic blocks: worst step {num(bworst)} at {bat:.0f} (tol {tol:.1f})"
        f" {'OK' if bworst >= -tol else 'VIOLATION'}",
        f"monotonic raw 1px: worst step {num(rworst)} at {rat:.0f}"
        f" (floor {floor:.1f}, tol {tol:.1f}) {'OK' if rworst >= -lim else 'VIOLATION'}",
        f"dip below {dark[0]} plateau {dark[1]:.1f}: ramp min {r.ramp.min():.1f}"
        f" margin {num(dip_b)} | raw margin {num(dip_r)}"
        f" {'OK' if dip_b >= -tol and dip_r >= -lim else 'VIOLATION'}",
        f"overshoot above {bright[0]} plateau {bright[1]:.1f}: ramp max {r.ramp.max():.1f}"
        f" margin {num(over_b)} | raw margin {num(over_r)}"
        f" {'OK' if over_b <= tol and over_r <= lim else 'VIOLATION'}",
    ]
    bad: list[str] = []
    if bworst < -tol:
        bad.append(f"non-monotonic block step {num(bworst)} at {bat:.0f}")
    if rworst < -lim:
        bad.append(f"non-monotonic raw step {num(rworst)} at {rat:.0f}")
    if dip_b < -tol or dip_r < -lim:
        bad.append(f"dip {num(min(dip_b, dip_r))} below {dark[0]} plateau {dark[1]:.1f}")
    if over_b > tol or over_r > lim:
        bad.append(f"overshoot {num(max(over_b, over_r))} above {bright[0]}"
                   f" plateau {bright[1]:.1f}")
    return lines, bad


def num(v: float) -> str:
    """Signed, one decimal, and never a misleading '-0.0' next to an OK."""
    return f"{round(v, 1) + 0.0:+.1f}"


def fmt(v: np.ndarray) -> str:
    return " ".join(f"{x:.1f}" for x in v)


def parse_band(s: str) -> tuple[float, float]:
    lo, hi = (float(x) for x in s.split(","))
    if not 0.0 <= lo < hi <= 1.0:
        raise ValueError(f"band must be 0 <= lo < hi <= 1, got {s}")
    return lo, hi


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--mode15", required=True)
    ap.add_argument("--scene", required=True)
    ap.add_argument("--mode15-b")
    ap.add_argument("--scene-b")
    ap.add_argument("--axis", choices=("x", "y"), default="x")
    ap.add_argument("--band", default="0.35,0.65")
    ap.add_argument("--tol", type=float, default=2.0)
    a = ap.parse_args()
    if bool(a.mode15_b) != bool(a.scene_b):
        print("FAIL --mode15-b and --scene-b must be given together")
        return 2
    try:
        band = parse_band(a.band)
        pairs = [("A", a.mode15, a.scene)]
        if a.mode15_b:
            pairs.append(("B", a.mode15_b, a.scene_b))
        m15 = {t: load_rgb(p) for t, p, _ in pairs}
        scn = {t: load_rgb(s) for t, _, s in pairs}
        sizes = {t: (size_of(m15[t]), size_of(scn[t])) for t, _, _ in pairs}
        pit = {t: pitch(s)[0] for t, _, s in pairs}
    except (OSError, ValueError) as e:
        print(f"FAIL cannot read inputs: {e}")
        return 2

    # 1. Admissibility, before a single pixel is measured.
    print("1) admissible: " + " | ".join(
        f"{t} m15 {w}x{h} vs scene {sw}x{sh} pitch {pit[t]:.2f}px"
        for t, ((w, h), (sw, sh)) in sizes.items()))
    junk = [t for t, ((_, _), (sw, _)) in sizes.items() if pit[t] > sw / MIN_TILES_ACROSS]
    if junk:
        # Observed: on an untextured crop the estimator's argmax saturates at its
        # lowest bin and returns the band edge, which is not a tile pitch at all.
        print(f"   WARN pitch estimate unreliable for {','.join(junk)} (no periodic"
              f" peak; > 1/{MIN_TILES_ACROSS} of the frame) - window size and the"
              " zoom gate below are not trustworthy")
    for t, (ms, ss) in sizes.items():
        if ms != ss:
            print(f"FAIL pair {t} mode15 {ms} != scene {ss} - not the same frame geometry")
            return 1
    if len(pairs) == 2:
        if sizes["A"][1] != sizes["B"][1]:
            print(f"FAIL pair sizes differ {sizes['A'][1]} vs {sizes['B'][1]}"
                  " - not comparable")
            return 1
        rel = abs(pit["A"] - pit["B"]) / ((pit["A"] + pit["B"]) / 2.0)
        if rel > PITCH_TOL_FRAC:
            print(f"FAIL scene tile pitch {pit['A']:.2f} vs {pit['B']:.2f} px differs"
                  f" {rel * 100:.1f}% > {PITCH_TOL_FRAC * 100:.0f}% - zoom drift, re-capture")
            return 1
        print(f"   scene pitch agrees within {rel * 100:.1f}%")

    # 2. Locate the boundary from the categorical mode-15 frame.
    locs: dict[str, Located] = {}
    for t, _, _ in pairs:
        got = locate(m15[t], a.axis, band)
        if isinstance(got, dict):
            print(f"   pair {t} band class fractions: "
                  + " ".join(f"{k}={v:.3f}" for k, v in got.items()))
            print("FAIL no seen/remembered boundary found")
            return 1
        locs[t] = got

    # 3. Window ~3 tiles per side, sized from the measured pitch, shared across pairs.
    n = min([int(round(TILES_PER_SIDE * pit[t])) for t, _, _ in pairs]
            + [locs[t].max_n for t, _, _ in pairs])
    if n < 3:
        print(f"FAIL boundary too close to the frame edge: usable half-window {n} px")
        return 1
    ramps = {t: extract(scn[t], a.axis, band, locs[t], n, pit[t]) for t, _, _ in pairs}

    # 4. Report.
    ln = 2
    bad: dict[str, list[str]] = {}
    for t, _, _ in pairs:
        loc, r = locs[t], ramps[t]
        print(f"{ln}) {t} boundary: {a.axis}={loc.pos:.1f} ({loc.crossings} crossing(s),"
              f" remembered on the {'low' if loc.rem_first else 'high'} side);"
              f" window +/-{n}px = {n / pit[t]:.1f} tiles/side")
        print(f"{ln + 1}) {t} plateaus: remembered {r.rem:.1f} | visible {r.vis:.1f}"
              f" (outer {r.plateau_px}px each side)")
        print(f"{ln + 2}) {t} ramp[{len(r.ramp)}] remembered->visible: {fmt(r.ramp)}")
        lines, bad[t] = check(r, a.tol)
        for j, s in enumerate(lines):
            print(f"{ln + 3 + j}) {t} {s}")
        ln += 3 + len(lines)

    # 5. Before/after: a delta here is expected, only a broken ramp fails.
    if len(pairs) == 2:
        d = ramps["B"].ramp - ramps["A"].ramp
        print(f"{ln}) delta B-A per block: " + " ".join(num(x) for x in d))
        print(f"{ln + 1}) note: a non-zero delta in the band is EXPECTED (max composite"
              " -> lerp cross-fade); only non-monotonicity, a dip or an overshoot fails")

    fails = [f"{t}: {m}" for t, ms in bad.items() for m in ms]
    if fails:
        print("FAIL " + "; ".join(fails))
        return 1
    print(f"PASS ramp monotonic between plateaus, no dip or overshoot (tol {a.tol:.1f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
