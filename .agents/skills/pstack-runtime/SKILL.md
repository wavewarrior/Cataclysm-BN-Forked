---
name: pstack-runtime
description: "Compatibility contract for running the adapted pstack skills in both Claude Code and Oh My Pi (OMP). Apply whenever a pstack workflow delegates work, reads transcripts, authors a skill, drives a runtime, or watches a PR."
---

# Pstack runtime contract

The pstack workflows are host-neutral. Resolve their capability words against the tools available in the current session. Never invent a tool, agent type, model slug, path, or cloud capability.

## Shared installation and invocation

The canonical skill root is `~/.claude/skills/`. Claude Code discovers it directly. OMP discovers the same root through its Claude user-skills provider.

Invoke a skill with the host's syntax:

- Claude Code: `/<name>`.
- OMP: `/skill:<name>`.

Inside a skill, refer to another skill by name. Do not hard-code either slash syntax into an agent brief.

## Subagents and model roles

Use the current host's subagent mechanism. Translate roles by capability, not by a Cursor-specific schema:

- Exploration or bulk reading: a read-only research/scout agent.
- Implementation: a general or code-writing agent with the required write tools.
- Review or judgment: a review-capable agent with no write access unless the workflow explicitly applies fixes.
- MCP-backed investigation: an agent that retains the required MCP tools; read-only means a no-write posture when the host cannot sandbox writes without removing MCPs.

Launch independent workers in one parallel batch. Use background jobs only when the host supports them and the parent has independent work to do. Keep each writer in its own worktree or output directory. The parent owns synthesis, verifies every accepted result, and never trusts a worker's completion claim by itself.

If `~/.claude/pstack-models.md` exists, read the section for the current host. A role value is a host-native agent or model selector. `inherit-parent` means omit the selector. If the host cannot apply the configured selector, fall back to `inherit-parent` and report the mismatch; never guess a replacement slug. Without a config file, use host defaults and vary available reviewer specializations when a panel needs independent perspectives.

## Transcripts and history

Use a transcript path or history URI explicitly supplied by the user or current host. Stay inside the active workspace and named session.

- Claude Code: use the active workspace's transcript location when the system exposes it; otherwise locate only the cwd-derived project directory under `~/.claude/projects/`.
- OMP: use the current conversation, an explicit `history://<id>` or `agent://<id>` URI, or an explicit current-session file under `~/.omp/agent/sessions/`.

Never glob every project or session. If the active transcript cannot be identified safely, write a tight digest from the current conversation and pass that instead.

## Skill authoring

Project skills live at `.claude/skills/<name>/SKILL.md`; personal skills live at `~/.claude/skills/<name>/SKILL.md`. Both hosts discover those locations in this setup.

Every skill needs `name` and `description` frontmatter. Keep supporting files inside its directory. Before delivery, verify the frontmatter parses, every referenced relative file exists, and the skill is discoverable in a fresh host session.

## Runtime proof and long waits

Drive the real surface with the host's available browser, debugger, process, HTTP, Flutter, or terminal tools. Repeat against a checkable predicate while the session remains active. For long waits, use the host's supervised process or job mechanism; do not assume Cursor `/loop`, cloud agents, or cloud wake chains exist.

Watch PRs and CI with the available source-control tools, normally `gh`. Re-read the merge-ready head before acting. Do not assume a built-in babysit skill exists.
