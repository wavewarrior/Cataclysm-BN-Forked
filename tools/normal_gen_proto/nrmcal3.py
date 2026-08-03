
import sys
sys.path.insert(0, r"C:\WORK")
import os, json, numpy as np
from nrmproto import Params, fetch, colour_gradient, _mean_run
TS = os.path.join(sys.argv[1], "gfx", "MSX++UnDeadPeopleEdition")
cfg = json.load(open(os.path.join(TS, "tile_config.json"), encoding="utf-8"))
want = {"t_wall":None,"t_wall_w":None,"t_rock_wall":None,"t_floor":None,
        "t_wall_glass":None,"t_metal_floor":None,"t_sidewalk":None,"t_grass":None,
        "t_shrub":None,"t_door_c":None,"t_carpet_red":None,"t_pavement":None}
for sh in cfg["tiles-new"]:
    for t in sh.get("tiles", []) or []:
        ids = t.get("id"); ids = ids if isinstance(ids, list) else [ids]
        for i in ids:
            if i in want and want[i] is None:
                fg = t.get("fg")
                if isinstance(fg, list):
                    fg = fg[0] if not isinstance(fg[0], dict) else fg[0].get("sprite")
                if isinstance(fg, int): want[i] = fg

def box2(rgb):
    h, w = rgb.shape[0]//2*2, rgb.shape[1]//2*2
    a = rgb[:h, :w]
    return a.reshape(h//2, 2, w//2, 2, 3).mean(axis=(1, 3))

p = Params()
GOOD = {"t_rock_wall","t_floor","t_sidewalk","t_pavement","t_metal_floor"}
BAD  = {"t_wall","t_wall_w","t_grass","t_door_c"}
print(f"{'tile':<15}{'dens':>7}{'dens2x':>8}{'ratio':>7}{'gap':>7}  verdict")
rows=[]
for name, fg in want.items():
    if fg is None: continue
    tile, _ = fetch(TS, fg)
    if tile is None: continue
    rgb = tile[..., :3].astype(np.float64)
    solid = (tile[..., 3]/255.0) > 0.5
    if not solid.any(): continue
    e = (colour_gradient(rgb) > p.edge_threshold) & solid
    d1 = float(e.mean())
    small = box2(rgb)
    e2 = colour_gradient(small) > p.edge_threshold
    d2 = float(e2.mean())
    ratio = (d2 / d1) if d1 > 1e-6 else 0.0
    keep = ~e
    gap = 0.5*(_mean_run(keep)+_mean_run(keep.T))
    v = "structured" if name in GOOD else ("noise" if name in BAD else "?")
    rows.append((name, d1, d2, ratio, gap, v))
    print(f"{name:<15}{d1:>7.3f}{d2:>8.3f}{ratio:>7.2f}{gap:>7.2f}  {v}")
print()
for lab, sel in (("structured", GOOD), ("noise", BAD)):
    sub=[r for r in rows if r[0] in sel]
    if sub:
        print(f"{lab:<11} ratio {min(r[3] for r in sub):.2f}..{max(r[3] for r in sub):.2f}"
              f"   gap {min(r[4] for r in sub):.2f}..{max(r[4] for r in sub):.2f}")
