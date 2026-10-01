#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "catch/catch_amalgamated.hpp"
#include "coordinates.h"
#include "game.h"
#include "level_cache_freshness.h"
#include "lightmap.h"
#include "map.h"
#include "map_helpers.h"
#include "mapdata.h"
#include "monster.h"
#include "state_helpers.h"
#include "string_formatter.h"
#include "submap.h"
#include "type_id.h"

// Equivalence pins for the change kinds of the Level cache freshness module (spec #8,
// ticket #11).
//
// For each kind, the kind and the bit-setter sequence it replaces run on identical
// fixtures inside one test body (the fixture is rebuilt between the two runs, so both
// start from the same freshness state) and the observable freshness outcome is compared
// through the module's query interface: which parts are stale on which levels, which
// submap bits and submap dirty flags rose, how far the Structure-rebuild generations
// moved, what the support-loss and re-memorise queues hold.
//
// Two things are deliberately NOT captured, because they stay at the call sites and are
// not Level cache freshness (see the header): the pathfinding-cache dirt and the vehicle
// move-notification counter. `pf_dirty` follows from the former, so it is excluded too —
// it is the one flag the change kinds never touch.
//
// Generations are compared as DELTAS from their value right after fixture setup: the
// counters are per-level members that survive `clear_all_state()`, so absolute values
// would differ between two runs of the same fixture for reasons unrelated to the change.
//
// Vocabulary: GLOSSARY.md; plan: plans/level-cache-freshness.md.

namespace {

constexpr tripoint_bub_ms player_home( 60, 60, 0 );
// Per-bit comparisons run in this z-window; "any bit is set" is still checked for every
// level. The window keeps the capture small without losing coverage of the interesting
// levels (player level, one above, one below).
constexpr int z_lo = -1;
constexpr int z_hi = 1;

const char *part_name( const level_cache_part part ) {
    switch( part ) {
        case level_cache_part::transparency:
            return "transparency";
        case level_cache_part::outside:
            return "outside";
        case level_cache_part::floor:
            return "floor";
        case level_cache_part::absorption:
            return "absorption";
        case level_cache_part::sound_wall:
            return "sound_wall";
        case level_cache_part::lightmap:
            return "lightmap";
        case level_cache_part::seen:
            return "seen";
        case level_cache_part::visibility:
            return "visibility";
        case level_cache_part::lm_valid:
            return "lm_valid";
        case level_cache_part::suspension_dirty:
            return "suspension_dirty";
        case level_cache_part::suspension_init:
            return "suspension_init";
        case level_cache_part::vehicle_caches:
            return "vehicle_caches";
        case level_cache_part::vehicle_floor:
            return "vehicle_floor";
        case level_cache_part::has_any_floor:
            return "has_any_floor";
        case level_cache_part::has_any_vehicle_floor:
            return "has_any_vehicle_floor";
        case level_cache_part::colored_light_active:
            return "colored_light_active";
        case level_cache_part::veh_in_active_range:
            return "veh_in_active_range";
        case level_cache_part::memory_seen_all:
            return "memory_seen_all";
        case level_cache_part::NUM:
            break;
    }
    return "?";
}

// The parts a change kind can raise. The rest are builder-owned stamps
// (`has_any_floor`, `colored_light_active`, `suspension_init`, ...) that no kind touches.
constexpr std::array<level_cache_part, 13> observed_parts = { {
    level_cache_part::transparency,
    level_cache_part::outside,
    level_cache_part::floor,
    level_cache_part::absorption,
    level_cache_part::sound_wall,
    level_cache_part::lightmap,
    level_cache_part::seen,
    level_cache_part::visibility,
    level_cache_part::lm_valid,
    level_cache_part::suspension_dirty,
    level_cache_part::vehicle_caches,
    level_cache_part::vehicle_floor,
    level_cache_part::memory_seen_all,
} };

struct generation_baseline {
    std::uint64_t transparency[3];
    std::uint64_t outside[3];
    std::uint64_t cpu_lm[3];
};

generation_baseline capture_generations( map &here ) {
    generation_baseline base;
    for( int z = z_lo; z <= z_hi; ++z ) {
        const level_cache &ch = here.access_cache( z );
        const int zi = z - z_lo;
        base.transparency[zi] = level_cache_freshness::transparency_generation( ch );
        base.outside[zi] = level_cache_freshness::outside_generation( ch );
        base.cpu_lm[zi] = level_cache_freshness::cpu_lightmap_generation( ch );
    }
    return base;
}

// The support-loss and re-memorise queues are consumed by the next fall check / memorise
// pass, so reading them means draining them. Both sides of every comparison drain the
// same way, so the comparison stays fair.
void record_queues( map &here, std::vector<std::string> &lines ) {
    std::set<tripoint_bub_ms> losses = level_cache_freshness::take_support_losses( here );
    for( const tripoint_bub_ms &p : losses ) {
        lines.push_back( string_format( "support-lost %d %d %d", p.x(), p.y(), p.z() ) );
    }
    for( int z = z_lo; z <= z_hi; ++z ) {
        level_cache &ch = here.access_cache( z );
        std::vector<tripoint_bub_ms> pts = level_cache_freshness::take_memory_seen_points( ch );
        std::sort( pts.begin(), pts.end() );
        pts.erase( std::unique( pts.begin(), pts.end() ), pts.end() );
        for( const tripoint_bub_ms &p : pts ) {
            lines.push_back( string_format( "memory-point z%d %d %d %d", z, p.x(), p.y(),
                                            p.z() ) );
        }
    }
}

// Full freshness state of the z-window, plus generation deltas since `base`, plus the
// submap dirty flags of the window (the co-op defect of issue #7 lives in those flags).
std::vector<std::string> capture( map &here, const generation_baseline &base ) {
    std::vector<std::string> lines;
    const int mapsize = here.getmapsize();
    const enum_bitset<level_cache_part> bitset_parts = level_cache_bitset_parts();
    for( int z = z_lo; z <= z_hi; ++z ) {
        level_cache &ch = here.access_cache( z );
        const int zi = z - z_lo;
        for( const level_cache_part part : observed_parts ) {
            if( level_cache_freshness::stale( ch, part ) ) {
                lines.push_back( string_format( "stale z%d %s", z, part_name( part ) ) );
            }
            if( !bitset_parts[part] ) {
                continue;
            }
            // Per-submap bits recorded individually, so a coverage difference shows up
            // instead of collapsing into "some bit is set".
            for( size_t bit = 0; bit < static_cast<size_t>( mapsize ) * mapsize; ++bit ) {
                if( level_cache_freshness::stale( ch, part, bit ) ) {
                    lines.push_back( string_format( "bit z%d %s %zu", z, part_name( part ),
                                                    bit ) );
                }
            }
        }
        for( int sx = 0; sx < mapsize; ++sx ) {
            for( int sy = 0; sy < mapsize; ++sy ) {
                submap *const sm = here.get_submap_at_grid( tripoint_bub_sm( sx, sy, z ) );
                if( sm == nullptr ) {
                    continue;
                }
                const char *flags = "";
                if( sm->transparency_dirty ) { flags = "T"; }
                if( sm->floor_dirty ) { flags = "F"; }
                if( sm->outside_dirty ) { flags = "O"; }
                if( sm->absorption_dirty ) { flags = "A"; }
                if( flags[0] != '\0' ) {
                    lines.push_back( string_format( "smflag z%d %d %d %s%s%s%s", z, sx, sy,
                        sm->transparency_dirty ? "T" : "", sm->floor_dirty ? "F" : "",
                        sm->outside_dirty ? "O" : "", sm->absorption_dirty ? "A" : "" ) );
                }
            }
        }
        lines.push_back( string_format( "gen-transparency z%d +%llu", z,
            static_cast<unsigned long long>(
                level_cache_freshness::transparency_generation( ch ) - base.transparency[zi] ) ) );
        lines.push_back( string_format( "gen-outside z%d +%llu", z,
            static_cast<unsigned long long>(
                level_cache_freshness::outside_generation( ch ) - base.outside[zi] ) ) );
        lines.push_back( string_format( "gen-cpu-lm z%d +%llu", z,
            static_cast<unsigned long long>(
                level_cache_freshness::cpu_lightmap_generation( ch ) - base.cpu_lm[zi] ) ) );
    }
    lines.push_back( string_format( "visibility-aggregate %d",
                                    level_cache_freshness::visibility_stale( here ) ? 1 : 0 ) );
    record_queues( here, lines );
    std::sort( lines.begin(), lines.end() );
    return lines;
}


std::string only_in( const std::vector<std::string> &a, const std::vector<std::string> &b ) {
    std::string out;
    for( const std::string &line : a ) {
        if( std::find( b.begin(), b.end(), line ) == b.end() ) {
            out += line + "\n";
        }
    }
    return out;
}

void drain_work_lists( map &here ) {
    // A setup refresh has work lists to hand over (the suspension pass can report tiles
    // as unsupported, the memory pass can queue points). Draining them keeps a capture
    // focused on what the change under test raises.
    level_cache_freshness::take_support_losses( here );
    for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
        level_cache_freshness::take_memory_seen_points( here.access_cache( z ) );
    }
}

