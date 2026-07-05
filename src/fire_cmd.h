#pragma once

#include "coordinates.h"

#include <vector>

/**
 * B4 — Ranged combat stage split.
 *
 * Stage 1: resolve_aim_line() — deterministic, no side effects, no RNG.
 *   Returns the geometric LOS path from source to target using find_clear_path().
 *   Safe for co-op client-side visual prediction: calling it with the same inputs
 *   always produces the same path on every machine.
 *
 * Stage 2 (Phase 2+): resolve_hit() — dispersion roll + impact tile + damage.
 *   Contains all RNG (dispersion.roll(), one_in(), rng(), roll_remainder()).
 *   Server-authoritative only; lag-comp (A5.3) plugs in here.
 *
 * Stage 3 (Phase 3+): emit_visuals() — animation + sound + messages along trajectory.
 *   No game state mutations; runs on both client and server.
 *
 * ── Invariant ────────────────────────────────────────────────────────────────
 * resolve_aim_line() MUST contain zero calls to rng(), one_in(), roll_remainder(),
 * dispersion.roll(), or any other stochastic function.  Verified by tests.
 */

/// Deterministic LOS path from source to the aimed-at target tile.
/// Equivalent to map::find_clear_path(source, target) — exposed here so
/// co-op clients can call it for visual bullet prediction without
/// needing a map& reference at the call site.
using fire_trajectory = std::vector<tripoint_bub_ms>;

/// Compute the deterministic aim line from source toward target.
/// Tries offset variants of Bresenham to find a clear LOS;
/// falls back to the ideal Bresenham line if none is clear.
/// Returns [first_step, …, target] — source tile is EXCLUDED (matches
/// find_clear_path() / line_to() semantics where Bresenham emits the
/// first step, not the origin).  front() is the tile adjacent to source;
/// back() == target.
/// Pure: no RNG, no game state mutations, safe to call from any thread.
auto resolve_aim_line(
    const tripoint_bub_ms& source,
    const tripoint_bub_ms& target) -> fire_trajectory; // *NOPAD*
