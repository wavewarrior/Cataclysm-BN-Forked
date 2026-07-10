# [DONE] Parallel Game Loading Plan

> **Implementer**: copy this file to `plans/parallel-loading-plan.md` in the repo before making any code changes (AGENTS.md requirement: plan lives in two places).

---

## Implementation outcome (2026-07-07)

**What actually shipped** (branch `feat/parallel-loading`, 3 commits on `feature/improvements`):
- **Steps 1–3 (parallel Phase-A reads + preloaded_content_ cache seed)**: `load_data_from_path` reads all `.json` files in parallel via `parallel_for` + `read_entire_file`. Phase B dispatches through the original unmodified `load_all_from_json` — the span-based Phase B described below was implemented and then replaced because `extract_object_spans` had irresolvable span-boundary fragility (`skip_object` consumes trailing separators, making `tell()` positions include commas; `membuf` without `seekoff/seekpos` returned `tell()=-1` universally). The robust fix was to keep parallel I/O and discard the span reparse entirely.
- **Step 4 (Win32 FILE_FLAG_SEQUENTIAL_SCAN + Defender advisory)**: shipped as planned; compile-unverifiable on macOS.
- **CMakeLists SKIP_PRECOMPILE_HEADERS** for `catalua_bindings*.cpp`: pre-existing PCH/-O0 conflict on `feature/improvements`, fixed as a separate commit.

**What was dropped — Step 5 (parallel mapgen setup_common)**:
Implemented fully (H1–H6, warm_shared_palette_piece_ids, collect_and_prepare, parallel_for over setup_fns) and reverted. Both the parallel and the serial-restructured versions of `calculate_mapgen_weights()` produced a hang confirmed by CATA_JSON_PERF output stopping after `finalize[Vehicle prototypes]` with no subsequent `mapgen_setup:` line. Root cause traced to `warm_shared_palette_piece_ids()` calling `check()` on all flat-palette pieces, and/or the collect_and_prepare restructure changing setup ordering. No TSan preset exists in this repo to validate concurrency safety. `src/mapgen.cpp` was restored to `origin/feature/improvements` exactly. The orphaned `get_preloaded_content` method (added for the Step-5 H4 worker bypass) was removed.

**Performance result (macOS, CATA_JSON_PERF, `bn` mod)**: `parse_ms≈5000ms`, `finalize[Mapgen weights]≈8600ms serial unchanged`, `total_wall_ms≈14.5s`. The "load in seconds" goal is NOT met. Parallel file reads address Win11 per-file-open/Defender latency (~2.6s on Win11, marginal on Mac); the dominant 8.6s serial mapgen setup is the key follow-up requiring TSan + profiling.

The plan below documents the intended architecture as designed; the sections on Step 5 and the span-based Phase B were NOT shipped.

---

## Context

Game startup takes unacceptably long — especially on Windows 11 where it is 3–5× slower than macOS M1. Two root causes: (1) `DynamicDataLoader::load_data_from_path` reads ~800–1100 JSON files serially — on Windows every `CreateFile` triggers a Windows Defender real-time scan (~0.5–2 ms each); (2) `calculate_mapgen_weights()` runs ~740+ mapgen `setup_common()` calls serially in a single thread, estimated at 200–800 µs per entry. The codebase already has `cata_thread_pool`, `parallel_for`, and `parallel_for_chunked` (`src/thread_pool.h`), and the pool is initialized in `game::game()` (line 418) before any JSON loading. The goal is sub-5-second load time on Win11 by parallelizing file I/O and mapgen setup, with parallel finalization as a deferred, measurement-gated step.

---

## Approach

### Architectural invariant (enforced throughout)

Every `load_object` handler writes to global singleton factories (`item_controller`, `MonsterGenerator::generator()`, etc.) — NOT thread-safe. `copy-from` and `deferred_json` impose load-order dependencies. **No `load_object` call, no `type_function_map` dispatch, and no `finalize_*` call may ever run on a worker thread.** Workers may only: read file bytes from disk, and scan JSON text for object byte boundaries. Everything else is main-thread only.

---

### Step 0 — Create feature branch from `improvements`

All work in this plan lands on a single branch created from `improvements`. Run before any code changes:

```sh
git fetch origin
git checkout -b feat/parallel-loading origin/improvements
```

If the repo uses `git gtr` (coderabbitai/git-worktree-runner), use:

```sh
git checkout improvements && git pull origin improvements
git gtr new feat/parallel-loading
```

Commit each step atomically following Conventional Commits. Suggested commit sequence: `perf: add membuf and preloaded_content_ cache` → `perf: split-phase parallel JSON load` → `perf: FILE_FLAG_SEQUENTIAL_SCAN on Win32` → `perf: parallel mapgen setup_common`.

---

### Step 1 — Add zero-copy stream buffer and file-content cache

**Files**: `src/init.cpp`, `src/init.h`

Add `membuf` to `init.cpp` anonymous namespace — wraps a `const char*` buffer as a read-only `std::streambuf`, avoiding string copies when constructing `JsonIn` from pre-loaded content:

```cpp
namespace {
struct membuf : std::streambuf {
    membuf( const char *base, std::size_t size ) {
        auto *p = const_cast<char *>( base );
        setg( p, p, p + size );
    }
};
} // namespace
```

Add to `DynamicDataLoader` private section in `init.h`, after the `stream_cache` declaration (line 77):

```cpp
// Pre-loaded file content keyed by absolute path.
// Populated during load_data_from_path; cleared at end of finalize_loaded_data.
// Used by get_cached_stream (deferred finalization) and get_preloaded_content
// (parallel mapgen setup) to eliminate repeated Defender-scanned file opens.
std::unordered_map<std::string, std::string> preloaded_content_;
```

Add `#include <unordered_map>` to `init.h`.

Add a new public method declaration in `init.h`:

```cpp
// Thread-safe: preloaded_content_ is read-only during finalization.
// Returns nullptr if the file was not pre-loaded.
const std::string *get_preloaded_content( const std::string &path ) const noexcept;
```

Implement in `init.cpp`:

```cpp
const std::string *DynamicDataLoader::get_preloaded_content(
    const std::string &path ) const noexcept
{
    auto it = preloaded_content_.find( path );
    return ( it != preloaded_content_.end() ) ? &it->second : nullptr;
}
```

Update `get_cached_stream` in `init.cpp` (lines 188–203) to use `preloaded_content_` before `read_entire_file`:

```cpp
shared_ptr_fast<std::istream> DynamicDataLoader::get_cached_stream( const std::string &path )
{
    assert( !finalized && "Cannot open data file after finalization." );
    assert( stream_cache && "Stream cache is only available during finalization" );
    shared_ptr_fast<std::istringstream> cached = stream_cache->cache.get( path, nullptr );
    if( !cached ) {
        auto it = preloaded_content_.find( path );
        const std::string content = ( it != preloaded_content_.end() )
                                    ? it->second
                                    : read_entire_file( path );
        cached = make_shared_fast<std::istringstream>( content );
    } else if( cached.use_count() > 2 ) {
        cached = make_shared_fast<std::istringstream>( cached->str() );
    }
    stream_cache->cache.insert( stream_cache_limit, path, cached );
    return cached;
}
```

Update the `on_out_of_scope` in `finalize_loaded_data` (line 804) to also clear `preloaded_content_`:

```cpp
on_out_of_scope reset_stream_cache( [this]() {
    stream_cache.reset();
    preloaded_content_.clear(); // free ~50 MB of pre-loaded JSON text
} );
```

**Why**: The ~740 mapgen deferred entries are resolved in `calculate_mapgen_weights()` via `get_cached_stream`. The LRU stream cache is cold at the start of every `finalize_loaded_data`. With `preloaded_content_` populated from Phase A, every deferred mapgen re-read hits memory — zero additional Windows Defender scans during finalization.

---

### Step 2 — Add object-span extractor (worker-safe JSON tokenizer)

**File**: `src/init.cpp`, `src/init.h`

Add nested types and a static private method to `DynamicDataLoader` in `init.h` (inside the class, private section):

```cpp
struct object_span { int offset; int length; };
struct loaded_json_file {
    std::string path;
    std::string src;
    std::string base_path;
    std::string content;                  // owned file text
    std::vector<object_span> spans;       // top-level JSON objects, in file order
};
static auto extract_object_spans( loaded_json_file &lf ) -> void;
```

Implement in `init.cpp` before `load_data_from_path`:

```cpp
// static
auto DynamicDataLoader::extract_object_spans( loaded_json_file &lf ) -> void
{
    // Worker-safe: reads lf.content only, writes lf.spans only.
    // Never calls load_object or any type_function_map handler.
    if( lf.content.empty() ) { return; }
    membuf buf( lf.content.data(), lf.content.size() );
    std::istream stream( &buf );
    JsonIn jsin( stream, lf.path );

    jsin.eat_whitespace();
    if( jsin.test_array() ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            if( !jsin.test_object() ) { jsin.skip_value(); continue; }
            const int start = jsin.tell();
            jsin.skip_object();
            const int end = jsin.tell();
            lf.spans.push_back( { start, end - start } );
        }
    } else if( jsin.test_object() ) {
        const int start = jsin.tell();
        jsin.skip_object();
        const int end = jsin.tell();
        lf.spans.push_back( { start, end - start } );
    } else {
        // Neither array nor object: sentinel span so Phase B hits the
        // existing load_all_from_json error path unchanged.
        lf.spans.push_back( { 0, static_cast<int>( lf.content.size() ) } );
    }
}
```

**Worker-safety**: `JsonIn` holds only `std::istream*` + a `shared_ptr<std::string>` for the path. `skip_object()` reads characters, tracks brace depth — no global or shared mutable state. Each worker operates on an independent `membuf`/`istream`/`JsonIn` stack.

**Edge cases**: Empty file → early return, spans empty, Phase B skips silently. Non-object/non-array file → sentinel span, Phase B hits existing error path. Non-object array elements → `skip_value()` discards them, same as current `load_all_from_json`.

---

### Step 3 — Rewrite `load_data_from_path` with explicit split-phase

**File**: `src/init.cpp`, function `DynamicDataLoader::load_data_from_path` (line 625–659)

Add `#include "thread_pool.h"` to `init.cpp` if not already present (grep for it first; as of this audit it was absent).

Replace the entire function body:

```cpp
void DynamicDataLoader::load_data_from_path( const std::string &path, const std::string &src,
        loading_ui &ui )
{
    assert( !finalized && "Can't load additional data after finalization. Must be unloaded first." );

    str_vec files = get_files_from_path( ".json", path, true, true );
    if( files.empty() ) {
        std::ifstream tmp( path.c_str(), std::ios::in );
        if( tmp ) { files.push_back( path ); }
    }

    // ── PHASE A (workers): read bytes + tokenize object spans ──────────────────
    // Workers call ONLY read_entire_file() and extract_object_spans().
    // Workers MUST NOT call load_object(), any type_function_map handler,
    // any SDL API, or any Lua API.
    // Each slot written by exactly one worker — no mutex needed.
    std::vector<loaded_json_file> loaded( files.size() );
    parallel_for( 0, static_cast<int>( files.size() ), [&]( int i ) {
        loaded[i].path      = files[i];
        loaded[i].src       = src;
        loaded[i].base_path = path;
        loaded[i].content   = read_entire_file( files[i] );
        extract_object_spans( loaded[i] );
    } );

    // ── PHASE BOUNDARY ─────────────────────────────────────────────────────────
    // All file content is in memory. All object byte-spans are known.
    // load_object has NOT been called. Factory singletons are unmodified.

    g_last_load_metrics = {};
    g_last_load_metrics.files = static_cast<int>( loaded.size() );
    for( auto &lf : loaded ) {
        g_last_load_metrics.bytes += lf.content.size();
        preloaded_content_.try_emplace( lf.path, lf.content );
    }

    // ── PHASE B (main thread): serial dispatch ─────────────────────────────────
    // Iterates in original file order (preserving mod-override semantics).
    // Behavior is identical to the pre-split serial baseline.
    for( auto &lf : loaded ) {
        for( const object_span &span : lf.spans ) {
            try {
                membuf buf( lf.content.data() + span.offset, span.length );
                std::istream stream( &buf );
                // Use the string-path overload (json.h:183) — NOT the json_source_location
                // overload (json.h:185) which calls seek(loc.offset); membuf already
                // starts at span offset 0.
                JsonIn jsin( stream, lf.path );
                JsonObject jo = jsin.get_object();
                load_object( jo, lf.src, lf.base_path, lf.path );
                jo.finish();
            } catch( const JsonError &err ) {
                throw std::runtime_error( err.what() );
            }
        }
        inp_mngr.pump_events(); // once per file, main thread only
    }
}
```

**`preloaded_content_.try_emplace`**: If two mods supply files at the same path, first mod wins — matching existing behavior where the first `load_data_from_path` call wins the deferred re-parse slot.

