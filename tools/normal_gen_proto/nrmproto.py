"""Offline prototype of procedural normal-atlas generation for BN tilesets.

Implements the "beveling" technique from arXiv:2212.09692 sec II-D, which that survey
found to be the only fully-automatic method producing coherent geometry for pixel art:

    dual binary masks -> euclidean distance transform -> weighted merge
    -> gaussian smooth -> Sobel -> normal

Mask 1 (external): the alpha silhouette.
Mask 2 (internal): colour contours INSIDE the silhouette. This is the mask that matters
for BN terrain, because a wall tile is fully opaque -- mask 1 degenerates to "distance to
tile border" and would stamp an identical pyramid on every tile.

Two deviations from the paper, both forced by measurement on this tileset:

  * 3x3 MEDIAN before contour detection. UnDeadPeople concrete walls are 1px
    noise/ordered dither; a per-pixel colour delta saturates on them (measured 0.45-0.60
    edge density -> the height map is mush). A median annihilates 1px checkerboard by
    SHAPE while preserving multi-pixel boundaries, so it needs no per-tileset constant.
  * ADAPTIVE threshold: a percentile of each sprite's own gradient magnitude. A global
    constant cannot serve both high-contrast brick mortar and low-contrast brown-on-brown
    plank seams -- measured: th=0.22 keeps brick (0.53 density) but kills planks (0.00).
    Per-sprite it also survives a user swapping tilesets.
"""
import json
import os

import numpy as np
from PIL import Image


# ---------------------------------------------------------------- EDT (exact)
def _edt_1d(f):
    """Felzenszwalb & Huttenlocher exact 1-D squared distance transform."""
    n = f.shape[0]
    d = np.empty(n, dtype=np.float64)
    v = np.zeros(n, dtype=np.intp)
    z = np.empty(n + 1, dtype=np.float64)
    k = 0
    z[0] = -np.inf
    z[1] = np.inf
    for q in range(1, n):
        while True:
            p = v[k]
            s = ((f[q] + q * q) - (f[p] + p * p)) / (2.0 * q - 2.0 * p)
            if s <= z[k] and k > 0:
                k -= 1
                continue
            k += 1
            v[k] = q
            z[k] = s
            z[k + 1] = np.inf
            break
    k = 0
    for q in range(n):
        while z[k + 1] < q:
            k += 1
        dq = q - v[k]
        d[q] = dq * dq + f[v[k]]
    return d


def edt(mask):
    """Exact EDT: distance from each True pixel to the nearest False pixel."""
    f = np.where(mask, 1e12, 0.0).astype(np.float64)
    out = np.empty_like(f)
    for x in range(f.shape[1]):
        out[:, x] = _edt_1d(f[:, x])
    for y in range(f.shape[0]):
        out[y, :] = _edt_1d(out[y, :])
    return np.sqrt(np.maximum(out, 0.0))


# ---------------------------------------------------------------- filters
def gaussian(a, sigma):
    if sigma <= 0:
        return a
    r = max(1, int(round(3.0 * sigma)))
    x = np.arange(-r, r + 1, dtype=np.float64)
    k = np.exp(-(x * x) / (2.0 * sigma * sigma))
    k /= k.sum()
    p = np.pad(a, ((0, 0), (r, r)), mode="edge")
    a = np.apply_along_axis(lambda m: np.convolve(m, k, mode="valid"), 1, p)
    p = np.pad(a, ((r, r), (0, 0)), mode="edge")
    return np.apply_along_axis(lambda m: np.convolve(m, k, mode="valid"), 0, p)


def median3(img):
    """3x3 median per channel. Edge-preserving, and it removes 1px dither by shape."""
    p = np.pad(img, ((1, 1), (1, 1), (0, 0)), mode="edge")
    stack = [p[dy:dy + img.shape[0], dx:dx + img.shape[1], :]
             for dy in range(3) for dx in range(3)]
    return np.median(np.stack(stack, axis=0), axis=0)


def sobel(h):
    p = np.pad(h, 1, mode="edge")
    gx = (-p[:-2, :-2] + p[:-2, 2:]
          - 2.0 * p[1:-1, :-2] + 2.0 * p[1:-1, 2:]
          - p[2:, :-2] + p[2:, 2:]) / 8.0
    gy = (-p[:-2, :-2] - 2.0 * p[:-2, 1:-1] - p[:-2, 2:]
          + p[2:, :-2] + 2.0 * p[2:, 1:-1] + p[2:, 2:]) / 8.0
    return gx, gy

