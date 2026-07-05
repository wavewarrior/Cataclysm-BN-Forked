# Cataclysm: Bright Nights - Agent Guidelines

## HARD CONSTRAINTS (NEVER VIOLATE)

Before writing **ANY** code, verify:

| ❌ VIOLATION                           | ✅ REQUIRED                                                                      |
| -------------------------------------- | -------------------------------------------------------------------------------- |
| manual iterator loops (`it++`, `++it`) | `std::ranges::*`, `collection \| std::views::*`, or range-based `for` if clearer |
| `int foo()`                            | `auto foo() -> int`                                                              |
| `Type x = value`                       | `auto x = value`                                                                 |
| `void fn(a, b, c, d, e)`               | `void fn(options_struct)`                                                        |
| `[](){\n return 1; \n }`               | `[](){ return 1; }`                                                              |

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
