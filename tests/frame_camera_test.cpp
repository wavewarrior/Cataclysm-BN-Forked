#include "frame_camera.h"

#include <algorithm>
#include <catch/catch_amalgamated.hpp>
#include <cmath>
#include <utility>

using Catch::Approx;

// The camera the frame derives once (T3, ADR-0003 seam 1): where the two
// derivations agree, where they legitimately differ, and what the fallback
// publishes. No GPU, no window, no world.

namespace
{

/// A settled frame: 32x32 tiles, a 30x20 screen of them, the sidebar pixel
/// offset, and a tile-context origin one tile off the fresh clip origin on both
/// axes — the normal state of affairs, because `cata_tiles::draw()` floors
/// `center + subtile` while the clip rect omits the subtile term.
auto base_source() -> frame_camera_source
{
    frame_camera_source s;
    s.have_game = true;
    s.have_tile_context = true;
    s.player_x = 100;
    s.player_y = 200;
    s.player_z = 0;
    s.view_offset_x = 0;
    s.view_offset_y = 0;
    s.pos_x = 20;
    s.pos_y = 15;
    s.tile_w = 32;
    s.tile_h = 32;
    s.screentile_w = 30;
    s.screentile_h = 20;
    // A = (80, 185); the draw origin rounded up one tile on both axes.
    s.map_origin_x = 81;
    s.map_origin_y = 186;
    s.draw_off_px_x = 0;
    s.draw_off_px_y = 128;
    s.screen_w = 960;
    s.screen_h = 640;
    return s;
}

} // namespace

TEST_CASE( "a settled frame crops to the clip rect and carries both cameras", "[frame_camera]" )
{
    const frame_camera cam = derive_frame_camera( base_source() );
    CHECK( cam.mode == camera_mode::cropped );
    // A: floor(player + view_offset) - POS, subtile term omitted.
    CHECK( cam.cam_x0 == 80 );
    CHECK( cam.cam_y0 == 185 );
    CHECK( cam.cam_w == 30 );
    CHECK( cam.cam_h == 20 );
    // B: op / tile - origin.
    CHECK( cam.float_camera_valid );
    CHECK( cam.cam_off_x == Approx( -81.0f ) );
    CHECK( cam.cam_off_y == Approx( 128.0f / 32.0f - 186.0f ) );
    CHECK( cam.op_x == Approx( 0.0f ) );
    CHECK( cam.op_y == Approx( 128.0f ) );
    CHECK( cam.tile_px == Approx( 32.0f ) );
    CHECK( cam.tile_stamp_px == Approx( 32.0f ) );
    CHECK( cam.have_tile_context );
    CHECK( cam.map_origin_x == 81 );
    CHECK( cam.draw_off_px_y == 128 );
    CHECK( cam.screen_w == 960 );
    CHECK( cam.player_y == 200 );
    // Published values are the raw ones whenever B is valid.
    CHECK( cam.pub_cam_off_x() == Approx( cam.cam_off_x ) );
    CHECK( cam.pub_tile_px() == Approx( cam.tile_px ) );
    CHECK( cam.pub_player_z() == 0 );
}

TEST_CASE( "the derivations agree exactly when the subtile term rounds down", "[frame_camera]" )
{
    // The AC's "where they agree": with a zero subtile term the draw origin IS
    // the clip origin, and then B's offset reconstructs A exactly —
    // cam_off == op / tile - A. This is the invariant a later unification
    // would rely on, pinned for the one state where it holds.
    frame_camera_source s = base_source();
    s.map_origin_x = 80;  // == cam_x0
    s.map_origin_y = 185; // == cam_y0
    s.draw_off_px_x = 64;
    const frame_camera cam = derive_frame_camera( s );
    CHECK( cam.cam_off_x == Approx( cam.op_x / cam.tile_px - cam.cam_x0 ) );
    CHECK( cam.cam_off_y == Approx( cam.op_y / cam.tile_px - cam.cam_y0 ) );
}

