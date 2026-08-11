# Character creation — wizard flow

Reference: `UI_designs/10_charcreation_genotype.png`.

## Brief (as given)

- Char creation fills the screen: panel at **98%**, borders slightly inset so they read.
- POINTS pool choice becomes **large side-by-side clickable cards** in a flexbox.
  Card top = slot for main content (placeholder rune for now); card bottom = a slot
  built for the option's info text.
- **Large arrow components** at far left and right showing the shortcut and the
  previous/next step's name; clickable as well as key-driven.
- **Bottom of screen**: the shortcut and label for leaving creation to the main menu.

## Decisions taken with the user

- **Character preview becomes RmlUi content**, rendered to a texture and referenced from
  the document, rather than a GPU sprite drawn underneath it. This is what makes 98%
  possible at all (see "Why the preview blocks 98%").
- **The step strip stays, restyled as a wizard**: "steps in a process", not tabs. The
  arrows are the primary navigation; the strip tells you where you are in the sequence.

## Why the preview blocks 98%

Frame composition in `composite_swapchain_pass_b` (`sdl_render_frame.cpp:1468-1507`):

1. blit `world_ldr_target()`
2. blit `ui_target()`  ← the avatar sprite lands here
3. `end_pass` overlay → hud particles → `rmlui_layer::render_in_pass()`

So RmlUi draws **last, on top**, and the avatar is composited **before** it. A panel
background is one opaque rect and no child can unpaint it, which is why panels are
currently 72% with the preview living in the clear strip outside them
(`nc_prepare_preview`). At 98% that strip is gone and PROFESSION / TRAITS / BIONICS /
OVERVIEW lose the avatar again — the exact regression fixed earlier this session.

### Chosen mechanism

`rmlui_render_interface` keeps `p->textures` as `handle -> SDL_GPUTexture*`, and
`LoadTexture` already intercepts synthetic sources (`?proc:<variant>` →
`gen_runic_frame` → `upload_rgba`). The avatar is *already* a GPU texture because
`ui_composite_target::texture()` is one. So:

1. Give the avatar its own `ui_composite_target` sized to the portrait box.
2. Draw the avatar into that target instead of into the shared `ui_target`.
3. Add a synthetic `?avatar` source to `LoadTexture` that registers that target's
   existing texture and returns its handle.

No CPU readback and no resample, so the sprite stays pixel-crisp.

**Two correctness traps, both must be handled:**

- **Ownership.** `ReleaseTexture` destroys the `SDL_GPUTexture` it finds in the map. The
  avatar target is owned by `render_state`, not by the interface, so it needs a
  *borrowed* set that `ReleaseTexture` skips — otherwise RmlUi frees a texture the
  renderer still owns.
- **Invalidation.** RmlUi caches by source string. The target is recreated on resize, so
  the handle must be invalidated and re-resolved, or the document keeps a dangling
  texture. A generation counter in the source (`?avatar:<gen>`) is the cheapest fix that
  reuses the existing string-keyed cache.

## Sequencing (no intermediate broken state)

POINTS, SCENARIO, STATS and SKILLS have **no** preview, so they can go to 98%
immediately. The four preview tabs stay at 72% until the texture path lands. That
delivers the flow first, as asked, without a window where 4 tabs render a black box.

### Phase 1 — shell, on the four preview-free tabs

- `.nc-panel` 98% with inset visible border.
- Wizard step strip replacing the tab strip: same 8 entries, styled as ordered steps
  (done / current / upcoming), not as tabs.
- Arrow components left/right: shortcut + destination step name, `data-event-click`.
- Bottom bar: exit shortcut + "main menu".
- Shortcuts come from `ctxt.get_desc()` for the real bound actions (`NEXT_TAB`,
  `PREV_TAB`, `QUIT`) — NOT invented Q/E, so rebinding and localisation stay correct.

### Phase 2 — POINTS cards

- 1-3 cards in a flexbox. **Count is data-driven, not always 3**:
  `CHARACTER_POINT_POOLS` yields 1 (`multi_pool`), 2 (`no_freeform`) or 3 (default), so
  the row must look deliberate at any of the three.
- Card = rune slot (top) + info slot (bottom). Placeholder art via the existing
  `?proc:runic-icon:<size>:<seed>:<hex>` mechanism.
- Click selects the pool (mouse parity with CONFIRM). Pattern already proven in
  `main_menu.cpp:823` — `c.BindEventCallback("on_item", …)` mutating loop state.