// Open daylight ground, player placed, Level cache fresh.
void set_up_open_daylight_map() {
    clear_all_state();
    build_test_map( ter_id( "t_dirt" ) );
    g->place_player( player_home );
    set_time( calendar::turn_zero + 12_hours );
    get_avatar().recalc_sight_limits();
    refresh_level_cache();
    drain_work_lists( get_map() );
}

// Replace terrain underneath the caches without raising any freshness state — the effect
// of the co-op tile sync. `submap::set_ter` invalidates the submap's own lazy emitter
// cache but touches neither the Level cache bitsets nor the submap dirty flags.
void swap_terrain_silently( map &here, const tripoint_bub_ms &p, const ter_id &what ) {
    point_sm_ms loc;
    submap *const sm = here.get_submap_at( p, loc );
    REQUIRE( sm != nullptr );
    sm->set_ter( loc, what );
}


// Flags of the terrain change described by an old/new pair, computed with the same
// predicates `map::ter_set` inspects (map_access.cpp), so the kind raises exactly the
// branch that would have run.
level_cache_freshness::terrain_changed ter_change_flags( const tripoint_bub_ms &at,
        const ter_id &old_id, const ter_id &new_id ) {
    const ter_t &old_t = old_id.obj();
    const ter_t &new_t = new_id.obj();
    return level_cache_freshness::terrain_changed {
        .at = at,
        .transparency = old_t.transparent != new_t.transparent,
        .no_floor = old_t.has_flag( TFLAG_NO_FLOOR ) != new_t.has_flag( TFLAG_NO_FLOOR ),
        .z_transparent = old_t.has_flag( TFLAG_Z_TRANSPARENT )
                         != new_t.has_flag( TFLAG_Z_TRANSPARENT ),
        .sun_roof_above = old_t.has_flag( TFLAG_SUN_ROOF_ABOVE )
                          != new_t.has_flag( TFLAG_SUN_ROOF_ABOVE ),
        .suspended = old_t.has_flag( TFLAG_SUSPENDED ) != new_t.has_flag( TFLAG_SUSPENDED ),
        // `ter_set` invalidates the lightmap unconditionally.
        .lightmap = true,
    };
}

level_cache_freshness::terrain_changed furn_change_flags( const tripoint_bub_ms &at,
        const furn_id &old_id, const furn_id &new_id ) {
    const furn_t &old_t = old_id.obj();
    const furn_t &new_t = new_id.obj();
    return level_cache_freshness::terrain_changed {
        .at = at,
        .transparency = old_t.transparent != new_t.transparent,
        .no_floor = old_t.has_flag( TFLAG_NO_FLOOR ) != new_t.has_flag( TFLAG_NO_FLOOR ),
        .z_transparent = old_t.has_flag( TFLAG_Z_TRANSPARENT )
                         != new_t.has_flag( TFLAG_Z_TRANSPARENT ),
        .sun_roof_above = old_t.has_flag( TFLAG_SUN_ROOF_ABOVE )
                          != new_t.has_flag( TFLAG_SUN_ROOF_ABOVE ),
        .suspended = old_t.has_flag( TFLAG_SUSPENDED ) != new_t.has_flag( TFLAG_SUSPENDED ),
        .lightmap = old_t.light_emitted != new_t.light_emitted,
        // `furn_set` always queues a support check at the edited tile.
        .support_here = true,
    };
}
} // namespace

