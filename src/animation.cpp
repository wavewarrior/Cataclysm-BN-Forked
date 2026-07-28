#include "animation.h"

#include "avatar.h"
#include "cached_options.h"
#include "character.h"
#include "coordinates.h"
#include "cursesdef.h"
#include "enums.h"
#include "game_constants.h"
#include "game.h"
#include "line.h"
#include "map.h"
#include "monster.h"
#include "mtype.h"
#include "options.h"
#include "output.h"
#include "point.h"
#include "popup.h"
#include "posix_time.h"
#include "ranged.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"
#include "weather.h"

#include <memory>

#include "cata_tiles.h" // all animation functions will be pushed out to a cata_tiles function in some manner
#include "sdltiles.h"

#include <algorithm>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

class basic_animation
{
    public:
        explicit basic_animation( const int scale ) :
            delay( static_cast<size_t>( get_option<int>( "ANIMATION_DELAY" ) ) * scale * 1000000L ) {
        }

        void popup() const {
            static_popup popup;
            popup
            .wait_message( "%s", _( "Hang on a bit…" ) )
            .on_top( true );
        }
        void draw() const {
            g->invalidate_main_ui_adaptor();
            ui_manager::redraw_invalidated();
            refresh_display();
        }
        void progress( bool draw_popup = true ) const {
            if( draw_popup ) { popup(); }
            draw();

            // NOLINTNEXTLINE(cata-no-long): timespec uses long int
            long int remain = delay;
            while( remain > 0 ) {
            // NOLINTNEXTLINE(cata-no-long): timespec uses long int
            long int do_sleep = std::min( remain, 100'000'000L );
                timespec to_sleep = timespec { 0, do_sleep };
                nanosleep( &to_sleep, nullptr );
                inp_mngr.pump_events();
                remain -= do_sleep;
            }
        }

    private:
        // NOLINTNEXTLINE(cata-no-long): timespec uses long int
        long int delay;
};

class explosion_animation : public basic_animation
{
    public:
        explosion_animation() :
            basic_animation( EXPLOSION_MULTIPLIER ) {
        }
};

class bullet_animation : public basic_animation
{
    public:
        bullet_animation() : basic_animation( 1 ) {
        }
};

class wave_animation : public basic_animation
{
    public:
        wave_animation() : basic_animation( 1 ) {
        }
};

bool is_point_visible( const tripoint_bub_ms &p, int margin = 0 )
{
    return g->is_in_viewport( p, margin ) && g->u.sees( p );
}

bool is_radius_visible( const tripoint_bub_ms &center, int radius )
{
    return is_point_visible( center, -radius );
}

bool is_layer_visible( const std::map<tripoint_bub_ms, explosion_tile> &layer )
{
    return std::ranges::any_of( layer,
    []( const std::pair<tripoint_bub_ms, explosion_tile> &element ) {
        return is_point_visible( element.first );
    } );
}

// Convert p to screen position relative to u's current position and view
tripoint_rel_ms relative_view_pos( const avatar &u, const tripoint_bub_ms &p ) noexcept
{
    return p - ( u.bub_pos() + u.view_offset ) + point_rel_ms( POSX, POSY );
}

constexpr explosion_neighbors operator | ( explosion_neighbors lhs, explosion_neighbors rhs )
{
    return static_cast<explosion_neighbors>( static_cast< int >( lhs ) | static_cast< int >( rhs ) );
}

constexpr explosion_neighbors operator ^ ( explosion_neighbors lhs, explosion_neighbors rhs )
{
    return static_cast<explosion_neighbors>( static_cast< int >( lhs ) ^ static_cast< int >( rhs ) );
}

[[maybe_unused]]
auto get_bullet_dir( const std::vector<tripoint_bub_ms> &trajectory, size_t i ) -> direction
{
    return i == 0 && trajectory.size() > 1 ?
           direction_from( trajectory[i], trajectory[i + 1] ) :
           ( i >= 1 && i < trajectory.size() ) ?
           direction_from( trajectory[i - 1], trajectory[i] ) :
           direction::NORTH;
}

