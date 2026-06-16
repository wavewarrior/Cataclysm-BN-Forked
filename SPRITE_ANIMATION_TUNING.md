# Sprite Animation Tuning (F4 "Animation" tab)

Live dev knobs for the sprite-animation system (movement bob/slide, idle sway,
hit reaction, attack lunge). Added 2026-06-16. See `plans/sprite_animation_plan.md`
for the underlying system design.

## Where things live

- **`animation_tuning` struct** — `src/creature.h` (~line 242). Holds every tunable:
  the original opt-out bools + amplitude/duration knobs, plus 13 fine-grained shape
  knobs (added 2026-06-16) whose **defaults equal the former hardcoded literals** in
  `update_animation_state`.
- **The math** — `update_animation_state()` in `src/creature.cpp` (~line 189). All
  oscillation rates, tilts, slide/burst durations now read from the tuning struct
  instead of inline literals.
- **Per-frame tuning source** — `cata_tiles::refresh_anim_frame()` in
  `src/cata_tiles.cpp` (~line 2989). Pulls each field from the in-game options once
  per frame into the file-scope `s_anim_tuning`.
- **F4 panel** — `draw_animation_tab()` in `src/sdl_lighting_devui.cpp`. Sliders +
  effect on/off checkboxes + a "live override" checkbox.

## The override gate (important)

`refresh_anim_frame()` re-reads the options **every frame**, so naive F4 edits to
`s_anim_tuning` would be clobbered before the next draw. The fix:

- `static bool s_anim_override` (cata_tiles.cpp) + accessors `debug_anim_tuning()`
  / `debug_anim_override()` (declared in `cata_tiles.h`, defined in `cata_tiles.cpp`).
- When override is **on**: `refresh_anim_frame()` forces `anim_enabled_ = true` and
  **returns early** — the F4 panel owns `s_anim_tuning`, edits persist.
- When override is **off**: options are pulled each frame as before. F4 sliders still
  edit the struct but get overwritten next frame (so they mirror the options, read-
  only-ish). Defaults make override-off behave **exactly** as pre-2026-06-16.

This cross-TU accessor pattern (free funcs over a file-scope static, forward-declared
type in the header) mirrors the existing `creatures_require_animation()` wrapper.

## Knob list (fine-grained, default = old literal)

| Field | Default | Controls |
|---|---|---|
| `move_slide_dur` | 0.15 s | single-tile slide travel time |
| `move_bob_freq` | 20.9 | bob/tilt oscillation rate |
| `move_tilt_deg` | 3° | peak move tilt |
| `idle_freq` | 1.6 | idle sway rate |
| `idle_tilt_deg` | 1.2° | peak idle lean |
| `idle_vbob_mult` | 0.9 | foot-plant vertical lift, × idle_sway |
| `hit_burst_total` | 0.4 s | total length of a multi-hit burst |
| `hit_flash_frac` | 0.6 | flash duration as fraction of hit duration |
| `hit_freq` | 15.7 | kick/tilt rate |
| `hit_tilt_deg` | 5° | peak hit tilt |
| `attack_freq` | 15.7 | lunge rate |
| `attack_ranged_mult` | **-0.5** | ranged amplitude × (negative = recoil back) |
| `attack_tilt_melee_deg` | -3° | peak melee lunge tilt |
| `attack_tilt_ranged_deg` | 2° | peak ranged recoil tilt |

`attack_ranged_mult` carries the sign that used to be the leading `-` on the ranged
amplitude — keep it negative for recoil-away, not lunge-toward.

## Adding a new knob (recipe)

1. Add the field to `animation_tuning` (creature.h) with its current literal as default.
2. Replace the literal in `update_animation_state` (creature.cpp) with `t.<field>`.
3. If it should follow the options: add a `get_option<...>` line in `refresh_anim_frame`
   (cata_tiles.cpp) **and** a matching option in the options registry. Otherwise it is
   F4-only (defaults at startup, editable when override is on).
4. Add a `dbg_slider(...)` to `draw_animation_tab` (sdl_lighting_devui.cpp).
