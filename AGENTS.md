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

| Directory        | Contents                                                                                                                    |
| ---------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `src/`           | ~1000 .cpp/.h files — all game logic. Flat structure with headers co-located beside sources.                                |
| `src/lighting/`  | 40+ files — Vulkan render pipeline: sprite batching, ambient occlusion, bloom, volumetric fog, sound wave visualization.    |
| `src/lua/`       | 50+ files — vendored Lua 5.4 VM plus binding layer for exposing game APIs to Lua scripts.                                   |
| `src/physics/`   | 7 files — Box2D integration for vehicle physics simulation.                                                                 |
| `data/json/`     | JSON game content: items, recipes, monsters, mutations, terrain, vehicles, mapgens, etc.                                    |
| `data/mods/`     | Bundled mods, each with a `modinfo.json` descriptor.                                                                        |
| `tests/`         | Catch2 v3 test suite — 200+ test files organized by domain.                                                                 |
| `docs/`          | Developer documentation, modding guides, i18n docs. Follows Diátaxis framework (explanation, reference, guides, tutorials). |
| `lang/po/`       | Gettext `.po` translation files for all supported locales.                                                                  |
| `lang/`          | Localization tooling: extraction scripts, POT generation, MO compilation, stats.                                            |
| `scripts/`       | Deno/TypeScript automation: doc generation, migrations, changelog tools.                                                    |
| `build-scripts/` | Shell scripts for build, lint, and validation tasks.                                                                        |
| `tools/`         | Standalone utility programs (e.g. `check_po_printf_format.py`).                                                             |

## God files

The largest sources are `src/character.cpp`, `src/map.cpp`, `src/iuse.cpp`, `src/vehicle.cpp`, `src/iexamine.cpp`, `src/iuse_actor.cpp`, `src/activity_actor.cpp`, `src/game.cpp`, `src/overmap.cpp`, `src/mapgen.cpp`. Expect long compiles and heavy coupling when touching them; `wc -l` for current sizes.

## Coding Standards (new/modified code)

These standards apply to **new and modified code**. The existing codebase predates many of these conventions — do not churn legacy signatures to match.

| ❌ AVOID                               | ✅ PREFER                                                                        |
| -------------------------------------- | -------------------------------------------------------------------------------- |
| manual iterator loops (`it++`, `++it`) | `std::ranges::*`, `collection \| std::views::*`, or range-based `for` if clearer |
| `int foo()`                            | `auto foo() -> int`                                                              |
| `Type x = value`                       | `auto x = value`                                                                 |
| `void fn(a, b, c, d, e)`               | `void fn(options_struct)`                                                        |
| `[](){\n return 1; \n }`               | `[](){ return 1; }`                                                              |

**Adoption reality:**

| Mandate                          | Current usage                            | Policy                                                           |
| -------------------------------- | ---------------------------------------- | ---------------------------------------------------------------- |
| `std::ranges`/`views`            | 935 + 220 occurrences, 165 `++it` remain | Required for new collection code                                 |
| Trailing return types            | 1,877 occurrences                        | Required for new functions                                       |
| `std::expected` for fallible fns | 3 uses (2 files)                         | Required for new fallible APIs; do not churn existing signatures |
| `std::optional`                  | 1,036 occurrences                        | Genuinely adopted; continue using                                |
| `constexpr`                      | 1,415 occurrences                        | Healthy adoption; continue using                                 |

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
- **MUST** preserve unused parameter names as comments instead of deleting them, e.g. `bool /*is_avatar*/` not `bool`; applies to functions and lambdas.
- **MUST** keep Lua function parameters typed with EmmyLua/LuaLS annotations, including existing and local helper functions: `---@param` and table `---@class`/`---@field` shapes where parameters are tables. Do not require or add `---@return` solely for annotation enforcement when the return type is inferable. Before touching Lua, inspect the file's annotation style and preserve complete function typing.
- **MUST** fix missing Lua binding type declarations at the binding/doc-generation source; do not hard-code generated binding classes in `data/raw/generate_types.lua` as a shortcut.
- **MUST** test C++ Lua binding behavior with real bound objects when adding or changing bindings; Lua-only mocks may supplement but must not be the sole validation for binding correctness.
- **MUST** use options struct for functions with more than 3 parameters. Use designated initializers at call sites.
- **MUST NOT** manually write an options/struct type at a call site when the function parameter type makes it inferable; use `{ .field = value }` instead of `options_type{ .field = value }`.
- **SHOULD** search for existing solution because it's a large, legacy codebase.
- **Formatting**: new non-translated formatting SHOULD use `std::format`; translated/user-visible strings MUST keep `string_format` printf-style (PO placeholder contract). Explicit non-goal: touching any of the 2,088 existing `string_format` call sites.
- **MUST** verify helper-specific matching semantics before relying on string prefixes. For overmap terrain `OtMatchType.PREFIX` / `is_ot_match`, pass the base token without a trailing separator, e.g. `"robofachq"`, because the matcher itself requires the following character to be `_`.