TEST_CASE( "the omitted subtile term can floor the draw origin one tile back", "[frame_camera]" )
{
    // The AC's "subtile flooring". `bub_pos` and `view_offset` are integers, so
    // A's own floor is the identity; the divergence comes from the SUBTILE term
    // A omits. `cata_tiles::draw` floors `center + subtile_off` where
    // `subtile_off` is the SIGNED camera-spring residual
    // (`game_misc.cpp:633,638`): a residual in [0,1) leaves the draw origin on
    // A's tile, a negative one floors it one tile BEHIND A.
    frame_camera_source s = base_source();
    s.map_origin_x = 79; // A - 1: a negative residual floored the draw origin
    s.map_origin_y = 184;
    const frame_camera cam = derive_frame_camera( s );
    CHECK( cam.cam_x0 - cam.map_origin_x == 1 );
    CHECK( cam.cam_y0 - cam.map_origin_y == 1 );
    // B follows the origin, not the player: the camera offset moves with it.
    CHECK( cam.cam_off_x == Approx( -79.0f ) );
    CHECK( cam.cam_off_y == Approx( 128.0f / 32.0f - 184.0f ) );
    // A stayed put: it never reads the origin.
    CHECK( cam.cam_x0 == 80 );
    CHECK( cam.cam_y0 == 185 );
}

TEST_CASE( "a stale tile-context origin moves B and not A", "[frame_camera]" )
{
    // The clip rect tracks the player; the float camera tracks the origin
    // `cata_tiles::draw()` last wrote. A refresh with no preceding redraw (the
    // end-of-turn pump_events frame) leaves them pointing at different tiles,
    // and BOTH are used this frame. Pinned so a later unification is a
    // deliberate, pixel-checked decision.
    frame_camera_source s = base_source();
    s.player_x = 112; // walked east since the last draw
    const frame_camera cam = derive_frame_camera( s );
    CHECK( cam.cam_x0 == 92 );              // A followed the player
    CHECK( cam.cam_off_x == Approx( -81.0f ) ); // B did not
}

TEST_CASE( "the pixel offset enters B and not A", "[frame_camera]" )
{
    // The sidebar shifts the drawing area; the clip rect is in tile space and
    // must ignore it, while the float camera absorbs it.
    frame_camera_source s = base_source();
    s.draw_off_px_x = 160;
    const frame_camera with = derive_frame_camera( s );
    const frame_camera without = derive_frame_camera( base_source() );
    CHECK( with.cam_x0 == without.cam_x0 );
    CHECK( with.cam_y0 == without.cam_y0 );
    CHECK( with.cam_w == without.cam_w );
    CHECK( with.cam_off_x == Approx( without.cam_off_x + 160.0f / 32.0f ) );
    CHECK( with.op_x == Approx( 160.0f ) );
}

TEST_CASE( "zoom scales B and leaves A's origin alone", "[frame_camera]" )
{
    // Tile size is a divisor of the pixel offset only. The clip origin is in
    // tiles, so a zoom changes the extent (through screentile_w/h) and B, but
    // never A's origin.
    frame_camera_source s = base_source();
    s.tile_w = 16;
    s.tile_h = 16;
    s.screentile_w = 60;
    s.screentile_h = 40;
    s.draw_off_px_y = 64;
    const frame_camera cam = derive_frame_camera( s );
    CHECK( cam.cam_x0 == 80 );
    CHECK( cam.cam_y0 == 185 );
    CHECK( cam.cam_w == 60 );
    CHECK( cam.cam_h == 40 );
    CHECK( cam.tile_px == Approx( 16.0f ) );
    CHECK( cam.cam_off_y == Approx( 64.0f / 16.0f - 186.0f ) );
}

TEST_CASE( "no tile context: whole_bubble and the startup publication", "[frame_camera]" )
{
    // The main menu and the frames before the tileset loads: no clip rect, so
    // the lighting build covers the whole bubble, and the published values are
    // the ones the unwritten mirror holds — zero offsets, tile_px 32.
    frame_camera_source s = base_source();
    s.have_tile_context = false;
    const frame_camera cam = derive_frame_camera( s );
    CHECK( cam.mode == camera_mode::whole_bubble );
    CHECK( cam.cam_x0 == -1 );
    CHECK( cam.cam_y0 == -1 );
    CHECK( cam.cam_w == 0 );
    CHECK( cam.cam_h == 0 );
    CHECK_FALSE( cam.float_camera_valid );
    CHECK( cam.pub_cam_off_x() == Approx( 0.0f ) );
    CHECK( cam.pub_cam_off_y() == Approx( 0.0f ) );
    CHECK( cam.pub_op_x() == Approx( 0.0f ) );
    CHECK( cam.pub_op_y() == Approx( 0.0f ) );
    CHECK( cam.pub_tile_px() == Approx( 32.0f ) );
    // The player fields publish 0 too: the mirror's z-cull saw 0 on these
    // frames today, even though the game exists.
    CHECK( cam.pub_player_x() == 0 );
    CHECK( cam.pub_player_y() == 0 );
    CHECK( cam.pub_player_z() == 0 );
    // The tile-context facts stay zero-filled: the play-area mask and the rain
    // origin gate on the tile context and must see nothing.
    CHECK_FALSE( cam.have_tile_context );
    CHECK( cam.map_origin_x == 0 );
    CHECK( cam.draw_off_px_y == 0 );
    CHECK( cam.screentile_w == 0 );
    // The GPU stamp keeps ITS OWN fallback: 32 with no tile context, as
    // `assemble_light_inputs` computed it.
    CHECK( cam.tile_stamp_px == Approx( 32.0f ) );
}

