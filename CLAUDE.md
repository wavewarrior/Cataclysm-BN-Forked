# Cataclysm-BN-Forked

At the start of every session in this codebase, read:

```
/Users/nigel.fierens/.claude/projects/-Users-nigel-fierens-dev-projects-Cataclysm-BN-Forked/memory/project_rendering_pipeline.md
```

This file contains the full SDL_GPU rendering architecture (phase 2i-B): refresh_display pass order, queue lifecycle, draw_sprite_at GPU/legacy paths, atlas upload, font queues, sprite_instance layout, and D3D12 invariants. Do not re-discover this from the source files each session — read the memory file first.

Also read `src/lighting/CLAUDE.md` for module-level detail (file map, queue lifecycle, known gotchas).

## Agent skills

### Issue tracker

Issues and specs live as local markdown files under `.scratch/<feature>/`. See `docs/agents/issue-tracker.md`.

### Triage labels

Default five canonical roles (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`), recorded as `Status:` lines. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: root `GLOSSARY.md` + `docs/adr/`, created lazily. See `docs/agents/domain.md`.