TEST_CASE( "terrain-changed matches the ter_set and furn_set setter sequences",
           "[level_cache_freshness]" ) {
    const tripoint_bub_ms at( 61, 60, 0 );

    // Scenario: which old/new pair, how to plant the old one, how to mutate it.
    // The reference arm runs the REAL mutator, so the pin compares the kind against the
    // code it will replace rather than against a paraphrase of it.
    struct scenario {
        const char *name;
        const char *old_ter;
        const char *new_ter;
        const char *old_furn;
        const char *new_furn;
    };
    const std::vector<scenario> scenarios = { {
        // Opacity flips: transparency + seen, no floor change.
        { "t_dirt to t_wall", "t_dirt", "t_wall", "f_null", "f_null" },
        // Floor opens: floor cache, seen here and below, support check here.
        { "t_floor to t_open_air", "t_floor", "t_open_air", "f_null", "f_null" },
        // Furniture starts emitting light: lightmap only, plus the support checks.
        { "f_null to f_floor_lamp_on", "t_dirt", "t_dirt", "f_null", "f_floor_lamp_on" },
    } };

    for( const scenario &s : scenarios ) {
        CAPTURE( s.name );
        const bool via_furniture = std::string( s.new_furn ) != "f_null";

        // Plant the "old" state silently, then bring the caches up to date, so both arms
        // start from the same world and the same freshness state.
        const auto fixture = [&]( map &here ) {
            set_up_open_daylight_map();
            if( std::string( s.old_ter ) != "t_dirt" ) {
                swap_terrain_silently( here, at, ter_id( s.old_ter ) );
            }
            if( via_furniture ) {
                point_sm_ms loc;
                submap *const sm = here.get_submap_at( at, loc );
                REQUIRE( sm != nullptr );
                sm->set_furn( loc, furn_id( s.old_furn ) );
            }
            refresh_level_cache();
            drain_work_lists( here );
        };

        map &here = get_map();
        fixture( here );
        generation_baseline base = capture_generations( here );
        std::vector<std::string> via_mutator;
        if( via_furniture ) {
            here.furn_set( at, furn_id( s.new_furn ) );
        } else {
            here.ter_set( at, ter_id( s.new_ter ) );
        }
        via_mutator = capture( here, base );

        fixture( here );
        base = capture_generations( here );
        if( via_furniture ) {
            level_cache_freshness::report( here,
                                           furn_change_flags( at, furn_id( s.old_furn ),
                                                   furn_id( s.new_furn ) ) );
        } else {
            level_cache_freshness::report( here,
                                           ter_change_flags( at, ter_id( s.old_ter ),
                                                   ter_id( s.new_ter ) ) );
        }
        const std::vector<std::string> via_kind = capture( here, base );

        INFO( "raised by the mutator but not by the kind:\n"
              << only_in( via_mutator, via_kind ) );
        INFO( "raised by the kind but not by the mutator:\n"
              << only_in( via_kind, via_mutator ) );
        CHECK( via_kind == via_mutator );
    }
}

TEST_CASE( "terrain-changed reproduces the field, trap and weather opacity sequences",
           "[level_cache_freshness]" ) {
    // The field/trap/weather sites raise a transparency+seen pair without touching
    // support or memory. The kind reproduces them with the tile/level/all-levels
    // transparency scope and the two extras switched off; the reference arm still
    // spells the per-cache verbs that succeeded the old setters.
    const tripoint_bub_ms at( 61, 60, 0 );

    struct shape {
        const char *name;
        level_cache_freshness::terrain_changed::transparency_scope scope;
        tripoint_bub_ms seen_at;
        void ( *reference )( map &, const tripoint_bub_ms & );
    };
    const auto field_pair = []( map &here, const tripoint_bub_ms & p ) {
        level_cache_freshness::mark_transparency( here, p );
        level_cache_freshness::mark_seen( here, p );
    };
    const auto trap_pair = []( map &here, const tripoint_bub_ms & p ) {
        level_cache_freshness::mark_seen( here, p );
        level_cache_freshness::mark_transparency( here, p.z() );
    };
    const auto weather_pair = []( map &here, const tripoint_bub_ms & ) {
        for( int i = -OVERMAP_DEPTH; i <= OVERMAP_HEIGHT; i++ ) {
            level_cache_freshness::mark_transparency( here, i );
        }
        level_cache_freshness::mark_seen( here, tripoint_bub_ms::zero() );
    };
    using scope = level_cache_freshness::terrain_changed::transparency_scope;
    const std::vector<shape> shapes = { {
        // map_field / map_terrain / mapbuffer field sync: one tile's opacity.
        { "field pair", scope::tile, at, field_pair },
        // trapfunc map_regen: whole level of the trap, seen at the trap tile.
        { "trap pair", scope::level, at, trap_pair },
        // weather sight-penalty change: every level, seen at the bubble origin (0,0,0).
        { "weather pair", scope::all_levels, tripoint_bub_ms::zero(), weather_pair },
    } };

    for( const shape &s : shapes ) {
        CAPTURE( s.name );
        const auto fixture = [&]( map &here ) {
            set_up_open_daylight_map();
            refresh_level_cache();
            drain_work_lists( here );
        };

        map &here = get_map();
        fixture( here );
        generation_baseline base = capture_generations( here );
        s.reference( here, at );
        const std::vector<std::string> via_setters = capture( here, base );

        fixture( here );
        base = capture_generations( here );
        level_cache_freshness::report( here, level_cache_freshness::terrain_changed {
            .at = s.seen_at,
            .transparency = true,
            .scope = s.scope,
            .support_above = false,
            .memory_seen = false,
        } );
        const std::vector<std::string> via_kind = capture( here, base );

        INFO( "raised by the setters but not by the kind:\n"
              << only_in( via_setters, via_kind ) );
        INFO( "raised by the kind but not by the setters:\n"
              << only_in( via_kind, via_setters ) );
        CHECK( via_kind == via_setters );
        // The seen raise must actually fire for the tile-shaped shapes; the weather
        // seen point sits on an unseen tile in this fixture, where the old setter is
        // itself a no-op, so only the first two shapes assert it.
        if( s.scope != scope::all_levels ) {
            CHECK( level_cache_freshness::stale( here.access_cache( 0 ),
                                                 level_cache_part::seen ) );
        }
    }
}

