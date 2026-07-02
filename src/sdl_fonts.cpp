#include "sdltiles.h" // IWYU pragma: associated  — tilecontext, overmap_tilecontext, windowsPalette
#include "sdl_fonts.h"

#include <algorithm>
#include <memory>
#include <string>

#include "cached_options.h"  // use_tiles, use_tiles_overmap
#include "cata_tiles.h"      // cata_tiles::get_tile_width/height
#include "catacharset.h"     // UTF8_getch, utf32_to_utf8
#include "game_ui.h"         // to_map_font_dim_*, from_map_font_dimension
#include "sdl_font.h"        // Font, FontFallbackList
#include "sdl_geometry.h"    // GeometryRenderer
#include "sdl_display.h"     // display_context, g_display, fontwidth, fontheight
#include "sdl_wrappers.h"    // SDL_Renderer_Ptr, GeometryRenderer_Ptr
#include "wcwidth.h"         // mk_wcwidth

// ---------------------------------------------------------------------------
// Font dimension helpers — used by the to/from functions below
// ---------------------------------------------------------------------------

static auto map_font_width() -> int
{
    if( tilecontext ) {
    return tilecontext->get_tile_width();
    }
    return ( g_display.map_font ? g_display.map_font.get() : g_display.font.get() )->width;
}

static auto map_font_height() -> int
{
    if( tilecontext ) {
    return tilecontext->get_tile_height();
    }
    return ( g_display.map_font ? g_display.map_font.get() : g_display.font.get() )->height;
}

static auto overmap_font_width() -> int
{
    if( overmap_tilecontext ) {
    return overmap_tilecontext->get_tile_width();
    }
    return ( g_display.overmap_font ? g_display.overmap_font.get() : g_display.font.get() )->width;
}

static auto overmap_font_height() -> int
{
    if( overmap_tilecontext ) {
    return overmap_tilecontext->get_tile_height();
    }
    return ( g_display.overmap_font ? g_display.overmap_font.get() : g_display.font.get() )->height;
}

// ---------------------------------------------------------------------------
// Text drawing
// ---------------------------------------------------------------------------

auto draw_string( Font &font,
                  const SDL_Renderer_Ptr &renderer,
                  const GeometryRenderer_Ptr &geometry,
                  const std::string &str,
                  point p,
                  const unsigned char color ) -> point
{
    const char *cstr = str.c_str();
    int len = str.length();
    while( len > 0 ) {
        const uint32_t ch32 = UTF8_getch( &cstr, &len );
        const std::string ch = utf32_to_utf8( ch32 );
        font.OutputChar( renderer, geometry, ch, p, color );
        p.x += mk_wcwidth( ch32 ) * font.width;
    }
    return p;
}

// ---------------------------------------------------------------------------
// Font dimension conversions (declared in game_ui.h)
// ---------------------------------------------------------------------------

void to_map_font_dim_width( int &w )
{
    w = ( w * fontwidth ) / map_font_width();
}

void to_map_font_dim_height( int &h )
{
    h = ( h * fontheight ) / map_font_height();
}

void to_map_font_dimension( int &w, int &h )
{
    to_map_font_dim_width( w );
    to_map_font_dim_height( h );
}

void from_map_font_dimension( int &w, int &h )
{
    w = ( w * map_font_width() + fontwidth - 1 ) / fontwidth;
    h = ( h * map_font_height() + fontheight - 1 ) / fontheight;
}

void to_overmap_font_dimension( int &w, int &h )
{
    w = ( w * fontwidth ) / overmap_font_width();
    h = ( h * fontheight ) / overmap_font_height();
}
