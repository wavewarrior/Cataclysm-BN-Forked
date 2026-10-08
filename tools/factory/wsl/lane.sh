#!/usr/bin/env bash
# Format checks + line-filtered clang-tidy (cata-* plugin, LLVM 22) for one lane branch.
#   lane.sh <git-common-dir> <branch> <sha> <affected-files.txt> <line-filter.json> \
#           <changed-cpp.txt> <changed-json.txt>
# Runs in the ext4 clone made by setup.sh. Two lanes share that clone, so it is serialised with
# flock. Only new or changed lines can fail tidy (line-filter.json), so legacy code in a touched
# file is exempt. Exit 0 = clean.
set -euo pipefail

LANE_HOME="${BN_TIDY_HOME:-$HOME/bn-tidy}"
COMMON_DIR="$1"
BRANCH="$2"
SHA="$3"
AFFECTED="$4"
LINE_FILTER="$5"
CHANGED_CPP="$6"
CHANGED_JSON="$7"

exec 9>"$LANE_HOME/lock"
echo "lane.sh: waiting for the lint lane lock"
flock 9

# shellcheck disable=SC1091
source "$LANE_HOME/env.sh"
cd "$LANE_HOME/src"

git fetch --quiet "$COMMON_DIR" "refs/heads/$BRANCH"
if [ "$(git rev-parse FETCH_HEAD)" != "$SHA" ]; then
    echo "lane.sh: branch $BRANCH moved (expected $SHA, fetched $(git rev-parse FETCH_HEAD))" >&2
    exit 1
fi
git checkout --quiet --detach FETCH_HEAD
git reset --quiet --hard
git clean -fdq

status=0

# existing_lines <list-file> <regex>: the listed files that match and still exist.
existing_lines() {
    grep -E "$2" "$1" | while IFS= read -r f; do [ -f "$f" ] && echo "$f"; done || true
}

# 0. JSON: formatting the changed files must change nothing.
existing_lines "$CHANGED_JSON" '\.json$' >"$LANE_HOME/json-files.txt"
if [ -s "$LANE_HOME/json-files.txt" ]; then
    mapfile -t json_files <"$LANE_HOME/json-files.txt"
    echo "lane.sh: json format check on ${#json_files[@]} file(s)"
    if ! bash build-scripts/format-json.sh "${json_files[@]}" >"$LANE_HOME/json-format.log" 2>&1; then
        tail -n 40 "$LANE_HOME/json-format.log"
        status=1
    fi
    if ! git diff --quiet; then
        echo "FORMAT-JSON: these files are not formatted; run build-scripts/format-json.sh:"
        git diff --name-only | sed 's/^/  /'
        git checkout -- .
        status=1
    fi
fi

existing_lines "$AFFECTED" '^(src|tests)/.*\.(cpp|h|hpp)$' >"$LANE_HOME/files.txt"
existing_lines "$CHANGED_CPP" '^(src|tests)/.*\.(cpp|h|hpp)$' >"$LANE_HOME/format-files.txt"
if [ ! -s "$LANE_HOME/files.txt" ] && [ ! -s "$LANE_HOME/format-files.txt" ]; then
    echo "lane.sh: no C++ files to check"
    exit "$status"
fi

# 1. C++ format: formatting the changed files must change nothing.
if [ -s "$LANE_HOME/format-files.txt" ]; then
    mapfile -t fmt_files <"$LANE_HOME/format-files.txt"
    bash build-scripts/format-cpp.sh "${fmt_files[@]}"
    if ! git diff --quiet; then
        echo "FORMAT: these files are not formatted (clang-format 22 / astyle); run build-scripts/format-cpp.sh:"
        git diff --name-only | sed 's/^/  /'
        git diff | head -n 80
        git checkout -- .
        status=1
    fi
fi

if [ ! -s "$LANE_HOME/files.txt" ]; then
    exit "$status"
fi
echo "lane.sh: tidy on $(wc -l <"$LANE_HOME/files.txt") file(s)"

BUILD_PATH="$LANE_HOME/build"
export BUILD_PATH
# build-clang-tidy-plugin.sh ends in a non-idempotent `ln -s`; clear the old link first.
rm -f compile_commands.json
echo "lane.sh: configuring and building the cata-* plugin"
SOUND=0 bash build-scripts/build-clang-tidy-plugin.sh >"$LANE_HOME/plugin-build.log" 2>&1 || {
    tail -n 60 "$LANE_HOME/plugin-build.log"
    exit 1
}

# 2. clang-tidy on new/changed lines. Command-line checks merge onto .clang-tidy, so cata-*
# stays on; confirm that before trusting an empty result.
PLUGIN="$BUILD_PATH/tools/clang-tidy-plugin/libCataAnalyzerPlugin.so"
CHECKS="modernize-use-trailing-return-type,modernize-use-auto"
WERROR="modernize-use-trailing-return-type,modernize-use-auto,cata-*"
listed="$(clang-tidy --load="$PLUGIN" --checks="$CHECKS" --list-checks 2>&1)"
for need in modernize-use-trailing-return-type modernize-use-auto cata-no-long cata-no-pair-tuple-return; do
    echo "$listed" | grep -q "$need" || {
        echo "lane.sh: check $need is not enabled; refusing to report a clean result" >&2
        echo "$listed" >&2
        exit 1
    }
done

FILTER="$(cat "$LINE_FILTER")"
jobs="${NUM_JOBS:-$(nproc)}"
set +e
xargs -a "$LANE_HOME/files.txt" -n1 -P"$jobs" clang-tidy -quiet \
    --load="$PLUGIN" -p "$BUILD_PATH" \
    --checks="$CHECKS" --warnings-as-errors="$WERROR" \
    --line-filter="$FILTER"
tidy_status=$?
set -e
if [ "$tidy_status" -ne 0 ]; then
    echo "TIDY: clang-tidy reported problems on new or changed lines"
    status=1
fi

exit "$status"
