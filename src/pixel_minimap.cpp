#include "pixel_minimap.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "avatar.h"
#include "cata_utility.h"
#include "character.h"
#include "color.h"
#include "coordinate_conversions.h"
#include "creature.h"
#include "debug.h"
#include "game.h"
#include "game_constants.h"
#include "int_id.h"
#include "lightmap.h"
#include "map.h"
#include "mapdata.h"
#include "math_defines.h"
#include "monster.h"
#include "pixel_minimap_projectors.h"
#include "sdl_utils.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_position.h"

namespace
{

// Updated each frame in draw() to reflect the runtime g_mapsize.
static point total_tiles_count = { ( 11 - 2 ) *SEEX, ( 11 - 2 ) *SEEY }; // default size=2

// The actual tile grid rendered by the projector. May be smaller than total_tiles_count
// (clipped to keep tile_size >= 2 so dots always have a 1-pixel gap) or larger
// (minimum 60-tile radius = 120 tiles, padding with black beyond the bubble edge).
static auto view_tiles_count = total_tiles_count;
// The view_tiles_count that the current projector was built from.
// Updated whenever set_screen_rect actually rebuilds; used to detect stale projectors
// whose tile_size happens to match even though view_tiles_count changed.
static auto built_view_tiles_count = point {0, 0};

point get_pixel_size( point tile_size, pixel_minimap_mode mode )
{
    switch( mode ) {
        case pixel_minimap_mode::solid:
            return tile_size;

        case pixel_minimap_mode::squares:
            return { std::max( tile_size.x - 1, 1 ), std::max( tile_size.y - 1, 1 ) };

        case pixel_minimap_mode::dots:
            return { point_south_east };
    }

    return {};
}

/// Returns a number in range [0..1]. The range lasts for @param phase_length_ms (milliseconds).
float get_animation_phase( int phase_length_ms )
{
    if( phase_length_ms == 0 ) {
        return 0.0f;
    }

    return std::fmod<float>( SDL_GetTicks(), phase_length_ms ) / phase_length_ms;
}

SDL_Color get_map_color_at( const tripoint &p )
{
    const map &here = get_map();
    if( const auto vp = here.veh_at( p ) ) {
        return curses_color_to_SDL( vp->vehicle().part_color( vp->part_index() ) );
    }

    if( const auto furn_id = here.furn( p ) ) {
        return curses_color_to_SDL( furn_id->color() );
    }

    return curses_color_to_SDL( here.ter( p )->color() );
}

bool is_critter_animated( Creature *critter )
{
    if( const monster *m = dynamic_cast<monster *>( critter ) ) {
        //faction status (attacking or tracking) determines if red highlights get applied to creature
        const monster_attitude matt = m->attitude( &get_avatar() );

        if( MATT_ATTACK == matt || MATT_FOLLOW == matt ) {
            return true;
        }
    }
    return false;
}

SDL_Color get_critter_color( Creature *critter, int flicker, int mixture )
{
    SDL_Color result = curses_color_to_SDL( critter->symbol_color() );

    if( is_critter_animated( critter ) ) {
        const SDL_Color red_pixel = SDL_Color{ 0xFF, 0x0, 0x0, 0xFF };
        result = adjust_color_brightness( mix_colors( result, red_pixel, mixture ), flicker );
    }

    return result;
}

} // namespace

// a texture pool to avoid recreating textures every time player changes their view
struct pixel_minimap::submap_cache {
    std::array<SDL_Color, SEEX * SEEY> minimap_colors = {};
    bool touched = false;

    SDL_Color &color_at( point p ) {
        assert( p.x < SEEX );
        assert( p.y < SEEY );
        return minimap_colors[p.y * SEEX + p.x];
    }
};

pixel_minimap::pixel_minimap( const SDL_Renderer_Ptr &renderer,
                              const GeometryRenderer_Ptr &geometry ) :
    renderer( renderer ),
    geometry( geometry ),
    type( pixel_minimap_type::ortho ),
    screen_rect{ 0, 0, 0, 0 }
{
}

pixel_minimap::~pixel_minimap() = default;

void pixel_minimap::set_type( pixel_minimap_type type )
{
    this->type = type;
    reset();
}

void pixel_minimap::set_settings( const pixel_minimap_settings &settings )
{
    this->settings = settings;
    reset();
}

