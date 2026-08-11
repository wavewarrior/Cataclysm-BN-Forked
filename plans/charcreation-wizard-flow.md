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

## Acceptance

- All 8 tabs at 98% with the preview visible on the 4 that own one.
- Cards clickable; arrows clickable and key-driven; bottom bar exits to main menu.
- Shortcut labels reflect actual bindings.
- Existing `[newchar]` tests still pass; `nc_scale` geometry untouched.
- Verified by capture per tab, at the default size and at 170x48 (1366x768-class), per
  the range in `plans/charcreation-visual-overhaul.md`.