[[maybe_unused]] auto get_bullet_rotation( direction dir ) -> int
{
    switch( dir ) {
    case direction::NORTH:
        return 0;
    case direction::NORTHEAST:
        return 5;
    case direction::EAST:
        return 3;
    case direction::SOUTHEAST:
        return 8;
    case direction::SOUTH:
        return 2;
    case direction::SOUTHWEST:
        return 7;
    case direction::WEST:
        return 1;
    case direction::NORTHWEST:
        return 6;
    default:
        return 0;
}
}

} // namespace

void explosion_handler::draw_explosion( const tripoint_bub_ms &p, const int r,
                                        const nc_color &/*col*/,
                                        const std::string &exp_name )
{
    if( test_mode ) {
        // avoid segfault from null tilecontext in tests
        return;
    }

    if( !is_radius_visible( p, r ) ) {
        return;
    }

    explosion_animation anim;

    int i = 1;
    shared_ptr_fast<game::draw_callback_t> explosion_cb =
    make_shared_fast<game::draw_callback_t>( [&]() {
        // TODO: not xpos ypos?
        tilecontext->init_explosion( p, i, exp_name );
    } );
    g->add_draw_callback( explosion_cb );

    const bool visible = is_radius_visible( p, r );
    for( i = 1; i <= r; i++ ) {
        if( visible ) {
            anim.progress();
        }
    }

    if( r > 0 ) {
        tilecontext->void_explosion();
    }
}

void explosion_handler::draw_custom_explosion( const tripoint_bub_ms &,
        const std::map<tripoint_bub_ms, nc_color> &all_area,
        const std::string &exp_name )
{
    if( test_mode ) {
        // avoid segfault from null tilecontext in tests
        return;
    }

    constexpr explosion_neighbors all_neighbors = N_NORTH | N_SOUTH | N_WEST | N_EAST;
    // We will "shell" the explosion area
    // Each phase will strip a single layer of points
    // A layer contains all points that have less than 4 neighbors in cardinal directions
    // Layers will first be generated, then drawn in inverse order

    // Start by getting rid of everything except current z-level
    std::map<tripoint_bub_ms, explosion_tile> neighbors;
    // In tiles mode, the coordinates have to be absolute
    const auto view_center = relative_view_pos( g->u, g->u.bub_pos() );
    for( const auto &pr : all_area ) {
        // Relative point is only used for z level check
        const auto relative_point = relative_view_pos( g->u, pr.first );
        if( relative_point.z() == view_center.z() ) {
            neighbors[pr.first] = explosion_tile{ N_NO_NEIGHBORS, pr.second };
        }
    }

    // Searches for a neighbor, sets the neighborhood flag on current point and on the neighbor
    const auto set_neighbors = [&]( const tripoint_bub_ms & pos,
                                    explosion_neighbors & ngh,
                                    explosion_neighbors here,
    explosion_neighbors there ) {
        if( ( ngh & here ) == N_NO_NEIGHBORS ) {
            auto other = neighbors.find( pos );
            if( other != neighbors.end() ) {
                ngh = ngh | here;
                other->second.neighborhood = other->second.neighborhood | there;
            }
        }
    };

    // If the point we are about to remove has a neighbor in a given direction
    // unset that neighbor's flag that our current point is its neighbor
    const auto unset_neighbor = [&]( const tripoint_bub_ms & pos,
                                     const explosion_neighbors ngh,
                                     explosion_neighbors here,
    explosion_neighbors there ) {
        if( ( ngh & here ) != N_NO_NEIGHBORS ) {
            auto other = neighbors.find( pos );
            if( other != neighbors.end() ) {
                other->second.neighborhood = ( other->second.neighborhood | there ) ^ there;
            }
        }
    };

    // Find all neighborhoods
    for( auto &pr : neighbors ) {
        const tripoint_bub_ms &pt = pr.first;
        explosion_neighbors &ngh = pr.second.neighborhood;

        set_neighbors( pt + point_west, ngh, N_WEST, N_EAST );
        set_neighbors( pt + point_east, ngh, N_EAST, N_WEST );
        set_neighbors( pt + point_north, ngh, N_NORTH, N_SOUTH );
        set_neighbors( pt + point_south, ngh, N_SOUTH, N_NORTH );
    }

    // We need to save the layers because we will draw them in reverse order
    std::list< std::map<tripoint_bub_ms, explosion_tile> > layers;
    while( !neighbors.empty() ) {
        std::map<tripoint_bub_ms, explosion_tile> layer;
        bool changed = false;
        // Find a layer that can be drawn
        for( const auto &pr : neighbors ) {
            if( pr.second.neighborhood != all_neighbors ) {
                changed = true;
                layer.insert( pr );
            }
        }
        if( !changed ) {
            // An error, but a minor one - let it slide
            return;
        }
        // Remove the layer from the area to process
        for( const auto &pr : layer ) {
            const tripoint_bub_ms &pt = pr.first;
            const explosion_neighbors ngh = pr.second.neighborhood;

            unset_neighbor( pt + point_west, ngh, N_WEST, N_EAST );
            unset_neighbor( pt + point_east, ngh, N_EAST, N_WEST );
            unset_neighbor( pt + point_north, ngh, N_NORTH, N_SOUTH );
            unset_neighbor( pt + point_south, ngh, N_SOUTH, N_NORTH );
            neighbors.erase( pr.first );
        }

        layers.push_front( std::move( layer ) );
    }

    explosion_animation anim;
    // We need to draw all explosions up to now
    std::map<tripoint_bub_ms, explosion_tile> combined_layer;

    shared_ptr_fast<game::draw_callback_t> explosion_cb =
    make_shared_fast<game::draw_callback_t>( [&]() {
        tilecontext->init_custom_explosion_layer( combined_layer, exp_name );
    } );
    g->add_draw_callback( explosion_cb );

    for( const auto &layer : layers ) {
        combined_layer.insert( layer.begin(), layer.end() );
        if( is_layer_visible( layer ) ) {
            anim.progress();
        }
    }

    tilecontext->void_custom_explosion();
}

