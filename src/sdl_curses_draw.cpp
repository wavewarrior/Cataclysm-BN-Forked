#include "sdl_framebuffer.h" // cache_should_skip, cache_update, …
#include "sdl_display.h"     // g_display, display_context, fontwidth, fontheight
#include "sdl_font.h"        // Font_Ptr

#include "avatar.h"          // g->u

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cached_options.h"
#include "cata_tiles.h"
#include "catacharset.h"
#include "color.h"
#include "cursesdef.h"
#include "cursesport.h"
#include "game.h"
#include "game_ui.h"
#include "output.h"
#include "overmap_ui.h"
#include "point.h"
#include "rml_screen.h"
#include "sdl_geometry.h"
#include "sdl_wrappers.h"
#include "lighting/rmlui_layer.h"
#include "sdltiles.h"       // ::tilecontext, ::overmap_tilecontext, ::fontwidth, …
#include "wcwidth.h"

// ---------------------------------------------------------------------------
// File-scope reference aliases for globals
// ---------------------------------------------------------------------------

static Font_Ptr &font = g_display.font;
static Font_Ptr &map_font = g_display.map_font;

static SDL_Renderer_Ptr &renderer = g_display.renderer;
static GeometryRenderer_Ptr &geometry = g_display.geometry;
static int &WindowWidth = g_display.WindowWidth;
static int &WindowHeight = g_display.WindowHeight;
static int &scaling_factor = g_display.scaling_factor;
static bool &needupdate = g_display.needupdate;
static std::weak_ptr<void> winBuffer;



// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool operator==( const cata_cursesport::WINDOW *const lhs,
                        const catacurses::window &rhs )
{
    return lhs == rhs.get();
}

static inline auto color_as_sdl( const unsigned char color ) -> const SDL_Color &
{
    return ::windowsPalette[color];
}

static inline auto window_family_of( const catacurses::window &w ) -> window_family
{
    if( g && w == g->w_terrain ) {
        return window_family::terrain;
    }
    if( g && w == g->w_overmap ) {
        return window_family::overmap;
    }
    return window_family::terminal;
}

static inline auto suppress_cell_bg( const cata_cursesport::WINDOW *win,
                                     const SDL_Color &c ) noexcept -> bool
{
    if( !win || !win->transparent_backdrop ) {
    return false;
}
return c.r == 0 && c.g == 0 && c.b == 0;
}

// ---------------------------------------------------------------------------
// draw_window — render a curses window into the SDL framebuffer
// ---------------------------------------------------------------------------

