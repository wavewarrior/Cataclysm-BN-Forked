# Tier 3a — Measure the bubble cost curve

## STATUS (reviewed 2026-06-27)
0% — pure measurement/decision plan, no code to implement. Premises confirmed:
init_bubble_config (game.cpp), REALITY_BUBBLE_SIZE option, dense level_cache array all match.
Still self-contained and useful; its conclusion gates whether to raise the default (correctly
says "not until 1a/1b land" — 1a HAS landed, 1b has not). Standalone, no overlap with the
other plans. KEEP as-is. Low priority until someone wants to raise the default bubble size.

## Context

`REALITY_BUBBLE_SIZE` is a runtime option (1–16, default 4). Cost grows
~quadratically with the radius. We need quantified data to:
1. Decide if the default (4) is optimal.
2. Decide whether to cap or auto-scale by hardware.
3. Inform the Tier-1 incremental/amortised rebuild design (they make larger
   bubbles affordable).

## Current state

- Default 4 → `g_mapsize = 11` → 132×132 tiles = upstream parity.
- Each increment adds ~2 submaps to each axis → cost ~`O(size²)` in both RAM
  and `build_map_cache` time.
- `init_bubble_config` (`game.cpp:246`): `g_half_mapsize = size + 1`,
  `g_mapsize = 2 * half + 1`.

### Cache memory per tile

`level_cache` contains (per z, 21 z-levels):
- `lm`: 4 × `float` = 16 B
- `sm`: 1 × `float` = 4 B
- `light_source_buffer`: 1 × struct (≈16 B)
- `outside_cache`: 1 × `char` = 1 B
- `sheltered_cache`: 1 × `char` = 1 B
- `floor_cache`: 1 × `char` = 1 B
- `transparency_cache`: 1 × `float` = 4 B
- `vehicle_floor_cache`: 1 × `char` = 1 B
- `seen_cache`: 1 × `char` = 1 B (approx)
- `visibility_cache`: 1 × `char` = 1 B
- etc.

Rough estimate: ~50 B / tile × 21 z × tiles.

## Measurement plan

### Step 1 — RAM measurement

For each bubble size 3, 4, 6, 8:

1. Start a new world, stand at a fixed position.
2. Read the process RSS from the OS:
   ```sh
   ps -o rss= -p $(pgrep -f cataclysm-bn-tiles)
   ```
3. Record and compute per-size delta.

### Step 2 — Cache rebuild time

For each bubble size 3, 4, 6, 8:

1. Walk 5 submap crossings while `[build_cache][perf]` is logging.
2. Extract `total=` ms values:
   ```sh
   rg '\[build_cache\]\[perf\]' debug.log | awk '{print $NF}' | sort -n
   ```
3. Record P50 and P95 for each size.

### Step 3 — Visual quality

For each bubble size 3, 4, 6, 8:
- Does the visible world extend far enough for normal gameplay?
- Are there pop-in artifacts at the screen edge?
- SDF quality (GPU lighting) unchanged — the SDF is sized for `MAPSIZE_MAX` at
  init, so it still covers the full maximum bubble regardless of current size.

## Expected results

| Size | g_mapsize | Tiles | Relative tiles | Cache RAM (est) | build_map_cache P50 |
|------|-----------|-------|----------------|-----------------|---------------------|
| 3    | 9         | 108²  | 0.67×          | ~500 MB         | ~6-8ms              |
| 4    | 11        | 132²  | 1.00×          | ~750 MB         | ~10-12ms            |
| 6    | 15        | 180²  | 1.86×          | ~1.4 GB         | ~18-22ms            |
| 8    | 19        | 228²  | 2.98×          | ~2.2 GB         | ~30-36ms            |

Measured numbers will differ from estimates; this table is a rough guide.

## Recommendation criteria

- **Keep default 4** if size 4 is ≤15ms P95 (near the 16.6ms frame budget) and
  size 6 is consistently >16.6ms P95.
- **Auto-scale** if the cost curve shows a clear hardware boundary (e.g., 8+ GB
  RAM machines can handle size 6, 4 GB machines should stay at 4). Use
  `std::thread::hardware_concurrency()` and total RAM at startup.
- **Do not raise default** until Tier 1a/1b (incremental lightmap, amortised
  rebuild) land — those make larger bubbles affordable.