TEST_CASE( "light-changed matches invalidate_lightmap", "[level_cache_freshness]" ) {
    set_up_open_daylight_map();
    map &here = get_map();
    generation_baseline base = capture_generations( here );
    level_cache_freshness::invalidate_lightmap( here );
    const std::vector<std::string> via_setter = capture( here, base );

    set_up_open_daylight_map();
    base = capture_generations( here );
    level_cache_freshness::report( here, level_cache_freshness::light_changed {} );
    const std::vector<std::string> via_kind = capture( here, base );

    INFO( "setter only:\n" << only_in( via_setter, via_kind )
          << "kind only:\n" << only_in( via_kind, via_setter ) );
    CHECK( via_kind == via_setter );
}

TEST_CASE( "tile-scoped light-changed matches mark_lightmap", "[level_cache_freshness]" ) {
    // `at` sits in submap (6,5); the fixture player sits in (5,5), so the pin can see
    // the tile scope spare the player's own submap and every other bit.
    const tripoint_bub_ms at( 72, 60, 0 );

    set_up_open_daylight_map();
    map &here = get_map();
    level_cache &ch = here.access_cache( 0 );
    const size_t bit_at = static_cast<size_t>( ch.bidx( at.x() / SEEX, at.y() / SEEY ) );
    const size_t bit_player = static_cast<size_t>(
        ch.bidx( player_home.x() / SEEX, player_home.y() / SEEY ) );
    generation_baseline base = capture_generations( here );
    const std::vector<std::string> plain = capture( here, base );
    level_cache_freshness::mark_lightmap( here, at );
    const std::vector<std::string> via_setter = capture( here, base );
    CHECK( via_setter != plain );
    CHECK( level_cache_freshness::stale( ch, level_cache_part::lightmap, bit_at ) );
    CHECK_FALSE( level_cache_freshness::stale( ch, level_cache_part::lightmap, bit_player ) );

    set_up_open_daylight_map();
    base = capture_generations( here );
    const std::vector<std::string> plain_again = capture( here, base );
    level_cache_freshness::report( here, level_cache_freshness::light_changed {
        .at = at,
        .scope = level_cache_freshness::light_changed::lightmap_scope::tile,
    } );
    const std::vector<std::string> via_kind = capture( here, base );
    CHECK( via_kind != plain_again );
    CHECK( level_cache_freshness::stale( here.access_cache( 0 ), level_cache_part::lightmap,
                                          bit_at ) );
    CHECK_FALSE( level_cache_freshness::stale( here.access_cache( 0 ),
                                               level_cache_part::lightmap, bit_player ) );

    INFO( "setter only:\n" << only_in( via_setter, via_kind )
          << "kind only:\n" << only_in( via_kind, via_setter ) );
    CHECK( via_kind == via_setter );
}

TEST_CASE( "light-changed with the visibility option matches the paired invalidates",
           "[level_cache_freshness]" ) {
    set_up_open_daylight_map();
    map &here = get_map();
    generation_baseline base = capture_generations( here );
    const std::vector<std::string> plain = capture( here, base );
    level_cache_freshness::invalidate_lightmap( here );
    level_cache_freshness::invalidate_visibility( here );
    const std::vector<std::string> via_setters = capture( here, base );
    CHECK( via_setters != plain );

    set_up_open_daylight_map();
    base = capture_generations( here );
    const std::vector<std::string> plain_again = capture( here, base );
    level_cache_freshness::report( here, level_cache_freshness::light_changed {
        .visibility = true,
    } );
    const std::vector<std::string> via_kind = capture( here, base );
    CHECK( via_kind != plain_again );

    INFO( "setters only:\n" << only_in( via_setters, via_kind )
          << "kind only:\n" << only_in( via_kind, via_setters ) );
    CHECK( via_kind == via_setters );
}

// The freshness writes `map::on_vehicle_moved` performed before ticket #14 routed it
// through the `vehicle_moved` kind, spelled out from the pre-migration source. The
// notification counter and the GPU residency push stay at the call site and are not
// freshness, so they are absent here; so is `pf_dirty`, which the capture excludes.
void reference_vehicle_move_sequence( map &here, const tripoint_bub_sm &sm_min,
                                      const tripoint_bub_sm &sm_max, const int smz ) {
    level_cache &ch = here.access_cache( smz );
    level_cache_freshness::mark( ch,
        freshness_parts( { level_cache_part::veh_in_active_range } ) );
    level_cache_freshness::mark_vehicle_caches( here, smz );
    level_cache_freshness::invalidate_lightmap( here );
    level_cache_freshness::forget_solar_hour( here );
    level_cache_freshness::mark_seen( here, smz );
    level_cache_freshness::mark( ch, freshness_parts( { level_cache_part::visibility } ) );

    const auto mark_rect = [&]( const int x0, const int y0, const int x1, const int y1,
    const level_cache_part part ) {
        level_cache &level = here.access_cache( smz );
        for( int x = x0; x <= x1; ++x ) {
            for( int y = y0; y <= y1; ++y ) {
                level_cache_freshness::mark( level, freshness_parts( { part } ),
                                             static_cast<size_t>( level.bidx( x, y ) ) );
                if( submap * const sm = here.get_submap_at_grid(
                        tripoint_bub_sm( point_bub_sm( x, y ), smz ) ) ) {
                    if( part == level_cache_part::transparency ) {
                        sm->transparency_dirty = true;
                    } else if( part == level_cache_part::outside ) {
                        sm->outside_dirty = true;
                    } else {
                        sm->floor_dirty = true;
                    }
                }
            }
        }
    };
    mark_rect( sm_min.x(), sm_min.y(), sm_max.x(), sm_max.y(), level_cache_part::transparency );
    mark_rect( sm_min.x(), sm_min.y(), sm_max.x(), sm_max.y(), level_cache_part::floor );
    mark_rect( sm_min.x() - 1, sm_min.y() - 1, sm_max.x() + 1, sm_max.y() + 1,
               level_cache_part::outside );

    // Vehicles can extend through the floor: the level above gets seen, visibility and
    // floor over the occupancy rectangle only.
    const int above_z = smz + 1;
    level_cache_freshness::mark_seen( here, above_z );
    level_cache_freshness::mark( here.access_cache( above_z ),
                                 freshness_parts( { level_cache_part::visibility } ) );
    level_cache &ch_above = here.access_cache( above_z );
    for( int x = sm_min.x(); x <= sm_max.x(); ++x ) {
        for( int y = sm_min.y(); y <= sm_max.y(); ++y ) {
            level_cache_freshness::mark( ch_above, freshness_parts( { level_cache_part::floor } ),
                                         static_cast<size_t>( ch_above.bidx( x, y ) ) );
            if( submap * const sm = here.get_submap_at_grid(
                    tripoint_bub_sm( point_bub_sm( x, y ), above_z ) ) ) {
                sm->floor_dirty = true;
            }
        }
    }
}

