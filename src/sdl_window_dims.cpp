#include "sdl_window_dims.h"

#include <algorithm>
#include <optional>
#include <cmath>

#include "cached_options.h"  // use_tiles, use_tiles_overmap, tile_iso
#include "cata_tiles.h"      // tilecontext, overmap_tilecontext, get_tile_width/height
#include "cursesport.h"      // cata_cursesport::WINDOW, projected_window_width/height, get_scaling_factor
#include "game.h"            // g
#include "input.h"           // input_context, input_event, coordinate
#include "options.h"         // get_option
#include "output.h"          // get_terminal_width/height
#include "point.h"           // point, point_bub_ms, tripoint_bub_ms, point_rel_ms
#include "cuboid_rectangle.h" // inclusive_rectangle
#include "sdl_font.h"        // Font::width/height
#include "sdl_display.h"     // display_context g_display, fontwidth, fontheight
#include "sdl_input.h"       // sdl_input::last_mouse_px

#include <SDL3/SDL.h> // SDL_GetTicks, SDL_GetMouseState (used unguarded below)

//***********************************
// Projected window dimensions      *
//***********************************

int projected_window_width()
{
    return get_option<int>( "TERMINAL_X" ) * fontwidth;
}

int projected_window_height()
{
    return get_option<int>( "TERMINAL_Y" ) * fontheight;
}

//***********************************
// Terminal dimensions               *
//***********************************

int get_terminal_width()
{
    return g_display.TERMINAL_WIDTH;
}

int get_terminal_height()
{
    return g_display.TERMINAL_HEIGHT;
}

//***********************************
// Scaling factor                    *
//***********************************

int get_scaling_factor()
{
    return g_display.scaling_factor;
}

//***********************************
// Window / font sizing              *
//***********************************

namespace
{

auto get_window_dimensions( const catacurses::window &win,
                            point pos, point size ) -> window_dimensions
{
    window_dimensions dim;
    if( g && win == g->w_terrain ) {
        // tiles might have different dimensions than standard font
        dim.scaled_font_size.x = tilecontext->get_tile_width();
        dim.scaled_font_size.y = tilecontext->get_tile_height();
    } else if( g_display.map_font && g && win == g->w_terrain ) {
        // map font (if any) might differ from standard font
        dim.scaled_font_size.x = g_display.map_font->width;
        dim.scaled_font_size.y = g_display.map_font->height;
    } else if( g_display.overmap_font && g && win == g->w_overmap ) {
        // tiles-only fork: the overmap always renders via the tile path
        dim.scaled_font_size.x = overmap_tilecontext->get_tile_width();
        dim.scaled_font_size.y = overmap_tilecontext->get_tile_height();
    } else {
        dim.scaled_font_size.x = fontwidth;
        dim.scaled_font_size.y = fontheight;
    }

    // multiplied by the user's specified scaling factor regardless of whether tiles are in use
    dim.scaled_font_size *= get_scaling_factor();

    if( win ) {
        cata_cursesport::WINDOW *const pwin = win.get<cata_cursesport::WINDOW>();
        dim.window_pos_cell = pwin->pos;
        dim.window_size_cell.x = pwin->width;
        dim.window_size_cell.y = pwin->height;
    } else {
        dim.window_pos_cell = pos;
        dim.window_size_cell = size;
    }

    // the window position is *always* in standard font dimensions!
    dim.window_pos_pixel = point( dim.window_pos_cell.x * fontwidth,
                                  dim.window_pos_cell.y * fontheight );
    // But the size of the window is in the font dimensions of the window.
    dim.window_size_pixel.x = dim.window_size_cell.x * dim.scaled_font_size.x;
    dim.window_size_pixel.y = dim.window_size_cell.y * dim.scaled_font_size.y;

    return dim;
}

} // namespace

window_dimensions get_window_dimensions( const catacurses::window &win )
{
    return get_window_dimensions( win, point_zero, point_zero );
}

window_dimensions get_window_dimensions( point pos, point size )
{
    return get_window_dimensions( {}, pos, size );
}

auto get_sdl_window_size() -> point
{
    return point( std::max( 1, g_display.WindowWidth / g_display.scaling_factor ),
    std::max( 1, g_display.WindowHeight / g_display.scaling_factor ) );
}

auto get_sdl_font_size() -> point
{
    return point( fontwidth, fontheight );
}

//***********************************
// Mouse coordinate mapping          *
//***********************************