namespace
{

auto get_bullet_sprite( const char bullet, const std::string &custom_sprite ) -> std::string
{
    if( !custom_sprite.empty() ) {
    return custom_sprite;
}
if( bullet == '*' ) {
    return "animation_bullet_normal_0deg";
}
if( bullet == '#' ) {
    return "animation_bullet_flame";
}
if( bullet == '`' ) {
    return "animation_bullet_shrapnel";
}
return {};
}

} // namespace

void game::draw_bullet( const tripoint_bub_ms &t, const int i,
                        const std::vector<tripoint_bub_ms> &trajectory, const char bullet,
                        const std::string &custom_sprite )
{
    if( !is_point_visible( t ) ) {
        return;
    }

    const auto sprite = get_bullet_sprite( bullet, custom_sprite );
    const auto rotation = get_bullet_rotation( get_bullet_dir( trajectory, static_cast<size_t>( i ) ) );

    // Previous tile for interpolation; first tile (muzzle) snaps.
    const tripoint_bub_ms &prev = ( i > 0 ) ? trajectory[i - 1] : t;

    const auto delay_ms = get_option<int>( "ANIMATION_DELAY" );
    tilecontext->particles().emit( particle{
        .sprite = sprite,
        .rotation = rotation,
        .path = { prev, t },
        .start_wall = static_cast<double>( SDL_GetTicks() ) / 1000.0,
        .duration = static_cast<float>( delay_ms ) / 1000.f
    } );

    static_popup popup;
    popup.wait_message( "%s", _( "Hang on a bit…" ) ).on_top( true );

    // Render loop — particle interpolates via wall-clock during normal draw pass.
    // NOLINTNEXTLINE(cata-no-long)
    long int remain = static_cast<long int>( delay_ms ) * 1'000'000L;
    while( remain > 0 ) {
        invalidate_main_ui_adaptor();
        ui_manager::redraw_invalidated();
        refresh_display();

        // NOLINTNEXTLINE(cata-no-long)
        const long int do_sleep = std::min( remain, 16'000'000L );
        const timespec ts{ 0, do_sleep };
        nanosleep( &ts, nullptr );
        inp_mngr.pump_events();
        remain -= do_sleep;
    }
}

