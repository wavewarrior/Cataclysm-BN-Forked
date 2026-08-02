# Map Handling & Performance — Research + Optimization Roadmap

## STATUS (reviewed 2026-06-27)
Tier 0a (walking-hitch fix) DONE + committed `5315065c12`. Tier 1a (per-submap incremental
lightmap) DONE — Phase A + B1/B2/B3 all verified in code (lightmap_dirty bitset, per-submap
skip, solar-level tracking). Tiers 0b/0c (residual spike pins) = still diagnosis-only, not
started. Tier 1b (amortise non-player-z), Tier 2a (parallelise structural across z), Tier 3a
(bubble curve), Tier 3b (lazy alloc) = NOT started. NOTE: structural phases 1a/1b/1c already
use intra-z parallel_for and Phase 1d uses parallel_for-over-z gated on PARALLEL_MAP_CACHE —
the status table below understates existing parallelism. Roadmap still the right index doc; keep.

## Context

Goal: broad, thorough understanding of how the map is handled and where its
performance goes — architecturally, for chunk loading, and for lighting — plus
how the same problems are solved in industry / comparable games. This roadmap is
the reference doc; individual plans under `plans/` implement each tier.

## Pain axes

1. **Walking-hitch residual spikes** — ~2/10 shifts still spike to 20–23ms
2. **Steady-state FPS** — some non-shift turns show lightmap=8ms all-z
3. **Big-base/driving/dense-scene worst case** — entity counts, vehicles, SDF
4. **Memory** — bubble size quadratic scaling

---

## 1. Architecture (three-tier spatial model)

| Scale | Size | Role | Code |
|-------|------|------|------|
| map square (tile) | 1×1 | what you walk on | `tripoint_bub_ms` |
| submap | 12×12 (`SEEX`×`SEEY`) | unit of load/save/shift | `submap.h:91`, `maptile_soa` |
| overmap terrain (OMT) | 2×2 submaps | unit of mapgen | overmap |
| overmap | 180×180 OMT | unit of large-scale gen (cities) | `overmap.*` |

### Reality bubble — dynamic, runtime-sized

- `REALITY_BUBBLE_SIZE` option, 1–16, default 4 (`game_constants.h:130`).
- `init_bubble_config(size)` (`game.cpp:246`): `g_half_mapsize = size+1`,
  `g_mapsize = 2*half+1`. Default 4 → `g_mapsize = 11` → 132×132 tiles —
  identical to upstream CDDA.
- Compile-time `MAPSIZE = 2*REALITY_BUBBLE_SIZE_MAX+3 = 35` (`game_constants.h:34`)
  is only the allocation ceiling.

### Z-levels — all loaded simultaneously

- `OVERMAP_DEPTH=10`, `OVERMAP_HEIGHT=10`, `OVERMAP_LAYERS=21` (`game_constants.h:64-68`).
- Per-z `level_cache` (`map.h:308-436`), `std::array<unique_ptr<level_cache>,21> caches`.
- Cross-z coupling is structural only: `outside_cache[z]` reads `floor[z+1]`;
  light does NOT propagate between z lightmaps directly.

### `build_map_cache` — the cost centre

Phases (verified at `map.cpp:9848-10117`):

| Phase | Lines | Z-scope | Notes |
|-------|-------|---------|-------|
| 1a floor | 9888 | all-z | `build_floor_cache(z)` |
| 1b outside/sheltered | 9908 | all-z, top-down | reads floor[z+1] |
| 1c transparency | 9916 | all-z | reads outside |
| 1d parallel-caches | 9928 | all-z | vehicle clears + dirty levels |
| 2 suspension | 9975 | all-z | serial (support_cache_dirty) |
| 3 vehicles | 9985 | all-z | serial (neighbour z writes) |
| seen | 10004 | player-z | shadowcast FOV |
| 4 lightmap | 10011 | dirty z only | sunlight + entity lights |

### In-tree probes (ready to run)

- `[build_cache][perf]` phase+z-split (`map.cpp:10111`)
- `[shift-probe][outside|lightmap|invalidate|invalidate-bt]` (`map.cpp:9528/10039/10796/10803`)
- `[render][perf]` 10-phase breakdown (`sdl_render_frame.cpp:~869`)
- Tracy `ZoneScopedN` zones on render+sim paths

---

## 2. Ranked optimization roadmap

### Tier 0 — finish what's in flight (do first, near-zero risk)

| Item | Plan | Status |
|------|------|--------|
| 0a. Commit walking-hitch fix | `walking_hitch_cache_shift_plan.md` | ✅ DONE — committed `5315065c12` |
| 0b. Pin residual all-z structural spike | `residual_all_z_structural_spike_plan.md` | ✅ DIAGNOSED 2026-08-02 — premise falsified. Not an all-z structural rebuild and `invalidate_map_cache` never fires; the 2/17 >16ms spike is **synchronous mapgen inside `map::shift`** (`map::loadn` "Missing mapbuffer data. Regenerating."). Fix = extend async loader lookahead to generate, not new architecture. Deliberately unfixed: ~1 dropped frame, high worldgen-regression risk. |
| 0c. Pin non-shift all-z lightmap | `non_shift_all_z_lightmap_plan.md` | ❌ ARCHIVED — superseded by 1a (plan moved to `plans/done/`) |

### Tier 1 — structural, high payoff, moderate effort

| Item | Plan | Status |
|------|------|--------|
| 1a. Per-submap incremental lightmap | `per_submap_incremental_lightmap_plan.md` | ✅ DONE — Phase A + B1/B2/B3 in code |
| 1b. Amortise non-player-z structural rebuild | `amortise_non_player_z_rebuild_plan.md` | Not started |

### Tier 2 — parallelism (do AFTER 1a/1b)

| Item | Plan | Status |
|------|------|--------|
| 2a. Parallelise build_map_cache across z | `parallelize_build_map_cache_plan.md` | Not started |
| 2b. Finish GI/SDF GPU-compute migration | `GI_COMPUTE_AND_PERF_PLAN.md` | In flight |

### Tier 3 — memory & bubble

| Item | Plan | Status |
|------|------|--------|
| 3a. Measure bubble cost curve | `bubble_cost_curve_plan.md` | Not started |
| 3b. Lazy non-visible-z cache allocation | `lazy_non_visible_z_cache_plan.md` | Not started |

---

## 3. Related plans in repo

- `plans/SIM_PERFORMANCE_PLAN.md` (done) — Monster AI, NPC LOD, vehicle throttling, active-item striding
- `plans/LIGHTING_OPTIMIZATION_PLAN.md` — GPU lighting crash fix (P0-P6)
- `plans/done/LIGHTING_PERF_RESEARCH.md` — SDF rebuild gate research
