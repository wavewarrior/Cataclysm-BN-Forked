# Overmap Modernization — Plan

## Context

`overmap.cpp` (6700 lines) is the second-largest file in the gameplay core (after `item.cpp`). It carries 26 TODO markers, public data members in `overmap.h`, and a partial migration to JSON-driven loading. `overmapbuffer.cpp` (2235 lines) adds 18 more TODOs.

| Metric | overmap.cpp | overmapbuffer.cpp | overmap_ui.cpp |
|--------|-------------|-------------------|----------------|
| Lines | 6700 | 2235 | 2767 |
| TODO/FIXME markers | 26 | 18 | 7 |
| `ranges::` | ~3 | ~5 | ~2 |
| Trailing returns | few | few | few |
| Public data members | overmap.h:434 "TODO: make private" | — | — |

This plan covers code quality decomposition and modernization. Overmap generation logic is not changed.

## Phases

### Phase 1 — Split by function

Extract overland components into domain-specific files:

| New file | Content from overmap.cpp |
|----------|-------------------------|
| `overmap_pathfinding.cpp` | Pathfinding algorithms (`path_to()`, `cost_for_path()`, `get_connection_to()`, etc.) |
| `overmap_terrain.cpp` | Terrain generation (`generate()`, `place_terrain()`, `place_buildings()`, etc.) |
| `overmap_specials.cpp` | Special placement (`place_specials()`, `Spawn_rotation()`, `place_extra()`) |
| `overmap_connections.cpp` | Connection logic (`build_connections()`, `connect_road_t_types()`, etc.) — merge with partial `overmap_connection.cpp` |
| `overmap_labels.cpp` | Label functionality (`add_note()`, `delete_note()`, `note_string()`) — merge with partial `overmap_label.cpp` |

Target: `overmap.cpp` < 3000 lines after split.

### Phase 2 — Encapsulate data members

`overmap.h:434` has: `// TODO: make private`

Several `overmap` data members are public and accessed directly from `overmapbuffer.cpp`, `overmap_ui.cpp`, and Lua bindings:

```cpp
// Current (overmap.h):
std::vector<overmap_special_placement> overmap_special_placements;
std::vector<overmap_connection> connections;
std::vector<city> cities;
// etc.
```

Move to private with accessors:
```cpp
class overmap {
    std::vector<overmap_special_placement> overmap_special_placements_;
public:
    auto overmap_special_placements() const -> const std::vector<overmap_special_placement> &;
    auto add_special_placement( overmap_special_placement p ) -> void;
    // ...
};
```

Also encapsulate `overmap_special` and `overmap_tile` members where they are publicly accessible and mutable.

### Phase 3 — Complete JSON migration

Several TODOs reference incomplete JSON migration:
- `overmap.h:160`: "TODO: Needs to load from a JSON somewhere, move to json"
- `overmap.h:472`: "TODO: Should have individual instances grouped by placement"

Scope:
- Audit remaining hardcoded overmap specials/connections/locations against JSON equivalents
- Move any that have no JSON counterpart into JSON definitions
- Remove `from_legacy()` shim when all legacy formats are covered

### Phase 4 — C++23 modernization pass

- Options structs for pathfinding queries (currently pass 5+ raw params)
- `ranges::*` for overmap buffer searches and filtering
- Trailing return types
- `std::expected` for fallible operations (pathfinding failure, connection validation)
- Designated initializers for struct construction

## Verification (per phase)

- Build green. Overmap generation produces identical output for same seed.
- Phase 1: `wc -l src/overmap.cpp` drops.
- Phase 2: `rg "(overmap|overmap_tile|overmap_special)\.\w+ = " src/` returns only setter-style calls.
- Phase 3: `rg "TODO.*JSON" src/overmap*.cpp src/overmap*.h` returns 0.
- Phase 4: `rg "for\s*\(.*int\s+\w+\s*=\s*0" src/overmap*` drops.

## Files

| File | Phase |
|------|-------|
| `src/overmap.cpp` | 1 (source, shrinks), 4 (C++23) |
| `src/overmap.h` | 2 (encapsulation), 3 (JSON migration) |
| `src/overmap_pathfinding.cpp` (new) | 1 |
| `src/overmap_terrain.cpp` (new) | 1 |
| `src/overmap_specials.cpp` (new) | 1 |
| `src/overmap_connections.cpp` (new) | 1 |
| `src/overmap_labels.cpp` (new) | 1 (merge with overmap_label.cpp) |
| `src/overmapbuffer.cpp` | 2 (update to accessor API) |
| `src/overmap_ui.cpp` | 2 (update to accessor API) |
| `src/CMakeLists.txt` | 1 |

## Effort: 2–3 weeks
- Phase 1: 3–4 days (file split, mechanical)
- Phase 2: 2–3 days (encapsulation + call-site audit)
- Phase 3: 2–3 days (JSON migration)
- Phase 4: 1–2 days (C++23)
