# Dependency boundary

Spec: #201. Decision record: `docs/adr/0007-ports-for-capabilities-core-frameworks-for-foundations.md`. Vocabulary: Port, Adapter, Core framework in `GLOSSARY.md`.

## Destination

Every library the game needs for a capability sits behind a Port with one Adapter chosen by a CMake option; libraries the game builds on are pinned Core frameworks (RmlUi, Lua with sol2, SDL3 including SDL_GPU). The project shrinks without losing functionality.

## Decisions (grilling session, 2026-10-15)

- Swap time: compile time, via CMake option. Scope: every third-party library. Size is judged on three axes (shipped binary, build time and fetched deps, repo size), ranked per library.
- Core frameworks, no port: RmlUi, Lua + sol2, SDL3 + SDL_GPU (ADR-0003 stands).
- Delete: fmt (snprintf in the string formatter), SDL_ttf and the inert `font_engine` (font port over FreeType), runtime shadercross (build-time blobs; shadercross stays as build tooling, rebuild-only dev loop).
- New ports: font (FreeType), image (SDL_image), save-store (SQLite). Existing seams relocate to `src/ports/` with a clean cutover; adapters in `src/adapters/<lib>/`.
- simplexnoise: re-derive bit-identical from a public-domain reference or keep and declare the GPL-3 file.
- sol2: keep, shrink instantiation cost, success is any measured reduction.
- Small fixes: ballistics through the physics port, pin shadercross `GIT_TAG`, drop unused `zlib.h` include, Lua `DLOPEN` and README.
- Keeps: Catch2, minicoro, wdirent, pinyin data, libbacktrace, Tracy, SDL_mixer, SDL_net.
- No enforcement rule yet; a human lands it in `tools/check/**` later.

## Measurements behind the decisions

macOS RelWithDebInfo binary, 177,421,808 B; `__TEXT` 78,577,664 B; link uses `-Wl,-no_deduplicate` outside Release.

| Library | Bytes in `__TEXT` | Share |
|---|---|---|
| sol2 | 32,458,424 | 41.3% |
| game code | 22,724,336 | 28.9% |
| luna (Lua binding layer) | 2,249,732 | 2.9% |
| RmlUi | 855,124 | 1.1% |
| Box2D | 130,388 | 0.2% |
| fmt | 69,104 | 0.1% |

Shipped macOS sidecars: libdxcompiler 30.5 MB, libdxil 4.2 MB, spirv-cross 3.5 MB.

## Tickets

| # | Title | Blocked by |
|---|---|---|
| 202 | ports and adapters layout, compress and coop_transport as tracer | none |
| 203 | agent-guidelines waiver for header relocation (human) | none |
| 204 | replace fmt with snprintf | none |
| 205 | simplexnoise bit-identical re-derivation | none |
| 206 | Lua native-loading fix and README | none |
| 207 | shaders to build-time blobs, macOS | none |
| 208 | shader blobs for Linux and Windows | 207 |
| 209 | remove runtime shadercross, pin source | 207, 208 |
| 210 | physics port, Box2D adapter, ballistics | 202, 203 |
| 211 | audio port, SDL_mixer adapter | 202, 203 |
| 212 | profiling port, Tracy adapter | 202, 203 |
| 213 | curses port | 202, 203 |
| 214 | string formatter port relocation | 202, 203, 204 |
| 215 | font port over FreeType | 202 |
| 216 | image port | 202 |
| 217 | save-store port | 202 |
| 218 | sol2 size reduction | none |
| 219 | docs | 210-217 |

## Open items

- Release (identical-code-folding) sol2 share is unmeasured; #218 reports what it measures.
- #203 is a protected-path edit; lanes cannot do it, and #210-#214 stay blocked until it lands.
