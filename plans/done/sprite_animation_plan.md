# Sprite Animation System — Revised Plan (post-grill)

## Context

The repo wants Stoneshard-tier "alive" sprite juice — movement, idle breathing, hit
reaction, attack lunge, and tile-bash feedback — on the GPU tiled renderer. An earlier
draft of this file was design-grilled and found to have four collisions with shipped/in-flight
code and one foundational unit bug. This version supersedes that draft and encodes the
decisions made during the grill. Outcome target: 40fps sprite juice, gated as a config
opt-out, faithful to multiple-attacker swarm reactions.

### What the draft got wrong (corrected here)
1. **Clock.** Draft animated everything off `calendar::turn` (game time, frozen between
   frames). The renderer already has a wall-clock — `anim_time` = `fmod(SDL_GetTicks()/1000, …)`
   (sdltiles.cpp:1186), living in `debug_params` and driving the foliage-sway shader. Use
   wall-clock; store **no `time_point`** on the creature.
2. **Reinvented plumbing.** A per-sprite `sway` float is already threaded
   `draw_from_id_string → draw_sprite_at → enqueue_tile_sprite → sprite_instance.pad1 →
   sprite.vert` (cata_tiles.h:224/257). The transform threading mirrors this exactly.
3. **Idle infra exists.** `idle_animation_manager` (cata_tiles.h:695, instance
   `idle_animations`:1213) already pumps the redraw loop via `terrain_requires_animation()
   = idle_animations.enabled() && idle_animations.present()` (cata_tiles.cpp:4008).
4. **Old hit system is dual-path.** `game::draw_hit_mon`/`draw_hit_player` have a tiles
   branch **and** curses branches (`*_curses`, animation.cpp:709/739). Retire only the
   tiles branch; curses keeps its flash.

## Locked Decisions

1. **Clock** — wall-clock (`SDL_GetTicks()`), not `calendar::turn`. Draft's `*0.1f` turn
   math = frozen-between-frames bug.
2. **State location** — `animation_state` lives on `Creature` as **pure data** (floats +
   `double latched_wall` + seq counters + a small hit ring). No `SDL` include in
   `creature.h`. Lives/dies with the object → dissolves keying/cleanup (monsters have no
   stable id; position keying breaks on the move event).
3. **Latch** — render-side. Sim hooks write trigger data + bump a counter; render
   (`cata_tiles.cpp`) detects the new event and writes `latched_wall = SDL_GetTicks()/1000`
   into the struct. SDL touches the field only from render.
4. **Detection** — per-event monotonic `uint32` seq counters (no turn-stamps; avoids the
   1s turn-granularity collision).
5. **Hit reaction = bounded ring queue.** All same-turn hits land between two frames (no
   redraw in `game::monmove`, game.cpp:5526). Single-slot would show only the last attacker.
   Per-creature ring buffer of `{dir_x, dir_y, intensity, seq}`, **cap 3, drop-oldest**;
   render pops in order, per-hit duration shrinks with depth so total ≈0.4s → rapid
   directional stutter.
6. **Redraw pump** — force it at **40fps (25ms timeout)** whenever sprite-anims enabled and
   a creature is visible (or any event anim / tile-hit is decaying). Add a
   `creatures_require_animation()` predicate; add a 25ms branch to `anim_timeout`
   (handle_action.cpp:313). Perf hit accepted.
7. **Breathing** — continuous; rides the forced pump for **all visible creatures**.
8. **Movement = slide + bob, capped.** Slide only when Chebyshev `dist(from,to)==1`;
   teleport/multi-tile snap (bob only). Offset folds into the dst rect (no new primitive).
9. **Config = options only.** Master opt-out toggle + per-effect toggles + key amplitude/
   duration knobs in `options.cpp`. **No JSON file, no loader.** Hardcoded constants are the
   in-code fallback.
10. **Old system** — retire only the **tiles** branch of `draw_hit_mon`/`draw_hit_player`;
    curses paths untouched.
11. **Flash color** — victim-derived at render via `is_avatar()`: avatar hit → white,
    monster/NPC hit → red. No extra data on the hit event.

## Data Model

`src/creature.h` — pure-data struct on `Creature` (NOT serialized; transient render gloss):

```cpp
struct animation_state {
    // --- written by sim hooks ---
    float move_dir_x = 0.f, move_dir_y = 0.f;
    bool  move_slide = false;            // dist==1 gate result
    uint32 move_seq = 0;

    float attack_dir_x = 0.f, attack_dir_y = 0.f;
    bool  attack_ranged = false;
    uint32 attack_seq = 0;

    struct hit_evt { float dir_x, dir_y, intensity; uint32 seq; };
    std::array<hit_evt, 3> hit_ring{};   // bounded, drop-oldest
    uint32 hit_push = 0;                 // sim tail (monotonic)

    // --- owned by render (latch + cursor) ---
    uint32 move_latched = 0, attack_latched = 0, hit_consumed = 0;
    double move_wall = 0.0, attack_wall = 0.0, hit_wall = 0.0;
};
```

