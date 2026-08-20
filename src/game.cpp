#include "game.h"
#include "coop_server.h"
#include "coop_client.h"
#include "coop_session.h"

#include "camera_debug.h"

#include <algorithm>
#include <bitset>
#include <cassert>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwctype>
#include <exception>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <numeric>
#include <queue>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "achievement.h"
#include "action.h"
#include "action_time_scale.h"
#include "activity_time_cadence.h"
#include "activity_actor.h"
#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "activity_type.h"
#include "activity_monmove_cache.h"
#include "armor_layers.h"
#include "artifact.h"
#include "auto_note.h"
#include "auto_pickup.h"
#include "avatar.h"
#include "avatar_action.h"
#include "avatar_functions.h"
#include "batch_turns.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "catalua_bindings_coords_common.h"
#include "cata_cartesian_product.h"
#include "cata_utility.h"
#include "catalua_hooks.h"
#include "catalua_sol.h"
#include "cached_options.h"
#include "catacharset.h"
#include "character.h"
#include "character_display.h"
#include "character_functions.h"
#include "character_martial_arts.h"
#include "character_turn.h"
#include "clzones.h"
#include "color.h"
#include "computer_session.h"
#include "construction.h"
#include "construction_group.h"
#include "context_menu.h"
#include "coordinates.h"
#include "crafting.h"
#include "creature_tracker.h"
#include "monster.h"
#include "monster_action.h"
#include "monster_plan.h"
#include "thread_pool.h"
#include "cursesport.h"
#include "damage.h"
#include "debug.h"
#include "dependency_tree.h"
#include "diary.h"
#include "distraction_manager.h"
#include "active_tile_data_def.h"
#include "distribution_grid.h"
#include "drop_token.h"
#include "fluid_grid.h"
#include "editmap.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "explosion_queue.h"
#include "faction.h"
#include "field.h"
#include "field_type.h"
#include "filesystem.h"
#include "flag_trait.h"
#include "flag.h"
#include "fstream_utils.h"
#include "game_constants.h"
#include "game_inventory.h"
#include "game_ui.h"
#include "gamemode.h"
#include "gates.h"
#include "harvest.h"
#include "help.h"
#include "iexamine.h"
#include "init.h"
#include "mapgen_async.h"
#include "input.h"
#include "int_id.h"
#include "inventory.h"
#include "item.h"
#include "item_category.h"
#include "item_contents.h"
#include "item_functions.h"
#include "item_stack.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "json.h"
#include "kill_tracker.h"
#include "lighting/rmlui_layer.h"
#include "lightmap.h"
#include "line.h"
#include "live_view.h"
#include "loading_ui.h"
#include "locations.h"
#include "npc.h"
#include "magic.h"
#include "map.h"
#include "physics/physics_world.h"
#include "map_functions.h"
#include "map_item_stack.h"
#include "map_iterator.h"
#include "map_selector.h"
#include "mapbuffer.h"
#include "mapbuffer_registry.h"
#include "mapdata.h"
#include "mapsharing.h"
#include "memorial_logger.h"
#include "memory_fast.h"
#include "messages.h"
#include "mission.h"
#include "mod_manager.h"
#include "monattack.h"
#include "monexamine.h"
#include "monfaction.h"
#include "monstergenerator.h"
#include "morale_types.h"
#include "mtype.h"
#include "mutation.h"
#include "npc_class.h"
#include "omdata.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmap_ui.h"
#include "overmapbuffer.h"
#include "panels.h"
#include <RmlUi/Core.h>
#include "rml_screen.h"
#include "rml_util.h"
#include "sidebar_anim.h"
#include "path_info.h"
#include "pathfinding.h"
#include "pickup.h"
#include "utils/pit_trap_helpers.h"
#include "player.h"
#include "player_activity.h"
#include "point_float.h"
#include "popup.h"
#include "profession.h"
#include "profile.h"
#include "ranged.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "ret_val.h"
#include "rng.h"
#include "rot.h"
#include "safemode_ui.h"
#include "salvage.h"
#include "scenario.h"
#include "scent_map.h"
#include "scores_ui.h"
#include "sdl_cursor.h"
#include "sdl_render_frame.h"
#include "sdl_wrappers.h"
#include "sdltiles.h"
#include "sounds.h"
#include "start_location.h"
#include "stats_tracker.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_input_popup.h"
#include "fire_spread_loader.h"
#include "submap.h"
#include "submap_fields.h"
#include "type_id.h"
#include "tileray.h"
#include "timed_event.h"
#include "translations.h"
#include "trap.h"
#include "ui.h"
#include "ui_manager.h"
#include "uistate.h"
#include "units.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "veh_interact.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_grab.h"
#include "vehicle_part.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "wcwidth.h"
#include "weather.h"
#include "world_type.h"
#include "worldfactory.h"
#include "location_vector.h"
#include "monfaction.h"
#if defined( CATA_SDL )
#include "compute/gpu_lm.h"
#include "compute/gpu_platform.h"
#endif
class computer;

#include "cata_tiles.h"

#if defined(_WIN32)
#if 1 // HACK: Hack to prevent reordering of #include "platform_win.h" by IWYU
#   include "platform_win.h"
#endif
#   include <tchar.h>
#endif

#define dbg(x) DebugLogFL((x),DC::Game)

// Runtime reality-bubble configuration globals (declared in game_constants.h).
// Initialised by init_bubble_config() from the REALITY_BUBBLE_SIZE option.
// Defaults match size=4 (player-facing radius), which reproduces the original 11×11 submap grid.
int g_reality_bubble_size = 4;
int g_half_mapsize = 5;
int g_mapsize = 11;
int g_mapsize_x = 132;
int g_mapsize_y = 132;
int g_half_mapsize_x = 60;
int g_half_mapsize_y = 60;
int g_max_view_distance = 60;
// Visibility threshold at max view distance. Computed as 1/exp(LIGHT_TRANSPARENCY_OPEN_AIR * g_max_view_distance).
// Default matches the old hardcoded 0.1 threshold for g_max_view_distance=60.
float g_visible_threshold = 0.1f;

/// Update all runtime globals from an explicit bubble size value.
static void init_bubble_config( int size )
{
    g_reality_bubble_size = size;
    // g_half_mapsize = size + 1 (the center submap is the implied +1).
    // Formula: radius = size+1, grid = (2*radius+1)^2 submaps.
    g_half_mapsize        = size + 1;
    g_mapsize             = 2 * g_half_mapsize + 1;
    g_mapsize_x           = SEEX * g_mapsize;
    g_mapsize_y           = SEEY * g_mapsize;
    g_half_mapsize_x      = SEEX * g_half_mapsize;
    g_half_mapsize_y      = SEEY * g_half_mapsize;
    g_max_view_distance   = SEEX * g_half_mapsize;
    // Compute visibility threshold so the "obstructed" cutoff scales with view distance.
    // At g_max_view_distance tiles through clear air, visibility = 1/exp(t*d) = g_visible_threshold.
    // This replaces the old hardcoded 0.1 threshold (which assumed g_max_view_distance=60).
    g_visible_threshold   = 1.0f / std::exp( LIGHT_TRANSPARENCY_OPEN_AIR * g_max_view_distance );
}

/// Read REALITY_BUBBLE_SIZE from options and update all runtime globals.
/// Must be called before map construction (game::setup) and after each load.
static void init_bubble_config()
{
    init_bubble_config( get_option<int>( "REALITY_BUBBLE_SIZE" ) );
}

static auto discard_monster_map_for_loaded_bubble( map &here,
        const std::string &dimension_id ) -> void
{
    const auto origin = here.get_abs_sub();
    const auto zmin = here.has_zlevels() ? -OVERMAP_DEPTH : origin.z();
    const auto zmax = here.has_zlevels() ? OVERMAP_HEIGHT : origin.z();
    const auto z_range = std::views::iota( zmin, zmax + 1 );
    const auto xy_range = std::views::iota( 0, g_mapsize );
    std::ranges::for_each(
        cata::views::cartesian_product( z_range, xy_range, xy_range ),
    [&]( auto tup ) {
        const auto [gz, gx, gy] = tup;
        get_overmapbuffer( dimension_id ).discard_monster_map(
            tripoint_abs_sm{ origin.x() + gx, origin.y() + gy, gz } );
    } );
}

static constexpr int DANGEROUS_PROXIMITY = 5;

static const activity_id ACT_OPERATION( "ACT_OPERATION" );
static const activity_id ACT_AUTODRIVE( "ACT_AUTODRIVE" );

static const skill_id skill_melee( "melee" );
static const skill_id skill_dodge( "dodge" );
static const skill_id skill_firstaid( "firstaid" );
static const skill_id skill_survival( "survival" );
static const skill_id skill_electronics( "electronics" );
static const skill_id skill_computer( "computer" );

static const species_id PLANT( "PLANT" );

static const std::string flag_AUTODOC_COUCH( "AUTODOC_COUCH" );

