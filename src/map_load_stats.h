#pragma once

/// Instrumentation for the one map-load cost the player can actually feel:
/// `map::loadn()` finding no mapbuffer data for a submap the reality bubble is
/// about to need, and running mapgen for its whole overmap tile synchronously.
///
/// That is the "row of map loads in view" case — it happens inside `map::shift()`,
/// between frames, while the player is walking. The paced lazy-border preloader is
/// supposed to have generated those overmap tiles turns earlier and off-screen, so
/// a non-zero count over a walk means the preloader is losing the race, not that
/// the renderer is at fault. `game::update_map()` reports the per-shift count in
/// its `[shift][perf]` line.
namespace map_load_stats
{

/// Record one synchronous generation. Called only from `map::loadn()`.
auto note_sync_generated() -> void;
/// Zero the counter and return what it held, so one shift's count is attributed
/// to that shift and nothing else.
auto take_sync_generated() -> unsigned;

} // namespace map_load_stats