**`g_last_load_metrics`**: `.files` and `.bytes` accumulate correctly; `.bytes` is summed from pre-loaded content rather than inline but the value is identical.

---

### Step 4 — Windows-specific: `FILE_FLAG_SEQUENTIAL_SCAN` and AV advisory

**File**: `src/filesystem.cpp`, function `read_entire_file` (line 157)

Read `filesystem.cpp` in full before editing to find the Windows-specific file open path within `cata_ifstream`. Add `FILE_FLAG_SEQUENTIAL_SCAN` to the `CreateFile` call on Windows. If `cata_ifstream` delegates to `std::fopen`/POSIX uniformly with no Win32-specific path, add a Win32-only `read_entire_file` overload:

```cpp
#if defined(_WIN32)
std::string read_entire_file( const std::string &path )
{
    // Reuse the existing wide-path conversion function already in this file.
    const std::wstring wpath = /* existing widen() / str_to_wstr() call */;
    HANDLE h = CreateFileW( wpath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr );
    if( h == INVALID_HANDLE_VALUE ) {
        // Fallback to existing POSIX path which produces the correct error message.
        return read_entire_file_posix( path );
    }
    LARGE_INTEGER sz{};
    GetFileSizeEx( h, &sz );
    std::string content( static_cast<std::size_t>( sz.QuadPart ), '\0' );
    DWORD read_bytes = 0;
    ReadFile( h, content.data(), static_cast<DWORD>( content.size() ),
              &read_bytes, nullptr );
    CloseHandle( h );
    content.resize( read_bytes );
    return content;
}
#endif
```

Name the non-Win32 overload `read_entire_file_posix` only if required for the fallback; otherwise the `#if defined(_WIN32)` guard is sufficient.

Additionally, add a one-time `DebugLog` in `game::load_static_data()` after line 445 (the `DynamicDataLoader::get_instance()` call):

```cpp
#if defined(_WIN32)
DebugLog( DL::Info, DC::Main )
    << "Performance tip: add '" << PATH_INFO::datadir()
    << "' to Windows Defender exclusions to reduce load time "
       "(Settings → Windows Security → Virus & threat protection → Exclusions).";
#endif
```

---

### Step 5 — Parallel mapgen `setup_common()`

**Context**: `calculate_mapgen_weights()` is the most expensive single finalization step. It runs 740+ `setup_common()` calls serially, each taking ~200–800 µs (palette flatten, row decode, ~25 `jmapgen_piece` category loads). The per-entry work has no cross-entry data dependency — but there are six shared-state hazards that must ALL be fixed before running them concurrently. Every fix in this step is a prerequisite for the parallelism; do them all before changing the loop.

**Hazard inventory and exact fixes**:

**H1 — `palette_placings_cache` concurrent insert** (PRIMARY BLOCKER)

Location: `static std::map<std::pair<std::string, int>, mapgen_palette::placing_map> palette_placings_cache` at `mapgen.cpp:3110`.

`load_internal()` writes to this map on cache miss. Concurrent `emplace` on `std::map` is a data race.

Fix: add a `std::mutex` protecting the write path. In `mapgen.cpp`, anonymous namespace, add:

```cpp
static std::mutex s_palette_placings_mutex;
```

In `load_internal()`, find the `palette_placings_cache.emplace(...)` call and wrap the miss path:

```cpp
// Before (concurrent-unsafe):
auto [it, inserted] = palette_placings_cache.emplace( key, placing_map{} );
if( inserted ) { /* populate it->second */ }

// After:
{
    std::lock_guard<std::mutex> lock( s_palette_placings_mutex );
    auto [it, inserted] = palette_placings_cache.emplace( key, placing_map{} );
    if( inserted ) { /* populate it->second inside the lock */ }
}
```

Re-read `load_internal()` at `mapgen.cpp:3262` before editing to find the exact emplace call site.

**H2 — `string_id::_cid/_version` race on shared `jmapgen_piece` objects**

`s_flat_palettes.format_placings` entries are `shared_ptr<const jmapgen_piece>`. `mapgen_palette::add()` (line 3212) copies the `shared_ptr` — both source and destination point to the SAME `jmapgen_piece`. When workers call `.id()` on `ter_str_id`/`furn_str_id`/etc. inside those shared pieces, they race on writing `string_id::_cid` and `string_id::_version` (declared `mutable` at `string_id.h:351–353`). Two independent relaxed atomics on `_cid` and `_version` do NOT fix this: a reader can observe a fresh `_version` paired with a stale `_cid`, pass the validity check, and return the WRONG `int_id` — silent data corruption. And atomicizing `string_id.h` is prohibited (>10 usages, AGENTS.md §headers, taxes every runtime id lookup).