- Title + subtitle treatment from the reference ("character creation" / ": choose
  pool :").

### Phase 3 — preview to texture

- Dedicated `ui_composite_target`, `?avatar:<gen>` source, borrowed-texture set,
  resize invalidation.
- Then the remaining four tabs go to 98% and the portrait becomes placeable content.
- Delete `nc_prepare_preview`'s panel-edge coupling (`nc_panel_pct`,
  `nc_panel_right_col`) — that whole mechanism exists only to dodge occlusion and
  becomes dead once the preview composites on top.

## Landed

All three phases are in. Commits: `70f9a47dca` (shell + cards), plus the preview-texture
work.

### Discoveries worth keeping

- **Clicks need `SELECT`, not `ANY_INPUT`.** `MOUSE_LEFT` binds to action id `SELECT`
  (`keybindings.json:1175`). An unregistered mouse action resolves to `CATA_ERROR`, and
  `input.cpp:894-897` `continue`s on that **before** the `registered_any_input` check at
  `:912` — so `ANY_INPUT` cannot rescue a mouse event; only registering the action it
  maps to can. Without it the click callback fires but `handle_input()` never returns,
  leaving the loop parked until an unrelated keypress, which then gets hijacked into a
  step change. All eight loops now register `SELECT`, and `nc_nav` is cleared BEFORE
  polling since the callback fires during `handle_input`.
  - Corollary, unfixed and outside this change: **`main_menu.cpp` registers `ANY_INPUT`
    but not `SELECT`, so the main menu's own `data-event-click` handlers are very likely
    dead.** Consistent with the "mouse navigation unverified" note in
    `plans/charcreation-visual-overhaul.md`.
- **`{{fg2}}` / `{{fg3}}` do not exist.** `theme.json` defines only `fg`, `fg0`, `fg4`,
  and `ui_theme::substitute_tokens` turns an unknown token into `#ff00ffff`. So
  `list_vehicles.rcss:85`, `gamemode_defense.rcss:50` and `veh_interact.rcss:119,125`
  render **magenta** text today. One-token fixes each, unrelated to this work.
- **`calc_character_pos()` is screen-space.** It returns `pos.x * termx_pixels + …`
  (~1640 px here). Feeding it to the avatar pass, which projects at `AVATAR_TARGET_PX`,
  puts every sprite outside the target and renders a blank portrait. The sprite is
  centred in the target instead.
- **`scale-none`, not `contain`, for the portrait decorator.** The target is 512 square
  but the sprite occupies only a tile's worth in the middle, so `contain` scaled mostly
  empty margin down to the box and drew the avatar about quarter size. `scale-none`
  centres at native resolution and crops the margin — which also keeps the sprite
  pixel-exact, the whole reason for sampling the target rather than resampling it.
- **`character_preview_rmlui_enabled()` defaults OFF.** It only ever gated the old
  floating chrome document, so testing it to decide whether to draw the curses border
  drew that border on top of the new in-document portrait box. The condition is the
  CREATOR's mode (`newcharacter_rmlui_enabled()`).
- **The old chrome document is deleted**, not left dormant: `data/gui/character_preview.{rml,rcss}`
  plus `cp_rml_open/close/position` and their data model. It existed to frame a sprite
  that no longer draws there.
- `position: absolute` on `.nc-portrait` resolves against `.nc-panel`, not `.nc-stage` —
  the same behaviour that broke the balance scale earlier. Here that IS the wanted
  placement (panel top-right), so it is relied on deliberately and documented as such.

## Acceptance

- [x] All 8 steps at 98%; portrait visible on the 4 that own one (verified on TRAITS,
      OVERVIEW and PROFESSION — PROFESSION previously showed an oversized clipped smear
      and is now clean).
- [x] Cards clickable and arrows clickable — verified with REAL mouse events, not
      screenshots: clicking the arrow advanced POINTS→SCENARIO and updated both arrows;
      clicking the Freeform card took cursor+chosen and switched the budget line.
- [x] Shortcut labels come from actual bindings (`[TAB]`, `[BACKTAB]`, `[ESC]`), capped
      to one key via `get_desc(action, 1)` — QUIT otherwise prints "ESC, q, Q or SPACE".
- [x] `[newchar],[traits]`: 565 assertions, 142 cases, exit 0. `nc_scale` untouched.
- [x] Build clean; no new warnings in touched files.

### Not yet verified

- STATS, SKILLS, SCENARIO, BIONICS were not re-captured after the 98% change (POINTS,
  TRAITS, PROFESSION, OVERVIEW were).
- Not re-checked at 170x48 (1366x768-class) since the shell landed. The portrait box is
  a fixed 208dp inside a 98% panel, so it no longer depends on the panel-vs-box
  clearance the old geometry sweep was about — but the claim is untested at that size.
- Portrait zoom (`zoom_in`/`zoom_out`) past the target's 512 square will clip rather
  than scale, since the decorator is `scale-none`. Untested.
