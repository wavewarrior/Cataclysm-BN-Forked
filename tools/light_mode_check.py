#!/usr/bin/env python3
"""Gate the sprite light_mode classification from a debug-view-16 capture.

Exists because every brightness-based verification in this project has been faked at
least once by capture nondeterminism, not by the shader under test. Zoom keypresses
sent back to back get dropped, so two runs of the SAME scenario land on different zoom
levels - measured tile pitch 8.02 px vs 42.86 px, which moved whole-frame mean luma by
19.7 and manufactured a 67% "signal" out of nothing. The window itself came up
1920x1080 and 2560x1440 on consecutive launches. Any luma profile or pixel diff must
therefore be gated on matching resolution and tile pitch before it means anything
(see tools/shadow_zoom_check.py).

Debug view 16 sidesteps all of that. It replaces each drawn fragment with a flat
CATEGORICAL hue naming the branch the composite took - red `unlit`, green `gpu_lit`,
blue `memory` - with no shading at all. Class membership does not depend on zoom, on
resolution, on tile pitch, on time of day, or on where the avatar happens to stand: a
tile is classified correctly or it is not. So this tool answers "is every tile
classified as intended" with a question luma cannot ask, and in particular catches the
fail-bright defect this work removes, where a visible pitch-dark tile was classified
`unlit` and rendered at full albedo.

Prints a handful of lines and a PASS/FAIL verdict; never an image. Exit 0 = gate
passed, 1 = gate failed, 2 = usage or IO error.

Usage:
  python tools/light_mode_check.py CAPTURE.png [--viewport x,y,w,h] [--expect-memory]
      [--no-expect-unlit] [--max-unclassified 0.02]
  python tools/light_mode_check.py A.png B.png        # paired same-state null
"""
from __future__ import annotations

import argparse
import sys

import numpy as np
from PIL import Image

# Populations, in report order. `undrawn` is a legitimate class (nothing was drawn at
# all), which is why it must never dilute the unclassified denominator.
CLASSES = ("unlit", "gpu_lit", "memory", "undrawn", "unclassified")

# Membership is decided by DOMINANT CHANNEL, not by absolute thresholds, and this is a
# measured decision rather than a convenience. The shader emits pure 1.0/0.0 channels,
# but those values do not survive to the screenshot: the world target goes through a
# post-tonemap ASC-CDL grade (and bloom / chromatic aberration) before presentation.
# Positive control, captured from the running game by forcing debug view 16's `gpu_lit`
# branch to pure white: (1,1,1) arrived as rgb(192.6, 190.9, 195.0) - a ~0.755
# compression that is very nearly channel-neutral. So an absolute `>= 200` test is
# wrong by construction, while the HUE ORDER is preserved with enormous margin: real
# `gpu_lit` pixels measured rgb(7, 193, 2), i.e. green ahead of the runner-up by ~27x.
#
# Requiring the dominant channel to beat BOTH others by DOMINANCE is therefore the
# invariant that actually holds. It is safe against cross-class blending because debug
# view 16 forces alpha to 1 (see `dbg_opaque` in sprite.frag.hlsl), so stacked quads
# overwrite rather than mix; anything still ambiguous is reported as unclassified.
#
# DOM_FLOOR closes a hole in the ratio test on its own: when the two runner-up channels
# are near zero, `DOMINANCE * max(g, b)` collapses toward 0, so a dim fringe pixel like
# rgb(13, 0, 0) - barely above BLACK - would satisfy "red dominates" and be counted as
# `unlit`. That is a false positive on the single assertion this tool exists to make.
# Real class pixels measured ~193 in their dominant channel, so a floor of 60 sits far
# below any genuine class member and far above bloom / chromatic-aberration fringing.
DOMINANCE, DOM_FLOOR, BLACK = 2.0, 60, 12

# Share of the unclassified population held by its single most common triple, above
# which the scatter is one systematic wrong colour rather than blends between quads.
SYSTEMATIC_SHARE = 0.25

# A categorical frame of an unchanged scene must reproduce exactly, so the paired null
# tolerance is tight: 1 percentage point of the viewport per class.
NULL_TOLERANCE_PP = 1.0


def resolve_viewport(spec: str | None, size: tuple[int, int]) -> tuple[int, int, int, int]:
    """A viewport written with decimals is a fraction of the frame, matching vv's `rect`.
    The install has come up at two different client sizes on this box, so an absolute
    viewport is a latent silent-wrong-region bug."""
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


