#include "cata_tiles.h"
#include "cata_tiles_internal.h"
#include "lighting/solid_overlay.h"
#include "physics/physics_world.h"

#include "action.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "character.h"
#include "character_id.h"
#include "character_state_provider.h"
#include "clzones.h"
#include "cuboid_rectangle.h"
#include "cursesdef.h"
#include "cursesport.h"
#include "debug.h"
#include "dynamic_atlas.h"
#include "field.h"
#include "field_type.h"
#include "game.h"
#include "game_constants.h"
#include "init.h"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "json.h"
#include "lightmap.h"
#include "line.h"
#include "make_static.h"
#include "map.h"
#include "map_memory.h"
#include "mapbuffer.h"
#include "mapdata.h"
#include "mod_tileset.h"
#include "monster.h"
#include "monstergenerator.h"
#include "mtype.h"
#include "npc.h"
#include "omdata.h"
#include "options.h"
#include "overlay_ordering.h"
#include "overmap.h"
#include "overmap_location.h"
#include "overmapbuffer.h"
#include "player.h"
#include "profile.h"
#include "rect_range.h"
#include "rml_screen.h"
#include "scent_map.h"
#include "sdl_wrappers.h"
#include "sdl_display.h"
#include "sdltiles.h"
#include "shadowcasting.h"
#include "sounds.h"
#include "splatmap_stamps.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_utils.h"
#include "submap.h"
#include "submap_load_manager.h"
#include "tileray.h"
#include "translations.h"
#include "trap.h"
#include "type_id.h"
#include "units_temperature.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_position.h"
#include "weather.h"
#include "weighted_list.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_set>

using namespace cata_tiles_internal;
#define dbg(x) DebugLogFL((x), DC::SDL)

static const efftype_id effect_ridden( "ridden" );

static const trait_id trait_INATTENTIVE( "INATTENTIVE" );

static const std::string ZOMBIE_REVIVAL_INDICATOR( "zombie_revival_indicator" );

static const flag_id flag_TINT_NO_FG( "TINT_NO_FG" );
static const flag_id flag_TINT_NO_BG( "TINT_NO_BG" );
static const flag_id flag_TINT_NONE( "TINT_NONE" );


namespace
{

std::string get_ascii_tile_id( const uint32_t sym, const int FG, const int BG )
{
    return std::string( {
        'A', 'S', 'C', 'I', 'I', '_', static_cast<char>( sym ), static_cast<char>( FG ),
        static_cast<char>( BG )} );
}


struct draw_zone_overlay_options {
    SDL_Rect rect;
    SDL_Color color;
    std::multimap<point, formatted_text> &overlay_strings;
    std::string name = empty_string;
    int alpha = 64;
    bool draw_label = true;
};

void draw_zone_overlay( const draw_zone_overlay_options& opt )
{
    lighting::overlay_rect(
    { static_cast<float>( opt.rect.x ), static_cast<float>( opt.rect.y ),
      static_cast<float>( opt.rect.w ), static_cast<float>( opt.rect.h ) },
    lighting::overlay_color_from_bytes( opt.color.r, opt.color.g, opt.color.b, opt.alpha ) );

    if( opt.draw_label && !opt.name.empty() ) {
        const point center( opt.rect.x + opt.rect.w / 2, opt.rect.y + opt.rect.h / 2 );
        opt.overlay_strings
        .emplace( center, formatted_text( opt.name, catacurses::white, text_alignment::center ) );
    }
}

} // namespace

static int msgtype_to_tilecolor( const game_message_type type, const bool bOldMsg )
{
    const int iBold = bOldMsg ? 0 : 8;

    switch( type ) {
        case m_good:
            return iBold + catacurses::green;
        case m_bad:
            return iBold + catacurses::red;
        case m_mixed:
        case m_headshot:
            return iBold + catacurses::magenta;
        case m_neutral:
            return iBold + catacurses::white;
        case m_warning:
        case m_critical:
            return iBold + catacurses::yellow;
        case m_info:
        case m_grazing:
            return iBold + catacurses::blue;
        default:
            break;
    }

    return -1;
}

formatted_text::formatted_text(
    const std::string& text, const int color, const direction text_direction )
    : text( text ),
      color( color )
{
    switch( text_direction ) {
        case direction::NORTHWEST:
        case direction::WEST:
        case direction::SOUTHWEST:
            alignment = text_alignment::right;
            break;
        case direction::NORTH:
        case direction::CENTER:
        case direction::SOUTH:
            alignment = text_alignment::center;
            break;
        default:
            alignment = text_alignment::left;
            break;
    }
}

void idle_animation_manager::prepare_for_redraw()
{
    // Forget about animations from previous frame
    present_ = false;

    if( !enabled_ ) {
        frame = 0;
        return;
    }

    // Use system clock to keep steady frame rate
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>( now );
    auto value = now_ms.time_since_epoch();
    // Aiming roughly at the standard 60 frames per second
    frame = value.count() / 17;
}

struct tile_render_info {
    tripoint_bub_ms pos;
    // accumulator for 3d tallness of sprites rendered here so far;
    int height_3d = 0;
    int screen_row = 0;
    lit_level ll;
    bool invisible[5];
    tile_render_info(
        const tripoint_bub_ms& pos, const int height_3d, const lit_level ll,
        const bool ( &invisible )[5] )
        : pos( pos ),
          height_3d( height_3d ),
          ll( ll ) {
        std::copy( invisible, invisible + 5, this->invisible );
    }
};

namespace
{

/// Record this frame's splatmap state: the tile_sprite_queue_ cut index (already
/// taken by the caller) plus one composite quad per visible submap on z-level
/// `z`. Freshly cached submaps are seeded from current field data so a reload
/// regenerates equivalent decals.
///
/// `to_screen` MUST be cata_tiles::player_to_screen — deriving the projection
/// any other way is what makes the composite drift off the tile sprites by a
/// sub-tile offset.
template <typename ToScreen>
auto record_splat_frame(
    const std::vector<tile_render_info> &draw_points, int z, const tripoint_abs_sm &abs_sub,
    int tile_w, int tile_h, std::size_t cut, const ToScreen &to_screen ) -> void
{
    lighting::render_state &rs = lighting::get_render_state();

    // Distinct submap indices covered by this z-level's draw points. A screen
    // holds a handful of submaps, so a flat vector beats a hash set here.
    std::vector<point> seen;
    std::vector<lighting::splat_quad> quads;
    for( const tile_render_info &p : draw_points ) {
        if( p.pos.z() != z ) {
            continue;
        }
        const point idx( p.pos.x() / SEEX, p.pos.y() / SEEY );
        if( std::ranges::contains( seen, idx ) ) {
            continue;
        }
        seen.push_back( idx );

        const point_bub_ms origin( idx.x * SEEX, idx.y * SEEY );
        const point tl = to_screen( origin );
        const std::uint64_t key =
            splatmap::key_of( tripoint_abs_sm( abs_sub.x() + idx.x, abs_sub.y() + idx.y, z ) );
        quads.push_back( {
            .dst_x = static_cast<float>( tl.x ),
            .dst_y = static_cast<float>( tl.y ),
            .dst_w = static_cast<float>( SEEX * tile_w ),
            .dst_h = static_cast<float>( SEEY * tile_h ),
            .key = key} );

        // touch() returns false only for a FRESHLY created entry, which is
        // exactly when its decal history has to be rebuilt from the fields.
        if( !rs.splatmap().touch( key ) ) {
            splatmap::seed_submap( tripoint_bub_ms( origin.x(), origin.y(), z ), key );
        }
    }
    rs.set_splat_frame( cut, std::move( quads ) );
}

} // namespace

cata_tiles::cata_tiles( const SDL_Renderer_Ptr& renderer, const GeometryRenderer_Ptr& geometry )
    : renderer( renderer ),
      geometry( geometry )
{
    assert( renderer );

    tile_height = 0;
    tile_width = 0;
    tile_ratiox = 0;
    tile_ratioy = 0;

    in_animation = false;
    do_draw_explosion = false;
    do_draw_custom_explosion = false;
    do_draw_line = false;
    do_draw_cursor = false;
    do_draw_highlight = false;
    do_draw_weather = false;
    do_draw_sct = false;
    do_draw_zones = false;

    nv_goggles_activated = false;
    env_goggles_activated = false;

    on_options_changed();
}

cata_tiles::~cata_tiles() = default;


const tile_type *tileset::find_tile_type( const std::string& id ) const
{
    const auto iter = tile_ids.find( id );
    return iter != tile_ids.end() ? &iter->second : nullptr;
}

std::optional<tile_lookup_res> tileset::find_tile_type_by_season(
    const std::string& id, season_type season ) const
{
    assert( season < season_type::NUM_SEASONS );
    const auto iter = tile_ids_by_season[season].find( id );

    if( iter == tile_ids_by_season[season].end() ) { return std::nullopt; }
    auto& res = iter->second;
    if( res.season_tile ) {
    return *res.season_tile;
} else if( res.default_tile ) { // can skip this check, but just in case
    return tile_lookup_res( iter->first, *res.default_tile );
    }
    debugmsg( "empty record found in `tile_ids_by_season` for key: %s", id );
    return std::nullopt;
}

tile_type &tileset::create_tile_type( const std::string& id, tile_type&& new_tile_type )
{
    // Must overwrite existing tile
    // TODO: c++17 - replace [] + find() with insert_or_assign()
    tile_ids[id] = std::move( new_tile_type );
    auto inserted = tile_ids.find( id );

    const std::string& inserted_id = inserted->first;
    tile_type& inserted_tile = inserted->second;

    // populate cache by season
    constexpr size_t suffix_len = 15;
    constexpr char season_suffix[4][suffix_len] =
    {"_season_spring", "_season_summer", "_season_autumn", "_season_winter"};
    bool has_season_suffix = false;
    for( int i = 0; i < 4; i++ ) {
        if( id.ends_with( season_suffix[i] ) ) {
            has_season_suffix = true;
            // key is id without _season suffix
            season_tile_value& value =
                tile_ids_by_season[i][id.substr( 0, id.size() - strlen( season_suffix[i] ) )];
            // value stores reference to string id with _season suffix
            value.season_tile = tile_lookup_res( inserted_id, inserted_tile );
            break;
        }
    }
    // tile doesn't have _season suffix, add it as "default" into all four seasons
    if( !has_season_suffix ) {
        for( auto& tile_ids_for_season : tile_ids_by_season ) {
            tile_ids_for_season[id].default_tile = &inserted_tile;
        }
    }

    return inserted_tile;
}





#if defined(DYNAMIC_ATLAS)
std::tuple<bool, SDL_Surface *, SDL_Rect> tileset::get_sprite_surface( int sprite_index ) const
{
    const auto base_tex_key = tileset_lookup_key{
        sprite_index,     TILESET_NO_MASK, tileset_fx_type::none,
        TILESET_NO_COLOR, TILESET_NO_WARP, point_zero};

    const auto tex_it = tile_lookup.find( base_tex_key );
    if( tex_it == tile_lookup.end() ) { return std::make_tuple( false, nullptr, SDL_Rect{} ); }

    // Note: Caller must ensure readback_load() has been called on the atlas
    // before calling this function. This is done once per character in
    // draw_entity_with_overlays to avoid repeated GPU->CPU transfers.
    return tileset_atlas->readback_find( tex_it->second.tex );
}


size_t tileset::register_warp_surface(
    SDL_Surface_Ptr surface, const point offset, const bool offset_mode ) const
{
    // Compute hash of the surface content
    const size_t hash = get_surface_hash( surface.get(), nullptr );
    if( hash == TILESET_NO_WARP ) {
        // Extremely unlikely, but avoid collision with "no warp" sentinel
        // Just use the surface anyway with a modified hash
        warp_cache[1] = warp_cache_entry{std::move( surface ), offset, offset_mode};
        return 1;
    }
    // Only store if not already cached (same UV state = same hash)
    if( warp_cache.find( hash ) == warp_cache.end() ) {
        warp_cache[hash] = warp_cache_entry{std::move( surface ), offset, offset_mode};
    }
    return hash;
}

std::tuple<SDL_Surface *, point, bool> tileset::get_warp_surface( const size_t warp_hash ) const
{
    const auto it = warp_cache.find( warp_hash );
    if( it == warp_cache.end() ) { return std::make_tuple( nullptr, point_zero, true ); }
    const auto& entry = it->second;
    return std::make_tuple( entry.surface.get(), entry.offset, entry.offset_mode );
}

#endif



void cata_tiles::set_draw_scale( float scale )
{
    assert( tileset_ptr );
    tile_width = tileset_ptr->get_tile_width() * tileset_ptr->get_tile_pixelscale() * scale / 16;
    tile_height = tileset_ptr->get_tile_height() * tileset_ptr->get_tile_pixelscale() * scale / 16;

    tile_ratiox = ( static_cast<float>( tile_width ) / static_cast<float>( fontwidth ) );
    tile_ratioy = ( static_cast<float>( tile_height ) / static_cast<float>( fontheight ) );
}




std::pair<std::string, bool> tileset::get_tint_controller( const std::string& tint_type )
{
    if( tint_pairs.contains( tint_type ) ) { return tint_pairs[tint_type]; }
    return {};
}

const color_tint_pair *tileset::get_tint( const std::string& tint_id )
{
    if( tints.contains( tint_id ) ) { return &tints[tint_id]; }
    return nullptr;
}










// Tuning knobs for the sprite-animation system, refreshed once per frame from options
// (avoids re-reading 14 options per creature). File-scope keeps creature.h's
// animation_tuning type out of cata_tiles.h.
static animation_tuning s_anim_tuning;
// When set by the F4 "Animation" tab, the panel owns s_anim_tuning and we stop
// clobbering it with the option values each frame (and force animations on).
static bool s_anim_override = false;