namespace
{

auto get_longest_trajectory_size( const std::vector<std::vector<tripoint_bub_ms>> &trajectories ) ->
size_t
{
    auto longest_trajectory_size = size_t{ 0 };
    for( const auto &trajectory : trajectories ) {
        longest_trajectory_size = std::max( longest_trajectory_size, trajectory.size() );
    }
    return longest_trajectory_size;
}

} // namespace

void draw_bullet_trajectories( const draw_bullet_trajectories_options &options )
{
    if( options.trajectories.empty() || !tilecontext ) {
        return;
    }

    const auto sprite = get_bullet_sprite( options.bullet, options.custom_sprite );
    const auto delay_ms = get_option<int>( "ANIMATION_DELAY" );
    const auto tile_dur = static_cast<float>( delay_ms ) / 1000.f;
    const auto wall_start = static_cast<double>( SDL_GetTicks() ) / 1000.0;
    if( options.draw_as_line ) {
        // Emit one particle per trajectory using the full path — same as the
        // animated branch below, but with flight-direction rotation instead of
        // per-shot end-direction.
        auto longest = size_t{ 0 };
        for( const auto &trajectory : options.trajectories ) {
            if( trajectory.size() < 2 ) {
                continue;
            }
            // Skip the first point (muzzle) for the visible flight path.
            auto flight = std::vector<tripoint_bub_ms>( trajectory.begin() + 1, trajectory.end() );
            if( flight.size() < 2 ) {
                continue;
            }
            longest = std::max( longest, flight.size() );
            const auto rotation = get_bullet_rotation(
                                      get_bullet_dir( flight, flight.size() - 1 ) );
            tilecontext->particles().emit( particle{
                .sprite = sprite,
                .rotation = rotation,
                .path = std::move( flight ),
                .start_wall = wall_start,
                .duration = static_cast<float>( trajectory.size() - 1 ) * tile_dur,
            } );
        }
        if( longest < 2 ) {
            return;
        }
        static_popup popup;
        popup.wait_message( "%s", _( "Hang on a bit…" ) ).on_top( true );
        // NOLINTNEXTLINE(cata-no-long)
        long int remain = static_cast<long int>( delay_ms )
                          * static_cast<long int>( longest - 1 ) * 1'000'000L;
        while( remain > 0 ) {
            g->invalidate_main_ui_adaptor();
            ui_manager::redraw_invalidated();
            refresh_display();
            // NOLINTNEXTLINE(cata-no-long)
            const long int do_sleep = std::min( remain, 16'000'000L );
            const timespec ts{ 0, do_sleep };
            nanosleep( &ts, nullptr );
            inp_mngr.pump_events();
            remain -= do_sleep;
        }
        return;
    }

    const auto longest_trajectory_size = get_longest_trajectory_size( options.trajectories );

    // Emit one particle per trajectory — all start simultaneously.
    for( const auto &trajectory : options.trajectories ) {
        if( trajectory.size() < 2 ) {
            continue;
        }
        const auto rotation = get_bullet_rotation(
                                  get_bullet_dir( trajectory, trajectory.size() - 1 ) );
        tilecontext->particles().emit( particle{
            .sprite = sprite,
            .rotation = rotation,
            .path = trajectory,
            .start_wall = wall_start,
            .duration = static_cast<float>( trajectory.size() - 1 ) * tile_dur
        } );
    }

    if( longest_trajectory_size < 2 ) {
        return;
    }

    static_popup popup;
    popup.wait_message( "%s", _( "Hang on a bit…" ) ).on_top( true );

    // Single render loop for all particles.
    // NOLINTNEXTLINE(cata-no-long)
    long int remain = static_cast<long int>( delay_ms )
                      * static_cast<long int>( longest_trajectory_size - 1 ) * 1'000'000L;
    while( remain > 0 ) {
        g->invalidate_main_ui_adaptor();
        ui_manager::redraw_invalidated();
        refresh_display();

        // NOLINTNEXTLINE(cata-no-long)
        const long int do_sleep = std::min( remain, 16'000'000L );
        const timespec ts{ 0, do_sleep };
        nanosleep( &ts, nullptr );
        inp_mngr.pump_events();
        remain -= do_sleep;
    }
}

