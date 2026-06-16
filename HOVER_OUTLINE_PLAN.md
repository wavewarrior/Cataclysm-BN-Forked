# Hover Outline Shader (Stoneshard-style)

## Context

Colored outline around a creature on mouse hover (and around ALL visible creatures
while **Alt** is held) — like Stoneshard's interactable highlight. Scope =
monsters + NPCs + player avatar. Color = attitude-based (hostile=red, neutral=yellow,
friendly=green; player=self/cyan). Trigger = hover one creature, OR hold Alt for all.

Renderer is SDL_GPU, single instanced pass, sprites batched by atlas page
(`refresh_display` → `flush_tile_sprites`). Lighting is inline in
`data/shaders/lighting/src/sprite.frag.hlsl`. One creature draws via
`cata_tiles::draw_critter_at` → `draw_from_id_string` → `draw_sprite_at` →
`enqueue_tile_sprite` (cata_tiles.h:223).

## Approach — offset-copy silhouette + per-instance shader mask

Classic 2D outline, NOT in-shader edge dilation (which can't draw outside the
sprite quad and bleeds into atlas-packed neighbours — the known normal-Sobel
atlas-bleed problem).

1. While drawing a creature, record the start index of its sprites in the tile
   queue. After the body + ALL worn-item overlays are queued (but before the
   attitude indicator), `render_state::build_outline_ring` copies that whole range
   **8 times** with small pixel offsets (N,S,E,W + 4 diagonals) and splices the
   copies in **at the start index** — so every silhouette sits behind every real
   layer. The union of the offset copies = one composite dilated silhouette; the
   real layers cover the interior; only the outer ring shows. No per-item inner
   seams (the failure mode of drawing each layer's outline inline).
2. New per-instance flag `sprite_instance.pad2` (was unused — no struct size
   change) tells the fragment shader to skip lighting and output `tint.rgb × texel.a`.

One creature on hover = 8×(layers) extra instances (trivial). Alt-all = same per
on-screen creature. The ring correctly follows the full composited silhouette
(body + gear), since it dilates the union of all the creature's layers.

**Note:** silhouette copies are TALL sprites, so they're also picked up by the
sun shadow-caster pass — the creature's own shadow thickens slightly. Harmless;
revisit only if it reads wrong.

## Changes

### Shader — silhouette mask mode
- `sprite.vert.hlsl`: `VS_OUT` (ends at `light_pos: TEXCOORD4`) → add
  `float outline : TEXCOORD5;`; in `main` add `o.outline = s.pad2;`.
- `sprite.frag.hlsl`: mirror `float outline : TEXCOORD5;` in its `VS_OUT` input;
  right after the atlas sample (`const float4 texel = Atlas.Sample(...)`, ~532),
  early-out before all lighting:
  ```hlsl
  if (i.outline > 0.5) {
      if (texel.a < 0.35) discard;
      return float4(i.tint.rgb, texel.a * i.tint.a);
  }
  ```

### Enqueue plumbing
- `cata_tiles.h` `enqueue_tile_sprite` (223): add trailing `float outline = 0.0f`;
  set `s.pad2 = outline;` (next to `s.pad1 = sway;` at 266). Default 0 = no-op for
  every existing caller.

### Creature draw path (`cata_tiles.cpp`)
- New outer-class members: `std::optional<tripoint_bub_ms> hover_tile_;`
  `bool outline_all_ = false;` + transient `bool want_outline_ = false;`
  `SDL_Color outline_color_;`. Public `set_hover_tile(std::optional<tripoint_bub_ms>)`.
- `draw_critter_at` (5825): once creature + `attitude`/`is_player` known and the base
  sprite resolves, if `outline_all_ || (hover_tile_ && p == *hover_tile_)` set
  `want_outline_` + `outline_color_` (hostile→red, neutral→yellow, friendly→green,
  player→cyan). Set the flag just before the base-sprite `draw_from_id_string`; reset
  after (so the attitude overlay at ~5948 is not silhouetted).
- `draw_sprite_at` (4715): at the main-sprite enqueue (~4876, fg/base only), if
  `want_outline_`, loop 8 offset dirs and `enqueue_tile_sprite(..., fdst_offset, flip,
  1.0f, rotation+tilt, color.r/255, color.g/255, color.b/255, 0.0f, 0.0f, /*outline*/1.0f)`
  **before** the real enqueue. Offset = `OUTLINE_PX` (default 2, tile-scaled).
- Tuning = live **F4 "Effects" → "Hover outline"** knobs (CPU-side globals in
  `sdl_lighting_devui.{h,cpp}`, no shader cbuffer change since `debug_params` is full):
  `g_outline_enable`, `g_outline_thickness` (frac of tile width), `g_outline_alpha`,
  and per-attitude colours `g_outline_col_{hostile,neutral,friendly,self}[4]`.
  `outline_color_for` + `draw_sprite_at` read these each frame.

### Hover + Alt wiring
- Hover tile: in `game::handle_mouseview` (~2980) call
  `tilecontext->set_hover_tile(liveview_pos)`; clear with `std::nullopt` on
  `liveview.hide()` (~2991).
- Alt-all: poll in `cata_tiles::draw()` —
  `outline_all_ = SDL_GetModState() & (SDL_KMOD_LALT|SDL_KMOD_RALT)`. Shows on next
  animation-timeout redraw (sub-second; registered Alt action = optional follow-up).

## Verification (Metal)

```
cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles
```
1. Hover zombie → red; NPC → attitude color; self → cyan.
2. Mouse off creature → clears.
3. Hold Alt → all visible creatures outline; release → clears.
4. Ring sits OUTSIDE sprite, flat/unlit (constant day/night), no edge rainbow.
5. Non-hovered terrain/items/creatures unchanged (outline defaults 0).

Safety: `OUTLINE_PX=0` or never setting `want_outline_` disables; shader change is a
single early-out guarded by `i.outline > 0.5`.
