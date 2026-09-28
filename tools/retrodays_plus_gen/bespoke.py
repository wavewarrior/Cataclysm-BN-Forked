#!/usr/bin/env python3
"""Author bespoke 16x16 sprites for a bounded high-value id subset.

Selection (deterministic, cap 24): all `type==GUN` target ids (alphabetical),
then all `type==MONSTER` target ids whose resolved `species[0]` is ZOMBIE or
HUMAN (alphabetical). The first 24 of that concatenation are chosen.

Each chosen id gets an explicit 16x16 pixel grid (indices into a fixed
8-entry palette: transparent + black/white/gray/dark_gray/steel/brown/blood)
authored to be visually distinguishable from the generic procedural family
shape (gun / z_zombie / z_human) it would otherwise receive.

Sprites are native 16x16 pixels; the wiring script sets `"sprite_width": 16,
"sprite_height": 16, "pixelscale": 0.625` on this sheet's tile_config.json
entry so each icon still renders at exactly one tile's on-screen footprint.

Usage:
    python3 tools/retrodays_plus_gen/bespoke.py \
        --resolved /tmp/rdp_resolved.json \
        --out-sheet gfx/RetroDays+Tileset/bespoke_plus.png \
        --out-entries /tmp/rdp_bespoke_entries.json \
        --out-ids /tmp/rdp_bespoke_ids.txt
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Callable

from PIL import Image

CELL = 16
COLS = 16
CAP = 24

# Fixed 8-entry bespoke palette (index -> RGBA). 0 is transparent.
PALETTE: dict[int, tuple[int, int, int, int]] = {
    0: (0, 0, 0, 0),  # transparent
    1: (0, 0, 0, 255),  # black
    2: (255, 255, 255, 255),  # white
    3: (150, 150, 150, 255),  # gray
    4: (99, 99, 99, 255),  # dark_gray
    5: (90, 100, 112, 255),  # steel
    6: (97, 56, 28, 255),  # brown
    7: (139, 0, 0, 255),  # blood
}

Grid = list[list[int]]


def log(msg: str) -> None:
    print(msg, file=sys.stderr)


def new_grid() -> Grid:
    return [[0 for _ in range(CELL)] for _ in range(CELL)]


def place(grid: Grid, row: int, col: int, digit: int) -> None:
    if 0 <= row < CELL and 0 <= col < CELL:
        grid[row][col] = digit


def line(grid: Grid, r0: int, c0: int, r1: int, c1: int, digit: int, width: int = 1) -> None:
    """Short accessory stroke; width=2 also fills the next column over."""
    steps = max(abs(r1 - r0), abs(c1 - c0), 1)
    for i in range(steps + 1):
        r = round(r0 + (r1 - r0) * i / steps)
        c = round(c0 + (c1 - c0) * i / steps)
        place(grid, r, c, digit)
        if width > 1:
            place(grid, r, c + 1, digit)


def rect(grid: Grid, r0: int, c0: int, r1: int, c1: int, digit: int) -> None:
    for r in range(r0, r1 + 1):
        for c in range(c0, c1 + 1):
            place(grid, r, c, digit)


def circle(grid: Grid, cr: int, cc: int, radius: float, digit: int) -> None:
    for r in range(CELL):
        for c in range(CELL):
            if math.hypot(r - cr, c - cc) <= radius:
                place(grid, r, c, digit)


def render_grid(grid: Grid) -> Image.Image:
    img = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    px = img.load()
    for r in range(CELL):
        for c in range(CELL):
            px[c, r] = PALETTE[grid[r][c]]
    return img


# ---------------------------------------------------------------------------
# Base humanoid body (used by the 16 feral/zombie-human bespoke sprites).
# Columns 6-10 hold the body; columns 0-5 and 11-15 are free for accessories.
# ---------------------------------------------------------------------------

def base_human() -> Grid:
    g = new_grid()
    rect(g, 1, 7, 2, 9, 1)
    rect(g, 2, 7, 3, 9, 3)
    place(g, 2, 7, 1)
    place(g, 2, 9, 1)
    rect(g, 4, 6, 4, 10, 1)
    for r in (5, 6, 7, 8):
        rect(g, r, 6, r, 10, 3)
        place(g, r, 6, 1)
        place(g, r, 10, 1)
    rect(g, 9, 6, 9, 10, 4)
    for r in range(10, 15):
        place(g, r, 6, 1)
        place(g, r, 7, 4)
        place(g, r, 9, 4)
        place(g, r, 10, 1)
    rect(g, 15, 6, 15, 7, 1)
    rect(g, 15, 9, 15, 10, 1)
    return g


def armor_bulk(g: Grid) -> Grid:
    """Widen the torso silhouette with steel plates for armored variants."""
    for r in (5, 6, 7, 8):
        place(g, r, 5, 5)
        place(g, r, 11, 5)
    rect(g, 4, 5, 4, 11, 5)
    rect(g, 9, 5, 9, 11, 5)
    return g


# ---------------------------------------------------------------------------
# 8 bespoke GUN sprites (each a full explicit 16x16 grid, no shared base).
# ---------------------------------------------------------------------------

def gun_25mm_autocannon() -> Grid:
    g = new_grid()
    line(g, 6, 15, 9, 8, 5, width=2)
    line(g, 7, 15, 10, 8, 5, width=2)
    rect(g, 8, 3, 11, 9, 4)
    rect(g, 9, 4, 10, 8, 5)
    rect(g, 10, 9, 14, 12, 5)
    rect(g, 11, 10, 13, 11, 4)
    rect(g, 11, 0, 13, 3, 6)
    return g


def gun_25mm_autocannon_sawn() -> Grid:
    g = new_grid()
    line(g, 8, 14, 9, 9, 5, width=2)
    rect(g, 8, 3, 11, 9, 4)
    rect(g, 9, 4, 10, 8, 5)
    rect(g, 10, 9, 14, 12, 5)
    rect(g, 11, 10, 13, 11, 4)
    rect(g, 10, 2, 11, 3, 6)
    return g


def gun_25mm_cannon_crude() -> Grid:
    g = new_grid()
    rect(g, 6, 9, 9, 15, 3)
    rect(g, 7, 10, 8, 14, 4)
    rect(g, 8, 2, 12, 9, 4)
    rect(g, 9, 3, 11, 8, 3)
    for c in (3, 5, 7):
        place(g, 8, c, 1)
    rect(g, 11, 1, 13, 3, 6)
    return g


def gun_howitzer_gun_crude() -> Grid:
    g = new_grid()
    line(g, 3, 14, 9, 5, 5, width=2)
    rect(g, 9, 2, 12, 11, 3)
    rect(g, 10, 3, 11, 10, 4)
    for cx in (4, 10):
        circle(g, 13, cx, 2, 4)
        circle(g, 13, cx, 1, 3)
    rect(g, 13, 6, 13, 8, 4)
    return g


def gun_mut_quills() -> Grid:
    g = new_grid()
    rect(g, 6, 6, 10, 10, 6)
    rect(g, 7, 7, 9, 9, 7)
    for dr, dc in ((-4, 0), (4, 0), (0, -4), (0, 4), (-4, -4), (-4, 4), (4, -4), (4, 4),
                   (-3, -1), (-3, 1), (3, -1), (3, 1)):
        line(g, 8 + dr // 2, 8 + dc // 2, 8 + dr, 8 + dc, 1)
    return g


def gun_reach_bow() -> Grid:
    g = new_grid()
    line(g, 1, 6, 5, 2, 6, width=2)
    line(g, 5, 2, 8, 1, 6, width=2)
    line(g, 8, 1, 11, 2, 6, width=2)
    line(g, 11, 2, 15, 6, 6, width=2)
    line(g, 1, 6, 15, 6, 3)
    rect(g, 7, 6, 9, 6, 2)
    line(g, 8, 6, 8, 14, 4)
    return g


def gun_tank_gun_crude() -> Grid:
    g = new_grid()
    rect(g, 5, 10, 8, 15, 5)
    rect(g, 6, 2, 11, 10, 4)
    rect(g, 7, 3, 10, 9, 3)
    rect(g, 11, 1, 14, 11, 4)
    return g


def gun_tank_gun_crude_105mm() -> Grid:
    g = new_grid()
    rect(g, 4, 9, 9, 15, 5)
    place(g, 4, 12, 2)
    place(g, 9, 12, 2)
    rect(g, 5, 11, 8, 15, 4)
    rect(g, 6, 1, 12, 9, 4)
    rect(g, 7, 2, 11, 8, 3)
    rect(g, 12, 0, 15, 11, 4)
    return g


GUN_BUILDERS: dict[str, Callable[[], Grid]] = {
    "25mm_autocannon": gun_25mm_autocannon,
    "25mm_autocannon_sawn": gun_25mm_autocannon_sawn,
    "25mm_cannon_crude": gun_25mm_cannon_crude,
    "howitzer_gun_crude": gun_howitzer_gun_crude,
    "mut_quills": gun_mut_quills,
    "reach_bow": gun_reach_bow,
    "tank_gun_crude": gun_tank_gun_crude,
    "tank_gun_crude_105mm": gun_tank_gun_crude_105mm,
}


# ---------------------------------------------------------------------------
# 16 bespoke feral/zombie-human sprites: base_human() + a distinct accessory.
# ---------------------------------------------------------------------------

def mon_devourer() -> Grid:
    g = base_human()
    for r, c in ((2, 6), (2, 10), (4, 5), (4, 11), (6, 5), (6, 11)):
        place(g, r, c, 7)
    rect(g, 2, 8, 3, 8, 7)
    return g


def mon_feral_armored_battleaxe() -> Grid:
    g = armor_bulk(base_human())
    line(g, 10, 12, 2, 14, 6, width=2)
    rect(g, 0, 12, 3, 15, 5)
    rect(g, 1, 13, 2, 14, 2)
    return g


def mon_feral_armored_mace() -> Grid:
    g = armor_bulk(base_human())
    line(g, 10, 11, 3, 13, 6, width=2)
    circle(g, 2, 13, 2, 5)
    circle(g, 2, 13, 1, 4)
    for dr, dc in ((-2, 0), (2, 0), (0, -2), (0, 2)):
        place(g, 2 + dr, 13 + dc, 1)
    return g


def mon_feral_blackops_boss_bioop() -> Grid:
    g = base_human()
    rect(g, 5, 0, 10, 2, 5)
    place(g, 6, 0, 7)
    place(g, 8, 0, 2)
    line(g, 6, 2, 6, 6, 6)
    return g


def mon_feral_blackops_boss_commander() -> Grid:
    g = base_human()
    place(g, 0, 8, 2)
    rect(g, 4, 4, 4, 12, 4)
    rect(g, 5, 3, 8, 4, 4)
    rect(g, 5, 12, 8, 13, 4)
    return g


def mon_feral_blackops_boss_sniper() -> Grid:
    g = base_human()
    line(g, 6, 10, 6, 15, 5)
    rect(g, 5, 4, 6, 10, 4)
    place(g, 6, 13, 2)
    return g


def mon_feral_blackops_boss_technician() -> Grid:
    g = base_human()
    rect(g, 7, 11, 9, 13, 5)
    line(g, 9, 12, 12, 12, 5)
    place(g, 6, 11, 2)
    return g


def mon_feral_bodyguard() -> Grid:
    g = armor_bulk(base_human())
    rect(g, 3, 0, 12, 3, 5)
    rect(g, 4, 1, 11, 2, 3)
    place(g, 7, 1, 2)
    place(g, 8, 1, 2)
    return g


def mon_feral_corpo() -> Grid:
    g = base_human()
    line(g, 4, 7, 8, 8, 7)
    line(g, 4, 9, 8, 8, 7)
    place(g, 4, 6, 2)
    place(g, 4, 10, 2)
    return g


def mon_feral_fancy_crossbow() -> Grid:
    g = base_human()
    line(g, 5, 12, 9, 12, 6)
    line(g, 5, 10, 5, 14, 5)
    line(g, 5, 10, 7, 9, 5)
    line(g, 5, 14, 7, 15, 5)
    return g


def mon_feral_fancy_rapier() -> Grid:
    g = base_human()
    line(g, 9, 11, 0, 15, 5)
    place(g, 9, 11, 2)
    rect(g, 8, 10, 9, 12, 6)
    return g


def mon_feral_fancy_rapier_fake() -> Grid:
    g = base_human()
    line(g, 9, 5, 0, 1, 5)
    place(g, 9, 5, 2)
    rect(g, 8, 4, 9, 6, 6)
    return g


def mon_feral_human_axe() -> Grid:
    g = base_human()
    line(g, 10, 11, 4, 13, 6)
    rect(g, 2, 12, 4, 14, 3)
    place(g, 3, 13, 2)
    return g


def mon_feral_human_chainsaw() -> Grid:
    g = base_human()
    rect(g, 7, 10, 9, 15, 4)
    for c in range(10, 16):
        place(g, 7, c, 1 if c % 2 == 0 else 3)
    rect(g, 8, 8, 9, 10, 6)
    return g


def mon_feral_human_crowbar() -> Grid:
    g = base_human()
    line(g, 10, 11, 3, 13, 5)
    line(g, 3, 13, 3, 15, 5)
    return g


def mon_feral_human_pipe() -> Grid:
    g = base_human()
    line(g, 11, 11, 2, 14, 5)
    return g


ZH_BUILDERS: dict[str, Callable[[], Grid]] = {
    "mon_devourer": mon_devourer,
    "mon_feral_armored_battleaxe": mon_feral_armored_battleaxe,
    "mon_feral_armored_mace": mon_feral_armored_mace,
    "mon_feral_blackops_boss_bioop": mon_feral_blackops_boss_bioop,
    "mon_feral_blackops_boss_commander": mon_feral_blackops_boss_commander,
    "mon_feral_blackops_boss_sniper": mon_feral_blackops_boss_sniper,
    "mon_feral_blackops_boss_technician": mon_feral_blackops_boss_technician,
    "mon_feral_bodyguard": mon_feral_bodyguard,
    "mon_feral_corpo": mon_feral_corpo,
    "mon_feral_fancy_crossbow": mon_feral_fancy_crossbow,
    "mon_feral_fancy_rapier": mon_feral_fancy_rapier,
    "mon_feral_fancy_rapier_fake": mon_feral_fancy_rapier_fake,
    "mon_feral_human_axe": mon_feral_human_axe,
    "mon_feral_human_chainsaw": mon_feral_human_chainsaw,
    "mon_feral_human_crowbar": mon_feral_human_crowbar,
    "mon_feral_human_pipe": mon_feral_human_pipe,
}

ALL_BUILDERS: dict[str, Callable[[], Grid]] = {**GUN_BUILDERS, **ZH_BUILDERS}


def select_ids(resolved: dict[str, dict]) -> list[str]:
    guns = sorted(tid for tid, info in resolved.items() if info.get("type") == "GUN")
    zh = sorted(
        tid
        for tid, info in resolved.items()
        if info.get("type") == "MONSTER" and info.get("species") and info["species"][0] in ("ZOMBIE", "HUMAN")
    )
    chosen = (guns + zh)[:CAP]
    log(f"guns available: {len(guns)}, zombie/human monsters available: {len(zh)}, chosen: {len(chosen)}")
    return chosen


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resolved", type=Path, required=True)
    parser.add_argument("--out-sheet", type=Path, required=True)
    parser.add_argument("--out-entries", type=Path, required=True)
    parser.add_argument("--out-ids", type=Path, required=True)
    args = parser.parse_args()

    resolved = json.loads(args.resolved.read_text(encoding="utf-8"))
    chosen = select_ids(resolved)

    missing_builders = [tid for tid in chosen if tid not in ALL_BUILDERS]
    if missing_builders:
        raise SystemExit(f"no bespoke builder authored for: {missing_builders}")

    args.out_ids.write_text("\n".join(chosen) + "\n", encoding="utf-8")
    log(f"wrote {args.out_ids}")

    cols = COLS
    rows = (len(chosen) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * CELL, max(rows, 1) * CELL), (0, 0, 0, 0))
    entries = []
    for idx, tid in enumerate(chosen):
        grid = ALL_BUILDERS[tid]()
        cell_img = render_grid(grid)
        cx = (idx % cols) * CELL
        cy = (idx // cols) * CELL
        sheet.paste(cell_img, (cx, cy), cell_img)
        entries.append({"id": tid, "local": idx})

    args.out_sheet.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.out_sheet)
    log(f"wrote {args.out_sheet} ({sheet.width}x{sheet.height}, {len(chosen)} bespoke sprites)")

    args.out_entries.write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")
    log(f"wrote {args.out_entries}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
