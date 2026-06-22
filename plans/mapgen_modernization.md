# Mapgen Modernization — Plan

## Context

`mapgen.cpp` (6766 lines) has **83 manual index loops** — the highest count of any file in the project. It is dominated by C-style 2D grid traversal:

```cpp
for( int i = -MON_RADIUS; i <= MON_RADIUS; i++ ) {
    for( int j = -MON_RADIUS; j <= MON_RADIUS; j++ ) {
        density += omap.ter( abs_omt + point_rel_omt( i, j ) )->get_mondensity();
    }
}

for( int y = 2; y < SEEY * 2 - 2; y++ ) {
    for( int x = 2; x < SEEX * 2 - 2; x++ ) {
        // tile-by-tile operations
    }
}
```

| Metric | Value |
|--------|-------|
| Lines | 6766 |
| Manual index loops | 83 |
| `ranges::` | 1 |
| `std::views` | 0 |
| Trailing returns | ~5 |
| `std::optional` | 0 |
| Raw `new`/`delete` | 4/3 |

This plan covers code modernization only. No behavioral changes to mapgen output.

## Phases

### Phase 1 — Introduce `mapgen_grid` helper

Create a lightweight view over a `SEEX*2 × SEEY*2` (or overmap-tile) grid:

```cpp
class mapgen_grid {
    mapgendata &md;
    point pos;
    point size;
public:
    auto for_each_tile( auto f ) const -> void {
        for( int y = pos.y; y < pos.y + size.y; y++ )
            for( int x = pos.x; x < pos.x + size.x; x++ )
                f( point( x, y ) );
    }
    auto for_each_tile_rect( point from, point to, auto f ) const -> void { ... }
    auto transform_tile( point p, auto f ) -> decltype(f(p)) { ... }
    auto tile_range() const { return std::views::cartesian_product( ... ); }
};
```

Each method uses `std::invoke` for generality. This step adds the abstraction and converts 5–10 of the most common index loops as a proof of concept, without changing any output.

### Phase 2 — Convert all index loops to algorithms

Convert the remaining 70+ manual index loops to use `mapgen_grid` methods or `std::views::cartesian_product` + `ranges::for_each`. Each conversion is individually reviewable. Split into sub-phases by mapgen function domain:

- 2a: Terrain/furniture placement loops (most common)
- 2b: Monster/vehicle spawn loops
- 2c: Special building loops (note: many are nested index loops)

Conversion pattern:

| Before | After |
|--------|-------|
| `for( int y = 2; y < SEEY * 2 - 2; y++ ) for( int x = 2; ... ) { ... }` | `grid.for_each_tile_rect({2,2}, {SEEY*2-2, SEEX*2-2}, [&](point p) { ... })` |
| `for( int i = -R; i <= R; i++ ) for( int j = -R; ... ) { ... }` | `for( auto [i, j] : std::views::cartesian_product( views::iota(-R, R+1), views::iota(-R, R+1) ) ) { ... }` |

### Phase 3 — Decompose monolithic functions

Several `mapgen_*` functions exceed 300 lines. Extract:

- `mapgen_house()` → `mapgen_house_bedroom()`, `mapgen_house_kitchen()`, etc. (building-room generators)
- `mapgen_lab()` → `mapgen_lab_corridor()`, `mapgen_lab_room()`, etc.
- Shared terrain-pattern helpers → `mapgen_terrain_utils.cpp`

Move building-specific generators out into a new `mapgen_building.cpp` (target: `mapgen.cpp` < 4000 lines).

### Phase 4 — C++23 modernization pass

- Options structs for functions with 4+ bare params (common in mapgen: `place_items(ter_id, items, chance, x1, y1, x2, y2, ...)`)
- `std::expected` for mapgen validation (e.g., `validate_connection` → returns error or success)
- Trailing return types
- `auto` for local variables
- Raw `new`/`delete` → `std::unique_ptr` / automatic storage

## Verification (per phase)

- Build green. Phase 1: same `tests/mapgen_test.cpp` output.
- Phase 2: `rg "for\s*\(\s*(int|size_t|long)\s+\w+\s*=\s*" src/mapgen.cpp` count drops from 83 to ~10.
- Phase 3: `wc -l src/mapgen.cpp` drops to < 4000.
- All phases: `tests/mapgen_test.cpp` passes identically.

## Files

| File | Phase |
|------|-------|
| `src/mapgen.cpp` | 1–4 |
| `src/mapgen_grid.{h,cpp}` (new) | 1 |
| `src/mapgen_building.cpp` (new) | 3 |
| `src/mapgen_terrain_utils.cpp` (new) | 3 |
| `src/CMakeLists.txt` | 1, 3 |

## Effort: 2–3 weeks
- Phase 1: 2–3 days (grid abstraction + 10 conversions)
- Phase 2: 3–5 days (70+ loop conversions, most mechanical)
- Phase 3: 2–3 days (decomposition)
- Phase 4: 1–2 days (C++23)
