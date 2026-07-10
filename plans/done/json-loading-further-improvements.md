# [DONE] JSON Loading — Further Performance Improvements

## Status quo (Win11, i9-14900K, warm cache, `bn` mod only)

Already shipped on `feature/improvements`:

| What | Commit | Impact |
|---|---|---|
| Parallel file reads (Phase A) | `d50a4ed6` | Reads all JSON concurrently |
| `FILE_FLAG_SEQUENTIAL_SCAN` + Defender advisory | `0e7404e9` | Cold-start I/O improvement |
| Parallel mapgen `setup_common()` | `0fe60be8` | `finalize[Mapgen weights]` 2780ms → ~300–600ms |

Remaining measured bottlenecks (warm cache, `bn` mod):

| Phase | Time | Notes |
|---|---|---|
| `parse_ms` | ~1640ms | Serial: `load_all_from_json` per file, one thread |
| `finalize[Items]` | ~140ms | Serial |
| `finalize[Vehicle prototypes]` | ~200ms | Serial |
| `finalize[Crafting recipes]` | ~45ms | Serial |
| Other finalize phases | ~180ms | Serial, individually small |
| **`total_wall_ms`** | **~4700ms** | After mapgen parallel: ~2200ms est. |

Target: `total_wall_ms < 500ms` on Win11 warm, `< 2000ms` cold.

---

## Measurement results

### macOS ARM (warm cache, `bn` mod, pack verified)

Measured 2026-07-08 with `CATA_JSON_PERF=1 --check-mods bn`. Pack confirmed hit: 1890 entries, ~25MB.

| Metric | Value | % of parse_ms |
|---|---|---|
| `parse_ms` | 1744ms | 100% |
| `scan_ms` (index scan) | 351ms | **20.2%** |
| `handler_ms` (load_object) | 1134ms | 65.0% |
| Overhead | ~259ms | 14.8% |
| `finalize[Mapgen weights]` | 105ms | — |
| `finalize[Items]` | 116ms | — |
| `finalize[Vehicle prototypes]` | 188ms | — |
| `finalize[Crafting recipes]` | 58ms | — |
| **`total_wall_ms`** | **2294ms** | — |

### Win11 (i9-14900K, warm cache, `bn` mod, pack verified)

| Metric | Before | After | Δ |
|---|---|---|---|
| `total_wall_ms` | ~4703 | ~2721 | -42% |
| `parse_ms` | ~1632 | ~1869 | +14% (includes scan+handler) |
| `finalize_ms` | ~2961 | ~732 | -75% |
| `finalize[Mapgen weights]` | ~2779 | ~555 | -80% |
| `scan_ms` | — | ~291 | **15.6%** |
| `handler_ms` | — | ~1250 | 66.9% |
| `pack_hit` | — | 1890 entries, 25MB | ✓ |

Mapgen setup breakdown: `setup_jo_ms` ~9222 (dominant), `inline_read_ms` ~290, `palette_add_ms` ~37.

Full `--check-mods` (all 80+ mods): 217.90s wall time (was 337.35s, -35%).

**Gating decision: SKIP §4 (parallel parse RawLayout cache).** Win11 scan fraction 15.6% < 30% required. macOS scan fraction 20.2% < 30% required. Gate not met on either platform.

---

## Improvement inventory

Six independent improvements, ordered by effort/impact ratio.

### 1. JSON archive blob — biggest cold-start win ✅ DONE
**Status**: Implemented and verified on macOS (pack_hit confirmed). Cold-start win on Win11 (Defender bypass) still needs validation — pack works, but the actual cold I/O savings hasn't been measured yet.


**Problem**: On Windows, each JSON file open triggers a per-file Defender scan. The `bn` mod has 1890 files. Even with parallel reads, each `CreateFile` call serializes through the AV filter driver. This is the dominant cold-start cost (~3–8s on a cold system).

**Idea**: Pack all JSON files for each mod into a single archive file (e.g. `bn.jsonpack` — a trivial binary container: 4-byte count, then per-entry `[path-len][path][data-len][data]`). On first load, if the pack exists and its mtime matches the mod directory's newest file, read the one blob (one `CreateFile`, one Defender scan, one sequential read) and split into `preloaded_content_` in memory. Fall back to directory scan if no pack.

