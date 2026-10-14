# Issue-to-PR workflow

Reached from AGENTS.md "WHEN given a link to an issue". PR mechanics live in the `pr` skill
(`.agents/skills/pr/SKILL.md`); this file is the surrounding flow.

- **Context**: Fetch issue details via GitHub MCP.
- **Branch**: `deno task lane <issue>` creates the worktree `$LANE_ROOT/lane-<issue>` on branch
  `lane/<issue>-<slug>` and starts the agent session in it (see
  [the check guide](../en/dev/guides/check.md)). On the Mac, `deno task worktree new
  <type>/<issue-id>/<slug>` (see `tools/worktree/`) wraps `herdr worktree create` from the primary
  checkout, copies the gitignored `data/shaders/*.msl` and configures; `list` and `remove` manage
  the fleet. Working by hand instead, use plain git:
  `git worktree add -b lane/<issue>-<slug> ~/cbn/lanes/lane-<issue> origin/feature/improvements`.
- **Code**: Refer to "WHEN working on code changes" in AGENTS.md.
- **Definition of done**: `deno task check` green in the worktree. Formatting is fixed for you by the
  auto-fix step; never run a broad formatter target by hand.
- **PR**: Follow the `pr` skill: validated title (`conventional-prs`), body from
  [Template](../../.github/pull_request_template.md) with evidence, explicit `--title`/`--body-file`
  (never bare `--fill`). **NO fluff.** Pushing and opening the PR stay with the human or with whoever
  started the lane, never with the agent inside it.
- After opening or updating a PR, track `gh pr checks` until CI finishes or a concrete blocker is
  identified; inspect failing job logs, fix branch-owned failures, commit, and push before finalizing.
  For transient or infrastructure failures, rerun when permitted or report the exact failing job and
  evidence.
