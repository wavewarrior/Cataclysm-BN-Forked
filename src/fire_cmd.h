#pragma once

#include "coordinates.h"

#include <vector>

/**
 * B4 — Ranged combat stage split.
 *
 * Stage 1 (done): resolve_aim_line() — deterministic, no side effects, no RNG.
 * Stage 2 (deferred): resolve_hit() — dispersion roll + impact tile + damage.
 * Stage 3 (deferred): emit_visuals() — animation + sound + messages.
 *
 * Stage 2/3 require access to anonymous-namespace helpers in ranged.cpp
 * (calculate_dispersion, make_gun_projectile, etc.) AND must faithfully apply
 * the per-shot modifier block from fire_gun (enchantments, sling damage,
 * str_draw, NORANGEDCRIT).  Deferred until that block can be extracted cleanly.
 *
 * ── Invariant ────────────────────────────────────────────────────────────────
 * resolve_aim_line() MUST contain zero calls to rng(), one_in(), roll_remainder(),
 * dispersion.roll(), or any other stochastic function.  Verified by tests.
 */

/// Deterministic LOS path from source to the aimed-at target tile.
using fire_trajectory = std::vector<tripoint_bub_ms>;

/// Compute the deterministic aim line from source toward target.
/// Pure: no RNG, no game state mutations, safe to call from any thread.
auto resolve_aim_line(
    const tripoint_bub_ms& source,
    const tripoint_bub_ms& target ) -> fire_trajectory; // *NOPAD*