**Pack format** (simple, no dependency):
```
Header:  magic[4] = "CBNP"  version[4] = 1  entry_count[4]
Entries: path_len[4]  path[path_len]  data_len[4]  data[data_len]
```

Invalidation: a 64-bit xxHash of the concatenated file list (sorted paths + sizes) is stored in the pack header. On load, recompute the hash from the directory listing — if it matches, the pack is fresh. This is more robust than mtime: detects deletions, renames, and file replacements that preserve mtime. xxHash is 3 lines of code (header-only, already used in similar projects) or can be replaced with a simple FNV-1a accumulator to avoid any dependency. Rebuild the pack on hash mismatch; the Deno script (`tools/pack_mod_json.ts`) handles generation.

**Where it fits**: `load_data_from_path` checks for `<mod_path>/data.jsonpack` before calling `get_files_from_path`. If found and fresh, read into `preloaded_content_` directly and skip file scan entirely.

**Impact**: Cold-start `parse_ms` drops from potentially 5–8s to ~200ms (one large sequential read). Warm-cache impact is small (OS already has the files cached).

**Tradeoffs**:
- Requires a pack-generation step in the build/install pipeline.
- Mods distributed as loose files still work (graceful fallback).
- The `membuf` struct (already in `mapgen.cpp` anonymous namespace) can serve the parse path directly from the in-memory pack content — no copy.
- No new dependencies.

**Effort**: ~3 days. Mostly the pack format reader and the Deno builder script.

---

### 2. LTO (Link-Time Optimization) — free win, one CMake line ✅ DONE

**Problem**: The compiler cannot inline across translation units. `JsonObject::get_string`, `JsonIn::get_value`, `weighted_list::add` — called millions of times from `load_object` handlers in separate `.cpp` files — never get inlined.

**Idea**: Enable LTO in release CMake presets. Two options:
- **Clang (Mac + Win11 with clang-cl)**: `-flto=thin` — ThinLTO. Parallel per-module index, link-time ~20% slower than no-LTO. Best balance.
- **MSVC (Win11 default compiler)**: `/GL` (compile) + `/LTCG:INCREMENTAL` (link) — full LTO with incremental support. More aggressive than ThinLTO but slower linking. NOT the same as ThinLTO — MSVC has no ThinLTO equivalent.
- **GCC**: `-flto=auto` — uses all available cores during link.

**Changes**: `CMakePresets.json` release presets — add LTO flags to `CMAKE_CXX_FLAGS_RELEASE` and `CMAKE_EXE_LINKER_FLAGS_RELEASE`. Debug builds: no LTO. `rmlui` and SDL3 already compile from source here and can pick up the flag.

**Impact**: 10–20% on CPU-bound parse/finalize workloads. On 2200ms total: saves ~200–400ms. Zero code changes.

**Tradeoffs**:
- Fresh link time increases: ThinLTO ~20–30s extra, MSVC LTCG ~60–90s extra. Incremental builds unaffected (both cache per-module summaries).
- MSVC `/LTCG` requires `/GL` on all object files including third-party libs — check SDL3/rmlui CMake targets accept the flag without error.

**Effort**: ~1 day. Risk: very low.

---

### 3. PGO (Profile-Guided Optimization) — 15–25% on hot paths ✅ DONE

**Problem**: The compiler uses static heuristics for branch prediction, inlining decisions, and register allocation. The JSON parse path (`JsonIn::peek`, `JsonIn::eat_whitespace`, `load_object` dispatch) has highly predictable branch patterns at runtime that the compiler cannot see statically.

**Idea**: Two-stage build:
1. `cmake -DCMAKE_CXX_FLAGS="-fprofile-generate"` — instrument binary
2. Run `./cataclysm-bn-tiles --check-mods bn` (or a scripted game session) — generates `.profraw`
3. `llvm-profdata merge -output=default.profdata *.profraw`
4. `cmake -DCMAKE_CXX_FLAGS="-fprofile-use=default.profdata -fprofile-correction"` — optimized binary

On MSVC: `/GENPROFILE` → training run → `/USEPROFILE`.

