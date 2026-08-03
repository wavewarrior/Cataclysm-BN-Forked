#!/usr/bin/env python3
"""Offline A/B of the SDF soft-shadow march in data/shaders/lighting/src/shadow_trace.hlsl.

Why this exists
---------------
"Are wall shadows tight, and do corners smear?" is a question about shadow
GEOMETRY, which the shader math fully determines. Answering it by driving the
game proved unreliable: the build runs windowed-borderless on a virtual display
whose mode changes between launches, and the scripted zoom / set-time menu steps
can drop a keypress. A cross-launch null with IDENTICAL shaders came back 67%
changed with a -20 luma bias, i.e. harness noise dwarfed the effect being
measured. So the march is reproduced here exactly and evaluated against a
synthetic SDF: deterministic, no game required, and it isolates the one thing
under test.

Mirrors the HLSL step for step, including the parts that shape the artefact:
  * SDF read through the same subcell grid and bilinear filter, so the field is
    as coarse - and as non-Lipschitz - as the real one;
  * the same 0.15 minimum march step and the same sd < 0.05 hard-hit test;
  * distances in TILE units, as jfa_resolve writes them.

Run:  python tools/shadow_march_ab.py
"""

from __future__ import annotations

import math

SDF_SS = 8          # subcells per tile; must match jfa_shared.hlsl
MIN_STEP = 0.15     # march step floor, matches shadow_trace.hlsl
HARD_HIT = 0.05     # sd below this = fully occluded


class Scene:
    """Axis-aligned wall rectangles in tile space, sampled like the GPU SDF."""

    def __init__(self, rects):
        self.rects = rects

    def _exact(self, x, y):
        best = 1e9
        for x0, y0, x1, y1 in self.rects:
            dx = max(x0 - x, 0.0, x - x1)
            dy = max(y0 - y, 0.0, y - y1)
            best = min(best, math.hypot(dx, dy))
        return best

    def texel(self, ix, iy):
        # Subcell centre - the convention jfa_resolve / sdf_bilinear both use.
        return self._exact((ix + 0.5) / SDF_SS, (iy + 0.5) / SDF_SS)

    def bilinear(self, x, y):
        gx, gy = x * SDF_SS - 0.5, y * SDF_SS - 0.5
        x0, y0 = math.floor(gx), math.floor(gy)
        wx, wy = gx - x0, gy - y0
        a, b = self.texel(x0, y0), self.texel(x0 + 1, y0)
        c, d = self.texel(x0, y0 + 1), self.texel(x0 + 1, y0 + 1)
        return (a * (1 - wx) + b * wx) * (1 - wy) + (c * (1 - wx) + d * wx) * wy


def march(scene, origin, light, k, steps, *, aaltonen, ref_receiver):
    """One shadow ray.

    aaltonen     - apply the between-samples closest-approach correction.
    ref_receiver - key the penumbra to distance-from-RECEIVER (textbook IQ)
                   instead of the remaining distance to the light (the legacy
                   point-light reference in sprite.frag / gi_field).
    """
    ox, oy = origin
    dx, dy = light[0] - ox, light[1] - oy
    dist = math.hypot(dx, dy)
    if dist < 1e-6:
        return 1.0
    dx, dy = dx / dist, dy / dist

    shadow = 1.0
    t = min(0.3, dist * 0.5)
    prev_sd = 1e10
    prev_step = 1e10
    for _ in range(steps):
        if t >= dist - 0.4:
            break
        sd = scene.bilinear(ox + dx * t, oy + dy * t)
        if sd < HARD_HIT:
            return 0.0
        if aaltonen:
            y = (prev_step * prev_step + sd * sd - prev_sd * prev_sd) / (2.0 * prev_step)
            if 0.0 < y < sd:
                d = math.sqrt(sd * sd - y * y)
            else:
                y, d = 0.0, sd
        else:
            y, d = 0.0, sd
        t_hit = t - y
        denom = max(t_hit, 0.01) if ref_receiver else max(dist - t_hit, 0.01)
        shadow = min(shadow, k * d / denom)
        prev_sd = sd
        prev_step = max(sd, MIN_STEP)
        t += prev_step
    return max(0.0, min(1.0, shadow))


def sweep(scene, light, y_recv, xs, cfg, k, steps=48):
    return [(x, march(scene, (x, y_recv), light, k, steps, **cfg)) for x in xs]


def penumbra_width(profile):
    """Width in tiles of the 10%..90% part of one lit->shadow transition."""
    lit = [x for x, v in profile if v >= 0.9]
    dark = [x for x, v in profile if v <= 0.1]
    if not lit or not dark:
        return float("nan")
    # Left-hand edge of the wedge: rightmost fully-lit x below the darkest run.
    d0 = min(dark)
    lit_left = [x for x in lit if x < d0]
    if not lit_left:
        return float("nan")
    return d0 - max(lit_left)


CFGS = {
    "legacy   (no correction, ref=light)": dict(aaltonen=False, ref_receiver=False),
    "Aaltonen (correction,    ref=light)": dict(aaltonen=True, ref_receiver=False),
    "textbook (correction,    ref=recv) ": dict(aaltonen=True, ref_receiver=True),
}
# The textbook reference redefines what k means (penumbra half-angle 1/k about
# the ray), so it is shown at the gain that restores comparable sharpness.
GAIN = {"legacy   (no correction, ref=light)": 1.0,
        "Aaltonen (correction,    ref=light)": 1.0,
        "textbook (correction,    ref=recv) ": 4.0}

GAPS = (0.5, 1.0, 2.0, 4.0, 7.0)


def main():
    wall = Scene([(-3.5, -7.5, 3.5, -6.5)])   # straight wall, two sharp ends
    light = (0.0, -14.0)
    xs = [(-800 + i) / 100.0 for i in range(1600)]

    print("PENUMBRA WIDTH vs RECEIVER DISTANCE BELOW THE WALL")
    print("A physically-behaved shadow is TIGHT where it meets the occluder and")
    print("fans out with distance. A width that barely changes with distance is")
    print("the signature of keying the penumbra to the wrong end of the ray.")
    print()
    print("   " + " " * 38 + "gap below wall (tiles)")
    print("   " + " " * 38 + "  ".join(f"{g:6.1f}" for g in GAPS) + "   far/near")
    for label, cfg in CFGS.items():
        k = 8.0 * GAIN[label]
        widths = [penumbra_width(sweep(wall, light, -7.0 + g, xs, cfg, k)) for g in GAPS]
        near, far = widths[0], widths[-1]
        ratio = far / near if near and near > 0 and not math.isnan(near) else float("nan")
        print(f"  {label} k={k:4.0f}  "
              + "  ".join(f"{w:6.2f}" for w in widths)
              + f"   {ratio:6.2f}")

    print()
    print("CORNER: sweep just past a convex corner (the case the correction targets)")
    print("Banding reads as direction reversals along an otherwise monotonic ramp.")
    corner = Scene([(-0.5, -7.5, 3.5, -6.5), (2.5, -6.5, 3.5, -2.5)])
    cxs = [(-200 + i * 4) / 100.0 for i in range(150)]
    for label, cfg in CFGS.items():
        k = 8.0 * GAIN[label]
        vals = [v for _, v in sweep(corner, light, -1.0, cxs, cfg, k)]
        rev = sum(1 for i in range(1, len(vals) - 1)
                  if (vals[i] - vals[i - 1]) * (vals[i + 1] - vals[i]) < -1e-9)
        print(f"  {label} k={k:4.0f}  reversals={rev:3d}  "
              f"min={min(vals):.3f} max={max(vals):.3f}")


if __name__ == "__main__":
    main()