# ---------------------------------------------------------------- generator
class Params:
    def __init__(self, **kw):
        self.ext_weight = 0.5       # blend between external and internal EDT
        self.edge_threshold = 0.14  # colour delta counting as an internal contour
        self.blur = 1.1             # gaussian sigma on the merged height map
        self.slope = 2.6            # height->normal gain (the "blue channel" control)
        self.coh_lo = 2.9           # gap run at/below which a sprite reads as noise
        self.coh_hi = 4.0           # gap run at/above which it reads as structured
        self.min_density = 0.05     # below this the mask is too sparse to be structure
        self.flat_eps = 0.04        # max colour gradient below this => genuinely flat
        self.__dict__.update(kw)


def colour_gradient(rgb):
    """Max 4-neighbour colour distance per pixel, in 0..1 units."""
    c = rgb / 255.0
    g = np.zeros(c.shape[:2])
    for dy, dx in ((0, 1), (1, 0), (0, -1), (-1, 0)):
        sh = np.roll(np.roll(c, dy, axis=0), dx, axis=1)
        g = np.maximum(g, np.sqrt(((c - sh) ** 2).sum(axis=2)))
    return g


def internal_edge_mask(rgb, solid, p):
    """Colour-contour mask: True where a pixel differs enough from a 4-neighbour.

    Uses colour DISTANCE as a segment boundary, NOT luminance as height. That
    distinction is what keeps pre-baked shading from inverting volumes -- the
    documented failure mode of Sobel-from-colour (arXiv:2212.09692 sec IV-B).

    Deliberately NO median prefilter and NO adaptive percentile: both were measured
    on this tileset and both made it worse. A 3x3 median erased the brick mortar and
    plank seams that are the entire signal. A percentile threshold fires on a fixed
    fraction of pixels BY CONSTRUCTION -- even on a uniform sprite with no contours at
    all -- so the EDT then invents large smooth blobs out of quantization noise.
    Dither is rejected AFTER the fact by the coherence gate, a shape statistic rather
    than a colour constant, so it does not need retuning per tileset.
    """
    return (colour_gradient(rgb) > p.edge_threshold) & solid


def _mean_run(rows):
    runs, c = [], 0
    for row in rows:
        for v in row:
            if v:
                c += 1
            elif c:
                runs.append(c)
                c = 0
        if c:
            runs.append(c)
            c = 0
    return float(np.mean(runs)) if runs else 0.0


def coherence(mask):
    """Mean NON-edge (gap) run length, both axes. Separates structure from dither.

    Measured over 12 UnDeadPeople tiles at threshold 0.14 this separates cleanly:
    noise 1.60-2.89 (dithered concrete, grass, door) vs structured 4.04-23.84 (brick,
    planks, sidewalk, pavement, metal floor). Nothing lands in the gap.

    Two alternatives were implemented and measured, and both FAILED on this data:

    * EDGE run length (instead of gaps): herringbone plank seams are DIAGONAL, so
      they give short runs along both axes and scored 2.39 -- BELOW dithered concrete
      at 3.26. Axis-aligned edge runs cannot see diagonal structure.
    * Scale ratio (edge density at native res vs a 2x box downsample), which is
      orientation-free and so looked more principled: structured 0.00-1.35 vs noise
      0.02-0.63, i.e. heavily overlapping and unusable. Planks scored 0.06, identical
      to grass noise. Box downsampling AVERAGES, which erases 1px plank and pavement
      seams along with the dither, so it actually discriminates thick-vs-thin
      features rather than structure-vs-noise.

    Gap length has one hole -- it rises as edges get sparser, so a sprite carrying a
    few isolated speckles scores high and would pass the gate, which is the sparse
    seed -> invented blob failure this gate exists to stop. That is closed separately
    by `min_density`, not by this statistic.
    """
    keep = ~mask
    return 0.5 * (_mean_run(keep) + _mean_run(keep.T))