void game::draw_hit_mon( const tripoint_bub_ms &/*p*/, const monster &/*m*/, const bool /*dead*/ )
{
    if( test_mode ) {
        // avoid segfault from null tilecontext in tests
        return;
    }

    // Tiles: the old single-frame hit flash is retired — the sprite-animation system draws
    // the hit reaction (directional recoil + red flash) instead. Keep the pacing tick.
    bullet_animation().progress();
}

void game::draw_hit_player( const Character &/*p*/, const int /*dam*/ )
{
    if( test_mode ) {
        // avoid segfault from null tilecontext in tests
        return;
    }

    // Tiles: old single-frame hit flash retired — sprite-animation hit reaction (white flash
    // + recoil on the avatar) replaces it. Keep the pacing tick.
    bullet_animation().progress();
}

/* Line drawing code, not really an animation but should be separated anyway */
void game::draw_line( const tripoint_bub_ms &p, const tripoint_bub_ms &/*center*/,
                      const std::vector<tripoint_bub_ms> &points, bool /*noreveal*/ )
{
    if( !u.sees( p ) ) {
        return;
    }

    tilecontext->init_draw_line( p, points, "line_target", true );
}

void draw_line_of( const draw_sprite_line_options &options )
{
    // Build visible path from trajectory points.
    std::vector<tripoint_bub_ms> path;
    path.reserve( options.points.size() );
    for( const auto &pt : options.points ) {
        if( is_point_visible( pt ) ) {
            path.push_back( pt );
        }
    }
    if( path.size() < 2 ) {
        return;
    }

    const auto delay_ms = get_option<int>( "ANIMATION_DELAY" );
    const auto tile_dur = static_cast<float>( delay_ms ) / 1000.f;
    const auto rotation = options.rotate ? 0 :
                          get_bullet_rotation( get_bullet_dir( path, path.size() - 1 ) );

    tilecontext->particles().emit( particle{
        .sprite = options.sprite,
        .rotation = rotation,
        .path = path,
        .start_wall = static_cast<double>( SDL_GetTicks() ) / 1000.0,
        .duration = static_cast<float>( path.size() - 1 ) * tile_dur,
        .tumble = options.rotate,
    } );

    static_popup popup;
    popup.wait_message( "%s", _( "Hang on a bit…" ) ).on_top( true );

    // Render loop — particle interpolates via wall-clock.
    // NOLINTNEXTLINE(cata-no-long)
    long int remain = static_cast<long int>( delay_ms )
                      * static_cast<long int>( path.size() - 1 ) * 1'000'000L;
    while( remain > 0 ) {
        g->invalidate_main_ui_adaptor();
        ui_manager::redraw_invalidated();
        refresh_display();

        // NOLINTNEXTLINE(cata-no-long)
        const long int do_sleep = std::min( remain, 16'000'000L );
        const timespec ts{ 0, do_sleep };
        nanosleep( &ts, nullptr );
        inp_mngr.pump_events();
        remain -= do_sleep;
    }
}

void emit_impact_particle( const tripoint_bub_ms &pos, const bool blood )
{
    if( test_mode || !tilecontext ) {
        return;
    }
    const auto sprite = blood ? std::string{ "animation_impact_blood" }
                        :
                        std::string{ "animation_impact_sparks" };
    tilecontext->particles().emit( particle{
        .sprite = sprite,
        .path = { pos },
        .start_wall = static_cast<double>( SDL_GetTicks() ) / 1000.0,
        .duration = 0.3f,
    } );
}
void game::draw_line( const tripoint_bub_ms &p, const std::vector<tripoint_bub_ms> &points )
{
    if( test_mode ) {
        // avoid segfault from null tilecontext in tests
        return;
    }
    tilecontext->init_draw_line( p, points, "line_trail", false );
}

