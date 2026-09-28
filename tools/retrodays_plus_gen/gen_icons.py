#!/usr/bin/env python3
"""Generate Qud-style base-shape + tint 16x16 icons for every item/monster id
missing a real sprite in the RetroDays+ tileset.

Reads the JSON report produced by scripts/tileset_missing_items.ts (the
`missing.entries` array: `[{"id", "path"}, ...]`), resolves each id's
copy-from chain against data/json to find its color/type/species/symbol,
picks a base silhouette family, tints it by the id's resolved color, dedupes
identical (family, primary_rgb) icons into shared cells, and packs them into
a new sheet: gfx/RetroDays+Tileset/generated_plus.png.

Sprites are native 16x16 pixels (not the tileset's base 10x10). The wiring
script sets `"sprite_width": 16, "sprite_height": 16, "pixelscale": 0.625`
on this sheet's tile_config.json entry so `16 * 0.625 == 10`: each icon
still renders at exactly one tile's on-screen footprint, matching the
existing 10x10 sprites, while giving silhouettes far more working detail.

Usage:
    python3 tools/retrodays_plus_gen/gen_icons.py \
        --missing /tmp/rdp_missing.json \
        --data data/json \
        --colors data/raw/colors.json \
        --out-sheet gfx/RetroDays+Tileset/generated_plus.png \
        --out-entries /tmp/rdp_generated_entries.json \
        --out-resolved /tmp/rdp_resolved.json
"""
from __future__ import annotations

import argparse
import json
import math
import re
import sys
from pathlib import Path
from typing import Any, Callable

from PIL import Image

SKIP_INPUT_JSON = re.compile(r"(modinfo|default|replacements|mod_tileset)\.json$")
CELL = 16
COLS = 16
MAX_COPY_FROM_HOPS = 20

# ---------------------------------------------------------------------------
# Base-shape family builders. Each returns a list of CELL row-strings over
# '.' (transparent) / '#' (primary) / 'o' (detail/outline), drawn with a
# small rect/line/circle DSL so a 16x16 canvas stays maintainable and
# recognisable (vs. a hand-typed 16x16 ASCII wall).
# ---------------------------------------------------------------------------

Grid = list[list[str]]


def _new_grid() -> Grid:
    return [["." for _ in range(CELL)] for _ in range(CELL)]


def _px(g: Grid, r: int, c: int, ch: str) -> None:
    if 0 <= r < CELL and 0 <= c < CELL:
        g[r][c] = ch


def _rect(g: Grid, r0: int, c0: int, r1: int, c1: int, ch: str) -> None:
    for r in range(r0, r1 + 1):
        for c in range(c0, c1 + 1):
            _px(g, r, c, ch)


def _line(g: Grid, r0: int, c0: int, r1: int, c1: int, ch: str) -> None:
    steps = max(abs(r1 - r0), abs(c1 - c0), 1)
    for i in range(steps + 1):
        r = round(r0 + (r1 - r0) * i / steps)
        c = round(c0 + (c1 - c0) * i / steps)
        _px(g, r, c, ch)


def _circle(g: Grid, cr: int, cc: int, radius: float, ch: str, y_scale: float = 1.0) -> None:
    for r in range(CELL):
        for c in range(CELL):
            if math.hypot((r - cr) * y_scale, c - cc) <= radius:
                _px(g, r, c, ch)


def _rows(g: Grid) -> list[str]:
    return ["".join(row) for row in g]


def _gun_family() -> list[str]:
    g = _new_grid()
    _rect(g, 6, 11, 7, 15, "#")
    _rect(g, 5, 4, 7, 11, "#")
    _rect(g, 5, 4, 5, 11, "o")
    _rect(g, 7, 1, 9, 4, "#")
    _rect(g, 8, 1, 9, 2, "o")
    _rect(g, 8, 6, 11, 7, "#")
    _rect(g, 8, 8, 12, 9, "#")
    _rect(g, 8, 8, 12, 8, "o")
    _px(g, 5, 14, "#")
    return _rows(g)