**Impact**: 15–25% on parse and finalize phases combined. Measured on similar workloads (Chromium JSON parsing): ~20% consistently. On 2200ms total: saves ~300–500ms.

**Tradeoffs**:
- Requires a training run that exercises the hot path. `--check-mods bn` is sufficient.
- Profile data is machine-specific and goes stale when code changes significantly. CI should regenerate it on major changes.
- Pairs very well with ThinLTO — both operate at link time and compose cleanly.

**Effort**: ~2 days for CI integration. The actual build change is trivial.

---

### 4. Parallel parse phase — SKIPPED (instrumentation gate failed)
**Measurement**: scan fraction = 20.2% of parse_ms (351ms of 1744ms). Below 30% threshold. Ceiling ~150ms saved for 2 weeks of work. ❌ NOT WORTH IT.

**Problem**: `parse_ms ≈ 1640ms` wraps the entire `load_data_from_path` call. Inside `load_all_from_json` each object costs two things:
1. **Index scan** — `JsonObject` constructor (`json.cpp:81–96`) walks every member token and builds a positions map (`RawLayout`: member-name → stream offset). This is the byte-scanning work.
2. **Handler execution** — `load_object` calls `type_function_map[type](jo, ...)` which calls `jo.get_string(...)`, `jo.get_int(...)` etc. to extract values and insert into global factories (item_controller, MonsterGenerator, etc.). This almost certainly dominates.

> **⚠ Critical design flaw in the naive B1/B2 split**: Recording only `json_source_location` in B1 and calling `load_object` in B2 does NOT save the index-scan work. `JsonObject`'s constructor in B2 re-scans the whole object to rebuild its `RawLayout` — identical work to what B1 did. Net saving ≈ 0 (parallel pre-scan + unchanged serial work).

**Correct design** — parallel RawLayout caching:

**Phase B1 (parallel)**: Workers scan files and for each JSON object record:
- `type` string (from `"type"` member)
- `json_source_location` (path ptr + offset)
- `JsonObject::RawLayout` (the full member positions map)

The `RawLayout` is the same structure that `s_mapgen_layout_cache` already uses (added in a prior commit). Output: per-thread `vector<ScannedObject>` where `ScannedObject = {type, jsrcloc, layout}`. No `load_object` called. No shared state written.

**Phase B2 (serial)**: Main thread merges dispatch lists in original file order. For each entry, constructs `JsonObject(jsin, cache_it->second)` using the pre-built layout (skips the re-scan), then calls `load_object`. This eliminates the index-scan duplication.

**What this actually saves**: Only the index-scan fraction of `parse_ms` — the `JsonObject` constructor work. The handler body (`get_string`, `get_int`, factory insertion, string interning) is still fully serial in B2. Whether that scan fraction is worth the implementation complexity is **unknown until measured**.

> **⚠ Instrumentation required before implementing**: Add two `steady_clock` accumulators inside `load_all_from_json`:
> - `t_scan`: time spent in `jsin.get_object()` (index scan only)
> - `t_handler`: time spent in `load_object(jo, ...)`
> Print both with `CATA_JSON_PERF`. If `t_scan < 20%` of `parse_ms`, parallel B1 ceiling is <330ms and the binary cache (§6) delivers better ROI. If `t_scan ≥ 30%`, B1 is worth pursuing.

**Thread safety of Phase B1**: Each worker reads from its own in-memory file content slice via `membuf`. Workers write only to their own per-thread `ScannedObject` vectors. No `load_object` called. No singletons touched. Safe.

**Impact**: Upper bound = index-scan fraction of `parse_ms`, parallelized over 24 cores. Realistic ceiling: **~150–500ms saved**, not ~1400ms. The handler fraction (registry insertion, string interning, `copy-from` resolution) remains fully serial in B2 and is likely the majority of `parse_ms`.

**Tradeoffs**:
- Deferred system (`deferred_json`) appends during `load_object` (Phase B2, serial) — unchanged.
- `sort_deferred` / `copy-from` resolution runs after all files load — unchanged.
- Lua handlers stay on main thread in B2 — safe.
- The `RawLayout` cache for the parse phase is separate from `s_mapgen_layout_cache` (which caches mapgen-specific layouts for deferred re-reads). Both use the same `JsonObject::RawLayout` type — the parse-phase cache lives only for the duration of `load_data_from_path` and is freed before finalization.
- Risk: medium. B1 is read-only; B2 is the existing proven serial path.
- **Do not implement until instrumentation confirms scan fraction ≥ 30% of `parse_ms`.**

