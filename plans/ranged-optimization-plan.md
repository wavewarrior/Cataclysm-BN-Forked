# Ranged Combat Optimization Pass

## STATUS (reviewed 2026-09-14)
7 of the original 11 items landed incidentally as a side effect of `plans/done/ranged-tile-independent-plan.md`'s ballistics/ranged rework (2026-07-12) — see per-item status below. Original numbering is preserved (not renumbered) so historical item references stay valid.

## Findings (from 4 parallel audits)

### Critical — Gameplay Bug
1. **Double overpenetration penalty** — DONE. `ballistics.cpp:729` and `:747` both now call the (renamed) `apply_overpenetration_penalty` from two mutually exclusive branches; the original bug (applied twice in sequence) no longer exists.

### High — Performance
2. **ray_cast_angle per-step hypot** — DONE. `map::ray_cast_angle` (new function, `map.cpp:935-977`) uses integer/double squared-distance comparison (`ddx*ddx+ddy*ddy` vs `max_range_sq`), zero `hypot`/`sqrt` calls anywhere in the loop.
3. **Sprite lookup unconditional** — DONE. `ballistics.cpp:417`'s `if( tilecontext && do_animation )` now guards the entire `custom_bullet_sprite` lookup block.

### Medium — Code Quality + Performance
4. **Bodypart string lookups per-hit** — NOT DONE. `creature.cpp:1135-1153` still calls `bodypart_str_id("head")`/`bodypart_str_id("torso")` etc. inline at runtime on every hit-location roll, not cached as a file-scope `static const`.
5. **furn_t/ter_t copied by value** — DONE. `map::shoot` (`map_bash.cpp:1653-1657`) now holds `furn_id furn_here = furn(p);` and `const auto &furn`/`const auto &ter` const-refs into the interned `furn_t`/`ter_t`, not value copies.
6. **Duplicate furniture/terrain bash logic** — NOT DONE. `map_bash.cpp:1661-1756` still has two near-identical ~95-line blocks (`if( furn.bash.ranged )` / `if( ter.bash.ranged )`) with parallel `block_unaimed_chance`/`NO_PENETRATE_OBSTACLES`/laser-reduction logic duplicated verbatim at shifted line numbers.
7. **Box2D/non-Box2D creature detection duplication** — DONE, moot/superseded. The file now unconditionally includes `<box2d/box2d.h>` (no `#ifdef`), and creature hit detection is a single Box2D-raycast path with no parallel non-Box2D fallback branch remaining to deduplicate against — the original duplication this item targeted was deleted outright by the tile-independence rework.
8. **Burst-invariant recomputation** — DONE. `ranged.cpp:986-991`'s `shot_count`/`shot_half_angle`/`render_multishot`/`projectile_trajectories`/`grouped_shot_hits` are now declared once before the `while( curshot != shots )` burst loop (line 992); inside the loop they are only `.clear()`'d/`.reserve()`'d for reuse.

### Low — Cleanup
9. **Dead declaration** — DONE. The dead `deal_projectile_attack_internal` declaration no longer exists anywhere in `src/monster.cpp`/`src/monster.h` (removed).
10. **Lambda inside DDA loop** — DONE. `ballistics.cpp:565`'s `apply_overpenetration_penalty` lambda is declared immediately before the per-tile loop it is used in, with an explicit comment noting the closure is materialised once, not per-iteration.
11. **Vehicle-rotation terrain-hit ad-hoc penetration logic** — NOT DONE. `ballistics.cpp:674-691`'s `obstructed_by_vehicle_rotation` branch still calls `here.shoot(source, rand, proj, false)` directly and does its own ad-hoc `if( proj.impact.total_damage() <= 0 ) { ...; break; }` stopping logic, without reusing the now-hoisted `apply_overpenetration_penalty` lambda from item 10.

## Remaining open scope

Only items 4, 6, and 11 above are still open. Renumbered execution order for the remaining work:
1. Cache bodypart lookups (item 4) — per-shot perf.
2. Deduplicate furniture/terrain bash logic (item 6) — code quality.
3. Reuse the hoisted overpenetration lambda in the vehicle-rotation branch (item 11) — cleanup.