TEST_CASE( "vehicle-moved matches the pre-migration on_vehicle_moved sequence",
           "[level_cache_freshness][vehicle]" ) {
    const tripoint_bub_sm sm_min( 3, 4, 0 );
    const tripoint_bub_sm sm_max( 4, 5, 0 );

    set_up_open_daylight_map();
    map &here = get_map();
    // Both arms call `invalidate_lightmap()`, which marks visibility on every
    // level, so the direct visibility marks of the vehicle path are structurally
    // redundant and no capture can see them dropped. Clearing first only keeps the
    // starting state honest (the bit otherwise arrives pre-dirtied).
    level_cache_freshness::clear_visibility( here );
    generation_baseline base = capture_generations( here );
    reference_vehicle_move_sequence( here, sm_min, sm_max, 0 );
    const std::vector<std::string> via_setters = capture( here, base );

    set_up_open_daylight_map();
    level_cache_freshness::clear_visibility( here );
    base = capture_generations( here );
    // The real entry point, which now reports the kind; the notification counter it
    // raises is an observation for the GPU consumer, not freshness.
    here.on_vehicle_moved( sm_min, sm_max, 0 );
    here.take_vehicle_move_notifications();
    const std::vector<std::string> via_kind = capture( here, base );

    INFO( "setters only:\n" << only_in( via_setters, via_kind )
          << "kind only:\n" << only_in( via_kind, via_setters ) );
    CHECK( via_kind == via_setters );
}

TEST_CASE(
    "player-moved and z-level-changed raise no dirty bit, like the origin check",
    "[level_cache_freshness]" ) {
    set_up_open_daylight_map();
    map &here = get_map();
    generation_baseline base = capture_generations( here );
    const std::vector<std::string> untouched = capture( here, base );

    set_up_open_daylight_map();
    base = capture_generations( here );
    level_cache_freshness::report( here, level_cache_freshness::player_moved {} );
    const std::vector<std::string> via_kind = capture( here, base );

    // Issue #5: a player move inside the bubble raises no dirty bit at all; what forces
    // the seen rebuild is the stored origin no longer matching the viewer. The kind is
    // the faithful translation of that fact, so it raises nothing either.
    INFO( "kind only:\n" << only_in( via_kind, untouched ) );
    CHECK( via_kind == untouched );

    set_up_open_daylight_map();
    base = capture_generations( here );
    level_cache_freshness::report( here, level_cache_freshness::z_level_changed {} );
    CHECK( capture( here, base ) == untouched );
}

TEST_CASE(
    "player-moved feeds the same seen rebuild as a real viewer move",
    "[level_cache_freshness]" ) {
    // Discriminating test: a poisoned seen-cache entry survives a refresh when the
    // stored origin still matches the viewer, and is overwritten when the origin was
    // forgotten. This is the mechanism `build_map_cache` uses (the origin comparison at
    // its seen phase), so the kind must trip it exactly like a real move does.
    const tripoint_bub_ms sample( 61, 60, 0 );
    const auto poison_and_check = [&]( const bool report_move ) {
        set_up_open_daylight_map();
        map &here = get_map();
        level_cache &ch = here.access_cache( 0 );
        const size_t bit = static_cast<size_t>( ch.idx( sample.x(), sample.y() ) );
        REQUIRE( ch.seen_cache[bit] > 0.0f ); // visible before poisoning
        ch.seen_cache[bit] = 0.0f;
        if( report_move ) {
            level_cache_freshness::report( here, level_cache_freshness::player_moved {} );
        }
        refresh_view();
        return here.access_cache( 0 ).seen_cache[bit];
    };

    SECTION( "without the kind the poisoned entry survives" ) {
        CHECK( poison_and_check( false ) == 0.0f );
    }
    SECTION( "the kind forces the rebuild that restores it" ) {
        CHECK( poison_and_check( true ) > 0.0f );
    }
}