/// Global multiplier on per-tile depth_extrude_lean (0 = lean off, 1 = full, >1 = exaggerated).
float g_depth_lean_str = 1.0f;
/// Global multiplier on per-tile depth_extrude_dark (0 = darkening off, 1 = full).
float g_depth_dark_str = 1.0f;


void cata_tiles::draw(
    point dest, const tripoint_bub_ms& center, int width, int height,
    std::multimap<point, formatted_text> &overlay_strings,
    color_block_overlay_container& color_blocks )
{
    if( !g ) { return; }
    // Refresh the sprite-animation frame context (wall-clock + option tuning) once.
    refresh_anim_frame();

    // Hover-outline: holding Alt outlines ALL visible creatures (not just the one
    // under the cursor). Polled here since a bare modifier press may not trigger a
    // redraw on its own — it takes effect on the next animation-timeout redraw.
    outline_all_ = ( SDL_GetModState() & SDL_KMOD_ALT ) != 0;

    // Clear only tile sprites. UI/font queues are NOT cleared here so
    // partial UI redraws (tooltip, mouse-hover) still see sidebar content
    // from the last full tick. draw_om() uses clear_frame_queues() because
    // it is a full-screen view with its own UI layout.
    if( lighting::render_state * rs = &lighting::get_render_state(); rs->ready() ) {
        rs->clear_tile_queue();
        // Step 2: the sprite-alpha occluder list is derived from the tile sprite queue,
        // so it is rebuilt from scratch in lockstep with it. Sized to the RUNTIME cache
        // dims so captured_mask's stride matches TransBuf's (x * cache_y + y).
        const level_cache& mc = get_map().access_cache( center.z() );
        rs->occluders().resize( mc.cache_x, mc.cache_y );
        rs->occluders().begin();
    }

    ZoneScoped;
    {
        // GPU scissor — clips tile sprites to the map viewport.
        SDL_Rect clipRect = {dest.x, dest.y, width, height};
        lighting::get_render_state().set_tile_scissor( &clipRect );

        // No explicit black fill needed: the swapchain is cleared to black
        // by the tile_batcher pass (LOADOP_CLEAR) before tile sprites draw.
        // The old SDL_RenderFillRect here targeted the legacy display_buffer;
        // its GPU-path replacement (geometry->rect → ui_rect_queue) rendered
        // after tile sprites in the single-pass pipeline, covering them.
    }

    point s;
    get_window_tile_counts( width, height, s.x, s.y );

    init_light();
    map& here = get_map();
    const visibility_variables& cache = here.get_visibility_variables_cache();

    const bool iso_mode = tile_iso;

    const bool show_zones_overlay = g->show_zone_overlay && !iso_mode;

    struct zone_render_data {
        std::unordered_set<point_bub_ms> tiles;
        SDL_Color color;
        std::string name;
    };

    std::vector<zone_render_data> zones_to_draw;
    zones_to_draw.reserve( 64 );

    if( show_zones_overlay ) {
        const zone_manager& mgr = zone_manager::get_manager();
        std::ranges::for_each( mgr.get_zones(), [&]( const zone_manager::ref_const_zone_data & ref ) {
            const zone_data& zone = ref.get();
            if( !zone.get_enabled() ) { return; }

            const auto covered_points = get_zone_covered_points( zone );
            auto local_tiles = std::unordered_set<point_bub_ms>();
            std::ranges::for_each( covered_points, [&]( const tripoint_abs_ms & pos ) {
                if( pos.z() != center.z() ) { return; }
                local_tiles.insert( here.abs_to_bub( pos ).xy() );
            } );

            if( !local_tiles.empty() ) {
                zones_to_draw.push_back( {
                    std::move( local_tiles ), curses_color_to_SDL( zone.get_type().obj().color() ),
                    zone.get_name()} );
            }
        } );
    }

    const auto has_selected_zone = do_draw_zones;
    point_bub_ms selected_min = point_bub_ms::zero();
    point_bub_ms selected_max = point_bub_ms::zero();
    auto selected_z = 0;
    const auto has_custom_selected_zone = has_selected_zone && !zone_point_lookup.empty();
    if( has_selected_zone ) {
        const tripoint_bub_ms sel_start = zone_start + zone_offset;
        const tripoint_bub_ms sel_end = zone_end + zone_offset;
        selected_z = has_custom_selected_zone ? zone_points.front().z() : sel_start.z();
        if( has_custom_selected_zone ) {
            const auto min_x = std::ranges::
            minmax_element( zone_points, {}, []( const tripoint_bub_ms & p ) { return p.x(); } );
            const auto min_y = std::ranges::
            minmax_element( zone_points, {}, []( const tripoint_bub_ms & p ) { return p.y(); } );
            selected_min = point_bub_ms( min_x.min->x(), min_y.min->y() );
            selected_max = point_bub_ms( min_x.max->x(), min_y.max->y() );
        } else {
            selected_min = point_bub_ms(
                               std::min( sel_start.x(), sel_end.x() ), std::min( sel_start.y(), sel_end.y() ) );
            selected_max = point_bub_ms(
                               std::max( sel_start.x(), sel_end.x() ), std::max( sel_start.y(), sel_end.y() ) );
        }
    }

    if( has_selected_zone ) {
        const auto width = selected_max.x() - selected_min.x() + 1;
        const auto height = selected_max.y() - selected_min.y() + 1;
        if( width > 0 && height > 0 ) {
            const auto label = string_format( _( "(%dx%d)" ), width, height );
            const auto center = point_bub_ms(
                                    ( selected_min.x() + selected_max.x() ) / 2,
                                    ( selected_min.y() + selected_max.y() ) / 2 );
            overlay_strings.emplace(
                player_to_screen( center ) + point( tile_width / 2, 0 ),
                formatted_text( label, catacurses::white, direction::NORTH ) );
        }
    }

    if( iso_mode ) {
        // Sub-tile smoothing is disabled in iso: a cartesian pixel shift on op
        // is not the iso diamond projection of a sub-tile world move.
        o = center.xy();
        op = dest;
    } else {
        // Sub-tile scroll: floor the fractional center for the integer tile
        // origin, push the remainder into op as a pixel shift. o and op are the
        // single shared source of truth for both sprites (player_to_screen) and
        // lighting (cam_off = op/tile - o), so both scroll together and cannot
        // desync. With subtile_off == 0 this degenerates byte-for-byte to the
        // legacy framing. See plans/camera_subtile_contract.md.
        const double cfx = center.x() + static_cast<double>( subtile_off_x_ );
        const double cfy = center.y() + static_cast<double>( subtile_off_y_ );
        const int fx = static_cast<int>( std::floor( cfx ) );
        const int fy = static_cast<int>( std::floor( cfy ) );
        o = point_bub_ms( fx - POSX, fy - POSY );
        op = dest
             - point( static_cast<int>( std::lround( ( cfx - fx ) * tile_width ) ),
                      static_cast<int>( std::lround( ( cfy - fy ) * tile_height ) ) );
    }
    // Rounding up to include incomplete tiles at the bottom/right edges
    screentile_width = divide_round_up( width, tile_width );
    screentile_height = divide_round_up( height, tile_height );

    const int min_col = 0;
    const int max_col = s.x;
    const int min_row = 0;
    const int max_row = s.y;

    // limit the render area to maximum view range (121x121 square centered on player)
    const int min_visible_x = g->u.bub_pos().x() % SEEX;
    const int min_visible_y = g->u.bub_pos().y() % SEEY;
    const int max_visible_x = ( g->u.bub_pos().x() % SEEX ) + ( g_mapsize - 1 ) * SEEX;
    const int max_visible_y = ( g->u.bub_pos().y() % SEEY ) + ( g_mapsize - 1 ) * SEEY;

    // Map memory should be at least the size of the view range
    // so that new tiles can be memorized, and at least the size of the display
    // since at farthest zoom displayed area may be bigger than view range.
    const auto min_mm_reg =
        point_bub_ms( std::min( o.x(), min_visible_x ), std::min( o.y(), min_visible_y ) );
    const auto max_mm_reg =
        point_bub_ms( std::max( s.x + o.x(), max_visible_x ), std::max( s.y + o.y(), max_visible_y ) );
    g->u.prepare_map_memory_region(
        here.bub_to_abs( tripoint_bub_ms( min_mm_reg, center.z() ) ),
        here.bub_to_abs( tripoint_bub_ms( max_mm_reg, center.z() ) ) );

    idle_animations.set_enabled( get_option<bool>( "ANIMATIONS" ) );
    idle_animations.prepare_for_redraw();

    // set up a default tile for the edges outside the render area
    visibility_type offscreen_type = VIS_DARK;
    if( cache.u_is_boomered ) { offscreen_type = VIS_BOOMER_DARK; }

    // retrieve night vision goggle status once per draw
    auto vision_cache = g->u.get_vision_modes();
    nv_goggles_activated = vision_cache[NV_GOGGLES];
    env_goggles_activated = vision_cache[ENV_GOGGLES];

    // check that the creature for which we'll draw the visibility map is still alive at that point
    if( g->display_overlay_state( ACTION_DISPLAY_VISIBILITY ) && g->displaying_visibility_creature ) {
        const Creature* creature = g->displaying_visibility_creature;
        const auto is_same_creature_predicate = [&creature]( const Creature & c ) {
            return creature == &c;
        };
        if( g->get_creature_if( is_same_creature_predicate ) == nullptr ) {
            g->displaying_visibility_creature = nullptr;
        }
    }
    const point half_tile( tile_width / 2, 0 );
    const point quarter_tile( tile_width / 4, tile_height / 4 );
    if( g->display_overlay_state( ACTION_DISPLAY_VEHICLE_AI ) ) {
        for( const wrapped_vehicle& elem : here.get_vehicles() ) {
            const vehicle& veh = *elem.v;
            const auto veh_pos = veh.bub_ms_location().xy();
            for( const auto& overlay_data : veh.get_debug_overlay_data() ) {
                const auto pt = veh_pos + std::get<0>( overlay_data );
                const int color = std::get<1>( overlay_data );
                const std::string& text = std::get<2>( overlay_data );
                overlay_strings.emplace(
                    player_to_screen( pt ), formatted_text( text, color, text_alignment::left ) );
            }
        }
    }

    std::vector<tile_render_info> &draw_points = *draw_points_cache;
    int min_z = OVERMAP_HEIGHT;
    draw_points.clear();

    // Pre-compute sound visualization data once per frame (not per tile)
    std::unordered_map<tripoint_bub_ms, sounds::sound_vis_tile> sound_data;
    std::unordered_set<tripoint_bub_ms> ray_tiles;
    if( g->display_overlay_state( ACTION_DISPLAY_SOUND ) ) {
        sound_data = sounds::compute_sound_visualization( g->u.bub_pos() );
        const auto rays = sounds::compute_sound_rays( g->u.bub_pos() );
        for( const auto &ray : rays ) {
            for( const auto &pt : ray.path ) {
                ray_tiles.insert( pt );
            }
        }
    }


    for( int row = min_row; row < max_row; row++ ) {

        for( int col = min_col; col < max_col; col++ ) {
            int temp_x;
            int temp_y;
            if( iso_mode ) {
                // in isometric, rows and columns represent a checkerboard screen space, and we
                // place the appropriate tile in valid squares by getting position relative to the
                // screen center.
                if( modulo( row - s.y / 2, 2 ) != modulo( col - s.x / 2, 2 ) ) { continue; }
                temp_x = divide_round_down( col - row - s.x / 2 + s.y / 2, 2 ) + o.x();
                temp_y = divide_round_down( row + col - s.y / 2 - s.x / 2, 2 ) + o.y();
            } else {
                temp_x = col + o.x();
                temp_y = row + o.y();
            }

            bool invis =
                ( temp_y < min_visible_y || temp_y > max_visible_y || temp_x < min_visible_x
                  || temp_x > max_visible_x )
                && ( has_memory_at( {temp_x, temp_y, center.z()} )
                     || has_draw_override( {temp_x, temp_y, center.z()} ) );


            // Add scent value to the overlay_strings list for every visible tile when displaying
            // scent
            if( g->display_overlay_state( ACTION_DISPLAY_SCENT ) && !invis ) {
                const int scent_value = g->scent.get( {temp_x, temp_y, center.z()} );
                if( scent_value > 0 ) {
                    overlay_strings.emplace(
                        player_to_screen( point_bub_ms( temp_x, temp_y ) ) + point( tile_width / 2, 0 ),
                        formatted_text(
                            std::to_string( scent_value ), 8 + catacurses::yellow, direction::NORTH ) );
                }
            }

            // Add scent type to the overlay_strings list for every visible tile when displaying
            // scent
            if( g->display_overlay_state( ACTION_DISPLAY_SCENT_TYPE ) && !invis ) {
                const scenttype_id scent_type = g->scent.get_type( {temp_x, temp_y, center.z()} );
                if( !scent_type.is_empty() ) {
                    overlay_strings.emplace(
                        player_to_screen( point_bub_ms( temp_x, temp_y ) ) + point( tile_width / 2, 0 ),
                        formatted_text(
                            scent_type.c_str(), 8 + catacurses::yellow, direction::NORTH ) );
                }
            }

            if( g->display_overlay_state( ACTION_DISPLAY_RADIATION ) ) {
                const auto rad_override = radiation_override.find( {temp_x, temp_y, center.z()} );
                const bool rad_overridden = rad_override != radiation_override.end();
                if( rad_overridden || !invis ) {
                    const int rad_value =
                        rad_overridden
                        ? rad_override->second
                        : here.get_radiation( {temp_x, temp_y, center.z()} );
                    catacurses::base_color col;
                    if( rad_value > 0 ) {
                        col = catacurses::green;
                    } else {
                        col = catacurses::cyan;
                    }
                    overlay_strings.emplace(
                        player_to_screen( point_bub_ms( temp_x, temp_y ) ) + point( tile_width / 2, 0 ),
                        formatted_text( std::to_string( rad_value ), 8 + col, direction::NORTH ) );
                }
            }

            // Add temperature value to the overlay_strings list for every visible tile when
            // displaying temperature
            if( g->display_overlay_state( ACTION_DISPLAY_TEMPERATURE ) && !invis ) {
                const auto temp = get_weather().get_temperature(
                                      tripoint_abs_omt{temp_x, temp_y, center.z()} );
                short color;
                const short bold = 8;
                if( temp > 40_c ) {
                    color = catacurses::red;
                } else if( temp > 25_c ) {
                    color = catacurses::yellow + bold;
                } else if( temp > 10_c ) {
                    color = catacurses::green + bold;
                } else if( temp > 0_c ) {
                    color = catacurses::white + bold;
                } else if( temp > -10_c ) {
                    color = catacurses::cyan + bold;
                } else {
                    color = catacurses::blue + bold;
                }
                const auto display_option = get_option<std::string>( "USE_CELSIUS" );
                const int temp_value =
                    display_option == "kelvin" ? units::to_kelvins( temp )
                    : display_option == "fahrenheit"
                    ? units::to_fahrenheit( temp )
                    : units::to_celsius( temp );

                overlay_strings.emplace(
                    player_to_screen( point_bub_ms( temp_x, temp_y ) ) + point( tile_width / 2, 0 ),
                    formatted_text( std::to_string( temp_value ), color, direction::NORTH ) );
            }

            if( g->display_overlay_state( ACTION_DISPLAY_VISIBILITY )
                && g->displaying_visibility_creature && !invis ) {
                const bool visibility = g->displaying_visibility_creature->sees(
                {temp_x, temp_y, center.z()} );

                // color overlay.
                auto block_color =
                    visibility ? windowsPalette[catacurses::green] : SDL_Color{192, 192, 192, 255};
                block_color.a = 100;
                color_blocks.first = SDL_BLENDMODE_BLEND;
                color_blocks.second
                .emplace( player_to_screen( point_bub_ms( temp_x, temp_y ) ), block_color );

                // overlay string
                std::string visibility_str = visibility ? "+" : "-";
                overlay_strings.emplace(
                    player_to_screen( point_bub_ms( temp_x, temp_y ) )
                    + point( tile_width / 4, tile_height / 4 ),
                    formatted_text( visibility_str, catacurses::black, direction::NORTH ) );
            }

            static std::vector<SDL_Color> lighting_colors;
            // color hue in the range of [0..10], 0 being white,  10 being blue
            auto draw_debug_tile = [&]( const int color_hue, const std::string & text ) {
                if( lighting_colors.empty() ) {
                    SDL_Color white = {255, 255, 255, 255};
                    SDL_Color blue = {0, 0, 255, 255};
                    lighting_colors = color_linear_interpolate( white, blue, 9 );
                }
                point tile_pos = player_to_screen( point_bub_ms( temp_x, temp_y ) );

                // color overlay
                SDL_Color color = lighting_colors[std::min( std::max( 0, color_hue ), 10 )];
                color.a = 100;
                color_blocks.first = SDL_BLENDMODE_BLEND;
                color_blocks.second.emplace( tile_pos, color );

                // string overlay
                overlay_strings.emplace(
                    tile_pos + quarter_tile,
                    formatted_text( text, catacurses::black, direction::NORTH ) );
            };

            if( g->display_overlay_state( ACTION_DISPLAY_LIGHTING ) ) {
                if( g->displaying_lighting_condition == 0 ) {
                    const float light = here.ambient_light_at( {temp_x, temp_y, center.z()} );
                    // note: lighting will be constrained in the [1.0, 11.0] range.
                    const int intensity =
                        static_cast<int>( std::max( 1.0, LIGHT_AMBIENT_LIT - light + 1.0 ) ) - 1;
                    draw_debug_tile( intensity, string_format( "%.1f", light ) );
                }
            }

            if( g->display_overlay_state( ACTION_DISPLAY_TRANSPARENCY ) ) {
                const float tr = here.light_transparency( {temp_x, temp_y, center.z()} );
                const int intensity =
                    tr <= LIGHT_TRANSPARENCY_SOLID
                    ? 10
                    : static_cast<int>( ( tr - LIGHT_TRANSPARENCY_OPEN_AIR ) * 8 );
                draw_debug_tile( intensity, string_format( "%.2f", tr ) );
            }

            if( g->display_overlay_state( ACTION_DISPLAY_OUTSIDE ) ) {
                // Use the flat level_cache directly: it includes Phase3 vehicle overrides
                // (outside=false / sheltered=true for covered vehicle tiles) that the
                // lazy per-submap rebuild path does not reflect.
                const auto& ov_ch = here.access_cache( center.z() );
                if( ov_ch.inbounds( point_bub_ms( temp_x, temp_y ) ) ) {
                    const int ov_idx = ov_ch.idx( temp_x, temp_y );
                    const bool outside = ov_ch.outside_cache[ov_idx];
                    const bool sheltered = ov_ch.sheltered_cache[ov_idx];
                    // Three states: open field (green), overhang (yellow), indoors (red)
                    SDL_Color block_color;
                    std::string label;
                    if( outside && !sheltered ) {
                        block_color = {0, 200, 0, 100}; // green - fully open
                        label = "O";
                    } else if( outside && sheltered ) {
                        block_color = {200, 200, 0, 100}; // yellow - overhang
                        label = "S";
                    } else {
                        block_color = {200, 0, 0, 100}; // red - indoors
                        label = "I";
                    }
                    color_blocks.first = SDL_BLENDMODE_BLEND;
                    color_blocks.second
                    .emplace( player_to_screen( point_bub_ms( temp_x, temp_y ) ), block_color );
                    overlay_strings.emplace(
                        player_to_screen( point_bub_ms( temp_x, temp_y ) )
                        + point( tile_width / 4, tile_height / 4 ),
                        formatted_text( label, catacurses::black, direction::NORTH ) );
                }
            }

            // Sound propagation debug overlay (per-tile lookup only)
            if( g->display_overlay_state( ACTION_DISPLAY_SOUND ) ) {
                const tripoint_bub_ms tile_pos( temp_x, temp_y, center.z() );
                const bool on_ray = ray_tiles.count( tile_pos );

                const auto it = sound_data.find( tile_pos );
                if( it != sound_data.end() ) {
                    const auto &sv = it->second;
                    const float intensity_clamped = std::clamp( sv.intensity, 0.0f, 1.0f );
                    const float occlusion_factor = 1.0f - std::clamp( sv.occlusion_db / 60.0f, 0.0f, 1.0f );

                    SDL_Color block_color;
                    if( intensity_clamped < 0.25f ) {
                        block_color = { 0, static_cast<uint8_t>( intensity_clamped * 4.0f * 255 ), 255, 128 };
                    } else if( intensity_clamped < 0.5f ) {
                        block_color = { 0, 255, static_cast<uint8_t>( ( 1.0f - ( intensity_clamped - 0.25f ) * 4.0f ) * 255 ), 128 };
                    } else if( intensity_clamped < 0.75f ) {
                        block_color = { static_cast<uint8_t>( ( intensity_clamped - 0.5f ) * 4.0f * 255 ), 255, 0, 128 };
                    } else {
                        block_color = { 255, static_cast<uint8_t>( ( 1.0f - ( intensity_clamped - 0.75f ) * 4.0f ) * 255 ), 0, 128 };
                    }

                    const float gray = block_color.r * 0.299f + block_color.g * 0.587f + block_color.b * 0.114f;
                    block_color.r = static_cast<uint8_t>( gray + ( block_color.r - gray ) * occlusion_factor );
                    block_color.g = static_cast<uint8_t>( gray + ( block_color.g - gray ) * occlusion_factor );
                    block_color.b = static_cast<uint8_t>( gray + ( block_color.b - gray ) * occlusion_factor );

                    color_blocks.first = SDL_BLENDMODE_BLEND;
                    color_blocks.second.emplace( player_to_screen( point_bub_ms( temp_x, temp_y ) ), block_color );

                    std::string label = string_format( "%d%%", static_cast<int>( intensity_clamped * 100 ) );
                    overlay_strings.emplace(
                        player_to_screen( point_bub_ms( temp_x, temp_y ) ) + point( tile_width / 4, tile_height / 4 ),
                        formatted_text( label, catacurses::black, direction::NORTH ) );

                    if( sv.is_source ) {
                        SDL_Color src_color = { 255, 255, 255, 200 };
                        color_blocks.first = SDL_BLENDMODE_BLEND;
                        color_blocks.second.emplace( player_to_screen( point_bub_ms( temp_x, temp_y ) ), src_color );
                    }
                } else if( on_ray ) {
                    SDL_Color ray_color = { 0, 255, 255, 100 };
                    color_blocks.first = SDL_BLENDMODE_BLEND;
                    color_blocks.second.emplace( player_to_screen( point_bub_ms( temp_x, temp_y ) ), ray_color );
                }
            }


            lit_level ll = lit_level::BLANK;
            int last_vis = center.z() + 1;
            lit_level last_vis_ll = lit_level::BLANK;
            bool drew_occluded_overlay = false;
            bool had_visible_open_air = false;
            const int &x = temp_x;
            const int &y = temp_y;
            const auto queue_draw_point = [&]( tile_render_info info ) {
                info.screen_row = row;
                draw_points.push_back( info );
            };
            const auto seen_through_air_light = [&]( const tripoint_bub_ms & pos ) {
                const auto& light_cache = here.access_cache( pos.z() );
                if( light_cache.inbounds( pos.xy() )
                    && light_cache.sm[light_cache.idx( pos.x(), pos.y() )] > 0.0f ) {
                    return lit_level::BRIGHT;
                }
                const auto light = here.ambient_light_at( pos );
                if( light > LIGHT_SOURCE_BRIGHT ) { return lit_level::BRIGHT; }
                if( light > LIGHT_AMBIENT_LIT ) { return lit_level::LIT; }
                return lit_level::LOW;
            };

            const bool in_vis_bounds =
                ( y >= min_visible_y && y <= max_visible_y && x >= min_visible_x
                  && x <= max_visible_x );
            for( int z = center.z(); z >= -OVERMAP_DEPTH; z-- ) {
                const auto& ch = here.access_cache( z );

                const tripoint_bub_ms pos( temp_x, temp_y, z );

                const bool in_map_bounds = here.inbounds( pos );

                const bool has_memory = has_memory_at( pos );

                const bool stop_on_memory =
                    z != center.z() && has_memory
                    && ( !in_map_bounds || here.ter( pos ) != t_open_air );

                ll = ch.inbounds( {x, y} ) ? ch.visibility_cache[ch.idx( x, y )] : lit_level::BLANK;
                const auto visibility = here.get_visibility( ll, cache );
                if( ( fov_3d || z == center.z() ) && in_map_bounds ) {
                    if( !would_apply_vision_effects( visibility ) ) {
                        if( here.ter( pos ) != t_open_air ) {
                            last_vis = z;
                            last_vis_ll = ll;
                        } else {
                            had_visible_open_air = true;
                        }
                    } else if(
                        !has_memory && z < center.z() && visibility == visibility_type::VIS_HIDDEN
                        && !( fov_3d && had_visible_open_air && z < center.z() - fov_3d_z_range ) ) {
                        if( !drew_occluded_overlay ) {
                            drew_occluded_overlay = true;
                            // Draw a depth-faded semi-transparent overlay for the topmost occluded
                            // tile.
                            const tile_search_params
                            dark_tile{"lighting_lowlight_dark", C_LIGHTING, empty_string, 0, 0};
                            draw_from_id_string(
                                dark_tile, pos, std::nullopt, std::nullopt, lit_level::LIT, false,
                                center.z() - z, false );
                        }
                    }
                }

                const auto low_override = draw_below_override.find( pos );
                const bool low_overridden = low_override != draw_below_override.end();
                if( low_overridden
                    ? !low_override->second
                    : ( in_map_bounds && ( here.dont_draw_lower_floor( pos ) || stop_on_memory ) )
                    || ( !in_map_bounds && ( has_memory || pos.z() <= 0 ) ) ) {
                    // invisible to normal eyes
                    bool invisible[5];
                    invisible[0] = false;

                    if( !in_vis_bounds ) {
                        if( has_memory ) {
                            ll = lit_level::MEMORIZED;
                            invisible[0] = true;
                        } else if( has_draw_override( pos ) ) {
                            ll = lit_level::DARK;
                            invisible[0] = true;
                        } else {
                            apply_vision_effects( pos, offscreen_type );
                            break;
                        }
                    }

                    const auto height_3d =
                        ( pos.z() - center.z() ) * tileset_ptr->get_zlevel_height();

                    const auto render_seen_through_air =
                        fov_3d && had_visible_open_air && in_map_bounds
                        && z < center.z() - fov_3d_z_range;

                    for( int i = 0; i < 4; i++ ) {
                        const tripoint np = pos.raw() + neighborhood[i];
                        invisible[1 + i] =
                            np.y < min_visible_y || np.y > max_visible_y || np.x < min_visible_x
                            || np.x > max_visible_x
                            || ( !render_seen_through_air
                                 && would_apply_vision_effects( here.get_visibility(
                                         ch.visibility_cache[ch.idx( np.x, np.y )], cache ) ) );
                    }

                    if( !invisible[0] && apply_vision_effects( pos, visibility ) ) {
                        // Vehicle tiles are blocked from the 3D FOV by their own floor cache,
                        // but are physically visible when the player descends through open air.
                        // Check this before the has_memory branch so that a memorized roof tile
                        // doesn't suppress the live visible render on subsequent frames.
                        if( had_visible_open_air && in_map_bounds && here.veh_at( pos ).has_value() ) {
                            const auto& ch_above = here.access_cache( pos.z() + 1 );
                            const lit_level above_ll =
                                ch_above.inbounds( {pos.x(), pos.y()} )
                                ? ch_above.visibility_cache[ch_above.idx( pos.x(), pos.y() )]
                                : lit_level::BLANK;
                            invisible[0] = !render_seen_through_air && above_ll == lit_level::BLANK;
                            const auto vehicle_ll =
                                above_ll != lit_level::BLANK ? above_ll
                                : render_seen_through_air
                                ? seen_through_air_light( pos )
                                : ll;
                            if( render_seen_through_air ) { here.set_memory_seen_cache_dirty( pos ); }
                            min_z = std::min( pos.z(), min_z );
                            queue_draw_point(
                                tile_render_info( pos, height_3d, vehicle_ll, invisible ) );
                        } else {
                            if( render_seen_through_air ) {
                                here.set_memory_seen_cache_dirty( pos );
                                min_z = std::min( pos.z(), min_z );
                                queue_draw_point( tile_render_info(
                                                      pos, height_3d, seen_through_air_light( pos ), invisible ) );
                                break;
                            }
                            if( has_draw_override( pos ) || has_memory ) { invisible[0] = true; }
                            for( int cz = pos.z(); !invisible[0] && cz <= -center.z(); cz++ ) {
                                const Creature* critter = g->critter_at( {pos.xy(), cz}, true );
                                if( critter
                                    && ( g->u.sees_with_infrared( *critter )
                                         || g->u.sees_with_specials( *critter ) ) ) {
                                    invisible[0] = true;
                                }
                            }
                            if( invisible[0] ) {
                                min_z = std::min( pos.z(), min_z );
                                queue_draw_point( tile_render_info( pos, height_3d, ll, invisible ) );
                            } else if( last_vis != center.z() + 1 ) {
                                if( fov_3d && in_map_bounds && z < center.z() - fov_3d_z_range ) {
                                    here.set_memory_seen_cache_dirty( pos );
                                    min_z = std::min( pos.z(), min_z );
                                    queue_draw_point( tile_render_info(
                                                          pos, height_3d, seen_through_air_light( pos ), invisible ) );
                                } else {
                                    min_z = std::min( last_vis, min_z );
                                    queue_draw_point( tile_render_info(
                                                          tripoint_bub_ms( pos.xy(), last_vis ), height_3d, last_vis_ll,
                                                          invisible ) );
                                }
                            } else if( had_visible_open_air && in_map_bounds ) {
                                // No vehicle and no solid last_vis — placeholder so cross-z
                                // sprite draws (player character above) still execute.
                                min_z = std::min( pos.z(), min_z );
                                invisible[0] = true;
                                queue_draw_point( tile_render_info( pos, height_3d, ll, invisible ) );
                            }
                        }

                    } else {
                        min_z = std::min( pos.z(), min_z );
                        queue_draw_point( tile_render_info( pos, height_3d, ll, invisible ) );
                    }
                    break;
                }
            }
        }
    }

    const auto base_drawing_layers =
        std::array{&cata_tiles::draw_furniture, &cata_tiles::draw_graffiti, &cata_tiles::draw_trap};
    const auto final_drawing_layers =
        std::array{&cata_tiles::draw_zone_mark, &cata_tiles::draw_zombie_revival_indicators};

    const auto draw_zone_overlay_for = [&]( const tile_render_info & p ) {
        if( p.pos.z() != center.z() ) { return; }
        const auto screen_tl = player_to_screen( p.pos.xy() );
        const auto tile_rect = SDL_Rect{screen_tl.x, screen_tl.y, tile_width, tile_height};
        const auto in_selected_zone =
            has_selected_zone && p.pos.z() == selected_z
            && ( has_custom_selected_zone
                 ? zone_point_lookup.contains( p.pos )
                 : ( p.pos.x() >= selected_min.x() && p.pos.x() <= selected_max.x()
                     && p.pos.y() >= selected_min.y() && p.pos.y() <= selected_max.y() ) );
        auto selected_drawn = false;
        if( show_zones_overlay ) {
            for( const zone_render_data& zone : zones_to_draw ) {
                if( !zone.tiles.contains( p.pos.xy() ) ) { continue; }
                draw_zone_overlay( {
                    .rect = tile_rect,
                    .color = zone.color,
                    .overlay_strings = overlay_strings,
                    .alpha = in_selected_zone ? 128 : 64,
                    .draw_label = false} );
                selected_drawn = selected_drawn || in_selected_zone;
            }
        }
        if( in_selected_zone && !selected_drawn ) {
            draw_zone_overlay( {
                .rect = tile_rect,
                .color = curses_color_to_SDL( c_light_green ),
                .overlay_strings = overlay_strings,
                .alpha = 128,
                .draw_label = false} );
        }
    };

    if( !draw_points.empty() ) {
        for( const auto z : std::views::iota( min_z, center.z() + 1 ) ) {
            // ---- Pass 1: per-row terrain + base layers (unchanged) ----
            auto row_begin = draw_points.begin();
            while( row_begin != draw_points.end() ) {
                const auto row = row_begin->screen_row;
                const auto row_end = std::
                find_if_not( row_begin, draw_points.end(), [row]( const tile_render_info & info ) {
                    return info.screen_row == row;
                } );
                const auto row_points = std::ranges::subrange( row_begin, row_end );
                for( tile_render_info& p : row_points ) {
                    if( p.pos.z() == z ) {
                        draw_terrain( p.pos, p.ll, p.height_3d, p.invisible, center.z() - p.pos.z() );
                        draw_zone_overlay_for( p );
                    }
                }
                for( tile_render_info& p : row_points ) {
                    if( p.pos.z() == z ) {
                        for( const auto f : base_drawing_layers ) {
                            ( this->*f )( p.pos, p.ll, p.height_3d, p.invisible,
                                          center.z() - p.pos.z() );
                        }
                    }
                }
                row_begin = row_end;
            }

            // ---- Splatmap cut: the terrain→entity boundary --------------------
            // Pass 1 (terrain + base layers) has just finished for this z, and
            // Pass 2 below queues every ground entity, so this is the only clean
            // seam at which the decal composite can land over terrain and under
            // entities. Only the player's z composites, and the z loop ascends to
            // center.z(), so the last iteration wins and the index is unambiguous.
            if( z == center.z() && splatmap::active() ) {
                record_splat_frame(
                    draw_points, z, here.get_abs_sub(), tile_width, tile_height,
                    lighting::get_render_state().tile_sprite_count(),
                    [this]( point_bub_ms p ) { return player_to_screen( p ); } );
            }
            // ---- Pass 2: ground entities (field_or_item, vpart, in row order) + creature
            // collection ----
            const auto& ch = here.access_cache( z );
            std::vector<creature_draw_job> entity_jobs;
            auto gr_begin = draw_points.begin();
            while( gr_begin != draw_points.end() ) {
                const auto gr_row = gr_begin->screen_row;
                const auto gr_end = std::find_if_not(
                                        gr_begin, draw_points.end(),
                [gr_row]( const tile_render_info & info ) { return info.screen_row == gr_row; } );
                const auto gr_points = std::ranges::subrange( gr_begin, gr_end );
                for( tile_render_info& p : gr_points ) {
                    if( p.pos.z() > z ) { continue; }
                    const auto draw_at = tripoint_bub_ms( p.pos.xy(), z );
                    const bool cross_z = here.inbounds( p.pos ) && z != p.pos.z();
                    lit_level z_ll = p.ll;
                    bool( invis )[5];
                    std::copy( p.invisible, p.invisible + 5, invis );
                    if( cross_z ) {
                        z_ll = ch.inbounds( {p.pos.x(), p.pos.y()} )
                               ? ch.visibility_cache[ch.idx( p.pos.x(), p.pos.y() )]
                               : lit_level::BLANK;
                        std::fill_n( invis, 5, false );
                    }
                    // draw_field_or_item (gated: hide_unseen)
                    if( !cross_z || z_ll != lit_level::BLANK ) {
                        draw_field_or_item( draw_at, z_ll, p.height_3d, invis, center.z() - z );
                    }
                    // draw_vpart (ungated)
                    draw_vpart( draw_at, z_ll, p.height_3d, invis, center.z() - z );
                    // Collect creature (if present at this draw position)
                    const auto override = monster_override.find( draw_at );
                    if( override != monster_override.end() ) {
                        const mtype_id id = std::get<0>( override->second );
                        if( id ) {
                            const point screen_pt = player_to_screen( draw_at.xy() );
                            entity_jobs.push_back( creature_draw_job{
                                .pos = draw_at,
                                .ll = z_ll,
                                .height_3d = &p.height_3d,
                                .invisible = {invis[0], invis[1], invis[2], invis[3], invis[4]},
                                .z_drop = center.z() - z,
                                .creature = nullptr,
                                .xform = {},
                                .sort_key = static_cast<float>( screen_pt.y + tile_height )} );
                        }
                    } else {
                        const Creature* pcritter = g->critter_at( draw_at, true );
                        if( pcritter ) {
                            const sprite_xform xform = compute_anim_xform( *pcritter );
                            const point screen_pt = player_to_screen( draw_at.xy() );
                            entity_jobs.push_back( creature_draw_job{
                                .pos = draw_at,
                                .ll = z_ll,
                                .height_3d = &p.height_3d,
                                .invisible = {invis[0], invis[1], invis[2], invis[3], invis[4]},
                                .z_drop = center.z() - z,
                                .creature = pcritter,
                                .xform = xform,
                                .sort_key =
                                static_cast<float>( screen_pt.y + tile_height ) + xform.off_y} );
                        }
                    }
                }
                gr_begin = gr_end;
            }
            // ---- Pass 3: y-sort and draw creatures (GROUND_ENTITIES → sorted ENTITIES) ----
            std::stable_sort(
                entity_jobs.begin(), entity_jobs.end(),
            []( const creature_draw_job & a, const creature_draw_job & b ) {
                return a.sort_key < b.sort_key;
            } );
            for( auto& job : entity_jobs ) {
                if( job.creature ) {
                    prefetch_critter_ = job.creature;
                    prefetch_xform_ = job.xform;
                    prefetch_valid_ = true;
                }
                draw_critter_at( job.pos, job.ll, *job.height_3d, job.invisible, job.z_drop );
                prefetch_valid_ = false;
            }
        }
    }
    for( tile_render_info& p : draw_points ) {
        for( const auto f : final_drawing_layers ) {
            ( this->*f )( p.pos, p.ll, p.height_3d, p.invisible, 0 );
        }
    }

    // display number of monsters to spawn in mapgen preview
    for( const tile_render_info& p : draw_points ) {
        const auto mon_override = monster_override.find( p.pos );
        if( mon_override != monster_override.end() ) {
            const int count = std::get<1>( mon_override->second );
            const bool more = std::get<2>( mon_override->second );
            if( count > 1 || more ) {
                std::string text = "x" + std::to_string( count );
                if( more ) { text += "+"; }
                overlay_strings.emplace(
                    player_to_screen( p.pos.xy() ) + point( tile_width / 2, 0 ),
                    formatted_text( text, catacurses::red, direction::NORTH ) );
            }
        }
    }
    // tile overrides are already drawn in the previous code

    // ── Colored light overlay ───────────────────────────────────────────────
    // Draws a tinted rect over tiles that have colored light energy in the cache.
    // Only the chromatic (saturated) component produces a tint — white light
    // (equal RGB) is ignored. Alpha scales with the ratio of saturated energy
    // to total scalar light so the effect is subtle under bright ambient and
    // vivid in darkness. Disabled in isometric mode.
    if( !iso_mode ) {
        for( int z = min_z; z <= center.z(); ++z ) {
            const auto& zlev_cache = here.access_cache( z );
            if( !zlev_cache.has_colored_lights ) { continue; }
            for( const tile_render_info& p : draw_points ) {
                if( p.pos.z() != z ) { continue; }
                // Only visible sprite tiles can receive a tint.
                if( p.ll == lit_level::DARK || p.ll == lit_level::BLANK
                    || p.ll == lit_level::MEMORIZED ) {
                    continue;
                }
                const light_color_rgb& lc =
                    zlev_cache.light_color_cache[zlev_cache.idx( p.pos.x(), p.pos.y() )];
                if( !lc.is_colored() ) { continue; }
                // Subtract the achromatic (white) component to isolate saturated color.
                const float min_ch = std::min( {lc.r, lc.g, lc.b} );
                const float sat_r = lc.r - min_ch;
                const float sat_g = lc.g - min_ch;
                const float sat_b = lc.b - min_ch;
                const float sat_mag = std::max( {sat_r, sat_g, sat_b} );
                if( sat_mag < 0.01f ) {
                    continue; // pure white light, no tint
                }
                // Alpha: saturated energy relative to total scalar light at this tile.
                const four_quadrants& lm_val = zlev_cache.lm[zlev_cache.idx( p.pos.x(), p.pos.y() )];
                const float scalar = lm_val.max();
                const float ratio = scalar > 0.1f ? std::min( 1.0f, sat_mag / scalar ) : 0.0f;
                const Uint8 alpha = static_cast<Uint8>( ratio * 80.0f );
                if( alpha == 0 ) { continue; }
                // Normalize saturated color to full brightness for the SDL tint.
                const SDL_Color tint = {
                    static_cast<Uint8>( sat_r / sat_mag * 255.0f ),
                    static_cast<Uint8>( sat_g / sat_mag * 255.0f ),
                    static_cast<Uint8>( sat_b / sat_mag * 255.0f ), alpha
                };
                const point screen = player_to_screen( p.pos.xy() );
                const SDL_FRect draw_rect{
                    static_cast<float>( screen.x ), static_cast<float>( screen.y - p.height_3d ),
                    static_cast<float>( tile_width ), static_cast<float>( tile_height )};
                SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_BLEND );
                geometry->rect( renderer, draw_rect, tint );
                SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_NONE );
            }
        }
    }

    // ── Dawn/dusk warm color temperature overlay ────────────────────────────
    // During twilight, render a warm hue shift on outside tiles that have sunlight.
    if( ( is_dusk( calendar::turn ) || is_dawn( calendar::turn ) ) && center.z() >= 0 ) {
        const light_color_rgb ddc = dawn_dusk_color_for_lightmap( g->get_dimension_prefix() );
        if( ddc.is_colored() ) {
            for( int z = min_z; z <= center.z(); ++z ) {
                const auto& zlev_cache = here.access_cache( z );
                if( !zlev_cache.has_colored_lights ) { continue; }
                SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_BLEND );
                for( const tile_render_info& p : draw_points ) {
                    if( p.pos.z() != z ) { continue; }
                    // Only outside tiles with sunlight get the dawn/dusk tint.
                    if( !zlev_cache.outside_cache[zlev_cache.idx( p.pos.x(), p.pos.y() )] ) {
                        continue;
                    }
                    const point screen = player_to_screen( p.pos.xy() );
                    const SDL_FRect draw_rect{
                        static_cast<float>( screen.x ), static_cast<float>( screen.y - p.height_3d ),
                        static_cast<float>( tile_width ), static_cast<float>( tile_height )};
                    const SDL_Color ddc_color = {
                        static_cast<Uint8>( std::min( 255.0f, ddc.r * 255.0f ) ),
                        static_cast<Uint8>( std::min( 255.0f, ddc.g * 255.0f ) ),
                        static_cast<Uint8>( std::min( 255.0f, ddc.b * 255.0f ) ), 255
                    };
                    geometry->rect( renderer, draw_rect, ddc_color );
                }
                SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_NONE );
            }
        }
    }

    void_radiation_override();
    void_terrain_override();
    void_furniture_override();
    void_graffiti_override();
    void_trap_override();
    void_field_override();
    void_item_override();
    void_vpart_override();
    void_draw_below_override();
    void_monster_override();

    // Memorize everything the character just saw even if it wasn't displayed.
    for( int mem_y = min_visible_y; mem_y <= max_visible_y; mem_y++ ) {
        for( int mem_x = min_visible_x; mem_x <= max_visible_x; mem_x++ ) {
            half_open_rectangle<point>
            already_drawn( point( min_col, min_row ), point( max_col, max_row ) );
            if( iso_mode ) {
                // calculate the screen position according to the drawing code above (division
                // rounded down):

                // mem_x = ( col - row - sx / 2 + sy / 2 ) / 2 + o.x;
                // mem_y = ( row + col - sy / 2 - sx / 2 ) / 2 + o.y;
                // ( col - sx / 2 ) % 2 = ( row - sy / 2 ) % 2
                // ||
                // \/
                const int col = mem_y + mem_x + s.x / 2 - o.y() - o.x();
                const int row = mem_y - mem_x + s.y / 2 - o.y() + o.x();
                if( already_drawn.contains( point( col, row ) ) ) { continue; }
            } else {
                // calculate the screen position according to the drawing code above:

                // mem_x = col + o.x
                // mem_y = row + o.y
                // ||
                // \/
                // col = mem_x - o.x
                // row = mem_y - o.y
                if( already_drawn.contains( point( mem_x, mem_y ) - o.raw() ) ) { continue; }
            }

            const auto& _cz = here.access_cache( center.z() );
            lit_level lighting = _cz.visibility_cache[_cz.idx( mem_x, mem_y )];

            int z = center.z();
            for( ; z > -OVERMAP_DEPTH; z-- ) {
                const auto low_override = draw_below_override.find( {mem_x, mem_y, z} );
                const bool low_overridden = low_override != draw_below_override.end();
                const auto& _cur = here.access_cache( z );
                const auto& _lower = here.access_cache( z - 1 );
                if( low_overridden
                    ? !low_override->second
                    : ( here.dont_draw_lower_floor( {mem_x, mem_y, z} )
                        || ( fov_3d && lighting != lit_level::BLANK
                             && _lower.visibility_cache[_lower.idx( mem_x, mem_y )]
                             == lit_level::BLANK ) ) ) {
                    if( fov_3d ) { lighting = _cur.visibility_cache[_cur.idx( mem_x, mem_y )]; }
                    break;
                }
            }


            const auto& ch = here.access_cache( z );
            const tripoint_bub_ms p( mem_x, mem_y, z );

            if( apply_vision_effects( p, here.get_visibility( lighting, cache ) ) ) { continue; }
            int height_3d = 0;
            bool invisible[5];
            invisible[0] = false;
            for( int i = 0; i < 4; i++ ) {
                const auto np = p + neighborhood[i];
                invisible[1 + i] =
                    np.y() < min_visible_y || np.y() > max_visible_y || np.x() < min_visible_x
                    || np.x() > max_visible_x
                    || would_apply_vision_effects(
                        here.get_visibility( ch.visibility_cache[ch.idx( np.x(), np.y() )], cache ) );
            }
            // calling draw to memorize everything.
            // bypass cache check in case we learn something new about the terrain's connections
            draw_terrain( p, lighting, height_3d, invisible, 0 );
            if( here.check_seen_cache( p ) ) {
                draw_furniture( p, lighting, height_3d, invisible, 0 );
                draw_trap( p, lighting, height_3d, invisible, 0 );
                draw_vpart( p, lighting, height_3d, invisible, 0 );
                here.check_and_set_seen_cache( p );
            }
        }
    }

    in_animation =
        do_draw_explosion || do_draw_custom_explosion
        || do_draw_line || do_draw_cursor || do_draw_highlight || do_draw_weather || do_draw_sct
        || do_draw_zones || do_draw_cone_aoe || do_draw_aim_cone || do_draw_aim_crosshair
        || !particles_.idle() || hover_tile_.has_value();

    draw_footsteps_frame( center );
    if( in_animation ) {
        if( do_draw_explosion ) { draw_explosion_frame(); }
        if( do_draw_custom_explosion ) { draw_custom_explosion_frame(); }
        if( !particles_.idle() ) {
            particles_.update( anim_wall_now_ );
            for( const auto &p : particles_.active() ) {
                if( p.style != particle_style::sprite ) {
                    draw_particle_overlay( p );
                    continue;
                }
                if( !tile_iso ) {
                    active_anim_xform_ = sprite_xform{
                        .off_x = p.off_x * static_cast<float>( tile_width ),
                        .off_y = p.off_y * static_cast<float>( tile_height ),
                        .alpha = p.alpha };
                }
                const tile_search_params tile{ p.sprite, C_BULLET, empty_string, 0, p.rotation };
                draw_from_id_string(
                    tile, p.tile, std::nullopt, std::nullopt, lit_level::LIT, false, 0, false );
                active_anim_xform_ = {};
            }
        }
        if( do_draw_line ) {
            draw_line();
            void_line();
        }
        if( do_draw_weather ) {
            draw_weather_frame();
            void_weather();
        }
        if( do_draw_sct ) {
            draw_sct_frame( overlay_strings );
            void_sct();
        }
        if( do_draw_zones ) { void_zones(); }
        if( do_draw_cursor ) {
            draw_cursor();
            void_cursor();
        }
        if( do_draw_highlight ) {
            draw_highlight();
            void_highlight();
        }
        draw_hover_effect();
        if( do_draw_cone_aoe ) { draw_cone_aoe_frame(); }
        if( do_draw_throw_arc ) { draw_throw_arc(); }
        if( do_draw_throw_impact ) { draw_throw_impact(); }
        if( do_draw_aim_cone ) { draw_aim_cone(); }
        // Own branch, after the cone: it used to be a tail call inside draw_cursor(),
        // which both hid it behind the cone and tied it to do_draw_cursor — false
        // during hold-to-aim, so the reticle silently vanished in the one mode that
        // needs it most.
        if( do_draw_aim_crosshair ) { draw_aim_crosshair(); }
    } else if( g->u.view_offset != tripoint_rel_ms::zero() && !g->u.in_vehicle ) {
        // check to see if player is located at ter
        const tile_search_params tile{"cursor", C_NONE, empty_string, 0, 0};
        const auto pos = tripoint_bub_ms( g->ter_view_p.xy(), center.z() );
        draw_from_id_string( tile, pos, std::nullopt, std::nullopt, lit_level::LIT, false, 0, false );
    }
    if( g->u.controlling_vehicle ) {
        if( auto indicator_offset = g->get_veh_dir_indicator_location( true ) ) {
            const tile_search_params tile{"cursor", C_NONE, empty_string, 0, 0};
            const auto pos =
                indicator_offset->xy()
                + tripoint_bub_ms( g->u.bub_pos().x(), g->u.bub_pos().y(), center.z() );
            draw_from_id_string(
                tile, pos, std::nullopt, std::nullopt, lit_level::LIT, false, 0, false );
        }
    }

    const bool draw_submap_grid =
        g->debug_submap_grid_overlay
        || ( g->is_zones_manager_open() && g->is_zone_submap_grid_overlay_enabled() );

    if( draw_submap_grid && !iso_mode ) {
        point_abs_sm sm_start = project_to<coords::sm>(
                                    here.bub_to_abs( point_bub_ms( min_col, min_row ) + o.raw() ) );
        point_abs_sm sm_end = project_to<coords::sm>(
                                  here.bub_to_abs( point_bub_ms( max_col, max_row ) + o.raw() ) );

        bool zlevs = here.has_zlevels();
        int mapsize = here.getmapsize();
        auto mappos = here.get_abs_sub();
        half_open_rectangle<point>
        maprect( mappos.xy().raw(), mappos.xy().raw() + point( mapsize, mapsize ) );

        const auto is_map = [mappos, zlevs, maprect]( const tripoint & p ) {
            if( !maprect.contains( p.xy() ) ) { return false; }
            if( zlevs ) {
                return true;
            } else {
                return p.z == mappos.z();
            }
        };

        const auto is_mapbuffer = []( const tripoint_abs_sm & p ) {
            return ACTIVE_MAPBUFFER.is_submap_loaded( p );
        };

        const auto& dim = here.get_bound_dimension();

        constexpr int THICC = 1; // line thickness
        for( int sm_x = sm_start.x(); sm_x <= sm_end.x(); sm_x++ ) {
            for( int sm_y = sm_start.y(); sm_y <= sm_end.y(); sm_y++ ) {
                auto sm_p = point_abs_sm( sm_x, sm_y );
                auto sm_tp = tripoint_abs_sm( sm_x, sm_y, center.z() );
                point p1 = player_to_screen( here.abs_to_bub( project_to<coords::ms>( sm_p ) ) );
                point p3 = player_to_screen(
                               here.abs_to_bub( project_to<coords::ms>( sm_p + point_south_east ) ) );
                p3 -= point( THICC, THICC ); // Don't draw over other lines

                // Leave a small gap to indicate omt boundaries
                auto tmp = project_to<coords::sm>( project_to<coords::omt>( sm_tp ) ).xy();
                if( tmp.x() == sm_tp.x() ) { p1.x += 2; }
                if( tmp.y() == sm_tp.y() ) { p1.y += 2; }

                // Green  = in map grid (reality bubble)
                // Cyan   = actively simulated but not in grid (fire, power cable, etc.)
                // Red    = in MAPBUFFER but not simulated (lazy border / streamer pre-load)
                // Blue   = not loaded
                SDL_Color col;
                if( is_map( sm_tp.raw() ) ) {
                    col = {0, 220, 0, 255};
                } else if( submap_loader.is_simulated( dim, tripoint_abs_sm( sm_tp ) ) ) {
                    col = {0, 180, 180, 255};
                } else if( is_mapbuffer( sm_tp ) ) {
                    col = {220, 0, 0, 255};
                } else {
                    col = {0, 0, 220, 255};
                }

                geometry->vertical_line( renderer, p1, p3.y, THICC, col );
                geometry->vertical_line( renderer, point( p3.x, p1.y ), p3.y, THICC, col );
                geometry->horizontal_line( renderer, p1, p3.x, THICC, col );
                geometry->horizontal_line( renderer, point( p1.x, p3.y ), p3.x, THICC, col );
            }
        }
    }

    if( show_zones_overlay ) {
        for( const zone_render_data& zone : zones_to_draw ) {
            if( zone.name.empty() ) { continue; }

            if( zone.tiles.empty() ) { continue; }

            const auto min_x = std::ranges::
            minmax_element( zone.tiles, {}, []( const point_bub_ms & p ) { return p.x(); } );
            const auto min_y = std::ranges::
            minmax_element( zone.tiles, {}, []( const point_bub_ms & p ) { return p.y(); } );
            const point_bub_ms min_local( min_x.min->x(), min_y.min->y() );
            const point_bub_ms max_local( min_x.max->x(), min_y.max->y() );

            const point screen_tl = player_to_screen( ( min_local + zone_offset.xy() ) );
            const point screen_br =
                player_to_screen( ( max_local + zone_offset.xy() ) ) + point( tile_width, tile_height );
            const point center_pt(
                screen_tl.x + ( screen_br.x - screen_tl.x ) / 2,
                screen_tl.y + ( screen_br.y - screen_tl.y ) / 2 );

            overlay_strings.emplace(
                center_pt, formatted_text( zone.name, catacurses::white, text_alignment::center ) );
        }
    }

    // Box2D debug overlay — populates the GPU debug_line_pass with collision
    // shapes, contact manifolds, and body transforms.  The actual GPU draw
    // happens later in render_world_pass_w.  Skip in iso mode (projection differs).
    if( !tile_iso ) {
        auto &dl = lighting::get_render_state().debug_lines();
        dl.clear();
        if( auto *pw = g ? g->m.get_physics_world() : nullptr;
            pw && pw->debug_draw_enabled() ) {
            pw->draw_debug( dl );
        }
    }
    lighting::get_render_state().clear_tile_scissor();
}

