#pragma once

#include "coordinates.h"

#include <vector>

/**
 * B4 — Ranged combat stage split.
 *
 * Stage 1 (done): resolve_aim_line() — deterministic, no side effects, no RNG.
 * Stage 2 (dropped): resolve_hit() — dispersion roll + impact tile + damage.
 * Stage 3 (dropped): emit_visuals() — animation + sound + messages.
 *
 * Stage 2/3 were dropped because there is no functional gap to fill: the co-op
 * client already runs fire() locally (fire_gun → projectile_attack), which handles
 * single-shot animation internally via g->draw_bullet().  Multi-shot animation also
 * runs inside fire_gun.  emit_visuals({}) cannot preview a single shot without
 * calling projectile_attack (Stage 2 itself), making it a no-op abstraction.
 * resolve_hit() would be server-only but the server runs fire_gun unchanged.
 * Adding either function violates "no single-use abstractions" (CLAUDE.md).
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
