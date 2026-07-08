#include "sdl_framebuffer.h"
#include "sdl_display.h"   // g_display, display_context
#include "sdl_font.h"      // Font_Ptr

#include <algorithm>
#include <array>
#include <vector>

#include "cursesport.h"
#include "game.h"
#include "game_ui.h"
#include "output.h"
#include "point.h"

using cata_cursesport::curseline;
using cata_cursesport::cursecell;

// ---------------------------------------------------------------------------
// Per-family self-validating cache
// ---------------------------------------------------------------------------
namespace
{

struct family_cache {
    std::vector<curseline> cells;
    int font_width = 0;
    int font_height = 0;
    int font_scale = 0;
    bool has_signature = false;
};

std::array<family_cache, static_cast<size_t>( window_family::max )> per_family_caches;

/// Determine which framebuffer section each family uses and dispatch resize.
auto cache_for_family( window_family family ) -> family_cache &
{
    return per_family_caches[static_cast<size_t>( family )];
}

} // namespace

void cache_initialize_all( int term_height, int term_width,
                           int over_height, int over_width,
                           int terr_height, int terr_width )
{
    // The terminal cache uses the terminal dimensions; terrain and overmap
    // use the oversized grid so they can be indexed by map_font/overmap_font
    // coordinates relative to the terminal origin.
    struct { window_family family; int h; int w; } const init[] = {
        { window_family::terminal, term_height, term_width },
        {
            window_family::terrain,
            std::max( { term_height, over_height, terr_height } ),
            std::max( { term_width, over_width, terr_width } )
        },
        {
            window_family::overmap,
            std::max( { term_height, over_height, terr_height } ),
            std::max( { term_width, over_width, terr_width } )
        },
    };

    for( const auto &e : init ) {
        auto &fc = cache_for_family( e.family );
        fc.has_signature = false;
        fc.font_width = 0;
        fc.font_height = 0;
        fc.font_scale = 0;
        fc.cells.resize( e.h );
        for( auto &line : fc.cells ) {
            line.chars.assign( e.w, cursecell( "" ) );
        }
    }
}

void cache_invalidate_all()
{
    for( auto &fc : per_family_caches ) {
        fc.has_signature = false;
    }
}

auto cache_should_skip( window_family family, int row, int col,
                        const cursecell &cell,
                        int font_width, int font_height,
                        int font_scale ) -> bool
{
    auto &fc = cache_for_family( family );

    // If the font/scale signature has changed (or was never set), every cell
    // is a forced redraw.  Seal the new signature on first access.
    if( !fc.has_signature ) {
        fc.font_width = font_width;
        fc.font_height = font_height;
        fc.font_scale = font_scale;
        fc.has_signature = true;
        return false;  // force redraw
    }
    if( fc.font_width != font_width || fc.font_height != font_height ||
        fc.font_scale != font_scale ) {
        // font/scale changed — force everything; update signature
        fc.font_width = font_width;
        fc.font_height = font_height;
        fc.font_scale = font_scale;
        return false;
    }

    // Bounds check
    if( row >= static_cast<int>( fc.cells.size() ) ||
        col >= static_cast<int>( fc.cells[row].chars.size() ) ) {
        return false;
    }

    return fc.cells[row].chars[col] == cell;
}

void cache_update( window_family family, int row, int col,
                   const cursecell &cell )
{
    auto &fc = cache_for_family( family );
    if( row < static_cast<int>( fc.cells.size() ) &&
        col < static_cast<int>( fc.cells[row].chars.size() ) ) {
        fc.cells[row].chars[col] = cell;
    }
}

// ---------------------------------------------------------------------------
void cache_invalidate_region( window_family family, point p, int width, int height )
{
    auto &fc = cache_for_family( family );
    for( int j = 0, fby = p.y; j < height && fby < static_cast<int>( fc.cells.size() ); j++, fby++ ) {
        if( p.x < static_cast<int>( fc.cells[fby].chars.size() ) ) {
            int w = std::min( width, static_cast<int>( fc.cells[fby].chars.size() ) - p.x );
            std::fill_n( fc.cells[fby].chars.begin() + p.x, w, cursecell( "" ) );
        }
    }
}

// ---------------------------------------------------------------------------

