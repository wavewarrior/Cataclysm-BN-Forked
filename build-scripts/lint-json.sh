#!/bin/sh
# Validate that every JSON file the repo owns parses.
#
# Four things this script used to get wrong, all of which made it report nonsense rather than
# fail usefully:
#
#   1. It invoked `python`, which does not exist on a python3-only box (modern Linux, macOS).
#      Because the interpreter was invoked once PER FILE and its absence was indistinguishable
#      from a parse failure, a missing dependency printed "FAILED" for every JSON in the tree —
#      thousands of lines implying the game data was broken. Resolve it ONCE, and say plainly
#      when there is none.
#   2. It walked build output, where `_deps/.../DirectXShaderCompiler/.../compile_commands.json`
#      are deliberately malformed clang test fixtures. Whether this script "passed" therefore
#      depended on whether you had built yet.
#   3. It always exited 0. A lint that cannot fail is decorative — nothing could gate on it.
#   4. Editor config under `.vscode/` is JSONC (comments, trailing commas): valid for the
#      editor, not valid JSON. The original walk skipped dot-directories by accident of its
#      prune; that exclusion is now deliberate.

if command -v python3 >/dev/null 2>&1; then
    PY=python3
elif command -v python >/dev/null 2>&1; then
    PY=python
else
    echo "lint-json.sh: neither python3 nor python is on PATH; cannot validate JSON" >&2
    exit 2
fi

# Tracked files when git is available — that excludes build output and everything else ignored,
# without this script having to guess directory names. Falls back to a pruned walk in a source
# tarball. Both branches emit repo-relative paths with no leading "./" so one filter serves.
list_json() {
    if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git ls-files '*.json'
    else
        find . \( -name ".*" -o -name out -o -name build -o -name _deps \) \
            -not -name "." -prune -o -name "*.json" -type f -print |
            sed 's|^\./||'
    fi
}

failures=$(
    list_json | grep -v '^\.' | while IFS= read -r f; do
        [ -f "$f" ] || continue
        "$PY" -m json.tool "$f" >/dev/null 2>&1 || echo "FAILED: $f"
    done
)

if [ -n "$failures" ]; then
    echo "$failures"
    exit 1
fi
