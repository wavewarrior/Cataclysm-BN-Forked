# Ranged Combat Optimization Pass

## Findings (from 4 parallel audits)

### Critical — Gameplay Bug
1. **Double overpenetration penalty** (ballistics.cpp:~700-707): thrown items that fail embed get `traj_len *= modifier` applied TWICE in sequence (once for damage+range, once range-only on embed fail). Second call passes `0.0` (int→bool implicit) instead of `false`. Unintended multiplicative stacking.

### High — Performance
2. **ray_cast_angle per-step hypot** (map.cpp:876): `std::hypot()` (transcendental sqrt) called every DDA step. Used per-frame by aiming UI, per-sound by audio propagation, per-shot by ballistics. Replace with integer squared-distance comparison.
3. **Sprite lookup unconditional** (ballistics.cpp:393-407): `custom_bullet_sprite` string concatenation + tileset lookup runs on every `projectile_attack` call even when `do_animation` is false. Guard with animation check.

### Medium — Code Quality + Performance
4. **Bodypart string lookups per-hit** (creature.cpp:1139-1158): `bodypart_str_id("head")` etc. resolved at runtime per projectile hit. Cache as `static const` like the `ammo_effect_*` identifiers at file scope.
5. **furn_t/ter_t copied by value** (map_bash.cpp:1557-1560): `map::shoot` copies full struct objects per-tile. Should be const references.
6. **Duplicate furniture/terrain bash logic** (map_bash.cpp:1562-1663): ~50 lines copy-pasted between furniture and terrain `ranged_bash_info` paths.
7. **Box2D/non-Box2D creature detection duplication** (ballistics.cpp): Friendly-fire skip, digging skip, monster cast duplicated across both `#ifdef` branches. Extract shared helper.
8. **Burst-invariant recomputation** (ranged.cpp:987-989, 1021-1025): `projectile_trajectories`, `grouped_shot_hits`, shot_count/angle rebuilt every burst iteration.

### Low — Cleanup
9. **Dead declaration** (monster.h:434-437): `deal_projectile_attack_internal` declared, never defined.
10. **Lambda inside DDA loop** (ballistics.cpp:~660): `apply_overpenetration_penalty` lambda could be hoisted above loop.
11. **Vehicle-rotation terrain-hit** duplicates ad-hoc penetration logic instead of reusing the penalty lambda.

## Execution Order
1. Fix overpenetration bug (correctness)
2. ray_cast_angle squared-distance optimization (highest perf impact)
3. Guard sprite lookup + cache bodypart lookups (per-shot perf)
4. map::shoot ref fix + bash dedup (code quality)
5. Deduplicate Box2D/non-Box2D + hoist burst invariants (cleanup)
6. Remove dead code, hoist lambda (trivial)
