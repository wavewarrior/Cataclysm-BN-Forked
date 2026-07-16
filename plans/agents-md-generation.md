# Repository Guidelines Generation Plan

## Status: ✅ COMPLETED

Generated `AGENTS.md` at the project root — a concise, practical guide for AI assistants working in the Cataclysm-BN Forked codebase. Merged scout findings with existing AGENTS.md (binding hand-crafted rules).

## Result

- **Lines**: 262 (+82 new, -1 title rename)
- **Sections**: 14 total (6 new, 8 preserved verbatim)
- **Preserved sections**: Byte-identical to original (verified with `diff`)
- **New sections**: Project Overview, Architecture & Data Flow, Key Directories, Important Files, Runtime/Tooling Preferences, Testing & QA

## Corrections During Execution

- Fixed `npc.cpp` line count (plan said 114882, actual is 3707; `character.cpp` at 11713 is actual largest)
- Removed nonexistent `src/factories/` directory reference (factories live flat in `src/`)
- All cross-reference paths verified with `glob`