void game::draw_cursor( const tripoint_bub_ms &p )
{
    tilecontext->init_draw_cursor( p );
}

auto game::draw_aim_crosshair( point pixel ) -> void
{
    if( !tilecontext ) { return; }
tilecontext->init_draw_aim_crosshair( pixel );
}

auto game::draw_aim_cone( const point_bub_ms &src, float aim_rad,
                          float spread_half_rad, int max_range, int z ) -> void
{
    if( !tilecontext ) { return; }
tilecontext->init_draw_aim_cone( src, aim_rad, spread_half_rad, max_range, z );
}

auto game::draw_throw_arc( const tripoint_bub_ms &src, const tripoint_bub_ms &dst,
                           float charge ) -> void
{
    if( !tilecontext ) { return; }
tilecontext->init_draw_throw_arc( src, dst, charge );
}

auto game::draw_throw_impact( const tripoint_bub_ms &dst, float max_radius_tiles ) -> void
{
    if( !tilecontext ) { return; }
tilecontext->init_draw_throw_impact( dst, max_radius_tiles );
}

auto game::void_throw_impact() -> void
{
    if( !tilecontext ) { return; }
tilecontext->void_throw_impact();
}

void game::draw_highlight( const tripoint_bub_ms &p )
{
    if( test_mode ) {
        // avoid segfault from null tilecontext in tests
        return;
    }

    tilecontext->init_draw_highlight( p );
}

void game::draw_weather( const weather_printable &w )
{
    tilecontext->init_draw_weather( w, w.wtype->animation.tile );
}

void game::draw_sct()
{
    tilecontext->init_draw_sct();
}

void game::draw_zones( const zone_draw_options &options )
{
    tilecontext->init_draw_zones( options );
}

void game::draw_radiation_override( const tripoint_bub_ms &p, const int rad )
{
    tilecontext->init_draw_radiation_override( p, rad );
}

void game::draw_terrain_override( const tripoint_bub_ms &p, const ter_id &id )
{
    tilecontext->init_draw_terrain_override( p, id );
}

void game::draw_furniture_override( const tripoint_bub_ms &p, const furn_id &id )
{
    tilecontext->init_draw_furniture_override( p, id );
}

void game::draw_graffiti_override( const tripoint_bub_ms &p, const bool has )
{
    tilecontext->init_draw_graffiti_override( p, has );
}

void game::draw_trap_override( const tripoint_bub_ms &p, const trap_id &id )
{
    tilecontext->init_draw_trap_override( p, id );
}

void game::draw_field_override( const tripoint_bub_ms &p, const field_type_id &id )
{
    tilecontext->init_draw_field_override( p, id );
}

void game::draw_item_override( const tripoint_bub_ms &p, const itype_id &id, const mtype_id &mid,
                               const bool hilite )
{
    tilecontext->init_draw_item_override( p, id, mid, hilite );
}

void game::draw_vpart_override(
    const tripoint_bub_ms &p, const vpart_id &id, const int part_mod, const units::angle veh_dir,
    const bool hilite, tripoint_mnt_veh mount )
{
    // TRIPOINT MIGRATION FIXME
    tilecontext->init_draw_vpart_override( p, id, part_mod, veh_dir, hilite, mount.xy().raw() );
}

void game::draw_below_override( const tripoint_bub_ms &p, const bool draw )
{
    tilecontext->init_draw_below_override( p, draw );
}

void game::draw_monster_override( const tripoint_bub_ms &p, const mtype_id &id, const int count,
                                  const bool more, const Attitude att )
{
    tilecontext->init_draw_monster_override( p, id, count, more, att );
}

bucketed_points bucket_by_distance( const tripoint_bub_ms &origin,
                                    const std::map<tripoint_bub_ms, double> &to_bucket )
{
    std::map<int, one_bucket> by_distance;
    for( const auto& [pt, val] : to_bucket ) {
        int dist = trig_dist_squared( origin, pt );
        by_distance[dist].emplace_back( point_with_value{ pt.raw(), val} );
    }
    bucketed_points buckets;
    for( const auto& [_, bucket] : by_distance ) {
        buckets.emplace_back( bucket );
    }
    return buckets;
}