bool cata_tiles::terrain_requires_animation() const
{
    return idle_animations.enabled() && idle_animations.present();
}

void cata_tiles::display_character( const Character& ch, const point_bub_ms& p )
{
    int height_3d = 0;
    draw_entity_with_overlays( ch, tripoint_bub_ms( p, 0 ), lit_level::BRIGHT, height_3d, true );
}

void cata_tiles::get_window_tile_counts(
    const int width, const int height, int &columns, int &rows ) const
{
    if( tile_iso ) {
    columns = std::ceil( static_cast<double>( width ) / tile_width ) * 2 + 4;
        rows = std::ceil( static_cast<double>( height ) / ( tile_width / 2.0 - 1 ) ) * 2 + 4;
    } else {
        columns = std::ceil( static_cast<double>( width ) / tile_width );
        rows = std::ceil( static_cast<double>( height ) / tile_height );
    }
}


bool cata_tiles::draw_from_id_string(
    const tile_search_params& tile, const tripoint_bub_ms& pos, const tint_config& bg_tint,
    const tint_config& fg_tint, lit_level ll, bool apply_visual_effects, int overlay_count,
    const bool as_independent_entity, int &height_3d, float sway )
{
    // If the ID string does not produce a drawable tile
    // it will revert to the "unknown" tile.
    // The "unknown" tile is one that is highly visible so you kinda can't miss it :D

    // check to make sure that we are drawing within a valid area
    // [0->width|height / tile_width|height]

    half_open_rectangle<point_bub_ms>
    screen_bounds( o, o + point( screentile_width, screentile_height ) );
    if( !as_independent_entity && !tile_iso && !screen_bounds.contains( pos.xy() ) ) { return false; }

    auto retract = 0;
    if( !as_independent_entity && ( prevent_occlusion_retract || prevent_occlusion_transp ) ) {
        if( prevent_occlusion == 1 ) {
            retract = 100;
        } else if( prevent_occlusion == 2 ) {
            const auto screen_center =
                point( static_cast<int>( o.x() + screentile_width / 2.0f ),
                       static_cast<int>( o.y() - 1 + screentile_height / 2.0f ) );
            const auto distance =
                tile_iso ? rl_dist( o, pos.xy() ) : rl_dist( screen_center, pos.xy().raw() );
            const auto d_min =
                prevent_occlusion_min_dist > 0.0f
                ? prevent_occlusion_min_dist
                : tileset_ptr->get_prevent_occlusion_min_dist();
            const auto d_max =
                prevent_occlusion_max_dist > 0.0f
                ? prevent_occlusion_max_dist
                : tileset_ptr->get_prevent_occlusion_max_dist();
            const auto d_range = d_max - d_min;
            const auto d_slope = d_range <= 0.0f ? 100.0f : 1.0f / d_range;
            const auto distance_factor = ( static_cast<float>( distance ) - d_min ) * d_slope;
            retract = static_cast<int>( 100.0f * ( 1.0f - std::clamp( distance_factor, 0.0f, 1.0f ) ) );
        }
    }

    // Trying to search for tile type
    auto search_result =
        prevent_occlusion_transp && retract > 0 && tile.category != C_OVERMAP_TERRAIN
    ? tile_type_search( tile_search_params{
        tile.id + "_transparent", tile.category, tile.subcategory, tile.subtile,
        tile.rota} )
        : std::optional<tile_search_result> {};
    if( search_result == std::nullopt ) { search_result = tile_type_search( tile ); }
    if( search_result == std::nullopt ) { return false; }
    if( !prevent_occlusion_retract ) { retract = 0; }

    const tile_type* tt = search_result.value().tt;
    std::string found_id = search_result.value().found_id;

    const tile_type& display_tile = *tt;
    // check to see if the display_tile is multitile, and if so if it has the key related to subtile
    if( tile.subtile != -1 && display_tile.multitile ) {
        const auto& display_subtiles = display_tile.available_subtiles;
        const auto end = std::end( display_subtiles );
        if( std::find( begin( display_subtiles ), end, multitile_keys[tile.subtile] ) != end ) {
            // append subtile name to tile and re-find display_tile
            const tile_search_params multi_tile = {
                found_id + "_" + multitile_keys[tile.subtile], tile.category, tile.subcategory, -1,
                tile.rota
            };
            return draw_from_id_string(
                       multi_tile, pos, bg_tint, fg_tint, ll, apply_visual_effects, overlay_count,
                       as_independent_entity, height_3d );
        }
    }

    // translate from player-relative to screen relative tile position
    const auto screen_pos =
        as_independent_entity ? pos.xy() : point_bub_ms( player_to_screen( pos.xy() ) );

    // Step 2 (grid-decoupled lighting): publish the map tile + its screen square for
    // push_occluder_footprint, which runs deep inside draw_sprite_at where only the
    // screen position is available. `as_independent_entity` sprites are not anchored
    // to a map tile at all, so poison the tile to keep a stale value from leaking.
    occluder_tile_ = as_independent_entity ? tripoint_bub_ms( -1, -1, 0 ) : pos;
    occluder_tile_screen_ = screen_pos.raw();

    auto simple_point_hash = []( const auto & p ) { return p.x + p.y * 65536; };

    // Remove this once the tripoint migration is complete
    auto simple_point_hash_new = []( const auto & p ) { return p.x() + p.y() * 65536; };

    bool has_variations = display_tile.sprite.fg.size() > 1 || display_tile.sprite.bg.size() > 1;
    bool variations_enabled = !display_tile.animated || idle_animations.enabled();
    // with animated tiles, seed is used for stagger
    bool seed_for_animation = has_variations && variations_enabled && display_tile.animated;
    bool seed_from_map_coords = false;
    int true_rota = tile.rota;

    // seed the PRNG to get a reproducible random int
    // TODO: faster solution here
    unsigned int seed = 0;
    map& here = get_map();
    // TODO: determine ways other than category to differentiate more types of sprites
    switch( tile.category ) {
        case C_TERRAIN:
        case C_FIELD:
        case C_LIGHTING:
            // stationary map tiles, seed based on map coordinates
            seed_from_map_coords = true;
            break;
        case C_VEHICLE_PART:
            // vehicle parts, seed based on coordinates within the vehicle
            // TODO: also use some vehicle id, for less predictability
        {
            // new scope for variable declarations
            const auto vp_override = vpart_override.find( pos );
            const bool vp_overridden = vp_override != vpart_override.end();
            if( vp_overridden ) {
                const vpart_id& vp_id = std::get<0>( vp_override->second );
                if( vp_id ) {
                    point mount = std::get<4>( vp_override->second );
                    seed = simple_point_hash( mount );
                }
            } else {
                const optional_vpart_position vp = here.veh_at( tripoint_bub_ms( pos ) );
                if( vp ) { seed = simple_point_hash_new( vp->mount() ); }
            }

            // convert vehicle 360-degree direction (0=E,45=SE, etc) to 4-way tile
            // rotation (0=N,1=W,etc)
            tileray face = tileray( units::from_degrees( true_rota ) );
            true_rota = 3 - face.dir4();
        }
        break;
        case C_FURNITURE: {
            // If the furniture is not movable, we'll allow seeding by the position
            // since we won't get the behavior that occurs where the tile constantly
            // changes when the player grabs the furniture and drags it, causing the
            // seed to change.
            const furn_str_id fid( found_id );
            if( fid.is_valid() ) {
                const furn_t &f = fid.obj();
                if( !f.is_movable() ) { seed = simple_point_hash_new( here.bub_to_abs( pos ) ); }
            }
        }
        break;
        case C_ITEM:
        case C_TRAP:
            if( seed_for_animation ) { seed_from_map_coords = true; }
            // TODO: come up with ways to make random sprites consistent for these types
            break;
        case C_OVERMAP_TERRAIN:
            seed = simple_point_hash_new( pos );
            break;
        case C_NONE:
        case C_BULLET:
        case C_HIT_ENTITY:
        case C_WEATHER:
            // TODO: come up with ways to make random sprites consistent for these types
            break;
        case C_MONSTER:
            // FIXME: add persistent id to Creature type, instead of using monster pointer address
            if( !monster_override.contains( pos ) ) {
                seed = reinterpret_cast<uintptr_t>( g->critter_at<monster>( tripoint_bub_ms( pos ) ) );
            }
            break;
        default:
            // player
            if( found_id.starts_with( "player_" ) ) {
                seed = g->u.name[0];
                break;
            }
            // NPC
            if( found_id.starts_with( "npc_" ) ) {
                if( npc * const guy = g->critter_at<npc>( tripoint_bub_ms( pos ) ) ) {
                    seed = guy->getID().get_value();
                    break;
                }
            }
    }

    // make sure we aren't going to rotate the tile if it shouldn't be rotated
    if( !display_tile.rotates && tile.category != C_NONE && tile.category != C_MONSTER
        && tile.category != C_BULLET ) {
        true_rota = 0;
    }

    unsigned int loc_rand = 0;
    // only bother mixing up a hash/random value if the tile has some sprites to randomly pick
    // between or has an idle animation and idle animations are enabled
    if( has_variations && variations_enabled ) {
        if( seed_from_map_coords ) { seed = simple_point_hash_new( g->m.bub_to_abs( pos ) ); }
        static const auto rot32 = []( const unsigned int x, const int k ) {
            return ( x << k ) | ( x >> ( 32 - k ) );
        };
        // use a fair mix function to turn the "random" seed into a random int
        // taken from public domain code at http://burtleburtle.net/bob/c/lookup3.c 2015/12/11
        unsigned int a = seed, b = -seed, c = seed * seed;
        c ^= b;
        c -= rot32( b, 14 );
        a ^= c;
        a -= rot32( c, 11 );
        b ^= a;
        b -= rot32( a, 25 );
        c ^= b;
        c -= rot32( b, 16 );
        a ^= c;
        a -= rot32( c, 4 );
        b ^= a;
        b -= rot32( a, 14 );
        c ^= b;
        c -= rot32( b, 24 );
        loc_rand = c;

        // idle tile animations:
        if( display_tile.animated ) {
            idle_animations.mark_present();
            // offset by loc_rand so that everything does not blink at the same time:
            int frame = idle_animations.current_frame() + loc_rand;
            int frames_in_loop = display_tile.sprite.fg.get_weight();
            if( frames_in_loop == 1 ) { frames_in_loop = display_tile.sprite.bg.get_weight(); }
            // loc_rand is actually the weighed index of the selected tile, and
            // for animations the "weight" is the number of frames to show the tile for:
            loc_rand = frame % frames_in_loop;
        }
    }

    // To make first layer of overlays more opaque and easy to distinguish
    overlay_count = overlay_count + ( overlay_count > 0 );
    // Overmap overlays usually have higher counts, so make them less opaque
    const int base_overlay_alpha = tile.category == C_OVERMAP_TERRAIN ? 12 : 24;

    // Overmap tiles are an UNLIT map view (sdf_map_w==0, no sun/sky/emitter).
    // The lit shader brightens game tiles via gpu_light (tint); with tint left
    // at a stale ~0 from the last in-game lit tile, max(tint, ambient=0.05)
    // collapses the overmap to texel*0.05 -> black. Force full-bright passthrough
    // (tint=1.0) up here so it applies to BOTH the transparent early-return path
    // below AND the normal path; the per-tile gpu_light=0 lit-branch at the
    // Phase-5 block skips C_OVERMAP_TERRAIN, so this is the overmap's final tint.
    if( tile.category == C_OVERMAP_TERRAIN ) { gpu_light_r = gpu_light_g = gpu_light_b = 1.0f; }

    // Let's branch transparent overmaps early if tranparency overlays are enabled
    // Because if tranparency is enabled then backgrounds should not be drawn
    if( tile.category == C_OVERMAP_TERRAIN && display_tile.has_om_transparency
        && overmap_transparency ) {
        draw_sprite_at(
            display_tile, screen_pos, loc_rand, /*fg:*/ true, true_rota, fg_tint, ll,
            apply_visual_effects, base_overlay_alpha * overlay_count, &height_3d, retract );
        return true;
    }

    // Phase 5: compute per-tile GPU light tint from lightmap color cache.
    // `pos` is in map tile coordinates; default to white (1,1,1) for UI tiles,
    // overmap tiles, out-of-bounds, or when the lightmap hasn't been generated
    // yet (lum == 0: main menu / first frame before generate_lightmap runs).
    // Phase 8: tint = 0 for game tiles so max(tint, gpu_light) = gpu_light.
    // GPU emitters + sky/sun are the sole brightness source for game tiles.
    // UI elements and main menu (g==nullptr or as_independent_entity) keep tint=1.0
    // so max(1.0, gpu_light) = 1.0 → full color passthrough regardless of lighting.
    gpu_light_r = gpu_light_g = gpu_light_b = 1.0f; // default: UI/main-menu passthrough
    if( !as_independent_entity && tile.category != C_OVERMAP_TERRAIN && g != nullptr ) {
        const map& here = get_map();
        if( here.inbounds( pos ) ) {
            const level_cache& mc = here.access_cache( pos.z() );
            const int idx = mc.idx( pos.x(), pos.y() );
            const float lum = mc.lm[idx].max();
            if( lum > 0.001f ) {
                // Tile IS lit by CPU lightmap → let GPU emitters drive brightness.
                // tint = 0 so max(0, gpu_light) = gpu_light (Stoneshard quality).
                gpu_light_r = gpu_light_g = gpu_light_b = 0.0f;
            }
            // lum == 0: lightmap not generated yet; keep tint=1.0 (safe white fallback).
        }
    }

    // Effect 3: memorized (out-of-sight but remembered) tiles carry
    // -(distance from player in tiles) as the sprite light_mul marker, so the
    // fragment shader dims + distance-fades remembered terrain (floored at
    // mem_dim → persists, never black). 0 = normal tile (no fade).
    gpu_light_mul = 0.0f;
    if( ll == lit_level::MEMORIZED && g != nullptr ) {
        gpu_light_mul = -static_cast<float>( trig_dist( g->u.bub_pos(), pos ) );
    }

    // draw it!
    draw_tile_at(
        display_tile, screen_pos, loc_rand, true_rota, bg_tint, fg_tint, ll, apply_visual_effects,
        height_3d, base_overlay_alpha * overlay_count, retract, sway );

    return true;
}