## Workflow

### WHEN given a link to an issue

Follow [docs/agents/pr-workflow.md](./docs/agents/pr-workflow.md): worktree branch `<type>/<issue-id>/<issue-slug>`, then the `pr` skill for title/body/evidence.

### WHEN a decision is the user's (grilling, wayfinder HITL tickets, design choices)

- **MUST** put the decisions to the user with the `ask` tool (one call per round, a question per decision, 2-5 options with tradeoffs in `description`, `recommended` set). **MUST NOT** present them as numbered or formatted questions in the chat reply, and **MUST NOT** restate the skill's format back to the user.
- **MUST** verify every flag, function, call site and number a question names (open the lines, or get `file:line` from a sub-agent) before asking. A scout summary is a lead, not a fact. If a fact is still being fetched, ask only the questions that do not depend on it.

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

- **Binary path (HARD — verify before trusting ANY test result)**: under `osx-arm-slim` (RelWithDebInfo) the live binaries are `out/build/osx-arm-slim/src/cataclysm-bn-tiles` and `out/build/osx-arm-slim/tests/cata_test-tiles`; the repo-root copies are stale Debug-era leftovers that still run and still exit 0/1 for code that no longer exists. NEVER trust either path's reputation: `ls -lT` all four and run the newest. Incident history and diagnostics: skill `cbn-osx-slim-binary-launch-path`.

- **Commit**: Commit **ATOMICALLY**. **MUST** Follow [Conventional Commits](./docs/en/contribute/changelog_guidelines.md). **MUST NOT** add body/footer unless critical.

## Factory (unattended lanes)

GitHub issues on the fork are the queue. `deno task factory run` gives each `factory:ready` ticket its own herdr worktree (`wt-factory-<issue>`), runs an implementer, then `deno task gate --tier full`, then a read-only reviewer, and only then pushes a `factory/*` branch and opens a draft PR into `feature/improvements`. The human approves a spec once (`deno task factory release <slug>`) and merges PRs. Design: `plans/agentic-software-factory.md`; numbers and the protected-path list: `tools/factory/config.ts`.

One skill owns each phase:

| Phase                  | Skill                                                                                                                 |
| ---------------------- | --------------------------------------------------------------------------------------------------------------------- |
| triage                 | `triage`                                                                                                              |
| design                 | `grill-with-docs`                                                                                                     |
| spec                   | `to-spec`                                                                                                             |
| tickets                | `to-tickets`: must emit `.github/ISSUE_TEMPLATE/factory-ticket.md` sections, labels `factory:draft` and `spec:<slug>` |
| implement              | `implement` with `tdd`                                                                                                |
| bug diagnosis          | `diagnosing-bugs`                                                                                                     |
| adversarial review     | `interrogate`, then `code-review`, then `blast-radius` for a `src/` change touching a header with more than 10 usages |
| verification authoring | `create-verification-skill`                                                                                           |

Do not enable `poteto-mode`, `autopilot-*` or any pstack playbook: they overlap `to-spec` and `implement`. Vendored pstack skills are pinned in `.agents/skills/PSTACK_UPSTREAM`.

- **MUST** in a factory lane (`FACTORY_LANE=1`): never push, merge, call `gh`, or edit protected paths (`tools/factory/**`, `.omp/hooks/**`, `.githooks/**`, `.github/workflows/**`, lint and format configs, `deno.jsonc`, `AGENTS.md`, `tools/clang-tidy-plugin/**`, `build-scripts/**`). The hook, the pre-push check and CI all enforce it. If blocked, say what you need in your final message and stop.
- The gate (`deno task gate`) is the only definition of done. New or changed C++ lines must satisfy `modernize-use-trailing-return-type`, `modernize-use-auto` and `cata-*` (including `cata-no-pair-tuple-return`); legacy lines in a touched file are exempt.