std::optional<tripoint_bub_ms> input_context::get_coordinates(
    const catacurses::window &capture_win_ )
{
    if( !coordinate_input_received ) {
        return std::nullopt;
    }

    const catacurses::window &capture_win = capture_win_ ? capture_win_ : g->w_terrain;
    const window_dimensions dim = get_window_dimensions( capture_win );

    const int &fw = dim.scaled_font_size.x;
    const int &fh = dim.scaled_font_size.y;
    point win_min = dim.window_pos_pixel;
    point win_size = dim.window_size_pixel;
    const point win_max = win_min + win_size;

    // Translate mouse coordinates to map coordinates based on tile size
    // Check if click is within bounds of the window we care about
    const inclusive_rectangle<point> win_bounds( win_min, win_max );
    if( !win_bounds.contains( coordinate ) ) {
        return std::nullopt;
    }

    point_bub_ms view_offset;
    if( capture_win == g->w_terrain ) {
        view_offset = g->ter_view_p.xy();
    }

    const point screen_pos = coordinate - win_min;
    point_bub_ms p;
    if( tile_iso ) {
        const float win_mid_x = win_min.x + win_size.x / 2.0f;
        const float win_mid_y = -win_min.y + win_size.y / 2.0f;
        const int screen_col = std::round( ( screen_pos.x - win_mid_x ) / ( fw / 2.0 ) );
        const int screen_row = std::round( ( screen_pos.y - win_mid_y ) / ( fw / 4.0 ) );
        const point_rel_ms selected( ( screen_col - screen_row ) / 2, ( screen_row + screen_col ) / 2 );
        p = view_offset + selected;
    } else {
        const point_rel_ms selected( screen_pos.x / fw, screen_pos.y / fh );
        p = view_offset + selected - dim.window_size_cell / 2;
    }

    return tripoint_bub_ms( p, g->get_levz() );
}

auto aim_angle_from_pixel( point pixel, const tripoint_bub_ms &src )
-> std::optional<units::angle>
{
    if( !tilecontext ) { return std::nullopt; }
const auto o  = tilecontext->get_tile_map_origin().raw();
const auto op = tilecontext->get_drawing_pixel_offset();
const auto tw = std::max( 1, tilecontext->get_tile_width() );
    const auto th = std::max( 1, tilecontext->get_tile_height() );
    // Same formula as sdl_render_frame.cpp (cursor_light_emitter pixel→world conversion)
    const auto wx = ( pixel.x - static_cast<double>( op.x ) ) / tw + o.x;
    const auto wy = ( pixel.y - static_cast<double>( op.y ) ) / th + o.y;
    const auto dx = wx - ( src.x() + 0.5 );
    const auto dy = wy - ( src.y() + 0.5 );
    if( std::hypot( dx, dy ) < 0.01 ) { return std::nullopt; }
    return units::atan2( dy, dx );
}

auto input_context::get_aim_angle_to_src( const tripoint_bub_ms &src ) const
-> std::optional<units::angle>
{
    if( !coordinate_input_received ) { return std::nullopt; }
return aim_angle_from_pixel( coordinate, src );
}

// These four were wrapped in `#ifdef TILES` and so compiled to their stub branches:
// the build never defines TILES (it was dropped with add_definitions(-DTILES) — see
// the note at the top of cursesport.cpp, which stripped its own guard for the same
// reason), and this was the last file still testing it. The consequences were all
// silent: is_rmb_held() was hard-false, so RMB hold-to-aim could never observe the
// press and the targeting UI behaved like a toggle; get_sdl_ticks() was hard-zero,
// so the throw charge meter never advanced; and get_tracked_mouse_pos() was
// hard-(0,0), which is why the aim crosshair was never seen on screen.

auto is_rmb_held() -> bool
{
    // NOT SDL_GetMouseState: its button mask is only populated while a window holds
    // mouse focus, and inside the ranged-targeting modal loop it reads as "no buttons
    // down". sdl_input tracks the physical state from the event stream instead.
    return sdl_input::rmb_down();
}

auto get_sdl_ticks() -> uint64_t
{
    return static_cast<uint64_t>( SDL_GetTicks() );
}

auto get_sdl_mouse_pos() -> point
{
    float mx = 0.0f, my = 0.0f;
    SDL_GetMouseState( &mx, &my );
    return point( static_cast<int>( mx ), static_cast<int>( my ) );
}

auto get_tracked_mouse_pos() -> point
{
    // The persistent record sdl_input keeps of the last SDL motion event. NOT
    // g_display.last_input.mouse_pos: CheckMessages resets last_input at the top of
    // every call and every keyboard event reassigns it, so that field reads (0, 0)
    // as soon as the player touches a key — useless inside a keyboard-driven loop,
    // which is exactly where the aim crosshair needs it.
    return sdl_input::last_mouse_px();
}

auto get_aim_mouse_pos() -> point
{
    // SDL only fills in the live position while a window holds mouse focus, and
    // inside the ranged-targeting modal loop it comes back (0, 0) — which would
    // pin every aim overlay to the map's top-left corner. Fall back to the last
    // motion event sdl_input recorded, which survives that loop.
    const auto live = get_sdl_mouse_pos();
    return live != point_zero ? live : get_tracked_mouse_pos();
}