def load(path: str, spec: str | None) -> tuple[np.ndarray, tuple[int, int], tuple[int, int, int, int]]:
    with Image.open(path) as im:
        frame = im.size
        vp = resolve_viewport(spec, frame)
        a = np.asarray(im.convert("RGB"), dtype=np.uint8)
    x, y, w, h = vp
    return a[y:y + h, x:x + w], frame, vp


def classify(a: np.ndarray) -> tuple[dict[str, int], np.ndarray]:
    """Categorical membership only - no luminance is computed anywhere in this file."""
    r, g, b = (a[..., i].astype(np.int32) for i in range(3))
    undrawn = a.max(axis=2) <= BLACK
    lit = ~undrawn
    m = {
        "unlit": lit & (r >= DOM_FLOOR) & (r >= DOMINANCE * np.maximum(g, b)),
        "gpu_lit": lit & (g >= DOM_FLOOR) & (g >= DOMINANCE * np.maximum(r, b)),
        "memory": lit & (b >= DOM_FLOOR) & (b >= DOMINANCE * np.maximum(r, g)),
        "undrawn": undrawn,
    }
    unclassified = ~(m["unlit"] | m["gpu_lit"] | m["memory"] | m["undrawn"])
    counts = {k: int(v.sum()) for k, v in m.items()}
    counts["unclassified"] = int(unclassified.sum())
    return counts, unclassified


def offenders(a: np.ndarray, mask: np.ndarray, n: int = 3) -> tuple[list[tuple[tuple[int, int, int], int]], int]:
    """The N most common unclassified triples, plus how many distinct triples exist."""
    px = a[mask].astype(np.int32)
    if px.size == 0:
        return [], 0
    key = (px[:, 0] << 16) | (px[:, 1] << 8) | px[:, 2]
    vals, cnt = np.unique(key, return_counts=True)
    top = [(int(vals[i]), int(cnt[i])) for i in np.argsort(cnt)[::-1][:n]]
    return [(((v >> 16) & 255, (v >> 8) & 255, v & 255), c) for v, c in top], int(vals.size)


def pct(count: int, total: int) -> float:
    return 100.0 * count / max(1, total)


def dim_band(a: np.ndarray) -> dict[str, int]:
    """Population between BLACK and DOM_FLOOR, split by dominant hue.

    Closes the one way `unlit 0.00%` could be a FALSE NEGATIVE. DOM_FLOOR exists to
    stop bloom fringe being counted as a class, but it cuts both ways: a genuinely
    `unlit` pixel rendered dim would fall below the floor and be reported as
    `unclassified` rather than as `unlit`, so a clean 0.00% would not actually mean
    "no unlit tiles". Real captures do reach here -- measured green-dominant pixels
    as low as 13 -- so the risk is not hypothetical.

    Reporting the dim band BY DOMINANT HUE settles it without a threshold argument:
    if the band holds no red-dominant pixel, then no unlit tile is hiding in it at
    any brightness, and the 0.00% claim is sound.
    """
    r, g, b = (a[..., i].astype(np.int32) for i in range(3))
    peak = a.max(axis=2)
    band = (peak > BLACK) & (peak <= DOM_FLOOR)
    return {
        "total": int(band.sum()),
        "unlit": int((band & (r >= DOMINANCE * np.maximum(g, b))).sum()),
        "gpu_lit": int((band & (g >= DOMINANCE * np.maximum(r, b))).sum()),
        "memory": int((band & (b >= DOMINANCE * np.maximum(r, g))).sum()),
    }


def report(counts: dict[str, int], total: int, tag: str = "") -> None:
    body = "  ".join(f"{k} {pct(counts[k], total):.2f}%" for k in CLASSES)
    print(f"{tag}{body}")


def unclassified_frac(counts: dict[str, int], total: int) -> tuple[float, int]:
    nonblack = total - counts["undrawn"]
    return (counts["unclassified"] / nonblack if nonblack else 0.0), nonblack