TEST_CASE( "startup: no game and no tile context", "[frame_camera]" )
{
    // The first frames after `rs.ready()`: the publication is the static zero
    // state the mirror held. The ticket and the #58 addendum say `tile_px` 0;
    // the mirror's default initializer (`sdl_lighting_devui.h:19`) says 32,
    // and 32 is what the readers with no fallback (rain's `tile_pixel_size`,
    // the sound wave's `tile_px_inv`) actually observe, so 32 is the
    // no-change value. Pinned as a named constant so the premise cannot drift
    // again. The glow pass keeps its OWN fallback to 32 either way.
    const frame_camera cam = derive_frame_camera( frame_camera_source{} );
    CHECK( cam.mode == camera_mode::whole_bubble );
    CHECK_FALSE( cam.float_camera_valid );
    CHECK( cam.pub_cam_off_x() == Approx( 0.0f ) );
    CHECK( cam.pub_cam_off_y() == Approx( 0.0f ) );
    CHECK( cam.pub_op_x() == Approx( 0.0f ) );
    CHECK( cam.pub_op_y() == Approx( 0.0f ) );
    CHECK( cam.pub_player_x() == 0 );
    CHECK( cam.pub_player_z() == 0 );
    CHECK( cam.screen_w == 0 );
    CHECK( cam.pub_tile_px() == Approx( frame_camera::k_startup_tile_px ) );
    CHECK( frame_camera::k_startup_tile_px == Approx( 32.0f ) );
}

TEST_CASE( "whole_bubble with a tile context keeps the float camera populated", "[frame_camera]" )
{
    // The windowed bnplay driver (issue #128): the game and the tile context
    // both exist and the screentile counts are zero, so the MODE is
    // whole_bubble while B is fully written. Mode and B's validity are
    // independent axes — zeroing B on whole_bubble would change the gated
    // Trial frames, which is exactly the path the pixel gate measures.
    frame_camera_source s = base_source();
    s.screentile_w = 0;
    s.screentile_h = 0;
    const frame_camera cam = derive_frame_camera( s );
    CHECK( cam.mode == camera_mode::whole_bubble );
    CHECK( cam.float_camera_valid );
    CHECK( cam.cam_off_y == Approx( 128.0f / 32.0f - 186.0f ) );
    CHECK( cam.op_y == Approx( 128.0f ) );
    CHECK( cam.tile_px == Approx( 32.0f ) );
    CHECK( cam.map_origin_x == 81 );
    CHECK( cam.screentile_w == 0 );
}

TEST_CASE( "a zero tile width invalidates B but keeps the clip rect", "[frame_camera]" )
{
    // `assemble_light_inputs` refused to divide by a zero tile width; the clip
    // rect never divided, so it survives. The one combination where the mode
    // is cropped and B is not valid.
    frame_camera_source s = base_source();
    s.tile_w = 0;
    s.tile_h = 0;
    const frame_camera cam = derive_frame_camera( s );
    CHECK( cam.mode == camera_mode::cropped );
    CHECK( cam.cam_w == 30 );
    CHECK_FALSE( cam.float_camera_valid );
    CHECK( cam.pub_cam_off_x() == Approx( 0.0f ) );
    CHECK( cam.pub_tile_px() == Approx( 32.0f ) );
    // The GPU stamp keeps the RAW width here — 0, not the camera's 32 — as
    // the stamp did today, and the raw widths stay carried: the play-area mask
    // multiplied by 0 and collapsed, which the camera must not paper over.
    CHECK( cam.tile_stamp_px == Approx( 0.0f ) );
    CHECK( cam.tile_w == 0 );
    CHECK( cam.tile_h == 0 );
    // Clamping to 1 happens only in the cursor inverse map.
    CHECK( cam.cursor_tile_w() == 1 );
    CHECK( cam.cursor_tile_h() == 1 );
}