// The freshness writes `map::shift` performs, spelled out from the real code
// (src/map.cpp: forget_seen_origin; per level the memory-seen re-memorise queue, the
// translate of transparency/floor/outside then lightmap then absorption, the edge bands
// via the mapbuffer, seen for |z - player_z| <= 1, the visibility bit, the suspension
// flag; finally the support-loss translate). #14 replaces those calls with the kind, so
// this reference is the sequence the kind must reproduce.
void reference_shift_sequence( map &here, const point_rel_sm &sp, const int player_z ) {
    level_cache_freshness::forget_seen_origin( here );
    const int mapsize = here.getmapsize();
    const auto bands = [&]( const int gridz, const int count, const auto &mark ) {
        for( int band = 0; band < count; ++band ) {
            for( int smx = 0; smx < mapsize; ++smx ) {
                for( int smy = 0; smy < mapsize; ++smy ) {
                    const bool edge_column = sp.x() > 0 ? smx == mapsize - 1 - band
                                             : sp.x() < 0 && smx == band;
                    const bool edge_row = sp.y() > 0 ? smy == mapsize - 1 - band
                                        : sp.y() < 0 && smy == band;
                    if( edge_column || edge_row ) {
                        mark( tripoint_bub_sm( smx, smy, gridz ) );
                    }
                }
            }
        }
    };
    for( int gridz = -OVERMAP_DEPTH; gridz <= OVERMAP_HEIGHT; ++gridz ) {
        level_cache &gc = here.access_cache( gridz );
        level_cache_freshness::queue_memory_seen_all( gc );
        level_cache_freshness::translate( gc, freshness_parts( {
            level_cache_part::transparency, level_cache_part::floor,
            level_cache_part::outside } ), sp, mapsize );
        level_cache_freshness::translate( gc,
            freshness_parts( { level_cache_part::lightmap } ), sp, mapsize );
        level_cache_freshness::translate( gc,
            freshness_parts( { level_cache_part::absorption } ), sp, mapsize );
        const auto mark_edge = [&]( const level_cache_part part, const int count,
        const bool buffer_flag ) {
            bands( gridz, count, [&]( const tripoint_bub_sm & smp ) {
                level_cache_freshness::mark( gc, freshness_parts( { part } ),
                                            static_cast<size_t>( gc.bidx( smp.x(), smp.y() ) ) );
                if( buffer_flag ) {
                    const tripoint_abs_sm abs_sm = map_local_to_abs( here, smp );
                    here.get_mapbuffer().mark_submap_caches_dirty( {
                        .begin = abs_sm.xy(),
                        .end = abs_sm.xy() + point_rel_sm( 1, 1 ),
                        .zlev = abs_sm.z(),
                        .transparency = part == level_cache_part::transparency,
                        .floor = part == level_cache_part::floor,
                        .outside = part == level_cache_part::outside,
                        .absorption = part == level_cache_part::absorption,
                    } );
                }
            } );
        };
        mark_edge( level_cache_part::floor, 1, true );
        mark_edge( level_cache_part::outside, 3, true );
        mark_edge( level_cache_part::transparency, 3, true );
        mark_edge( level_cache_part::lightmap, 1, false );
        mark_edge( level_cache_part::absorption, 2, true );
        if( std::abs( gridz - player_z ) <= 1 ) {
            level_cache_freshness::mark_seen( here, gridz );
        }
        level_cache_freshness::mark_visibility( here, gridz );
        level_cache_freshness::mark_suspension( here, gridz );
    }
    const half_open_rectangle<point_bub_ms> boundaries_2d( point_bub_ms::zero(),
        point_bub_ms( g_mapsize_x, g_mapsize_y ) );
    level_cache_freshness::translate_support_losses( here,
        point_rel_ms( -sp.x() * SEEX, -sp.y() * SEEY ), boundaries_2d );
}

TEST_CASE( "map-shifted matches the shift's translate-plus-edge-dirt sequence",
           "[level_cache_freshness]" ) {
    const point_rel_sm shift( 1, 0 );
    const int player_z = 0;

    set_up_open_daylight_map();
    map &here = get_map();
    generation_baseline base = capture_generations( here );
    reference_shift_sequence( here, shift, player_z );
    const std::vector<std::string> via_reference = capture( here, base );

    set_up_open_daylight_map();
    base = capture_generations( here );
    level_cache_freshness::report( here, level_cache_freshness::map_shifted {
        .shift = shift, .player_z = player_z
    } );
    const std::vector<std::string> via_kind = capture( here, base );

    INFO( "reference only:\n" << only_in( via_reference, via_kind )
          << "kind only:\n" << only_in( via_kind, via_reference ) );
    CHECK( via_kind == via_reference );
}

TEST_CASE( "map-shifted translates carried dirt and never blankets",
           "[level_cache_freshness]" ) {
    set_up_open_daylight_map();
    map &here = get_map();
    const int mapsize = here.getmapsize();
    level_cache &ch = here.access_cache( 0 );
    // An interior submap with carried dirt, and a clean interior neighbour away from
    // every edge band.
    const int dirty_sx = mapsize / 2;
    const int clean_sx = mapsize / 2 + 2;
    level_cache_freshness::mark( ch, freshness_parts( { level_cache_part::floor } ),
                                 static_cast<size_t>( ch.bidx( dirty_sx, 5 ) ) );
    level_cache_freshness::report( here, level_cache_freshness::map_shifted {
        .shift = point_rel_sm( 1, 0 ), .player_z = 0
    } );

    // The dirt travelled with the cache it describes: the old slot is clean again and
    // the slot the submap slid into is dirty.
    CHECK_FALSE( level_cache_freshness::stale( ch, level_cache_part::floor,
                static_cast<size_t>( ch.bidx( dirty_sx, 5 ) ) ) );
    CHECK( level_cache_freshness::stale( ch, level_cache_part::floor,
               static_cast<size_t>( ch.bidx( dirty_sx - 1, 5 ) ) ) );
    // A retained interior submap that was clean stayed clean: the shift dirtied the
    // shifted-in edge, not the whole map.
    CHECK_FALSE( level_cache_freshness::stale( ch, level_cache_part::floor,
                static_cast<size_t>( ch.bidx( clean_sx, 5 ) ) ) );
    CHECK_FALSE( level_cache_freshness::stale( ch, level_cache_part::transparency,
                static_cast<size_t>( ch.bidx( clean_sx, 5 ) ) ) );
    CHECK_FALSE( level_cache_freshness::stale( ch, level_cache_part::lightmap,
                static_cast<size_t>( ch.bidx( clean_sx, 5 ) ) ) );
    // The shifted-in edge column is dirty for every part the shift dirties there.
    for( const level_cache_part part : { level_cache_part::floor, level_cache_part::outside,
            level_cache_part::transparency, level_cache_part::lightmap,
            level_cache_part::absorption } ) {
        INFO( part_name( part ) );
        CHECK( level_cache_freshness::stale( ch, part,
                   static_cast<size_t>( ch.bidx( mapsize - 1, 5 ) ) ) );
    }
}

TEST_CASE( "world-replaced matches the non-incremental loadn setter sequence",
           "[level_cache_freshness]" ) {
    const int mapsize = get_map().getmapsize();
    const tripoint_bub_sm first( 0, 0, -OVERMAP_DEPTH );
    const tripoint_bub_sm last( mapsize - 1, mapsize - 1, OVERMAP_HEIGHT );

    // The non-incremental branch of `map::loadn` (map.cpp) applied per level over the
    // whole bubble: the sequence a bulk world load runs.
    const auto reference_sequence = [&]( map &here ) {
        for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
            level_cache_freshness::mark_transparency( here, z );
            level_cache_freshness::mark_floor( here, z );
            level_cache_freshness::mark_outside( here, z );
            level_cache_freshness::mark_absorption( here, z );
            level_cache_freshness::mark_seen( here, z );
            level_cache_freshness::mark_suspension( here, z );
            level_cache_freshness::mark( here.access_cache( z ),
                                         freshness_parts( { level_cache_part::lightmap } ) );
            level_cache_freshness::mark_vehicle_caches( here, z );
        }
    };

    set_up_open_daylight_map();
    map &here = get_map();
    generation_baseline base = capture_generations( here );
    reference_sequence( here );
    const std::vector<std::string> via_loadn = capture( here, base );

    set_up_open_daylight_map();
    base = capture_generations( here );
    level_cache_freshness::report( here, level_cache_freshness::world_replaced {
        .first = first, .last = last
    } );
    const std::vector<std::string> via_kind = capture( here, base );

    INFO( "loadn sequence only:\n" << only_in( via_loadn, via_kind )
          << "kind only:\n" << only_in( via_kind, via_loadn ) );
    CHECK( via_kind == via_loadn );
}

