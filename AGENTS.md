# Repository Guidelines

## Project Overview

[Cataclysm: Bright Nights](https://github.com/CataclysmBN/Cataclysm-BN) is an open-source, procedurally-generated roguelike survival game. Top-down perspective, turn-based gameplay with deep crafting, combat, and base-building systems.

- **Language**: C++20/23 with heavy use of modern features (ranges, concepts, stackful fibers via minicoro).
- **Renderer**: SDL3 + Vulkan (tileset-based sprite rendering with advanced lighting).
- **Content**: JSON-driven — items, recipes, monsters, mutations, mapgens, vehicles, and more are all defined in `data/json/` and loaded via factory classes at startup.
- **Scripting**: Embedded Lua 5.4 VM for modding and in-game scripting.
- **Formatting**: fmt 12.2 (vendored, flat naming as `fmtlib_*.h`) for internal formatting; `string_format` printf-style for translator-facing strings.
- **Networking**: Optional co-op multiplayer via SDL3_net.
- **Platforms**: Linux, macOS, Windows.

## Architecture & Data Flow

- **Global singleton**: `extern std::unique_ptr<game> g` owns all subsystems — world, entities, trackers, event bus, calendar, UI, and input. Refactoring touches this extensively.
- **Event bus**: `event_bus` class (pub/sub pattern) for decoupled subsystem communication. Events are type-safe templates: `event_type::avatar_moves`, `event_type::character_kills_monster`, `event_type::angers_amigara_horrors`, etc.
- **JSON-driven factories**: `item_factory`, `monster_factory`, `mutation_factory`, `recipe`, `mapgen`, etc. construct all game content from `data/json/` at startup. Factory classes live flat in `src/` (e.g. `item_factory.cpp`, `generic_factory.cpp`).
- **Type-safe identifiers**: `string_id<T>` template provides compile-time type-safe string identifiers (e.g. `item_type`, `monster_type`).
- **Entity tracking**: `weak_ptr_fast<T>` and `creature_tracker` for efficient entity lifetime management without full `std::weak_ptr` overhead.
- **Chunked tile map**: `map.cpp` / `submap.cpp` implement a chunked, streaming tile map with procedural generation. Terrain is stored in fixed-size submaps that load/unload based on player position.
- **Range adapters**: C++20 range adapters like `non_dead_range<T>`, `monster_range` for ergonomic entity iteration throughout the codebase.

## Key Directories

| Directory | Contents |
|-----------|----------|
| `src/` | ~1000 .cpp/.h files — all game logic. Flat structure with headers co-located beside sources. |
| `src/lighting/` | 40+ files — Vulkan render pipeline: sprite batching, ambient occlusion, bloom, volumetric fog, sound wave visualization. |
| `src/lua/` | 50+ files — vendored Lua 5.4 VM plus binding layer for exposing game APIs to Lua scripts. |
| `src/physics/` | 7 files — Box2D integration for vehicle physics simulation. |
| `data/json/` | JSON game content: items, recipes, monsters, mutations, terrain, vehicles, mapgens, etc. |
| `data/mods/` | Bundled mods, each with a `modinfo.json` descriptor. |
| `tests/` | Catch2 v3 test suite — 200+ test files organized by domain. |
| `docs/` | Developer documentation, modding guides, i18n docs. Follows Diátaxis framework (explanation, reference, guides, tutorials). |
| `lang/po/` | Gettext `.po` translation files for all supported locales. |
| `lang/` | Localization tooling: extraction scripts, POT generation, MO compilation, stats. |
| `scripts/` | Deno/TypeScript automation: doc generation, migrations, changelog tools. |
| `build-scripts/` | Shell scripts for build, lint, and validation tasks. |
| `tools/` | Standalone utility programs (e.g. `check_po_printf_format.py`). |

## Important Files

| File | Lines | Role |
|------|-------|------|
| `src/character.cpp` | 11713 | Largest source file — player/NPC character logic, stats, effects, inventory. |
| `src/map.cpp` | 9817 | Chunked tile map, streaming, procedural generation, terrain interaction. |
| `src/iuse.cpp` | 8919 | Item-use system — dispatches all item interactions. |
| `src/vehicle.cpp` | 8430 | Vehicle construction, parts, movement, and physics integration. |
| `src/iexamine.cpp` | 8186 | Examine/dispatch system for item and terrain examination. |
| `src/iuse_actor.cpp` | 7879 | Actor-driven item-use activities (cooking, sewing, etc.). |
| `src/activity_actor.cpp` | 7426 | Activity system — long-running player actions with interruption handling. |
| `src/game.cpp` | 7196 | Central orchestrator — game loop, tick processing, global state, subsystem init. |
| `src/overmap.cpp` | 6700 | World-overview map for long-distance travel and wilderness generation. |
| `src/mapgen.cpp` | 6555 | Map generation engine — parses JSON mapgen rules into terrain. |
| `src/avatar.cpp` | 1580 | Avatar-specific player behavior (extends character). |
| `src/CMakeLists.txt` | — | Source compilation, target definitions, header/source globs. |
| `CMakePresets.json` | — | Build presets: `linux-slim`, `osx-arm-slim`, `linux-full`, `windows-tiles-sounds-x64-msvc`, etc. |
| `tests/test_main.cpp` | — | Catch2 v3 test runner — initializes full game state, mods, world, RNG seeding. |
| `tests/map_helpers.h` | — | Test fixtures: `build_test_map`, `spawn_test_monster`, and map manipulation helpers. |
| `tests/player_helpers.h` | — | Test helpers: `spawn_npc`, `arm_character`, and player state setup. |

## Coding Standards (new/modified code)

These standards apply to **new and modified code**. The existing codebase predates many of these conventions — do not churn legacy signatures to match.

| ❌ AVOID                                   | ✅ PREFER                                                                        |
| ------------------------------------------ | -------------------------------------------------------------------------------- |
| manual iterator loops (`it++`, `++it`)     | `std::ranges::*`, `collection \| std::views::*`, or range-based `for` if clearer |
| `int foo()`                                | `auto foo() -> int`                                                              |
| `Type x = value`                           | `auto x = value`                                                                 |
| `void fn(a, b, c, d, e)`                   | `void fn(options_struct)`                                                        |
| `[](){\n return 1; \n }`                   | `[](){ return 1; }`                                                              |

**Adoption reality:**

| Mandate | Current usage | Policy |
|---------|---------------|--------|
| `std::ranges`/`views` | 935 + 220 occurrences, 165 `++it` remain | Required for new collection code |
| Trailing return types | 1,877 occurrences | Required for new functions |
| `std::expected` for fallible fns | 3 uses (2 files) | Required for new fallible APIs; do not churn existing signatures |
| `std::optional` | 1,036 occurrences | Genuinely adopted; continue using |
| `constexpr` | 1,415 occurrences | Healthy adoption; continue using |

**Prefer `std::ranges`/`std::views`/`std::ranges::to`/cata_algo.h for collection work. Avoid manual iterator increment loops unless required by mutation semantics.**

- prefer function-local `using namespace std::views;` and use `transform`/`filter` unqualified.
- prefer function-local `namespace ranges = std::ranges;` and use `ranges::*` without `std::`
- prefer method/function references over lambdas whenever possible, e.g. `transform( &vpart_position::part_index )` instead of `transform( []( const auto &vp ) { return vp.part_index(); } )`.

## Coding Convention

```c++
const auto foo = 3; //< **MUST** use `auto` for type. `const` **MUST** come before `auto`.

auto bar() -> int; //< **MUST** use trailing return types.
using my_callback_t = std::function<auto( int ) -> bool>; //< **MUST** use trailing return types in type aliases.
auto baz() -> int&; // *NOPAD*  //< **MUST** append `// *NOPAD*` for references/pointer returns to prevent astyle bugs.
auto qux() -> int { return 42; } //< **MUST** use single-line functions whenever possible.

auto qux = my_struct{ .a = 1, .b = 2 }; //< **MUST** use designated initializers.
auto two_value() -> my_data; //< **MUST NOT** use `std::pair`/`std::tuple` for multiple return values. Create a struct instead.
auto may_have_value() -> std::optional<int>; //< **MUST** use `std::optional` for functions that may not return a value.
auto may_fail() -> std::expected<int, std::string>; //< **MUST** use `std::expected` for functions that may fail.

/// **MUST** use triple slash for doc comments like rust's.
/// **MUST** use snake_case for functions and variables.
struct comparable {
  int x;
  int y;
  auto operator<=>( const comparable & ) const = default; // *NOPAD* //< **MUST** use `<=>` for comparisons and append `// *NOPAD*` at the end to prevent astyle bugs.
}

auto values = xs
  | std::views::filter( []( const auto & v ) { return v.is_valid(); } ) //< **MUST** use single line expression if it's single line expression
  | std::views::transform( []( const auto & v ) { return v.get_value(); } ) //< **SHOULD** use `auto` for lambda params
  | std::ranges::to<std::vector>(); //< **MUST** use `std::ranges` over for loops for collections.

namespace { // **MUST** use anonymous namespace for internal linkage over `static`.

// **MUST** use options struct for functions with >3 parameters
struct button_options {
  point pos;
  std::string text;
  nc_color fg = c_white;
  nc_color bg = c_black;
  bool enabled = true;
};
auto print_button( const catacurses::window &w, const button_options &opts ) -> void;

} // namespace
```

- **SHOULD NOT** modify existing headers with >10 usages. Create new header with pure functions.
- **MUST** use modern C++23 features.
- **MUST** use options struct for functions with more than 3 parameters. Use designated initializers at call sites.
- **MUST NOT** manually write an options/struct type at a call site when the function parameter type makes it inferable; use `{ .field = value }` instead of `options_type{ .field = value }`.
- **SHOULD** search for existing solution because it's a large, legacy codebase.
- **Formatting**: new non-translated formatting SHOULD use `std::format`; translated/user-visible strings MUST keep `string_format` printf-style (PO placeholder contract). Explicit non-goal: touching any of the 2,088 existing `string_format` call sites.

## Workflow

### WHEN given a link to an issue

- **Context**: Fetch issue details via GitHub MCP.
- **Branch**: Use `coderabbitai/git-worktree-runner` to create branch: `git gtr new <type>/<issue-id>/<issue-slug>`
  - type MUST be one of: `feat`, `fix`, `refactor`, `chore`, `build`, `ci`
- **Code**: Refer to [code changes](#when-working-on-code-changes).
- **PR**: Use [Template](./.github/pull_request_template.md). **DO NOT ADD fluff**. create via `git push && gh pr create --web --fill`.

### WHEN creating a plan

**MUST** write the plan to two places simultaneously:
1. `local://<slug>.md` — for subagent handoff and `do` execution
2. `plans/<slug>.md` in this repo — permanent record that survives session resets

The `plans/` directory exists in this repo. Use the same kebab-case slug for both. The repo file is the source of truth for long-running or multi-session work.

### WHEN working on code changes

- **Style**: Follow [Code Style](./docs/en/dev/explanation/code_style.md). Use `_( "text" )` for L10n.
- **Format**: Format code before building/testing.

```sh
# Format C++ code
cmake --build build --target format
# Format JSON files
cmake --build build --target style-json-parallel
# Format scripts
deno fmt
deno task dprint fmt
```

- **Verify**: Build and fix any issues. Do not skip the game binary target when validating code changes; build `cataclysm-bn-tiles` together with tests.

```sh
# Build project and tests
cmake --preset linux-full
cmake --build --preset linux-full --target cataclysm-bn-tiles cata_test-tiles
```

- **Build rules (HARD — never violate)**:
  1. **NEVER run a build synchronously or with a short timeout.** A killed build corrupts `.ninja_deps`/`.ninja_log`, causing ninja to do a near-full rebuild on every subsequent run. Always start builds as background jobs with a 1200 s+ timeout and poll to completion:
     ```sh
     # CORRECT
     cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles &
     # then poll; never kill mid-run
     ```
  2. **NEVER bundle a build into a `&&`-chain with a short cap.** If the cap fires, ninja is killed mid-write and the dep log is corrupt.
  3. **Recovery from corrupted dep log**: run ONE complete uninterrupted build to completion — ninja repairs its own log during a clean run.
  4. **`src/CMakeLists.txt` header glob must NOT use `CONFIGURE_DEPENDS`**. The headers glob (`CATACLYSM_BN_HEADERS`) must be plain — the compiler's `-MMD` flags already track header dependencies. `CONFIGURE_DEPENDS` on headers triggers a cmake re-run on every new `.h`, which cascades into a full shadercross/LLVM/RmlUI rebuild. The `.cpp` glob keeps `CONFIGURE_DEPENDS` (needed to detect new source files).
  5. **ccache cap**: default 5 GB is too small for LLVM + SPIRV-Tools objects (constant evictions). Project cap is set to **20 GB** (`ccache --max-size=20G`). Verify with `ccache -s`; if cleanups spike, increase the cap.

- **Test**: Create/update relevant `tests/` (Catch2).

```sh
# Run Tests
./out/build/linux-full/tests/cata_test-tiles "[optional-filter]"

# Validate JSON
./build-scripts/lint-json.sh

# Check Mods (validates mod JSON files)
./out/build/linux-full/cataclysm-bn-tiles --check-mods

# Generate Lua Documentation (if conflicts with lua_annotations.lua or docs/en/mod/lua/reference/lua.md)
deno task docs:gen
```

- **Commit**: Commit **ATOMICALLY**. **MUST** Follow [Conventional Commits](./docs/en/contribute/changelog_guidelines.md). **MUST NOT** add body/footer unless critical.

## WHEN working on i18n / PO context

- **MUST NOT** reduce requested string/context coverage for review risk or churn. If the user names a word and its meanings, handle every named meaning.
- If adding JSON context requires loader support, add loader support instead of leaving a source uncontexted.
- **MUST** run `msgfmt -f -c -o /tmp/ko.mo lang/po/ko.po` after touching Korean PO files and fix reported errors before PR.
- **MUST** run `./tools/check_po_printf_format.py` after touching PO files and fix reported errors before PR.
- Do not call PO/printf errors pre-existing to skip them when the task touches that locale or validation path.
- If a mistake is found during the task, update AGENTS/skill immediately and fix the current branch before summarizing.

## WHEN translating docs

When translating, MUST search for correct glossary, e.g

```sh
rg -C2 -i '<<TARGET>>' lang/po/<<LANG>>.po | rg -v '^(#:|--)' | head -n 20
rg -C2 -i 'speedway' lang/po/ko.po | rg -v '^(#:|--)' | head -n 20
```

## Token Optimization (MANDATORY for verbose outputs)

Use installed token reduction tools to compress tool outputs before they enter context:

- **rtk** — Prepend `rtk` to terminal commands with verbose output (builds, tests, git logs, large listings):
  ```sh
  rtk cmake --build --preset windows-tiles-sounds-x64-msvc --target cataclysm-bn-tiles
  rtk ./out/build/win-rel-deb/tests/cata_test-tiles "[filter]"
  rtk git log --oneline -50
  ```
- **Headroom** — Compress large file contents or search results via Python `execute_code` when output exceeds ~2000 chars.

See `token-optimization` skill for details. Track savings with `rtk gain`.

## References

- **Docs**: [Building](./docs/en/dev/guides/building/cmake.md), [Formatting](./docs/en/dev/guides/formatting.md), [Dev Index](./docs/en/dev/).
- **Review**: [LLM Guide](./.github/llm_review_guide.md).

- When fixing a bug, preserve requested behavior and visible content unless the user explicitly asks to remove it; fix the underlying issue instead of suppressing the affected feature.
- When reviewing PRs that stop tracking generated or externally pulled files, verify ignore rules by running the generator/pull command or checking `git status --ignored`; do not assume removed tracked files are ignored.
- When generated or externally pulled files are removed from tracking, verify all CI and release consumers still receive required files or directories.

## Runtime/Tooling Preferences

- **CMake**: ≥ 3.24, Ninja build generator. Out-of-source builds enforced.
- **Compilers**: Clang (preferred Linux/macOS), GCC-14 (CI), MSVC (Windows).
- **Deno**: Used for all TypeScript scripts (doc generation, migrations, changelog tools). Run `deno task <name>` for scripted workflows.
- **Cache**: ccache (Linux/macOS) or sccache (Windows). Project cap is **20 GB** (`ccache --max-size=20G`).
- **Linker**: mold (Linux) for fast linking; default linker elsewhere.
- **Localization**: gettext toolchain (`msgfmt`, `msgmerge`, `xgettext`). See `lang/` for extraction and compilation scripts.

## Testing & QA

- **Framework**: Catch2 v3 (amalgamated, bundled in `tests/catch/`).
- **Binary**: `./out/build/<preset>/tests/cata_test-tiles` — run with optional filter string, e.g. `"[item]"` or `"~[.]"`.
- **Domain tags**: Tests are tagged by domain — `[item]`, `[melee]`, `[json]`, `[coop]`, `[calendar]`, `[map]`, `[vehicle]`, etc. Filter with `"[tag]"` to run only relevant tests.
- **Slow tests**: Tagged `[.]` and excluded by default. Include them with `"~[.]"` or explicitly.
- **Helper modules**:
  - `tests/map_helpers.h` — `build_test_map`, `spawn_test_monster`, and map manipulation utilities.
  - `tests/player_helpers.h` — `spawn_npc`, `arm_character`, and player state setup.
  - `tests/fake_messages.cpp` — Stub for UI/message output to suppress console spam in tests.
  - `tests/assertion_helpers.h` — `check_containers_equal` and other container comparison utilities.
  - `tests/stringmaker.h` — Catch2 `StringMaker` specializations for game types.
- **Probabilistic testing**: `statistics<T>` template with Z-score confidence intervals for stochastic test assertions.
- **Custom runner**: `tests/test_main.cpp` initializes full game state including mod loading, world setup, and RNG seeding before each test.