def _mod_family() -> list[str]:
    g = _new_grid()
    _rect(g, 6, 3, 9, 13, "#")
    _circle(g, 7, 11, 2, "o")
    for c in (4, 6, 8, 10):
        _px(g, 5, c, "o")
        _px(g, 10, c, "o")
    _rect(g, 6, 3, 9, 4, "o")
    return _rows(g)


def _magazine_family() -> list[str]:
    g = _new_grid()
    _rect(g, 2, 6, 4, 10, "#")
    _rect(g, 3, 7, 3, 9, "o")
    for i, r in enumerate(range(5, 14)):
        t = i / 8
        width = round(5 - 1 * t)
        c_center = round(8 - 2 * t)
        c0 = c_center - width // 2
        c1 = c0 + width
        _rect(g, r, c0, r, c1, "#")
        _px(g, r, c0, "o")
        if width >= 4:
            _px(g, r, c0 + 1, "o")
    return _rows(g)


def _ammo_family() -> list[str]:
    g = _new_grid()
    _rect(g, 1, 6, 4, 9, "#")
    _rect(g, 5, 5, 11, 10, "#")
    _rect(g, 6, 6, 10, 9, "o")
    _rect(g, 12, 5, 13, 10, "#")
    return _rows(g)


def _armor_family() -> list[str]:
    g = _new_grid()
    _rect(g, 3, 5, 4, 6, "#")
    _rect(g, 3, 9, 4, 10, "#")
    _rect(g, 4, 3, 12, 12, "#")
    _rect(g, 5, 4, 11, 11, "o")
    _rect(g, 7, 6, 9, 9, "#")
    return _rows(g)


def _book_family() -> list[str]:
    g = _new_grid()
    _rect(g, 2, 3, 13, 12, "#")
    _rect(g, 3, 4, 12, 11, "o")
    _rect(g, 2, 7, 13, 8, "#")
    for r in range(4, 12, 2):
        _line(g, r, 4, r, 6, "#")
        _line(g, r, 9, r, 11, "#")
    return _rows(g)


def _food_family() -> list[str]:
    g = _new_grid()
    _px(g, 2, 8, "#")
    _px(g, 1, 9, "#")
    _circle(g, 9, 8, 6, "#")
    _circle(g, 9, 8, 3, "o")
    return _rows(g)


def _container_family() -> list[str]:
    g = _new_grid()
    _rect(g, 2, 6, 4, 9, "#")
    _rect(g, 4, 5, 5, 10, "#")
    _rect(g, 5, 3, 13, 12, "#")
    _rect(g, 6, 4, 12, 11, "o")
    _rect(g, 8, 4, 8, 11, "#")
    return _rows(g)


def _tool_family() -> list[str]:
    g = _new_grid()
    _rect(g, 1, 5, 5, 11, "#")
    _rect(g, 2, 6, 4, 10, "o")
    _rect(g, 5, 7, 14, 9, "#")
    _rect(g, 6, 7, 13, 8, "o")
    return _rows(g)


def _engine_family() -> list[str]:
    g = _new_grid()
    _rect(g, 4, 3, 12, 13, "#")
    _rect(g, 5, 4, 11, 12, "o")
    for c in (5, 8, 11):
        _rect(g, 1, c, 4, c + 1, "#")
    _rect(g, 7, 5, 9, 11, "#")
    return _rows(g)


def _wheel_family() -> list[str]:
    g = _new_grid()
    _circle(g, 8, 8, 7, "#")
    _circle(g, 8, 8, 5, "o")
    _circle(g, 8, 8, 2, "#")
    return _rows(g)


def _bionic_family() -> list[str]:
    g = _new_grid()
    _rect(g, 3, 3, 12, 12, "#")
    _rect(g, 4, 4, 11, 11, "o")
    for r, c in ((2, 5), (2, 9), (13, 5), (13, 9)):
        _px(g, r, c, "#")
    _rect(g, 6, 6, 9, 9, "#")
    return _rows(g)


def _generic_family() -> list[str]:
    g = _new_grid()
    _rect(g, 4, 4, 11, 11, "#")
    _rect(g, 5, 5, 10, 10, "o")
    return _rows(g)