def verdict(reasons: list[str], ok_note: str) -> int:
    if reasons:
        print(f"FAIL {'; '.join(reasons)}")
        return 1
    print(f"PASS {ok_note}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(prog="light_mode_check", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture", nargs="+", help="debug-view-16 PNG; a second one runs the paired null")
    ap.add_argument("--viewport", help="x,y,w,h - decimals are a fraction of the frame")
    ap.add_argument("--expect-memory", action="store_true",
                    help="require a non-zero memory population")
    ap.add_argument("--no-expect-unlit", action="store_true",
                    help="assert the viewport contains no unlit pixels")
    # 0.05 is a MEASURED residual, not a fudge. Every removable cause was removed at
    # source: view 16 forces alpha 1 so stacked quads overwrite instead of mixing, and
    # bloom, volumetric fog and the spatial post effects are skipped for replace-mode
    # views. That took a real capture from 28% unclassified to 2.75%. What is left is
    # atlas bilinear filtering at quad seams (1224 distinct triples, top holding 9.8%,
    # i.e. a thin scatter), which is irreducible at non-integer tile scale.
    ap.add_argument("--max-unclassified", type=float, default=0.05,
                    help="allowed unclassified fraction of non-black pixels (default 0.05)")
    args = ap.parse_args()

    if len(args.capture) > 2:
        print("usage error: at most two captures", file=sys.stderr)
        return 2
    try:
        frames = [load(p, args.viewport) for p in args.capture]
    except (OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    reasons: list[str] = []
    stats = []
    for path, (a, frame, vp) in zip(args.capture, frames):
        h, w = a.shape[:2]
        print(f"frame {frame[0]}x{frame[1]}  viewport {vp[0]},{vp[1]} {w}x{h} = {w * h} px  {path}")
        stats.append((a, w * h, *classify(a)))

    for path, (a, total, counts, unmask) in zip(args.capture, stats):
        tag = "" if len(stats) == 1 else f"{path}: "
        report(counts, total, tag)
        frac, nonblack = unclassified_frac(counts, total)
        print(f"{tag}unclassified vs non-black: {frac:.4f} "
              f"({counts['unclassified']} / {nonblack} px, limit {args.max_unclassified:.4f})")
        if frac > args.max_unclassified:
            top, distinct = offenders(a, unmask)
            for rgb, c in top:
                print(f"  offender rgb{rgb} x{c} ({pct(c, counts['unclassified']):.2f}% of unclassified)")
            share = top[0][1] / counts["unclassified"] if top else 0.0
            if share >= SYSTEMATIC_SHARE:
                print(f"  {distinct} distinct triples, top holds {share * 100:.1f}% -> "
                      f"systematic wrong colour, not antialiasing: shader bug")
            else:
                print(f"  {distinct} distinct triples, top holds {share * 100:.1f}% -> "
                      f"thin scatter of blends, looks like antialiasing between quads")
            reasons.append(f"{tag}unclassified {frac:.4f} > {args.max_unclassified:.4f} of non-black")
        db = dim_band(a)
        print(f"{tag}dim band ({BLACK}..{DOM_FLOOR}] {db['total']} px by dominant hue: "
              f"unlit {db['unlit']}  gpu_lit {db['gpu_lit']}  memory {db['memory']}")
        if args.no_expect_unlit and db["unlit"]:
            # A red-dominant pixel below the floor is an unlit tile the class counts
            # could not see, so `unlit 0.00%` would be a false negative.
            reasons.append(f"{tag}{db['unlit']} red-dominant px hide in the dim band, "
                           f"so the unlit count is not trustworthy")
        if args.no_expect_unlit and counts["unlit"]:
            reasons.append(f"{tag}unlit population {pct(counts['unlit'], total):.2f}% "
                           f"({counts['unlit']} px) but --no-expect-unlit given")
        if args.expect_memory and not counts["memory"]:
            reasons.append(f"{tag}memory population is 0 but --expect-memory given")

    if len(stats) == 1:
        return verdict(reasons, "every population is a known light_mode class within limits")

    (a0, t0, c0, _), (a1, t1, c1, _) = stats
    f0, f1 = frames[0][1], frames[1][1]
    if f0 != f1 or a0.shape != a1.shape:
        reasons.append(f"frames {f0[0]}x{f0[1]} vs {f1[0]}x{f1[1]}, viewports "
                       f"{a0.shape[1]}x{a0.shape[0]} vs {a1.shape[1]}x{a1.shape[0]} - "
                       f"differing resolutions, not comparable, re-capture")
        print("delta(pp) n/a - resolution or viewport mismatch")
        return verdict(reasons, "")
    deltas = {k: pct(c1[k], t1) - pct(c0[k], t0) for k in CLASSES}
    print("delta(pp) " + "  ".join(f"{k} {deltas[k]:+.2f}" for k in CLASSES))
    worst = max(deltas, key=lambda k: abs(deltas[k]))
    if abs(deltas[worst]) >= NULL_TOLERANCE_PP:
        reasons.append(f"paired null broken: {worst} moved {deltas[worst]:+.2f}pp "
                       f"(limit {NULL_TOLERANCE_PP:.2f}pp)")
    return verdict(reasons, f"paired null holds, worst class {worst} {deltas[worst]:+.2f}pp")


if __name__ == "__main__":
    sys.exit(main())
