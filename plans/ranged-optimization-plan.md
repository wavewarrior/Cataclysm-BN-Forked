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
4. **Bodypart string lookups per-hit** — DONE (2026-09-14). The hit-location roll and severity-cap logic (`creature.cpp:1132-1186`) now reference the existing extern globals `body_part_head`/`body_part_torso`/`body_part_leg_l`/`body_part_leg_r`/`body_part_arm_l`/`body_part_arm_r` (`bodypart.h:28-39`, constructed once in `bodypart.cpp`) instead of constructing `bodypart_str_id("head")` etc. inline per hit; no new statics were added, reusing the project's existing single source of truth. Verified: build clean (only pre-existing unrelated warnings on untouched lines), `[ranged],[monster],[vehicle]` = 139/141 passed, 1 skipped, 1 failed-as-expected (tagged), no new failures — identical result before and after the switch to the existing globals.
5. **furn_t/ter_t copied by value** — DONE. `map::shoot` (`map_bash.cpp:1653-1657`) now holds `furn_id furn_here = furn(p);` and `const auto &furn`/`const auto &ter` const-refs into the interned `furn_t`/`ter_t`, not value copies.
6. **Duplicate furniture/terrain bash logic** — NOT DONE, deliberately deferred. `map_bash.cpp` is merge-touched territory (`plans/merge-main-into-improvements.md:744-753` shows `map::bash` was already restructured by the in-flight merge); doing this dedup now would add conflict surface to a file the merge will revisit. Pick up after the merge lands/stabilizes.
7. **Box2D/non-Box2D creature detection duplication** — DONE, moot/superseded. The file now unconditionally includes `<box2d/box2d.h>` (no `#ifdef`), and creature hit detection is a single Box2D-raycast path with no parallel non-Box2D fallback branch remaining to deduplicate against — the original duplication this item targeted was deleted outright by the tile-independence rework.
8. **Burst-invariant recomputation** — DONE. `ranged.cpp:986-991`'s `shot_count`/`shot_half_angle`/`render_multishot`/`projectile_trajectories`/`grouped_shot_hits` are now declared once before the `while( curshot != shots )` burst loop (line 992); inside the loop they are only `.clear()`'d/`.reserve()`'d for reuse.

### Low — Cleanup
9. **Dead declaration** — DONE. The dead `deal_projectile_attack_internal` declaration no longer exists anywhere in `src/monster.cpp`/`src/monster.h` (removed).
10. **Lambda inside DDA loop** — DONE. `ballistics.cpp:565`'s `apply_overpenetration_penalty` lambda is declared immediately before the per-tile loop it is used in, with an explicit comment noting the closure is materialised once, not per-iteration.
11. **Vehicle-rotation terrain-hit ad-hoc penetration logic** — DONE (2026-09-14), with a real behavior change. `ballistics.cpp:674-695`'s `obstructed_by_vehicle_rotation` branch now tracks damage before/after `here.shoot()` and calls the hoisted `apply_overpenetration_penalty( is_projectile_modify_overpenetration )` when it drops, mirroring the two existing call sites at `:729`/`:747`; the existing stop-and-rewind logic (`traj_len = i - 1; tp = prev_point; break;`) is preserved unchanged, so the projectile still backs up one tile rather than landing inside the rotated vehicle. Previously this branch applied no overpenetration modifier at all when it hit a wall through the gap — that asymmetry with the two other terrain-hit call sites is what this item's "cleanup" framing was about, but the practical effect is that firing through a rotated-vehicle gap and hitting something now loses range/damage the same way the other two paths already did, which it did not before. **Test coverage caveat**: no test in `tests/` exercises this specific branch for a fired projectile — the only test hit for `obstructed_by_vehicle_rotation` is `ranged_aoe_test.cpp:107`, a reachability check unrelated to ballistics penetration. The 139/141 pass result for `[ranged],[monster],[vehicle]` does not confirm this branch fired during that run; it only confirms nothing else regressed.

## Remaining open scope

Item 6 only, deferred until `plans/merge-main-into-improvements.md` lands/stabilizes to avoid compounding conflicts in `map_bash.cpp`.