void cata_tiles::draw_om_tile_recursively(
    const tripoint_abs_omt omp, const std::string& id, int rotation, int subtile,
    int base_z_offset )
{
    const tile_search_params tile{id, C_OVERMAP_TERRAIN, "overmap_terrain", subtile, rotation};
    const auto tt = tile_type_search( tile );
    if( tt == std::nullopt ) { return; }

    if( tt->tt->has_om_transparency ) {
        // So current tile has transparent pixels, so we need to render below one first
        const tripoint_abs_omt new_pos = omp + tripoint( 0, 0, -1 );
        int new_rotation = 0, new_subtile = 0;
        const std::string new_id =
            get_omt_id_rotation_and_subtile( new_pos, new_rotation, new_subtile );
        draw_om_tile_recursively( new_pos, new_id, new_rotation, new_subtile, base_z_offset + 1 );
    }

    auto [bgCol, fgCol] = get_overmap_color( ACTIVE_OVERMAP_BUFFER, omp );

    const lit_level ll = ACTIVE_OVERMAP_BUFFER.is_explored( omp ) ? lit_level::LOW : lit_level::LIT;
    draw_from_id_string(
        tile, omp.reinterpret_as<tripoint_bub_ms>(), bgCol, fgCol, ll, false, base_z_offset, false );
}