static bool draw_window( Font_Ptr &font, const catacurses::window &w, point offset )
{
    if( scaling_factor > 1 ) {
        SDL_SetRenderLogicalPresentation( renderer.get(), WindowWidth / scaling_factor,
                                          WindowHeight / scaling_factor, SDL_LOGICAL_PRESENTATION_STRETCH );
    }

    cata_cursesport::WINDOW *const win = w.get<cata_cursesport::WINDOW>();

    const window_family family = window_family_of( w );
    const int fontScale = ::tilecontext->get_tile_width();

    // clear the oversized buffer proportionally
    invalidate_framebuffer_proportion( win );

    // When this window is being drawn inside an ui_adaptor redraw_cb, its draws
    // route into the adaptor's retained GPU slice, which was just cleared. The
    // per-cell framebuffer dirty-cell skip below assumes a persistent backbuffer
    // (the removed display_buffer) keeps unchanged cells on screen — but the
    // slice has no such persistence, so unchanged cells would be dropped (e.g. a
    // navigated uilist collapsing to current+previous row). Force a FULL re-push
    // of this window's cells into the slice by bypassing the skip while routing.
    const bool slice_active = lighting::get_render_state().ready() &&
                              lighting::get_render_state().slice_routing_active();

    // Keeping track of the last drawn window to determine compatibility.
    const cata_cursesport::WINDOW *winBuffer = static_cast<cata_cursesport::WINDOW *>
        ( ::winBuffer.lock().get() );
    bool oldWinCompatible = false;
    /*
    Let's try to keep track of different windows.
    A number of windows are coexisting on the screen, so don't have to interfere.

    g->w_terrain, g->w_minimap, g->w_HP, g->w_status, g->w_status2, g->w_messages,
     g->w_location, and g->w_minimap, can be buffered if either of them was
     the previous window.

    g->w_overmap and g->w_omlegend are likewise.

    Everything else works on strict equality because there aren't yet IDs for some of them.
    */
    if( g && ( w == g->w_terrain || w == g->w_minimap ) ) {
        if( winBuffer == g->w_terrain || winBuffer == g->w_minimap ) {
            oldWinCompatible = true;
        }
    } else if( g && ( w == g->w_overmap || w == g->w_omlegend ) ) {
        if( winBuffer == g->w_overmap || winBuffer == g->w_omlegend ) {
            oldWinCompatible = true;
        }
    } else {
        if( win == winBuffer ) {
            oldWinCompatible = true;
        }
    }

    // TODO: Get this from UTF system to make sure it is exactly the kind of space we need
    static const std::string space_string = " ";

    bool update = false;
    for( int j = 0; j < win->height; j++ ) {
        if( !win->line[j].touched ) {
            continue;
        }

        const int fby = win->pos.y + j;
        update = true;
        win->line[j].touched = false;
        for( int i = 0; i < win->width; i++ ) {
            const int fbx = win->pos.x + i;

            const auto &cell = win->line[j].chars[i];

            const int drawx = offset.x + i * font->width;
            const int drawy = offset.y + j * font->height;
            if( drawx + font->width > WindowWidth || drawy + font->height > WindowHeight ) {
                // Outside of the display area, would not render anyway
                continue;
            }

            // Self-validating per-family cache: skip unchanged cells when
            // the font/scale signature matches the cache's recorded signature.
            if( !slice_active && oldWinCompatible &&
                cache_should_skip( family, fby, fbx, cell,
                                   font->width, font->height, fontScale ) ) {
                continue;
            }
            cache_update( family, fby, fbx, cell );

            if( cell.ch.empty() ) {
                continue; // second cell of a multi-cell character
            }

            // Spaces are used a lot, so this does help noticeably
            if( cell.ch == space_string ) {
                const SDL_Color bg_col = color_as_sdl( cell.BG );
                if( !suppress_cell_bg( win, bg_col ) ) {
                    geometry->rect( renderer, point( drawx, drawy ), font->width, font->height,
                                    bg_col );
                }
                continue;
            }
            const int codepoint = UTF8_getch( cell.ch );
            const catacurses::base_color FG = cell.FG;
            const catacurses::base_color BG = cell.BG;
            int cw = ( codepoint == UNKNOWN_UNICODE ) ? 1 : utf8_width( cell.ch );
            if( cw < 1 ) {
                // utf8_width() may return a negative width
                continue;
            }
            bool use_draw_ascii_lines_routine = get_option<bool>( "USE_DRAW_ASCII_LINES_ROUTINE" );
            unsigned char uc = static_cast<unsigned char>( cell.ch[0] );
            switch( codepoint ) {
                case LINE_XOXO_UNICODE:
                    uc = LINE_XOXO_C;
                    break;
                case LINE_OXOX_UNICODE:
                    uc = LINE_OXOX_C;
                    break;
                case LINE_XXOO_UNICODE:
                    uc = LINE_XXOO_C;
                    break;
                case LINE_OXXO_UNICODE:
                    uc = LINE_OXXO_C;
                    break;
                case LINE_OOXX_UNICODE:
                    uc = LINE_OOXX_C;
                    break;
                case LINE_XOOX_UNICODE:
                    uc = LINE_XOOX_C;
                    break;
                case LINE_XXXO_UNICODE:
                    uc = LINE_XXXO_C;
                    break;
                case LINE_XXOX_UNICODE:
                    uc = LINE_XXOX_C;
                    break;
                case LINE_XOXX_UNICODE:
                    uc = LINE_XOXX_C;
                    break;
                case LINE_OXXX_UNICODE:
                    uc = LINE_OXXX_C;
                    break;
                case LINE_XXXX_UNICODE:
                    uc = LINE_XXXX_C;
                    break;
                case LINE_XDXO_UNICODE:
                    uc = LINE_XDXO_C;
                    break;
                case LINE_DXOX_UNICODE:
                    uc = LINE_DXOX_C;
                    break;
                case LINE_XOXD_UNICODE:
                    uc = LINE_XOXD_C;
                    break;
                case LINE_OXDX_UNICODE:
                    uc = LINE_OXDX_C;
                    break;
                case UNKNOWN_UNICODE:
                    use_draw_ascii_lines_routine = true;
                    break;
                default:
                    use_draw_ascii_lines_routine = false;
                    break;
            }
            {
                const SDL_Color bg_col = color_as_sdl( BG );
                if( !suppress_cell_bg( win, bg_col ) ) {
                    geometry->rect( renderer, point( drawx, drawy ),
                                    font->width * cw, font->height, bg_col );
                }
            }
            if( use_draw_ascii_lines_routine ) {
                font->draw_ascii_lines( renderer, geometry, uc, point( drawx, drawy ), FG );
            } else {
                font->OutputChar( renderer, geometry, cell.ch, point( drawx, drawy ), FG );
            }
        }
    }
    win->draw = false; //We drew the window, mark it as so
    ::winBuffer = w.weak_ptr();

    return update;
}

