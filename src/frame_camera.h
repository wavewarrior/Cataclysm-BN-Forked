#pragma once
#ifndef CATA_SRC_FRAME_CAMERA_H
#    define CATA_SRC_FRAME_CAMERA_H

#    include <algorithm>
#    include <cmath>
#    include <cstdint>
#    include <utility>

// The frame's camera, derived ONCE per frame (T3, decision 2 of #58). Two
// derivations carried side by side because they legitimately differ:
//  A. the integer tile clip rect the lighting build uses, from the player
//     position and the view offset (subtile term omitted, extents from the
//     tile context's screentile counts) — recomputed every frame, never stale.
//  B. the float camera offset, pixel offset and tile size the overlay and
//     world passes use, from the tile context's drawing pixel offset and map
//     origin — written by `cata_tiles::draw()`, so stale on a refresh with no
//     preceding redraw.
// `camera_mode` replaces the `cam_w == 0` convention. Mode and B's validity are
// INDEPENDENT axes: the windowed driver runs whole_bubble with the game and the
// tile context both present (issue #128), so B is fully populated there.
//
// Where B is unterivable the published values are the ones the unwritten
// `s_emo` mirror holds: zero offsets and `tile_px = 32`, the
// `EmitterOverlayState` default initializer (`sdl_lighting_devui.h:19`). The
// ticket and the #58 addendum both say 0; the code says 32, and the readers
// that consume the value with no fallback (the rain pass's `tile_pixel_size`,
// the sound-wave pass's `tile_px_inv`) observe 32 today, so 32 is the
// no-change value. Consumers that do fall back (glow, shafts, dust, overlays)
// keep their own 32.
//
// Pure by construction: no windowing, GPU or game includes, so `cata_test-tiles`
// can assert the derivation without a GPU, a window or a world (ADR-0003).

/// How the frame's view relates to the screen; names the branch the
/// `cam_w == 0` convention expressed implicitly.
enum class camera_mode : std::uint8_t {
    /// The clip rect has positive extent: the view is cropped to the screen.
    cropped,
    /// No usable clip rect: the lighting build covers the whole reality bubble.
    whole_bubble,
};

/// The raw per-frame facts the camera is derived from, read by the builder at
/// the points the two derivations read them today. `pos_x`/`pos_y` are the
/// `POSX`/`POSY` globals (mutable, set from the terrain window size at
/// `game_misc.cpp:420-421`), passed in so the derivation stays pure.
struct frame_camera_source {
    /// `g != nullptr` — the gate both derivations used. Blocks that
    /// additionally require an active world keep their own guard.
    bool have_game = false;
    int player_x = 0;
    int player_y = 0;
    int player_z = 0;
    int view_offset_x = 0;
    int view_offset_y = 0;
    /// `POSX`, `POSY`.
    int pos_x = 0;
    int pos_y = 0;
    /// `tilecontext != nullptr`. When false, every tile-context fact below is
    /// unused and the camera falls back.
    bool have_tile_context = false;
    /// `get_tile_width()` / `get_tile_height()` — RAW, unclamped: the play-area
    /// mask multiplies by these, the cursor inverse map clamps them to 1.
    int tile_w = 0;
    int tile_h = 0;
    /// `get_screentile_width()` / `get_screentile_height()`.
    int screentile_w = 0;
    int screentile_h = 0;
    /// `get_tile_map_origin().raw()` — stale between redraws, by design.
    int map_origin_x = 0;
    int map_origin_y = 0;
    /// `get_drawing_pixel_offset()`.
    int draw_off_px_x = 0;
    int draw_off_px_y = 0;
    /// The drawable size (`ctx.swapchain_w/h`), which is what the mirror's
    /// `screen_w`/`screen_h` carried.
    int screen_w = 0;
    int screen_h = 0;
};

/// The frame's camera. A value; copyable; the whole frame agrees on it.
struct frame_camera {
    /// What an underivable B publishes: the unwritten mirror's startup default.
    static constexpr float k_startup_tile_px = 32.0f;

    camera_mode mode = camera_mode::whole_bubble;

    // ── A: the integer tile clip rect (lighting build) ─────────────────────
    /// Bubble-local tile coords (the SDF grid's space). The no-tile-context
    /// case keeps today's `-1, -1, 0, 0`.
    int cam_x0 = -1;
    int cam_y0 = -1;
    int cam_w = 0;
    int cam_h = 0;