// Hover-outline (HOVER_OUTLINE_PLAN.md): after a creature's sprites are queued,
// render_state::build_outline_ring splices offset silhouette copies of the whole
// range behind them — one clean composite ring around body + worn items. Ring
// thickness, alpha, enable and per-attitude colours are live F4 knobs (g_outline_*).


// Step 2 of the grid-decoupled lighting plan. See cata_tiles.h for why this lives
// on the outer class rather than on `texture`.
void cata_tiles::push_occluder_footprint( const occluder_footprint_options &opts )
{
    if( !opts.tex || opts.atlas_w <= 0 || opts.atlas_h <= 0 || opts.destination.w <= 0
        || opts.destination.h <= 0 || tile_width <= 0 || tile_height <= 0 || !g ) {
        return;
    }
    // The SDF is single-z, so only the player's own level may feed it. Tiles drawn
    // from a lower z (z_drop) share x/y with the level above and would corrupt it.
    if( occluder_tile_.z() != g->u.bub_pos().z() ) {
        return;
    }
    lighting::render_state& rs = lighting::get_render_state();
    if( !rs.ready() ) { return; }

    const level_cache& mc = get_map().access_cache( occluder_tile_.z() );
    if( !mc.inbounds( occluder_tile_.xy() ) ) { return; }
    const int tx = occluder_tile_.x();
    const int ty = occluder_tile_.y();
    const int idx = mc.idx( tx, ty );
    if( idx < 0 || idx >= static_cast<int>( mc.transparency_cache.size() ) ) { return; }

    // block = how much light this tile stops, independent of the art. The cache is an
    // ATTENUATION COEFFICIENT, not a fraction: LIGHT_TRANSPARENCY_SOLID (exactly 0) is
    // the opaque sentinel and open air is only LIGHT_TRANSPARENCY_OPEN_AIR (0.0384);
    // larger values attenuate more. Hard occluders are exact; everything else gets
    // Beer-Lambert over one tile relative to open air, so glass and bars land near 0
    // and keep NOT occluding (matching the binary rule the rest of the engine uses)
    // while hedges and smoke land high and now dapple.
    const float t = mc.transparency_cache[idx];
    const float blk =
        ( t <= LIGHT_TRANSPARENCY_SOLID )
        ? 1.0f
        : std::clamp( 1.0f - std::exp( -( t - LIGHT_TRANSPARENCY_OPEN_AIR ) ), 0.0f, 1.0f );
    // Nothing to seed. Leaving the tile uncaptured lets occ_base fall back to the
    // tile-square TransBuf seed, which for a transmitting tile is "open" anyway.
    if( blk <= 0.0f ) { return; }

    // Quad geometry in TILE units, relative to this tile's own screen square, exactly
    // as sprite.vert draws it (centre + size + rotation about the centre).
    const float inv_tw = 1.0f / static_cast<float>( tile_width );
    const float inv_th = 1.0f / static_cast<float>( tile_height );
    const float cx = ( static_cast<float>( opts.destination.x )
                       + 0.5f * static_cast<float>( opts.destination.w )
                       - static_cast<float>( occluder_tile_screen_.x ) ) * inv_tw;
    const float cy = ( static_cast<float>( opts.destination.y )
                       + 0.5f * static_cast<float>( opts.destination.h )
                       - static_cast<float>( occluder_tile_screen_.y ) ) * inv_th;
    const float sw = static_cast<float>( opts.destination.w ) * inv_tw;
    const float sh = static_cast<float>( opts.destination.h ) * inv_th;
    constexpr float deg_to_rad = 3.14159265358979323846f / 180.0f;
    const float rot = static_cast<float>( opts.rotation_degrees ) * deg_to_rad;

    // Conservative reject: the ROTATED bounding box must overlap this tile's own
    // square [0,1]^2. |cos|/|sin| give the exact rotated extent, so this is tight for
    // the 90-degree steps single-sprite terrain actually uses.
    const float ac = std::abs( std::cos( rot ) );
    const float as = std::abs( std::sin( rot ) );
    const float hw = 0.5f * ( sw * ac + sh * as );
    const float hh = 0.5f * ( sw * as + sh * ac );
    if( cx + hw <= 0.0f || cx - hw >= 1.0f || cy + hh <= 0.0f || cy - hh >= 1.0f ) {
        return;
    }

    // Same srcrect -> normalised-UV conversion (flip folded into the sign) that
    // enqueue_tile_sprite performs, so the compute rasteriser samples exactly the
    // texels the fragment pass does.
    const SDL_FRect& sr = opts.tex->src_rect();
    const float inv_aw = 1.0f / static_cast<float>( opts.atlas_w );
    const float inv_ah = 1.0f / static_cast<float>( opts.atlas_h );
    float u = sr.x * inv_aw;
    float v = sr.y * inv_ah;
    float uw = sr.w * inv_aw;
    float vh = sr.h * inv_ah;
    if( opts.flip & SDL_FLIP_HORIZONTAL ) {
        u += uw;
        uw = -uw;
    }
    if( opts.flip & SDL_FLIP_VERTICAL ) {
        v += vh;
        vh = -vh;
    }

    rs.occluders().push( {
        .u0 = u, .v0 = v, .uw = uw, .vh = vh,
        .tile_x = static_cast<float>( tx ), .tile_y = static_cast<float>( ty ),
        .cx = cx, .cy = cy, .sw = sw, .sh = sh,
        .rot = rot, .block = blk } );
}

