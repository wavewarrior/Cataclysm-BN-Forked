"""Render a contact sheet proving (or refuting) the procedural bevel on real BN tiles.

For each tile: source | internal-edge mask | height | normal | N.L lit from N/S/E/W.
Scaled 6x with nearest so pixel structure is judgeable.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, r"C:\WORK")
from nrmproto import Params, encode, fetch, gen_normal, shade  # noqa: E402

TS = os.path.join(sys.argv[1], "gfx", "MSX++UnDeadPeopleEdition")
OUT = sys.argv[2]
SCALE = 6

# fg indices: t_wall plus a floor and a couple of structural tiles for contrast.
TILES = [
    ("t_wall", 16275),
    ("t_wall+1", 16276),
    ("t_wall+2", 16277),
    ("t_floor", None),
]

# resolve t_floor by id
import json  # noqa: E402
cfg = json.load(open(os.path.join(TS, "tile_config.json"), encoding="utf-8"))
want = {"t_floor": None, "t_rock_wall": None, "t_wall_w": None}
for sheet in cfg["tiles-new"]:
    for t in sheet.get("tiles", []) or []:
        ids = t.get("id")
        ids = ids if isinstance(ids, list) else [ids]
        for i in ids:
            if i in want and want[i] is None:
                fg = t.get("fg")
                if isinstance(fg, list):
                    fg = fg[0] if not isinstance(fg[0], dict) else fg[0].get("sprite")
                if isinstance(fg, int):
                    want[i] = fg
print("resolved:", want)
TILES = [("t_wall", 16275), ("t_wall+1", 16276)]
for k, v in want.items():
    if v is not None:
        TILES.append((k, v))

LIGHTS = [("N", (0, -1, 0.45)), ("S", (0, 1, 0.45)),
          ("W", (-1, 0, 0.45)), ("E", (1, 0, 0.45))]
COLS = ["source", "edges", "height", "normal"] + [f"lit {n}" for n, _ in LIGHTS]

rows = []
report = []
for name, fg in TILES:
    tile, sheet = fetch(TS, fg)
    if tile is None:
        print("MISS", name, fg)
        continue
    r = gen_normal(tile, Params())
    n, h, edges, full = r["n"], r["h"], r["edges"], r["full_tile"]
    cells = [tile]
    em = np.zeros_like(tile)
    em[..., 3] = 255
    em[edges] = (255, 80, 80, 255)
    cells.append(em)
    hv = (np.clip(h / (h.max() or 1), 0, 1) * 255).astype(np.uint8)
    cells.append(np.dstack([hv, hv, hv, np.full_like(hv, 255)]))
    nn = encode(n)
    cells.append(np.dstack([nn, np.full(nn.shape[:2], 255, np.uint8)]))
    for _, L in LIGHTS:
        cells.append(shade(tile, n, L))
    rows.append((name, sheet["file"], full, cells))
    # quantify directionality: mean lit value top half vs bottom half, light from S
    lit_s = shade(tile, n, (0, 1, 0.45))[..., :3].astype(np.float64).mean(axis=2)
    hh = lit_s.shape[0] // 2
    report.append((name, f"{r['coh']:.1f}/{r['amp']:.2f}", float(n[..., 0].std()),
                   float(n[..., 1].std()),
                   float(lit_s[hh:].mean() - lit_s[:hh].mean())))

if not rows:
    raise SystemExit("no tiles resolved")

tw = rows[0][3][0].shape[1] * SCALE
th = rows[0][3][0].shape[0] * SCALE
PAD, HDR, LBL = 8, 22, 108
W = LBL + len(COLS) * (tw + PAD) + PAD
H = HDR + len(rows) * (th + PAD) + PAD
sheet_img = Image.new("RGBA", (W, H), (24, 24, 28, 255))
d = ImageDraw.Draw(sheet_img)
for ci, c in enumerate(COLS):
    d.text((LBL + ci * (tw + PAD) + 2, 5), c, fill=(220, 220, 220, 255))
for ri, (name, f, full, cells) in enumerate(rows):
    y = HDR + ri * (th + PAD)
    d.text((4, y + th // 2 - 12), name, fill=(230, 230, 120, 255))
    d.text((4, y + th // 2 + 2), "FULL" if full else "silh", fill=(160, 160, 160, 255))
    for ci, cell in enumerate(cells):
        im = Image.fromarray(cell, "RGBA").resize((tw, th), Image.NEAREST)
        bg = Image.new("RGBA", (tw, th), (60, 60, 66, 255))
        bg.alpha_composite(im)
        sheet_img.paste(bg, (LBL + ci * (tw + PAD), y))
sheet_img.save(OUT)
print("wrote", OUT, sheet_img.size)
print()
print(f"{'tile':<14}{'coh/amp':<10}{'nx std':>8}{'ny std':>8}{'S-lit bottom-top':>19}")
for name, full, sx, sy, delta in report:
    print(f"{name:<14}{full:<10}{sx:>8.3f}{sy:>8.3f}{delta:>19.2f}")