static Font_Ptr &font = g_display.font;
static Font_Ptr &map_font = g_display.map_font;
static Font_Ptr &overmap_font = g_display.overmap_font;

static bool &need_invalidate_framebuffers = g_display.need_invalidate_framebuffers;

void reinitialize_framebuffer( const bool force_invalidate )
{
    static int prev_height = -1;
    static int prev_width = -1;
    const int new_height = std::max( { TERMY, OVERMAP_WINDOW_HEIGHT, TERRAIN_WINDOW_HEIGHT } );
    const int new_width = std::max( { TERMX, OVERMAP_WINDOW_WIDTH, TERRAIN_WINDOW_WIDTH } );
    if( new_height != prev_height || new_width != prev_width ) {
        prev_height = new_height;
        prev_width = new_width;
        for( auto &fc : per_family_caches ) {
            fc.has_signature = false;
            fc.cells.resize( new_height );
            for( auto &line : fc.cells ) {
                line.chars.assign( new_width, cursecell( "" ) );
            }
        }
    } else if( force_invalidate || need_invalidate_framebuffers ) {
        need_invalidate_framebuffers = false;
        cache_invalidate_all();
    }
}

void invalidate_framebuffer_proportion( cata_cursesport::WINDOW *win )
{
    const int oversized_width = std::max( TERMX, std::max( OVERMAP_WINDOW_WIDTH,
                                          TERRAIN_WINDOW_WIDTH ) );
    const int oversized_height = std::max( TERMY, std::max( OVERMAP_WINDOW_HEIGHT,
                                           TERRAIN_WINDOW_HEIGHT ) );

    if( oversized_height == 0 || oversized_width == 0 ) {
        return;
    }
    if( !g || win == nullptr ) {
        return;
    }
    if( win == g->w_overmap.get() || win == g->w_terrain.get() ) {
        return;
    }

    const point termpixel( win->pos.x * font->width, win->pos.y * font->height );
    const int termpixel_x2 = termpixel.x + win->width * font->width - 1;
    const int termpixel_y2 = termpixel.y + win->height * font->height - 1;

    if( map_font != nullptr && map_font->width != 0 && map_font->height != 0 ) {
        const int mapfont_x = termpixel.x / map_font->width;
        const int mapfont_y = termpixel.y / map_font->height;
        const int mapfont_x2 = std::min( termpixel_x2 / map_font->width, oversized_width - 1 );
        const int mapfont_y2 = std::min( termpixel_y2 / map_font->height, oversized_height - 1 );
        const int mapfont_width = mapfont_x2 - mapfont_x + 1;
        const int mapfont_height = mapfont_y2 - mapfont_y + 1;
        cache_invalidate_region( window_family::terrain, point( mapfont_x, mapfont_y ),
                                 mapfont_width, mapfont_height );
    }

    if( overmap_font != nullptr && overmap_font->width != 0 && overmap_font->height != 0 ) {
        const int overmapfont_x = termpixel.x / overmap_font->width;
        const int overmapfont_y = termpixel.y / overmap_font->height;
        const int overmapfont_x2 = std::min( termpixel_x2 / overmap_font->width, oversized_width - 1 );
        const int overmapfont_y2 = std::min( termpixel_y2 / overmap_font->height,
                                             oversized_height - 1 );
        const int overmapfont_width = overmapfont_x2 - overmapfont_x + 1;
        const int overmapfont_height = overmapfont_y2 - overmapfont_y + 1;
        cache_invalidate_region( window_family::overmap, point( overmapfont_x, overmapfont_y ),
                                 overmapfont_width, overmapfont_height );
    }
}

void cata_cursesport::handle_additional_window_clear( WINDOW *win )
{
    if( !g ) {
        return;
    }
    if( win == g->w_terrain.get() ) {
        auto &fc = cache_for_family( window_family::terrain );
        for( auto &line : fc.cells ) {
            std::fill_n( line.chars.begin(), line.chars.size(), cursecell( "" ) );
        }
    }
    if( win == g->w_overmap.get() ) {
        auto &fc = cache_for_family( window_family::overmap );
        for( auto &line : fc.cells ) {
            std::fill_n( line.chars.begin(), line.chars.size(), cursecell( "" ) );
        }
    }
}