def _z_zombie_family() -> list[str]:
    g = _new_grid()
    _circle(g, 3, 8, 2, "#")
    _circle(g, 3, 8, 1, "o")
    _rect(g, 5, 6, 10, 10, "#")
    _rect(g, 6, 6, 9, 9, "o")
    _line(g, 6, 6, 9, 2, "#")
    _line(g, 7, 6, 9, 3, "o")
    _line(g, 6, 10, 9, 13, "#")
    _rect(g, 10, 6, 14, 7, "#")
    _rect(g, 10, 9, 14, 10, "#")
    return _rows(g)


def _z_human_family() -> list[str]:
    g = _new_grid()
    _circle(g, 2, 8, 2, "#")
    _rect(g, 4, 6, 5, 10, "#")
    _rect(g, 5, 5, 10, 11, "#")
    _rect(g, 6, 6, 9, 10, "o")
    _rect(g, 5, 3, 10, 4, "#")
    _rect(g, 5, 12, 10, 13, "#")
    _rect(g, 10, 6, 14, 7, "#")
    _rect(g, 10, 9, 14, 10, "#")
    return _rows(g)


def _z_mammal_family() -> list[str]:
    g = _new_grid()
    _circle(g, 9, 2, 2, "#")
    _rect(g, 7, 4, 11, 13, "#")
    _rect(g, 8, 5, 10, 12, "o")
    for c in (5, 8, 11, 13):
        _rect(g, 11, c, 14, c + 1, "#")
    _line(g, 7, 13, 9, 15, "#")
    return _rows(g)


def _z_bird_family() -> list[str]:
    g = _new_grid()
    _circle(g, 7, 9, 3, "#")
    _circle(g, 7, 9, 2, "o")
    _circle(g, 4, 12, 2, "#")
    _line(g, 4, 14, 4, 15, "#")
    _line(g, 5, 8, 9, 1, "#")
    _line(g, 6, 8, 10, 2, "#")
    _line(g, 7, 8, 9, 3, "o")
    _rect(g, 10, 8, 12, 8, "#")
    _rect(g, 10, 10, 12, 10, "#")
    return _rows(g)


def _z_fish_family() -> list[str]:
    g = _new_grid()
    _circle(g, 8, 7, 5, "#", y_scale=1.3)
    _circle(g, 8, 7, 3, "o", y_scale=1.3)
    _line(g, 5, 13, 8, 15, "#")
    _line(g, 11, 13, 8, 15, "#")
    _line(g, 5, 7, 3, 7, "#")
    return _rows(g)


def _z_worm_family() -> list[str]:
    g = _new_grid()
    coords = [(2, 3), (3, 5), (4, 7), (5, 9), (6, 10), (7, 11), (8, 12), (9, 12), (10, 11), (11, 9), (12, 7)]
    for i, (r, c) in enumerate(coords):
        _rect(g, r, c, r + 1, c + 1, "#" if i % 2 == 0 else "o")
    return _rows(g)


def _z_insect_family() -> list[str]:
    g = _new_grid()
    _circle(g, 4, 8, 2, "#")
    _circle(g, 9, 8, 4, "#")
    _circle(g, 9, 8, 2, "o")
    for dc in (-6, -3, 3, 6):
        _line(g, 9, 8, 12, 8 + dc, "#")
    _line(g, 4, 6, 2, 3, "o")
    _line(g, 4, 10, 2, 13, "o")
    return _rows(g)


def _z_reptile_family() -> list[str]:
    g = _new_grid()
    _circle(g, 9, 3, 2, "#")
    _rect(g, 8, 5, 11, 13, "#")
    _rect(g, 9, 6, 10, 12, "o")
    for c in (6, 9, 12):
        _rect(g, 11, c, 13, c + 1, "#")
    _line(g, 9, 13, 7, 15, "#")
    return _rows(g)


def _z_robot_family() -> list[str]:
    g = _new_grid()
    _line(g, 1, 8, 3, 8, "#")
    _rect(g, 3, 5, 6, 11, "#")
    _rect(g, 4, 6, 5, 10, "o")
    _rect(g, 7, 4, 12, 12, "#")
    _rect(g, 8, 5, 11, 11, "o")
    _rect(g, 9, 6, 10, 10, "#")
    _rect(g, 12, 5, 14, 6, "#")
    _rect(g, 12, 10, 14, 11, "#")
    return _rows(g)