TEST_CASE( "restricted world-replaced matches the partial bulk sequences",
           "[level_cache_freshness]" ) {
    // Two callers replace less than everything: an OMT regeneration leaves the seen
    // cache, the lightmap and the vehicles alone; a background repaint additionally
    // leaves the floor family alone.  The part options must reproduce the old setter
    // sequences exactly, couplings included.
    const int mapsize = get_map().getmapsize();
    const auto whole_bubble = [&]( const int z ) {
        return level_cache_freshness::world_replaced {
            .first = tripoint_bub_sm( 0, 0, z ),
            .last = tripoint_bub_sm( mapsize - 1, mapsize - 1, z ),
        };
    };

    // The regeneration shape: transparency (with its absorption coupling), outside,
    // floor with the z-1 cascade, absorption, suspension.
    set_up_open_daylight_map();
    map &here = get_map();
    generation_baseline base = capture_generations( here );
    level_cache_freshness::mark_transparency( here, 0 );
    level_cache_freshness::mark_outside( here, 0 );
    level_cache_freshness::mark_floor( here, 0 );
    level_cache_freshness::mark_absorption( here, 0 );
    level_cache_freshness::mark_suspension( here, 0 );
    const std::vector<std::string> via_setters = capture( here, base );

    set_up_open_daylight_map();
    base = capture_generations( here );
    level_cache_freshness::report( here, [&] {
        auto change = whole_bubble( 0 );
        change.seen = false;
        change.lightmap = false;
        change.vehicle = false;
        return change;
    }() );
    const std::vector<std::string> via_kind = capture( here, base );

    INFO( "setters only:\n" << only_in( via_setters, via_kind )
          << "kind only:\n" << only_in( via_kind, via_setters ) );
    CHECK( via_kind == via_setters );

    // The repaint shape: transparency (absorption coupling again), seen, outside.
    set_up_open_daylight_map();
    base = capture_generations( here );
    level_cache_freshness::mark_transparency( here, 0 );
    level_cache_freshness::mark_seen( here, 0 );
    level_cache_freshness::mark_outside( here, 0 );
    const std::vector<std::string> via_paint = capture( here, base );

    set_up_open_daylight_map();
    base = capture_generations( here );
    level_cache_freshness::report( here, [&] {
        auto change = whole_bubble( 0 );
        change.lightmap = false;
        change.floor = false;
        change.suspension = false;
        change.vehicle = false;
        return change;
    }() );
    const std::vector<std::string> via_kind_paint = capture( here, base );

    INFO( "paint setters only:\n" << only_in( via_paint, via_kind_paint )
          << "paint kind only:\n" << only_in( via_kind_paint, via_paint ) );
    CHECK( via_kind_paint == via_paint );
}

TEST_CASE( "world-replaced makes the caches see terrain swapped underneath them",
           "[level_cache_freshness]" ) {
    // Issue #7's mechanism, documented and pinned: terrain replaced under a resident
    // submap is only picked up when BOTH the Level cache bitsets and the submap dirty
    // flags rise. The co-op tile sync raises the submap flags only, so the builders
    // early-return on their clean bitsets and the level arrays keep describing the old
    // world. No co-op code is involved; the swap is simulated directly on the submap.
    const tripoint_bub_ms opaque_at( 61, 60, 0 );   // t_dirt -> t_wall: opacity
    const tripoint_bub_ms roof_at( 61, 60, 1 );     // t_floor -> t_open_air: floor/sky

    struct observables {
        int transparency;
        int floor_here;
        int outside_below;
    };
    const auto read_observables = [&]( map &here ) {
        const level_cache &ch0 = here.access_cache( 0 );
        const level_cache &ch1 = here.access_cache( 1 );
        return observables {
            static_cast< int >( std::lround( 1000.0f *
                ch0.transparency_cache[ch0.idx( opaque_at.x(), opaque_at.y() )] ) ),
            static_cast< int >( ch1.floor_cache[ch1.idx( roof_at.x(), roof_at.y() )] ),
            static_cast< int >( ch0.outside_cache[ch0.idx( roof_at.x(), roof_at.y() )] ),
        };
    };
    const auto fixture = [&]( map &here ) {
        set_up_open_daylight_map();
        // `build_test_map` only fills z=0, so the roof needs a real placement — through
        // the real mutator, so the caches are consistent before the swap. A 3x3 block:
        // `outside_cache` is dilated over the 3x3 tiles above, so a lone floored tile
        // surrounded by open air would already read as outside.
        for( int dx = -1; dx <= 1; ++dx ) {
            for( int dy = -1; dy <= 1; ++dy ) {
                REQUIRE( here.ter_set( roof_at + tripoint( dx, dy, 0 ), ter_id( "t_floor" ) ) );
            }
        }
        refresh_level_cache();
    };
    const auto silent_swap = [&]( map &here ) {
        swap_terrain_silently( here, opaque_at, ter_id( "t_wall" ) );
        swap_terrain_silently( here, roof_at, ter_id( "t_open_air" ) );
    };

    map &here = get_map();
    fixture( here );
    const observables before = read_observables( here );
    // Pre-state the pin depends on: open ground, floored above, sky blocked above.
    REQUIRE( before.transparency != static_cast< int >( LIGHT_TRANSPARENCY_SOLID ) );
    REQUIRE( before.floor_here != 0 );
    REQUIRE( before.outside_below == 0 );

    SECTION( "submap flags alone leave the level arrays stale (the issue #7 defect)" ) {
        silent_swap( here );
        // What the co-op tile sync does: the submap dirty flags, nothing else.
        for( const tripoint_bub_ms &p : { opaque_at, roof_at } ) {
            point_sm_ms loc;
            submap *const sm = here.get_submap_at( p, loc );
            REQUIRE( sm != nullptr );
            sm->transparency_dirty = true;
            sm->floor_dirty = true;
            sm->outside_dirty = true;
            sm->absorption_dirty = true;
        }
        refresh_view();
        const observables after = read_observables( here );
        CHECK( after.transparency == before.transparency );
        CHECK( after.floor_here == before.floor_here );
        CHECK( after.outside_below == before.outside_below );
    }

    SECTION( "world-replaced over the area makes the arrays reflect the swap" ) {
        silent_swap( here );
        const tripoint_bub_sm sm0 = project_to<coords::sm>( opaque_at );
        level_cache_freshness::report( here, level_cache_freshness::world_replaced {
            .first = sm0, .last = tripoint_bub_sm( sm0.xy(), 1 )
        } );
        refresh_view();
        const observables after = read_observables( here );
        CHECK( after.transparency != before.transparency );
        CHECK( after.floor_here == 0 );
        CHECK( after.outside_below != 0 );
    }
}