bool cata_tiles::draw_sprite_at(
    const tile_type& tile, point_bub_ms p, unsigned int loc_rand, bool is_fg, int rota,
    const tint_config& tint, lit_level ll, bool apply_visual_effects, int overlay_count,
    int *height_3d, int retract, size_t warp_hash, float sway )
{


    const auto& sv_list_sprite = is_fg ? tile.sprite.fg : tile.sprite.bg;
    const auto picked_sprite_list = sv_list_sprite.pick( loc_rand );
    if( !picked_sprite_list ) { return true; }
    const auto& sprite_list = *picked_sprite_list;
    if( sprite_list.empty() ) { return true; }

    const auto& sv_list_tint_mask = is_fg ? tile.masks.tint.fg : tile.masks.tint.bg;
    const auto picked_tint_mask_list = sv_list_tint_mask.pick( loc_rand );
    if( !picked_tint_mask_list ) {
        debugmsg( "Failed to load tint mask" );
        return true;
    }
    const auto& tint_mask_list = *picked_tint_mask_list;
    if( tint_mask_list.size() != sprite_list.size() ) {
        debugmsg( "Sprite and mask lists size mismatch" );
        return true;
    }

    /*
     * If single tile:
     *   Don't rotate a background tile
     *   Rotate foreground
     * If multiple tiles:
     *   Don't apply sprite rotation in SDL
     *   2: tiles, tile 0 is N/S, tile 1 is E/W
     *   4: tiles, 0=N, 1=E, 2=S, 3=W
     *   5+ tiles: Animation, should not be used for rotation
     */
    const auto num_sprites = sprite_list.size();
    const auto is_single_sprite = num_sprites == 1;
    constexpr auto rotate_sprite_fg = true;
    const auto rotate_sprite_bg = tile.rotates && !tile.is_multitile_subtile;
    const auto rotate_sprite = is_single_sprite && ( is_fg ? rotate_sprite_fg : rotate_sprite_bg );
    const auto sprite_num = is_single_sprite ? 0 : ( rota % num_sprites );

    tint_config effective_tint = tint;

    tileset_fx_type fx_type;
    if( ll == lit_level::MEMORIZED ) {
        fx_type = tileset_fx_type::memory;
    } else if( apply_visual_effects && nv_goggles_activated ) {
        fx_type = ll == lit_level::LOW ? tileset_fx_type::night : tileset_fx_type::overexposed;
    } else if( apply_visual_effects && env_goggles_activated ) {
        fx_type =
            ll == lit_level::LOW
            ? tileset_fx_type::enhanced_night
            : tileset_fx_type::enhanced_overexposed;
    } else if( overlay_count > 0 && static_z_effect ) {
        fx_type = tileset_fx_type::z_overlay;
        effective_tint = {};
    } else if( apply_visual_effects && g->u.is_underwater() ) {
        fx_type =
            ll == lit_level::LOW ? tileset_fx_type::underwater_dark : tileset_fx_type::underwater;
    } else if( ll == lit_level::LOW ) {
        fx_type = tileset_fx_type::shadow;
    } else {
        fx_type = tileset_fx_type::none;
    }

    const int tile_idx = sprite_list[sprite_num];
    const int mask_idx = tint_mask_list[sprite_num];

    if( tile.flags.contains( flag_TINT_NONE ) || ( is_fg && tile.flags.contains( flag_TINT_NO_FG ) )
        || ( !is_fg && tile.flags.contains( flag_TINT_NO_BG ) ) ) {
        effective_tint = {};
    } else if( effective_tint.color == TILESET_NO_COLOR && tile.default_tint.has_value() ) {
        effective_tint.color = tile.default_tint.value();
    }

    // Use active_warp_hash if no explicit warp_hash provided (for character rendering)
    const size_t effective_warp_hash =
        ( warp_hash == TILESET_NO_WARP ) ? active_warp_hash : warp_hash;

    const auto tile_offset =
        retract <= 0
        ? tile.offset
        : ( retract >= 100
            ? tile.offset_retracted
            : tile.offset + ( ( tile.offset_retracted - tile.offset ) * retract ) / 100 );

    // Pass warp_hash and tile_offset to get_or_default - UV remapping is now handled there
    const auto [sprite_tex, warp_offset] = tileset_ptr->get_or_default(
            tile_idx, mask_idx, fx_type, effective_tint, effective_warp_hash, tile_offset );

    if( !sprite_tex ) { return true; }

    int width = 0;
    int height = 0;
    std::tie( width, height ) = sprite_tex->dimension();

    const int height_3d_val = height_3d ? *height_3d : 0;

    // Apply warp_offset to destination position
    // warp_offset is in sprite-local coordinates, scale to screen coordinates
    const int warp_offset_screen_x = warp_offset.x * tile_width / tileset_ptr->get_tile_width();
    const int warp_offset_screen_y = warp_offset.y * tile_height / tileset_ptr->get_tile_height();

    SDL_Rect destination;
    destination.x =
        p.x() + divide_round_down( tile_offset.x * tile_width, tileset_ptr->get_tile_width() )
        + warp_offset_screen_x;
    destination.y =
        p.y()
        + divide_round_down(
            ( tile_offset.y - height_3d_val ) * tile_width, tileset_ptr->get_tile_width() )
        + warp_offset_screen_y;
    destination.w = width * tile_width * tile.pixelscale / tileset_ptr->get_tile_width();
    destination.h = height * tile_height * tile.pixelscale / tileset_ptr->get_tile_height();

    // Sprite-animation transform (identity for non-creature tiles). Offset shifts the
    // rect; flash brightens the light tint (white for the avatar, red for others); tilt is
    // added to the rotation in render().
    // fg_only transforms (tile-bash recoil) must not move the bg layer (ground stays put);
    // creature transforms leave fg_only false and apply to all layers (rigid body).
    if( active_anim_xform_.active() && !( active_anim_xform_.fg_only && !is_fg ) ) {
        const sprite_xform& xf = active_anim_xform_;
        destination.x += static_cast<int>( xf.off_x );
        destination.y += static_cast<int>( xf.off_y );
        gpu_light_r = std::max( 0.0f, gpu_light_r + xf.flash_r );
        gpu_light_g = std::max( 0.0f, gpu_light_g + xf.flash_g );
        gpu_light_b = std::max( 0.0f, gpu_light_b + xf.flash_b );
    }

    // Height extrusion: extend the fg quad upward when the tile has depth_extrude_px set.
    // Shifts dst_y up and increases dst_h so the extended region renders above the sprite.
    // bg layer is never extended (produces no bleed).
    float effective_extrude_px = 0.0f;
    float effective_extrude_dark = 0.0f;
    float effective_extrude_lean = 0.0f;
    if( is_fg && tile.depth_extrude_px > 0.0f ) {
        // Convert tile-def pixels to screen pixels (same formula as height_3d_val).
        const float screen_ext =
            tile.depth_extrude_px * static_cast<float>( tile_width )
            / static_cast<float>( tileset_ptr->get_tile_width() );
        destination.y -= static_cast<int>( screen_ext );
        destination.h += static_cast<int>( screen_ext );
        effective_extrude_px = screen_ext;
        effective_extrude_dark = tile.depth_extrude_dark * g_depth_dark_str;
        effective_extrude_lean = tile.depth_extrude_lean * g_depth_lean_str;
    }

    // GPU-only render. Rotation is stored in sprite_instance but the HLSL
    // shader currently ignores it — rotated sprites appear unrotated until
    // the vertex shader rotation math is implemented. Preferable to invisible.
    auto render = [&]( const int rotation, const SDL_FlipMode flip ) {
        dynamic_atlas* atlas = tileset_ptr->texture_atlas();
        const auto gpu =
            atlas ? atlas->find_gpu_texture_full( sprite_tex->sdl_texture_handle() )
            : dynamic_atlas::gpu_lookup{nullptr, 0, 0};

        if( !gpu.texture ) {
            static bool warned = false;
            if( !warned ) {
                warned = true;
                dbg( DL::Warn ) << "GPU atlas miss in draw_sprite_at — sprite invisible";
            }
            return 0;
        }

        const SDL_FRect
        fdst{static_cast<float>( destination.x ), static_cast<float>( destination.y ),
             static_cast<float>( destination.w ), static_cast<float>( destination.h )};
        // Only the foreground (vegetation) layer gets foliage sway.
        // The background layer (ground under the tile) must stay static.
        const float enq_sway = is_fg ? sway : 0.0f;
        sprite_tex->enqueue_tile_sprite(
            gpu.texture, gpu.atlas_w, gpu.atlas_h, fdst, flip, active_anim_xform_.alpha,
            static_cast<double>( rotation ) + active_anim_xform_.tilt_deg, gpu_light_r, gpu_light_g,
            gpu_light_b, gpu_light_mul, enq_sway,
            /*outline=*/0.0f, effective_extrude_px, effective_extrude_dark, effective_extrude_lean );
        // Step 2: capture this sprite's alpha footprint for the SDF seed. Foreground
        // only — the BACKGROUND layer of a wall tile is the floor underneath it, which
        // is opaque across the whole square and would re-create the very tile-square
        // occluder this work exists to remove.
        if( occluder_capture_ && is_fg ) {
            push_occluder_footprint( {
                .tex = sprite_tex,
                .destination = destination,
                .flip = flip,
                .atlas_w = gpu.atlas_w,
                .atlas_h = gpu.atlas_h,
                .rotation_degrees = static_cast<double>( rotation ) + active_anim_xform_.tilt_deg } );
        }
        if( !static_z_effect && overlay_count > 0 ) {
            const auto [overlay_tex, overlay_warp_offset] = tileset_ptr->get_or_default(
                    tile_idx, TILESET_NO_MASK, tileset_fx_type::z_overlay, TILESET_NO_COLOR,
                    effective_warp_hash, tile_offset );
            if( overlay_tex ) {
                const auto ov_gpu = atlas->find_gpu_texture_full( overlay_tex->sdl_texture_handle() );
                if( ov_gpu.texture ) {
                    const float a = std::min( 192, overlay_count ) / 255.0f;
                    overlay_tex->enqueue_tile_sprite(
                        ov_gpu.texture, ov_gpu.atlas_w, ov_gpu.atlas_h, fdst, flip, a,
                        static_cast<double>( rotation ) + active_anim_xform_.tilt_deg, gpu_light_r,
                        gpu_light_g, gpu_light_b );
                }
            }
        }
        return 0;
    };

    int ret = 0;
    if( rotate_sprite ) {
        switch( rota ) {
            default:
            case 0:
                // unrotated (and 180, with just two sprites)
                ret = render( 0, SDL_FLIP_NONE );
                break;
            case 1:
                // 90 degrees (and 270, with just two sprites)
                if( !tile_iso ) {
                    // never rotate isometric tiles
                    ret = render( -90, SDL_FLIP_NONE );
                } else {
                    ret = render( 0, SDL_FLIP_NONE );
                }
                break;
            case 2:
                // 180 degrees, implemented with flips instead of rotation
                if( !tile_iso ) {
                    // never flip isometric tiles vertically
                    ret = render(
                              0, static_cast<SDL_FlipMode>( SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL ) );
                } else {
                    ret = render( 0, SDL_FLIP_NONE );
                }
                break;
            case 3:
                // 270 degrees
                if( !tile_iso ) {
                    // never rotate isometric tiles
                    ret = render( 90, SDL_FLIP_NONE );
                } else {
                    ret = render( 0, SDL_FLIP_NONE );
                }
                break;
            case 4:
                // flip horizontally
                ret = render( 0, SDL_FLIP_HORIZONTAL );
                break;
            case 5:
                // 45 degrees
                if( !tile_iso ) {
                    // never rotate isometric tiles
                    ret = render( 45, SDL_FLIP_NONE );
                } else {
                    ret = render( 0, SDL_FLIP_NONE );
                }
                break;
            case 6:
                // 315 degrees
                if( !tile_iso ) {
                    // never rotate isometric tiles
                    ret = render( -45, SDL_FLIP_NONE );
                } else {
                    ret = render( 0, SDL_FLIP_NONE );
                }
                break;
            case 7:
                // 225 degrees
                if( !tile_iso ) {
                    // never rotate isometric tiles
                    ret = render( -135, SDL_FLIP_NONE );
                } else {
                    ret = render( 0, SDL_FLIP_NONE );
                }
                break;
            case 8:
                // 135 degrees
                if( !tile_iso ) {
                    // never rotate isometric tiles
                    ret = render( 135, SDL_FLIP_NONE );
                } else {
                    ret = render( 0, SDL_FLIP_NONE );
                }
                break;
        }
    } else {
        // don't rotate, same as case 0 above
        ret = render( 0, SDL_FLIP_NONE );
    }

    // this reference passes all the way back up the call chain back to
    // cata_tiles::draw() std::vector<tile_render_info> draw_points[].height_3d
    // where we are accumulating the height of every sprite stacked up in a tile
    if( height_3d ) { *height_3d += tile.height_3d; }
    return true;
}