Fix: pre-warm — single-threaded, immediately after `pre_flatten_palettes()` — force `.id()` resolution on all `string_id` fields inside the shared `jmapgen_piece` objects. After warming, workers only READ already-cached `_cid` values; no writes, no race.

Add a new static helper in `mapgen.cpp` anonymous namespace, immediately before `calculate_mapgen_weights()`:

```cpp
// Pre-warm string_id→int_id conversions on all jmapgen_piece objects shared
// across palette entries in s_flat_palettes. These are stored as
// shared_ptr<const jmapgen_piece> and referenced by multiple mapgen entries;
// without pre-warming, parallel setup_common() calls would race on writing
// the mutable _cid/_version fields inside those shared pieces.
//
// jmapgen_piece::check() (mapgen.h:195) is a const method that reads — and
// therefore warms — all string_id fields. Called here before the parallel
// window; all type factories (terrain, furniture, items, …) are fully
// finalized before calculate_mapgen_weights() runs (step 24 of 43), so
// check() resolves every id correctly.
static void warm_shared_palette_piece_ids()
{
    for( const auto &[pid, palette] : s_flat_palettes ) {
        const std::string context = "palette " + pid.str();
        for( const auto &[key, placings] : palette.format_placings ) {
            for( const auto &piece : placings ) {
                piece->check( context, palette.parameters );
            }
        }
    }
}
```

`jmapgen_piece::check()` signature (confirmed at `mapgen.h:195`):
```cpp
virtual void check( const std::string &oter_name, const mapgen_parameters & ) const { }
```
The base class default is a no-op; subclass overrides validate and warm string_ids.

**H3 — `mapgen_defer::*` global error state**

Location: `namespace mapgen_defer` at `mapgen.cpp:647–660`. The variables `defer`, `jsi`, `member`, `message` are plain namespace-level globals used as an error-signal channel within a single `setup_common()` call.

Fix: make each `thread_local`:

```cpp
namespace mapgen_defer {
thread_local std::string member;
thread_local std::string message;
thread_local bool defer;
thread_local JsonObject jsi;
} // namespace mapgen_defer
```

No other change needed — the rest of the code reads/writes these within a single call stack.

**H4 — `get_cached_stream()` LRU cache not thread-safe**

`DynamicDataLoader::get_cached_stream()` calls `stream_cache->cache.insert()` on every access. The LRU cache is a doubly-linked list with no mutex — concurrent inserts corrupt it.

Fix: in `setup_common()` at `mapgen.cpp:3429`, add a branch for worker threads that bypasses `get_cached_stream()`:

```cpp
// Phase A: stream access
shared_ptr_fast<std::istream> owned_stream; // null on main thread (uses get_cached_stream)
std::istream *stream_ptr = nullptr;

// Thread-local membuf storage (must outlive jsin below)
struct WorkerStream {
    membuf buf;
    std::istream stream;
    WorkerStream( const char *base, std::size_t size )
        : buf( base, size ), stream( &buf ) {}
};
std::optional<WorkerStream> worker_stream;

if( is_pool_worker_thread() ) {
    const std::string *content =
        DynamicDataLoader::get_instance().get_preloaded_content( *jsrcloc->path );
    if( !content ) {
        debugmsg( "mapgen parallel setup: no preloaded content for %s; skipping",
                  jsrcloc->path->c_str() );
        return;
    }
    worker_stream.emplace( content->data(), content->size() );
    stream_ptr = &worker_stream->stream;
} else {
    owned_stream = DynamicDataLoader::get_instance().get_cached_stream( *jsrcloc->path );
    stream_ptr = owned_stream.get();
}
JsonIn jsin( *stream_ptr, *jsrcloc );
```

**H5 — `g_mg_*` non-atomic counters**

Location: `mapgen.cpp:533–537`. Five `int64_t` globals incremented via `+=` in the hot path.

Fix: change all five to `std::atomic<int64_t>`, use `fetch_add` with `memory_order_relaxed`:

```cpp
// In mapgen.cpp anonymous namespace:
std::atomic<int64_t> g_mg_stream_us{0};
std::atomic<int64_t> g_mg_getobj_us{0};
std::atomic<int64_t> g_mg_setup_us{0};
std::atomic<int64_t> g_mg_inline_read_us{0};
std::atomic<int64_t> g_mg_palette_add_us{0};
```