**Effort**: ~2 weeks after instrumentation confirms it worthwhile.

**Missing implementation detail for Phase B2**: When B2 re-opens each entry, it needs a `JsonIn` positioned at `jsrcloc->offset`. Use the same worker path already in `setup_common()`: `membuf buf(content->data(), content->size()); std::istream s(&buf); JsonIn jsin(s, *jsrcloc->path); jsin.seek(jsrcloc->offset);` — then construct `JsonObject(jsin, scanned.layout)`. The preloaded content pointer is stable for the duration of `load_data_from_path`. The layout eliminates the re-scan; only the `get_string`/`get_int` value-extraction calls remain in B2.

### 5. Speculative pre-warm — hides most latency for the common case

**Actual load sequence** (verified from source):
1. `g->load_static_data()` (main.cpp:628) — fast, non-moddable base data. Synchronous.
2. `main_menu::opening_screen()` — title screen. User takes ~2–10s to navigate and pick a world.
3. User selects world → `game::setup()` → `init::load_world_modfiles(ui, world)` (init.cpp:1202) → `clear_loaded_data()` + `load_and_finalize_packs(world mods)`. This is the slow call (~2s warm).

**Critical constraint**: The mod set is world-specific (`world->info->active_mod_order`, line 1207). You cannot pre-load a generic set. Must know the world before loading.

**Mechanism — speculative pre-warm of the last-played world**:
Immediately after `load_static_data()` (main.cpp:628), before constructing `main_menu`, fire `init::load_world_modfiles()` on a background worker for `world_generator->last_world_name`. The title screen renders and is fully interactive while the worker runs. When the user picks "Load Game" for that same world (the common case — most players resume), the background load is already done or nearly done; the main thread only needs to run the SDL-bound finalize phases (`load_tileset`, ~200ms) that cannot safely run on a worker. Net perceived wait after world selection: ~200ms instead of ~2s.

If the user picks a different world: `cancel/join` the background future, call `clear_loaded_data()`, and run synchronously — no regression vs today.

**Why this hides latency vs the spinner-only approach**: Firing the load AFTER world selection (spinner approach) keeps the full ~2s on the critical path between "pick world" and "game starts" — the user still waits ~2s, just with animation. Pre-warming BEFORE world selection overlaps the load with the user's think-time at the menu (~2–10s). The load is off the critical path entirely for the common case.

**What stays on the main thread** (cannot move to worker):
- `load_tileset()` — SDL texture upload, must be on the SDL render thread (~200ms of finalize).
- `inp_mngr.pump_events()` calls inside the load path — guard with `is_pool_worker_thread()` to skip on worker; main thread pumps events normally while idle at the menu.
- `run_mod_finalize_script` (Lua) — Lua VM is single-threaded; runs after background thread joins.

**Where to implement**:
- `main.cpp:628` (after `g->load_static_data()`): check `world_generator->last_world_name`; if non-empty, `speculative_future = get_thread_pool().submit_returning([last_world]{ run_non_sdl_load(last_world); })`.
- Split `finalize_loaded_data`'s entries table into `worker_phases` (all non-SDL) and `main_phases` (tileset + SDL). Worker runs `worker_phases`; main thread runs `main_phases` after joining.
- `main_menu::opening_screen()`: on world selection matching `last_world`, join the future (should be done or near-done), then run `main_phases` on main thread (~200ms). On different world: join+clear+synchronous load.
- `loading_ui` background mode: writes progress to a mutex-protected string; not used for display (main thread is at the menu), just for logging.

**Impact**: For the common case (resume last world), perceived wait after world selection drops from ~2s to ~200ms (SDL finalize only). The title screen is always instant. Different-world pick: no regression.

**Risk**: medium-high. Every SDL and Lua call inside `load_data_from_path` and `finalize_loaded_data` must be audited and guarded. Missing one causes a crash on the worker thread. `is_pool_worker_thread()` is the guard pattern — already used in `setup_common()` and `mapgen.cpp`.