def _z_plant_family() -> list[str]:
    g = _new_grid()
    _line(g, 2, 8, 12, 8, "#")
    _line(g, 3, 8, 12, 8, "o")
    for r, dc in ((4, -4), (4, 4), (7, -5), (7, 5), (9, -3), (9, 3)):
        _line(g, r, 8, r - 2, 8 + dc, "#")
    _circle(g, 3, 8, 2, "#")
    return _rows(g)


def _z_fungus_family() -> list[str]:
    g = _new_grid()
    for r in range(0, 8):
        for c in range(CELL):
            if math.hypot(r - 7, (c - 8) * 0.8) <= 7 and r <= 7:
                _px(g, r, c, "#")
    _rect(g, 7, 1, 7, 14, "#")
    for c in (3, 6, 9, 12):
        _px(g, 2, c, "o")
        _px(g, 4, c + 1, "o")
    _rect(g, 8, 6, 13, 9, "#")
    _rect(g, 9, 7, 12, 8, "o")
    return _rows(g)


def _z_nether_family() -> list[str]:
    g = _new_grid()
    _circle(g, 8, 8, 5, "#")
    _circle(g, 8, 8, 3, "o")
    for ang in range(0, 360, 45):
        r2 = 8 + round(6 * math.sin(math.radians(ang)))
        c2 = 8 + round(6 * math.cos(math.radians(ang)))
        _line(g, 8, 8, r2, c2, "o")
    _circle(g, 8, 8, 1, "#")
    return _rows(g)


def _z_blob_family() -> list[str]:
    g = _new_grid()
    _circle(g, 10, 8, 6, "#")
    _circle(g, 10, 8, 4, "o")
    _circle(g, 6, 5, 2, "#")
    _circle(g, 5, 11, 2, "#")
    return _rows(g)


def _z_mutant_family() -> list[str]:
    g = _new_grid()
    _circle(g, 3, 7, 2, "#")
    _rect(g, 5, 5, 10, 10, "#")
    _rect(g, 6, 6, 9, 9, "o")
    _line(g, 5, 5, 2, 2, "#")
    _circle(g, 2, 2, 2, "o")
    _line(g, 6, 10, 9, 13, "#")
    _rect(g, 10, 6, 14, 7, "#")
    _rect(g, 10, 9, 14, 10, "#")
    return _rows(g)


ITEM_FAMILY_BUILDERS: dict[str, Callable[[], list[str]]] = {
    "gun": _gun_family,
    "mod": _mod_family,
    "magazine": _magazine_family,
    "ammo": _ammo_family,
    "armor": _armor_family,
    "book": _book_family,
    "food": _food_family,
    "container": _container_family,
    "tool": _tool_family,
    "engine": _engine_family,
    "wheel": _wheel_family,
    "bionic": _bionic_family,
    "generic": _generic_family,
}

MONSTER_FAMILY_BUILDERS: dict[str, Callable[[], list[str]]] = {
    "z_zombie": _z_zombie_family,
    "z_human": _z_human_family,
    "z_mammal": _z_mammal_family,
    "z_bird": _z_bird_family,
    "z_fish": _z_fish_family,
    "z_worm": _z_worm_family,
    "z_insect": _z_insect_family,
    "z_reptile": _z_reptile_family,
    "z_robot": _z_robot_family,
    "z_plant": _z_plant_family,
    "z_fungus": _z_fungus_family,
    "z_nether": _z_nether_family,
    "z_blob": _z_blob_family,
    "z_mutant": _z_mutant_family,
}

ITEM_MASKS: dict[str, list[str]] = {name: build() for name, build in ITEM_FAMILY_BUILDERS.items()}
MONSTER_MASKS: dict[str, list[str]] = {name: build() for name, build in MONSTER_FAMILY_BUILDERS.items()}