Update every `g_mg_*_us += duration` site to `g_mg_*_us.fetch_add( duration, std::memory_order_relaxed )`.

Run `grep -n 'g_mg_' src/mapgen.cpp` to find all sites.

**H6 — `inp_mngr.pump_events()` in setup loop**

SDL is main-thread only. `pump_events()` is currently called after each container's `setup()` in `mapgen_factory::setup()` (line 428) and after each nested/update entry.

Fix: remove `inp_mngr.pump_events()` from the inner setup loop. Call it once on the main thread after the `parallel_for` barrier completes (the barrier itself guarantees all workers are done). In `calculate_mapgen_weights()`, after the parallel setup call:

```cpp
parallel_for( 0, n_fns, [&]( int i ) { setup_fns[i]->setup(); } );
inp_mngr.pump_events(); // once, main thread, after barrier
```

---

**Implementation — restructure `calculate_mapgen_weights()` and supporting methods**

After all 6 hazard fixes are in place, restructure `mapgen.cpp`:

**New method**: `mapgen_basic_container::collect_and_prepare(std::vector<mapgen_function*>& out)`:

```cpp
void mapgen_basic_container::collect_and_prepare( std::vector<mapgen_function *> &out ) {
    // Builds weights_ from mapgens_ (same filtering as setup()), drains mapgens_,
    // and appends eligible function pointers to out for parallel setup.
    for( const std::shared_ptr<mapgen_function> &ptr : mapgens_ ) {
        if( ptr->weight >= 1 ) {
            weights_.add( ptr, ptr->weight );
            out.push_back( ptr.get() );
        }
    }
    mapgens_.clear();
}
```

**New method**: `mapgen_factory::post_setup_cleanup()`:

```cpp
void mapgen_factory::post_setup_cleanup() {
    mapgens_.erase( "null" );
    any_direct_lua_generator_ = std::ranges::any_of( mapgens_, []( const auto &omw ) {
        return omw.second.has_direct_lua_generator();
    } );
}
```

**Rewrite `calculate_mapgen_weights()`** (replace lines 567–624):

```cpp
void calculate_mapgen_weights()
{
    // Reset instrumentation counters (now atomic, so store is fine here on main thread)
    g_mg_stream_us = g_mg_getobj_us = g_mg_setup_us =
    g_mg_inline_read_us = g_mg_palette_add_us = 0;

    // ── SYNC: pre-flatten palettes ─────────────────────────────────────────────
    // Populates s_flat_palettes (read-only during setup window).
    // Also populates palette_placings_cache for all named palettes.
    mapgen_palette::pre_flatten_palettes();
    warm_shared_palette_piece_ids(); // H2 fix: pre-warm shared piece string_ids before parallel window

    // ── COLLECT: build weights_, drain mapgens_, collect function ptrs ─────────
    // Done on main thread: per-container mutations, no parallelism yet.
    std::vector<mapgen_function *> setup_fns;
    for( auto &omw : oter_mapgen ) { // range-for over mapgens_ via public accessor
        omw.second.collect_and_prepare( setup_fns );
    }
    for( auto &pr : nested_mapgen ) {
        pr.second.precalc();
        for( auto &entry : pr.second ) {
            setup_fns.push_back( entry.obj.get() );
        }
    }
    for( auto &pr : update_mapgen ) {
        for( auto &ptr : pr.second ) {
            setup_fns.push_back( ptr.get() );
        }
    }

    // ── PARALLEL: setup_common() for each entry ────────────────────────────────
    // Each entry writes only to its own fields (is_ready, objects, parameters).
    // All 6 hazards (H1–H6) must be fixed before this executes correctly.
    parallel_for( 0, static_cast<int>( setup_fns.size() ), [&]( int i ) {
        setup_fns[i]->setup();
    } );
    inp_mngr.pump_events(); // once after barrier, main thread only

    // ── SYNC: post-setup cleanup ────────────────────────────────────────────────
    oter_mapgen.post_setup_cleanup();
    s_flat_palettes.clear();

    // ── PARALLEL: finalize_parameters (pure per-entry, no shared state) ────────
    parallel_for( 0, static_cast<int>( setup_fns.size() ), [&]( int i ) {
        setup_fns[i]->finalize_parameters();
    } );
    inp_mngr.pump_events(); // once after barrier, main thread only

    if( s_json_perf_enabled ) {
        fprintf( stderr,
                 "[JSON_PERF]   mapgen_setup: stream_ms=%lld  get_object_ms=%lld  "
                 "setup_jo_ms=%lld  inline_read_ms=%lld  palette_add_ms=%lld\n",
                 static_cast<long long>( g_mg_stream_us.load() / 1000 ),
                 static_cast<long long>( g_mg_getobj_us.load() / 1000 ),
                 static_cast<long long>( g_mg_setup_us.load() / 1000 ),
                 static_cast<long long>( g_mg_inline_read_us.load() / 1000 ),
                 static_cast<long long>( g_mg_palette_add_us.load() / 1000 ) );
    }
}
```

