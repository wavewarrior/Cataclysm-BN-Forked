
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
p = Params()
GOOD = {"t_rock_wall","t_floor","t_sidewalk","t_carpet_red","t_pavement"}
BAD  = {"t_wall","t_wall_w","t_grass"}
print(f"{'tile':<16}{'dens':>7}{'gap_run':>9}{'edge_run':>9}{'verdict':>10}")
rows=[]
for name, fg in want.items():
    if fg is None: continue
    tile, _ = fetch(TS, fg)
    if tile is None: continue
    rgb = tile[..., :3].astype(np.float64)
    solid = (tile[..., 3]/255.0) > 0.5
    g = colour_gradient(rgb)
    e = (g > p.edge_threshold) & solid
    keep = ~e
    gap = 0.5*(_mean_run(keep)+_mean_run(keep.T))
    er  = max(_mean_run(e), _mean_run(e.T))
    v = "structured" if name in GOOD else ("noise" if name in BAD else "?")
    rows.append((name, e.mean(), gap, er, v))
    print(f"{name:<16}{e.mean():>7.3f}{gap:>9.2f}{er:>9.2f}{v:>10}")
print()
for lab, sel in (("structured", GOOD), ("noise", BAD)):
    sub=[r for r in rows if r[0] in sel]
    if sub:
        print(lab, "gap_run range", f"{min(r[2] for r in sub):.2f}..{max(r[2] for r in sub):.2f}")