static const efftype_id effect_accumulated_mutagen( "accumulated_mutagen" );
static const efftype_id effect_adrenaline_mycus( "adrenaline_mycus" );
static const efftype_id effect_ai_controlled( "ai_controlled" );
static const efftype_id effect_ai_waiting( "ai_waiting" );
static const efftype_id effect_assisted( "assisted" );
static const efftype_id effect_blind( "blind" );
static const efftype_id effect_bouldering( "bouldering" );
static const efftype_id effect_contacts( "contacts" );
static const efftype_id effect_docile( "docile" );
static const efftype_id effect_downed( "downed" );
static const efftype_id effect_drunk( "drunk" );
static const efftype_id effect_evil( "evil" );
static const efftype_id effect_feral_killed_recently( "feral_killed_recently" );
static const efftype_id effect_flu( "flu" );
static const efftype_id effect_infected( "infected" );
static const efftype_id effect_laserlocked( "laserlocked" );
static const efftype_id effect_lying_down( "lying_down" );
static const efftype_id effect_monster_disarmed( "monster_disarmed" );
static const efftype_id effect_no_sight( "no_sight" );
static const efftype_id effect_npc_suspend( "npc_suspend" );
static const efftype_id effect_onfire( "onfire" );
static const efftype_id effect_pacified( "pacified" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_ridden( "ridden" );
static const efftype_id effect_riding( "riding" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_stunned( "stunned" );
static const efftype_id effect_tied( "tied" );
static const efftype_id dashing_effect( "dashing" );

static const bionic_id bio_remote( "bio_remote" );
static const bionic_id bio_probability_travel( "bio_probability_travel" );

static const itype_id itype_battery( "battery" );
static const itype_id itype_grapnel( "grapnel" );
static const itype_id itype_holybook_bible1( "holybook_bible1" );
static const itype_id itype_holybook_bible2( "holybook_bible2" );
static const itype_id itype_holybook_bible3( "holybook_bible3" );
static const itype_id itype_manhole_cover( "manhole_cover" );
static const itype_id itype_rm13_armor_on( "rm13_armor_on" );
static const itype_id itype_rope_30( "rope_30" );
static const itype_id itype_swim_fins( "swim_fins" );

static const trait_id trait_BADKNEES( "BADKNEES" );
static const trait_id trait_ILLITERATE( "ILLITERATE" );
static const trait_id trait_LEG_TENT_BRACE( "LEG_TENT_BRACE" );
static const trait_id trait_M_IMMUNE( "M_IMMUNE" );
static const trait_id trait_PROF_FERAL( "PROF_FERAL" );
static const trait_id trait_VINES2( "VINES2" );
static const trait_id trait_VINES3( "VINES3" );
static const trait_id trait_THICKSKIN( "THICKSKIN" );
static const trait_id trait_WEB_ROPE( "WEB_ROPE" );
static const trait_id trait_INATTENTIVE( "INATTENTIVE" );
static const trait_id trait_WAYFARER( "WAYFARER" );
static const trait_id trait_HAS_NEMESIS( "HAS_NEMESIS" );
static const trait_id trait_DEBUG_INFINITE_SPEED( "DEBUG_INFINITE_SPEED" );

static const trait_flag_str_id trait_flag_MUTATION_FLIGHT( "MUTATION_FLIGHT" );
static const trait_flag_str_id trait_flag_MUTATION_SWIM( "MUTATION_SWIM" );

static const trap_str_id tr_unfinished_construction( "tr_unfinished_construction" );

static const faction_id your_followers( "your_followers" );

//The one and only game instance
std::unique_ptr<game> g;

std::atomic<uint32_t> g_npc_friends_dirty_version{ 1 };
uint32_t g_npcmove_attitude_epoch{ 0 };

//The one and only uistate instance
uistatedata uistate;

bool is_valid_in_w_terrain( point p )
{
    return p.x >= 0 && p.x < TERRAIN_WINDOW_WIDTH && p.y >= 0 && p.y < TERRAIN_WINDOW_HEIGHT;
}

static void achievement_attained( const achievement *a )
{
    g->u.add_msg_if_player( m_good, _( "You completed the achievement \"%s\"." ),
                            a->name() );
}

// This is the main game set-up process.
game::game() :
    map_ptr( 1, true ),
    liveview( *liveview_ptr ),
    scent_ptr( *this, *map_ptr ),
    achievements_tracker_ptr( *stats_tracker_ptr, *kill_tracker_ptr, achievement_attained ),
    m( *map_ptr ),
    u( *u_ptr ),
    scent( *scent_ptr ),
    timed_events( *timed_event_manager_ptr ),
    uquit( QUIT_NO ),
    new_game( false ),
    safe_mode( SAFE_MODE_ON ),
    mostseen( 0 ),
    u_shared_ptr( &u, null_deleter{} ),
    safe_mode_warning_logged( false ),
    next_npc_id( 1 ),
    next_mission_id( 1 ),
    remoteveh_cache_time( calendar::before_time_starts ),
    user_action_counter( 0 ),
    tileset_zoom( DEFAULT_TILESET_ZOOM ),
    seed( 0 ),
    last_mouse_edge_scroll( std::chrono::steady_clock::now() ),
    fake_items( new temp_item_location( ) )
{
    // Force thread pool startup before first turn to avoid a latency spike.
    get_thread_pool();

    // Create the primary dimension's grid tracker (key ""); other dimensions
    // are constructed lazily on first use.
    grid_trackers_[""] = std::make_unique<distribution_grid_tracker>( MAPBUFFER, "" );
    submap_loader.add_listener( grid_trackers_[""].get() );

    first_redraw_since_waiting_started = true;
    reset_light_level();
    events().subscribe( &*kill_tracker_ptr );
    events().subscribe( &*stats_tracker_ptr );
    events().subscribe( &*memorial_logger_ptr );
    events().subscribe( &*achievements_tracker_ptr );
    events().subscribe( &*spell_events_ptr );
    world_generator = std::make_unique<worldfactory>();
    // do nothing, everything that was in here is moved to init_data() which is called immediately after g = new game; in main.cpp
    // The reason for this move is so that g is not uninitialized when it gets to installing the parts into vehicles.
}

game::~game() = default;

// Load everything that will not depend on any mods
void game_ui::init_ui()
{
    // clear the screen
    static bool first_init = true;

    if( first_init ) {

        first_init = false;

        //class variable to track the option being active
        //only set once, toggle action is used to change during game
        pixel_minimap_option = get_option<bool>( "PIXEL_MINIMAP" );
    }

    // First get TERMX, TERMY
    TERMX = get_terminal_width();
    TERMY = get_terminal_height();

    get_options().get_option( "TERMINAL_X" ).setValue( TERMX * get_scaling_factor() );
    get_options().get_option( "TERMINAL_Y" ).setValue( TERMY * get_scaling_factor() );
    get_options().save();
}

// temporarily switch out of fullscreen for functions that rely
// on displaying some part of the sidebar
/*
 * Initialize more stuff after mapbuffer is loaded.
 */
bool game::has_gametype() const
{
    return gamemode && gamemode->id() != special_game_type::NONE;
}

special_game_type game::gametype() const
{
    return gamemode ? gamemode->id() : special_game_type::NONE;
}

std::optional<tripoint_bub_ms> game::find_local_stairs_leading_to( map &mp, const int z_after )
{
    const int movez = z_after - get_levz();
    const bool going_down = movez == -1;
    const bool going_up = movez == 1;

    //i tried 40, 80, and 100 here and got the same result almost every time? works for our purposes though
    for( const tripoint_bub_ms &candidate : closest_points_first( u.bub_pos(), 80 ) ) {
        if( ( going_up && ( mp.has_flag( TFLAG_GOES_UP, candidate ) ||
                            mp.has_flag( TFLAG_ELEVATOR, candidate ) ) ) ||
            ( going_down && ( mp.has_flag( TFLAG_GOES_DOWN, candidate ) ||
                              mp.has_flag( TFLAG_ELEVATOR, candidate ) ) ) ) {
            return candidate;
        }
    }

    return std::nullopt;
}

void game::suggest_auto_walk_to_stairs( Character &u, map &m, const std::string &direction )
{
    const bool can_autowalk_stairs = get_option<bool>( "SUGGEST_AUTOWALK_STAIRCASE" );

    if( !can_autowalk_stairs ) {
        return;
    }

    const int z_after = direction == "up" ? u.bub_pos().z() + 1 : u.bub_pos().z() - 1;
    std::optional<tripoint_bub_ms> stair_pos = find_local_stairs_leading_to( m, z_after );

    if( !stair_pos || !u.sees( *stair_pos ) ) {
        return;
    }

    auto route = m.route( u.bub_pos(), *stair_pos, u.get_legacy_pathfinding_settings(),
                          u.get_legacy_path_avoid() );
    if( route.size() <= 1 ) {
        return;
    }

    // Detect if it's an elevator
    bool is_elevator = m.has_flag( TFLAG_ELEVATOR, *stair_pos );
    // Choose message depending on type
    std::string dir_text;
    if( !is_elevator ) {
        dir_text = direction == "up" ? " (up)" : " (down)";
    }
    if( query_yn( "Walk to %s%s?", m.ter( *stair_pos ).obj().name(), dir_text ) ) {
        route.pop_back();
        u.set_destination( route, u.remove_activity() );
        u.activity = std::make_unique<player_activity>();
    }
}

// Set up all default values for a new game
vehicle *game::place_vehicle_nearby(
    const vproto_id &id, const point_abs_omt &origin, int min_distance,
    int max_distance, const std::vector<std::string> &omt_search_types,
    bool notwater )
{
    std::vector<std::string> search_types = omt_search_types;
    if( search_types.empty() ) {
        vehicle veh( id );
        if( !notwater && veh.can_float() && veh.has_part( VPFLAG_FLOATS ) ) {
            search_types.emplace_back( "river_shore" );
            search_types.emplace_back( "lake_shore" );
            search_types.emplace_back( "lake_surface" );
        } else {
            search_types.emplace_back( "field" );
            search_types.emplace_back( "road" );
        }
    }
    // TODO: Pull-up find_params and use that scan result instead
    // find nearest road
    omt_find_params find_params;
    for( const std::string &search_type : search_types ) {
        find_params.types.emplace_back( search_type, ot_match_type::type );
    }
    find_params.search_range = { min_distance, max_distance };
    find_params.search_layers = std::nullopt;

    // if player spawns underground, park their car on the surface.
    const tripoint_abs_omt omt_origin( origin, 0 );
    for( const tripoint_abs_omt &goal : get_overmapbuffer( current_dimension_id_ ).find_all( omt_origin,
            find_params ) ) {
        // try place vehicle there.
        tinymap target_map;
        target_map.load( project_to<coords::sm>( goal ), false );
        const tripoint_bub_ms tinymap_center( SEEX, SEEY, goal.z() );
        static constexpr std::array<units::angle, 4> angles = {{
                0_degrees, 90_degrees, 180_degrees, 270_degrees
            }
        };
        vehicle *veh = target_map.add_vehicle(
                           id, tinymap_center, random_entry( angles ), rng( 50, 80 ),
                           0, false, false, true );
        if( veh ) {
            auto abs = target_map.bub_to_abs( tinymap_center );
            const auto proj = project_remain<coords::sm>( abs );
            veh->abs_sm_pos = proj.quotient_tripoint;
            veh->sm_ms_pos = proj.remainder;
            veh->dimension_id_ = target_map.get_bound_dimension();
            get_overmapbuffer( veh->dimension_id_ ).add_vehicle( veh );
            veh->tracking_on = true;
            return veh;
        }
    }
    return nullptr;
}

//Make any nearby overmap npcs active, and put them in the right location.
static std::string generate_memorial_filename( const std::string &char_name )
{
    // <name>-YYYY-MM-DD-HH-MM-SS.txt
    //       123456789012345678901234 ~> 24 chars + a null
    constexpr size_t suffix_len = 24 + 1;
    constexpr size_t max_name_len = FILENAME_MAX - suffix_len;

    const size_t name_len = char_name.size();
    // Here -1 leaves space for the ~
    const size_t truncated_name_len = ( name_len >= max_name_len ) ? ( max_name_len - 1 ) : name_len;

    std::ostringstream memorial_file_path;

    memorial_file_path << ensure_valid_file_name( char_name );

    // Add a ~ if the player name was actually truncated.
    memorial_file_path << ( ( truncated_name_len != name_len ) ? "~-" : "-" );

    // Add a timestamp for uniqueness.
    char buffer[suffix_len] {};
    std::time_t t = std::time( nullptr );
    std::strftime( buffer, suffix_len, "%Y-%m-%d-%H-%M-%S", std::localtime( &t ) );
    memorial_file_path << buffer;

    return memorial_file_path.str();
}

bool &death_rip_rmlui_enabled()
{
    // Default ON — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

static int veh_lumi( vehicle &veh )
{
    float veh_luminance = 0.0;
    float iteration = 1.0;
    auto lights = veh.lights( true );

    for( const auto pt : lights ) {
        const auto &vp = pt->info();
        if( vp.has_flag( VPFLAG_CONE_LIGHT ) ||
            vp.has_flag( VPFLAG_WIDE_CONE_LIGHT ) ) {
            veh_luminance += vp.bonus / iteration;
            iteration = iteration * 1.1;
        }
    }
    // Calculation: see lightmap.cpp
    return LIGHT_RANGE( ( veh_luminance * 3 ) );
}

void game::calc_driving_offset( vehicle *veh )
{
    if( veh == nullptr || !get_option<bool>( "DRIVING_VIEW_OFFSET" ) ) {
        set_driving_view_offset( point_zero );
        return;
    }
    const int g_light_level = static_cast<int>( light_level( u.bub_pos().z() ) );
    const int light_sight_range = u.sight_range( g_light_level );
    int sight = std::max( veh_lumi( *veh ), light_sight_range );

    // The maximal offset will leave at least this many tiles
    // between the PC and the edge of the main window.
    static const int border_range = 2;
    point max_offset( ( getmaxx( w_terrain ) + 1 ) / 2 - border_range - 1,
                      ( getmaxy( w_terrain ) + 1 ) / 2 - border_range - 1 );

    // velocity at or below this results in no offset at all
    static const float min_offset_vel = 1 * vehicles::cmps_per_tile;
    // velocity at or above this results in maximal offset
    static const float max_offset_vel = std::min( max_offset.y, max_offset.x ) *
                                        vehicles::cmps_per_tile;
    float velocity = veh->velocity;
    rl_vec2d offset = veh->move_vec();
    if( !veh->skidding && veh->player_in_control( u ) &&
        std::abs( veh->cruise_velocity - veh->velocity ) < 7 * vehicles::cmps_per_tile ) {
        // Use the cruise controlled velocity, but only if
        // it is not too different from the actual velocity.
        // The actual velocity changes too often (see above slowdown).
        // Using it makes would make the offset change far too often.
        offset = veh->face_vec();
        velocity = veh->cruise_velocity;
    }
    float rel_offset;
    if( std::fabs( velocity ) < min_offset_vel ) {
        rel_offset = 0;
    } else if( std::fabs( velocity ) > max_offset_vel ) {
        rel_offset = ( velocity > 0 ) ? 1 : -1;
    } else {
        rel_offset = ( velocity - min_offset_vel ) / ( max_offset_vel - min_offset_vel );
    }
    // Squeeze into the corners, by making the offset vector longer,
    // the PC is still in view as long as both offset.x and
    // offset.y are <= 1
    if( std::fabs( offset.x ) > std::fabs( offset.y ) && std::fabs( offset.x ) > 0.2 ) {
        offset.y /= std::fabs( offset.x );
        offset.x = ( offset.x > 0 ) ? +1 : -1;
    } else if( std::fabs( offset.y ) > 0.2 ) {
        offset.x /= std::fabs( offset.y );
        offset.y = offset.y > 0 ? +1 : -1;
    }
    offset.x *= rel_offset;
    offset.y *= rel_offset;
    offset.x *= max_offset.x;
    offset.y *= max_offset.y;
    // [ ----@---- ] sight=6
    // [ --@------ ] offset=2
    // [ -@------# ] offset=3
    // can see sights square in every direction, total visible area is
    // (2*sight+1)x(2*sight+1), but the window is only
    // getmaxx(w_terrain) x getmaxy(w_terrain)
    // The area outside of the window is maxoff (sight-getmax/2).
    // If that value is <= 0, the whole visible area fits the window.
    // don't apply the view offset at all.
    // If the offset is > maxoff, only apply at most maxoff, everything
    // above leads to invisible area in front of the car.
    // It will display (getmax/2+offset) squares in one direction and
    // (getmax/2-offset) in the opposite direction (centered on the PC).
    const point maxoff( ( sight * 2 + 1 - getmaxx( w_terrain ) ) / 2,
                        ( sight * 2 + 1 - getmaxy( w_terrain ) ) / 2 );
    if( maxoff.x <= 0 ) {
        offset.x = 0;
    } else if( offset.x > 0 && offset.x > maxoff.x ) {
        offset.x = maxoff.x;
    } else if( offset.x < 0 && -offset.x > maxoff.x ) {
        offset.x = -maxoff.x;
    }
    if( maxoff.y <= 0 ) {
        offset.y = 0;
    } else if( offset.y > 0 && offset.y > maxoff.y ) {
        offset.y = maxoff.y;
    } else if( offset.y < 0 && -offset.y > maxoff.y ) {
        offset.y = -maxoff.y;
    }

    // Turn the offset into a vector that increments the offset toward the desired position
    // instead of setting it there instantly, should smooth out jerkiness.
    const point offset_difference( -driving_view_offset + point( offset.x, offset.y ) );

    const point offset_sign( ( offset_difference.x < 0 ) ? -1 : 1,
                             ( offset_difference.y < 0 ) ? -1 : 1 );
    // Shift the current offset in the direction of the calculated offset by one tile
    // per draw event, but snap to calculated offset if we're close enough to avoid jitter.
    offset.x = ( std::abs( offset_difference.x ) > 1 ) ?
               ( driving_view_offset.x + offset_sign.x ) : offset.x;
    offset.y = ( std::abs( offset_difference.y ) > 1 ) ?
               ( driving_view_offset.y + offset_sign.y ) : offset.y;

    set_driving_view_offset( point( offset.x, offset.y ) );
}

auto game::advance_time_action_tick() -> int
{
    const auto calendar_turns = action_time_scale::calendar_turns_for_next_tick(
                                    time_action_scale_turn_remainder );
    action_time_scale::set_calendar_turns_this_tick( calendar_turns );
    calendar::turn += time_duration::from_turns( calendar_turns );
    return calendar_turns;
}

// MAIN GAME LOOP
// Returns true if game is over (death, saved, quit, etc)
bool game::do_turn()
{
    ZoneScopedN( "game::do_turn" );
    const auto reset_time_action_tick = on_out_of_scope( [this]() {
        action_time_scale::set_calendar_turns_this_tick_to_next_tick(
            time_action_scale_turn_remainder );
    } );
    // perf probe: per-turn SIM cost (post-input) + the big sub-phases, rolling
    // avg every 120 turns. Renders are ~1ms but frames are ~30ms apart while
    // moving — this finds where the per-turn time actually goes.
    using _perf_clk = std::chrono::steady_clock;
    static double _perf_sim = 0.0, _perf_cache = 0.0, _perf_mon = 0.0, _perf_world = 0.0;
    static int    _perf_n = 0;
    {
        ZoneScopedN( "do_turn_initial_cleanup" );
        cleanup_arenas();
        if( is_game_over() ) {
            return cleanup_at_end();
        }
    }
    if( new_game ) {
        new_game = false;
    }
    if( try_activity_fixed_window_skip() ) {
        return false;
    }
    const bool asleep = u.in_sleep_state();
    const auto vehperf = asleep && !character_funcs::is_driving( u ) &&
                         get_option<bool>( "SLEEP_SKIP_VEH" );
    const auto soundperf = asleep && get_option<bool>( "SLEEP_SKIP_SOUND" );
    const auto monperf = asleep && get_option<bool>( "SLEEP_SKIP_MON" );
    const auto npcperf = asleep && get_option<bool>( "SLEEP_SKIP_NPC" );
    {
        ZoneScopedN( "do_turn_population_plots" );
        TracyPlot( "Total Monsters", static_cast<int64_t>( critter_tracker->size() ) );
        auto total_npcs = int64_t{ 0 };
        auto simulated_npcs = int64_t{ 0 };
        for( const shared_ptr_fast<npc> &guy : active_npc ) {
            if( !guy || guy->is_dead() ) {
                continue;
            }
            ++total_npcs;
            if( guy->is_simulated() ) {
                ++simulated_npcs;
            }
        }
        TracyPlot( "Total NPCs", total_npcs );
        TracyPlot( "Total Simulated NPCs", simulated_npcs );
    }
    // Actual stuff
    {
        ZoneScopedN( "do_turn_calendar" );
        if( !gamemode ) {
            gamemode = std::make_unique<special_game>();
        }
        gamemode->per_turn();
        advance_time_action_tick();
    }
    swapping_dimensions = false;

    // Mark all visibility caches dirty for this turn.  The first redraw will run
    // update_visibility_cache; subsequent redraws within the same turn skip it.
    // Lightmap is NOT blanket-invalidated here — per-submap dirty tracking handles
    // the incremental rebuild; only submaps with actual changes are rebuilt.
    {
        ZoneScopedN( "do_turn_invalidate_visibility" );
        m.invalidate_visibility_caches();
    }

    // starting a new turn, clear out temperature cache
    weather_manager &weather = get_weather();
    {
        ZoneScopedN( "do_turn_clear_temp_cache" );
        weather.clear_temp_cache();
    }

    if( npcs_dirty ) {
        ZoneScopedN( "do_turn_load_npcs" );
        load_npcs();
    }

    {
        ZoneScopedN( "do_turn_timed_events" );
        timed_events.process();
    }
    {
        ZoneScopedN( "do_turn_missions" );
        mission::process_all();
    }
    // If controlling a vehicle that is owned by someone else
    if( u.in_vehicle && u.controlling_vehicle ) {
        vehicle *veh = veh_pointer_or_null( m.veh_at( u.bub_pos() ) );
        if( veh && !veh->handle_potential_theft( u, true ) ) {
            veh->handle_potential_theft( u, false, false );
        }
    }
    // If riding a horse - chance to spook
    if( u.is_mounted() ) {
        u.check_mount_is_spooked();
    }
    if( action_time_scale::once_every_this_tick( 1_days ) ) {
        ZoneScopedN( "do_turn_overmap_mongroups" );
        get_overmapbuffer( current_dimension_id_ ).process_mongroups();
    }

    // Move hordes every 2.5 min
    if( action_time_scale::once_every_this_tick( time_duration::from_minutes( 2.5 ) ) ) {
        ZoneScopedN( "do_turn_overmap_hordes" );
        get_overmapbuffer( current_dimension_id_ ).move_hordes();
        if( u.has_trait( trait_HAS_NEMESIS ) ) {
            get_overmapbuffer( current_dimension_id_ ).move_nemesis();
        }
        // Hordes that reached the reality bubble need to spawn,
        // make them spawn in invisible areas only.
        m.spawn_monsters( false );
    }

    debug_hour_timer.print_time();

    {
        ZoneScopedN( "do_turn_update_body" );
        u.update_body();
    }

    // Auto-save if autosave is enabled
    if( get_option<bool>( "AUTOSAVE" ) &&
        action_time_scale::once_every_this_tick( 1_turns * get_option<int>( "AUTOSAVE_TURNS" ) ) &&
        !u.is_dead_state() ) {
        ZoneScopedN( "do_turn_autosave" );
        autosave();
    }

    {
        ZoneScopedN( "do_turn_weather_update" );
        weather.update_weather();
        reset_light_level();
    }

    {
        ZoneScopedN( "do_turn_pre_action_updates" );
        perhaps_add_random_npc();
        process_voluntary_act_interrupt();
        process_activity();
        update_performance_bubble();
    }
    if( !soundperf ) {
        ZoneScopedN( "do_turn_player_sound" );
        // Sound information and is broken up into three main blocks: Player, Monsters, NPCs
        // Player is special in that they are immediatly informed of the sounds they made on their turn for displayed sound marker purposes
        // Each sound block is generally a map::cull_heard_sounds(), feeding the AI in question remaining sounds, and then moving said AI.
        // Cull stale sounds that have been heard by all parties, we need to do this three times per cycle.
        // We do this before each respective party's turn to hear noise.
        // This block should catch stale sounds from NPCs.
        m.cull_heard_sounds();
        // Process sound events into sound markers for display to the player.
        sounds::process_sound_markers( &u );

        if( u.is_deaf() ) {
            sfx::do_hearing_loss();
        }
    }

    if( !u.has_effect( effect_sleep ) || uquit == QUIT_WATCH ) {
        if( u.moves > 0 || uquit == QUIT_WATCH ) {
            ZoneScopedN( "do_turn_player_action_loop" );
            while( u.moves > 0 || uquit == QUIT_WATCH ) {
                cleanup_dead();
                mon_info_update();
                // Process any new sounds the player caused during their turn.
                if( !soundperf ) {
                    sounds::process_sound_markers( &u );
                }
                if( !u.activity && !u.has_distant_destination() && uquit != QUIT_WATCH && wait_popup ) {
                    wait_popup.reset();
                    ui_manager::redraw();
                }

                if( queue_screenshot ) {
                    invalidate_main_ui_adaptor();
                    ui_manager::redraw();
                    take_screenshot();
                    queue_screenshot = false;
                }

                const auto moves_before_action = u.moves;
                auto handled_action = false;
                {
                    ZoneScopedN( "do_turn_handle_action" );
                    handled_action = handle_action();
                }
                if( handled_action ) {
                    ++moves_since_last_save;
                }

                if( !soundperf && u.moves != moves_before_action ) {
                    sounds::reset_markers();
                    u.volume = 0;
                }

                if( is_game_over() ) {
                    return cleanup_at_end();
                }

                if( !soundperf && u.moves <= 0 ) {
                    const auto is_unheard_by_player = []( const auto & sound ) {
                        return !sound.heard_by_player;
                    };
                    const auto has_unheard_player_sounds = std::ranges::any_of(
                            m.m_sound_cache.sound_instances, is_unheard_by_player );
                    if( has_unheard_player_sounds ) {
                        sounds::process_sound_markers( &u );
                    }
                }

                if( uquit == QUIT_WATCH ) {
                    break;
                }
                if( u.activity ) {
                    process_activity();
                }
            }
        }
    }

    if( driving_view_offset.x != 0 || driving_view_offset.y != 0 ) {
        ZoneScopedN( "do_turn_driving_offset" );
        // Still have a view offset, but might not be driving anymore,
        // or the option has been deactivated,
        // might also happen when someone dives from a moving car.
        // or when using the handbrake.
        vehicle *veh = veh_pointer_or_null( m.veh_at( u.bub_pos() ) );
        calc_driving_offset( veh );
    }

    // perf probe: sim_total spans the whole post-input world+sim block; the
    // _perf_world window below isolates the pre-AI world tick
    // (scent/falling/vehmove/process_items/grids/fluid).
    const auto _perf_sim_t0 = _perf_clk::now();
    // No-scent debug mutation has to be processed here or else it takes time to start working
    {
        ZoneScopedN( "do_turn_scent" );
        if( !u.has_active_bionic( bionic_id( "bio_scent_mask" ) ) &&
            !u.has_trait( trait_id( "DEBUG_NOSCENT" ) ) ) {
            scent.set( u.bub_pos(), u.scent, u.get_type_of_scent() );
            get_overmapbuffer( current_dimension_id_ ).set_scent( u.abs_omt_pos(),  u.scent );
        }
        scent.update( u.bub_pos(), m );
    }

    // We need floor cache before checking falling 'n stuff
    {
        ZoneScopedN( "do_turn_build_floor_caches" );
        m.build_floor_caches();
    }

    if( !vehperf ) {
        ZoneScopedN( "do_turn_vehicle_physics" );
        m.process_falling();
        autopilot_vehicles();
        m.vehmove();
    }
    {
        ZoneScopedN( "do_turn_process_items" );
        m.process_items();
    }
    {
        ZoneScopedN( "do_turn_creature_in_field" );
        m.creature_in_field( u );
    }
    {
        ZoneScopedN( "do_turn_distribution_trackers" );
        for( auto &[dim_id, tracker_ptr] : grid_trackers_ ) {
            if( tracker_ptr ) {
                tracker_ptr->update( calendar::turn );
            }
        }
    }
    {
        ZoneScopedN( "do_turn_dimension_ticks" );
        tick_portal_links();
        tick_temporary_pocket_dimensions();
        tick_vehicle_portal_taps();
    }
    {
        ZoneScopedN( "do_turn_fluid_grid" );
        fluid_grid::update( calendar::turn );
    }

    _perf_world += std::chrono::duration<double, std::milli>( _perf_clk::now() - _perf_sim_t0 ).count();
    // Update vision caches for monsters. If this turns out to be expensive,
    // consider a stripped down cache just for monsters.
    {
        ZoneScopedN( "do_turn_monster_visibility_cache" );
        const auto _t0 = _perf_clk::now();
        m.build_map_cache( get_levz(), true );
        _perf_cache += std::chrono::duration<double, std::milli>( _perf_clk::now() - _t0 ).count();
    }
    // This has to be done after updating our map caches, as sound propagation relies on terrain.
    if( !soundperf ) {
        ZoneScopedN( "do_turn_sound_before_monsters" );
        // Cull stale sounds that have been heard by everyone. Should nominally catch all stale sounds made by the player on the prior turn.
        m.cull_heard_sounds();
        // Apply sounds from previous turn to monster AI.
        // Process sounds marks all sounds in the sound_caches vector as heard by monsters.
        sounds::process_sounds();
    }

    if( !monperf ) {
        ZoneScopedN( "do_turn_monmove" );
        const auto _t0 = _perf_clk::now();
        monmove();
        _perf_mon += std::chrono::duration<double, std::milli>( _perf_clk::now() - _t0 ).count();
    }

    if( !soundperf ) {
        ZoneScopedN( "do_turn_sound_before_npcs" );
        // Cull any noises that have already been heard by everyone. This should generally cull all stale sounds made by monsters on the prior turn.
        m.cull_heard_sounds();
        // Batch floodfill sounds made by monsters or other qued sources.
        m.batch_flood_fill_sounds();
        // Apply remaining sounds to NPC AI here so that they are reacting to the most recent monster noises and player noises, not recent player noises and prior turn monster noises.
        // process_sounds_npc also marks all sounds present in the vector as heard by npcs.
        sounds::process_sounds_npc();
    }

    if( !npcperf ) {
        ZoneScopedN( "do_turn_npcmove" );
        npcmove();
    } else {
        ZoneScopedN( "do_turn_sleep_skip_npc_process" );
        sleep_skip_npc_process();
    }
    if( action_time_scale::once_every_this_tick( 5_minutes ) ) {
        ZoneScopedN( "do_turn_overmap_npc_move" );
        overmap_npc_move();
    }

    if( !soundperf ) {
        ZoneScopedN( "do_turn_sound_after_npcs" );
        // Floodfill any sounds cued up by NPCs during their respective turns or from other sources.
        m.batch_flood_fill_sounds();
    }
    // We want to clear our floodfill que anyways, so that sounds dont accumulate in the que if soundperf is on.
    // This function will also print a debug sound diagnostic to the log if !soundperf.
    {
        ZoneScopedN( "do_turn_clear_floodfill" );
        sounds::clear_floodfill_que( soundperf );
    }
    {
        ZoneScopedN( "do_turn_update_stair_monsters" );
        update_stair_monsters();
    }
    {
        ZoneScopedN( "do_turn_final_mon_info_update" );
        mon_info_update();
    }
    {
        ZoneScopedN( "do_turn_player_process_turn" );
        u.process_turn();
    }

    {
        ZoneScopedN( "do_turn_lua_every_x" );
        cata::run_on_every_x_hooks( *DynamicDataLoader::get_instance().lua );
    }

    {
        ZoneScopedN( "do_turn_explosions" );
        explosion_handler::get_explosion_queue().execute();
    }
    {
        ZoneScopedN( "do_turn_cleanup_dead" );
        cleanup_dead();
    }

    if( u.moves < 0 && get_option<bool>( "FORCE_REDRAW" ) ) {
        ZoneScopedN( "do_turn_force_redraw" );
        ui_manager::redraw();
        refresh_display();
    }

    if( get_levz() >= 0 && !u.is_underwater() ) {
        ZoneScopedN( "do_turn_weather_effects" );
        handle_weather_effects( weather.weather_id );
    }

    {
        ZoneScopedN( "do_turn_wait_activity_redraw" );
        handle_wait_activity_redraw();
    }

    {
        ZoneScopedN( "do_turn_bodytemp_wetness" );
        u.update_bodytemp( m, weather );
        character_funcs::update_body_wetness( u, get_weather().get_precise() );
        u.apply_wetness_morale( weather.temperature );
    }

    if( !u.is_deaf() ) {
        sfx::remove_hearing_loss();
    }
    {
        ZoneScopedN( "do_turn_sfx" );
        sfx::do_danger_music();
        sfx::do_vehicle_engine_sfx();
        sfx::do_vehicle_exterior_engine_sfx();
        sfx::do_fatigue();
    }

    // Tick all loaded submaps: fields for every submap, items/vehicles for batch-eligible ones.
    {
        ZoneScopedN( "do_turn_world_tick" );
        const auto _t0 = _perf_clk::now();
        world_tick();
        _perf_world += std::chrono::duration<double, std::milli>( _perf_clk::now() - _t0 ).count();
    }

    // Fire-spread (and other non-bubble) requests created during world_tick()
    // must be realised before the next turn.  Let the load manager diff
    // the desired set and load/unload as needed.
    // Ensure trackers exist for all active dimensions before update() fires
    // on_submap_loaded events (mirrors the logic in load_map / update_map).
    for( const auto &dim_id : submap_loader.active_dimensions() ) {
        ZoneScopedN( "do_turn_ensure_distribution_trackers" );
        ensure_distribution_grid_tracker_for( dim_id );
    }
    {
        ZoneScopedN( "do_turn_lazy_border_focus" );
        submap_loader.update_lazy_border_focus( current_dimension_id_, u.abs_pos() );
    }
    {
        ZoneScopedN( "do_turn_submap_loader_update" );
        submap_loader.update( is_draw_tiles_mode() );
    }
    // Destroy trackers for non-primary dimensions with no remaining tracked submaps.
    {
        ZoneScopedN( "do_turn_cleanup_distribution_trackers" );
        for( auto it = grid_trackers_.begin(); it != grid_trackers_.end(); ) {
            if( !it->first.empty() && !it->second->has_tracked_submaps() ) {
                submap_loader.remove_listener( it->second.get() );
                it = grid_trackers_.erase( it );
            } else {
                ++it;
            }
        }
    }

    // Finally, clear pathfinding cache
    {
        ZoneScopedN( "do_turn_clear_pathfinding" );
        Pathfinding::clear_d_maps();
    }

    _perf_sim += std::chrono::duration<double, std::milli>( _perf_clk::now() - _perf_sim_t0 ).count();
    if( ++_perf_n >= 20 ) {
        dbg( DL::Info ) << "[sim][perf] " << _perf_n << " turns avg: sim_total="
                        << ( _perf_sim / _perf_n ) << "ms (build_map_cache=" << ( _perf_cache / _perf_n )
                        << " monmove=" << ( _perf_mon / _perf_n ) << " world_tick="
                        << ( _perf_world / _perf_n ) << ")";
        _perf_sim = _perf_cache = _perf_mon = _perf_world = 0.0;
        _perf_n = 0;
    }

    return false;
}

void game::set_driving_view_offset( point p )
{
    // remove the previous driving offset,
    // store the new offset and apply the new offset.
    u.view_offset.x() -= driving_view_offset.x;
    u.view_offset.y() -= driving_view_offset.y;
    driving_view_offset.x = p.x;
    driving_view_offset.y = p.y;
    u.view_offset.x() += driving_view_offset.x;
    u.view_offset.y() += driving_view_offset.y;
}

void game::autopilot_vehicles()
{
    for( wrapped_vehicle &veh : m.get_vehicles() ) {
        vehicle *&v = veh.v;
        if( v->is_following ) {
            v->drive_to_local_target( m.bub_to_abs( u.bub_pos() ), true );
        } else if( v->is_patrolling ) {
            v->autopilot_patrol();
        }
    }
}

void game::catch_a_monster( monster *fish, const tripoint_bub_ms &pos, Character *who,
                            const time_duration &catch_duration ) // catching function
{
    //spawn the corpse, rotten by a part of the duration
    m.add_item_or_charges( pos, item::make_corpse( fish->type->id, calendar::turn + rng( 0_turns,
                           catch_duration ) ) );
    if( u.sees( pos ) ) {
        u.add_msg_if_player( m_good, _( "You caught a %s." ), fish->type->nname() );
    }
    //quietly kill the caught
    fish->no_corpse_quiet = true;
    fish->die( who );
}

static bool cancel_auto_move( Character &who, const std::string &text )
{
    if( who.has_destination() && query_yn( _( "%s Cancel Auto-move?" ), text ) )  {
        add_msg( m_warning, _( "Auto-move canceled." ) );
        if( !who.omt_path.empty() ) {
            who.omt_path.clear();
        }
        who.clear_destination();
        return true;
    }
    return false;
}

bool game::cancel_activity_or_ignore_query( const distraction_type type, const std::string &text )
{
    invalidate_main_ui_adaptor();
    if( ( !u.activity && !u.has_distant_destination() ) ||
        u.activity->is_distraction_ignored( type ) ) {
        return false;
    }
    if( u.has_distant_destination() ) {
        if( cancel_auto_move( u, text ) ) {
            return true;
        } else {
            u.set_destination( u.get_auto_move_route(),
                               std::make_unique<player_activity>( std::make_unique<travelling_activity_actor>() ) );
            return false;
        }
    }

    const bool force_uc = get_option<bool>( "FORCE_CAPITAL_YN" );
    const auto &allow_key = force_uc ? input_context::disallow_lower_case
                            : input_context::allow_all_keys;

    const auto &action = query_popup()
                         .context( "CANCEL_ACTIVITY_OR_IGNORE_QUERY" )
                         .message( force_uc ?
                                   pgettext( "cancel_activity_or_ignore_query",
                                       "<color_light_red>%s %s (Case Sensitive)</color>" ) :
                                   pgettext( "cancel_activity_or_ignore_query",
                                       "<color_light_red>%s %s</color>" ),
                                   text, u.activity->get_stop_phrase() )
                         .option( "YES", allow_key )
                         .option( "NO", allow_key )
                         .option( "MANAGER", allow_key )
                         .option( "IGNORE", allow_key )
                         .query()
                         .action;

    if( action == "YES" ) {
        u.cancel_activity();
        return true;
    }
    if( action == "IGNORE" ) {
        u.activity->ignore_distraction( type );
        for( auto &activity : u.backlog ) {
            activity->ignore_distraction( type );
        }
    }
    if( action == "MANAGER" ) {
        u.cancel_activity();
        get_distraction_manager().show();
        return true;
    }

    ui_manager::redraw();
    refresh_display();

    return false;
}

bool game::cancel_activity_query( const std::string &text )
{
    invalidate_main_ui_adaptor();
    if( u.has_distant_destination() ) {
        if( cancel_auto_move( u, text ) ) {
            return true;
        } else {
            u.set_destination( u.get_auto_move_route(),
                               std::make_unique<player_activity>( std::make_unique<travelling_activity_actor>() ) );
            return false;
        }
    }
    if( !u.activity ) {
        return false;
    }
    if( query_yn( "%s %s", text, u.activity->get_stop_phrase() ) ) {
        u.cancel_activity();
        u.clear_destination();
        u.resume_backlog_activity();
        return true;
    }
    return false;
}

unsigned int game::get_seed() const
{
    return seed;
}

void game::set_critter_died()
{
    critter_died = true;
}

static int maptile_field_intensity( maptile &mt, field_type_id fld )
{
    auto field_ptr = mt.find_field( fld );

    return field_ptr == nullptr ? 0 : field_ptr->get_field_intensity();
}

int get_heat_radiation( const tripoint_bub_ms &location, bool direct )
{
    // Direct heat from fire sources
    // Cache fires to avoid scanning the map around us bp times
    // Stored as intensity-distance pairs
    int temp_mod = 0;
    int best_fire = 0;
    Character &player_character = get_avatar();
    map &here = get_map();
    // Convert it to an int id once, instead of 139 times per turn
    const field_type_id fd_fire_int = fd_fire.id();
    for( const tripoint_bub_ms &dest : here.points_in_radius( location, 6 ) ) {
        int heat_intensity = 0;

        maptile mt = here.maptile_at( tripoint_bub_ms( dest ) );

        int ffire = maptile_field_intensity( mt, fd_fire_int );
        if( ffire > 0 ) {
            heat_intensity = ffire;
        } else  {
            heat_intensity = mt.get_ter()->heat_radiation;
        }
        if( heat_intensity == 0 ) {
            // No heat source here
            continue;
        }
        if( player_character.bub_pos() == location ) {
            if( !here.pl_line_of_sight( dest, -1 ) ) {
                continue;
            }
        } else if( !here.sees( location, dest, -1 ) ) {
            continue;
        }
        // Ensure fire_dist >= 1 to avoid divide-by-zero errors.
        const int fire_dist = std::max( 1, square_dist( dest, location ) );
        temp_mod += 6 * heat_intensity * heat_intensity / fire_dist;
        best_fire = std::max( best_fire, heat_intensity );
    }
    if( direct ) {
        return best_fire;
    }
    return temp_mod;
}

int get_convection_temperature( const tripoint_bub_ms &location )
{
    int temp_mod = 0;
    map &here = get_map();
    // Directly on lava tiles
    int lava_mod = here.tr_at( location ).loadid == tr_lava ?
                   fd_fire.obj().get_convection_temperature_mod() : 0;
    // Modifier from fields
    for( const auto &fd : here.field_at( location ) ) {
        // Nullify lava modifier when there is open fire
        if( fd.first.obj().has_fire ) {
            lava_mod = 0;
        }
        temp_mod += fd.second.convection_temperature_mod();
    }
    return temp_mod + lava_mod;
}

int game::assign_mission_id()
{
    int ret = next_mission_id;
    next_mission_id++;
    return ret;
}

npc *game::find_npc( character_id id )
{
    // Search all dimensions — the NPC might not be in the active dimension.
    npc *result = nullptr;
    for_each_overmapbuffer( [&]( const std::string &, overmapbuffer & omb ) {
        if( !result ) {
            result = omb.find_npc( id ).get();
        }
    } );
    return result;
}

static void update_faction_api( npc *guy )
{
    if( guy->get_faction_ver() < 2 ) {
        guy->set_fac( your_followers );
        guy->set_faction_ver( 2 );
    }
}

void game::handle_key_blocking_activity()
{
    input_context ctxt = get_default_mode_input_context();
    const std::string action = ctxt.handle_input( 0 );
    bool refresh = true;
    if( action == "pause" || action == "main_menu" ) {
        if( u.activity->interruptable_with_kb ) {
            cancel_activity_query( _( "Confirm:" ) );
        }
    } else if( action == "player_data" ) {
        character_display::disp_info( u );
    } else if( action == "messages" ) {
        Messages::display_messages();
    } else if( action == "help" ) {
        get_help().display_help();
    } else if( action != "HELP_KEYBINDINGS" ) {
        refresh = false;
    }
    if( refresh ) {
        ui_manager::redraw();
        refresh_display();
    }
}

// Checks input to see if mouse was moved and handles the mouse view box accordingly.
// Returns true if input requires breaking out into a game action.
bool game::handle_mouseview( input_context &ctxt, std::string &action )
{
    std::optional<tripoint_bub_ms> liveview_pos;

    do {
        action = ctxt.handle_input();
        if( action == "MOUSE_MOVE" ) {
            const std::optional<tripoint_bub_ms> mouse_pos = ctxt.get_coordinates( w_terrain );
            // Hover-outline: the creature under the cursor (if any) gets a ring.
            if( tilecontext ) {
                tilecontext->set_hover_tile( mouse_pos );
            }
            // Contextual cursor (Step 5). Skip entirely while any RmlUi doc
            // owns the mouse — CSS `cursor:` on its hoverable rows is the
            // pass-through path there (see rmlui_system_interface.h; it's a
            // documented no-op today for every migrated screen alike, not
            // something this change touches). Otherwise hint at what's under
            // the cursor: a crosshair over a creature the player can see, a
            // hand over an adjacent examine target — map has no
            // is_examine_target()/has_examine_target() helper, so this reuses
            // action.h's can_examine_at(), the closest existing equivalent
            // (same predicate the action-menu greys ACTION_EXAMINE with) —
            // else the plain arrow.
            if( !rmlui_layer::capturing_input() ) {
                if( !mouse_pos ) {
                    set_game_cursor( cursor_kind::arrow );
                } else if( const monster *mon = critter_at<monster>( *mouse_pos ); mon && u.sees( *mon ) ) {
                    set_game_cursor( cursor_kind::crosshair );
                } else if( square_dist( mouse_pos->xy(), u.bub_pos().xy() ) <= 1 &&
                           can_examine_at( *mouse_pos ) ) {
                    set_game_cursor( cursor_kind::hand );
                } else {
                    set_game_cursor( cursor_kind::arrow );
                }
            }
            if( mouse_pos && ( !liveview_pos || *mouse_pos != *liveview_pos ) ) {
                liveview_pos = mouse_pos;
                liveview.show( *liveview_pos );
            } else if( !mouse_pos ) {
                liveview_pos.reset();
                liveview.hide();
            }
            ui_manager::redraw();
        }
    } while( action == "MOUSE_MOVE" ); // Freeze animation when moving the mouse

    if( action != "TIMEOUT" ) {
        // Keyboard event, break out of animation loop
        liveview.hide();
        // Hover-outline: clear once the player switches to the keyboard.
        if( tilecontext ) {
            tilecontext->set_hover_tile( std::nullopt );
        }
        if( !rmlui_layer::capturing_input() ) {
            set_game_cursor( cursor_kind::arrow );
        }
        return false;
    }

    // Mouse movement or un-handled key
    return true;
}

std::pair<tripoint_rel_ms, tripoint_rel_ms> game::mouse_edge_scrolling( input_context &ctxt,
        const int speed,
        const tripoint_rel_ms &last, bool iso )
{
    const int rate = get_option<int>( "EDGE_SCROLL" );
    auto ret = std::make_pair( tripoint_rel_ms::zero(), last );
    if( rate == -1 ) {
        // Fast return when the option is disabled.
        return ret;
    }
    // Ensure the parameters are used even if the #if below is false
    ( void ) ctxt;
    ( void ) speed;
    ( void ) iso;
    auto now = std::chrono::steady_clock::now();
    if( now < last_mouse_edge_scroll + std::chrono::milliseconds( rate ) ) {
        return ret;
    } else {
        last_mouse_edge_scroll = now;
    }
    const input_event event = ctxt.get_raw_input();
    if( event.type == input_event_t::mouse ) {
        const point threshold( projected_window_width() / 100, projected_window_height() / 100 );
        if( event.mouse_pos.x <= threshold.x ) {
            ret.first.x() -= speed;
            if( iso ) {
                ret.first.y() -= speed;
            }
        } else if( event.mouse_pos.x >= projected_window_width() - threshold.x ) {
            ret.first.x() += speed;
            if( iso ) {
                ret.first.y() += speed;
            }
        }
        if( event.mouse_pos.y <= threshold.y ) {
            ret.first.y() -= speed;
            if( iso ) {
                ret.first.x() += speed;
            }
        } else if( event.mouse_pos.y >= projected_window_height() - threshold.y ) {
            ret.first.y() += speed;
            if( iso ) {
                ret.first.x() -= speed;
            }
        }
        ret.second = ret.first;
    } else if( event.type == input_event_t::timeout ) {
        ret.first = ret.second;
    }
    return ret;
}

std::pair<tripoint_rel_omt, tripoint_rel_omt> game::mouse_edge_scrolling( input_context &ctxt,
        const int speed,
        const tripoint_rel_omt &last, bool iso )
{
    const auto ret = mouse_edge_scrolling( ctxt, speed, last.reinterpret_as<tripoint_rel_ms>(), iso );
    return std::make_pair( ret.first.reinterpret_as<tripoint_rel_omt>(),
                           ret.second.reinterpret_as<tripoint_rel_omt>() );
}

tripoint_rel_ms game::mouse_edge_scrolling_terrain( input_context &ctxt )
{
    auto ret = mouse_edge_scrolling( ctxt, std::max<int>( DEFAULT_TILESET_ZOOM / tileset_zoom, 1 ),
                                     last_mouse_edge_scroll_vector_terrain, tile_iso );
    last_mouse_edge_scroll_vector_terrain = ret.second;
    last_mouse_edge_scroll_vector_overmap = tripoint_rel_omt::zero();
    return ret.first;
}

tripoint_rel_omt game::mouse_edge_scrolling_overmap( input_context &ctxt )
{
    // overmap has no iso mode
    auto ret = mouse_edge_scrolling( ctxt, 2, last_mouse_edge_scroll_vector_overmap, false );
    last_mouse_edge_scroll_vector_overmap = ret.second;
    last_mouse_edge_scroll_vector_terrain = tripoint_rel_ms::zero();
    return ret.first;
}

input_context get_default_mode_input_context()
{
    input_context ctxt( "DEFAULTMODE" );
    // Because those keys move the character, they don't pan, as their original name says
    ctxt.set_iso( true );
    ctxt.register_action( "UP", to_translation( "Move North" ) );
    ctxt.register_action( "RIGHTUP", to_translation( "Move Northeast" ) );
    ctxt.register_action( "RIGHT", to_translation( "Move East" ) );
    ctxt.register_action( "RIGHTDOWN", to_translation( "Move Southeast" ) );
    ctxt.register_action( "DOWN", to_translation( "Move South" ) );
    ctxt.register_action( "LEFTDOWN", to_translation( "Move Southwest" ) );
    ctxt.register_action( "LEFT", to_translation( "Move West" ) );
    ctxt.register_action( "LEFTUP", to_translation( "Move Northwest" ) );
    ctxt.register_action( "pause" );
    ctxt.register_action( "LEVEL_DOWN", to_translation( "Descend Stairs" ) );
    ctxt.register_action( "LEVEL_UP", to_translation( "Ascend Stairs" ) );
    ctxt.register_action( "toggle_map_memory" );
    ctxt.register_action( "center" );
    ctxt.register_action( "shift_n" );
    ctxt.register_action( "shift_ne" );
    ctxt.register_action( "shift_e" );
    ctxt.register_action( "shift_se" );
    ctxt.register_action( "shift_s" );
    ctxt.register_action( "shift_sw" );
    ctxt.register_action( "shift_w" );
    ctxt.register_action( "shift_nw" );
    ctxt.register_action( "cycle_move" );
    ctxt.register_action( "reset_move" );
    ctxt.register_action( "toggle_run" );
    ctxt.register_action( "toggle_crouch" );
    ctxt.register_action( "open_movement" );
    ctxt.register_action( "open" );
    ctxt.register_action( "close" );
    ctxt.register_action( "smash" );
    ctxt.register_action( "loot" );
    ctxt.register_action( "examine" );
    ctxt.register_action( "advinv" );
    ctxt.register_action( "pickup" );
    ctxt.register_action( "pickup_all" );
    ctxt.register_action( "pickup_feet" );
    ctxt.register_action( "grab" );
    ctxt.register_action( "haul" );
    ctxt.register_action( "butcher" );
    ctxt.register_action( "chat" );
    ctxt.register_action( "look" );
    ctxt.register_action( "peek" );
    ctxt.register_action( "listitems" );
    ctxt.register_action( "zones" );
    ctxt.register_action( "inventory" );
    ctxt.register_action( "compare" );
    ctxt.register_action( "organize" );
    ctxt.register_action( "apply" );
    ctxt.register_action( "apply_wielded" );
    ctxt.register_action( "wear" );
    ctxt.register_action( "take_off" );
    ctxt.register_action( "eat" );
    ctxt.register_action( "open_consume" );
    ctxt.register_action( "read" );
    ctxt.register_action( "wield" );
    ctxt.register_action( "pick_style" );
    ctxt.register_action( "reload_item" );
    ctxt.register_action( "reload_weapon" );
    ctxt.register_action( "reload_wielded" );
    ctxt.register_action( "unload" );
    ctxt.register_action( "unload_all" );
    ctxt.register_action( "throw" );
    ctxt.register_action( "THROW_QUICKSLOT" );
    ctxt.register_action( "fire" );
    // RMB press-to-aim. action_ident( ACTION_AIM_HOLD ) is "aim_hold"; without this
    // registration the DEFAULTMODE context cannot resolve MOUSE_RIGHT_DOWN to it and
    // right-click aiming never starts.
    ctxt.register_action( "aim_hold" );
    ctxt.register_action( "cast_spell" );
    ctxt.register_action( "cast_last_spell" );
    ctxt.register_action( "fire_burst" );
    ctxt.register_action( "select_fire_mode" );
    ctxt.register_action( "select_default_ammo" );
    ctxt.register_action( "drop" );
    ctxt.register_action( "drop_adj" );
    ctxt.register_action( "bionics" );
    ctxt.register_action( "mutations" );
    ctxt.register_action( "sort_armor" );
    ctxt.register_action( "wait" );
    ctxt.register_action( "craft" );
    ctxt.register_action( "recraft" );
    ctxt.register_action( "long_craft" );
    ctxt.register_action( "construct" );
    ctxt.register_action( "disassemble" );
    ctxt.register_action( "salvage" );
    ctxt.register_action( "sleep" );
    ctxt.register_action( "control_vehicle" );
    ctxt.register_action( "auto_travel_mode" );
    ctxt.register_action( "safemode" );
    ctxt.register_action( "autosafe" );
    ctxt.register_action( "autoattack" );
    ctxt.register_action( "ignore_enemy" );
    ctxt.register_action( "whitelist_enemy" );
    ctxt.register_action( "save" );
    ctxt.register_action( "quicksave" );
    ctxt.register_action( "quickload" );
    ctxt.register_action( "SUICIDE" );
    ctxt.register_action( "player_data" );
    ctxt.register_action( "map" );
    ctxt.register_action( "sky" );
    ctxt.register_action( "missions" );
    ctxt.register_action( "factions" );
    ctxt.register_action( "scores" );
    ctxt.register_action( "morale" );
    ctxt.register_action( "messages" );
    ctxt.register_action( "help" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "open_options" );
    ctxt.register_action( "open_autopickup" );
    ctxt.register_action( "open_autonotes" );
    ctxt.register_action( "open_safemode" );
    ctxt.register_action( "open_distraction_manager" );
    ctxt.register_action( "open_color" );
    ctxt.register_action( "open_world_mods" );
    ctxt.register_action( "debug" );
    ctxt.register_action( "lua_console" );
    ctxt.register_action( "lua_reload" );
    ctxt.register_action( "open_wiki" );
    ctxt.register_action( "open_hhg" );
    ctxt.register_action( "debug_scent" );
    ctxt.register_action( "debug_scent_type" );
    ctxt.register_action( "debug_temp" );
    ctxt.register_action( "debug_visibility" );
    ctxt.register_action( "debug_lighting" );
    ctxt.register_action( "debug_radiation" );
    ctxt.register_action( "debug_outside" );
    ctxt.register_action( "debug_submap_grid" );
    ctxt.register_action( "debug_sound_absorption" );
    ctxt.register_action( "debug_sound_walls" );
    ctxt.register_action( "debug_hour_timer" );
    ctxt.register_action( "debug_fps" );
    ctxt.register_action( "debug_mode" );
    ctxt.register_action( "zoom_out" );
    ctxt.register_action( "zoom_in" );
    ctxt.register_action( "toggle_fullscreen" );
    ctxt.register_action( "toggle_pixel_minimap" );
    ctxt.register_action( "toggle_zone_overlay" );
    ctxt.register_action( "toggle_panel_adm" );
    ctxt.register_action( "toggle_soma_detail" );
    ctxt.register_action( "reload_tileset" );
    ctxt.register_action( "toggle_auto_features" );
    ctxt.register_action( "toggle_auto_pulp_butcher" );
    ctxt.register_action( "toggle_auto_mining" );
    ctxt.register_action( "toggle_auto_foraging" );
    ctxt.register_action( "toggle_auto_pickup" );
    ctxt.register_action( "toggle_thief_mode" );
    ctxt.register_action( "diary" );
    ctxt.register_action( "action_menu" );
    ctxt.register_action( "main_menu" );
    ctxt.register_action( "item_action_menu" );
    ctxt.register_action( "ANY_INPUT" );
    ctxt.register_action( "COORDINATE" );
    ctxt.register_action( "MOUSE_MOVE" );
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "SEC_SELECT" );
    ctxt.register_action( "CO_OP_CHAT", to_translation( "Co-op: Send Chat Message" ) );
    ctxt.register_action( "CO_OP_TAP_SHOULDER", to_translation( "Co-op: Tap Partner's Shoulder" ) );
    ctxt.register_action( "CO_OP_EMOTE", to_translation( "Co-op: High Five" ) );
    ctxt.register_action( "CO_OP_STABILIZE", to_translation( "Co-op: Stabilize Downed Partner" ) );
    ctxt.register_action( "CO_OP_PASS_ITEM", to_translation( "Co-op: Pass Item to Partner" ) );
    ctxt.register_action( "CO_OP_MARK_OVERMAP", to_translation( "Co-op: Place Shared Map Marker" ) );
    return ctxt;
}

vehicle *game::remoteveh()
{
    if( calendar::turn == remoteveh_cache_time ) {
        return remoteveh_cache;
    }
    remoteveh_cache_time = calendar::turn;
    std::stringstream remote_veh_string( u.get_value( "remote_controlling_vehicle" ) );
    if( remote_veh_string.str().empty() ||
        ( !u.has_active_bionic( bio_remote ) && !u.has_active_item_with_action( "REMOTEVEH" ) ) ) {
        remoteveh_cache = nullptr;
    } else {
        tripoint_bub_ms vp;
        remote_veh_string >> vp.x() >> vp.y() >> vp.z();
        vehicle *veh = veh_pointer_or_null( m.veh_at( vp ) );
        if( veh && veh->fuel_left( itype_battery, true ) > 0 ) {
            remoteveh_cache = veh;
        } else {
            remoteveh_cache = nullptr;
        }
    }
    return remoteveh_cache;
}

void game::setremoteveh( vehicle *veh )
{
    remoteveh_cache_time = calendar::turn;
    remoteveh_cache = veh;
    if( veh != nullptr && !u.has_active_bionic( bio_remote ) &&
        !u.has_active_item_with_action( "REMOTEVEH" ) ) {
        debugmsg( "Tried to set remote vehicle without bio_remote or remotevehcontrol" );
        veh = nullptr;
    }

    if( veh == nullptr ) {
        u.remove_value( "remote_controlling_vehicle" );
        return;
    }

    std::stringstream remote_veh_string;
    const tripoint_bub_ms vehpos = veh->bub_ms_location();
    remote_veh_string << vehpos.x() << ' ' << vehpos.y() << ' ' << vehpos.z();
    u.set_value( "remote_controlling_vehicle", remote_veh_string.str() );
}

bool game::try_get_left_click_action( action_id &act, const tripoint_bub_ms &mouse_target )
{
    bool new_destination = true;
    if( !destination_preview.empty() ) {
        auto &final_destination = destination_preview.back();
        if( final_destination.x() == mouse_target.x() && final_destination.y() == mouse_target.y() ) {
            // Second click
            new_destination = false;
            u.set_destination( destination_preview );
            destination_preview.clear();
            act = u.get_next_auto_move_direction();
            if( act == ACTION_NULL ) {
                // Something went wrong
                u.clear_destination();
                return false;
            }
        }
    }

    if( new_destination ) {
        destination_preview = m.route( u.bub_pos(), mouse_target, u.get_legacy_pathfinding_settings(),
                                       u.get_legacy_path_avoid() );
        return false;
    }

    return true;
}

bool game::try_get_right_click_action( action_id &act, const tripoint_bub_ms &mouse_target )
{
    const bool cleared_destination = !destination_preview.empty();
    u.clear_destination();
    destination_preview.clear();

    if( cleared_destination ) {
        // Produce no-op if auto-move had just been cleared on this action
        // e.g. from a previous single left mouse click. This has the effect
        // of right-click canceling an auto-move before it is initiated.
        return false;
    }

    // Project Zomboid-style priority chain: a bare right-click while wielding
    // something is a direct combat action with no menu in the way; Shift+
    // right-click (any weapon state), or a right-click while unarmed, falls
    // back to the contextual action menu.
    const bool shift_held = ( SDL_GetModState() & SDL_KMOD_SHIFT ) != 0;
    if( !shift_held && u.is_armed() ) {
        act = u.primary_weapon().is_gun() ? ACTION_FIRE : ACTION_THROW;
        return true;
    }

    return show_tile_context_menu( act, mouse_target );
}

auto game::show_tile_context_menu( action_id &act, const tripoint_bub_ms &target ) -> bool
{
    const bool is_adjacent = square_dist( target.xy(), u.bub_pos().xy() ) <= 1;
    const bool is_self = square_dist( target.xy(), u.bub_pos().xy() ) <= 0;

    // Hotkey hints reuse the same action names/bindings registered in
    // get_default_mode_input_context(); an unbound action just shows no hint
    // instead of a verbose "Unbound globally!".
    const input_context hint_ctxt = get_default_mode_input_context();
    const auto hint_for = [&]( action_id a ) -> std::string {
        const std::string ident = action_ident( a );
        if( hint_ctxt.keys_bound_to( ident ).empty() )
        {
            return std::string();
        }
        return hint_ctxt.get_desc( ident, true );
    };

    std::vector<context_action> actions;
    const auto add_once = [&]( const std::string & label, action_id a ) {
        if( std::ranges::none_of( actions, [a]( const context_action & c ) { return c.act == a; } ) ) {
            actions.push_back( context_action{ .label = label, .hotkey_hint = hint_for( a ), .act = a } );
        }
    };

    if( const monster *const mon = critter_at<monster>( target ); mon && u.sees( *mon ) ) {
        add_once( _( "Look" ), ACTION_LOOK );
        if( u.primary_weapon().is_gun() ) {
            add_once( _( "Fire" ), ACTION_FIRE );
        }
        if( is_adjacent ) {
            add_once( _( "Attack" ), ACTION_AUTOATTACK );
        }
    }
    // The plan's "Trade" entry has no matching action_id in this codebase (no
    // ACTION_TRADE; npc_trading::trade() takes an npc& directly, incompatible
    // with this act-based menu contract, and adding a new action_id touches
    // action.h/action.cpp/keybindings well outside this change's file
    // ownership). "Talk" (ACTION_CHAT) is the closest reachable equivalent —
    // it opens the same chat menu 't' does, which offers "Talk to <npc>" when
    // one is nearby — and is offered on its own.
    if( const npc *const np = critter_at<npc>( target ); np && u.sees( *np ) ) {
        add_once( _( "Look" ), ACTION_LOOK );
        add_once( _( "Talk" ), ACTION_CHAT );
    }
    if( can_interact_at( ACTION_CLOSE, target ) ) {
        add_once( _( "Close" ), ACTION_CLOSE );
    }
    if( can_interact_at( ACTION_OPEN, target ) ) {
        add_once( _( "Open" ), ACTION_OPEN );
    }
    if( can_interact_at( ACTION_PICKUP, target ) ) {
        add_once( _( "Pickup" ), ACTION_PICKUP );
    }
    if( is_adjacent ) {
        add_once( _( "Examine" ), ACTION_EXAMINE );
    }
    if( is_self ) {
        add_once( _( "Pickup" ), ACTION_PICKUP );
        add_once( _( "Wait" ), ACTION_WAIT );
    }
    add_once( _( "Look" ), ACTION_LOOK );

    float mx = 0.0f;
    float my = 0.0f;
    SDL_GetMouseState( &mx, &my );
    const std::optional<action_id> chosen = show_context_menu(
            point( static_cast<int>( mx ), static_cast<int>( my ) ), actions );
    if( !chosen ) {
        return false;
    }
    act = *chosen;
    return true;
}

bool game::is_game_over()
{
    if( uquit == QUIT_WATCH ) {
        // deny player movement and dodging
        u.moves = 0;
        // prevent pain from updating
        u.set_pain( 0 );
        // prevent dodging
        u.dodges_left = 0;
        return false;
    }
    if( uquit == QUIT_DIED ) {
        if( u.in_vehicle ) {
            m.unboard_vehicle( u.bub_pos() );
        }
        // C3: send death-status + inventory drop to host before corpse placement.
        // Hooked at QUIT_DIED (not is_dead_state()) so it only fires once death is
        // final — PROMPT_ON_CHARACTER_DEATH quickload bails out at 3479 before this.
        if( coop_client_ ) { coop_client_->notify_death(); }
        u.place_corpse();
        return true;
    }
    if( uquit == QUIT_SUICIDE ) {
        if( u.in_vehicle ) {
            m.unboard_vehicle( u.bub_pos() );
        }
        // Same hook as QUIT_DIED: send death-status + inventory drop to host.
        if( coop_client_ ) { coop_client_->notify_death(); }
        return true;
    }
    if( uquit != QUIT_NO ) {
        return true;
    }
    // is_dead_state() already checks hp_torso && hp_head, no need to for loop it
    if( u.is_dead_state() ) {
        if( get_option<bool>( "PROMPT_ON_CHARACTER_DEATH" ) &&
            !query_yn(
                _( "Your character is dead, do you accept this?\n\nSelect Yes to abandon the character to their fate, select No to try again." ) ) ) {
            g->quickload();
            return false;
        }

        auto followers = get_follower_list()
        | std::views::transform( [&]( const auto & elem ) { return get_overmapbuffer( current_dimension_id_ ).find_npc( elem ); } )
        | std::views::filter( []( const auto & follower ) { return follower && !follower->is_dead_state(); } )
        | std::ranges::to<std::vector>();

        if( !followers.empty() ) {
            uilist charmenu;
            charmenu.text = _( "Continue as one of your followers?" );
            int charnum = 0;
            for( const auto &follower : followers ) {
                charmenu.addentry( charnum++, true, MENU_AUTOASSIGN, follower->get_name() );
            }
            charmenu.addentry( charnum, true, 'q', _( "No, end the game" ) );
            charmenu.query();
            if( charmenu.ret >= 0 && static_cast<size_t>( charmenu.ret ) < followers.size() ) {
                if( u.in_vehicle ) { m.unboard_vehicle( u.bub_pos() ); }
                uquit = QUIT_NO;
                get_avatar().control_npc( *followers.at( charmenu.ret ) );
                return false;
            }
        }

        Messages::deactivate();
        if( get_option<std::string>( "DEATHCAM" ) == "always" ) {
            uquit = QUIT_WATCH;
        } else if( get_option<std::string>( "DEATHCAM" ) == "ask" ) {
            uquit = query_yn( _( "Watch the last moments of your life…?" ) ) ?
                    QUIT_WATCH : QUIT_DIED;
        } else if( get_option<std::string>( "DEATHCAM" ) == "never" ) {
            uquit = QUIT_DIED;
        } else {
            // Something funky happened here, just die.
            dbg( DL::Error ) << "no deathcam option given to options, defaulting to QUIT_DIED";
            uquit = QUIT_DIED;
        }
        return is_game_over();
    }
    return false;
}

void game::death_screen()
{
    gamemode->game_over();
    Messages::display_messages();
    u.get_avatar_diary()->death_entry();
    show_scores_ui( *achievements_tracker_ptr, stats(), get_kill_tracker() );
    disp_NPC_epilogues();
    follower_ids.clear();
    display_faction_epilogues();
}

void game::win()
{
    win_screen();
    const time_duration game_duration = calendar::turn - calendar::start_of_game;
    memorial().add(
        pgettext( "memorial_male", "Closed the portal in %1$.1f days (%2$d seconds)." ),
        pgettext( "memorial_female", "Closed the portal in %1$.1f days (%2$d seconds)." ),
        to_days<float>( game_duration ), to_seconds<int>( game_duration ) );
    if( !u.is_dead_state() ) {
        Messages::display_messages();
        show_scores_ui( *achievements_tracker_ptr, stats(), get_kill_tracker() );
    }
}

void game::win_screen()
{
    // TODO: Move this wall somewhere
    const time_duration game_duration = calendar::turn - calendar::start_of_game;
    std::string msg = _( "You managed to close the portal and end the invasion!" );
    msg += '\n';
    if( u.is_dead_state() ) {
        translation t = translation::to_translation( "win_game",
                        "Unfortunately, you had to sacrifice your life to achieve this." );
        msg += colorize( t, c_red ) + '\n';
        memorial().add(
            pgettext( "memorial_male", "Sacrificed his life to close the portal." ),
            pgettext( "memorial_female", "Sacrificed her life to close the portal." ) );
    } else {
        translation t = translation::to_translation( "win_game", "You managed to survive the ordeal." );
        msg += colorize( t, c_green ) + '\n';
        memorial().add(
            pgettext( "memorial_male", "Safely closed the portal." ),
            pgettext( "memorial_female", "Safely closed the portal." ) );
    }
    msg += string_format( _( "It took you %1$.1f days (%2$d seconds)." ),
                          to_days<float>( game_duration ), to_seconds<int>( game_duration ) );
    // TODO: Print starting stats, traits, skills, all mods ever used, easiest of settings
    popup( msg );
}

//Saves all factions and missions and npcs.
//Saves per-dimension data like Weather and overmapbuffer state
event_bus &game::events()
{
    return *event_bus_ptr;
}

stats_tracker &game::stats()
{
    return *stats_tracker_ptr;
}

kill_tracker &game::get_kill_tracker()
{
    return *kill_tracker_ptr;
}

memorial_logger &game::memorial()
{
    return *memorial_logger_ptr;
}

spell_events &game::spell_events_subscriber()
{
    return *spell_events_ptr;
}

/**
 * Writes information about the character out to a text file timestamped with
 * the time of the file was made. This serves as a record of the character's
 * state at the time the memorial was made (usually upon death) and
 * accomplishments in a human-readable format.
 */
void game::disp_NPC_epilogues()
{
    // TODO: This search needs to be expanded to all NPCs
    for( const auto &elem : follower_ids ) {
        shared_ptr_fast<npc> guy = get_overmapbuffer( current_dimension_id_ ).find_npc( elem );
        if( !guy ) {
            continue;
        }
        const auto new_win = []() {
            return catacurses::newwin( FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH,
                                       point( std::max( 0, ( TERMX - FULL_SCREEN_WIDTH ) / 2 ),
                                              std::max( 0, ( TERMY - FULL_SCREEN_HEIGHT ) / 2 ) ) );
        };
        scrollable_text( new_win, guy->disp_name(), guy->get_epilogue() );
    }
}

void game::display_faction_epilogues()
{
    for( const auto &elem : faction_manager_ptr->all() ) {
        if( elem.second.known_by_u() ) {
            const std::vector<std::string> epilogue = elem.second.epilogue();
            if( !epilogue.empty() ) {
                const auto new_win = []() {
                    return catacurses::newwin( FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH,
                                               point( std::max( 0, ( TERMX - FULL_SCREEN_WIDTH ) / 2 ),
                                                      std::max( 0, ( TERMY - FULL_SCREEN_HEIGHT ) / 2 ) ) );
                };
                scrollable_text( new_win, elem.second.name(),
                                 std::accumulate( epilogue.begin() + 1, epilogue.end(), epilogue.front(),
                []( const std::string & lhs, const std::string & rhs ) -> std::string {
                    return lhs + "\n" + rhs;
                } ) );
            }
        }
    }
}

struct npc_dist_to_player {
    const tripoint_abs_omt ppos;
    npc_dist_to_player() : ppos( get_player_character().abs_omt_pos() ) { }
    // Operator overload required to leverage sort API.
    bool operator()( const shared_ptr_fast<npc> &a,
                     const shared_ptr_fast<npc> &b ) const {
        const tripoint_abs_omt apos = a->abs_omt_pos();
        const tripoint_abs_omt bpos = b->abs_omt_pos();
        return square_dist( ppos.xy(), apos.xy() ) <
               square_dist( ppos.xy(), bpos.xy() );
    }
};

void game::disp_NPCs()
{
    const tripoint_abs_omt ppos = u.abs_omt_pos();
    const auto &lpos = u.bub_pos();
    std::vector<shared_ptr_fast<npc>> npcs = get_overmapbuffer(
            current_dimension_id_ ).get_npcs_near_player( 100 );
    std::sort( npcs.begin(), npcs.end(), npc_dist_to_player() );

    // Display player position + nearby NPCs + monsters as a scrollable uilist.
    // (Replaced curses window with uilist, which is RmlUi-backed.)
    uilist menu;
    menu.allow_cancel = true;
    menu.title = string_format( _( "Pos: %s  Local: %s" ), ppos.to_string(), lpos.to_string() );
    for( size_t i = 0; i < npcs.size() && i < 100; i++ ) {
        const tripoint_abs_omt apos = npcs[i]->abs_omt_pos();
        menu.addentry( static_cast<int>( i ), false, MENU_AUTOASSIGN,
                       "%s: %s", npcs[i]->name, apos.to_string() );
    }
    for( const monster &m : all_monsters() ) {
        menu.addentry( -1, false, MENU_AUTOASSIGN,
                       "%s: %d, %d, %d", m.name(),
                       m.bub_pos().x(), m.bub_pos().y(), m.bub_pos().z() );
    }
    menu.query();
}

// A little helper to draw footstep glyphs.
game::draw_callback_t::draw_callback_t( const std::function<void()> &cb )
    : cb( cb )
{
}

game::draw_callback_t::~draw_callback_t()
{
    if( added ) {
        g->invalidate_main_ui_adaptor();
    }
}

void game::draw_callback_t::operator()()
{
    if( cb ) {
        cb();
    }
}

std::optional<tripoint_rel_ms> game::get_veh_dir_indicator_location( bool next ) const
{
    if( !get_option<bool>( "VEHICLE_DIR_INDICATOR" ) ) {
    return std::nullopt;
}
const optional_vpart_position vp = m.veh_at( u.bub_pos() );
if( !vp ) {
    return std::nullopt;
}
vehicle *const veh = &vp->vehicle();
rl_vec2d face = next ? veh->dir_vec() : veh->face_vec();
float r = 10.0;
return tripoint_rel_ms( static_cast<int>( r * face.x ), static_cast<int>( r * face.y ),
                        u.bub_pos().z() );
}

//Gets the next free ID, also used for player ID's.
character_id game::assign_npc_id()
{
    character_id ret = next_npc_id;
    ++next_npc_id;
    return ret;
}

Creature *game::is_hostile_nearby()
{
    auto distance = ( safe_mode_proximity <= 0 ) ? g_max_view_distance : safe_mode_proximity;
    return is_hostile_within( distance );
}

Creature *game::is_hostile_very_close()
{
    return is_hostile_within( DANGEROUS_PROXIMITY );
}

Creature *game::is_hostile_within( int distance )
{
    for( auto &critter : u.get_visible_creatures( distance ) ) {
        if( u.attitude_to( *critter ) == Attitude::A_HOSTILE ) {
            return critter;
        }
    }

    return nullptr;
}
//Gets Contiguious Fishable Terrain in radius starting from the tripoint
/* Knockback target at t by force number of tiles in direction from s to t
   stun > 0 indicates base stun duration, and causes impact stun; stun == -1 indicates only impact stun
   dam_mult multiplies impact damage, bash effect on impact, and sound level on impact */


template<typename T>
T *game::critter_at( const tripoint_bub_ms &p, bool allow_hallucination )
{
    using lookup_type = std::remove_cv_t<T>;
    constexpr auto wants_monster = std::is_base_of_v<lookup_type, monster>;
    constexpr auto wants_player = std::is_base_of_v<lookup_type, avatar>;
    constexpr auto wants_npc = std::is_base_of_v<lookup_type, npc>;
    constexpr auto return_ridden_monster = std::is_same_v<lookup_type, monster> ||
                                           std::is_same_v<lookup_type, Creature>;

    if( const shared_ptr_fast<monster> mon_ptr = critter_tracker->find( p ) ) {
        if( !allow_hallucination && mon_ptr->is_hallucination() ) {
            return nullptr;
        }
        // if we wanted to check for an NPC / player / avatar,
        // there is sometimes a monster AND an NPC/player there at the same time.
        // because the NPC/player etc may be riding that monster.
        // so only return the monster if we were actually looking for a monster.
        // otherwise, keep looking for the rider.
        // critter_at<creature> or critter_at() with no template will still default to returning monster first,
        // which is ok for the occasions where that happens.
        if( !mon_ptr->has_effect( effect_ridden ) || return_ridden_monster ) {
            if constexpr( wants_monster ) {
                return dynamic_cast<T *>( mon_ptr.get() );
            } else {
                return nullptr;
            }
        }
    }
    if constexpr( wants_player ) {
        if( p == u.bub_pos() ) {
            return dynamic_cast<T *>( &u );
        }
    }
    if constexpr( wants_npc ) {
        for( auto &cur_npc : active_npc ) {
            if( cur_npc->bub_pos() == p && !cur_npc->is_dead() ) {
                return dynamic_cast<T *>( cur_npc.get() );
            }
        }
    }
    return nullptr;
}

template<typename T>
const T *game::critter_at( const tripoint_bub_ms &p, bool allow_hallucination ) const
{
    return const_cast<game *>( this )->critter_at<T>( p, allow_hallucination );
}

template const monster *game::critter_at<monster>( const tripoint_bub_ms &, bool ) const;
template const npc *game::critter_at<npc>( const tripoint_bub_ms &, bool ) const;
template const player *game::critter_at<player>( const tripoint_bub_ms &, bool ) const;
template const avatar *game::critter_at<avatar>( const tripoint_bub_ms &, bool ) const;
template avatar *game::critter_at<avatar>( const tripoint_bub_ms &, bool );
template const Character *game::critter_at<Character>( const tripoint_bub_ms &, bool ) const;
template Character *game::critter_at<Character>( const tripoint_bub_ms &, bool );
template const Creature *game::critter_at<Creature>( const tripoint_bub_ms &, bool ) const;

template<typename T>
shared_ptr_fast<T> game::shared_from( const T &critter )
{
    if( static_cast<const Creature *>( &critter ) == static_cast<const Creature *>( &u ) ) {
        // u is not stored in a shared_ptr, but it won't go out of scope anyway
        return std::dynamic_pointer_cast<T>( u_shared_ptr );
    }
    if( critter.is_monster() ) {
        if( const shared_ptr_fast<monster> mon_ptr = critter_tracker->find( critter.bub_pos() ) ) {
            if( static_cast<const Creature *>( mon_ptr.get() ) == static_cast<const Creature *>( &critter ) ) {
                return std::dynamic_pointer_cast<T>( mon_ptr );
            }
        }
    }
    if( critter.is_npc() ) {
        for( auto &cur_npc : active_npc ) {
            if( static_cast<const Creature *>( cur_npc.get() ) == static_cast<const Creature *>( &critter ) ) {
                return std::dynamic_pointer_cast<T>( cur_npc );
            }
        }
    }
    return nullptr;
}

template shared_ptr_fast<Creature> game::shared_from<Creature>( const Creature & );
template shared_ptr_fast<Character> game::shared_from<Character>( const Character & );
template shared_ptr_fast<player> game::shared_from<player>( const player & );
template shared_ptr_fast<avatar> game::shared_from<avatar>( const avatar & );
template shared_ptr_fast<monster> game::shared_from<monster>( const monster & );
template shared_ptr_fast<npc> game::shared_from<npc>( const npc & );

template<typename T>
T *game::critter_by_id( const character_id &id )
{
    if( id == u.getID() ) {
        // player is always alive, therefore no is-dead check
        return dynamic_cast<T *>( &u );
    }
    return find_npc( id );
}

// monsters don't have ids
template Character *game::critter_by_id<Character>( const character_id & );
template player *game::critter_by_id<player>( const character_id & );
template npc *game::critter_by_id<npc>( const character_id & );
template Creature *game::critter_by_id<Creature>( const character_id & );

static bool can_place_monster( const monster &mon, const tripoint_bub_ms &p )
{
    if( const monster *const critter = g->critter_at<monster>( p ) ) {
        // Creature_tracker handles this. The hallucination monster will simply vanish
        if( !critter->is_hallucination() ) {
            return false;
        }
    }
    // Although monsters can sometimes exist on the same place as a Character (e.g. ridden horse),
    // it is usually wrong. So don't allow it.
    if( g->critter_at<Character>( p ) ) {
        return false;
    }
    return mon.will_move_to( p );
}

static std::optional<tripoint_bub_ms> choose_where_to_place_monster( const monster &mon,
        const tripoint_range<tripoint_bub_ms> &range )
{
    return random_point( range, [&]( const tripoint_bub_ms & p ) {
        return can_place_monster( mon, p );
    } );
}

monster *game::place_critter_at( const mtype_id &id, const tripoint_bub_ms &p )
{
    return place_critter_around( id, p, 0 );
}

monster *game::place_critter_at( const shared_ptr_fast<monster> &mon, const tripoint_bub_ms &p )
{
    return place_critter_around( mon, p, 0 );
}

monster *game::place_critter_around( const mtype_id &id, const tripoint_bub_ms &center,
                                     const int radius )
{
    // TODO: change this into an assert, it must never happen.
    if( id.is_null() ) {
        return nullptr;
    }
    const auto temp = make_shared_fast<monster>( id );
    if( !coop_session::get().is_client() ) {
        cata::run_hooks( "on_creature_spawn", [&]( sol::table & params ) {
            params["creature"] = temp.get();
        } );
        cata::run_hooks( "on_monster_spawn", [&]( sol::table & params ) {
            params["monster"] = temp.get();
        } );
    }
    return place_critter_around( temp, center, radius );
}

monster *game::place_critter_around( const shared_ptr_fast<monster> &mon,
                                     const tripoint_bub_ms &center,
                                     const int radius,
                                     bool forced )
{
    std::optional<tripoint_bub_ms> where;
    if( forced || can_place_monster( *mon, center ) ) {
        where = center;
    }

    // This loop ensures the monster is placed as close to the center as possible,
    // but all places that equally far from the center have the same probability.
    for( int r = 1; r <= radius && !where; ++r ) {
        where = choose_where_to_place_monster( *mon, m.points_in_radius( center, r ) );
    }

    if( !where ) {
        return nullptr;
    }
    mon->spawn( *where );
    return critter_tracker->add( mon ) ? mon.get() : nullptr;
}

monster *game::place_critter_within( const mtype_id &id,
                                     const tripoint_range<tripoint_bub_ms> &range )
{
    // TODO: change this into an assert, it must never happen.
    if( id.is_null() ) {
        return nullptr;
    }
    const auto temp = make_shared_fast<monster>( id );
    cata::run_hooks( "on_creature_spawn", [&]( sol::table & params ) {
        params["creature"] = temp.get();
    } );
    cata::run_hooks( "on_monster_spawn", [&]( sol::table & params ) {
        params["monster"] = temp.get();
    } );
    return place_critter_within( temp, range );
}

monster *game::place_critter_within( const shared_ptr_fast<monster> &mon,
                                     const tripoint_range<tripoint_bub_ms> &range )
{
    const std::optional<tripoint_bub_ms> where = choose_where_to_place_monster( *mon, range );
    if( !where ) {
        return nullptr;
    }
    mon->spawn( *where );
    return critter_tracker->add( mon ) ? mon.get() : nullptr;
}

size_t game::num_creatures() const
{
    return critter_tracker->size() + active_npc.size() + 1; // 1 == g->u
}

bool game::update_zombie_pos( const monster &critter, const tripoint_bub_ms &pos )
{
    return critter_tracker->update_pos( critter, pos );
}

void game::remove_zombie( const monster &critter )
{
    critter_tracker->remove( critter );
}

void game::erase_npc( character_id id )
{
    auto it = std::ranges::find_if( active_npc, [id]( const shared_ptr_fast<npc> &n ) {
        return n->getID() == id;
    } );
    if( it == active_npc.end() ) {
        debugmsg( "game::erase_npc: NPC (%d) not found in active_npc.", id.get_value() );
        return;
    }
    if( auto *pw = get_map().get_physics_world() ) {
        pw->on_creature_removed( it->get() );
    }
    active_npc.erase( it );
}

void game::clear_zombies()
{
    critter_tracker->clear();
}

/**
 * Attempts to spawn a hallucination at given location.
 * Returns false if the hallucination couldn't be spawned for whatever reason, such as
 * a monster already in the target square.
 * @return Whether or not a hallucination was successfully spawned.
 */
void static delete_cyborg_item( map &m, const tripoint_bub_ms &couch_pos, item *cyborg )
{
    // if this tile has an autodoc on a vehicle, delete the cyborg item from here
    if( const std::optional<vpart_reference> vp = get_map().veh_at( couch_pos ).part_with_feature(
            flag_AUTODOC_COUCH, false ) ) {
        auto dest_veh = &vp->vehicle();
        int dest_part = vp->part_index();

        for( item * const &it : dest_veh->get_items( dest_part ) ) {
            if( it == cyborg ) {
                dest_veh->remove_item( dest_part, it );
            }

        }

    }
    // otherwise delete it from the ground
    else {
        m.i_rem( couch_pos, cyborg );
    }
}

void game::save_cyborg( item *cyborg, const tripoint_bub_ms &couch_pos, Character &installer )
{
    int assist_bonus = installer.get_effect_int( effect_assisted );

    float adjusted_skill = installer.bionics_adjusted_skill( skill_firstaid,
                           skill_computer,
                           skill_electronics,
                           -1 );

    int damage = cyborg->damage();
    int dmg_lvl = cyborg->damage_level( 4 );
    int difficulty = 12;

    if( damage != 0 ) {

        popup( _( "WARNING: Patient's body is damaged.  Difficulty of the procedure is increased by %s." ),
               dmg_lvl );

        // Damage of the cyborg increases difficulty
        difficulty += dmg_lvl;
    }

    int chance_of_success = bionic_manip_cos( adjusted_skill + assist_bonus, difficulty );
    int success = chance_of_success - rng( 1, 100 );

    if( !g->u.query_yn(
            _( "WARNING: %i percent chance of SEVERE damage to all body parts!  Continue anyway?" ),
            100 - chance_of_success ) ) {
        return;
    }

    if( success > 0 ) {
        add_msg( m_good, _( "Successfully removed Personality override." ) );
        add_msg( m_bad, _( "Autodoc immediately destroys the CBM upon removal." ) );

        delete_cyborg_item( g->m, couch_pos, cyborg );

        const string_id<npc_template> npc_cyborg( "cyborg_rescued" );
        shared_ptr_fast<npc> tmp = make_shared_fast<npc>();
        tmp->load_npc_template( npc_cyborg );
        const auto proj = project_remain<coords::sm>( bub_to_abs( couch_pos ) );
        tmp->spawn_at_precise( proj.quotient, proj.remainder_tripoint );
        get_overmapbuffer( current_dimension_id_ ).insert_npc( tmp );
        tmp->hurtall( dmg_lvl * 10, nullptr );
        tmp->add_effect( effect_downed, rng( 1_turns, 4_turns ), bodypart_str_id::NULL_ID(), 0, true );
        cata::run_hooks( "on_creature_spawn", [&]( sol::table & params ) {
            params["creature"] = tmp.get();
        } );
        cata::run_hooks( "on_npc_spawn", [&]( sol::table & params ) {
            params["npc"] = tmp.get();
        } );
        load_npcs();

    } else {
        const int failure_level = static_cast<int>( std::sqrt( std::abs( success ) * 4.0 * difficulty /
                                  adjusted_skill ) );
        const int fail_type = std::min( 5, failure_level );
        switch( fail_type ) {
            case 1:
            case 2:
                add_msg( m_info, _( "The removal fails." ) );
                add_msg( m_bad, _( "The body is damaged." ) );
                cyborg->set_damage( damage + 1000 );
                break;
            case 3:
            case 4:
                add_msg( m_info, _( "The removal fails badly." ) );
                add_msg( m_bad, _( "The body is badly damaged!" ) );
                cyborg->set_damage( damage + 2000 );
                break;
            case 5:
                add_msg( m_info, _( "The removal is a catastrophe." ) );
                add_msg( m_bad, _( "The body is destroyed!" ) );
                delete_cyborg_item( g->m, couch_pos, cyborg );
                break;
            default:
                break;
        }

    }

}

void game::exam_vehicle( vehicle &veh, tripoint_mnt_veh c )
{
    if( veh.magic ) {
        add_msg( m_info, _( "This is your %s" ), veh.name );
        return;
    }
    std::unique_ptr<player_activity> act = veh_interact::run( veh, c );
    if( *act ) {
        u.moves = 0;
        u.assign_activity( std::move( act ) );
    }
}

void game::toggle_gate( const tripoint_bub_ms &p )
{
    gates::toggle_gate( p, u );
}

// Used to set up the first Hotkey in the display set

void game::place_player_overmap( const tripoint_abs_omt &om_dest )
{
    // if player is teleporting around, they don't bring their horse with them
    if( u.is_mounted() ) {
        u.remove_effect( effect_riding );
        u.mounted_creature->remove_effect( effect_ridden );
        u.mounted_creature = nullptr;
    }
    // offload the active npcs.
    unload_npcs();
    for( monster &critter : all_monsters() ) {
        despawn_monster( critter );
    }
    if( u.in_vehicle ) {
        m.unboard_vehicle( u.bub_pos() );
    }

    m.clear_vehicle_cache( );
    const int minz = m.has_zlevels() ? -OVERMAP_DEPTH : get_levz();
    const int maxz = m.has_zlevels() ? OVERMAP_HEIGHT : get_levz();
    for( int z = minz; z <= maxz; z++ ) {
        m.clear_vehicle_list( z );
    }
    m.access_cache( get_levz() ).map_memory_seen_cache.reset();
    // offset because load_map expects the coordinates of the top left corner, but the
    // player will be centered in the middle of the map.
    // TODO: fix point types
    const tripoint_abs_sm map_sm_pos(
        project_to<coords::sm>( om_dest ).raw() + point( -g_half_mapsize, -g_half_mapsize ) );
    const tripoint_bub_ms player_pos( u.bub_pos().xy(), map_sm_pos.z() );
    const int z_before = u.bub_pos().z();
    load_map( map_sm_pos );
    // update weather now as it could be different on the new location
    get_weather().nextweather = calendar::turn;
    place_player( player_pos );
    // Rebuild terrain colliders for the destination z-level.
    //
    // map::on_submap_loaded only notifies the physics world for submaps whose z
    // matches g->u.bub_pos().z() (src/map.cpp), and load_map() above ran BEFORE
    // place_player() had moved the player — so every notification during that load
    // was tested against the OLD z.  On a z-changing travel that means the
    // destination level received no colliders at all, and the only other rebuild
    // path (PhysicsWorld::on_zlevel_changed) is reached solely from
    // game::vertical_shift, i.e. stairs and ramps.  Without this the player could
    // drive through walls until they next used a staircase.
    if( auto *pw = m.get_physics_world(); pw != nullptr ) {
        const int z_after = u.bub_pos().z();
        if( z_before != z_after ) {
            pw->on_zlevel_changed( m, z_before, z_after );
        }
    }
    m.spawn_monsters( true ); // Static monsters
    update_overmap_seen();
    // load_npcs() scans around the player's absolute position, updated by place_player().
    load_npcs();
}

void game::resize_reality_bubble_to( int new_size )
{
    // Capture player's absolute submap position before any coordinate system changes.
    const auto player_abs_sm = project_to<coords::sm>( u.abs_pos() );

    // The grid origin shifts by (old_half - new_half) submaps when the bubble changes size.
    // Compute this before any globals change so we can use it for two purposes:
    //   1. Deciding which monsters are outside the new bubble (shrink-only despawn).
    //   2. Updating surviving monsters' local navigation state.
    const auto old_half = static_cast<int>( g_half_mapsize );
    const auto new_half = new_size + 1;
    // Positive when shrinking (old origin < new origin), negative when growing.
    // Each monster's local navigation state must be translated by this many submaps
    // in X and Y so it lines up with the new grid origin.
    const auto grid_origin_delta_in_sm = old_half - new_half;

    // When shrinking, despawn monsters that fall outside the new bubble radius.
    if( grid_origin_delta_in_sm > 0 ) {
        for( monster &critter : all_monsters() ) {
            const auto critter_sm = project_to<coords::sm>( critter.abs_pos() );
            const auto diff = critter_sm - player_abs_sm;
            if( std::abs( diff.x() ) > new_half || std::abs( diff.y() ) > new_half ) {
                despawn_monster( critter );
            }
        }
    }

    // Selectively unload NPCs that fall outside the new bubble.
    {
        auto out_of_range = std::ranges::stable_partition( active_npc,
        [&]( const shared_ptr_fast<npc> &n ) {
            const auto npc_sm = project_to<coords::sm>( n->abs_pos() );
            if( !m.has_zlevels() && npc_sm.z() != get_levz() ) {
                return false;  // wrong z-level — evict
            }
            const auto diff = npc_sm - player_abs_sm;
            return std::abs( diff.x() ) <= new_half && std::abs( diff.y() ) <= new_half;
        } );
        std::ranges::for_each( out_of_range, []( const auto & n ) { n->on_unload(); } );
        active_npc.erase( out_of_range.begin(), out_of_range.end() );
    }

    // Release submap loader handles so load_map() recreates them with the new radius.
    if( reality_bubble_handle_ != 0 ) {
        submap_loader.release_load( reality_bubble_handle_ );
        reality_bubble_handle_ = 0;
    }
    if( lazy_border_handle_ != 0 ) {
        submap_loader.release_load( lazy_border_handle_ );
        lazy_border_handle_ = 0;
    }

    // Update globals and rebuild the map grid.
    // grid[] is cleared by resize(); submaps stay resident in the mapbuffer
    // with their dirty flags intact and will be saved on normal eviction.
    init_bubble_config( new_size );
    m.resize( g_mapsize );
    reality_bubble_radius_ = g_half_mapsize;

    // Compute the new top-left abs_sub so load_map centers on the player.
    const auto new_abs_sub = tripoint_abs_sm(
                                 player_abs_sm.x() - g_half_mapsize,
                                 player_abs_sm.y() - g_half_mapsize,
                                 player_abs_sm.z() );

    // Reload the map around the player; this fills grid[], recreates load handles,
    // rebuilds distribution_grid_tracker and fluid_grid.
    load_map( new_abs_sub, /*pump_events=*/false );

    // Adjust surviving monsters' local navigation state to the new coordinate origin.
    // Monster positions are absolute; only cached bubble-coordinate goals and paths
    // need re-anchoring here. The tracker cache must be rebuilt after load_map()
    // changes abs_sub, because monster::bub_pos() is derived from absolute position.
    if( grid_origin_delta_in_sm != 0 ) {
        for( monster &critter : all_monsters() ) {
            critter.shift( { grid_origin_delta_in_sm, grid_origin_delta_in_sm } );
            critter.clear_path();
        }
        critter_tracker->rebuild_cache();
    }

    // NPC positions are stored as absolute coordinates and do not need re-anchoring
    // when the bubble shifts. Clear paths since local-coordinate routes are now stale.
    {
        std::ranges::for_each( active_npc, []( const auto & n ) {
            n->path.clear();
        } );
    }

    // Flush the load/eviction diff immediately so the first boundary crossing
    // after resize doesn't stall on a bulk eviction of the old bubble's submaps.
    // on_submap_unloaded is safe here: map::on_submap_unloaded guards grid[]
    // writes behind contains_abs_sm(), so old out-of-bubble positions are
    // skipped and only vehicle/active-item tracking is cleaned up.
    submap_loader.update_lazy_border_focus( current_dimension_id_, u.abs_pos() );
    submap_loader.update();

    // When the bubble grew, submaps outside the old (smaller) bubble just entered.
    // Their stored monsters are still in the overmap monster_map; spawn them now
    // so the expanded bubble isn't empty until the next boundary crossing.
    if( grid_origin_delta_in_sm < 0 ) {
        m.spawn_monsters( false );
    }

    load_npcs();

    u.recalc_sight_limits();
    m.invalidate_map_cache( get_levz() );
    m.build_map_cache( get_levz() );

    // Discard pathfinding objects sized for the old bubble.
    Pathfinding::clear_pool();

}

void game::resize_reality_bubble()
{
    // Called when the user explicitly changes REALITY_BUBBLE_SIZE in the options menu.
    // Clear all bubble state so the new normal size takes effect immediately;
    // the next do_turn() will re-evaluate and re-shrink as appropriate.
    in_activity_bubble_ = false;
    underground_bubble_turns_ = 0;
    vehicle_bubble_turns_ = 0;
    combat_bubble_turns_ = 0;
    u.get_mon_visible().combat_hostile_count = 0;
    resize_reality_bubble_to( get_option<int>( "REALITY_BUBBLE_SIZE" ) );
}

void game::update_performance_bubble()
{
    const int normal_size      = get_option<int>( "REALITY_BUBBLE_SIZE" );
    const int mobile_size      = get_option<int>( "ACTIVITY_MOBILE_BUBBLE_SIZE" );
    const int idle_size        = get_option<int>( "ACTIVITY_IDLE_BUBBLE_SIZE" );
    const int underground_size = get_option<int>( "UNDERGROUND_BUBBLE_SIZE" );
    const int vehicle_size     = get_option<int>( "VEHICLE_BUBBLE_SIZE" );
    const int combat_size      = get_option<int>( "COMBAT_BUBBLE_SIZE" );
    const int grace_minutes    = get_option<int>( "ACTIVITY_BUBBLE_GRACE" );
    const int dynamic_grace    = get_option<int>( "DYNAMIC_BUBBLE_GRACE" );

    // --- Activity-based bubble (minute-scale hysteresis) ---
    const bool has_activity = static_cast<bool>( u.activity );

    const activity_bubble_effect bubble_effect = has_activity
        ? u.activity.get()->id().obj().bubble_effect()
        : activity_bubble_effect::none;

    const auto activity_target_size = [&]() -> int {
        switch( bubble_effect )
    {
        case activity_bubble_effect::mobile:
            return mobile_size;
        case activity_bubble_effect::idle:
            return idle_size;
        default:
            return 0;
    }
}();

    // Once entered, we stay shrunk until the activity ends regardless of remaining time.
    if( in_activity_bubble_ ) {
        if( !has_activity || bubble_effect == activity_bubble_effect::none ) {
            in_activity_bubble_ = false;
        }
    } else if( has_activity && activity_target_size > 0 && activity_target_size < normal_size &&
               u.activity.get()->get_moves_left() >= to_moves<int>( time_duration::from_minutes(
                           grace_minutes ) ) ) {
        in_activity_bubble_ = true;
    }

    // --- Dynamic conditions (turn-scale hysteresis via per-condition counters) ---
    // Each counter increments while its condition holds; resets to 0 the moment it doesn't.
    // The bubble shrinks once the counter reaches dynamic_grace (no exit hysteresis).

    // Use has_floor on the tile above rather than is_outside: this is a quick single-tile check
    // for underground bubble sizing; is_outside uses the 3×3 overhang rule which is broader.
    const bool underground_cond = underground_size > 0 && underground_size < normal_size
                                  && u.bub_pos().z() < 0
                                  && m.has_floor( u.bub_pos() + tripoint_above );
    underground_bubble_turns_ = underground_cond ? underground_bubble_turns_ + 1 : 0;

    const bool vehicle_cond = vehicle_size > 0 && vehicle_size < normal_size
                              && ( ( u.in_vehicle && u.controlling_vehicle ) || u.is_mounted() );
    vehicle_bubble_turns_ = vehicle_cond ? vehicle_bubble_turns_ + 1 : 0;

    const bool combat_cond = combat_size > 0 && combat_size < normal_size
                             && u.get_mon_visible().combat_hostile_count >= ( combat_bubble_turns_ >= dynamic_grace ? 4 : 5 );
    combat_bubble_turns_ = combat_cond ? combat_bubble_turns_ + 1 : std::min( combat_bubble_turns_ - 1,
                           dynamic_grace );

    // Compute the desired bubble size as the minimum of all applicable shrinks.
    auto target = normal_size;
    if( in_activity_bubble_ ) {
        target = std::min( target, activity_target_size );
    }
    if( underground_bubble_turns_ >= dynamic_grace ) {
        target = std::min( target, underground_size );
    }
    if( vehicle_bubble_turns_ >= dynamic_grace ) {
        target = std::min( target, vehicle_size );
    } else if( combat_bubble_turns_ >=
               dynamic_grace ) { // If the vehicle bubble is active, the combat bubble is ignored
        target = std::min( target, combat_size );
    }

    if( g_reality_bubble_size != target ) {
        resize_reality_bubble_to( target );
    }
}

const dimension_info *game::get_current_dimension_info() const
{
    auto it = loaded_dimensions_.find( current_dimension_id_ );
    return it != loaded_dimensions_.end() ? &it->second : nullptr;
}

void game::debug_hour_timer::toggle()
{
    enabled = !enabled;
    start_time = std::nullopt;
    add_msg( string_format( "debug timer %s", enabled ? "enabled" : "disabled" ) );
}


void game::debug_hour_timer::print_time()
{
    if( enabled ) {
        if( action_time_scale::once_every_this_tick( time_duration::from_hours( 1 ) ) ) {
            const IRLTimeMs now = std::chrono::time_point_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now() );
            if( start_time ) {
                add_msg( "in-game hour took: %d ms", ( now - *start_time ).count() );
            } else {
                add_msg( "starting debug timer" );
            }
            start_time = now;
        }
    }
}

//Check if an artifact's extra charge requirements are currently met
bool check_art_charge_req( item &it )
{
    player &p = g->u;
    bool reqsmet = true;
    const bool worn = p.is_worn( it );
    const bool wielded = p.is_wielding( it );
    const bool heldweapon = ( wielded && !it.is_armor() ); //don't charge wielded clothes
    map &here = get_map();
    switch( it.type->artifact->charge_req ) {
        case( ACR_NULL ):
        case( NUM_ACRS ):
            break;
        case( ACR_EQUIP ):
            //Generated artifacts won't both be wearable and have charges, but nice for mods
            reqsmet = ( worn || heldweapon );
            break;
        case( ACR_SKIN ):
            //As ACR_EQUIP, but also requires nothing worn on bodypart wielding or wearing item
            if( !worn && !heldweapon ) {
                reqsmet = false;
                break;
            }
            for( const body_part bp : all_body_parts ) {
                if( it.covers( convert_bp( bp ) ) || ( heldweapon && ( bp == bp_hand_r || bp == bp_hand_l ) ) ) {
                    reqsmet = true;
                    for( auto &i : p.worn ) {
                        if( i->covers( convert_bp( bp ) ) && ( &it != i ) && i->get_coverage( convert_bp( bp ) ) > 50 ) {
                            reqsmet = false;
                            break; //This one's no good, check the next body part
                        }
                    }
                    if( reqsmet ) {
                        break;    //Only need skin contact on one bodypart
                    }
                }
            }
            break;
        case( ACR_SLEEP ):
            reqsmet = p.has_effect( effect_sleep );
            break;
        case( ACR_RAD ):
            reqsmet = ( ( here.get_radiation( p.bub_pos() ) > 0 ) || ( p.get_rad() > 0 ) );
            break;
        case( ACR_WET ):
            reqsmet = std::any_of( p.get_body().begin(), p.get_body().end(),
            []( const std::pair<const bodypart_str_id, bodypart> &elem ) {
                return elem.second.get_wetness() != 0;
            } );
            if( !reqsmet &&
                sum_conditions( calendar::turn - action_time_scale::calendar_duration_this_tick(),
                                calendar::turn, p.abs_pos() ).rain_amount > 0
                && !( p.in_vehicle && here.veh_at( p.bub_pos() )->is_inside() ) ) {
                reqsmet = true;
            }
            break;
        case( ACR_SKY ):
            reqsmet = ( p.bub_pos().z() > 0 );
            break;
    }
    return reqsmet;
}

int game::get_levx() const
{
    return m.get_abs_sub().x();
}

int game::get_levy() const
{
    return m.get_abs_sub().y();
}

int game::get_levz() const
{
    return m.get_abs_sub().z();
}

overmap &game::get_cur_om() const
{
    // The player is located in the middle submap of the map.
    const tripoint_abs_sm sm = m.get_abs_sub() + tripoint_rel_sm( g_half_mapsize, g_half_mapsize, 0 );
    const tripoint_abs_om pos_om = project_to<coords::om>( sm );
    // TODO: fix point types
    return get_overmapbuffer( current_dimension_id_ ).get( pos_om.xy() );
}

std::vector<npc *> game::allies()
{
    return get_npcs_if( [&]( const npc & guy ) {
        if( !guy.is_hallucination() ) {
            return guy.is_ally( g->u );
        } else {
            return false;
        }
    } );
}

std::vector<Creature *> game::get_creatures_if( const std::function<bool( const Creature & )>
        &pred )
{
    std::vector<Creature *> result;
    for( Creature &critter : all_creatures() ) {
        if( pred( critter ) ) {
            result.push_back( &critter );
        }
    }
    return result;
}

std::vector<npc *> game::get_npcs_if( const std::function<bool( const npc & )> &pred )
{
    std::vector<npc *> result;
    for( npc &guy : all_npcs() ) {
        if( pred( guy ) ) {
            result.push_back( &guy );
        }
    }
    return result;
}

template<>
bool game::non_dead_range<monster>::iterator::valid()
{
    current = iter->lock();
    return current && !current->is_dead();
}

template<>
bool game::non_dead_range<npc>::iterator::valid()
{
    current = iter->lock();
    return current && !current->is_dead();
}

template<>
bool game::non_dead_range<Creature>::iterator::valid()
{
    current = iter->lock();
    // There is no Creature::is_dead function, so we can't write
    // return current && !current->is_dead();
    if( !current ) {
        return false;
    }
    const Creature *const critter = current.get();
    if( critter->is_monster() ) {
        return !static_cast<const monster *>( critter )->is_dead();
    }
    if( critter->is_npc() ) {
        return !static_cast<const npc *>( critter )->is_dead();
    }
    return true; // must be g->u
}

game::monster_range::monster_range( game &game_ref )
{
    const auto &monsters = game_ref.critter_tracker->get_monsters_list();
    items->insert( items->end(), monsters.begin(), monsters.end() );
}

game::Creature_range::Creature_range( game &game_ref ) : u( &game_ref.u, []( Character * ) { } )
{
    const auto &monsters = game_ref.critter_tracker->get_monsters_list();
    items->insert( items->end(), monsters.begin(), monsters.end() );
    items->insert( items->end(), game_ref.active_npc.begin(), game_ref.active_npc.end() );
    items->emplace_back( u );
}

game::npc_range::npc_range( game &game_ref )
{
    items->insert( items->end(), game_ref.active_npc.begin(), game_ref.active_npc.end() );
}

game::Creature_range game::all_creatures()
{
    return Creature_range( *this );
}

game::monster_range game::all_monsters()
{
    return monster_range( *this );
}

game::npc_range game::all_npcs()
{
    return npc_range( *this );
}

Creature *game::get_creature_if( const std::function<bool( const Creature & )> &pred )
{
    for( Creature &critter : all_creatures() ) {
        if( pred( critter ) ) {
            return &critter;
        }
    }
    return nullptr;
}

world *game::get_active_world() const
{
    return world_generator->active_world.get();
}

void game::shift_destination_preview( const point_rel_ms &delta )
{
    for( tripoint_bub_ms &p : destination_preview ) {
        p = p + delta;
    }
}

item *game::add_fake_item( detached_ptr<item> &&it )
{
    it->set_flag( flag_TEMPORARY_ITEM );
    fake_items.push_back( std::move( it ) );
    return fake_items.back();
}

namespace cata_event_dispatch
{
void avatar_moves( const avatar &u, const map &m, const tripoint_abs_ms &p )
{
    mtype_id mount_type;
    if( u.is_mounted() ) {
        mount_type = u.mounted_creature->type->id;
    }
    g->events().send<event_type::avatar_moves>( mount_type, m.ter( abs_to_bub( p ) ).id(),
            u.get_movement_mode(), u.is_underwater(), p.z() );
}
} // namespace cata_event_dispatch

event_bus &get_event_bus()
{
    return g->events();
}

distribution_grid_tracker &get_distribution_grid_tracker()
{
    // If a dimension tracker exists and the player is in that dimension,
    // prefer it; otherwise fall back to "".
    const std::string &dim = g->m.get_bound_dimension();
    auto it = g->grid_trackers_.find( dim );
    if( it != g->grid_trackers_.end() && it->second ) {
        return *it->second;
    }
    return *g->grid_trackers_.at( "" );
}

distribution_grid_tracker *get_distribution_grid_tracker_for( const std::string &dim_id )
{
    if( !g ) {
        return nullptr;
    }
    const auto it = g->grid_trackers_.find( dim_id );
    if( it != g->grid_trackers_.end() && it->second ) {
        return it->second.get();
    }
    return nullptr;
}

distribution_grid_tracker &ensure_distribution_grid_tracker_for( const std::string &dim_id )
{
    auto it = g->grid_trackers_.find( dim_id );
    if( it != g->grid_trackers_.end() && it->second ) {
        return *it->second;
    }
    g->grid_trackers_[dim_id] = std::make_unique<distribution_grid_tracker>(
                                    MAPBUFFER_REGISTRY.get( dim_id ), dim_id );
    auto &tracker = *g->grid_trackers_[dim_id];
    submap_loader.add_listener( &tracker );
    // Replay on_submap_loaded for all currently-resident submaps of this
    // dimension so the new tracker picks up existing grid_link_tile nodes.
    for( auto &[raw_pos, sm_ptr] : MAPBUFFER_REGISTRY.get( dim_id ) ) {
        if( sm_ptr ) {
            tracker.on_submap_loaded( tripoint_abs_sm( raw_pos ), dim_id );
        }
    }
    return tracker;
}

void cleanup_arenas()
{
    ZoneScoped;
    bool cont = true;
    while( cont ) {
        cont = false;
        cont |= cata_arena<item>::cleanup();
    }
}

const scenario *get_scenario()
{
    return g->scen;
}
void set_scenario( const scenario *new_scenario )
{
    g->scen = new_scenario;
}

auto game::poll_event() -> input_event
{
    const auto old_delay = inp_mngr.get_timeout();
    inp_mngr.set_timeout( 0 );               // non-blocking
    const auto evt = inp_mngr.get_input_event();
    inp_mngr.set_timeout( old_delay );        // restore
    return evt;
}



auto game::coop_game_tick() -> void
{
    if( coop_server_ ) {
    // Host: server drives the world sim + sync
    coop_server_->coop_world_tick();
    } else if( coop_client_ ) {
    // Client thin path: send queued actions + apply incoming SYNC.
    // World state (tiles, monsters) is host-authoritative — no local sim.
    // process_turn() is called inside apply_sync() once per turn advanced;
    // that fires it correctly during both normal play and fast-forward bursts.
    coop_client_->coop_world_tick();
    } else {
        // Single-player: direct world sim
        post_action_world_step();
    }
}
