#!/usr/bin/env python3
"""Generate procedural terrain transition OVERLAY strips offline.

Instead of blending two sprites (which creates mud), this generates thin
semi-transparent border strips that composite over terrain edges.
Each strip fades from terrain A's edge color to terrain B's edge color
over 2-4 pixels, preserving the underlying pixel art intact.

The strips are directional (_n/_s/_e/_w) and designed to be placed
as overlay tiles at terrain boundaries.

Usage:
    python gen_transitions.py [--pairs t_pavement:t_grass,...]
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[2]
TILESET_DIR = REPO / "gfx" / "MSX++UnDeadPeopleEdition"
CONFIG_FILE = TILESET_DIR / "tile_config.json"
SPRITE_SIZE = 32


def load_cfg() -> dict:
    with open(CONFIG_FILE) as f:
        return json.load(f)


def build_sprite_lookup(cfg: dict) -> dict[int, tuple[str, int]]:
    """global_sprite_index -> (png_filename, local_grid_position)."""
    lookup: dict[int, tuple[str, int]] = {}
    for sheet in cfg.get("tiles-new", []):
        fname = sheet.get("file", "")
        local = 0
        for tile in sheet.get("tiles", []):
            fg = tile.get("fg")
            if isinstance(fg, int):
                lookup[fg] = (fname, local)
                local += 1
            elif isinstance(fg, list):
                for entry in fg:
                    if isinstance(entry, dict):
                        s = entry.get("sprite")
                        if isinstance(s, int):
                            lookup[s] = (fname, local)
                            local += 1
                    elif isinstance(entry, int):
                        lookup[entry] = (fname, local)
                        local += 1
    return lookup


def extract_sprite(sprite_idx: int, lookup: dict) -> Image.Image | None:
    if sprite_idx not in lookup:
        return None
    fname, local = lookup[sprite_idx]
    png = TILESET_DIR / fname
    if not png.exists():
        return None
    atlas = Image.open(str(png)).convert("RGBA")
    cols = atlas.width // SPRITE_SIZE
    row, col = divmod(local, cols)
    x, y = col * SPRITE_SIZE, row * SPRITE_SIZE
    return atlas.crop((x, y, x + SPRITE_SIZE, y + SPRITE_SIZE)).convert("RGBA")


def get_terrain_composite(cfg: dict, lookup: dict, terrain_id: str) -> Image.Image | None:
    """Extract and composite fg+bg for a terrain ID."""
    for sheet in cfg.get("tiles-new", []):
        for tile in sheet.get("tiles", []):
            if tile.get("id") != terrain_id:
                continue
            fg_raw = tile.get("fg")
            bg_raw = tile.get("bg")

            fg_idx = None
            if isinstance(fg_raw, int):
                fg_idx = fg_raw
            elif isinstance(fg_raw, list):
                best_w, best_s = -1, None
                for entry in fg_raw:
                    if isinstance(entry, dict):
                        w = entry.get("weight", 0)
                        s = entry.get("sprite")
                        if isinstance(w, int) and isinstance(s, int) and w > best_w:
                            best_w, best_s = w, s
                    elif isinstance(entry, int):
                        return extract_sprite(entry, lookup)
                fg_idx = best_s

            if fg_idx is None:
                return None

            fg_img = extract_sprite(fg_idx, lookup)
            if fg_img is None:
                return None

            if bg_raw is not None and isinstance(bg_raw, int):
                bg_img = extract_sprite(bg_raw, lookup)
                if bg_img is not None:
                    composite = bg_img.copy()
                    composite.paste(fg_img, (0, 0), fg_img)
                    return composite

            return fg_img
    return None


def sample_edge_colors(sprite: Image.Image, edge: str) -> list[list[int]]:
    """Sample dominant colors along one edge of a sprite.

    Returns a list of [r, g, b] colors, one per row/column along the edge,
    skipping transparent pixels.
    """
    arr = np.array(sprite)
    colors = []

    if edge == "bottom":
        # Sample last 4 rows, average horizontally
        for dy in range(4):
            y = SPRITE_SIZE - 1 - dy
            row = arr[y, :, :]
            mask = row[:, 3] > 128
            if mask.any():
                avg = row[mask].mean(axis=0)[:3].tolist()
                colors.append([int(c) for c in avg])
                break
    elif edge == "top":
        for dy in range(4):
            y = dy
            row = arr[y, :, :]
            mask = row[:, 3] > 128
            if mask.any():
                avg = row[mask].mean(axis=0)[:3].tolist()
                colors.append([int(c) for c in avg])
                break
    elif edge == "right":
        for dx in range(4):
            x = SPRITE_SIZE - 1 - dx
            col = arr[:, x, :]
            mask = col[:, 3] > 128
            if mask.any():
                avg = col[mask].mean(axis=0)[:3].tolist()
                colors.append([int(c) for c in avg])
                break
    elif edge == "left":
        for dx in range(4):
            x = dx
            col = arr[:, x, :]
            mask = col[:, 3] > 128
            if mask.any():
                avg = col[mask].mean(axis=0)[:3].tolist()
                colors.append([int(c) for c in avg])
                break

    return colors


def noise(x: int, y: int, seed: int) -> float:
    n = x * 374761393 + y * 668265263 + seed
    n = (n ^ (n >> 13)) * 1274126177
    return ((n ^ (n >> 16)) & 0x7FFFFFFF) / 0x7FFFFFFF


def make_overlay_strip(spr_a: Image.Image, spr_b: Image.Image,
                       direction: str, strip_width: int = 3, seed: int = 42) -> Image.Image:
    """Create a semi-transparent overlay strip for terrain transitions.

    The strip is a 32x32 sprite that is mostly transparent, with a colored
    band along one edge that fades from terrain A's color to terrain B's color.

    For direction 'n' (A on top, B on bottom):
      - The strip has a horizontal band near the bottom edge
      - Colors fade from A's bottom color to B's top color
      - Designed to be placed on the B tile, showing the transition upward

    Returns a 32x32 RGBA image.
    """
    arr_a = np.array(spr_a)
    arr_b = np.array(spr_b)

    # Sample edge colors
    if direction in ("n", "s"):
        # Horizontal transition
        if direction == "n":
            # A on top, B on bottom: strip goes on B tile, fading up
            color_from = _avg_edge_color(arr_a, "bottom")
            color_to = _avg_edge_color(arr_b, "top")
        else:
            # B on top, A on bottom: strip goes on A tile, fading down
            color_from = _avg_edge_color(arr_b, "bottom")
            color_to = _avg_edge_color(arr_a, "top")
    else:
        # Vertical transition
        if direction == "e":
            # A on left, B on right: strip goes on B tile, fading left
            color_from = _avg_edge_color(arr_a, "right")
            color_to = _avg_edge_color(arr_b, "left")
        else:
            # B on left, A on right: strip goes on A tile, fading right
            color_from = _avg_edge_color(arr_b, "right")
            color_to = _avg_edge_color(arr_a, "left")

    # Create transparent overlay
    out = np.zeros((SPRITE_SIZE, SPRITE_SIZE, 4), dtype=np.float32)

    if direction in ("n", "s"):
        # Horizontal band
        if direction == "n":
            # Band at bottom of sprite (rows 29-31)
            band_start = SPRITE_SIZE - strip_width
            band_end = SPRITE_SIZE
            for y in range(band_start, band_end):
                t = (y - band_start) / max(1, strip_width - 1)  # 0 at top of band, 1 at bottom
                # Fade: top of band = color_from (A), bottom = color_to (B)
                r = color_from[0] * (1 - t) + color_to[0] * t
                g = color_from[1] * (1 - t) + color_to[1] * t
                b = color_from[2] * (1 - t) + color_to[2] * t
                # Alpha: stronger in middle, softer at edges
                alpha = 180  # Semi-transparent
                # Add slight noise to alpha for organic feel
                for x in range(SPRITE_SIZE):
                    nv = noise(x, y, seed)
                    out[y, x, 0] = r
                    out[y, x, 1] = g
                    out[y, x, 2] = b
                    out[y, x, 3] = alpha * (0.7 + nv * 0.3)
        else:
            # Band at top of sprite (rows 0-2)
            band_start = 0
            band_end = strip_width
            for y in range(band_start, band_end):
                t = y / max(1, strip_width - 1)
                r = color_from[0] * (1 - t) + color_to[0] * t
                g = color_from[1] * (1 - t) + color_to[1] * t
                b = color_from[2] * (1 - t) + color_to[2] * t
                alpha = 180
                for x in range(SPRITE_SIZE):
                    nv = noise(x, y, seed)
                    out[y, x, 0] = r
                    out[y, x, 1] = g
                    out[y, x, 2] = b
                    out[y, x, 3] = alpha * (0.7 + nv * 0.3)
    else:
        # Vertical band
        if direction == "e":
            # Band at right edge (cols 29-31)
            band_start = SPRITE_SIZE - strip_width
            band_end = SPRITE_SIZE
            for x in range(band_start, band_end):
                t = (x - band_start) / max(1, strip_width - 1)
                r = color_from[0] * (1 - t) + color_to[0] * t
                g = color_from[1] * (1 - t) + color_to[1] * t
                b = color_from[2] * (1 - t) + color_to[2] * t
                alpha = 180
                for y in range(SPRITE_SIZE):
                    nv = noise(x, y, seed)
                    out[y, x, 0] = r
                    out[y, x, 1] = g
                    out[y, x, 2] = b
                    out[y, x, 3] = alpha * (0.7 + nv * 0.3)
        else:
            # Band at left edge (cols 0-2)
            band_start = 0
            band_end = strip_width
            for x in range(band_start, band_end):
                t = x / max(1, strip_width - 1)
                r = color_from[0] * (1 - t) + color_to[0] * t
                g = color_from[1] * (1 - t) + color_to[1] * t
                b = color_from[2] * (1 - t) + color_to[2] * t
                alpha = 180
                for y in range(SPRITE_SIZE):
                    nv = noise(x, y, seed)
                    out[y, x, 0] = r
                    out[y, x, 1] = g
                    out[y, x, 2] = b
                    out[y, x, 3] = alpha * (0.7 + nv * 0.3)

    return Image.fromarray(out.astype(np.uint8), "RGBA")


def _avg_edge_color(arr: np.ndarray, edge: str) -> tuple[float, float, float]:
    """Average non-transparent color along an edge."""
    if edge == "bottom":
        for dy in range(4):
            y = SPRITE_SIZE - 1 - dy
            row = arr[y, :, :]
            mask = row[:, 3] > 128
            if mask.any():
                return tuple(row[mask].mean(axis=0)[:3])
    elif edge == "top":
        for dy in range(4):
            row = arr[dy, :, :]
            mask = row[:, 3] > 128
            if mask.any():
                return tuple(row[mask].mean(axis=0)[:3])
    elif edge == "right":
        for dx in range(4):
            x = SPRITE_SIZE - 1 - dx
            col = arr[:, x, :]
            mask = col[:, 3] > 128
            if mask.any():
                return tuple(col[mask].mean(axis=0)[:3])
    elif edge == "left":
        for dx in range(4):
            col = arr[:, dx, :]
            mask = col[:, 3] > 128
            if mask.any():
                return tuple(col[mask].mean(axis=0)[:3])
    return (128.0, 128.0, 128.0)  # Fallback gray


def shorten(tid: str) -> str:
    """t_grass_dead -> grass, t_water_pool_shallow_outdoors -> water."""
    s = tid.removeprefix("t_")
    for suf in ["_underground", "_dead", "_long", "_tall", "_white", "_golf",
                "_murky", "_pool", "_outdoors", "_shallow", "_green", "_gray",
                "_yellow", "_red", "_purple", "_no_roof"]:
        s = s.replace(suf, "")
    return s


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--pairs", default=None)
    p.add_argument("--outdir", default=str(TILESET_DIR))
    p.add_argument("--strip-width", type=int, default=3)
    p.add_argument("--seed", type=int, default=42)
    args = p.parse_args()

    cfg = load_cfg()
    lookup = build_sprite_lookup(cfg)

    if args.pairs:
        pairs = [(a.strip(), b.strip()) for a, b in (x.split(":") for x in args.pairs.split(","))]
    else:
        pairs = [
            ("t_pavement", "t_grass"),
            ("t_concrete", "t_grass"),
            ("t_concrete", "t_dirt"),
            ("t_pavement", "t_dirt"),
            ("t_sidewalk", "t_grass"),
            ("t_sand", "t_grass"),
            ("t_dirt", "t_grass"),
            ("t_mud", "t_grass"),
            ("t_con