void pixel_minimap::prepare_cache_for_updates( const tripoint &center )
{
    const tripoint new_center_sm = get_map().get_abs_sub().raw() + ms_to_sm_copy( center );
    const tripoint center_sm_diff = cached_center_sm - new_center_sm;

    //invalidate the cache if the game shifted more than one submap in the last update, or if z-level changed.
    if( std::abs( center_sm_diff.x ) > 1 ||
        std::abs( center_sm_diff.y ) > 1 ||
        std::abs( center_sm_diff.z ) > 0 ) {
        cache.clear();
    } else {
        for( auto &mcp : cache ) {
            mcp.second.touched = false;
        }
    }

    cached_center_sm = new_center_sm;
}

//deletes the mapping of unused submap caches from the main map
//the touched flag prevents deletion
void pixel_minimap::clear_unused_cache()
{
    for( auto it = cache.begin(); it != cache.end(); ) {
        it = it->second.touched ? std::next( it ) : cache.erase( it );
    }
}

void pixel_minimap::flush_cache_updates()
{
    // GPU path: color data lives in minimap_colors arrays.
    // render_cache() draws directly to screen via queue_ui_rect each frame.
}

void pixel_minimap::update_cache_at( const tripoint &pos )
{
    const map &here = get_map();
    auto sm_pos = tripoint_abs_sm( pos );
    const level_cache &access_cache = here.access_cache( sm_pos.z() );
    const bool nv_goggle = get_avatar().get_vision_modes()[NV_GOGGLES];
    const bool env_goggle = get_avatar().get_vision_modes()[ENV_GOGGLES];

    submap_cache &cache_item = get_cache_at( here.get_abs_sub().raw() + sm_pos.raw() );
    const auto ms_pos = project_to<coords::ms>( sm_pos );

    cache_item.touched = true;

    for( int y = 0; y < SEEY; ++y ) {
        for( int x = 0; x < SEEX; ++x ) {
            const auto p = ms_pos + tripoint{ x, y, 0 };
            const lit_level lighting = access_cache.visibility_cache[access_cache.idx( p.x(), p.y() )];

            SDL_Color color;

            if( lighting == lit_level::BLANK || lighting == lit_level::DARK ) {
                // TODO: Map memory?
                color = { 0x00, 0x00, 0x00, 0xFF };
            } else {
                color = get_map_color_at( p.raw() );

                //color terrain according to lighting conditions
                if( nv_goggle || env_goggle ) {
                    if( lighting == lit_level::LOW ) {
                        color = color_pixel_nightvision( color );
                    } else if( lighting != lit_level::DARK && lighting != lit_level::BLANK ) {
                        color = color_pixel_overexposed( color );
                    }
                } else if( lighting == lit_level::LOW ) {
                    color = color_pixel_grayscale( color );
                }

                color = adjust_color_brightness( color, settings.brightness );
            }

            SDL_Color &current_color = cache_item.color_at( { x, y } );

            if( current_color != color ) {
                current_color = color;
            }
        }
    }
}

pixel_minimap::submap_cache &pixel_minimap::get_cache_at( const tripoint &abs_sm_pos )
{
    return cache.try_emplace( abs_sm_pos ).first->second;
}

void pixel_minimap::process_cache( const tripoint &center )
{
    // Refresh the tile count to match the current runtime map size.
    total_tiles_count = { ( g_mapsize - 2 ) *SEEX, ( g_mapsize - 2 ) *SEEY };

    prepare_cache_for_updates( center );

    for( int y = 0; y < g_mapsize; ++y ) {
        for( int x = 0; x < g_mapsize; ++x ) {
            update_cache_at( { x, y, center.z } );
        }
    }

    flush_cache_updates();
    clear_unused_cache();
}