    // ── B: the float camera the overlay and world passes use ───────────────
    /// False exactly where `assemble_light_inputs` skipped the mirror write
    /// today; the `pub_*` accessors then return the startup values.
    bool float_camera_valid = false;
    /// `op_x / tile_px - map_origin_x`: the tile-space camera origin, defined
    /// so that `pixel = (tile + cam_off) * tile_px`. Already bakes in the
    /// pixel offset — adding `op` again double-counts it (issue #127).
    float cam_off_x = 0.0f;
    float cam_off_y = 0.0f;
    /// The drawing pixel offset (sidebar / HUD bar), logical pixels.
    float op_x = 0.0f;
    float op_y = 0.0f;
    /// The tile context's WIDTH, or 32 when unterivable. Every consumer that
    /// treats tiles as square reads this, as they read the mirror's `tile_px`.
    float tile_px = k_startup_tile_px;

    // ── Facts consumers re-derived from the tile context today ─────────────
    /// Whether a tile context existed this frame: the play-area mask and the
    /// rain spawn loop gate on it alone, not on B's validity.
    bool have_tile_context = false;
    /// Raw tile width/height (unclamped): the HUD play-area mask.
    int tile_w = 0;
    int tile_h = 0;
    int screentile_w = 0;
    int screentile_h = 0;
    /// The stale tile-context origin: the rain spawn origin, the F4 panel.
    int map_origin_x = 0;
    int map_origin_y = 0;
    /// Integer pixel offset, for the F4 publication.
    int draw_off_px_x = 0;
    int draw_off_px_y = 0;
    /// The viewer tile (raw fact; the published mirror values are `pub_*`).
    int player_x = 0;
    int player_y = 0;
    int player_z = 0;
    /// The drawable size.
    int screen_w = 0;
    int screen_h = 0;
    /// The GPU stamp's tile size (`frame_light_inputs::tile_pixel_size`): the
    /// tile context's width, or 32 with no tile context. Kept distinct from
    /// `tile_px` so the stamp keeps its own value on a zero-width tileset.
    float tile_stamp_px = k_startup_tile_px;

    // Published values: exactly what `s_emo` holds today. On an invalid B the
    // mirror was never written, and the reachable frames of that kind are
    // startup frames, where it still holds its static zero state — hence the
    // zeros and the 32 default below, not the raw facts.
    auto pub_cam_off_x() const -> float { return float_camera_valid ? cam_off_x : 0.0f; }
    auto pub_cam_off_y() const -> float { return float_camera_valid ? cam_off_y : 0.0f; }
    auto pub_op_x() const -> float { return float_camera_valid ? op_x : 0.0f; }
    auto pub_op_y() const -> float { return float_camera_valid ? op_y : 0.0f; }
    auto pub_tile_px() const -> float {
        // What the mirror's `tile_px` holds on every reachable frame: the
        // tile width when B is valid; the startup default otherwise (the
        // debug-zero branch writes the raw stamp, but every consumer of an
        // invalid B either applies its own `> 0 ? : 32` fallback — mapping
        // both 0 and 32 to 32 — or is gated out; the publication site still
        // writes the stamp to the mirror itself, so the F4 panel is
        // byte-identical regardless).
        return float_camera_valid ? tile_px : k_startup_tile_px;
    }
    auto pub_player_x() const -> int { return float_camera_valid ? player_x : 0; }
    auto pub_player_y() const -> int { return float_camera_valid ? player_y : 0; }
    auto pub_player_z() const -> int { return float_camera_valid ? player_z : 0; }

    /// The tile size the cursor/hover inverse map divides by: today's
    /// `std::max( 1, get_tile_width() )` at the two call sites.
    auto cursor_tile_w() const -> int { return std::max( 1, tile_w ); }
    auto cursor_tile_h() const -> int { return std::max( 1, tile_h ); }

    /// A world tile point to logical screen pixels: the single-application
    /// transform the overlays, the glow pass and the shafts use. Takes the
    /// site's own tile size because each site keeps its `> 0 ? : 32` fallback
    /// around the published value; reads the PUBLISHED offsets, i.e. exactly
    /// what the mirror handed these passes.
    auto screen_pos_of( float tile_x, float tile_y, float tp ) const -> std::pair<float, float> {
        return {( tile_x + pub_cam_off_x() ) * tp, ( tile_y + pub_cam_off_y() ) * tp};
    }
    /// The sound-pulse transform, WHICH DOUBLE-COUNTS THE PIXEL OFFSET:
    /// `cam_off` already contains `op / tile_px`, and this adds `op` again
    /// (issue #127). Preserved verbatim by the strict no-behaviour-change rule
    /// and kept apart from `screen_pos_of` so the difference is visible in a
    /// name and pinned by a test.
    auto sound_pulse_pos_of( float tile_x, float tile_y, float tp ) const -> std::pair<float, float> {
        const std::pair<float, float> p = screen_pos_of( tile_x, tile_y, tp );
        return {p.first + pub_op_x(), p.second + pub_op_y()};
    }
    /// The world tile under a logical-pixel screen point, via the stale tile
    /// context — the inverse of the drawing transform, as the cursor light and
    /// the dev-light hover computed it today.
    auto world_tile_at( float screen_x, float screen_y ) const -> std::pair<float, float>;