TEST_CASE(
    "non-square tiles: the square consumers take the width, the inverse map both",
    "[frame_camera]" )
{
    // Non-square tilesets exist. Every consumer that read the mirror's single
    // `tile_px` keeps reading the WIDTH; the cursor inverse map is the only
    // place the HEIGHT is used, and it clamps to 1.
    frame_camera_source s = base_source();
    s.tile_w = 64;
    s.tile_h = 32;
    s.draw_off_px_y = 64;
    const frame_camera cam = derive_frame_camera( s );
    CHECK( cam.tile_px == Approx( 64.0f ) );
    // B divides by the WIDTH in both axes, as the mirror did.
    CHECK( cam.cam_off_y == Approx( 64.0f / 64.0f - 186.0f ) );
    CHECK( cam.cursor_tile_w() == 64 );
    CHECK( cam.cursor_tile_h() == 32 );
    // The inverse map uses the height: 64 logical px of offset is 2 tall tiles.
    const std::pair<float, float> t = cam.world_tile_at( 0.0f, 128.0f );
    CHECK( t.first == Approx( 81.0f ) );
    CHECK( t.second == Approx( 186.0f + 2.0f ) );
}

TEST_CASE( "the cursor inverse map reproduces the two hand-written sites", "[frame_camera]" )
{
    // The cursor light and the dev-light hover computed the same expression
    // twice, from the stale origin and the raw pixel offset. Both now call
    // `world_tile_at`; this pins it against the arithmetic they used.
    const frame_camera_source s = base_source();
    const frame_camera cam = derive_frame_camera( s );
    const float msx = 437.0f;
    const float msy = 211.0f;
    const float wx =
        ( msx - static_cast<float>( s.draw_off_px_x ) ) / static_cast<float>( std::max( 1, s.tile_w ) )
        + static_cast<float>( s.map_origin_x );
    const float wy =
        ( msy - static_cast<float>( s.draw_off_px_y ) ) / static_cast<float>( std::max( 1, s.tile_h ) )
        + static_cast<float>( s.map_origin_y );
    const std::pair<float, float> t = cam.world_tile_at( msx, msy );
    CHECK( t.first == Approx( wx ) );
    CHECK( t.second == Approx( wy ) );
}

TEST_CASE( "the sound-pulse transform still adds the pixel offset on top", "[frame_camera]" )
{
    // Known inconsistency (issue #127), deliberately preserved: the pulse path
    // adds `op` to a camera offset that already contains it. The two transforms
    // are separate named helpers so the difference is asserted, not accidental.
    const frame_camera cam = derive_frame_camera( base_source() );
    const float source_x = 95.5f;
    const float source_y = 190.5f;
    const std::pair<float, float> pulse = cam.sound_pulse_pos_of( source_x, source_y, cam.tile_px );
    const std::pair<float, float> single = cam.screen_pos_of( source_x, source_y, cam.tile_px );
    // The pulse position equals the old hand-written expression ...
    const float old_x = ( source_x + cam.cam_off_x ) * cam.tile_px + cam.op_x;
    const float old_y = ( source_y + cam.cam_off_y ) * cam.tile_px + cam.op_y;
    CHECK( pulse.first == Approx( old_x ) );
    CHECK( pulse.second == Approx( old_y ) );
    // ... and is NOT the single-application transform: the double-add is really
    // present, differing by exactly the pixel offset.
    CHECK( pulse.first == Approx( single.first ) ); // op_x is 0
    CHECK( pulse.second == Approx( single.second + 128.0f ) );
}

TEST_CASE( "the camera is a value: deriving twice from one source is equal", "[frame_camera]" )
{
    // The frame's whole point: two phases cannot see two different cameras,
    // because the derivation reads only its argument.
    const frame_camera_source s = base_source();
    CHECK( derive_frame_camera( s ) == derive_frame_camera( s ) );
}