for _name, _mask in {**ITEM_MASKS, **MONSTER_MASKS}.items():
    assert len(_mask) == CELL, f"{_name}: expected {CELL} rows, got {len(_mask)}"
    for _row in _mask:
        assert len(_row) == CELL, f"{_name}: row {_row!r} is not {CELL} chars"

ITEM_TYPE_TO_FAMILY: dict[str, str] = {
    "GUN": "gun",
    "GUNMOD": "mod",
    "TOOLMOD": "mod",
    "MAGAZINE": "magazine",
    "AMMO": "ammo",
    "BATTERY": "ammo",
    "ARMOR": "armor",
    "PET_ARMOR": "armor",
    "TOOL_ARMOR": "armor",
    "BOOK": "book",
    "COMESTIBLE": "food",
    "CONTAINER": "container",
    "TOOL": "tool",
    "ENGINE": "engine",
    "WHEEL": "wheel",
    "BIONIC_ITEM": "bionic",
    "GENERIC": "generic",
}

SPECIES_TO_FAMILY: dict[str, str] = {
    "ZOMBIE": "z_zombie",
    "HUMAN": "z_human",
    "MAMMAL": "z_mammal",
    "PLEISTOCENE_MAMMAL": "z_mammal",
    "BIRD": "z_bird",
    "FISH": "z_fish",
    "MOLLUSK": "z_worm",
    "WORM": "z_worm",
    "NEMATODE": "z_worm",
    "INSECT": "z_insect",
    "SPIDER": "z_insect",
    "REPTILE": "z_reptile",
    "AMPHIBIAN": "z_reptile",
    "ROBOT": "z_robot",
    "CYBORG": "z_robot",
    "PLANT": "z_plant",
    "FUNGUS": "z_fungus",
    "NETHER": "z_nether",
    "HORROR": "z_nether",
    "HALLUCINATION": "z_nether",
    "SLIME": "z_blob",
    "BLOB": "z_blob",
    "MUTANT": "z_mutant",
    "CENTIPEDE": "z_insect",
    "INSECT_FLYING": "z_insect",
    "ABERRATION": "z_nether",
    "LEECH_PLANT": "z_plant",
}

COLOR_TOKEN_TO_NAME: dict[str, str] = {
    "black": "BLACK",
    "white": "WHITE",
    "red": "RED",
    "green": "GREEN",
    "blue": "BLUE",
    "cyan": "CYAN",
    "magenta": "MAGENTA",
    "yellow": "YELLOW",
    "brown": "BROWN",
    "pink": "LMAGENTA",
    "gray": "GRAY",
    "light_gray": "GRAY",
    "dark_gray": "DGRAY",
    "light_red": "LRED",
    "light_green": "LGREEN",
    "light_blue": "LBLUE",
    "light_cyan": "LCYAN",
    "light_magenta": "LMAGENTA",
}


def log(msg: str) -> None:
    print(msg, file=sys.stderr)


def load_json_entries(path: Path) -> list[dict[str, Any]]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:  # noqa: BLE001
        log(f"skip unparsable {path}: {exc}")
        return []
    if isinstance(data, dict):
        data = [data]
    if not isinstance(data, list):
        return []
    return [entry for entry in data if isinstance(entry, dict)]


REPORTABLE_TYPES = {
    "AMMO", "ARMOR", "BATTERY", "BIONIC_ITEM", "BOOK", "COMESTIBLE", "CONTAINER",
    "ENGINE", "GENERIC", "GUN", "GUNMOD", "MAGAZINE", "MONSTER", "PET_ARMOR",
    "TOOL", "TOOLMOD", "TOOL_ARMOR", "WHEEL",
}


def _prefer(existing: dict[str, Any] | None, new: dict[str, Any]) -> dict[str, Any]:
    """Resolve an id collision between two JSON objects sharing the same id.

    Some ids (e.g. `tread1`) name both a real item/monster definition and an
    unrelated object of a different `type` (vehicle_part, material,
    ammunition_type, requirement, vehicle, MIGRATION, ...) that happens to
    reuse the same id string by convention. A REPORTABLE_TYPES entry always
    wins so the id's own item/monster identity survives, regardless of glob
    file order.
    """
    if existing is None:
        return new
    existing_ok = existing.get("type") in REPORTABLE_TYPES
    new_ok = new.get("type") in REPORTABLE_TYPES
    if new_ok and not existing_ok:
        return new
    if existing_ok and not new_ok:
        return existing
    return new  # last-wins when both or neither are reportable-type


