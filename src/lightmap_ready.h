#pragma once

/// True once `map::generate_lightmap` has completed at least once this run.
///
/// This is a GLOBAL one-way latch, deliberately NOT per-z-level.
///
/// Sprite lighting classification needs to know "does a lightmap exist yet". The obvious
/// refinement — a per-z-level flag — is WRONG here:
/// * `generate_lightmap` only runs for the levels in `dirty_seen_cache_levels`
///   (`src/map_cache.cpp`, around line 909), while `caches[]` is pre-allocated for every
///   one of `OVERMAP_LAYERS` levels.
/// * So a z-level that gets DRAWN without ever being LIT — lower floors reached via
///   `draw_below`, or the first frame after a z-transition — would report "not ready" and
///   be classified `unlit`, i.e. FULL-BRIGHT albedo. That is precisely the fail-bright
///   defect this work exists to remove, reintroduced on a narrower trigger.
/// * The question this flag actually answers is only "is there a world with lighting at
///   all yet" — the main menu, character creation, and the frames before the very first
///   `generate_lightmap`. That is genuinely global.
///
/// It never needs resetting on world unload: quitting to the main menu destroys `g`, and
/// the classification's separate `world_present` term (which requires `g != nullptr`)
/// already returns `unlit` there.
auto lightmap_ever_generated() noexcept -> bool;

/// Latch the flag. Idempotent; called by `map::generate_lightmap_worker`.
auto mark_lightmap_generated() noexcept -> void;