void pixel_minimap::set_screen_rect( const SDL_Rect &screen_rect )
{
    if( this->screen_rect == screen_rect && projector
        && built_view_tiles_count == view_tiles_count ) {
        return;
    }

    this->screen_rect = screen_rect;

    projector = create_projector( screen_rect );
    pixel_size = get_pixel_size( projector->get_tile_size(), settings.mode );

    const auto size_on_screen = projector->get_tiles_size( view_tiles_count );

    if( settings.scale_to_fit ) {
        // TODO: scale_to_fit renders at natural pixel size for now (no transform);
        // will be fixed when phase 2i-B-7d adds scissor rect support.
        main_tex_clip_rect = SDL_Rect{ 0, 0, size_on_screen.x, size_on_screen.y };
        screen_clip_rect = fit_rect_inside( main_tex_clip_rect, screen_rect );
    } else {
        const point d( ( size_on_screen.x - screen_rect.w ) / 2,
                       ( size_on_screen.y - screen_rect.h ) / 2 );

        main_tex_clip_rect = SDL_Rect{
            std::max( d.x, 0 ),
            std::max( d.y, 0 ),
            size_on_screen.x - 2 * std::max( d.x, 0 ),
            size_on_screen.y - 2 * std::max( d.y, 0 )
        };

        screen_clip_rect = SDL_Rect{
            screen_rect.x - std::min( d.x, 0 ),
            screen_rect.y - std::min( d.y, 0 ),
            main_tex_clip_rect.w,
            main_tex_clip_rect.h
        };
    }

    cache.clear();
    built_view_tiles_count = view_tiles_count;
}

void pixel_minimap::reset()
{
    projector.reset();
    cache.clear();
}

void pixel_minimap::render( const tripoint &center )
{
    render_cache( center );
    render_critters( center );
}

void pixel_minimap::render_cache( const tripoint &center )
{
    const tripoint sm_center = get_map().get_abs_sub().raw() + ms_to_sm_copy( center );
    const auto sm_offset = tripoint{
        view_tiles_count.x / SEEX / 2,
        view_tiles_count.y / SEEY / 2, 0
    };

    auto ms_remain = center.xy();
    ms_to_sm_remain( ms_remain );
    const auto ms_offset = point{
        view_tiles_count.x / 2 - sm_offset.x * SEEX - ms_remain.x,
        view_tiles_count.y / 2 - sm_offset.y * SEEY - ms_remain.y
    };

    // Offset to convert from main_tex space to screen space.
    const point screen_base{
        screen_clip_rect.x - main_tex_clip_rect.x,
        screen_clip_rect.y - main_tex_clip_rect.y
    };

    // TODO: instance-cap monitoring — at g_mapsize=11 this is ~17K rects;
    // very large bubbles may approach MAX_INSTANCES=65536.
    for( const auto &elem : cache ) {
        if( !elem.second.touched ) {
            continue;
        }

        const tripoint rel_pos = elem.first - sm_center;

        if( std::abs( rel_pos.x ) > sm_offset.x + 1 ||
            std::abs( rel_pos.y ) > sm_offset.y + 1 ||
            rel_pos.z != 0 ) {
            continue;
        }

        const tripoint sm_pos = rel_pos + sm_offset;
        const tripoint ms_pos = sm_to_ms_copy( sm_pos ) + ms_offset;

        for( int y = 0; y < SEEY; ++y ) {
            for( int x = 0; x < SEEX; ++x ) {
                const SDL_Color color = elem.second.minimap_colors[y * SEEX + x];
                if( color.a == 0 ) {
                    continue;
                }

                const point tile_pos = projector->get_tile_pos(
                                           { ms_pos.x + x, ms_pos.y + y }, view_tiles_count )
                                       + screen_base;

                if( tile_pos.x < screen_clip_rect.x ||
                    tile_pos.y < screen_clip_rect.y ||
                    tile_pos.x + pixel_size.x > screen_clip_rect.x + screen_clip_rect.w ||
                    tile_pos.y + pixel_size.y > screen_clip_rect.y + screen_clip_rect.h ) {
                    continue;
                }

                geometry->rect( renderer, tile_pos, pixel_size.x, pixel_size.y, color );
            }
        }
    }
}