static bool draw_window( Font_Ptr &font, const catacurses::window &w )
{
    cata_cursesport::WINDOW *const win = w.get<cata_cursesport::WINDOW>();
    // Use global font sizes here to make this independent of the
    // font used for this window.
    return draw_window( font, w, point( win->pos.x * ::fontwidth, win->pos.y * ::fontheight ) );
}

// ---------------------------------------------------------------------------
// clear_window_area
// ---------------------------------------------------------------------------

void clear_window_area( const catacurses::window &win_ )
{
    cata_cursesport::WINDOW *const win = win_.get<cata_cursesport::WINDOW>();
    geometry->rect( renderer, point( win->pos.x * ::fontwidth, win->pos.y * ::fontheight ),
                    win->width * ::fontwidth, win->height * ::fontheight, color_as_sdl( catacurses::black ) );
}

// ---------------------------------------------------------------------------
// set_window_transparent_backdrop
// ---------------------------------------------------------------------------

void cata_cursesport::set_window_transparent_backdrop( const catacurses::window &win,
        bool transparent )
{
    if( cata_cursesport::WINDOW *const w = win.get<cata_cursesport::WINDOW>() ) {
        w->transparent_backdrop = transparent;
    }
}

// ---------------------------------------------------------------------------
// curses_drawwindow — top-level dispatch for a single curses window
// ---------------------------------------------------------------------------