## WHEN working on i18n / PO context, or translating docs

Follow [docs/agents/i18n.md](./docs/agents/i18n.md): full coverage of every named meaning, `msgfmt` + `check_po_printf_format.py` gates before PR, glossary search in the target PO before coining a term.

## Token Optimization

- Prepend `rtk` to inherently verbose commands (builds, test runs, git logs, large listings) to compress their output before it enters context.
- The harness routes shell `grep`/`cat`/`find` to the dedicated `grep`/`read`/`glob` tools; write shell commands accordingly instead of fighting the block.
- Page large outputs instead of dumping them: `read` line ranges, `artifact://<id>` for spilled tool output, and `proc://<name>:-80` for the tail of a build/service log.

### Playtesting and visual verification

**Verify gameplay and render changes with `bnplay`, not by taking screenshots and looking at
them.** Read the `bnplay` skill (`.agents/skills/bnplay/SKILL.md`) first: it runs an Episode end
to end and lists the traps (one game per user directory, binary freshness, the watchdog).

- `deno task bnplay doctor` before trusting any run: it fails on a stale binary, a missing
  driver flag, a missing fixture baseline, stray games, and low memory or swap.
- An Episode is `fixture add` (once), `start <trial.toml>`, `step <session> '<json>'`, `stop`.
  `stop` prints a report of under ~500 tokens; its exit code is the verdict (0 pass, 1 oracle
  failed, 2 harness error, 3 inconclusive) and the transcript is the repro.
- Escalate cheapest first: the lean `step` response, then `view`/`query`, then the transcript and
  debug log, then `capture` (windowed Trials only, for renderer work; judge frames with the
  `paired_null`/`diff_vs_null`/`triplet` oracles, never by eye).
- The `computer` tool cannot measure anything and costs ~1,230 tokens per PNG; use it only to
  discover something once.
- Windows contributors: see `tools/visual_verify/README.md` for the `vv.py` harness.

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
- **Binary**: `out/build/<preset>/tests/cata_test-tiles` for RelWithDebInfo/Release caches,
  `./cata_test-tiles` for caches first configured as Debug. Run it with an optional filter
  string, e.g. `"[item]"` or `"~[.]"`. Check the mtime before trusting a result; see the
  binary-path rule under "WHEN working on code changes".
- **Pre-existing failures** (current tree, not caused by your diff): `translation_text_style_check`
  and `translation_text_style_check_error_recovery` in `tests/json_test.cpp`. Both are `[.]`-tagged
  (excluded by default; a bare `"[json]"` filter opts them IN) and expect a debugmsg they never get,
  because `text_style_check()` is only wired into `tools/clang-tidy-plugin/TextStyleCheck.cpp`, never
  into the runtime JSON reader. Do not attribute these to your change.
- **Domain tags**: Tests are tagged by domain — `[item]`, `[melee]`, `[json]`, `[coop]`, `[driver]` (the agent driver, `src/driver_*.cpp`), `[calendar]`, `[map]`, `[vehicle]`, etc. Filter with `"[tag]"` to run only relevant tests.
- **Slow tests**: Tagged `[.]` and excluded by default. Include them with `"~[.]"` or explicitly.
- **Helper modules**:
  - `tests/map_helpers.h` — `build_test_map`, `spawn_test_monster`, and map manipulation utilities.
  - `tests/player_helpers.h` — `spawn_npc`, `arm_character`, and player state setup.
  - `tests/fake_messages.cpp` — Stub for UI/message output to suppress console spam in tests.
  - `tests/assertion_helpers.h` — `check_containers_equal` and other container comparison utilities.
  - `tests/stringmaker.h` — Catch2 `StringMaker` specializations for game types.
- **Probabilistic testing**: `statistics<T>` template with Z-score confidence intervals for stochastic test assertions.
- **Custom runner**: `tests/test_main.cpp` initializes full game state including mod loading, world setup, and RNG seeding before each test.