def build_definition_index(data_root: Path) -> dict[str, dict[str, Any]]:
    """Index every JSON object with an 'id' or 'abstract' key, by that key."""
    index: dict[str, dict[str, Any]] = {}
    for path in sorted(data_root.rglob("*.json")):
        if SKIP_INPUT_JSON.search(path.name):
            continue
        for entry in load_json_entries(path):
            key = entry.get("id") if "id" in entry else entry.get("abstract")
            if isinstance(key, str):
                index[key] = _prefer(index.get(key), entry)
            elif isinstance(key, list):
                for k in key:
                    if isinstance(k, str):
                        index[k] = _prefer(index.get(k), entry)
    return index


def first_color_token(color: Any) -> str | None:
    if isinstance(color, str):
        return color
    if isinstance(color, list):
        for c in color:
            if isinstance(c, str):
                return c
    return None


def normalize_color_token(raw: str) -> str:
    """Extract the foreground color-name token from a raw color string."""
    token = raw.lower()
    for prefix in ("c_", "i_", "h_"):
        if token.startswith(prefix):
            token = token[len(prefix):]
            break
    parts = token.split("_")
    if parts and parts[0] in ("light", "dark") and len(parts) > 1:
        fg = "_".join(parts[:2])
    else:
        fg = parts[0] if parts else token
    return fg


def load_colordef(colors_path: Path) -> dict[str, tuple[int, int, int]]:
    data = json.loads(colors_path.read_text(encoding="utf-8"))
    colordef = data[0] if isinstance(data, list) else data
    out: dict[str, tuple[int, int, int]] = {}
    for key, value in colordef.items():
        if isinstance(value, list) and len(value) == 3:
            out[key] = (int(value[0]), int(value[1]), int(value[2]))
    return out


def resolve_chain(
    own_id: str,
    index: dict[str, dict[str, Any]],
) -> tuple[dict[str, Any], str | None, list[str], str]:
    """Walk copy-from up to MAX_COPY_FROM_HOPS hops.

    Returns (own_entry_or_empty, first_color_raw, first_species, first_symbol).
    """
    own_entry = index.get(own_id, {})
    color_raw: str | None = None
    species: list[str] = []
    symbol = ""

    visited: set[str] = set()
    current_id: str | None = own_id
    hops = 0
    while current_id is not None and hops <= MAX_COPY_FROM_HOPS:
        if current_id in visited:
            break
        visited.add(current_id)
        entry = index.get(current_id)
        if entry is None:
            break
        if color_raw is None:
            c = first_color_token(entry.get("color"))
            if c:
                color_raw = c
        if not species:
            sp = entry.get("species")
            if isinstance(sp, list) and sp and all(isinstance(s, str) for s in sp):
                species = sp
        if not symbol:
            sym = entry.get("symbol")
            if isinstance(sym, str) and sym:
                symbol = sym
        current_id = entry.get("copy-from") if isinstance(entry.get("copy-from"), str) else None
        hops += 1

    return own_entry, color_raw, species, symbol


def resolve_family_item(own_type: str) -> str:
    family = ITEM_TYPE_TO_FAMILY.get(own_type)
    if family is None:
        log(f"unmapped item type {own_type!r}; defaulting to generic")
        return "generic"
    return family


def resolve_family_monster(species: list[str], symbol: str) -> str:
    if species:
        family = SPECIES_TO_FAMILY.get(species[0])
        if family is not None:
            return family
        log(f"unmapped species {species[0]!r}; defaulting to z_blob")
        return "z_blob"
    if symbol.isalpha():
        return "z_mammal"
    if symbol.isdigit():
        return "z_robot"
    return "z_blob"