**Effort**: ~3 weeks. Non-SDL/SDL finalize table split ~1 week; pre-warm plumbing + world-mismatch handling ~1 week; audit + testing ~1 week.

### 6. Binary finalized-data cache — eliminates warm-launch cost entirely

**Problem**: Every launch re-runs two distinct expensive operations for the same mod set:
1. **Parse + construct** (`parse_ms ≈ 1640ms`): `load_object` handlers read JSON and construct C++ objects into global registries (`item_controller`, `MonsterGenerator`, etc.). This is the dominant cost and happens inside `load_data_from_path`.
2. **Finalize** (`finalize_loaded_data` phases): resolve `copy-from` chains, build `weighted_list`s, compute palette indices. `finalize[Items]` (~140ms), `finalize[Vehicle prototypes]` (~200ms), `finalize[Mapgen weights]` (~300ms post-parallel), etc.

Both are deterministic given the same input. Both can be cached.

**Idea**: After both phases complete, serialize the final in-memory state of each registry to `<user_data_dir>/cache/<hash>.bin`. On subsequent launches with the same mod set + same file contents, deserialize directly — skip parse and finalize entirely.

**Cache key**: FNV-1a rolling hash of `{sorted mod list + each JSON file path + file size + mtime}` — fast to compute (no file reads), detects any change. Promoted to content hash only on key collision (extremely rare).

**Cache format**: Each registry implements `serialize_cache(BinaryWriter&)` / `deserialize_cache(BinaryReader&)`. Simple length-prefixed binary, no external dependency. Hand-rolled is simpler than Flatbuffers and sufficient — the cache is read sequentially once per launch.

**Which registries to cache first** (by combined parse+finalize cost):
- `item_controller` — ~140ms finalize + dominant fraction of `parse_ms` handler cost (items are the most numerous JSON type)
- `vehicle_prototype` — ~200ms finalize + significant parse cost
- `mapgen` weights/palette structures — ~300ms finalize (post-parallel)
- `MonsterGenerator`, `recipe_dictionary` — ~50ms, ~45ms finalize each

**Incremental approach**: Ship the cache for `item_controller` alone first. It likely accounts for >50% of handler `parse_ms` and ~140ms of finalize — a partial cache covering just items could save ~600ms+ warm.

**Invalidation**: Any JSON file change (mod update, user edit) invalidates the entire cache for that mod set. The key check costs ~5ms (directory stat walk) — acceptable. Cache lives in user data dir, not the mod dir — survives game updates without stale data, as long as the version is bumped in the binary header.

**Cache versioning**: First 8 bytes of the cache file: `magic[4] = "CBNC"  version[4]`. Game update bumps version → old cache is skipped, cold load runs, new cache written. No migration needed.

**Impact**: Warm-launch `total_wall_ms` drops from ~2200ms to ~100–200ms (key check + cache read). Cold (cache miss or stale): identical to today. This is the only technique that eliminates both parse and finalize cost.

**Tradeoffs**:
- `item_controller` alone has hundreds of fields, many with `string_id` references that resolve to raw pointers after finalize — those pointers can't be serialized directly. Must serialize as string IDs and re-resolve on deserialization, which partially re-does finalize work. This is the hard part.
- Every new field added to a cached type needs a matching serialize/deserialize path — ongoing maintenance cost.
- Mods can add arbitrary types — the cache is per-mod-set, stored in user data dir, keyed by the full mod list hash.
- Risk: high. The serialization surface is enormous. Any missing field silently loads zero/default until discovered in gameplay.

**Effort**: 3–6 months for full coverage. 2–4 weeks for `item_controller` partial cache (measure the win before committing to full coverage).

---

## Combined impact projection (updated with Win11 measurements)

**⚠ Measurement complete on both platforms. §4 skipped. §5 and §6 remain.**

