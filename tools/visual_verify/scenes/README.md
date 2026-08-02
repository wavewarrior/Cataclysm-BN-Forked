# Lua scene builders

Scene-construction scripts for `vv.py` scenarios that need specific in-game
state (a night-time lighting stress scene, a creature load, an isolated single
light). They live here rather than in `data/lua/lib/` because they are test
fixtures, not shipped content.

## Why Lua instead of debug-menu keystrokes

Driving the debug menu by synthetic keystrokes is unreliable: any key that lands
outside the menu you think is open is interpreted as a game command. In practice
that started combat, opened the action menu, and left the game in a targeting
cursor mode - each failure costing a full launch cycle to diagnose. A scene
script is one short console line, is version controlled, and reports what it did
to `debug.log`.

## Running one

Copy the script into the install's data dir, then `require` it in-game:

```sh
cp tools/visual_verify/scenes/lightscene.lua out/install/<preset>/data/lua/lib/
```

In game: **ESC -> `c` (Debug Menu) -> `l` (Lua console) -> RETURN -> type
`require("lib.lightscene")` -> CTRL+S**.

- `dofile` / `loadfile` / `load` / `loadstring` are all set to nil by
  `cata::detail::forbid_unsafe_functions` (`src/catalua_bindings.cpp:391`), so
  `require` plus the `lib.*` searcher (`src/catalua_loader.cpp:111`, resolving to
  `<datadir>/lua/lib/<name>.lua`) is the only way to run a script from a file.
- The console's `EDIT` action is RETURN; inside `STRING_EDITOR` RETURN inserts a
  newline, so confirm with **CTRL+S** (`data/raw/keybindings/keybindings.json`).
- `require` caches in `package.loaded`, so a second `require` in one session is a
  no-op. That is fine - each `vv` run is a fresh launch.

## Reading the results

Each script writes a `*_RESULT` line via `gdebug.log_info`. Those only reach
`config/debug.log` when the Lua debug class is enabled - set `DEBUGLOG_CL_LUA`
to `true` in `config/options.json`, otherwise the lines are filtered out and a
successful run looks like a silent failure.

## Scripts

| Script | Purpose |
|---|---|
| `lightscene.lua` | 120 permanent lights on a 4-tile grid spanning the viewport. The lighting-heavy scene. |
| `lightmobs.lua` | 12 `mon_cow` for creature load. Passive on purpose - see the header comment. |
| `lightone.lua` | Exactly one light, for isolating per-emitter rendering behaviour. |

## Measuring lighting at all

Night is mandatory. `sprite.frag` resolves `combined = max(tint.rgb, gpu_total)`,
so in daylight the CPU lightmap tint dominates and GPU lighting changes are
invisible. Set the clock first (Debug Menu -> `m` Map -> `t` Change time ->
`h` hour), and pair every measurement with a same-state null capture.

Zoom matters nearly as much. At the default zoom a tile is ~8 screen px, so a
light pool tens of tiles wide is a handful of pixels and reads as "black with
dots"; `z` zooms in, `Z` out.

Whole-frame impressions are unreliable for this - a sparse field of bright
sprites aliases badly under downscaling. Prefer numbers: `vv.py stats --rect` on
the scene core plus an unlit control region, or a radial profile around a single
emitter.