**`for( auto &omw : oter_mapgen )`**: `oter_mapgen` is a `static mapgen_factory` (private class in mapgen.cpp, line 489). Since `mapgen_factory` has `mapgens_` as private, add a `for_each_container(F&&)` visitor to `mapgen_factory`:

```cpp
template<typename F>
void for_each_container( F &&f ) {
    for( auto &omw : mapgens_ ) { f( omw.second ); }
}
```

Use it in `calculate_mapgen_weights()`:

```cpp
oter_mapgen.for_each_container( [&]( mapgen_basic_container &c ) {
    c.collect_and_prepare( setup_fns );
} );
```

**`finalize_parameters_common()` is pure per-entry** (confirmed by agent: just `objects.merge_parameters_into(parameters, "")`). The existing `finalize_parameters()` virtual chain calls it — safe to run on workers.

---

### Step 6 — Parallel finalization waves (conditional — measure before implementing)

**Do not implement until the following two conditions are both met**:

1. **Measure**: Run `CATA_JSON_PERF=1 ./cataclysm-bn-tiles --check-mods 2>&1 | grep JSON_PERF` after Steps 1–5 on the actual Win11 machine. Record `finalize_ms`. If `finalize_ms` is less than 25% of `total_wall_ms`, the serial finalization is not the bottleneck — skip this step.

2. **Per-entry body audit**: The `FinalizerBodies` agent confirmed that Wave 1 entries are data-independent of each other, BUT every `generic_factory::finalize()` call routes through `DynamicDataLoader::load_deferred()` which writes to factory maps (unordered_map + vector inserts) with no locks. Running any two `generic_factory::finalize()` calls concurrently that share deferred-queue state is a data race. Before parallelizing ANY wave, read the body of every `finalize_*` function in that wave and confirm: (a) its deferred queue is empty at the point of parallelism (i.e., `sort_deferred` + `load_deferred` were run before entering the parallel window for that type), OR (b) `load_deferred` for that type does not execute during `finalize_all` (some types have no deferred entries). Document the result for each entry.

If both conditions are met, the first safe step is Wave 1 only (entries 0,1,2,4,5,6,7,8,9 in the `entries` vector — Flags, MutationFlags, BodyParts, WeatherTypes, WorldTypes, FieldTypes, AmmoEffects, Emissions, SidebarWidgets). Add a gated `parallel_for` over Wave 1 before the existing serial loop for entries 3 onward:

```cpp
// Wave 1: verified-independent entries only
// Indices must be re-verified against the live entries vector before merge —
// do NOT use these indices without re-reading finalize_loaded_data().
const std::array<int,9> wave1 = { 0, 1, 2, 4, 5, 6, 7, 8, 9 };
if( parallel_enabled ) {
    parallel_for( 0, (int)wave1.size(), [&]( int wi ) {
        entries[wave1[wi]].second();
    } );
    for( int idx : wave1 ) { ui.proceed(); }
} else {
    for( int idx : wave1 ) { entries[idx].second(); ui.proceed(); }
}
// Remaining entries serial (indices 3, 10–42 in original order)
for( int idx : remaining ) { entries[idx].second(); ui.proceed(); }
```

Gate behind `--check-mods` run 50+ times without error variation before shipping. No TSan preset exists; add one manually: `cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread" -DCMAKE_C_FLAGS="-fsanitize=thread"` and run `--check-mods` under TSan on a machine where TSan is available.

---

## Critical Files & Anchors