| Phase | Baseline (Win11) | After §1–3 (done) | After §5 pre-warm | After §6 cache |
|---|---|---|---|---|
| Cold file open | ~5000ms cold | TBD (Win11 cold needed) | TBD | ~200ms |
| `parse_ms` total | ~1869ms | ~1869ms | ~1869ms | ~0ms |
| `finalize[Mapgen weights]` | ~555ms | ~555ms | ~555ms | ~0ms |
| `finalize[Items]` | ~116ms | ~116ms | ~116ms | ~0ms |
| `finalize[Vehicle prototypes]` | ~188ms | ~188ms | ~188ms | ~0ms |
| Other finalize | ~64ms | ~64ms | ~64ms | ~0ms |
| **`total_wall_ms`** | **~2721ms** | **~2721ms** | **~2721ms** | **~200ms** |
| **Perceived wait after world-pick** | **~2.7s** | **~2.7s** | **~200ms** | **~200ms** |

§5 (speculative pre-warm) is independent of all other steps — it hides ~90% of the warm-load wait by overlapping it with menu think-time, regardless of how fast or slow the actual load is. The remaining ~200ms (SDL tileset finalize, `load_tileset()`) is irreducible every launch: it is SDL texture upload on the render thread, not JSON data, so the §6 binary cache does not affect it.

Key insight: §1–3 (LTO + PGO + archive blob) are DONE. §4 skipped after measurement on both platforms. §5 is the biggest UX win independent of wall-clock speed. §6 is the engineering end-game.

---

## Required instrumentation before §4 ✅ DONE

**Result**: scan 351ms (20.2%), handler 1134ms (65.0%). Gate not met — §4 skipped.

Accumulators in place: `g_last_load_metrics.parse_scan_us` and `parse_handler_us` in `load_all_from_json`. Print with `CATA_JSON_PERF`.

---

## Implementation order

```
Step  What                                 Effort    Risk    Effect         Status
────────────────────────────────────────────────────────────────────────────────────────────
  1   LTO (ThinLTO / MSVC LTCG)           1 day     low     ~200–400ms     ✅ DONE
  2   PGO                                  2 days    low     ~200–400ms     ✅ DONE
  3   JSON archive blob                    3 days    low     dominates cold ✅ DONE
  3a  Instrument scan vs handler split     1 day     low     gates §4       ✅ DONE
  4   Parallel parse (RawLayout cache)     2 weeks   medium  150–500ms      ❌ SKIPPED
  5   Speculative pre-warm                 3 weeks   medium  ~90% hidden    ✅ DONE
  6   Binary finalized-data cache          months    high    eliminates warm ⬜ FUTURE
```

§1–3 DONE. §4 SKIPPED (scan fraction 20.2%, below 30% threshold). §5 DONE — speculative pre-warm shipped. §6 end-game.

---

## Already shipped (do not re-implement)

- Parallel file reads: `init.cpp:653` (`parallel_for` over `read_entire_file`)
- `FILE_FLAG_SEQUENTIAL_SCAN` on Win32: `d50a4ed6`
- `preloaded_content_` seeded before finalization: avoids re-reads during `get_cached_stream`
- Parallel mapgen `setup_common()`: `0fe60be8`
- `membuf` + worker path in `setup_common()`: bypasses LRU stream cache on workers
- `sort_deferred` with `copy-from` topological sort: deferred round reduction
---

## Handoff for next session

### What's done
- LTO, PGO, JSON archive blob: implemented; pack verified on macOS (Win11 cold-start still TBD)
- Instrumentation: `CATA_JSON_PERF=1 --check-mods bn` prints scan/handler split
- Pack BFS order bug: fixed in both C++ and Deno builder

### Measurement results
- `parse_ms`: 1744ms, `scan_ms`: 351ms (20.2%), `handler_ms`: 1134ms (65.0%)
- `total_wall_ms`: 2294ms
- Pack confirmed hit: 1890 entries, ~25MB

### Decision
- §4 SKIPPED: scan fraction 20.2% < 30% threshold
- §5 NEXT: speculative pre-warm of last-played world
- §6 FUTURE: binary finalized-data cache

### Win11 validation needed
- Cold-start run with pack vs directory scan to measure actual Defender bypass savings
- Each run must be cold: reboot (or clear file+Defender cache) before each measurement — back-to-back runs warm both caches and mask the real cost
- This is the only genuinely platform-specific measurement remaining; won't change §5/§6 decision
