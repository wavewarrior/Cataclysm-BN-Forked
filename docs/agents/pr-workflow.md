# Issue-to-PR workflow

Reached from AGENTS.md "WHEN given a link to an issue". PR mechanics live in the `pr` skill (`.agents/skills/pr/SKILL.md`); this file is the surrounding flow.

- **Context**: Fetch issue details via GitHub MCP.
- **Branch**: Use `coderabbitai/git-worktree-runner` to create branch: `git gtr new <type>/<issue-id>/<issue-slug>`
  - type MUST be one of: `feat`, `fix`, `refactor`, `chore`, `build`, `ci`
- **Code**: Refer to "WHEN working on code changes" in AGENTS.md.
- **PR**: Follow the `pr` skill: validated title (`conventional-prs`), body from [Template](../../.github/pull_request_template.md) with evidence, explicit `--title`/`--body-file` (never bare `--fill`). **NO fluff.**
- After opening or updating a PR, track `gh pr checks` until CI finishes or a concrete blocker is identified; inspect failing job logs, fix branch-owned failures, commit, and push before finalizing. For transient or infrastructure failures, rerun when permitted or report the exact failing job and evidence.
- Before running broad formatter targets, prefer file-scoped formatting for touched files; if only a broad target exists, inspect and revert unrelated formatter-only changes before continuing.