bool cata_tiles::draw_tile_at(
    const tile_type& tile, point_bub_ms p, unsigned int loc_rand, int rota,
    const tint_config& bg_tint, const tint_config& fg_tint, lit_level ll, bool apply_visual_effects,
    int &height_3d, int overlay_count, int retract, float sway )
{
    draw_sprite_at( tile, p, loc_rand, /*fg:*/ false, rota, bg_tint, ll, apply_visual_effects,
                    overlay_count, nullptr, retract, TILESET_NO_WARP, sway );
    draw_sprite_at( tile, p, loc_rand, /*fg:*/ true, rota, fg_tint, ll, apply_visual_effects,
                    overlay_count, &height_3d, retract, TILESET_NO_WARP, sway );
    return true;
}

auto cata_tiles::draw_color_at( const SDL_Color& color, point_bub_ms pos ) -> void
{
    lighting::overlay_rect(
    { static_cast<float>( pos.x() ), static_cast<float>( pos.y() ),
      static_cast<float>( tile_width ), static_cast<float>( tile_height ) },
    lighting::overlay_color_from_bytes( color.r, color.g, color.b, color.a ) );
}

bool cata_tiles::would_apply_vision_effects( const visibility_type visibility ) const
{
    return visibility != VIS_CLEAR;
}

