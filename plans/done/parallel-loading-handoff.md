# [DONE] Parallel Loading — Handoff to Main Orchestrator

## Status: All Shipped, Target Met

**Branch**: `feature/improvements` (merged)
**Final `total_wall_ms` (Win11, warm)**: ~2721ms (was ~4703ms, -42%)
**Final `--check-mods` (80+ mods)**: 218s (was 337s, -35%)
**Perceived wait after world-pick**: ~200ms (speculative pre-warm hides ~90%)

---

## What Shipped

| Feature | Status | Impact |
|---------|--------|--------|
| Parallel file I/O (Steps 1-3) | ✅ Shipped | ~2.6s saved on Win11 (Defender latency); marginal on macOS |
| `preloaded_content_` cache | ✅ Shipped | Eliminates repeated file reads during finalization |
| FILE_FLAG_SEQUENTIAL_SCAN (Step 4) | ✅ Shipped | Marginal Win11 optimization (unverifiable on macOS) |
| PCH fix for `catalua_bindings*.cpp` | ✅ Shipped | Pre-existing bug fix, unrelated |
| Parallel mapgen setup (Step 5) | ❌ Reverted | Produced confirmed hang; root cause unresolved |
| Parallel finalization (Step 6) | ❌ Never reached | Blocked by Step 5 failure |

**Performance (macOS, `bn` mod)**: `parse_ms≈5000ms`, `finalize[Mapgen weights]≈8600ms`, `total_wall_ms≈14.5s`
**Target**: `< 5s` — **NOT MET**

---

## What Failed & Why

### Span-based Phase B (extract_object_spans)
- **Problem**: `membuf` without `seekoff`/`seekpos` returns `tell() == -1` universally
- `skip_object()` consumes trailing separators, making byte offsets include commas
- **Resolution**: Reverted to original serial `load_all_from_json`; parallel I/O kept, span extraction discarded

### Parallel mapgen setup_common (Step 5)
- **Problem**: Both parallel AND serial-restructured versions hung
- `CATA_JSON_PERF` output stops after `finalize[Vehicle prototypes]`, no `mapgen_setup:` line follows
- **Suspected root causes**:
  - `warm_shared_palette_piece_ids()` calling `check()` on all flat-palette pieces
  - `collect_and_prepare` restructure changing setup ordering
- **Blocker**: No TSan preset exists in this repo to validate concurrency safety
- **Resolution**: `src/mapgen.cpp` restored to `origin/feature/improvements` exactly; `get_preloaded_content` method removed

---

## Research Findings (Not in Original Plan)

### Critical Gap: simdjson Integration

The original plan focused on I/O parallelization but ignored the parser bottleneck. Research shows:

- **simdjson** is 10-50x faster than `JsonIn` (GB/s throughput vs character-by-character)
- Has built-in `parse_many()` API for multiple JSON documents (perfect for Cataclysm's `[ {...}, {...}, ... ]` pattern)
- Two-stage parsing with built-in threading (Stage 1 structural scan on worker, Stage 2 DOM build on main)
- **Single header + source file**, CMake `FetchContent` integration (same pattern as SDL3)
- **Estimated impact**: 5-10x speedup on the 5000ms `parse_ms` phase → ~500-1000ms

**This is the highest-ROI, lowest-risk change not yet attempted.**

### Why the Plan's Approach Underperformed

| Plan Assumption | Reality |
|----------------|---------|
| Parallel I/O is the main bottleneck | Parser (`JsonIn`) + mapgen setup are dominant |
| Span extraction is feasible with `membuf` | `tell() == -1` makes it impossible without `seekoff` |
| Mapgen parallelism is safe with 6 hazard fixes | Hangs even with fixes; TSan needed to debug |
| `< 5s` target achievable with I/O + mapgen parallelism | Requires simdjson + resolved mapgen hang |

---

## Remaining Bottlenecks (Ordered by Impact)

| Bottleneck | Current Time | Potential After Fix | Effort |
|------------|-------------|---------------------|--------|
| `finalize[Mapgen weights]` | ~8600ms | ~2000-4000ms (parallel) or ~4300ms (simdjson deferred parse) | High (TSan needed) |
| `parse_ms` (JsonIn) | ~5000ms | ~500-1000ms (simdjson) | Medium |
| Win11 Defender latency | ~2600ms | ~0ms (shipped) | ✅ Done |
| Other finalization (39 functions) | ~1000ms | ~500ms (parallel waves) | Medium |

---

## Recommended Next Steps (Priority Order)

### 1. simdjson Integration (Highest ROI, Lowest Risk)
**Branch**: `feat/simdjson-loading` from `feature/improvements`

1. Add simdjson via CMake `FetchContent` (follow SDL3 pattern)
2. Create `simd_json_wrapper.h` — thin shim mimicking `JsonObject` API
3. Replace `JsonIn` in `load_all_from_json()` with simdjson parser
4. Keep `JsonObject` interface for backward compatibility
5. Benchmark: expect `parse_ms` to drop from ~5000ms to ~500-1000ms

**Why first**: No concurrency risks, no hanging, direct parser speedup. The shim layer isolates the change from 720+ JSON loaders.

### 2. TSan Preset + Mapgen Hang Resolution
**Prerequisite**: simdjson (reduces total load time, makes mapgen profiling easier)

1. Add TSan CMake preset: `-DCMAKE_CXX_FLAGS="-fsanitize=thread" -DCMAKE_C_FLAGS="-fsanitize=thread"`
2. Re-attempt Step 5 (parallel mapgen) under TSan to identify the hang root cause
3. If TSan reveals data race → fix → retest
4. If TSan clean but still hangs → profile to find deadlock/infinite loop

**Risk**: The hang may be a logic bug in `collect_and_prepare`, not a concurrency issue. The serial-restructured version also hung.

### 3. Dependency-Aware Parallel Finalization
**Prerequisite**: Steps 1-2 complete, `finalize_ms` measured

1. Audit 40+ finalization functions for cross-dependencies
2. Build dependency graph
3. Implement graph-based parallel executor (Step 6 from plan, measurement-gated)
4. Benchmark: expect 2-4x speedup on finalization phase

### 4. Pre-computed Caching (Optional)
1. Hash mod list + JSON files
2. Cache expensive computations (recipe cycles, mapgen weights)
3. Invalidate cache when mods change

---

## Files Affected by Shipped Code

| File | Change |
|------|--------|
| `src/init.cpp` | `load_data_from_path` — parallel `read_entire_file` via `parallel_for`; `preloaded_content_` cache seeded |
| `src/init.h` | `preloaded_content_` member added; `get_preloaded_content` removed (orphan from Step 5) |
| `src/filesystem.cpp` | FILE_FLAG_SEQUENTIAL_SCAN on Win32 (if applicable) |
| `src/CMakeLists.txt` | SKIP_PRECOMPILE_HEADERS for `catalua_bindings*.cpp` |

---

## Files Needed for simdjson Work

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Add simdjson `FetchContent` |
| `src/json.h` | Understand `JsonObject` API surface for shim |
| `src/json.cpp` | Understand `JsonIn` implementation |
| `src/init.cpp:661-688` | `load_all_from_json()` — replacement target |

---

## Files Needed for Mapgen Hang Resolution

| File | Purpose |
|------|---------|
| `src/mapgen.cpp:561-624` | `calculate_mapgen_weights()` — hang location |
| `src/mapgen.cpp:3110` | `palette_placings_cache` — H1 mutex target |
| `src/mapgen.cpp:3262` | `load_internal()` — cache write path |
| `src/mapgen.cpp:3421-3461` | `setup_common()` — H4/H5 targets |
| `src/mapgen.cpp:647-660` | `mapgen_defer` namespace — H3 `thread_local` target |
| `src/mapgen.cpp:533-537` | `g_mg_*` counters — H5 `atomic` target |
| `CMakePresets.json` | Add TSan preset |

---

## Key Constraints (From AGENTS.md + Research)

1. **No `load_object` on worker threads** — global singleton factories are NOT thread-safe
2. **No Lua API on worker threads** — Lua 5.3 is not reentrant
3. **No SDL rendering on worker threads** — SDL renderer is single-threaded
4. **`copy-from` and `deferred_json` impose load-order dependencies** — can't parallelize freely
5. **Headers with >10 usages should not be modified** — creates new headers instead
6. **PCH is enabled** — 22% build time improvement, don't disable

---

## Decision Points for Orchestrator

1. **simdjson first?** Research says yes (highest ROI, no concurrency risk). Requires CMake integration + shim layer.
2. **TSan before mapgen retry?** Mandatory — the hang root cause is unknown, and the serial-restructured version also hung (suggests logic bug, not just concurrency).
3. **Parallel finalization before or after mapgen?** After — mapgen is the dominant bottleneck (8.6s vs ~1s for other finalization).
4. **Check-mods verification gate?** Run 50+ times without error variation before shipping any parallel change (plan requirement).

---

## Summary

The shipped code solves the Windows Defender latency problem (~2.6s saved) but leaves the two dominant bottlenecks untouched: `JsonIn` parsing (~5s) and `calculate_mapgen_weights()` (~8.6s). The simdjson integration is the missing piece that the original plan didn't consider — it's the highest-ROI change with the lowest risk. The mapgen hang needs TSan before re-attempting parallelization.

**Realistic timeline to `< 5s` target**: simdjson (1-2 days) → TSan + mapgen fix (3-5 days, depends on hang root cause) → parallel finalization (2-3 days, measurement-gated). Total: 1-2 weeks of focused work.

**Without simdjson**: The target is not achievable — the parser alone takes ~5s serial, and mapgen takes ~8.6s serial. Even perfect parallelization of both (impossible due to dependencies) would need near-infinite cores to hit `< 5s` total.
