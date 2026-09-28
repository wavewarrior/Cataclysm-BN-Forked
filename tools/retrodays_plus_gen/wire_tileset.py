#!/usr/bin/env python3
"""Append the generated_plus.png / bespoke_plus.png sheets to
gfx/RetroDays+Tileset/tile_config.json, append-only.

Pure text-splice: the existing file bytes are never re-serialised. This
keeps the diff limited to exactly the two appended sheet blocks and leaves
every existing sprite index untouched.

Both new sheets are native 16x16 pixels. `"pixelscale": 0.625` (= 10/16, the
tileset's base tile_info width divided by the sprite's native width) makes
each icon render at the same one-tile on-screen footprint as the existing
10x10 sprites — see cata_tiles.cpp's `destination.w = width * tile_width *
tile.pixelscale / tileset->get_tile_width()`.

Usage:
    python3 tools/retrodays_plus_gen/wire_tileset.py \
        --tile-config "gfx/RetroDays+Tileset/tile_config.json" \
        --generated-sheet generated_plus.png \
        --generated-entries /tmp/rdp_generated_entries.json \
        --bespoke-sheet bespoke_plus.png \
        --bespoke-entries /tmp/rdp_bespoke_entries.json \
        --tileset-dir "gfx/RetroDays+Tileset"
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from PIL import Image

CELL = 16
PIXELSCALE = CELL and round(10 / CELL, 6)  # 0.625: base tile_info is 10x10


def log(msg: str) -> None:
    print(msg, file=sys.stderr)


def compute_running_base(config: dict[str, Any], tileset_dir: Path) -> int:
    base = 0
    for sheet in config["tiles-new"]:
        img = Image.open(tileset_dir / sheet["file"])
        sw = sheet.get("sprite_width") or config["tile_info"][0]["width"]
        sh = sheet.get("sprite_height") or config["tile_info"][0]["height"]
        base += (img.width // sw) * (img.height // sh)
    return base


def find_array_span(text: str, key: str) -> tuple[int, int]:
    """Return (open_bracket_index, close_bracket_index) for `"key": [ ... ]`."""
    key_idx = text.index(f'"{key}"')
    open_idx = text.index("[", key_idx)
    depth = 0
    in_string = False
    escape = False
    i = open_idx
    while i < len(text):
        ch = text[i]
        if in_string:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
        else:
            if ch == '"':
                in_string = True
            elif ch == "[":
                depth += 1
            elif ch == "]":
                depth -= 1
                if depth == 0:
                    return open_idx, i
        i += 1
    raise ValueError(f"unbalanced array for key {key!r}")


def format_tile_entry(entry: dict[str, Any]) -> str:
    return f'{{ "id": "{entry["id"]}", "fg": {entry["fg"]}, "rotates": false }}'


def format_sheet(file_name: str, entries: list[dict[str, Any]]) -> str:
    lines = [
        "    {",
        f'      "file": "{file_name}",',
        '      "sprite_width": 16,',
        '      "sprite_height": 16,',
        f'      "pixelscale": {PIXELSCALE},',
        '      "tiles": [',
    ]
    for i, entry in enumerate(entries):
        comma = "," if i < len(entries) - 1 else ""
        lines.append(f"        {format_tile_entry(entry)}{comma}")
    lines.append("      ]")
    lines.append("    }")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tile-config", type=Path, required=True)
    parser.add_argument("--tileset-dir", type=Path, required=True)
    parser.add_argument("--generated-sheet", type=str, required=True, help="filename relative to tileset-dir")
    parser.add_argument("--generated-entries", type=Path, required=True)
    parser.add_argument("--bespoke-sheet", type=str, required=True, help="filename relative to tileset-dir")
    parser.add_argument("--bespoke-entries", type=Path, required=True)
    parser.add_argument("--expected-base", type=int, default=7249)
    args = parser.parse_args()

    text = args.tile_config.read_text(encoding="utf-8")
    config = json.loads(text)

    running_base = compute_running_base(config, args.tileset_dir)
    log(f"computed running base = {running_base}")
    assert running_base == args.expected_base, (
        f"running base {running_base} != expected {args.expected_base}; "
        "tile_config.json changed since the plan was authored"
    )

    generated_entries = json.loads(args.generated_entries.read_text(encoding="utf-8"))
    bespoke_entries = json.loads(args.bespoke_entries.read_text(encoding="utf-8"))
    bespoke_ids = {e["id"] for e in bespoke_entries}

    # Bespoke entries take precedence: drop any id from the procedural sheet
    # that also has a hand-authored bespoke sprite.
    filtered_generated = [e for e in generated_entries if e["id"] not in bespoke_ids]
    dropped = len(generated_entries) - len(filtered_generated)
    log(f"dropping {dropped} generated entries superseded by bespoke sprites")

    gen_img = Image.open(args.tileset_dir / args.generated_sheet)
    gen_cell_count = (gen_img.width // CELL) * (gen_img.height // CELL)

    gen_base = running_base
    bes_base = gen_base + gen_cell_count

    gen_fg_entries = [{"id": e["id"], "fg": gen_base + e["local"]} for e in filtered_generated]
    bes_fg_entries = [{"id": e["id"], "fg": bes_base + e["local"]} for e in bespoke_entries]

    log(f"generated_plus.png: base={gen_base}, {len(gen_fg_entries)} entries, {gen_cell_count} cells")
    log(f"bespoke_plus.png: base={bes_base}, {len(bes_fg_entries)} entries")

    sheet1_block = format_sheet(args.generated_sheet, gen_fg_entries)
    sheet2_block = format_sheet(args.bespoke_sheet, bes_fg_entries)

    open_idx, close_idx = find_array_span(text, "tiles-new")
    insert_pos = close_idx
    while insert_pos > 0 and text[insert_pos - 1] in " \t\r\n":
        insert_pos -= 1
    # insert_pos now sits right after the last existing sheet's closing '}'
    insertion = f",\n{sheet1_block},\n{sheet2_block}"
    new_text = text[:insert_pos] + insertion + text[insert_pos:]

    # Sanity: the result must still be valid JSON and array-length must grow by 2.
    # Validate BEFORE writing so a bad splice never touches disk.
    new_config = json.loads(new_text)
    assert len(new_config["tiles-new"]) == len(config["tiles-new"]) + 2

    args.tile_config.write_text(new_text, encoding="utf-8")
    log(f"wrote {args.tile_config}")
    log(f"tiles-new sheet count: {len(config['tiles-new'])} -> {len(new_config['tiles-new'])}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
