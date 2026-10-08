#!/usr/bin/env bash
# Guards the agent-skill layout that `npx skills add/update` and `core.symlinks=false` break silently.
#   1. Every .claude/skills/<name> is a symlink to ../../.agents/skills/<name>. A plain file holding
#      the target path (what git writes when core.symlinks=false) hides the skill from Claude Code.
#   2. Repo-owned skills are absent from skills-lock.json. The vendor `mattpocock/skills` ships a
#      generic `pr` that overwrites ours; a lock entry means the next `skills update` clobbers it.
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

repo_owned=(pr bnplay)
status=0

for entry in .claude/skills/*; do
    name="${entry##*/}"
    if [[ ! -L "$entry" ]]; then
        echo "error: $entry is not a symlink (git config core.symlinks must be true); fix: ln -sfn ../../.agents/skills/$name $entry" >&2
        status=1
    elif [[ "$(readlink "$entry")" != "../../.agents/skills/$name" || ! -d ".agents/skills/$name" ]]; then
        echo "error: $entry must link to ../../.agents/skills/$name (found $(readlink "$entry"))" >&2
        status=1
    fi
done

for name in "${repo_owned[@]}"; do
    if [[ -f skills-lock.json ]] && grep -q "\"$name\": {" skills-lock.json; then
        echo "error: skills-lock.json lists repo-owned skill '$name'; remove its entry, then 'git checkout -- .agents/skills/$name'" >&2
        status=1
    fi
done

exit "$status"
