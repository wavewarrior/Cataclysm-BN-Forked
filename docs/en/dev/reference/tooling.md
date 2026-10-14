# Developer Tooling

## Code style (C++)

C++ formatting uses [clang-format](https://clang.llvm.org/docs/ClangFormat.html) 22.1.8 for every
C++ file, configured by `.clang-format` at the repository root. Formatter-sensitive fixtures such
as `tools/clang-tidy-plugin/test/` are left unchanged. Use the repository task so the pinned
version is picked for you.

### Invoking C++ formatting

```sh
deno task fmt:cpp          # the C++ you changed, changed hunks only
deno task fmt:cpp --help   # the other modes (--staged, --whole-file, --all, --dry-run)
```

The task bootstraps clang-format 22.1.8 into `.cache/clang-tools/` on first use (no sudo); set
`CATA_CLANG_FORMAT` to use a different binary. There is no CMake `format` target any more.

### Correctness linting

```sh
deno task tidy:changed --base origin/feature/improvements   # 3 checks, changed lines only
deno task fix:tidy                                         # trailing-return / auto autofix
```

`tidy:changed` loads the Cata clang-tidy plugin from a build directory (default
`out/build/linux-fast`, which supplies the compile database); it prints the standalone
`cmake -S tools/clang-tidy-plugin` recipe to build the plugin when it is missing.

### Invoking formatting via pre-commit hook

Install the optional hook with:

```sh
just hooks-setup
```

The hook formats staged C++ with the pinned clang-format, restricted to the staged hunks.

## JSON style

See the [JSON style guide](../../mod/json/explanation/json_style).

## ctags

In addition to the usual means of creating a `tags` file via e.g.
[`ctags`](http://ctags.sourceforge.net/), we provide `tools/json_tools/cddatags.py` to augment a
`tags` file with locations of definitions taken from CDDA JSON data. `cddatags.py` is designed to
safely update a tags file containing source code tags, so if you want both types of tag in your
`tags` file then you can run `ctags -R . && tools/json_tools/cddatags.py`.

## clang-tidy

Cataclysm has a
[clang-tidy configuration file](https://github.com/cataclysmbn/Cataclysm-BN/blob/main/.clang-tidy)
and if you have `clang-tidy` available you can run it to perform static analysis of the codebase. We
test with `clang-tidy` from LLVM 22 with CI, so for the most consistent results, you might want to
use that version.

To run it, you have a few options.

- `clang-tidy` ships with a wrapper script `run-clang-tidy.py`.

- Use CMake's built-in support by adding `-DCMAKE_CXX_CLANG_TIDY=clang-tidy` or similar, pointing it
  to your chosen clang-tidy version.

- To run `clang-tidy` directly try something like

```sh
grep '"file": "' build/compile_commands.json | \
    sed "s+.*$PWD/++;s+\"$++" | \
    egrep '.' | \
    xargs -P 9 -n 1 clang-tidy -quiet
```

To focus on a subset of files add their names into the `egrep` regex in the middle of the
command-line.

## Custom clang-tidy plugin

We have written our own clang-tidy checks in a custom plugin. refer to
[clang-tidy.yml](https://github.com/cataclysmbn/Cataclysm-BN/blob/main/.github/workflows/clang-tidy.yml)
for the exact steps to build the plugin on ubuntu 24.04.

### Build plugin on Ubuntu 24.04

Follow exact steps in [cmake guide](../../dev/guides/building/cmake.md) with following changes:

Also install these additional dependencies:

```sh
sudo apt-get install \
  clang-22 libclang-22-dev llvm-22 llvm-22-dev clang-tidy-22
```

add `CATA_CLANG_TIDY_PLUGIN=ON` to cmake flags when configuring the build.

### Build plugin on Fedora 40

Follow exact steps in [atomic cmake guide](../../dev/guides/building/atomic_cmake.md) with following
changes:

Also install these additional dependencies:

```sh
sudo dnf install clang-devel llvm-devel clang-tools-extra-devel
```

add `CATA_CLANG_TIDY_PLUGIN=ON` to cmake flags when configuring the build.

### Using the plugin

To run the plugin on a single file, run following command on project root:

```sh
$ ./build-scripts/clang-tidy-wrapper.sh -fix src/achievement.cpp
```

To run the plugin on multiple files, use [GNU parallel](https://www.gnu.org/software/parallel/):

```sh
$ parallel ./build-scripts/clang-tidy-wrapper.sh -fix ::: src/*.cpp
```

## include-what-you-use

[include-what-you-use](https://github.com/include-what-you-use/include-what-you-use) (IWYU) is a
project intended to optimise includes. It will calculate the required headers and add and remove
includes as appropriate.

Running on this codebase revealed some issues. You will need a version of IWYU where the following
PR has been merged (which has not yet happened at time of writing, but with luck might make it into
the clang-10 release of IWYU):

- https://github.com/include-what-you-use/include-what-you-use/pull/775

Once you have IWYU built, build the codebase using cmake, with `CMAKE_EXPORT_COMPILE_COMMANDS=ON` on
to create a compilation database (Look for `compile_commands.json` in the build dir to see whether
that worked).

Then run:

```sh
iwyu_tool.py -p $CMAKE_BUILD_DIR/compile_commands.json -- -Xiwyu --mapping_file=$PWD/tools/iwyu/cata.imp | fix_includes.py --nosafe_headers --reorder
```

IWYU will sometimes add C-style library headers which clang-tidy doesn't like, so you might need to
run clang-tidy (as described above) and then re-run IWYU a second time.

There are mapping files in `tools/iwyu` intended to help IWYU pick the right headers. Mostly they
should be fairly obvious, but the SDL mappings might warrant further explanation. We want to force
most SDL includes to go via `sdl_wrappers.h`, because that handles the platform-dependence issues
(the include paths are different on Windows). There are a couple of exceptions (`SDL_version.h` and
`SDL_mixer.h`). The former is because `main.cpp` can't include all SDL headers, because they
`#define WinMain`. All the mappings in `sdl.imp` are designed to make this happen.

We have to use IWYU pragmas in some situations. Some of the reasons are:

- IWYU has a concept of
  [associated headers](https://github.com/include-what-you-use/include-what-you-use/blob/master/docs/IWYUPragmas.md#iwyu-pragma-associated),
  where each cpp file can have some number of such headers. The cpp file is expected to define the
  things declared in those headers. In Cata, the mapping between headers and cpp files is not nearly
  so simple, so there are files with multiple associated headers, and files with none. Headers that
  are not the associated header of any cpp file will not get their includes updated, which could
  lead to broken builds, so ideally all headers would be associated to some cpp file. You can use
  the following command to get a list of headers which are not currently associated to any cpp file
  (requires GNU sed):

```sh
diff <(ls src/*.h | sed 's!.*/!!') <(for i in src/*.cpp; do echo $i; sed -n '/^#include/{p; :loop n; p; /^$/q; b loop}' $i; done | grep 'e "' | grep -o '"[^"]*"' | sort -u | tr -d '"')
```

- Due to a [clang bug](https://bugs.llvm.org/show_bug.cgi?id=20666), uses in template arguments to
  explicit instantiations are not counted, which leads to some need for `IWYU pragma: keep`.

- Due to
  [these](https://github.com/include-what-you-use/include-what-you-use/blob/4909f206b46809775e9b5381f852eda62cbf4bf7/iwyu.cc#L1617)
  [missing](https://github.com/include-what-you-use/include-what-you-use/blob/4909f206b46809775e9b5381f852eda62cbf4bf7/iwyu.cc#L1629)
  features of IWYU, it does not count uses in template arguments to return types, which leads to
  other requirements for `IWYU pragma: keep`.

- IWYU seems to have particular trouble with types used in maps. Have not looked into this in
  detail, but again worked around it with pragmas.