def color_to_rgb(color_raw: str | None, colordef: dict[str, tuple[int, int, int]]) -> tuple[int, int, int]:
    if color_raw is None:
        return colordef.get("WHITE", (255, 255, 255))
    token = normalize_color_token(color_raw)
    name = COLOR_TOKEN_TO_NAME.get(token)
    if name is None:
        log(f"unmapped color token {token!r} (from {color_raw!r}); defaulting to WHITE")
        return colordef.get("WHITE", (255, 255, 255))
    return colordef.get(name, colordef.get("WHITE", (255, 255, 255)))


def detail_from_primary(primary: tuple[int, int, int]) -> tuple[int, int, int]:
    return tuple(round(c * 0.45) for c in primary)  # type: ignore[return-value]


def render_icon(mask: list[str], primary: tuple[int, int, int], detail: tuple[int, int, int]) -> Image.Image:
    img = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    px = img.load()
    pr = (*primary, 255)
    dt = (*detail, 255)
    for y, row in enumerate(mask):
        for x, ch in enumerate(row):
            if ch == "#":
                px[x, y] = pr
            elif ch == "o":
                px[x, y] = dt
            # '.' stays transparent
    return img


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--missing", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--colors", type=Path, required=True)
    parser.add_argument("--out-sheet", type=Path, required=True)
    parser.add_argument("--out-entries", type=Path, required=True)
    parser.add_argument("--out-resolved", type=Path, required=True)
    args = parser.parse_args()

    report = json.loads(args.missing.read_text(encoding="utf-8"))
    target_entries = report["missing"]["entries"]
    log(f"target ids: {len(target_entries)}")

    index = build_definition_index(args.data)
    log(f"indexed definitions: {len(index)}")

    colordef = load_colordef(args.colors)

    # (family, primary_rgb) -> local cell index
    cell_of: dict[tuple[str, tuple[int, int, int]], int] = {}
    cells: list[Image.Image] = []

    generated_entries: list[dict[str, Any]] = []
    resolved_info: dict[str, dict[str, Any]] = {}

    for row in target_entries:
        tid = row["id"]
        path = row.get("path", "")
        own_entry, color_raw, species, symbol = resolve_chain(tid, index)
        own_type = own_entry.get("type") if isinstance(own_entry, dict) else None

        if not own_type:
            # Contingency: no discoverable raw entry -> classify from report path.
            is_monster = "/monsters/" in path.replace("\\", "/")
            own_type = "MONSTER" if is_monster else "GENERIC"
            log(f"no raw entry for {tid!r}; inferred type={own_type} from path")

        if own_type == "MONSTER":
            family = resolve_family_monster(species, symbol)
            mask = MONSTER_MASKS[family]
        else:
            family = resolve_family_item(own_type)
            mask = ITEM_MASKS[family]

        primary = color_to_rgb(color_raw, colordef)
        detail = detail_from_primary(primary)

        key = (family, primary)
        if key not in cell_of:
            cell_of[key] = len(cells)
            cells.append(render_icon(mask, primary, detail))
        local = cell_of[key]

        generated_entries.append({"id": tid, "local": local})
        resolved_info[tid] = {
            "type": own_type,
            "family": family,
            "species": species,
            "symbol": symbol,
            "color_raw": color_raw,
            "primary_rgb": list(primary),
            "path": path,
        }

    n_cells = len(cells)
    cols = COLS
    rows = (n_cells + cols - 1) // cols
    sheet_w = cols * CELL
    sheet_h = max(rows, 1) * CELL
    sheet = Image.new("RGBA", (sheet_w, sheet_h), (0, 0, 0, 0))
    for idx, cell_img in enumerate(cells):
        cx = (idx % cols) * CELL
        cy = (idx // cols) * CELL
        sheet.paste(cell_img, (cx, cy), cell_img)

    args.out_sheet.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.out_sheet)
    log(f"wrote {args.out_sheet} ({sheet_w}x{sheet_h}, {n_cells} unique cells for {len(generated_entries)} ids)")

    args.out_entries.write_text(json.dumps(generated_entries, indent=2) + "\n", encoding="utf-8")
    log(f"wrote {args.out_entries}")

    args.out_resolved.write_text(json.dumps(resolved_info, indent=2) + "\n", encoding="utf-8")
    log(f"wrote {args.out_resolved}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