- `src/init.cpp:625–659` — `load_data_from_path` — split-phase replacement; re-read full body before editing
- `src/init.cpp:188–203` — `get_cached_stream` — `preloaded_content_` check added here
- `src/mapgen.cpp:3110` — `palette_placings_cache` — add mutex here (H1)
- `src/mapgen.cpp:3421–3461` — `setup_common()` (no-arg) — stream bypass branch for workers (H4); atomic counter increments (H5)
- `src/mapgen.cpp:561` — anonymous namespace — add `warm_shared_palette_piece_ids()` helper here (H2); confirm line before editing

---

## Verification

### Baseline (run before any change, record numbers)
```sh
CATA_JSON_PERF=1 ./out/build/osx-arm-slim/cataclysm-bn-tiles --check-mods 2>&1 | grep JSON_PERF
```
Record `total_wall_ms`, `finalize_ms`, `parse_ms` for the `bn` mod, and `mapgen_setup:` line. On Win11 run equivalent.

### Steps 1–3 (split-phase I/O)
```sh
CATA_JSON_PERF=1 ./out/build/osx-arm-slim/cataclysm-bn-tiles --check-mods 2>&1 | grep JSON_PERF
```
`parse_ms` for `bn` mod must drop significantly vs baseline (parallel I/O). Zero new errors in `--check-mods`. Start a new game and load a save to confirm items, monsters, recipes intact.

### Steps 1–5 (parallel mapgen setup)
```sh
CATA_JSON_PERF=1 ./out/build/osx-arm-slim/cataclysm-bn-tiles --check-mods 2>&1 | grep JSON_PERF
```
`mapgen_setup: setup_jo_ms` must drop proportionally to available cores vs baseline. Zero new errors in `--check-mods`.

Start a new game; enter a building and open a town. Verify interior layout (palette-driven mapgen), nested mapgen (e.g., military base subrooms), and update mapgen (e.g., bookshelves populated) are visually correct vs pre-change baseline save.

### Win11 end-to-end target
From world-select click to game start: **< 5 seconds** (was > 15 s). `CATA_JSON_PERF` output: `total_wall_ms < 5000`.

---

## Assumptions & Contingencies

- **`read_entire_file` is thread-safe**: Confirmed at `src/filesystem.cpp:157` — opens a file via `cata_ifstream`, reads to string, returns. No global state. If `cata_ifstream` holds thread-unsafe global state (verify by reading it), wrap with `std::mutex` in `read_entire_file` rather than abandoning parallel I/O.

- **`JsonIn::skip_object()` has no shared state**: Based on `JsonIn` holding only `std::istream*` + `shared_ptr<std::string>` for the path. If it is found to touch any global (e.g., a global parser state), fall back to a hand-rolled brace-counter scanner for the span extraction in Phase A.

- **H2 pre-warm covers only palette-shared pieces**: The pre-warm calls `check()` on pieces in `s_flat_palettes.format_placings` — the only `jmapgen_piece` objects shared across multiple mapgen entries. Per-entry fields (`fill_ter`, `predecessor_mapgen`, per-entry `objects`) live on separate instances and cannot race. If a future refactor shares pieces through a different path, the pre-warm boundary would need revisiting — add a comment in `warm_shared_palette_piece_ids()` documenting this invariant.

- **H4 worker stream fallback**: If `get_preloaded_content()` returns `nullptr` for a mapgen entry (possible if a mod adds a mapgen file after `load_data_from_path` completes), `setup_common()` logs a `debugmsg` and returns early, leaving `is_ready = false`. On gameplay, the missing mapgen generates `t_null` fill terrain (existing behavior for unresolved entries). Log a `debugmsg` with the path so it surfaces during `--check-mods` testing.

- **`palette_placings_cache` mutex hot-path**: The lock is taken only on a cache MISS. After `pre_flatten_palettes()` pre-populates the cache for all named palettes, misses occur only for inline JSON palettes (one per unique mapgen entry with an inline "palette" block). The lock is released before the parallel `setup_common()` CPU work begins, so contention is low. If profiling shows the mutex is a bottleneck, convert `palette_placings_cache` to `std::unordered_map` (O(1) lookup vs O(log N)) as a secondary optimization.

- **`preloaded_content_` memory**: ~50 MB for full `data/json/` DDA dataset, held from `load_data_from_path` through end of `finalize_loaded_data`. Acceptable on minimum hardware (4 GB RAM). If a world mod set exceeds 500 MB JSON text, consider only pre-loading files that have deferred entries (post-Phase-B scan of `deferred_json` queues).