def gen_normal(tile, p=None):
    p = p or Params()
    rgb = tile[..., :3].astype(np.float64)
    solid = (tile[..., 3].astype(np.float64) / 255.0) > 0.5

    # --- external term -------------------------------------------------------
    # GATE: when the opaque region fills the tile the silhouette carries no shape;
    # the EDT degenerates to distance-from-tile-border and would stamp the SAME
    # pyramid onto every wall and floor tile, giving a repeating diamond crease
    # locked to the tile grid. Drop it there; internal contours carry the relief.
    full_tile = (solid[0, :].any() and solid[-1, :].any()
                 and solid[:, 0].any() and solid[:, -1].any()
                 and solid.mean() > 0.97)
    if full_tile:
        h_ext = np.zeros(solid.shape)
        w = 0.0
    else:
        h_ext = edt(solid)
        if h_ext.max() > 0:
            h_ext /= h_ext.max()
        w = p.ext_weight

    # --- internal term -------------------------------------------------------
    grad = colour_gradient(rgb)
    edges = (grad > p.edge_threshold) & solid
    density = float(edges.mean())
    # Absolute short-circuit: a genuinely flat sprite has no contours to find, and
    # anything we extracted would be quantization noise promoted to geometry.
    flat = float(grad[solid].max()) < p.flat_eps if solid.any() else True
    # Sparse-mask short-circuit: this is the hole in the gap-run statistic. A handful
    # of isolated speckles leaves huge clean gaps, so coherence scores high, and the
    # EDT would then grow big smooth domes out of a few stray pixels -- geometry that
    # is not in the art at all. Too sparse to be structure => no relief.
    sparse = density < p.min_density
    coh = 0.0 if (flat or sparse) else coherence(edges)
    if edges.any() and not flat:
        h_int = edt(~edges & solid)
        if h_int.max() > 0:
            h_int /= h_int.max()
    else:
        h_int = np.zeros(solid.shape)
        if not full_tile:
            w = 1.0

    h = gaussian(w * h_ext + (1.0 - w) * h_int, p.blur)

    # Coherence gate: fade the INTERNAL relief out for noise-textured sprites.
    # Dither carries no geometry, so inventing relief there is strictly worse than
    # staying flat -- it reads as "cottage cheese" over the whole surface.
    amp = 0.0 if coh <= p.coh_lo else min(1.0, (coh - p.coh_lo) / (p.coh_hi - p.coh_lo))
    slope = p.slope * (amp if full_tile else max(amp, p.ext_weight))

    gx, gy = sobel(h)
    nx, ny, nz = -gx * slope, -gy * slope, np.ones(h.shape)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    n = np.stack([nx / ln, ny / ln, nz / ln], axis=2)
    n[~solid] = (0.0, 0.0, 1.0)
    return {"n": n, "h": h, "edges": edges, "full_tile": full_tile,
            "coh": coh, "amp": amp, "flat": flat}


def encode(n):
    return np.clip((n * 0.5 + 0.5) * 255.0, 0, 255).astype(np.uint8)


def shade(tile, n, light):
    """N.L render so directionality can actually be judged, not guessed."""
    L = np.array(light, dtype=np.float64)
    L /= np.linalg.norm(L)
    ndl = np.clip((n * L).sum(axis=2), 0.0, 1.0)
    lit = 0.25 + 0.75 * ndl
    out = np.clip(tile[..., :3].astype(np.float64) * lit[..., None], 0, 255)
    return np.concatenate([out.astype(np.uint8), tile[..., 3:4]], axis=2)


# ---------------------------------------------------------------- atlas index
def sheet_table(tsdir):
    cfg = json.load(open(os.path.join(tsdir, "tile_config.json"), encoding="utf-8"))
    base, out = 0, []
    for sheet in cfg["tiles-new"]:
        f = sheet.get("file")
        if not f:
            continue
        path = os.path.join(tsdir, f)
        if not os.path.exists(path):
            continue
        im = Image.open(path)
        sw = sheet.get("sprite_width", 32)
        sh = sheet.get("sprite_height", 32)
        cols, rows = im.width // sw, im.height // sh
        out.append({"file": f, "path": path, "base": base, "sw": sw, "sh": sh,
                    "cols": cols, "count": cols * rows})
        base += cols * rows
    return out


def locate(tsdir, fg):
    for s in sheet_table(tsdir):
        if s["base"] <= fg < s["base"] + s["count"]:
            lo = fg - s["base"]
            return s, ((lo % s["cols"]) * s["sw"], (lo // s["cols"]) * s["sh"])
    return None, None


def fetch(tsdir, fg):
    s, xy = locate(tsdir, fg)
    if not s:
        return None, None
    im = Image.open(s["path"]).convert("RGBA")
    x, y = xy
    return np.asarray(im.crop((x, y, x + s["sw"], y + s["sh"])), dtype=np.uint8), s