    auto operator==( const frame_camera & ) const -> bool = default;
};

/// The frame's camera, from the day's raw facts. Pure.
inline auto derive_frame_camera( const frame_camera_source& src ) -> frame_camera
{
    frame_camera cam;

    // A — the clip rect, transcribed from the old `build_lighting` block.
    if( src.have_tile_context ) {
        if( src.have_game ) {
            const float cx = static_cast<float>( src.player_x + src.view_offset_x );
            const float cy = static_cast<float>( src.player_y + src.view_offset_y );
            cam.cam_x0 = static_cast<int>( std::floor( cx ) ) - src.pos_x;
            cam.cam_y0 = static_cast<int>( std::floor( cy ) ) - src.pos_y;
        } else {
            cam.cam_x0 = src.map_origin_x;
            cam.cam_y0 = src.map_origin_y;
        }
        cam.cam_w = src.screentile_w;
        cam.cam_h = src.screentile_h;
    }
    // The consumer's own rule (`frame_build.cpp:136`), not "tile context
    // present": the windowed driver reaches whole_bubble WITH a tile context.
    cam.mode = ( cam.cam_w > 0 && cam.cam_h > 0 ) ? camera_mode::cropped : camera_mode::whole_bubble;

    // Facts the tile context owns, carried whenever it exists — the play-area
    // mask, the rain origin and the cursor inverse map gate on the tile
    // context alone, not on B's validity.
    cam.have_tile_context = src.have_tile_context;
    if( src.have_tile_context ) {
        cam.tile_w = src.tile_w;
        cam.tile_h = src.tile_h;
        cam.screentile_w = src.screentile_w;
        cam.screentile_h = src.screentile_h;
        cam.map_origin_x = src.map_origin_x;
        cam.map_origin_y = src.map_origin_y;
        cam.draw_off_px_x = src.draw_off_px_x;
        cam.draw_off_px_y = src.draw_off_px_y;
    }
    cam.player_x = src.player_x;
    cam.player_y = src.player_y;
    cam.player_z = src.player_z;
    cam.screen_w = src.screen_w;
    cam.screen_h = src.screen_h;

    // B — the float camera, transcribed from `assemble_light_inputs`: the same
    // gate (`g && tilecontext && tile_pixel_size > 0`), the same formula. The
    // gate's `tile_pixel_size` is the tile context's width, or 32 with no tile
    // context — the value the GPU stamp takes (`tile_stamp_px`), kept distinct
    // from the camera's `tile_px` so neither replaces the other.
    cam.tile_stamp_px =
        src.have_tile_context ? static_cast<float>( src.tile_w ) : frame_camera::k_startup_tile_px;
    if( src.have_game && src.have_tile_context && cam.tile_stamp_px > 0.0f ) {
        cam.float_camera_valid = true;
        cam.cam_off_x =
            static_cast<float>( src.draw_off_px_x ) / cam.tile_stamp_px
            - static_cast<float>( src.map_origin_x );
        cam.cam_off_y =
            static_cast<float>( src.draw_off_px_y ) / cam.tile_stamp_px
            - static_cast<float>( src.map_origin_y );
        cam.op_x = static_cast<float>( src.draw_off_px_x );
        cam.op_y = static_cast<float>( src.draw_off_px_y );
        cam.tile_px = cam.tile_stamp_px;
    }
    return cam;
}

inline auto frame_camera::world_tile_at( float screen_x, float screen_y ) const
-> std::pair<float, float>
{
    return {( screen_x - static_cast<float>( draw_off_px_x ) ) / static_cast<float>( cursor_tile_w() )
            + static_cast<float>( map_origin_x ),
            ( screen_y - static_cast<float>( draw_off_px_y ) ) / static_cast<float>( cursor_tile_h() )
            + static_cast<float>( map_origin_y )};
}

#endif // CATA_SRC_FRAME_CAMERA_H
