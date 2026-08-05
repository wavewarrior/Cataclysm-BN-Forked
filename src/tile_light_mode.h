#pragma once

/// How a sprite's final colour is composed from its albedo, its per-instance
/// tint and the GPU radiance buffers.
///
/// The value is carried to the GPU in `lighting::sprite_instance::light_mode`
/// as a `float`, so it crosses the vertex/fragment boundary through a varying.
/// The shader therefore compares it BY BAND — `> 1.5` is `memory`, `> 0.5` is
/// `gpu_lit`, anything else is `unlit` — and NEVER for equality: a float that
/// has been through interpolation hardware cannot be relied upon to still be
/// exactly 0, 1 or 2.
enum class sprite_light_mode : int {
    /// `albedo x tint`, with no radiance term at all. UI, fonts, the overmap,
    /// the main menu, flat overlays, rain, and world sprites drawn before any
    /// lightmap exists.
    unlit = 0,
    /// `albedo x tint x gpu_total` — world tiles the player can see.
    gpu_lit = 1,
    /// `lerp( gpu_lit_result, memory_result, frontier_cov )` — remembered
    /// terrain, fading from live lighting into the desaturated memory look.
    memory = 2,
};

/// Everything `classify_tile_light` needs, as plain data, so the decision is
/// testable without a live `game` or `map`.
struct tile_light_query {
    /// Sprite is a standalone entity preview / UI element (character-creation
    /// preview, vehicle preview), not a world tile.
    bool as_independent_entity = false;
    /// Sprite belongs to the overmap view: an unlit map with no SDF, sun, sky
    /// or emitters.
    bool is_overmap = false;
    /// A game exists (`g != nullptr`) AND the tile is inside the loaded map
    /// bubble (`map::inbounds`).
    bool world_present = false;
    /// A lightmap has been generated at least once this run. See
    /// `src/lightmap_ready.h`; it is a GLOBAL latch, not per-z.
    bool lighting_ready = false;
    /// Tile is drawn from map memory (`lit_level::MEMORIZED`).
    bool memorized = false;
};

/// Pick the composite mode for one sprite.
///
/// This exists to replace a rule that used a per-tile BRIGHTNESS test —
/// `level_cache::lm[idx].max() > 0.001` — as a proxy for "the world is ready".
/// A genuinely pitch-dark but VISIBLE tile matched that test's false branch,
/// so it rendered at full unlit albedo and its GPU shadows were discarded:
/// shadows were structurally unrepresentable exactly where they should have
/// been deepest. The hard 0.001 threshold also made the transition pop rather
/// than ramp. Darkness is now `gpu_lit` with a dark radiance, and only the
/// genuine ABSENCE of a lightmap is `unlit`.
constexpr auto classify_tile_light( const tile_light_query &q ) -> sprite_light_mode
{
    if( q.as_independent_entity || q.is_overmap || !q.world_present ) {
    return sprite_light_mode::unlit;
}
if( !q.lighting_ready ) {
    return sprite_light_mode::unlit;
}
if( q.memorized ) {
    return sprite_light_mode::memory;
}
return sprite_light_mode::gpu_lit;
}