TEST_CASE(
    "terrain-changed with a seen probe matches the vehicle part-edit opacity pair",
    "[level_cache_freshness][vehicle]" ) {
    // `vehicle::open_or_close`, the bicycle-rack merge, the split and the part-removal
    // handler all dirty the whole level's transparency cache and then probe the seen
    // cache at a tile that is NOT the vehicle (the bubble origin, or the part's own
    // tile). That probe is conditional inside `mark_seen`, so which tile it
    // lands on is observable and the kind must carry it.
    const tripoint_bub_ms vehicle_tile( 61, 60, 0 );

    set_up_open_daylight_map();
    map &here = get_map();
    generation_baseline base = capture_generations( here );
    level_cache_freshness::mark_transparency( here, 0 );
    level_cache_freshness::mark_seen( here, tripoint_bub_ms::zero() );
    const std::vector<std::string> via_setters = capture( here, base );

    set_up_open_daylight_map();
    base = capture_generations( here );
    level_cache_freshness::report( here, level_cache_freshness::terrain_changed {
        .at = vehicle_tile,
        .transparency = true,
        .scope = level_cache_freshness::terrain_changed::transparency_scope::level,
        .seen_probe = tripoint_bub_ms::zero(),
        .support_above = false,
        .memory_seen = false,
    } );
    const std::vector<std::string> via_kind = capture( here, base );

    INFO( "setters only:\n" << only_in( via_setters, via_kind )
          << "kind only:\n" << only_in( via_kind, via_setters ) );
    CHECK( via_kind == via_setters );

    // Discriminating: the seen dirtying is conditional on the content of the PROBE tile,
    // so the probe point is observable. Poisoning the probe tile's seen entry makes the
    // conditional decline; a probe elsewhere still sees lit content and dirties seen.
    const auto seen_rises = [&]( const tripoint_bub_ms &probe,
    const tripoint_bub_ms &poison ) {
        set_up_open_daylight_map();
        map &m = get_map();
        level_cache &ch = m.access_cache( 0 );
        REQUIRE( !level_cache_freshness::stale( ch, level_cache_part::seen ) );
        ch.seen_cache[static_cast<size_t>( ch.idx( poison.x(), poison.y() ) )] = 0.0f;
        ch.camera_cache[static_cast<size_t>( ch.idx( poison.x(), poison.y() ) )] = 0.0f;
        level_cache_freshness::report( m, level_cache_freshness::terrain_changed {
            .at = vehicle_tile,
            .transparency = true,
            .scope = level_cache_freshness::terrain_changed::transparency_scope::level,
            .seen_probe = probe,
            .support_above = false,
            .memory_seen = false,
        } );
        return level_cache_freshness::stale( m.access_cache( 0 ), level_cache_part::seen );
    };
    CHECK_FALSE( seen_rises( tripoint_bub_ms::zero(), tripoint_bub_ms::zero() ) );
    CHECK( seen_rises( player_home, tripoint_bub_ms::zero() ) );
    CHECK_FALSE( seen_rises( player_home, player_home ) );
}

TEST_CASE(
    "whole-level floor plus vehicle-caches bookkeeping match the part-removal handler",
    "[level_cache_freshness][vehicle]" ) {
    const int z = 0;

    set_up_open_daylight_map();
    map &here = get_map();
    generation_baseline base = capture_generations( here );
    level_cache_freshness::mark_floor( here, z + 1 );
    level_cache_freshness::mark_vehicle_caches( here, z );
    const std::vector<std::string> via_setters = capture( here, base );

    set_up_open_daylight_map();
    base = capture_generations( here );
    level_cache_freshness::report( here, level_cache_freshness::terrain_changed {
        .at = tripoint_bub_ms( 0, 0, z + 1 ),
        .floor_level = true,
        .support_above = false,
        .memory_seen = false,
    } );
    level_cache_freshness::mark_vehicle_caches( here, z );
    const std::vector<std::string> via_kind = capture( here, base );

    INFO( "setters only:\n" << only_in( via_setters, via_kind )
          << "kind only:\n" << only_in( via_kind, via_setters ) );
    CHECK( via_kind == via_setters );
}

TEST_CASE(
    "light-changed with no lightmap change matches the camera-toggle pair",
    "[level_cache_freshness][vehicle]" ) {
    set_up_open_daylight_map();
    map &here = get_map();
    generation_baseline base = capture_generations( here );
    level_cache_freshness::mark_seen( here, 0 );
    level_cache_freshness::invalidate_visibility( here );
    const std::vector<std::string> via_setters = capture( here, base );

    set_up_open_daylight_map();
    base = capture_generations( here );
    level_cache_freshness::report( here, level_cache_freshness::light_changed {
        .at = player_home,
        .scope = level_cache_freshness::light_changed::lightmap_scope::none,
        .visibility = true,
        .seen = true,
    } );
    const std::vector<std::string> via_kind = capture( here, base );

    INFO( "setters only:\n" << only_in( via_setters, via_kind )
          << "kind only:\n" << only_in( via_kind, via_setters ) );
    CHECK( via_kind == via_setters );
}