bucketed_points optimal_bucketing( const bucketed_points &buckets, size_t max_buckets )
{
    if( buckets.size() <= max_buckets ) {
        return buckets;
    }
    assert( max_buckets > 1 );

    std::vector<size_t> sizes = {};
    for( const one_bucket &bc : buckets ) {
        sizes.emplace_back( bc.size() );
    }

    bucketed_points optimal = buckets;
    // TODO: Good algorithm here, this one is a greedy finder of smallest adjacent size sums
    for( size_t i = 0; i < buckets.size() - max_buckets; i++ ) {
        auto smallest = sizes.begin();
        size_t smallest_sum = *smallest + *( smallest + 1 );
        for( auto iter = sizes.begin() + 1; ( iter + 1 ) != sizes.end(); iter++ ) {
            size_t sum = *iter + *( iter + 1 );
            if( sum < smallest_sum ) {
                smallest = iter;
                smallest_sum = sum;
            }
        }

        size_t distance = std::distance( sizes.begin(), smallest );
        sizes[distance] += sizes[distance + 1];
        sizes.erase( smallest + 1 );
        auto left_bucket = std::next( optimal.begin(), distance );
        auto right_bucket = std::next( left_bucket );
        left_bucket->insert( left_bucket->end(), right_bucket->begin(), right_bucket->end() );
        optimal.erase( right_bucket );
    }

    return optimal;
}

namespace ranged
{
void draw_cone_aoe( const tripoint_bub_ms &origin, const std::map<tripoint_bub_ms, double> &aoe )
{
    if( test_mode ) {
        return;
    }

    bucketed_points buckets = bucket_by_distance( origin, aoe );
    // That hardcoded value could be improved... Not sure about the name
    size_t max_bucket_count = std::min<size_t>( 10, aoe.size() );
    bucketed_points waves = optimal_bucketing( buckets, max_bucket_count );

    // This is copied from explosion code
    // Not sure if it couldn't be cleaner, without that lambda capture thing
    one_bucket combined_layer;
    combined_layer.reserve( aoe.size() );

    wave_animation anim;

    shared_ptr_fast<game::draw_callback_t> wave_cb =
    make_shared_fast<game::draw_callback_t>( [&]() {
        tilecontext->init_draw_cone_aoe( origin, combined_layer );
    } );
    g->add_draw_callback( wave_cb );

    for( const one_bucket &layer : waves ) {
        // Older layers get a fade effect
        for( point_with_value &pv : combined_layer ) {
            pv.val *= 1.0 - ( 2.0 / max_bucket_count );
        }
        combined_layer.insert( combined_layer.end(), layer.begin(), layer.end() );
        if( std::ranges::any_of( combined_layer,
        []( const point_with_value & element ) {
        return is_point_visible( tripoint_bub_ms( element.pt ) );
        } ) ) {
            anim.progress();
        }
    }

    tilecontext->void_cone_aoe();
}
} // namespace ranged

bool minimap_requires_animation()
{
    return tilecontext->minimap_requires_animation();
}

bool terrain_requires_animation()
{
    return tilecontext->terrain_requires_animation();
}

bool creatures_require_animation()
{
    return tilecontext->creatures_require_animation();
}

void note_tile_bash( const tripoint_bub_ms &p )
{
    // tilecontext is null on the curses/headless path; bash runs there too.
    if( !tilecontext ) {
        return;
    }
    // Recoil away from the basher (use the avatar as the origin — bashes are player-driven
    // or adjacent; this reads as the tile getting knocked away from you).
    float dx = 0.f;
    float dy = 0.f;
    if( g ) {
        const tripoint_bub_ms u = g->u.bub_pos();
        dx = static_cast<float>( p.x() - u.x() );
        dy = static_cast<float>( p.y() - u.y() );
        const float len = std::sqrt( dx * dx + dy * dy );
        if( len > 0.f ) {
            dx /= len;
            dy /= len;
        } else {
            dx = 0.f;
            dy = 0.f;
        }
    }
    tilecontext->register_tile_hit( p, dx, dy );
}
