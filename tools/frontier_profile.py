#!/usr/bin/env python3
"""Gate the seen/remembered frontier band for a dark rim or a bright overshoot.

The sprite lighting composite is moving from `max(mem_tint, gpu_total)` ("brighter
wins") to `lerp(gpu_lit_result, memory_result, frontier_cov)` - a genuine monotonic
cross-fade. The two forms agree at BOTH endpoints of `frontier_cov` (0 and 1) but not
in between, so the frontier band changes ON PURPOSE and "no visual diff" is the WRONG
acceptance criterion there. The right one - and the one thing the categorical
light-mode debug view cannot check, because it shows CLASS and not brightness - is:

    across a seen/remembered boundary the luma ramp must be MONOTONIC between the two
    plateaus: no dip below the darker side, no overshoot above the brighter one.

WHY THE PROFILE RUNS ALONG THE BOUNDARY NORMAL, NOT ALONG A SCREEN AXIS.
`frontier_cov` is a bilinear interpolation of four corner coverages, so it is monotonic
along the boundary NORMAL and along nothing else. Profiling down a screen axis samples
a SLANTED section of the ramp, and worse, band-averages across a boundary that curves
inside the band: two tiles at the same x sit at different phases of the cross-fade, so
their mean tilts and wiggles. That wiggle is arithmetic, not lighting. Measured cost of
getting this wrong: the axis form called `-4.5 at 2118` (daylight) and `-2.1 at 1588`
(night) FAILURES on output independently confirmed correct - the same dip appears in the
albedo-free `frontier_cov` GREEN channel of debug view 15, so it is geometry.

So this tool reads a `debug_mode 15` capture (the vision-frontier view, where GREEN
carries `frontier_cov`, R-high/B-low marks a REMEMBERED tile, a flat G~0.55 marks an
ordinary drawn one and R-high/B-high marks `lit_level::LOW`, the dim edge of sight), and
turns those class masks into a smoothed remembered-fraction FIELD: 1 on the remembered
side, 0 on the SEEN side, where seen means `ordinary` OR `low_sight`. That union is not a
detail - the night capture measured here contains ZERO `ordinary` pixels and 53%
`low_sight`, so defining the seen side as `ordinary` alone makes the night frontier
nonexistent. The field is 1-on-one-side by construction so its gradient points the same
way at every boundary pixel and no 180-degree unwrapping is needed. It then takes the
gradient to get a unit normal per boundary pixel, and walks the luma of a `debug_mode 0`
capture of the same state along +/- that normal. Averaging those per-offset samples over
every boundary pixel is the standard edge-profile measurement: offsets are aligned to
the boundary, so terrain variation ALONG the boundary averages out instead of tilting
the ramp. It never launches the game and never emits an image - only a few numbered
lines and a verdict, because every printed byte is a token the caller pays for.

`--viewport` is not optional in practice. The HUD is part of the frame and is NOT part
of the lighting composite: the sidebar and message bar are drawn on is_lit=false
segments where sprite_batcher forces debug_mode 0, so they are not categorical at all
and their luma is unrelated to any frontier. A boundary near the sidebar edge silently
averages HUD pixels into the "visible" plateau. So the profile is confined to the
viewport and the tool REFUSES rather than sampling one pixel outside it.

Thresholds are self-calibrating, per the harness convention that a threshold is a
measured null x2 and never a constant. The two plateau ends of the profile are flat by
construction, so their step magnitude IS this scene's residual noise and their deviation
from their own mean IS its level uncertainty; both are doubled and printed, so a null
result states its own detection limit.

Admissibility is checked before a single pixel is measured, because zoom is the dominant
nondeterminism in this project's scripted captures: dropped zoom keypresses put two runs
of the same scenario at 8.02 px vs 42.86 px tile pitch, a 19.7 luma swing that
manufactured a 67% "signal" out of an unchanged shader. Categorical hues survive that;
luma profiles do not.

Usage:
  python tools/frontier_profile.py --mode15 F15.png --scene SCENE.png
      [--mode15-b F15b.png --scene-b SCENEb.png] [--viewport 0.25,0.12,0.55,0.80]
      [--band 0.35,0.65] [--tol 2.0] [--profile normal|axis] [--axis x|y]

Exit: 0 gate passed, 1 gate failed, 2 usage/IO error.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import NamedTuple

import numpy as np
from PIL import Image

# Reuse the FFT pitch estimator instead of duplicating it. This tool exists because zoom
# drift silently invents signal; two independent copies of the measurement that detects
# it would be two things to keep honest. It takes the scene PATH and is called on the
# FULL frame on purpose: it already sub-crops to y 0.2-0.8, x 0.27-0.86 internally
# (shadow_zoom_check.py:23), which excludes the sidebar and the message bar, so the HUD
# never reaches the FFT. Pitch is a property of the zoom, not of the viewport.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from shadow_zoom_check import pitch  # noqa: E402

TILES_PER_SIDE = 3.0
MAX_RAMP = 16
PITCH_TOL_FRAC = 0.05
MIN_TILES_ACROSS = 8      # no playable zoom shows fewer; above this the FFT saturated
MIN_BOUNDARY_PX = 64      # below this the level set is too short to average over
MAX_BOUNDARY_PX = 4000    # more samples buy accuracy the noise floor already hides
MIN_CLASS_WEIGHT = 0.15   # blurred remembered+seen evidence needed at a boundary px
MIN_GRAD = 3e-3           # a 0->1 field smoothed over ~2r px has |grad| ~ 1/(2r)
MAX_SUBPIXEL = 2.0        # the first-order 0.5-crossing offset must stay local
# Averaging along the boundary only cancels terrain if the boundary FACES many ways. On
# a single straight frontier every sample at a given normal offset lands on the same
# screen line, so albedo is perfectly correlated with offset and no amount of averaging
# separates a bright terrain band from a real overshoot. The normal-angle spread is
# exactly that measurement, so it doubles as the confound detector.
MIN_SPREAD_DEG = 20.0


class Located(NamedTuple):
    """Where the seen/remembered boundary sits on the profiled axis (fallback path)."""

    pos: float          # sub-pixel crossing of remembered-fraction 0.5, viewport-local
    idx: int            # nearest integer position, used for windowing
    crossings: int      # how many 0.5 crossings the band contained
    rem_first: bool     # True when the remembered side is at the low end of the axis
    max_n: int          # largest half-window that still fits inside the VIEWPORT


class Frontier(NamedTuple):
    """The 0.5 level set of the smoothed remembered-fraction field, with normals."""

    px: np.ndarray      # (P,2) sub-pixel boundary origins, viewport-local, x then y
    nvec: np.ndarray    # (P,2) unit normals, every one pointing toward remembered
    angle: float        # dominant normal direction in degrees, 0 = +x, 90 = +y (down)
    axis_dev: float     # degrees off the nearest screen axis
    spread: float       # circular std of the per-pixel normal angle, degrees
    kept: int           # level-set pixels whose full window fits inside the viewport
    found: int          # level-set pixels before that window-fit filter
    ntiles: int         # DISTINCT terrain tiles the level set crosses = independent
                        # samples. Adjacent boundary pixels share a tile, so sqrt(kept)
                        # would overstate the averaging by sqrt(kept/ntiles).
    blur_r: int         # box-blur radius used to make the gradient stable
    slack: float        # MEDIAN px of room a profiled window has beyond the +/-n it
                        # needs; the minimum is pinned to 1 by the window-fit filter and
                        # so carries no information
    centroid: tuple[float, float]


class Thin(NamedTuple):
    """Why the boundary field could not be profiled along its normal."""

    kind: str           # "none" | "edge" -> refuse; "degenerate" -> axis fallback
    detail: str


class Ramp(NamedTuple):
    """A luma profile across one boundary, oriented remembered plateau first."""

    label: str          # "normal" or the fallback axis name, for position formatting
    prof: np.ndarray    # per-sample luma
    at: np.ndarray      # signed normal offset, or ABSOLUTE frame axis pixel
    ramp: np.ndarray    # <= MAX_RAMP block means, remembered -> visible, for reading
    ramp_at: np.ndarray
    rem: float          # remembered-side plateau mean
    vis: float          # visible-side plateau mean
    plateau_px: int
    sem: float          # worst standard error of the mean across the averaged samples


def load_rgb(path: str) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def size_of(rgb: np.ndarray) -> tuple[int, int]:
    return rgb.shape[1], rgb.shape[0]


def resolve_viewport(spec: str | None, size: tuple[int, int]) -> tuple[int, int, int, int]:
    """A viewport written with decimals is a fraction of the frame, matching vv's `rect`.
    Same convention as tools/light_mode_check.py: the install has come up at two
    different client sizes on this box, so an absolute viewport is a latent
    silent-wrong-region bug."""
    w, h = size
    if spec is None:
        return (0, 0, w, h)
    parts = spec.replace(" ", "").split(",")
    if len(parts) != 4:
        raise ValueError(f"bad viewport {spec!r}, want x,y,w,h")
    if "." in spec:
        f = [float(v) for v in parts]
        x, y, vw, vh = int(f[0] * w), int(f[1] * h), int(f[2] * w), int(f[3] * h)
    else:
        x, y, vw, vh = (int(v) for v in parts)
    x, y = max(0, x), max(0, y)
    vw, vh = min(vw, w - x), min(vh, h - y)
    if vw <= 0 or vh <= 0:
        raise ValueError(f"viewport {spec!r} is empty inside {w}x{h}")
    return (x, y, vw, vh)


def crop(rgb: np.ndarray, vp: tuple[int, int, int, int]) -> np.ndarray:
    x, y, w, h = vp
    return rgb[y:y + h, x:x + w]


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


def box_mean(a: np.ndarray, r: int) -> np.ndarray:
    """Separable box mean of radius `r`, edge-extended. Cumsum, because no scipy."""
    out = np.asarray(a, dtype=np.float64)
    for ax in (0, 1):
        n = out.shape[ax]
        pad = [(0, 0), (0, 0)]
        pad[ax] = (r + 1, r)
        c = np.cumsum(np.pad(out, pad, mode="edge"), axis=ax)
        hi = np.take(c, np.arange(2 * r + 1, 2 * r + 1 + n), axis=ax)
        lo = np.take(c, np.arange(n), axis=ax)
        out = (hi - lo) / (2 * r + 1)
    return out


def sample(img: np.ndarray, x: np.ndarray, y: np.ndarray) -> np.ndarray:
    """Bilinear lookup, because a step along the normal is not an integer pixel."""
    h, w = img.shape
    x = np.clip(x, 0.0, w - 1.001)
    y = np.clip(y, 0.0, h - 1.001)
    x0, y0 = x.astype(np.intp), y.astype(np.intp)
    fx, fy = x - x0, y - y0
    top = img[y0, x0] * (1.0 - fx) + img[y0, x0 + 1] * fx
    bot = img[y0 + 1, x0] * (1.0 - fx) + img[y0 + 1, x0 + 1] * fx
    return top * (1.0 - fy) + bot * fy


def band_span(shape: tuple[int, ...], axis: str,
              band: tuple[float, float]) -> tuple[int, int]:
    """The `band` slice of the viewport axis that `--axis` does NOT profile."""
    n = shape[0] if axis == "x" else shape[1]
    lo = max(0, min(n - 1, int(round(n * band[0]))))
    hi = max(lo + 1, min(n, int(round(n * band[1]))))
    return lo, hi


def seen_mask(cls: dict[str, np.ndarray]) -> np.ndarray:
    """Every class that means "the character can see this tile right now".

    `low_sight` (magenta->white, `lit_level::LOW`) is the dim edge of sight: drawn,
    currently visible, NOT remembered, so it belongs on the seen side. Measured on the
    two real captures: the night frame contains ZERO `ordinary` pixels and 53%
    `low_sight`, so a seen side defined as `ordinary` alone makes the night frontier
    literally nonexistent and every number derived from it fiction. `overlay` is a
    NEVER-seen tile's lighting overlay and `undrawn` is nothing at all, so both stay out
    of the weight entirely rather than being counted as seen.
    """
    return cls["ordinary"] | cls["low_sight"]


def frontier(m15: np.ndarray, pitch_px: float, n: int) -> Frontier | Thin:
    """Level set and unit normals of the smoothed remembered-fraction field.

    The field is the LOCAL remembered fraction among classified pixels: 1 on the
    remembered side, 0 on the seen side, so the gradient points toward remembered at
    EVERY boundary pixel and the per-pixel normals need no 180-degree unwrapping before
    they can be averaged. Normalising by class weight rather than by area is what stops
    undrawn regions, never-seen overlays and anything non-categorical from dragging the
    field toward 0 and inventing a boundary at their edge.

    No `--band` here on purpose: the band exists to give the fallback axis profile a
    strip to average, and for the normal profile it is pure loss - fewer boundary pixels
    to average over, for no HUD protection that `--viewport` does not already give.
    """
    cls = classify(m15)
    rem = cls["remembered"].astype(np.float64)
    wgt = rem + seen_mask(cls).astype(np.float64)
    # Smooth over about one tile: the masks are piecewise constant per tile, so an
    # unsmoothed gradient is a train of unit steps whose direction is quantised to the
    # four screen axes - exactly the bias this tool exists to remove. Two box passes
    # rather than one, because the gradient of a single box mean is still piecewise
    # constant and its normal jitters between neighbouring pixels.
    r = max(2, int(round(pitch_px / 2.0)))
    num = box_mean(box_mean(rem, r), r)
    den = box_mean(box_mean(wgt, r), r)
    field = np.full(den.shape, 0.5)
    np.divide(num, den, out=field, where=den > MIN_CLASS_WEIGHT)

    gy, gx = np.gradient(field)
    mag = np.hypot(gx, gy)
    s = field - 0.5
    cross = np.zeros(field.shape, dtype=bool)
    cross[:, :-1] |= s[:, :-1] * s[:, 1:] < 0.0
    cross[:-1, :] |= s[:-1, :] * s[1:, :] < 0.0
    ok = cross & (den > MIN_CLASS_WEIGHT) & (mag > MIN_GRAD)
    found = int(np.count_nonzero(ok))
    if found == 0:
        return Thin("none", "no remembered/seen 0.5 level set inside the viewport")
    # The whole +/-n window must stay inside the VIEWPORT whatever the local normal
    # turns out to be. Outside it lies the HUD, which is drawn with debug_mode forced to
    # 0 and has nothing to do with the composite; one such sample poisons a plateau.
    h, w = field.shape
    ok[:n + 1, :] = False
    ok[h - n - 1:, :] = False
    ok[:, :n + 1] = False
    ok[:, w - n - 1:] = False

    ys, xs = np.nonzero(ok)
    kept = int(ys.size)
    if kept < MIN_BOUNDARY_PX and found >= MIN_BOUNDARY_PX:
        return Thin("edge", f"{found} level-set px found but only {kept} keep a"
                            f" +/-{n}px window inside the viewport - the boundary hugs"
                            " the viewport edge, so the profile would average HUD or"
                            " out-of-viewport pixels")
    if kept < MIN_BOUNDARY_PX:
        return Thin("degenerate", f"only {kept} usable level-set px"
                                  f" (< {MIN_BOUNDARY_PX})")
    inv = 1.0 / mag[ys, xs]
    nx, ny = gx[ys, xs] * inv, gy[ys, xs] * inv
    t = -s[ys, xs] * inv                      # first-order walk onto the 0.5 level
    fine = np.abs(t) <= MAX_SUBPIXEL
    ys, xs, nx, ny, t = ys[fine], xs[fine], nx[fine], ny[fine], t[fine]
    kept = int(ys.size)
    if kept < MIN_BOUNDARY_PX:
        return Thin("degenerate", f"only {kept} level-set px sit within"
                                  f" {MAX_SUBPIXEL:.0f}px of the 0.5 level")
    if kept > MAX_BOUNDARY_PX:
        take = np.linspace(0, kept - 1, MAX_BOUNDARY_PX).astype(np.intp)
        ys, xs, nx, ny, t = ys[take], xs[take], nx[take], ny[take], t[take]

    px = np.stack([xs + t * nx, ys + t * ny], axis=1)
    nvec = np.stack([nx, ny], axis=1)
    # Room a profiled window has BEYOND the +/-n it needs. At zero the far plateau abuts
    # the viewport edge, where box_mean's edge extension fabricates field values out of
    # the last row. The MEDIAN, not the min: the window-fit filter pins the min to 1 by
    # construction, so the min would make every boundary that reaches the edge look
    # equally bad.
    reach = np.minimum(np.minimum(px[:, 0], px[:, 1]),
                       np.minimum(w - 1.0 - px[:, 0], h - 1.0 - px[:, 1]))
    slack = float(np.median(reach)) - n
    # Independent terrain samples: DISTINCT tiles the level set crosses. Terrain albedo
    # is per-tile, so a run of boundary pixels inside one tile is ONE sample, not many.
    tp = max(1, int(round(pitch_px)))
    cell = (ys // tp).astype(np.int64) * (w // tp + 2) + (xs // tp).astype(np.int64)
    ntiles = int(np.unique(cell).size)
    mxy = nvec.mean(axis=0)
    coh = float(np.hypot(mxy[0], mxy[1]))
    angle = float(np.degrees(np.arctan2(mxy[1], mxy[0])))
    spread = float(np.degrees(np.sqrt(max(0.0, -2.0 * np.log(min(max(coh, 1e-6), 1.0))))))
    dev = min(abs(angle - k * 90.0) for k in (-2, -1, 0, 1, 2))
    return Frontier(px, nvec, angle, dev, spread, kept, found, ntiles, r, slack,
                    (float(px[:, 0].mean()), float(px[:, 1].mean())))


def blockify(prof: np.ndarray, at: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """<= MAX_RAMP block means: the printed shape of the ramp, never a verdict."""
    nb = min(MAX_RAMP, len(prof))
    return (np.array([c.mean() for c in np.array_split(prof, nb)], dtype=np.float32),
            np.array([c.mean() for c in np.array_split(at, nb)], dtype=np.float32))


def extract_normal(scene: np.ndarray, fr: Frontier, n: int, pitch_px: float) -> Ramp:
    """Average the luma walk along +/- the LOCAL normal over every boundary pixel."""
    lum = luma(scene).astype(np.float64)
    at = np.arange(n, -n - 1, -1.0)           # +n is the remembered side, so it leads
    ox = fr.px[:, 0][:, None] + fr.nvec[:, 0][:, None] * at[None, :]
    oy = fr.px[:, 1][:, None] + fr.nvec[:, 1][:, None] * at[None, :]
    walk = sample(lum, ox, oy)
    prof = walk.mean(axis=0)
    ramp, ramp_at = blockify(prof, at)
    p = max(1, min(int(round(pitch_px)), len(prof) // 3))
    # How well the plateau LEVELS converged, over INDEPENDENT terrain samples. Measured
    # on the two plateaus only, never across the transition: inside the band different
    # boundary pixels legitimately sit at different cross-fade phases, and a defect
    # localised at the boundary would otherwise inflate its own error bar and let the
    # caveat below dismiss a real finding. Purely reported - it never enters a threshold,
    # because a floor that absorbed it could hide a defect. A margin at or below this
    # size is a statement about one draw of the terrain, not about the composite.
    ends = np.concatenate([walk[:, :p], walk[:, -p:]], axis=1)
    sem = float((ends.std(axis=0) / np.sqrt(max(fr.ntiles, 1))).max())
    return Ramp("normal", prof, at, ramp, ramp_at,
                float(prof[:p].mean()), float(prof[-p:].mean()), p, sem)


def band_of(a: np.ndarray, axis: str, band: tuple[float, float]) -> np.ndarray:
    """Slice the NON-profiled axis to `band` and orient the result as (band, L)."""
    lo, hi = band_span(a.shape, axis, band)
    return a[lo:hi, :] if axis == "x" else a[:, lo:hi].T


def locate(m15: np.ndarray, axis: str,
           band: tuple[float, float]) -> Located | dict[str, float]:
    """Find the remembered-fraction 0.5 crossing, or return the class fractions."""
    cls = classify(m15)
    rem = band_of(cls["remembered"].astype(np.float32), axis, band).mean(axis=0)
    sn = band_of(seen_mask(cls).astype(np.float32), axis, band).mean(axis=0)
    tot = rem + sn
    # MIN_CLASS_WEIGHT, not > 0: a column holding two stray remembered pixels and no
    # seen ones has ratio 1.0, so `> 0` manufactures a 0.5 crossing wherever such a
    # column happens to sit next to an empty one. That is how this fallback previously
    # reported an x-boundary on a capture whose frontier is HORIZONTAL.
    ok = np.flatnonzero(tot >= MIN_CLASS_WEIGHT)
    if ok.size < 2:
        return {k: float(band_of(v.astype(np.float32), axis, band).mean())
                for k, v in cls.items()}
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


def extract_axis(scene: np.ndarray, axis: str, band: tuple[float, float], loc: Located,
                 n: int, pitch_px: float, org: int) -> Ramp:
    """Fallback: window the band-averaged luma around the boundary, remembered first."""
    strip = band_of(luma(scene), axis, band)
    prof = strip.mean(axis=0)
    # Independent samples are TILE ROWS in the strip, not pixel rows: terrain albedo is
    # per-tile, so sqrt(row count) would overstate the averaging by sqrt(pitch).
    sem_all = strip.std(axis=0) / np.sqrt(max(strip.shape[0] / max(pitch_px, 1.0), 1.0))
    lo, hi = loc.idx - n, loc.idx + n + 1
    raw = prof[lo:hi]
    at = np.arange(lo, hi, dtype=np.float64) + org     # report in frame coordinates
    sem = float(sem_all[lo:hi].max())
    if not loc.rem_first:
        raw, at = raw[::-1], at[::-1]
    ramp, ramp_at = blockify(raw, at)
    p = max(1, min(int(round(pitch_px)), len(raw) // 3))
    return Ramp(axis, raw, at, ramp, ramp_at,
                float(raw[:p].mean()), float(raw[-p:].mean()), p, sem)


def where(r: Ramp, v: float) -> str:
    """A profile position: a signed normal offset, or an absolute frame axis pixel."""
    return f"{v:+.0f}px" if r.label == "normal" else f"{r.label}={v:.0f}"


def check(r: Ramp, tol: float) -> tuple[list[str], list[str], str]:
    """The verdict lines, the subset of them that failed, and the measured floors.

    Direction-agnostic on purpose: at night `mem_dim` floors remembered terrain ABOVE
    genuinely dark visible terrain, so the remembered plateau is the BRIGHT one and a
    gate that assumed a rising ramp would be grading the sign of the contrast rather
    than the shape of the ramp.
    """
    rising = r.vis >= r.rem
    sign = 1.0 if rising else -1.0
    dark, bright = (("remembered", r.rem), ("visible", r.vis)) if rising else \
        (("visible", r.vis), ("remembered", r.rem))
    p = r.plateau_px
    ends = [r.prof[:p], r.prof[-p:]]
    # Self-calibrating floors. The plateaus are flat by construction, so their step
    # magnitude IS this profile's residual noise and their deviation from their own mean
    # IS its level uncertainty. Doubled, per the harness convention that a threshold is
    # a measured null x2 and never a constant. On the normal profile both shrink as
    # boundary pixels are averaged, which is the point: the gate tightens itself instead
    # of being loosened by hand.
    step_n = np.concatenate([np.abs(np.diff(e)) for e in ends])
    dev_n = np.concatenate([np.abs(e - e.mean()) for e in ends])
    f_step = 2.0 * float(step_n.max()) if step_n.size else 0.0
    f_dev = 2.0 * float(dev_n.max()) if dev_n.size else 0.0
    lim_s, lim_d = max(tol, f_step), max(tol, f_dev)

    d = np.diff(r.prof) * sign
    k = int(np.argmin(d)) if d.size else 0
    worst = float(d[k]) if d.size else 0.0
    wat = (r.at[k] + r.at[k + 1]) / 2.0 if d.size else float(r.at[0])
    dip = float(r.prof.min()) - dark[1]
    over = float(r.prof.max()) - bright[1]
    lines = [
        f"monotonic: worst step {num(worst)} at {where(r, wat)}"
        f" (floor {f_step:.1f}, tol {tol:.1f}) {'OK' if worst >= -lim_s else 'VIOLATION'}",
        f"dip below {dark[0]} plateau {dark[1]:.1f}: profile min {r.prof.min():.1f}"
        f" margin {num(dip)} (floor {f_dev:.1f}, tol {tol:.1f})"
        f" {'OK' if dip >= -lim_d else 'VIOLATION'}",
        f"overshoot above {bright[0]} plateau {bright[1]:.1f}: profile max"
        f" {r.prof.max():.1f} margin {num(over)} (floor {f_dev:.1f}, tol {tol:.1f})"
        f" {'OK' if over <= lim_d else 'VIOLATION'}",
    ]
    bad: list[str] = []
    if worst < -lim_s:
        bad.append(f"non-monotonic step {num(worst)} at {where(r, wat)}")
    if dip < -lim_d:
        bad.append(f"dip {num(dip)} below {dark[0]} plateau {dark[1]:.1f}"
                   f" at {where(r, float(r.at[int(np.argmin(r.prof))]))}")
    if over > lim_d:
        bad.append(f"overshoot {num(over)} above {bright[0]} plateau {bright[1]:.1f}"
                   f" at {where(r, float(r.at[int(np.argmax(r.prof))]))}")
    return lines, bad, f"step {f_step:.1f} / level {f_dev:.1f}; sem<={r.sem:.1f}"


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
    ap.add_argument("--viewport",
                    help="x,y,w,h of the MAP viewport; decimals are a fraction of the"
                         " frame (as in tools/light_mode_check.py). Defaults to the"
                         " whole frame, which includes the HUD - the HUD is drawn with"
                         " debug_mode forced to 0 and is not part of the composite, so"
                         " pass e.g. 0.25,0.12,0.55,0.80 for any real capture")
    ap.add_argument("--profile", choices=("normal", "axis"), default="normal",
                    help="normal (default) profiles along the local boundary normal and"
                         " is THE gate; axis forces the legacy screen-axis fallback,"
                         " which is only valid for a straight axis-aligned boundary and"
                         " is otherwise a diagnostic")
    ap.add_argument("--axis", choices=("x", "y"), default="x",
                    help="axis for the FALLBACK profile, used when the boundary field"
                         " has no usable gradient; also selects which axis --band"
                         " restricts. The normal profile is the gate.")
    ap.add_argument("--band", default="0.35,0.65",
                    help="the strip, inside the viewport, that the FALLBACK axis profile"
                         " averages over. Ignored by the normal profile, which averages"
                         " the whole level set inside the viewport.")
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
        full = {t: (load_rgb(p), load_rgb(s)) for t, p, s in pairs}
    except (OSError, ValueError) as e:
        print(f"FAIL cannot read inputs: {e}")
        return 2

    # 1. Admissibility, before a single pixel is measured. Geometry first, because the
    #    viewport is resolved against the frame and a mismatched pair would crop two
    #    different regions.
    for t, (fm, fs) in full.items():
        if size_of(fm) != size_of(fs):
            print(f"FAIL pair {t} mode15 {size_of(fm)} != scene {size_of(fs)}"
                  " - not the same frame geometry")
            return 1
    if len(pairs) == 2 and size_of(full["A"][1]) != size_of(full["B"][1]):
        print(f"FAIL pair sizes differ {size_of(full['A'][1])} vs"
              f" {size_of(full['B'][1])} - not comparable")
        return 1
    try:
        vps = {t: resolve_viewport(a.viewport, size_of(fm)) for t, (fm, _) in full.items()}
    except ValueError as e:
        print(f"FAIL {e}")
        return 2
    m15 = {t: crop(fm, vps[t]) for t, (fm, _) in full.items()}
    scn = {t: crop(fs, vps[t]) for t, (_, fs) in full.items()}
    pit = {t: pitch(s)[0] for t, _, s in pairs}
    print("1) admissible: " + " | ".join(
        f"{t} frame {size_of(full[t][0])[0]}x{size_of(full[t][0])[1]} viewport"
        f" {vps[t][0]},{vps[t][1]} {vps[t][2]}x{vps[t][3]} pitch {pit[t]:.2f}px"
        for t, _, _ in pairs))
    junk = [t for t, _, _ in pairs if pit[t] > size_of(full[t][1])[0] / MIN_TILES_ACROSS]
    if junk:
        # Observed: on an untextured crop the estimator's argmax saturates at its lowest
        # bin and returns the band edge, which is not a tile pitch at all.
        print(f"   WARN pitch estimate unreliable for {','.join(junk)} (no periodic"
              f" peak; > 1/{MIN_TILES_ACROSS} of the frame) - window size and the"
              " zoom gate below are not trustworthy")
    if len(pairs) == 2:
        rel = abs(pit["A"] - pit["B"]) / ((pit["A"] + pit["B"]) / 2.0)
        if rel > PITCH_TOL_FRAC:
            print(f"FAIL scene tile pitch {pit['A']:.2f} vs {pit['B']:.2f} px differs"
                  f" {rel * 100:.1f}% > {PITCH_TOL_FRAC * 100:.0f}% - zoom drift,"
                  " re-capture")
            return 1
        print(f"   scene pitch agrees within {rel * 100:.1f}%")

    # 2. Window ~3 tiles per side, sized from the measured pitch, shared across pairs.
    n = min([int(round(TILES_PER_SIDE * pit[t])) for t, _, _ in pairs]
            + [min(vps[t][2], vps[t][3]) // 2 - 2 for t, _, _ in pairs])
    if n < 3:
        print(f"FAIL usable half-window {n} px - viewport or tile pitch degenerate")
        return 1

    # 3. Boundary geometry. The normal profile gates; the axis profile is only the
    #    fallback for a degenerate gradient, and both pairs must use the SAME one or the
    #    B-A delta compares two different measurements.
    mode = a.profile
    frs: dict[str, Frontier] = {}
    if mode == "normal":
        got = {t: frontier(m15[t], pit[t], n) for t, _, _ in pairs}
        stop = [(t, v) for t, v in got.items()
                if isinstance(v, Thin) and v.kind in ("none", "edge")]
        if stop:
            for t, v in stop:
                cf = classify(m15[t])
                print(f"   pair {t} viewport class fractions: "
                      + " ".join(f"{k}={float(m.mean()):.3f}" for k, m in cf.items()))
                print(f"FAIL pair {t} {v.detail}")
            return 1
        weak = ",".join(t for t, v in got.items() if isinstance(v, Thin))
        if weak:
            print(f"   WARN boundary field degenerate for {weak}"
                  f" ({got[weak.split(',')[0]].detail}) - falling back to the"
                  f" {a.axis}-axis profile, which is only valid for a straight"
                  " axis-aligned boundary")
            mode = "axis"
        else:
            frs = {t: v for t, v in got.items() if isinstance(v, Frontier)}

    ramps: dict[str, Ramp] = {}
    locs: dict[str, Located] = {}
    if mode == "normal":
        ramps = {t: extract_normal(scn[t], frs[t], n, pit[t]) for t, _, _ in pairs}
    else:
        for t, _, _ in pairs:
            loc = locate(m15[t], a.axis, band)
            if isinstance(loc, dict):
                print(f"   pair {t} viewport class fractions: "
                      + " ".join(f"{k}={v:.3f}" for k, v in loc.items()))
                print("FAIL no seen/remembered boundary found inside the viewport")
                return 1
            locs[t] = loc
        n = min([n] + [locs[t].max_n for t, _, _ in pairs])
        if n < 3:
            print(f"FAIL boundary too close to the VIEWPORT edge: usable half-window"
                  f" {n} px - widening it would average HUD pixels")
            return 1
        ramps = {t: extract_axis(scn[t], a.axis, band, locs[t], n, pit[t],
                                 vps[t][0] if a.axis == "x" else vps[t][1])
                 for t, _, _ in pairs}

    # 4. Report.
    ln = 2
    bad: dict[str, list[str]] = {}
    tight: list[str] = []
    straight: list[str] = []
    for t, _, _ in pairs:
        r = ramps[t]
        vx, vy = vps[t][0], vps[t][1]
        if mode == "normal":
            f = frs[t]
            cx, cy = f.centroid
            print(f"{ln}) {t} boundary normal {f.angle:+.1f}deg (axis dev"
                  f" {f.axis_dev:.1f}deg, 1/cos"
                  f" {1.0 / max(float(np.cos(np.radians(f.axis_dev))), 1e-6):.3f},"
                  f" spread {f.spread:.1f}deg); {len(f.px)} of {f.found} boundary px"
                  f" over {f.ntiles} tiles; centroid ({cx + vx:.0f},{cy + vy:.0f});"
                  f" blur r={f.blur_r}px; edge slack {f.slack:.0f}px")
            if f.slack < f.blur_r:
                tight.append(t)
            if f.spread < MIN_SPREAD_DEG and r.sem > a.tol:
                straight.append(t)
        else:
            loc = locs[t]
            org = vx if a.axis == "x" else vy
            print(f"{ln}) {t} FALLBACK {a.axis}-axis boundary:"
                  f" {a.axis}={loc.pos + org:.1f} ({loc.crossings} crossing(s),"
                  f" remembered on the {'low' if loc.rem_first else 'high'} side)")
        print(f"{ln + 1}) {t} window +/-{n}px = {n / pit[t]:.1f} tiles/side; plateaus"
              f" remembered {r.rem:.1f} | visible {r.vis:.1f} (outer {r.plateau_px}px)")
        print(f"{ln + 2}) {t} ramp[{len(r.ramp)}] remembered->visible: {fmt(r.ramp)}")
        lines, bad[t], floors = check(r, a.tol)
        print(f"{ln + 3}) {t} floors (measured null x2): {floors}")
        for j, s in enumerate(lines):
            print(f"{ln + 4 + j}) {t} {s}")
        ln += 4 + len(lines)

    # 5. Before/after: a delta here is expected, only a broken ramp fails.
    if len(pairs) == 2:
        d = ramps["B"].ramp - ramps["A"].ramp
        print(f"{ln}) delta B-A per block: " + " ".join(num(x) for x in d))
        print(f"{ln + 1}) note: a non-zero delta in the band is EXPECTED (max composite"
              " -> lerp cross-fade); only non-monotonicity, a dip or an overshoot fails")

    gate = "normal-profile" if mode == "normal" else f"{a.axis}-axis FALLBACK"
    if tight:
        # A window that fits by a few rows is measuring a plateau whose neighbours are
        # the viewport edge, where box_mean's edge extension repeats the last row into
        # the blur. The verdict still stands on what was sampled, but it is not a clean
        # null and saying so is cheaper than someone else re-deriving it.
        print(f"   CAVEAT pair(s) {','.join(tight)}: the far plateau sits within one"
              " blur radius of the viewport edge, so its level is partly the edge"
              " extension of the smoothing. NOT authoritative - re-capture with the"
              " frontier nearer the viewport centre.")
    if straight:
        print(f"   CAVEAT pair(s) {','.join(straight)}: normal spread <"
              f" {MIN_SPREAD_DEG:.0f}deg, i.e. the frontier is effectively ONE straight"
              " line, so every sample at a given offset lands on the same screen line"
              " and terrain albedo is correlated with offset. Averaging cannot separate"
              " a bright terrain band from a real overshoot here. NOT authoritative"
              " - re-capture with a curved or closed frontier.")
    fails = [f"{t}: {m}" for t, ms in bad.items() for m in ms]
    caveat = " [see CAVEAT]" if tight or straight else ""
    if fails:
        print(f"FAIL {gate} gate: " + "; ".join(fails) + caveat)
        return 1
    print(f"PASS {gate} gate: ramp monotonic between plateaus, no dip or overshoot"
          f" (tol {a.tol:.1f}, floors measured per pair)" + caveat)
    return 0


if __name__ == "__main__":
    sys.exit(main())
