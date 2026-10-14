---
title: Formatting & Linting
---

This guide explains how to format and lint code in Cataclysm: Bright Nights.

## Quick Reference

| Scope           | Tool                | Command          |
| --------------- | ------------------- | ---------------- |
| Staged files    | all formatters      | `just fmt`       |
| All files       | all formatters      | `just fmt --all` |
| C++ (`.cpp/.h`) | clang-format 22.1.8 | `just fmt-cpp`   |
| JSON            | json_formatter      | `just fmt-json`  |
| Markdown/TS     | deno fmt            | `just fmt-docs`  |
| Lua             | dprint              | `just fmt-lua`   |

## Automated Formatting

Pull requests are automatically formatted by [autofix.ci](https://autofix.ci). If your code has
style violations, a commit will be pushed to fix them.

> [!TIP]
> To avoid merge conflicts after autofix commits, either:
>
> 1. Run `git pull` to merge the autofix commit, then continue working
> 2. Format locally before pushing, then `git push --force` if needed

## C++ Formatting

C++ has exactly one formatter: [clang-format](https://clang.llvm.org/docs/ClangFormat.html)
version 22.1.8, configured by `.clang-format` at the repository root. Formatter-sensitive fixtures
such as `tools/clang-tidy-plugin/test/` are left unchanged.

You do not install anything. `deno task fmt:cpp` bootstraps the pinned clang-format on first use
into `.cache/clang-tools/` (via `uv` or `python3 -m venv`, no sudo) and uses it for every format
invocation afterwards. Override the binary with `CATA_CLANG_FORMAT=/path/to/clang-format` if you
must.

```sh
# Format the C++ changed against origin/feature/improvements (default)
deno task fmt:cpp
# Format specific files (their changed hunks only)
deno task fmt:cpp src/map.cpp src/map.cpp
# What would be formatted, without writing
deno task fmt:cpp --dry-run
```

By default only changed hunks are touched, so the command can never reflow lines you did not
modify; `--whole-file` and `--all` opt into larger sweeps. Style is auto-fixed and is never a
gate failure.

Correctness linting (determinism, no `long`, test filenames) runs over changed lines only:

```sh
deno task tidy:changed --base origin/feature/improvements
```

It needs the Cata clang-tidy plugin, built standalone next to the `linux-fast` build
(`cmake -S tools/clang-tidy-plugin -B out/build/linux-fast/tools/clang-tidy-plugin -G Ninja`, then
`cmake --build` on that directory); the command prints the exact recipe when it is missing.

## JSON Formatting

JSON files are formatted with `json_formatter`, a custom tool built from the project source.

### Using the script

```sh
just fmt-json
# or
build-scripts/format-json.sh
```

The script builds `json_formatter` in `out/build/json-format` and formats JSON files. It does not
require a configured game build or a CMake preset. Override the helper build directory with
`CATA_JSON_FORMAT_BUILD_DIR` if needed.

> [!NOTE]
> The `data/names/` directory is excluded from formatting because name files have special formatting
> requirements.

### JSON Syntax Validation

Before formatting, you can validate JSON syntax:

```sh
build-scripts/lint-json.sh
```

This runs Python's `json.tool` on all JSON files to catch syntax errors.

## Markdown & TypeScript Formatting

Markdown and TypeScript files are formatted with [Deno](https://deno.land/).

```sh
# Install Deno
curl -fsSL https://deno.land/install.sh | sh

# Format Markdown and TypeScript
deno fmt
```

## Lua Formatting

Lua files are formatted with [dprint](https://dprint.dev/) via Deno.

```sh
# Format Lua files
deno task dprint fmt
```

## Dialogue Validation

NPC dialogue files have additional validation:

```sh
tools/dialogue_validator.py data/json/npcs/* data/json/npcs/*/* data/json/npcs/*/*/*
```

## Pre-commit Workflow

Install the optional pre-commit hook with [`prek`](https://github.com/j178/prek):

```sh
prek install
# or
just hooks-setup
```

When you commit, the hook runs `just fmt`: Deno and dprint run normally, while C++ and JSON formatters
run only on staged created/updated files. Formatted staged files are re-added so the commit includes
the formatted result.

To run the same formatting manually against staged files:

```sh
just fmt
```

Before committing without the hook, run these checks:

```sh
# Format staged files
just fmt

# Format all formattable files
just fmt --all
```

## CI Integration

The CI pipeline runs these checks automatically:

1. **JSON syntax validation** - `build-scripts/lint-json.sh`
2. **JSON formatting** - `build-scripts/format-json.sh`
3. **Dialogue validation** - `tools/dialogue_validator.py`

If any check fails, the build will fail. Use the commands above to fix issues locally before
pushing.

## Editor Integration

### VS Code

Use the repository helper for C++ and the Deno extension for Markdown/TypeScript:

- **C++**: run `just fmt-cpp`
- **Deno**: [Deno](https://marketplace.visualstudio.com/items?itemName=denoland.vscode-deno)

### Visual Studio

Formatting needs no manual install on any platform - `deno task fmt:cpp` fetches the pinned
clang-format itself. `clang-tidy` for `deno task tidy:changed` comes from LLVM:

```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
irm get.scoop.sh | iex
scoop install llvm
clang-tidy --version
```

If any version command is not found, close and reopen PowerShell and Visual Studio so they reload
`PATH`.

Visual Studio's normal **Format Document** command does not run repository style. Run
`deno task fmt:cpp` in a terminal instead.

### Vim/Neovim

Use the repository task instead of calling clang-format directly:

```vim
autocmd BufWritePre *.cpp,*.h !deno task fmt:cpp --whole-file %
autocmd BufWritePre *.md,*.ts !deno fmt %
```

## Troubleshooting

### "json_formatter not found"

Run the JSON formatter script. It configures and builds the formatter helper automatically:

```sh
build-scripts/format-json.sh
```

### C++ formatting fails to run

`deno task fmt:cpp` bootstraps clang-format 22.1.8 into `.cache/clang-tools/` using `uv` or
`python3`. If the bootstrap fails (no network, no Python), install clang-format 22.1.8 yourself
and point the task at it:

```sh
CATA_CLANG_FORMAT=/path/to/clang-format-22.1.8 deno task fmt:cpp
```

### C++ looks unformatted after pulling

Run the task from the repository root; it picks the right files and hunks by itself:

```sh
deno task fmt:cpp
```
