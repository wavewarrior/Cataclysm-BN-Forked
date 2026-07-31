#pragma once

// Solid-colour screen-space overlay primitives (rects, lines, polylines, rings,
// wedges) queued into the world tile pass.
//
// Why this module exists: the animation overlays in cata_tiles_anim.cpp used to
// draw through SDL_RenderLines / SDL_RenderFillRect / SDL_RenderGeometry on
// `g_display.renderer`. Since the SDL_GPU cutover that renderer belongs to a
// HIDDEN mirror window (see the note above `static SDL_Renderer_Ptr &renderer`
// in sdltiles.cpp) and nothing in the tree ever presents it, so every one of
// those draws was invisible — the hover highlight, the aim cone, the throw arc
// and the zone fills all silently drew nothing.
//
// These helpers reproduce the same primitives on the live GPU path: each shape
// is one or more `sprite_instance` quads sampling the shared 1x1 white texture
// (lighting::gpu_geometry) tinted to the requested colour, with the fragment
// shader's flat-colour flag set so world lighting never dims what is really a
// UI overlay, and so the quads are skipped by the sun-shadow caster pass.
//
// Coordinates are screen pixels in the same space cata_tiles' tile sprites use
// (whatever player_to_screen returns), so iso and non-iso both work unchanged.
// Draw order is queue order: call these after the sprites they overlay.

#include <SDL3/SDL_rect.h>

#include <span>

namespace lighting
{

/// Straight RGBA, each component in 0..1.
struct overlay_color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

/// Build an overlay_color from the 0..255 channels SDL callers already have.
auto overlay_color_from_bytes( int r, int g, int b, int a ) -> overlay_color;

/// Axis-aligned filled rectangle. The crispest primitive here — prefer it over
/// overlay_line for horizontal/vertical edges.
auto overlay_rect( const SDL_FRect &r, const overlay_color &color ) -> void;

/// Filled quad centred on `centre`, rotated clockwise about that centre.
struct overlay_quad_options {
    SDL_FPoint centre{};
    float w = 0.0f;
    float h = 0.0f;
    float rotation = 0.0f; //< radians, clockwise (screen space, y down)
    overlay_color color{};
};
auto overlay_quad( const overlay_quad_options &opts ) -> void;

/// Line segment of `thickness` px. Axis-aligned segments degrade to an exact
/// overlay_rect; everything else becomes one rotated quad.
struct overlay_line_options {
    SDL_FPoint from{};
    SDL_FPoint to{};
    float thickness = 2.0f;
    overlay_color color{};
};
auto overlay_line( const overlay_line_options &opts ) -> void;

/// Open polyline through `points` (the SDL_RenderLines equivalent).
struct overlay_polyline_options {
    std::span<const SDL_FPoint> points;
    float thickness = 2.0f;
    overlay_color color{};
};
auto overlay_polyline( const overlay_polyline_options &opts ) -> void;

/// Circle outline approximated by `segments` chords.
struct overlay_ring_options {
    SDL_FPoint centre{};
    float radius = 0.0f;
    float thickness = 2.0f;
    int segments = 28;
    overlay_color color{};
};
auto overlay_ring( const overlay_ring_options &opts ) -> void;

/// Filled circular wedge: apex at `apex`, bisector pointing along `angle`,
/// opening `half_angle` either side, reaching `radius` px.
///
/// Built from `slabs` quads stacked along the bisector rather than a triangle
/// fan about the apex: the sprite pipeline only draws quads, and a fan of
/// radial quads would overlap at the apex and blend into a bright core. Stacked
/// slabs never overlap, so a translucent wedge keeps one uniform alpha.
///
/// The cost is geometric fidelity at the two long edges: each slab is
/// axis-aligned in the rotated frame, so the wedge's flanks are a staircase
/// whose step size grows with `half_angle` and shrinks with `slabs`. A caller
/// that stitches several narrow wedges into one wide fan (the aim cone does)
/// also gets a flat chord per wedge instead of a smooth outer arc, which reads
/// as a visible sawtooth on the far boundary — raise the caller's wedge count
/// if that matters more than the draw-call saving.
struct overlay_wedge_options {
    SDL_FPoint apex{};
    float angle = 0.0f;
    float half_angle = 0.0f;
    float radius = 0.0f;
    int slabs = 24;
    overlay_color color{};
};
auto overlay_wedge( const overlay_wedge_options &opts ) -> void;

} // namespace lighting