`src/cata_tiles.h` — tile bash (tiles don't move → tripoint key is safe, self-cleaning):
`std::unordered_map<tripoint_bub_ms, tile_hit_state> tile_hits_;`

## Per-Frame Compute (render-side)

Free function `update_animation_state(animation_state&, double wall_now)` in
`creature.cpp` (no SDL — caller passes the wall clock). Each frame, for every visible
creature, render:
1. Diffs `*_seq` vs `*_latched`; on a new event writes `*_wall = wall_now`, advances latch;
   for hits, pops the next `hit_ring` entry into the active slot, advances `hit_consumed`.
2. Computes outputs from `elapsed = wall_now - *_wall`: bob (`sin` decay), slide
   (`-movedir*tile*(1-t)` only if `move_slide`), breathing (`sin(wall_now*k)`, **center-
   anchored** so scaling dst doesn't drift the sprite), hit stutter (cos wobble + flash),
   attack lunge (melee fwd / ranged recoil), all → offsets/rotation/scale/tint.

## Trigger Hooks (sim writes only; symmetric for player AND monsters)

| Event | File / location | Writes |
|-------|-----------------|--------|
| Move | `character.cpp`, `monster.cpp` move path | dir, `move_slide = (chebyshev==1)`, `++move_seq` |
| Take damage | `Creature::deal_damage` / `Character::deal_damage` (character.cpp:9562) | push `hit_ring` (drop-oldest), `++hit_push`; **null-guard `source`** → flash only, no kick |
| Ranged hit | `creature.cpp:deal_projectile_attack` | same as above |
| Attack (melee) | `avatar_action.cpp`, `melee.cpp`, monster melee path | dir toward target, `attack_ranged=false`, `++attack_seq` |
| Attack (ranged) | `ranged.cpp` | dir, `attack_ranged=true`, `++attack_seq` |
| Tile bash | `map.cpp:bash` (~4494) | insert/refresh `tile_hits_[pos]` |

> Draft omitted **monster** attack-lunge hooks — wire them so monsters lunge at the player.

## Rendering Integration

- **Transform threading** mirrors the existing `sway` param: pass a small
  `sprite_transform{off_x, off_y, rot, scale, tint_rgba}` (or extend the trailing-param
  pattern) down `draw_from_id_string → draw_tile_at → draw_sprite_at → enqueue_tile_sprite`.
  Apply offset+scale to `dst_x/y/w/h`, set `rotation` + `tint_*` directly — all already on
  `sprite_instance` (64B, static_assert-locked; `pad1`=sway taken, no new field needed).
- **Flash color is victim-derived** (no new event data): at the render compute, branch on
  `creature.is_avatar()` (creature.h:207). Avatar victim → **white** flash
  (`tint_rgb = 1.0 + flash*0.5`); any other creature (monster/NPC) → **red** flash
  (`tint_r = 1.0 + flash*0.6`, `tint_g = tint_b = 1.0 - flash*0.5`). Both decay with flash→0.
  (Edge: friendly NPCs fall in the "red" bucket under this literal rule — acceptable; revisit
  only if NPC hit-feedback needs to read as ally.)
- **Rigid body** — `draw_entity_with_overlays` (cata_tiles.cpp:5973) applies the SAME
  transform to base sprite + all overlays (clothing/mutations/items).
- **Tile bash** — at the top of `draw_furniture`/`draw_terrain`, look up `tile_hits_[pos]`,
  erase if expired, else apply a decaying `sin` shake offset to the dst rect.
- **Pump** — add `creatures_require_animation()` (true while any creature anim is decaying,
  any breathing-eligible creature is visible, or any `tile_hits_` entry is live); fold into
  the `handle_action.cpp` loop's invalidate predicate and the 25ms `anim_timeout` branch.

## Risks / verify-in-design
- **Rotation pivot** — confirm `sprite.vert` rotates about sprite **center**, not top-left,
  before relying on `rotation`. If not, center the rotation in the vertex stage.
- **Scale anchor** — breathing scale must be center-anchored (offset dst by half the size
  delta) or sprites drift.
- **Slide vs lighting/FOV** — sprite drawn trailing while logically at destination; light/FOV
  sample the destination tile. Accepted (transient).
- **Monster melee hook site** — confirm `monattack` vs `monster::melee_attack` for the
  attack-lunge write.

## Implementation Order

1. `animation_state` struct + `update_animation_state()` free fn — `creature.h/.cpp`.
2. Master + per-effect options (no JSON) — `options.cpp`.
3. Move hooks (dist==1 gate) — `character.cpp`, `monster.cpp`.
4. Hit hooks (ring push, null-guard) — `character.cpp`, `creature.cpp`.
5. Attack hooks (player + monster, melee + ranged) — `avatar_action.cpp`, `melee.cpp`, `ranged.cpp`.
6. Render: latch + compute + transform threading in `draw_entity_with_overlays` and draw chain — `cata_tiles.cpp/.h`.
7. Pump: `creatures_require_animation()` + 25ms `anim_timeout` branch — `cata_tiles.cpp/.h`, `handle_action.cpp`.
8. Tile-bash tracking + shake — `cata_tiles.cpp/.h`, `map.cpp`.
9. Retire **tiles branch only** of old hit anim — `animation.cpp/.h`, `cata_tiles.cpp` (keep curses).
10. Build, tune, test.

## Verification (end-to-end, Metal build)
1. Build green; launch with the option **on**.
2. **Melee** a zombie → you lunge forward; zombie flashes **red** + wobbles away.
3. Stand among **3+ zombies**, take a turn of hits → **you flash white**, sequential directional stutter (not one hit).
4. **Move** one tile → slide+bob; blink/teleport (debug) → snaps, bob only.
5. **Stand still** → breathing oscillates smoothly (~40fps); sidebar/CPU shows the forced cadence.
6. **Bash** a wall/furniture → brief shake + flash on that tile sprite; expires cleanly.
7. Toggle option **off** → all effects gone, normal rendering.
8. Curses build → old single-frame hit flash still works (tiles retirement didn't touch it).