void pixel_minimap::render_critters( const tripoint &center )
{
    const int indicator_length = settings.beacon_blink_interval * 200;

    int flicker = 100;
    int mixture = 0;

    if( indicator_length > 0 ) {
        const float t = get_animation_phase( 2 * indicator_length );
        const float s = std::sin( 2 * M_PI * t );

        flicker = lerp_clamped( 25, 100, std::abs( s ) );
        mixture = lerp_clamped( 0, 100, std::max( s, 0.0f ) );
    }

    const level_cache &access_cache = get_map().access_cache( center.z );

    const auto start_x = center.x - view_tiles_count.x / 2;
    const auto start_y = center.y - view_tiles_count.y / 2;
    const point beacon_size = {
        std::max<int>( projector->get_tile_size().x * settings.beacon_size / 2, 2 ),
        std::max<int>( projector->get_tile_size().y * settings.beacon_size / 2, 2 )
    };

    const point screen_base{
        screen_clip_rect.x - main_tex_clip_rect.x,
        screen_clip_rect.y - main_tex_clip_rect.y
    };

    cached_has_animated_beacons = false;
    for( int y = 0; y < view_tiles_count.y; y++ ) {
        for( int x = 0; x < view_tiles_count.x; x++ ) {
            const tripoint p = tripoint{ start_x + x, start_y + y, center.z };
            if( !access_cache.inbounds( p.xy() ) ) {
                continue;
            }
            const lit_level lighting = access_cache.visibility_cache[access_cache.idx( p.x, p.y )];

            if( lighting == lit_level::DARK || lighting == lit_level::BLANK ) {
                continue;
            }

            const auto critter = g->critter_at( p, true );

            if( critter == nullptr || !get_avatar().sees( *critter ) ) {
                continue;
            }

            const point critter_pos = projector->get_tile_pos( { x, y }, view_tiles_count )
                                      + screen_base;
            const SDL_Rect critter_rect = SDL_Rect{
                critter_pos.x, critter_pos.y, beacon_size.x, beacon_size.y
            };
            const SDL_Color critter_color = get_critter_color( critter, flicker, mixture );
            cached_has_animated_beacons = cached_has_animated_beacons || is_critter_animated( critter );

            draw_beacon( critter_rect, critter_color );
        }
    }
}

//the main call for drawing the pixel minimap to the screen
void pixel_minimap::draw( const SDL_Rect &screen_rect, const tripoint &center )
{
    if( !g ) {
        return;
    }

    if( screen_rect.w <= 0 || screen_rect.h <= 0 ) {
        return;
    }

    // Update the tile count from the current runtime bubble size BEFORE set_screen_rect
    // so the projector is always built with the correct grid dimensions.
    total_tiles_count = { ( g_mapsize - 2 ) *SEEX, ( g_mapsize - 2 ) *SEEY };

    // view_tiles_count is the geographic tile grid the projector and renderer use.
    // Large bubble: clip to screen/2 (tile_size >= 2); small bubble: screen/3 (tile_size >= 3).
    // Out-of-bubble area is rendered as black.
    constexpr auto min_view_radius = 60;
    constexpr auto min_view_diameter = 2 * min_view_radius;
    view_tiles_count = {
        total_tiles_count.x > min_view_diameter
        ? std::min( total_tiles_count.x, screen_rect.w / 2 )
        : screen_rect.w / 3,
        total_tiles_count.y > min_view_diameter
        ? std::min( total_tiles_count.y, screen_rect.h / 2 )
        : screen_rect.h / 3
    };

    set_screen_rect( screen_rect );
    process_cache( center );
    render( center );
}

bool pixel_minimap::has_animated_elements() const
{
    return settings.beacon_blink_interval && cached_has_animated_beacons;
}

void pixel_minimap::draw_beacon( const SDL_Rect &rect, const SDL_Color &color )
{
    for( int x = -rect.w, x_max = rect.w; x <= x_max; ++x ) {
        for( int y = -rect.h + std::abs( x ), y_max = rect.h - std::abs( x ); y <= y_max; ++y ) {
            const int divisor = 2 * ( std::abs( y ) == rect.h - std::abs( x ) ? 1 : 0 ) + 1;
            const SDL_Color dot{ static_cast<Uint8>( color.r / divisor ),
                                 static_cast<Uint8>( color.g / divisor ),
                                 static_cast<Uint8>( color.b / divisor ),
                                 0xFF };
            geometry->rect( renderer, point( rect.x + x, rect.y + y ), 1, 1, dot );
        }
    }
}

std::unique_ptr<pixel_minimap_projector> pixel_minimap::create_projector(
    const SDL_Rect &max_screen_rect )
const
{
    switch( type ) {
        case pixel_minimap_type::ortho:
            return std::unique_ptr<pixel_minimap_projector> {
                new pixel_minimap_ortho_projector( view_tiles_count, max_screen_rect, settings.square_pixels )
            };

        case pixel_minimap_type::iso:
            return std::unique_ptr<pixel_minimap_projector> {
                new pixel_minimap_iso_projector( view_tiles_count, max_screen_rect, settings.square_pixels )
            };
    }

    return nullptr;
}