void cata_cursesport::curses_drawwindow( const catacurses::window &w )
{
    if( scaling_factor > 1 ) {
        SDL_SetRenderLogicalPresentation( renderer.get(), WindowWidth / scaling_factor,
                                          WindowHeight / scaling_factor, SDL_LOGICAL_PRESENTATION_STRETCH );
    }
    WINDOW *const win = w.get<WINDOW>();
    bool update = false;
    if( g && w == g->w_terrain ) {
        // color blocks overlay; drawn on top of tiles and on top of overlay strings (if any).
        color_block_overlay_container color_blocks;

        // Strings with colors do be drawn with map_font on top of tiles.
        std::multimap<point, formatted_text> overlay_strings;

        // game::w_terrain can be drawn by the ::tilecontext.
        // skip the normal drawing code for it.
        ::tilecontext->draw(
            point( win->pos.x * ::fontwidth, win->pos.y * ::fontheight ),
            g->ter_view_p,
            TERRAIN_WINDOW_TERM_WIDTH * font->width,
            TERRAIN_WINDOW_TERM_HEIGHT * font->height,
            overlay_strings,
            color_blocks );

        // color blocks overlay
        if( !color_blocks.second.empty() ) {
            SDL_BlendMode blend_mode;
            GetRenderDrawBlendMode( renderer, blend_mode ); // save the current blend mode
            SetRenderDrawBlendMode( renderer, color_blocks.first ); // set the new blend mode
            for( const auto &e : color_blocks.second ) {
                geometry->rect( renderer, e.first, ::tilecontext->get_tile_width(),
                                ::tilecontext->get_tile_height(), e.second );
            }
            SetRenderDrawBlendMode( renderer, blend_mode ); // set the old blend mode
        }

        // overlay strings
        // §7 world-text layer: when enabled, route on-map overlay text (SCT, zone
        // labels, debug overlays) through RmlUi's font engine instead of the curses
        // OutputChar path below. Cleared once here, refilled from overlay_strings.
        const bool wt_rml = world_text_rmlui_enabled();
        if( wt_rml ) {
            rmlui_layer::world_text_begin();
        }
        point prev_coord;
        int x_offset = 0;
        int alignment_offset = 0;
        for( const auto &iter : overlay_strings ) {
            const point coord = iter.first;
            const formatted_text ft = iter.second;
            const utf8_wrapper text( ft.text );

            // Strings at equal coords are displayed sequentially.
            if( coord != prev_coord ) {
                x_offset = 0;
            }

            // Calculate length of all strings in sequence to align them.
            if( x_offset == 0 ) {
                int full_text_length = 0;
                const auto range = overlay_strings.equal_range( coord );
                for( auto ri = range.first; ri != range.second; ++ri ) {
                    utf8_wrapper rt( ri->second.text );
                    full_text_length += rt.display_width();
                }

                alignment_offset = 0;
                if( ft.alignment == text_alignment::center ) {
                    alignment_offset = full_text_length / 2;
                } else if( ft.alignment == text_alignment::right ) {
                    alignment_offset = full_text_length - 1;
                }
            }

            if( wt_rml ) {
                // Emit the whole string once (GenerateString lays out per-glyph
                // advance); start pixel mirrors the curses path: window origin +
                // alignment + the entry's map-pixel coord.
                const int x0 = win->pos.x * ::fontwidth;
                const int y0 = win->pos.y * ::fontheight;
                const int x = x0 + ( x_offset - alignment_offset ) * map_font->width + coord.x;
                const int y = y0 + coord.y;
                const SDL_Color c = windowsPalette[ft.color];
                const unsigned int rgba = ( static_cast<unsigned>( c.r ) << 24 ) |
                                          ( static_cast<unsigned>( c.g ) << 16 ) |
                                          ( static_cast<unsigned>( c.b ) << 8 ) | 0xFFu;
                rmlui_layer::world_text_add( static_cast<float>( x ), static_cast<float>( y ),
                                             ft.text, rgba );
                prev_coord = coord;
                x_offset = text.display_width();
                continue;
            }

            int width = 0;
            for( size_t i = 0; i < text.size(); ++i ) {
                const int x0 = win->pos.x * ::fontwidth;
                const int y0 = win->pos.y * ::fontheight;
                const int x = x0 + ( x_offset - alignment_offset + width ) * map_font->width + coord.x;
                const int y = y0 + coord.y;

                // Clip to window bounds.
                if( x < x0 || x > x0 + ( TERRAIN_WINDOW_TERM_WIDTH - 1 ) * font->width
                    || y < y0 || y > y0 + ( TERRAIN_WINDOW_TERM_HEIGHT - 1 ) * font->height ) {
                    continue;
                }

                const uint32_t ch = text.at( i );
                const auto glyph = utf32_to_utf8( ch );
                const bool outlined_white = ft.color == catacurses::white ||
                                            ft.color == catacurses::white + 8;

                if( outlined_white ) {
                    static constexpr std::array<point, 4> outline_offsets = {
                        point_east,
                        point_north,
                        point_west,
                        point_south,
                    };
                    for( const point &offset : outline_offsets ) {
                        map_font->OutputChar( renderer, geometry, glyph,
                                              point( x + offset.x, y + offset.y ),
                                              catacurses::black );
                    }
                }

                map_font->OutputChar( renderer, geometry, glyph, point( x, y ), ft.color );
                width += mk_wcwidth( ch );
            }

            prev_coord = coord;
            x_offset = width;
        }

        cache_invalidate_region( window_family::terminal, win->pos,
                                 TERRAIN_WINDOW_TERM_WIDTH, TERRAIN_WINDOW_TERM_HEIGHT );

        update = true;
    } else if( g && w == g->w_overmap ) {
        ::overmap_tilecontext->draw_om( win->pos, overmap_ui::redraw_info.center,
                                        overmap_ui::redraw_info.blink );
        update = true;
    } else if( g && w == g->w_pixel_minimap && pixel_minimap_option ) {
        // ensure the space the minimap covers is "dirtied".
        // this is necessary when it's the only part of the sidebar being drawn
        // TODO: Figure out how to properly make the minimap code do whatever it is this does
        draw_window( font, w );

        // Make sure the entire minimap window is black before drawing.
        clear_window_area( w );
        ::tilecontext->draw_minimap(
            point( win->pos.x * ::fontwidth, win->pos.y * ::fontheight ),
            tripoint_bub_ms( g->u.bub_pos().xy(), g->ter_view_p.z() ),
            win->width * font->width, win->height * font->height );
        update = true;

    } else {
        // Either not using tiles (::tilecontext) or not the w_terrain window.
        update = draw_window( font, w );
    }
    if( update ) {
        needupdate = true;
    }
}