bool cata_tiles::apply_vision_effects(
    const tripoint_bub_ms& pos, const visibility_type visibility )
{
    if( !would_apply_vision_effects( visibility ) ) { return false; }
    const std::string* light_name = nullptr;
    switch( visibility ) {
        case VIS_HIDDEN: {
            light_name = &STATIC( std::string( "lighting_hidden" ) );
            break;
        }
        case VIS_LIT: {
            light_name = &STATIC( std::string( "lighting_lowlight_light" ) );
            break;
        }
        case VIS_BOOMER: {
            light_name = &STATIC( std::string( "lighting_boomered_light" ) );
            break;
        }
        case VIS_BOOMER_DARK: {
            light_name = &STATIC( std::string( "lighting_boomered_dark" ) );
            break;
        }
        case VIS_DARK: {
            light_name = &STATIC( std::string( "lighting_lowlight_dark" ) );
            break;
        }
        case VIS_CLEAR:
            // should never happen
            break;
    }

    // lighting is never rotated, though, could possibly add in random rotation?
    const tile_search_params tile{*light_name, C_LIGHTING, empty_string, 0, 0};
    draw_from_id_string( tile, pos, std::nullopt, std::nullopt, lit_level::LIT, false, 0, false );

    return true;
}

template <typename T>
auto get_map_memory_of_at( const tripoint_bub_ms& p ) -> std::optional<memorized_terrain_tile>
{
    if( !g->u.should_show_map_memory() ) { return std::nullopt; }

const memorized_terrain_tile t = g->u.get_memorized_tile( get_map().bub_to_abs( p ) );
if( !string_id<T>( t.tile ).is_valid() ) { return std::nullopt; }

return t;
}

template <>
auto get_map_memory_of_at<vpart_info>( const tripoint_bub_ms& p )
-> std::optional<memorized_terrain_tile>
{
    if( !g->u.should_show_map_memory() ) { return std::nullopt; }

const memorized_terrain_tile t = g->u.get_memorized_tile(
                                     get_map().bub_to_abs( tripoint_bub_ms( p ) ) );
    if( !t.tile.starts_with( "vp_" ) ) { return std::nullopt; }

const auto actual_part = t.tile.substr( 3 );
if( !string_id<vpart_info>( actual_part ).is_valid() ) { return std::nullopt; }

return t;
}

bool cata_tiles::has_memory_at( const tripoint_bub_ms& p )
{
    if( !g->u.should_show_map_memory() ) { return false; }

    const auto abs = get_map().bub_to_abs( p );
    // Check overlay slot (furniture, vpart, trap) and terrain slot separately,
    // since terrain is now stored in its own slot and may be the only memory present.
    if( !g->u.get_memorized_tile( abs ).tile.empty() ) { return true; }
    return !g->u.get_terrain_tile( abs ).tile.empty();
}

auto cata_tiles::get_ter_memory_at( const tripoint_bub_ms& p )
-> std::optional<memorized_terrain_tile>
{
    if( !g->u.should_show_map_memory() ) { return std::nullopt; }
const memorized_terrain_tile t = g->u.get_terrain_tile(
                                     get_map().bub_to_abs( tripoint_bub_ms( p ) ) );
    if( t.tile.empty() ) { return std::nullopt; }
return t;
}

auto cata_tiles::get_furn_memory_at( const tripoint_bub_ms& p )
-> std::optional<memorized_terrain_tile>
{
    return get_map_memory_of_at<furn_t>( p );
}

auto cata_tiles::get_trap_memory_at( const tripoint_bub_ms& p )
-> std::optional<memorized_terrain_tile>
{
    return get_map_memory_of_at<trap>( p );
}

auto cata_tiles::get_vpart_memory_at( const tripoint_bub_ms& p )
-> std::optional<memorized_terrain_tile>
{
    return get_map_memory_of_at<vpart_info>( p );
}

bool cata_tiles::draw_block( const tripoint_bub_ms& p, SDL_Color color, int scale )
{
    SDL_Rect rect;
    rect.h = tile_width / scale;
    rect.w = tile_height / scale;

    if( tile_iso ) {
        rect.h = ( rect.h * 2 ) / 3;
        rect.w = ( rect.w * 3 ) / 4;
    }
    // translate from player-relative to screen relative tile position
    point screen;
    if( tile_iso ) {
        screen.x =
            ( ( p.x() - o.x() ) - ( o.y() - p.y() ) + screentile_width - 2 ) * tile_width / 2 + op.x;
        // y uses tile_width because width is definitive for iso tiles
        // tile footprints are half as tall as wide, arbitrarily tall
        screen.y =
            ( ( p.y() - o.y() ) - ( p.x() - o.x() ) - 4 ) * tile_width / 4
            + screentile_height * tile_height / 2 + // TODO: more obvious centering math
            op.y;
    } else {
        screen.x = ( p.x() - o.x() ) * tile_width + op.x;
        screen.y = ( p.y() - o.y() ) * tile_height + op.y;
    }
    rect.x = screen.x + ( tile_width - rect.w ) / 2;
    rect.y = screen.y + ( tile_height - rect.h ) / 2;
    if( tile_iso ) { rect.y += tile_height / 8; }

    geometry->rect( renderer, point{rect.x, rect.y}, rect.w, rect.h, color );
    return true;
}






point cata_tiles::player_to_screen( point_bub_ms p ) const
{
    point screen;
    if( tile_iso ) {
        screen.x =
            ( ( p.x() - o.x() ) - ( o.y() - p.y() ) + screentile_width - 2 ) * tile_width / 2 + op.x;
        // y uses tile_width because width is definitive for iso tiles
        // tile footprints are half as tall as wide, arbitrarily tall
        screen.y =
            ( ( p.y() - o.y() ) - ( p.x() - o.x() ) - 4 ) * tile_width / 4
            + screentile_height * tile_height / 2 + // TODO: more obvious centering math
            op.y;
    } else {
        screen.x = ( p.x() - o.x() ) * tile_width + op.x;
        screen.y = ( p.y() - o.y() ) * tile_height + op.y;
    }
    return {screen};
}




