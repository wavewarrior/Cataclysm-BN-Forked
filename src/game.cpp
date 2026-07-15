#include "game.h"
#ifdef COOP_ENABLED
#include "coop_server.h"
#include "coop_client.h"
#include "coop_session.h"
#endif

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
#include "lightmap.h"
#include "line.h"
#include "live_view.h"
#include "loading_ui.h"
#include "locations.h"
#include "npc.h"
#include "magic.h"
#include "map.h"
#ifdef BOX2D_ENABLED
#include "physics/physics_world.h"
#endif
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
#include "sdl_render_frame.h"
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
void game::load_static_data()
{
    // UI stuff, not mod-specific per definition
    inp_mngr.init();            // Load input config JSON
    // Init mappings for loading the json stuff
    DynamicDataLoader::get_instance();
#if defined( _WIN32 )
    // Performance: each JSON file open on Windows triggers a real-time AV scan.
    // Adding the data directory to Windows Defender exclusions eliminates this cost.
    DebugLog( DL::Info, DC::Main )
            << "Performance tip: add '" << PATH_INFO::datadir()
            << "' to Windows Defender exclusions to reduce load time "
       "(Settings \u2192 Windows Security \u2192 Virus & threat protection \u2192 Exclusions).";
#endif
    fullscreen = false;
    was_fullscreen = false;
    show_panel_adm = false;
    panel_manager::get_manager().init();

    // These functions do not load stuff from json.
    // The content they load/initialize is hardcoded into the program.
    // Therefore they can be loaded here.
    // If this changes (if they load data from json), they have to
    // be moved to game::load_mod

    get_auto_pickup().load_global();
    get_safemode().load_global();
    get_distraction_manager().load();
}

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

void game::toggle_fullscreen()
{
    toggle_fullscreen_window();
}

void game::toggle_pixel_minimap()
{
    if( pixel_minimap_option ) {
        clear_window_area( w_pixel_minimap );
    }
    pixel_minimap_option = !pixel_minimap_option;
    mark_main_ui_adaptor_resize();
}

void game::reload_tileset( [[maybe_unused]] const std::function<void( std::string )> &out )
{
    // Disable UIs below to avoid accessing the tile context during loading.
    ui_adaptor ui( ui_adaptor::disable_uis_below {} );
    const auto tilesName = get_option<std::string>( "TILES" );
    const auto omTilesName = get_option<std::string>( "OVERMAP_TILES" );
    const auto saved_zoom = g->get_zoom();
    try {
        tilecontext->reinit();
        std::vector<mod_id> dummy;
        tilecontext->load_tileset(
            tilesName,
            world_generator->active_world ? world_generator->active_world->info->active_mod_order : dummy,
            /*precheck=*/false,
            /*force=*/true,
            /*pump_events=*/true
        );
        tilecontext->do_tile_loading_report( out );
    } catch( const std::exception &err ) {
        popup( _( "Loading the tileset failed: %s" ), err.what() );
    }
    if( tilesName == omTilesName ) {
        overmap_tilecontext = tilecontext;
    } else {
        try {
            repoint_overmap_tilecontext();
            std::vector<mod_id> dummy;
            overmap_tilecontext->load_tileset(
                omTilesName,
                world_generator->active_world ? world_generator->active_world->info->active_mod_order : dummy,
                /*precheck=*/false,
                /*force=*/true,
                /*pump_events=*/true
            );
            overmap_tilecontext->do_tile_loading_report( out );
        } catch( const std::exception &err ) {
            popup( _( "Loading the overmap tileset failed: %s" ), err.what() );
        }
    }
    // Reload resets the tile context scale to its default; reapply the previous zoom explicitly
    // even when the numeric zoom value did not change.
    tileset_zoom = saved_zoom;
    rescale_tileset( tileset_zoom );
    g->mark_main_ui_adaptor_resize();
}

// temporarily switch out of fullscreen for functions that rely
// on displaying some part of the sidebar
void game::temp_exit_fullscreen()
{
    if( fullscreen ) {
        was_fullscreen = true;
        toggle_fullscreen();
    } else {
        was_fullscreen = false;
    }
}

void game::reenter_fullscreen()
{
    if( was_fullscreen ) {
        if( !fullscreen ) {
            toggle_fullscreen();
        }
    }
}

/*
 * Initialize more stuff after mapbuffer is loaded.
 */
void game::setup( bool load_world_modfiles )
{
    loading_ui ui( true );

    // Clear all dimension overmapbuffers before reloading JSON data.
    // Each overmapbuffer holds raw `settings` pointers into region_settings_map,
    // which is wiped by load_world_modfiles → unload_data below.
    // Leaving stale overmaps in the registry after that wipe causes dangling-pointer
    // crashes (settings->id) in save_all_overmapbuffers() on the next session.
    for_each_overmapbuffer( []( const std::string &, overmapbuffer & buf ) {
        buf.clear();
    } );

    if( load_world_modfiles ) {
        init::load_world_modfiles( ui, get_active_world(), SAVE_ARTIFACTS );
        // Widget JSON (data/json/ui/*) is now loaded; build the data-driven
        // sidebar layouts so the "custom" layout becomes selectable.
        panel_manager::get_manager().reload_widget_layouts();
    }

    // Drop sidebar animation state from any prior session: the registry is a
    // process-lifetime singleton keyed by widget id, so without this the first
    // sidebar draw of a freshly loaded game would see this world's values differ
    // from the last and pop every icon (the "flash on load" we want to avoid).
    sidebar_anim::get().clear();
    sidebar_anim::get().load_specs(); // (re)bind icon animation specs from icons.json

    init_bubble_config();
    m.resize( g_mapsize );

    next_npc_id = character_id( 1 );
    next_mission_id = 1;
    new_game = true;
    next_activity_fixed_window_check_ = calendar::turn_zero;
    activity_fixed_window_force_normal_turn_ = false;
    saving_blocked_by_failed_load = false;
    uquit = QUIT_NO;   // We haven't quit the game
    bVMonsterLookFire = true;

    // invalidate calendar caches in case we were previously playing
    // a different world
    calendar::set_eternal_season( ::get_option<bool>( "ETERNAL_SEASON" ) );
    calendar::set_season_length( ::get_option<int>( "SEASON_LENGTH" ) );

    get_weather().weather_id = weather_type_id::NULL_ID();
    get_weather().nextweather = calendar::before_time_starts;

    turnssincelastmon = 0; //Auto safe mode init

    sounds::reset_sounds();
    clear_zombies();
    coming_to_stairs.clear();
    active_npc.clear();
    faction_manager_ptr->clear();
    mission::clear_all();
    Messages::clear_messages();
    timed_events = timed_event_manager();
    explosion_handler::get_explosion_queue().clear();

    SCT.vSCT.clear(); //Delete pending messages

    stats().clear();
    // reset kill counts
    get_kill_tracker().clear();
    achievements_tracker_ptr->clear();
    // reset follower list
    follower_ids.clear();
    scent.reset();

    remoteveh_cache_time = calendar::before_time_starts;
    remoteveh_cache = nullptr;

    token_provider_ptr->clear();
    // back to menu for save loading, new game etc
}

bool game::has_gametype() const
{
    return gamemode && gamemode->id() != special_game_type::NONE;
}

special_game_type game::gametype() const
{
    return gamemode ? gamemode->id() : special_game_type::NONE;
}

void game::load_map( const tripoint_abs_sm &pos_sm, const bool pump_events )
{
    // Bind the map to the target dimension BEFORE m.load() so loadn() uses the
    // correct MAPBUFFER_REGISTRY slot for submap lookups and generation.
    const std::string new_dim_id = get_dimension_prefix();
    const std::string old_dim_id = m.get_bound_dimension();

    // If the dimension has changed, release the old reality-bubble request and
    // flush prev_desired_ so update() does not evict freshly-generated submaps
    // for the new dimension (use-after-free via stale m.grid pointers).
    if( reality_bubble_handle_ != 0 && new_dim_id != old_dim_id ) {
        // Drain any in-flight submap load-manager tasks for the old dimension before
        // releasing handles and switching — workers must not race with the
        // new dimension's mapbuffer setup.
        submap_loader.drain_lazy_loads();
        submap_loader.release_load( reality_bubble_handle_ );
        reality_bubble_handle_ = 0;
        if( lazy_border_handle_ != 0 ) {
            submap_loader.release_load( lazy_border_handle_ );
            lazy_border_handle_ = 0;
        }
        fire_loader.clear( submap_loader );
        submap_loader.flush_prev_desired();
    }

    // Bind the map to the new dimension so loadn() stores generated submaps in
    // the correct MAPBUFFER_REGISTRY slot and on_submap_loaded() can find them.
    m.bind_dimension( new_dim_id );
    // Set the fluid grid's active overmapbuffer before m.load() so that any
    // mapgen triggered during loading (e.g. seed_liquid_charges_for_mapgen)
    // can access overmap data.  fluid_grid::load() sets it again after m.load()
    // and also rebuilds the tracker bounds — both calls are required.
    fluid_grid::bind_dimension( new_dim_id );

    m.load( pos_sm, true, pump_events );

    // Repopulate the distribution-grid tracker for the current dimension.
    // With dimension-aware generation, each dimension's submaps live in their own
    // registry slot, so we iterate over the bound dimension's buffer.
    {
        // Ensure a tracker exists for this dimension.
        if( grid_trackers_.find( new_dim_id ) == grid_trackers_.end() ) {
            grid_trackers_[new_dim_id] = std::make_unique<distribution_grid_tracker>(
                                             MAPBUFFER_REGISTRY.get( new_dim_id ), new_dim_id );
            submap_loader.add_listener( grid_trackers_[new_dim_id].get() );
        }
        auto &tracker = *grid_trackers_[new_dim_id];
        tracker.clear();
        for( auto &[raw_pos, sm_ptr] : MAPBUFFER_REGISTRY.get( new_dim_id ) ) {
            if( sm_ptr ) {
                tracker.on_submap_loaded( tripoint_abs_sm( raw_pos ), new_dim_id );
            }
        }
    }

    fluid_grid::load( m );

    // Register game and map as listeners (add_listener is idempotent).
    // game must be added before map so that on_submap_unloaded deactivates
    // entities before the map's listener clears grid[] pointers.
    submap_loader.add_listener( this );
    submap_loader.add_listener( &m );

    // The load-manager center is the middle of the loaded region, not the
    // top-left corner.  pos_sm is the top-left corner (abs_sub), so offset
    // by reality_bubble_radius_ in each horizontal direction.
    const tripoint_abs_sm bubble_center = pos_sm + point_rel_sm( reality_bubble_radius_,
                                          reality_bubble_radius_ );

    // Create or update the reality bubble request.
    if( reality_bubble_handle_ == 0 ) {
        reality_bubble_handle_ = submap_loader.request_load(
                                     load_request_source::reality_bubble,
                                     new_dim_id, bubble_center, reality_bubble_radius_ );
    } else {
        submap_loader.update_request( reality_bubble_handle_, bubble_center );
    }

    // Lazy border: one OMT-space layer kept in memory but not simulated.
    if( lazy_border_enabled ) {
        if( lazy_border_handle_ == 0 ) {
            lazy_border_handle_ = submap_loader.request_load(
                                      load_request_source::lazy_border,
                                      new_dim_id, bubble_center,
                                      reality_bubble_radius_ );
        } else {
            submap_loader.update_request( lazy_border_handle_, bubble_center );
        }
    } else if( lazy_border_handle_ != 0 ) {
        submap_loader.release_load( lazy_border_handle_ );
        lazy_border_handle_ = 0;
    }
    // map::loadn() now uses MAPBUFFER_REGISTRY.get(bound_dimension_), so
    // the load manager can safely fire on_submap_loaded/unloaded events.
    // Ensure a distribution_grid_tracker exists for every active dimension before
    // update() fires on_submap_loaded events.  ensure_distribution_grid_tracker_for
    // replays on_submap_loaded for already-resident submaps so that export nodes
    // (and their reverse nodes) are properly registered.
    for( const auto &dim_id : submap_loader.active_dimensions() ) {
        ensure_distribution_grid_tracker_for( dim_id );
    }
    submap_loader.update_lazy_border_focus( new_dim_id, u.abs_pos() );
    submap_loader.update();
    // Destroy trackers for non-primary dimensions that have no remaining tracked submaps.
    for( auto it = grid_trackers_.begin(); it != grid_trackers_.end(); ) {
        if( !it->first.empty() && !it->second->has_tracked_submaps() ) {
            submap_loader.remove_listener( it->second.get() );
            it = grid_trackers_.erase( it );
        } else {
            ++it;
        }
    }
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
bool game::start_game()
{
    if( !gamemode ) {
        gamemode = std::make_unique<special_game>();
    }

    seed = rng_bits();
    new_game = true;
    saving_blocked_by_failed_load = false;
    next_activity_fixed_window_check_ = calendar::turn_zero;
    activity_fixed_window_force_normal_turn_ = false;
    start_calendar();
    get_weather().nextweather = calendar::turn;
    safe_mode = ( get_option<bool>( "SAFEMODE" ) ? SAFE_MODE_ON : SAFE_MODE_OFF );
    mostseen = 0; // ...and mostseen is 0, we haven't seen any monsters yet.
    get_safemode().load_global();
    get_distraction_manager().load();

    init_autosave();

    static_popup popup;
    popup.message( "%s", _( "Please wait as we build your world" ) );
    ui_manager::redraw();
    refresh_display();

    load_master();

    // Populate the overworld dimension_info so get_current_dimension_info() is valid
    // from the very start of a new game.  Use the "default" world_type from JSON so
    // mods can override the name and region settings without touching this code.
    {
        const auto default_wt = world_types::get_default();
        const struct world_type *wt_ptr = default_wt.is_valid() ? &default_wt.obj() : nullptr;
        loaded_dimensions_[""] = dimension_info{
            .dimension_id = "",
            .world_type   = default_wt,
            .display_name = wt_ptr ? wt_ptr->name.translated() : std::string{},
            .pocket_info = std::nullopt
        };
        get_overmapbuffer( current_dimension_id_ ).current_region_type = wt_ptr ?
            wt_ptr->region_settings_id : "default";
        calendar::set_active_world_type( default_wt.str() );
    }

    u.setID( assign_npc_id() ); // should be as soon as possible, but *after* load_master

    const start_location &start_loc = u.random_start_location ? scen->random_start_location().obj() :
                                      u.start_location.obj();
    tripoint_abs_omt omtstart = overmap::invalid_tripoint;

    constexpr auto query_gen_failed = []() {
        return query_yn(
                   _( "Try again?\n\nIt may require several attempts until the game finds a valid starting location." ) );
    };

    do {
        omtstart = start_loc.find_player_initial_location();
        if( omtstart == overmap::invalid_tripoint ) {
            if( query_gen_failed() ) {
                // New-game generation only: the player is always in the overworld at
                // this point; no dimension travel has occurred, so primary == active.
                MAPBUFFER.clear();
                get_overmapbuffer( current_dimension_id_ ).clear();
            } else {
                return false;
            }
        }

        start_loc.prepare_map( omtstart );

        // Place vehicles spawned by scenario or profession, has to be placed very early to avoid bugs.
        if( u.starting_vehicle ) {
            auto veh = place_vehicle_nearby( u.starting_vehicle, omtstart.xy(), 0, 30, std::vector<std::string> {},
                                             u.prof->has_flag( "VEH_GROUNDED" ) );
            if( veh ) {
                veh->set_owner( u );
            } else if( query_gen_failed() ) {
                // Same new-game context — primary is always the active dimension here.
                MAPBUFFER.clear();
                get_overmapbuffer( current_dimension_id_ ).clear();
                omtstart = overmap::invalid_tripoint;
            } else {
                return false;
            }
        }
    } while( omtstart == overmap::invalid_tripoint );

    if( scen->has_map_extra() ) {
        // Map extras can add monster spawn points and similar and should be done before the main
        // map is loaded.
        start_loc.add_map_extra( omtstart, scen->get_map_extra() );
    }

    init_bubble_config();
    // Resize the map grid to match the (possibly changed) bubble-size option.
    // The grid may hold stale pointers from a previous session; resize() clears
    // them before reallocating to the new my_MAPSIZE.
    m.resize( g_mapsize );
    reality_bubble_radius_ = g_half_mapsize;

    auto lev = project_to<coords::sm>( omtstart );
    // The player is centered in the map, but lev[xyz] refers to the top left point of the map
    lev.x() -= g_half_mapsize;
    lev.y() -= g_half_mapsize;
    load_map( lev, /*pump_events=*/true );

    m.invalidate_map_cache( get_levz() );
    m.build_map_cache( get_levz() );
    // Do this after the map cache has been built!
    start_loc.place_player( u );
    // ...but then rebuild it, because we want visibility cache to avoid spawning monsters in sight
    m.invalidate_map_cache( get_levz() );
    m.build_map_cache( get_levz() );
    // Start the overmap with out immediate neighborhood visible, this needs to be after place_player
    get_overmapbuffer( current_dimension_id_ ).reveal( u.abs_omt_pos().xy(),
            get_option<int>( "DISTANCE_INITIAL_VISIBILITY" ), 0 );

    u.moves = 0;
    if( u.has_trait( trait_PROF_FERAL ) ) {
        u.add_effect( effect_feral_killed_recently, 3_days );
    }
    u.process_turn(); // process_turn adds the initial move points
    u.set_stamina( u.get_stamina_max() );
    get_weather().update_weather();
    u.next_climate_control_check = calendar::before_time_starts; // Force recheck at startup
    u.last_climate_control_ret = false;

    //Reset character safe mode/pickup rules
    get_auto_pickup().clear_character_rules();
    get_safemode().clear_character_rules();
    get_auto_notes_settings().clear();
    get_auto_notes_settings().default_initialize();

    //Put some NPCs in there!
    if( get_option<std::string>( "STARTING_NPC" ) == "always" ||
        ( get_option<std::string>( "STARTING_NPC" ) == "scenario" &&
          !g->scen->has_flag( "LONE_START" ) ) ) {
        create_starting_npcs();
    }
    if( !!u.prof ) {
        for( npc_class_id npcid : u.prof->npcs() ) {
            shared_ptr_fast<npc> tmp = make_shared_fast<npc>();
            tmp->randomize( npcid );
            auto point = random_point( m.points_in_radius( u.bub_pos(), 10 ), [&]( const tripoint_bub_ms & p ) {
                return m.has_floor( p ) && !is_dangerous_tile( p ) && m.passable( p );
            } );
            if( !point ) {
                break;
            }
            auto proj = project_remain<coords::sm>( bub_to_abs( *point ) );
            tmp->spawn_at_precise( proj.quotient, proj.remainder_tripoint );
            get_overmapbuffer( current_dimension_id_ ).insert_npc( tmp );
            tmp->set_fac( faction_id( "your_followers" ) );
            tmp->mission = NPC_MISSION_NULL;
            tmp->set_attitude( NPCATT_FOLLOW );
            add_npc_follower( tmp->getID() );
            cata::run_hooks( "on_creature_spawn", [&]( sol::table & params ) {
                params["creature"] = tmp.get();
            } );
            cata::run_hooks( "on_npc_spawn", [&]( sol::table & params ) {
                params["npc"] = tmp.get();
            } );
        }
    }
    //Load NPCs. Set nearby npcs to active.
    load_npcs();

    // Spawn the monsters for `Surrounded` starting scenarios
    std::vector<std::pair<mongroup_id, float>> surround_groups = get_scenario()->surround_groups();
    const bool surrounded_start_scenario = !surround_groups.empty();
    const bool surrounded_start_options = get_option<bool>( "BLACK_ROAD" );
    if( surrounded_start_options && !surrounded_start_scenario ) {
        surround_groups.emplace_back( mongroup_id( "GROUP_BLACK_ROAD" ), 70.0f );
    }
    const bool spawn_near = surrounded_start_options || surrounded_start_scenario;
    if( spawn_near ) {
        for( const std::pair<mongroup_id, float> &sg : surround_groups ) {
            start_loc.surround_with_monsters( omtstart, sg.first, sg.second );
        }
    }

    m.spawn_monsters( !spawn_near ); // Static monsters

    // Make sure that no monsters are near the player
    // This can happen in lab starts
    if( !spawn_near ) {
        for( monster &critter : all_monsters() ) {
            if( rl_dist( critter.bub_pos(), u.bub_pos() ) <= 5 ||
                m.clear_path( critter.bub_pos(), u.bub_pos(), 40, 1, 100 ) ) {
                remove_zombie( critter );
            }
        }
    }

    //Create mutation_category_level
    u.set_highest_cat_level();
    //Calculate mutation drench protection stats
    u.drench_mut_calc();
    u.add_effect( effect_accumulated_mutagen, 27_days, bodypart_str_id::NULL_ID() );
    if( scen->has_flag( "FIRE_START" ) ) {
        start_loc.burn( omtstart, 3, 3 );
    }
    if( scen->has_flag( "INFECTED" ) ) {
        u.add_effect( effect_infected, 1_turns, random_body_part() );
    }
    if( scen->has_flag( "BAD_DAY" ) ) {
        u.add_effect( effect_flu, 1000_minutes );
        u.add_effect( effect_drunk, 270_minutes );
        u.add_morale( MORALE_FEELING_BAD, -100, -100, 50_minutes, 50_minutes );
    }
    if( scen->has_flag( "HELI_CRASH" ) ) {
        start_loc.handle_heli_crash( u );
        bool success = false;
        for( auto v : m.get_vehicles() ) {
            std::string name = v.v->type.str();
            std::string search = std::string( "helicopter" );
            if( name.find( search ) != std::string::npos ) {
                for( const vpart_reference &vp : v.v->get_any_parts( VPFLAG_CONTROLS ) ) {
                    const tripoint_bub_ms pos = vp.pos();
                    u.setpos( pos );

                    // Delete the items that would have spawned here from a "corpse"
                    for( auto sp : v.v->parts_at_relative( vp.mount(), true ) ) {
                        vehicle_stack here = v.v->get_items( sp );

                        for( auto iter = here.begin(); iter != here.end(); ) {
                            iter = here.erase( iter );
                        }
                    }

                    auto mons = critter_tracker->find( pos );
                    if( mons != nullptr ) {
                        critter_tracker->remove( *mons );
                    }

                    success = true;
                    break;
                }
                if( success ) {
                    v.v->name = "Bird Wreckage";
                    break;
                }
            }
        }
    }
    if( scen->has_flag( "BORDERED" ) ) {
        overmap &starting_om = get_cur_om();
        for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; z++ ) {
            starting_om.place_special_forced( overmap_special_id( "world" ), { 0, 0, z },
                                              om_direction::type::north );
        }

    }
    for( auto &e : u.inv_dump() ) {
        e->set_owner( g->u );
    }
    // Now that we're done handling coordinates, ensure the player's submap is in the center of the map
    update_map( u );
    // Profession pets
    for( const mtype_id &elem : u.starting_pets ) {
        if( monster *const mon = place_critter_around( elem, u.bub_pos(), g_max_view_distance ) ) {
            mon->friendly = -1;
            mon->add_effect( effect_pet, 1_turns );
        } else {
            add_msg( m_debug, "cannot place starting pet, no space!" );
        }
    }
    // Assign all of this scenario's missions to the player.
    for( const mission_type_id &m : scen->missions() ) {
        const auto mission = mission::reserve_new( m, character_id() );
        mission->assign( u );
    }

    // Same for profession missions
    if( !!u.prof ) {
        for( const mission_type_id &m : u.prof->missions() ) {
            mission *new_mission = mission::reserve_new( m, character_id() );
            new_mission->assign( u );
        }
    }
    g->events().send<event_type::game_start>( u.getID() );
    for( Skill &elem : Skill::skills ) {
        int level = u.get_skill_level_object( elem.ident() ).level();
        if( level > 0 ) {
            g->events().send<event_type::gains_skill_level>( u.getID(), elem.ident(), level );
        }
    }

    cata::run_hooks( "on_game_started" );
    return true;
}

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
void game::load_npcs()
{
    ZoneScoped;
    const int radius = g_half_mapsize;
    // uses submap coordinates
    std::vector<shared_ptr_fast<npc>> just_added;
    for( const auto &temp : get_overmapbuffer( current_dimension_id_ ).get_npcs_near_player(
             radius ) ) {
        const character_id &id = temp->getID();
        const auto found = std::find_if( active_npc.begin(), active_npc.end(),
        [id]( const shared_ptr_fast<npc> &n ) {
            return n->getID() == id;
        } );
        if( found != active_npc.end() ) {
            continue;
        }
        if( temp->is_active() ) {
            continue;
        }

        const auto sm_loc = temp->abs_sm_pos();
        // NPCs who are out of bounds before placement would be pushed into bounds
        // This can cause NPCs to teleport around, so we don't want that
        if( sm_loc.x() < get_levx() || sm_loc.x() >= get_levx() + g_mapsize ||
            sm_loc.y() < get_levy() || sm_loc.y() >= get_levy() + g_mapsize ||
            ( sm_loc.z() != get_levz() && !m.has_zlevels() ) ) {
            continue;
        }

        add_msg( m_debug, "game::load_npcs: Spawning static NPC, %d:%d:%d (%d:%d:%d)",
                 get_levx(), get_levy(), get_levz(), sm_loc.x(), sm_loc.y(), sm_loc.z() );
        temp->place_on_map();
        // Validity guard: skip if the NPC's submap is not resident.
        // Bubble eviction is handled by on_submap_unloaded(); no inbounds check needed.
        if( m.get_submap_at( tripoint_bub_ms( temp->bub_pos() ) ) == nullptr ) {
            continue;
        }
        // In the rare case the npc was marked for death while
        // it was on the overmap. Kill it.
        if( temp->marked_for_death ) {
            temp->die( nullptr );
        } else {
            active_npc.push_back( temp );
            just_added.push_back( temp );
            ++g_npc_friends_dirty_version;
        }
    }

    // Activate NPCs for non-reality-bubble load requests (fire spread, player bases, scripts).
    // Each request gets a temporary tinymap providing the NPC context for that region.
    // tinymap disables the circle guard so all square-footprint submaps are loaded.
    for( const auto &req : submap_loader.non_bubble_requests() ) {
        const int mapsize = 2 * req.radius + 1;
        tinymap req_map( mapsize, m.has_zlevels() );
        req_map.bind_dimension( req.dimension_id );
        const tripoint_abs_sm top_left{
            req.center.raw().x - req.radius,
            req.center.raw().y - req.radius,
            req.center.raw().z
        };
        req_map.load( top_left, false );
        scoped_map_context ctx( req_map );

        for( auto z : std::views::iota( -OVERMAP_DEPTH, OVERMAP_HEIGHT + 1 ) ) {
            const tripoint_abs_sm center_z( req.center.raw().x, req.center.raw().y, z );
            for( const auto &temp : get_overmapbuffer( current_dimension_id_ ).get_npcs_near( center_z,
                    req.radius ) ) {
                const auto id = temp->getID();
                const auto already_active = std::ranges::any_of( active_npc,
                [id]( const shared_ptr_fast<npc> &n ) {
                    return n->getID() == id;
                } );
                if( already_active || temp->is_active() ) {
                    continue;
                }
                temp->place_on_map();
                const auto sm_loc = project_to<coords::sm>( temp->abs_pos() );
                if( !req_map.inbounds( sm_loc )
                    || req_map.get_submap_at_grid( req_map.abs_to_bub( sm_loc ) ) == nullptr ) {
                    continue;
                }
                if( temp->marked_for_death ) {
                    temp->die( nullptr );
                } else {
                    active_npc.push_back( temp );
                    just_added.push_back( temp );
                    ++g_npc_friends_dirty_version;
                }
            }
        }
    }

    for( const auto &npc : just_added ) {
        npc->on_load();
    }

    npcs_dirty = false;
}

void game::unload_npcs()
{
    for( const auto &npc : active_npc ) {
        npc->on_unload();
    }

    active_npc.clear();
}

void game::on_submap_loaded( const tripoint_abs_sm &/*pos*/, const std::string &/*dim_id*/ )
{
    // Schedule an NPC activation scan on the next do_turn().  Any NPCs whose
    // authoritative submap position falls within the newly-simulated submap
    // will be placed on the map by load_npcs().  This covers both reality-bubble
    // entries and non-bubble requests (fire_spread, player_base, script).
    set_npcs_dirty();
}

void game::on_submap_unloaded( const tripoint_abs_sm &pos, const std::string &/*dim_id*/ )
{
    // Deactivate any NPCs whose absolute position falls in the evicted submap.
    // abs_pos() returns position directly (no map lookup), so this is safe to call here.
    auto in_evicted = [&pos]( const shared_ptr_fast<npc> &n ) {
        const auto sm = project_to<coords::sm>( n->abs_pos() );
        return sm.x() == pos.x() && sm.y() == pos.y() && sm.z() == pos.z();
    };
    std::ranges::for_each( active_npc | std::views::filter( in_evicted ),
    []( const shared_ptr_fast<npc> &n ) {
        n->on_unload();
    } );
    std::erase_if( active_npc, in_evicted );

    // Evict monsters whose absolute submap position matches the unloaded submap.
    // all_monsters() snapshots weak_ptrs at construction; despawn_monster() marks hp=0 so the
    // non_dead_range iterator skips evicted entries on subsequent steps, mirroring shift_monsters().
    for( monster &critter : all_monsters() ) {
        const auto sm = project_to<coords::sm>( critter.abs_pos() );
        if( sm == pos ) {
            despawn_monster( critter );
        }
    }
}

void game::reload_npcs()
{
    // TODO: Make it not invoke the "on_unload" command for the NPCs that will be loaded anyway
    // and not invoke "on_load" for those NPCs that avoided unloading this way.
    unload_npcs();
    load_npcs();

    //needs to have all npcs loaded
    for( Character &guy : all_npcs() ) {
        guy.activity->init_all_moves( guy );
    }
}

void game::create_starting_npcs()
{
    if( !get_option<bool>( "STATIC_NPC" ) ||
        get_option<std::string>( "STARTING_NPC" ) == "never" ) {
        return; //Do not generate a starting npc.
    }

    //We don't want more than one starting npc per starting location
    const int radius = 1;
    if( !get_overmapbuffer( current_dimension_id_ ).get_npcs_near_player( radius ).empty() ) {
        return; //There is already an NPC in this starting location
    }

    shared_ptr_fast<npc> tmp = make_shared_fast<npc>();
    tmp->randomize( one_in( 2 ) ? NC_DOCTOR : NC_NONE );
    const auto proj = project_remain<coords::sm>( u.abs_pos() - point_rel_ms::south_east() );
    tmp->spawn_at_precise( proj.quotient, proj.remainder_tripoint );
    get_overmapbuffer( current_dimension_id_ ).insert_npc( tmp );
    tmp->form_opinion( u );
    tmp->set_attitude( NPCATT_NULL );
    //This sets the NPC mission. This NPC remains in the starting location.
    tmp->mission = NPC_MISSION_SHELTER;
    tmp->chatbin.first_topic = "TALK_SHELTER";
    tmp->toggle_trait( trait_id( "NPC_STARTING_NPC" ) );
    tmp->set_fac( faction_id( "no_faction" ) );
    //One random starting NPC mission
    tmp->add_new_mission( mission::reserve_random( ORIGIN_OPENER_NPC, tmp->abs_omt_pos(),
                          tmp->getID() ) );
    cata::run_hooks( "on_creature_spawn", [&]( sol::table & params ) {
        params["creature"] = tmp.get();
    } );
    cata::run_hooks( "on_npc_spawn", [&]( sol::table & params ) {
        params["npc"] = tmp.get();
    } );
}

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

bool game::cleanup_at_end()
{
    // Tier 7: tear down the sidebar HUD doc on leaving gameplay so it never lingers
    // (and renders) over the main menu. Idempotent / no-op when it was never opened.
    sidebar_hud_close();
    if( uquit == QUIT_DIED || uquit == QUIT_SUICIDE ) {
        // Put (non-hallucinations) into the overmap so they are not lost.
        for( monster &critter : all_monsters() ) {
            despawn_monster( critter );
        }
        if( u.has_trait( trait_HAS_NEMESIS ) ) {
            get_overmapbuffer( current_dimension_id_ ).remove_nemesis();
        }
        // Reset NPC factions and disposition
        reset_npc_dispositions();
        // Save the factions', missions and set the NPC's overmap coordinates
        // Npcs are saved in the overmap.
        save_factions_missions_npcs(); //missions need to be saved as they are global for all saves.
        // save artifacts.
        save_artifacts();

        // and the overmap, and the local map.
        save_maps(); //Omap also contains the npcs who need to be saved.
    }

    if( uquit == QUIT_DIED || uquit == QUIT_SUICIDE ) {
        std::vector<std::string> vRip;

        int iMaxWidth = 0;
        int iNameLine = 0;
        int iInfoLine = 0;

        if( u.has_amount( itype_holybook_bible1, 1 ) || u.has_amount( itype_holybook_bible2, 1 ) ||
            u.has_amount( itype_holybook_bible3, 1 ) ) {
            if( !( u.has_trait( trait_id( "CANNIBAL" ) ) || u.has_trait( trait_id( "PSYCHOPATH" ) ) ) ) {
                vRip.emplace_back( "               _______  ___" );
                vRip.emplace_back( "              <       `/   |" );
                vRip.emplace_back( "               >  _     _ (" );
                vRip.emplace_back( "              |  |_) | |_) |" );
                vRip.emplace_back( "              |  | \\ | |   |" );
                vRip.emplace_back( "   ______.__%_|            |_________  __" );
                vRip.emplace_back( " _/                                  \\|  |" );
                iNameLine = vRip.size();
                vRip.emplace_back( "|                                        <" );
                vRip.emplace_back( "|                                        |" );
                iMaxWidth = utf8_width( vRip.back() );
                vRip.emplace_back( "|                                        |" );
                vRip.emplace_back( "|_____.-._____              __/|_________|" );
                vRip.emplace_back( "              |            |" );
                iInfoLine = vRip.size();
                vRip.emplace_back( "              |            |" );
                vRip.emplace_back( "              |           <" );
                vRip.emplace_back( "              |            |" );
                vRip.emplace_back( "              |   _        |" );
                vRip.emplace_back( "              |__/         |" );
                vRip.emplace_back( "             % / `--.      |%" );
                vRip.emplace_back( "         * .%%|          -< @%%%" ); // NOLINT(cata-text-style)
                vRip.emplace_back( "         `\\%`@|            |@@%@%%" );
                vRip.emplace_back( "       .%%%@@@|%     `   % @@@%%@%%%%" );
                vRip.emplace_back( "  _.%%%%%%@@@@@@%%%__/\\%@@%%@@@@@@@%%%%%%" );

            } else {
                vRip.emplace_back( "               _______  ___" );
                vRip.emplace_back( "              |       \\/   |" );
                vRip.emplace_back( "              |            |" );
                vRip.emplace_back( "              |            |" );
                iInfoLine = vRip.size();
                vRip.emplace_back( "              |            |" );
                vRip.emplace_back( "              |            |" );
                vRip.emplace_back( "              |            |" );
                vRip.emplace_back( "              |            |" );
                vRip.emplace_back( "              |           <" );
                vRip.emplace_back( "              |   _        |" );
                vRip.emplace_back( "              |__/         |" );
                vRip.emplace_back( "   ______.__%_|            |__________  _" );
                vRip.emplace_back( " _/                                   \\| \\" );
                iNameLine = vRip.size();
                vRip.emplace_back( "|                                         <" );
                vRip.emplace_back( "|                                         |" );
                iMaxWidth = utf8_width( vRip.back() );
                vRip.emplace_back( "|                                         |" );
                vRip.emplace_back( "|_____.-._______            __/|__________|" );
                vRip.emplace_back( "             % / `_-.   _  |%" );
                vRip.emplace_back( "         * .%%|  |_) | |_)< @%%%" ); // NOLINT(cata-text-style)
                vRip.emplace_back( "         `\\%`@|  | \\ | |   |@@%@%%" );
                vRip.emplace_back( "       .%%%@@@|%     `   % @@@%%@%%%%" );
                vRip.emplace_back( "  _.%%%%%%@@@@@@%%%__/\\%@@%%@@@@@@@%%%%%%" );
            }
        } else {
            vRip.emplace_back( R"(           _________  ____           )" );
            vRip.emplace_back( R"(         _/         `/    \_         )" );
            vRip.emplace_back( R"(       _/      _     _      \_.      )" );
            vRip.emplace_back( R"(     _%\      |_) | |_)       \_     )" );
            vRip.emplace_back( R"(   _/ \/      | \ | |           \_   )" );
            vRip.emplace_back( R"( _/                               \_ )" );
            vRip.emplace_back( R"(|                                   |)" );
            iNameLine = vRip.size();
            vRip.emplace_back( R"( )                                 < )" );
            vRip.emplace_back( R"(|                                   |)" );
            vRip.emplace_back( R"(|                                   |)" );
            vRip.emplace_back( R"(|   _                               |)" );
            vRip.emplace_back( R"(|__/                                |)" );
            iMaxWidth = utf8_width( vRip.back() );
            vRip.emplace_back( R"( / `--.                             |)" );
            vRip.emplace_back( R"(|                                  ( )" );
            iInfoLine = vRip.size();
            vRip.emplace_back( R"(|                                   |)" );
            vRip.emplace_back( R"(|                                   |)" );
            vRip.emplace_back( R"(|     %                         .   |)" );
            vRip.emplace_back( R"(|  @`                            %% |)" );
            vRip.emplace_back( R"(| %@%@%\                *      %`%@%|)" );
            vRip.emplace_back( R"(%%@@@.%@%\%%            `\  %%.%%@@%@)" );
            vRip.emplace_back( R"(@%@@%%%%%@@@@@@%%%%%%%%@@%%@@@%%%@%%@)" );
        }

        const int iOffsetX = TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0;
        const int iOffsetY = TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0;

        catacurses::window w_rip = catacurses::newwin( FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH,
                                   point( iOffsetX, iOffsetY ) );
        sfx::do_player_death_hurt( g->u, true );
        sfx::fade_audio_group( sfx::group::weather, 2000 );
        sfx::fade_audio_group( sfx::group::time_of_day, 2000 );
        sfx::fade_audio_group( sfx::group::context_themes, 2000 );
        sfx::fade_audio_group( sfx::group::fatigue, 2000 );

        // Compute stats once — shared by both RmlUi and curses paths.
        const time_duration rip_survived = calendar::turn - calendar::start_of_cataclysm;
        const int rip_minutes = to_minutes<int>( rip_survived ) % 60;
        const int rip_hours   = to_hours<int>( rip_survived ) % 24;
        const int rip_days    = to_days<int>( rip_survived );
        std::string sSurvived;
        if( rip_days > 0 ) {
            sSurvived = string_format( "%dd %dh %dm", rip_days, rip_hours, rip_minutes );
        } else if( rip_hours > 0 ) {
            sSurvived = string_format( "%dh %dm", rip_hours, rip_minutes );
        } else {
            sSurvived = string_format( "%dm", rip_minutes );
        }
        const int iTotalKills = get_kill_tracker().monster_kill_count();

        // Build RmlUi art: spaces → &nbsp;, line breaks → <br/>, coloured chars
        // → cata_text_to_rml spans, uncoloured → rml_escape.
        Rml::String rip_art_rml;
        for( size_t iY = 0; iY < vRip.size(); ++iY ) {
            if( iY > 0 ) {
                rip_art_rml += "<br/>";
            }
            for( const char c : vRip[iY] ) {
                if( c == ' ' ) {
                    rip_art_rml += "&nbsp;";
                } else {
                    nc_color col = c_light_gray;
                    if( c == '%' )                   { col = c_green; }
                    else if( c == '_' || c == '|' )  { col = c_white; }
                    else if( c == '@' )              { col = c_brown; }
                    else if( c == '*' )              { col = c_red;   }
                    if( col != c_light_gray ) {
                        rip_art_rml += cata_text_to_rml( colorize( std::string( 1, c ), col ) );
                    } else {
                        rip_art_rml += rml_escape( std::string( 1, c ) );
                    }
                }
            }
        }

        struct rip_rml_t {
            Rml::String art_rml;
            Rml::String survived_rml;
            Rml::String kills_rml;
            Rml::String name_rml;
            Rml::DataModelHandle handle;
        };
        auto rml_data = std::make_unique<rip_rml_t>( rip_rml_t{
            .art_rml      = std::move( rip_art_rml ),
            .survived_rml = cata_text_to_rml(
                colorize( _( "Survived:" ), c_white ) + " " + colorize( sSurvived, c_white ) ),
            .kills_rml    = cata_text_to_rml(
                colorize( _( "Kills:" ), c_light_gray ) + " " +
                colorize( std::to_string( iTotalKills ), c_magenta ) ),
            .name_rml     = cata_text_to_rml(
                colorize( _( "In memory of:" ), c_light_gray ) + "\n" +
                colorize( u.name, c_white ) ),
        } );

        rml_doc rml;
        ui_adaptor ui;
        ui.on_screen_resize( [&]( ui_adaptor & ui ) {
            ui.position_from_window( w_rip );
        } );
        ui.mark_resize();

        ui.on_redraw( [&]( const ui_adaptor & ) {
            if( rml ) {
                return;
            }
        } );

        input_context ctxt( "DEATH_SCREEN" );
        rml.open( death_rip_rmlui_enabled(), "death_rip", ctxt,
        [&]( Rml::DataModelConstructor & c ) {
            c.Bind( "art_rml",      &rml_data->art_rml );
            c.Bind( "survived_rml", &rml_data->survived_rml );
            c.Bind( "kills_rml",    &rml_data->kills_rml );
            c.Bind( "name_rml",     &rml_data->name_rml );
            rml_data->handle = c.GetModelHandle();
        } );
        // Dirty all variables so RmlUi evaluates the pre-populated data on
        // the first (and only) frame. Without this the eager-populate path is
        // untested — bindings may not be read until dirtied.
        if( rml ) {
            rml_data->handle.DirtyAllVariables();
        }

        ui_manager::redraw();

        // Last words: RmlUi uses standalone string_input_popup (its own doc);
        // curses path embeds the popup into the rip window at the name position.
        const std::string sLastWords = [&]() -> std::string {
            if( rml )
        {
            return string_input_popup()
                .title( _( "Last Words" ) )
                .max_length( iMaxWidth - 4 - 1 )
                .query_string();
            }
            const int iStartX = FULL_SCREEN_WIDTH / 2 - ( ( iMaxWidth - 4 ) / 2 );
            return string_input_popup()
            .window( w_rip, point( iStartX, iNameLine + 3 ),
                     iStartX + iMaxWidth - 4 - 1 )
            .max_length( iMaxWidth - 4 - 1 )
            .query_string();
        }();
        death_screen();
        const bool is_suicide = uquit == QUIT_SUICIDE;
        events().send<event_type::game_over>( is_suicide, sLastWords );
        // Struck the save_player_data here to forestall Weirdness
        std::string char_filename = generate_memorial_filename( u.name );
        move_save_to_graveyard( char_filename );
        write_memorial_file( char_filename, sLastWords );
        memorial().clear();
        std::vector<std::string> characters = list_active_saves();
        // remove current player from the active characters list, as they are dead
        std::vector<std::string>::iterator curchar = std::find( characters.begin(),
            characters.end(), u.get_save_id() );
        if( curchar != characters.end() ) {
            characters.erase( curchar );
        }

        if( characters.empty() ) {
            bool queryDelete = false;
            bool queryReset = false;

            if( get_option<std::string>( "WORLD_END" ) == "query" ) {
                bool decided = false;
                std::string buffer = _( "Warning: NPC interactions and some other global flags "
                                        "will not all reset when starting a new character in an "
                                        "already-played world.  This can lead to some strange "
                                        "behavior.\n\n"
                                        "Are you sure you wish to keep this world?"
                                      );

                while( !decided ) {
                    uilist smenu;
                    smenu.allow_cancel = false;
                    smenu.addentry( 0, true, 'r', "%s", _( "Reset world" ) );
                    smenu.addentry( 1, true, 'd', "%s", _( "Delete world" ) );
                    smenu.addentry( 2, true, 'k', "%s", _( "Keep world" ) );
                    smenu.query();

                    switch( smenu.ret ) {
                        case 0:
                            queryReset = true;
                            decided = true;
                            break;
                        case 1:
                            queryDelete = true;
                            decided = true;
                            break;
                        case 2:
                            decided = query_yn( buffer );
                            break;
                    }
                }
            }

            if( queryDelete || get_option<std::string>( "WORLD_END" ) == "delete" ) {
                world_generator->delete_world( world_generator->active_world->info->world_name, true );

            } else if( queryReset || get_option<std::string>( "WORLD_END" ) == "reset" ) {
                world_generator->delete_world( world_generator->active_world->info->world_name, false );
            }
        } else if( get_option<std::string>( "WORLD_END" ) != "keep" ) {
            std::string tmpmessage;
            for( auto &character : characters ) {
                tmpmessage += "\n  ";
                tmpmessage += character;
            }
            popup( _( "World retained.  Characters remaining:%s" ), tmpmessage );
        }
        if( gamemode ) {
            gamemode = std::make_unique<special_game>(); // null gamemode or something..
        }
    }

    //Reset any offset due to driving
    set_driving_view_offset( point_zero );

    //clear all sound channels
    sfx::fade_audio_channel( sfx::channel::any, 300 );
    sfx::fade_audio_group( sfx::group::weather, 300 );
    sfx::fade_audio_group( sfx::group::time_of_day, 300 );
    sfx::fade_audio_group( sfx::group::context_themes, 300 );
    sfx::fade_audio_group( sfx::group::fatigue, 300 );

    // Clear dimension tracking state before clearing MAPBUFFER and item types.
    // Metadata must be cleared so stale pointers are not accessed after unload_data().
    fire_loader.clear( submap_loader );
    kept_pocket_dimension_id_.clear();
    loaded_dimensions_.clear();

    // Clear all registered dimension slots.  With multiple simultaneous dimensions
    // (overworld + pocket + nether, etc.) there may be more than two active buffers,
    // so clearing only primary and the player's current dimension would leave orphaned
    // submap data in memory and potentially dangling itype* pointers after unload_data().
    MAPBUFFER_REGISTRY.for_each( []( const std::string &, mapbuffer & buf ) {
        buf.clear();
    } );
    // Clear ALL dimension overmapbuffers, not just the active one.
    // Without this, dimensions the player visited (e.g. pocket dimensions) leave
    // live overmaps in the registry whose settings pointers dangle after
    // the unload_data() call below clears region_settings_map.
    for_each_overmapbuffer( []( const std::string &, overmapbuffer & buf ) {
        buf.clear();
    } );

    avatar &player_character = get_avatar();
    player_character = avatar();

    cleanup_references();
    cleanup_arenas();
    DynamicDataLoader::get_instance().unload_data();


    return true;
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

// MAIN GAME LOOP
// Returns true if game is over (death, saved, quit, etc)
bool game::do_turn()
{
    ZoneScopedN( "game::do_turn" );
    // perf probe: per-turn SIM cost (post-input) + the big sub-phases, rolling
    // avg every 120 turns. Renders are ~1ms but frames are ~30ms apart while
    // moving — this finds where the per-turn time actually goes.
    using _perf_clk = std::chrono::steady_clock;
    static double _perf_sim = 0.0, _perf_cache = 0.0, _perf_mon = 0.0, _perf_world = 0.0;
    static int    _perf_n = 0;
    {
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
        if( !gamemode ) {
            gamemode = std::make_unique<special_game>();
        }
        gamemode->per_turn();
        calendar::turn += 1_turns;
    }
    swapping_dimensions = false;

    // Mark all visibility caches dirty for this turn.  The first redraw will run
    // update_visibility_cache; subsequent redraws within the same turn skip it.
    // Lightmap is NOT blanket-invalidated here — per-submap dirty tracking handles
    // the incremental rebuild; only submaps with actual changes are rebuilt.
    m.invalidate_visibility_caches();

    // starting a new turn, clear out temperature cache
    weather_manager &weather = get_weather();
    {
        weather.clear_temp_cache();
    }

    if( npcs_dirty ) {
        load_npcs();
    }

    {
        timed_events.process();
    }
    {
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
    if( calendar::once_every( 1_days ) ) {
        get_overmapbuffer( current_dimension_id_ ).process_mongroups();
    }

    // Move hordes every 2.5 min
    if( calendar::once_every( time_duration::from_minutes( 2.5 ) ) ) {
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
        u.update_body();
    }

    // Auto-save if autosave is enabled
    if( get_option<bool>( "AUTOSAVE" ) &&
        calendar::once_every( 1_turns * get_option<int>( "AUTOSAVE_TURNS" ) ) &&
        !u.is_dead_state() ) {
        autosave();
    }

    {
        weather.update_weather();
        reset_light_level();
    }

    {
        perhaps_add_random_npc();
        process_voluntary_act_interrupt();
        process_activity();
        update_performance_bubble();
    }
    if( !soundperf ) {
        // Process NPC sound events before they move or they hear themselves talking
        for( npc &guy : all_npcs() ) {
            if( rl_dist( guy.bub_pos(), u.bub_pos() ) < g_max_view_distance ) {
                sounds::process_sound_markers( &guy );
            }
        }
        sounds::process_sound_markers( &u );

        if( u.is_deaf() ) {
            sfx::do_hearing_loss();
        }
    }

    // Process sound events into sound markers for display to the player.

    if( !u.has_effect( effect_sleep ) || uquit == QUIT_WATCH ) {
        if( u.moves > 0 || uquit == QUIT_WATCH ) {
            while( u.moves > 0 || uquit == QUIT_WATCH ) {
                cleanup_dead();
                mon_info_update();
                // Process any new sounds the player caused during their turn.
                if( !soundperf ) {
                    for( npc &guy : all_npcs() ) {
                        if( rl_dist( guy.bub_pos(), u.bub_pos() ) < g_max_view_distance ) {
                            sounds::process_sound_markers( &guy );
                        }
                    }
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

                if( handle_action() ) {
                    ++moves_since_last_save;
                }

                if( is_game_over() ) {
                    return cleanup_at_end();
                }

                if( uquit == QUIT_WATCH ) {
                    break;
                }
                if( u.activity ) {
                    process_activity();
                }
            }
            // Reset displayed sound markers now that the turn is over.
            // We only want this to happen if the player had a chance to examine the sounds.
            sounds::reset_markers();
        }
    }

    if( driving_view_offset.x != 0 || driving_view_offset.y != 0 ) {
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
        if( !u.has_active_bionic( bionic_id( "bio_scent_mask" ) ) &&
            !u.has_trait( trait_id( "DEBUG_NOSCENT" ) ) ) {
            scent.set( u.bub_pos(), u.scent, u.get_type_of_scent() );
            get_overmapbuffer( current_dimension_id_ ).set_scent( u.abs_omt_pos(),  u.scent );
        }
        scent.update( u.bub_pos(), m );
    }

    // We need floor cache before checking falling 'n stuff
    {
        m.build_floor_caches();
    }

    if( !vehperf ) {
        m.process_falling();
        autopilot_vehicles();
        m.vehmove();
    }
    {
        ZoneScopedN( "do_turn_process_items" );
        m.process_items();
    }
    {
        m.creature_in_field( u );
    }
    {
        for( auto &[dim_id, tracker_ptr] : grid_trackers_ ) {
            if( tracker_ptr ) {
                tracker_ptr->update( calendar::turn );
            }
        }
    }
    {
        tick_portal_links();
        tick_temporary_pocket_dimensions();
        tick_vehicle_portal_taps();
    }
    {
        fluid_grid::update( calendar::turn );
    }

    // Apply sounds from previous turn to monster and NPC AI.
    {
        sounds::process_sounds();
    }
    _perf_world += std::chrono::duration<double, std::milli>( _perf_clk::now() - _perf_sim_t0 ).count();
    // Update vision caches for monsters. If this turns out to be expensive,
    // consider a stripped down cache just for monsters.
    {
        const auto _t0 = _perf_clk::now();
        m.build_map_cache( get_levz(), true );
        _perf_cache += std::chrono::duration<double, std::milli>( _perf_clk::now() - _t0 ).count();
    }
    if( !monperf ) {
        const auto _t0 = _perf_clk::now();
        monmove();
        _perf_mon += std::chrono::duration<double, std::milli>( _perf_clk::now() - _t0 ).count();
    }
    if( !npcperf ) {
        npcmove();
    } else {
        sleep_skip_npc_process();
    }
    if( calendar::once_every( 5_minutes ) ) {
        overmap_npc_move();
    }

    update_stair_monsters();
    mon_info_update();
    {
        ZoneScopedN( "do_turn_player_process_turn" );
        u.process_turn();
    }

    {
        ZoneScopedN( "do_turn_lua_every_x" );
        cata::run_on_every_x_hooks( *DynamicDataLoader::get_instance().lua );
    }

    {
        explosion_handler::get_explosion_queue().execute();
    }
    {
        cleanup_dead();
    }

    if( u.moves < 0 && get_option<bool>( "FORCE_REDRAW" ) ) {
        ui_manager::redraw();
        refresh_display();
    }

    if( get_levz() >= 0 && !u.is_underwater() ) {
        handle_weather_effects( weather.weather_id );
    }

    handle_wait_activity_redraw();

    {
        u.update_bodytemp( m, weather );
        character_funcs::update_body_wetness( u, get_weather().get_precise() );
        u.apply_wetness_morale( weather.temperature );
    }

    if( !u.is_deaf() ) {
        sfx::remove_hearing_loss();
    }
    {
        sfx::do_danger_music();
        sfx::do_vehicle_engine_sfx();
        sfx::do_vehicle_exterior_engine_sfx();
        sfx::do_fatigue();
    }

    // reset player noise
    u.volume = 0;

    // Tick all loaded submaps: fields for every submap, items/vehicles for batch-eligible ones.
    {
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
        ensure_distribution_grid_tracker_for( dim_id );
    }
    submap_loader.update_lazy_border_focus( current_dimension_id_, u.abs_pos() );
    submap_loader.update();
    // Destroy trackers for non-primary dimensions with no remaining tracked submaps.
    {
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
        Pathfinding::clear_d_maps();
    }

    // Drain the OS input buffer so key-repeat events generated during world
    // processing don't accumulate and drive movement after key release.  Keep
    // input while activity or auto-move interruption checks are active, so
    // pause/menu keys can still stop long-running actions.
    if( !u.activity && !u.has_destination() ) {
        inp_mngr.pump_events();
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

void game::set_npcs_dirty()
{
    npcs_dirty = true;
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
    for( auto fd : here.field_at( location ) ) {
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

void game::add_npc_follower( const character_id &id )
{
    follower_ids.insert( id );
    u.follower_ids.insert( id );
}

void game::remove_npc_follower( const character_id &id )
{
    follower_ids.erase( id );
    u.follower_ids.erase( id );
}

static void update_faction_api( npc *guy )
{
    if( guy->get_faction_ver() < 2 ) {
        guy->set_fac( your_followers );
        guy->set_faction_ver( 2 );
    }
}

void game::validate_linked_vehicles()
{
    for( auto &veh : m.get_vehicles() ) {
        vehicle *v = veh.v;
        if( v->tow_data.other_towing_point != tripoint_bub_ms::zero() ) {
            vehicle *other_v = veh_pointer_or_null( m.veh_at( v->tow_data.other_towing_point ) );
            if( other_v ) {
                // the other vehicle is towing us.
                v->tow_data.set_towing( other_v, v );
                v->tow_data.other_towing_point = tripoint_bub_ms::zero();
            }
        }
    }
}

void game::validate_mounted_npcs()
{
    for( monster &m : all_monsters() ) {
        if( m.has_effect( effect_ridden ) && m.mounted_player_id.is_valid() ) {
            player *mounted_pl = g->critter_by_id<player>( m.mounted_player_id );
            if( !mounted_pl ) {
                // Target no longer valid.
                m.mounted_player_id = character_id();
                m.remove_effect( effect_ridden );
                continue;
            }
            mounted_pl->mounted_creature = shared_from( m );
            mounted_pl->setpos( m.bub_pos() );
            mounted_pl->add_effect( effect_riding, 1_turns, bodypart_str_id::NULL_ID() );
            m.mounted_player = mounted_pl;
        }
    }
}

void game::validate_npc_followers()
{
    // Make sure visible followers are in the list.
    const std::vector<npc *> visible_followers = get_npcs_if( [&]( const npc & guy ) {
        return guy.is_player_ally();
    } );
    for( npc *guy : visible_followers ) {
        update_faction_api( guy );
        add_npc_follower( guy->getID() );
    }
    // Make sure overmapbuffered NPC followers are in the list.
    for( const auto &temp_guy : get_overmapbuffer( current_dimension_id_ ).get_npcs_near_player(
             300 ) ) {
        npc *guy = temp_guy.get();
        if( guy->is_player_ally() ) {
            update_faction_api( guy );
            add_npc_follower( guy->getID() );
        }
    }
    // Make sure that serialized player followers sync up with game list
    for( const auto &temp_id : u.follower_ids ) {
        add_npc_follower( temp_id );
    }
}

std::set<character_id> game::get_follower_list()
{
    return follower_ids;
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
    ctxt.register_action( "fire" );
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
    ctxt.register_action( "debug_hour_timer" );
    ctxt.register_action( "debug_fps" );
    ctxt.register_action( "debug_mode" );
    ctxt.register_action( "zoom_out" );
    ctxt.register_action( "zoom_in" );
    ctxt.register_action( "toggle_fullscreen" );
    ctxt.register_action( "toggle_pixel_minimap" );
    ctxt.register_action( "toggle_zone_overlay" );
    ctxt.register_action( "toggle_panel_adm" );
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

    const bool is_adjacent = square_dist( mouse_target.xy(), u.bub_pos().xy() ) <= 1;
    const bool is_self = square_dist( mouse_target.xy(), u.bub_pos().xy() ) <= 0;
    if( const monster *const mon = critter_at<monster>( mouse_target ) ) {
        if( !u.sees( *mon ) ) {
            add_msg( _( "Nothing relevant here." ) );
            return false;
        }

        if( !u.primary_weapon().is_gun() ) {
            add_msg( m_info, _( "You are not wielding a ranged weapon." ) );
            return false;
        }

        // TODO: Add weapon range check. This requires weapon to be reloaded.

        act = ACTION_FIRE;
    } else if( is_adjacent &&
               m.close_door( tripoint_bub_ms( mouse_target.xy(), u.bub_pos().z() ), !m.is_outside( u.bub_pos() ),
                             true ) ) {
        act = ACTION_CLOSE;
    } else if( is_self ) {
        act = ACTION_PICKUP;
    } else if( is_adjacent ) {
        act = ACTION_EXAMINE;
    } else {
        add_msg( _( "Nothing relevant here." ) );
        return false;
    }

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
#ifdef COOP_ENABLED
        // C3: send death-status + inventory drop to host before corpse placement.
        // Hooked at QUIT_DIED (not is_dead_state()) so it only fires once death is
        // final — PROMPT_ON_CHARACTER_DEATH quickload bails out at 3479 before this.
        if( coop_client_ ) { coop_client_->notify_death(); }
#endif // COOP_ENABLED
        u.place_corpse();
        return true;
    }
    if( uquit == QUIT_SUICIDE ) {
        if( u.in_vehicle ) {
            m.unboard_vehicle( u.bub_pos() );
        }
#ifdef COOP_ENABLED
        // Same hook as QUIT_DIED: send death-status + inventory drop to host.
        if( coop_client_ ) { coop_client_->notify_death(); }
#endif // COOP_ENABLED
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

void game::move_save_to_graveyard( const std::string &dirname )
{
    const std::string save_dir           = get_active_world()->info->folder_path();
    const std::string graveyard_dir      = PATH_INFO::graveyarddir();
    const std::string graveyard_save_dir = graveyard_dir + dirname + "/";
    const std::string &prefix            = base64_encode( u.get_save_id() ) + ".";

    if( !assure_dir_exist( graveyard_dir ) ) {
        debugmsg( "could not create graveyard path '%s'", graveyard_dir );
    }

    if( !assure_dir_exist( graveyard_save_dir ) ) {
        debugmsg( "could not create graveyard path '%s'", graveyard_save_dir );
    }

    // Close the player SQLite handle before moving files — on Windows, MoveFileExW
    // fails (ERROR_SHARING_VIOLATION) if the file is open without FILE_SHARE_DELETE.
    get_active_world()->release_player_db();

    const auto save_files = get_files_from_path( prefix, save_dir );
    if( save_files.empty() ) {
        debugmsg( "could not find save files in '%s'", save_dir );
    }

    for( const auto &src_path : save_files ) {
        const std::string dst_path = graveyard_save_dir +
                                     src_path.substr( src_path.rfind( '/' ) + 1, std::string::npos );

        if( rename_file( src_path, dst_path ) ) {
            continue;
        }

        // rename() fails across filesystems (EXDEV); fall back to copy then delete
        if( copy_file( src_path, dst_path ) ) {
            if( !remove_file( src_path ) ) {
                debugmsg( "could not remove file '%s' after copying to graveyard", src_path );
            }
            continue;
        }

        debugmsg( "could not move file '%s' to graveyard '%s'", src_path, dst_path );
    }
}

void game::load_master()
{
    using namespace std::placeholders;
    get_active_world()->read_from_file( SAVE_MASTER, std::bind( &game::unserialize_master, this, _1 ),
                                        true );
}

bool game::load_dimension_data()
{
    using namespace std::placeholders;

    // Use dimension-specific filename
    std::string filename = "dimension_data";
    const std::string dim_prefix = get_dimension_prefix();
    if( !dim_prefix.empty() ) {
        filename += "_" + dim_prefix;
    }
    filename += ".gsav";

    // DO NOT reset region type here - it may have been pre-set by travel_to_dimension
    // Load dimension-specific data from dimension-specific file
    // If file exists, unserialize_dimension_data will set the correct region_type
    return get_active_world()->read_from_file( filename,
            std::bind( &game::unserialize_dimension_data, this, _1 ),
            true );
}

bool game::load( const std::string &world )
{
    // Join pre-warm thread before any DDL work.
    init::join_prewarm();
    drain_worker_thread_debugmsgs();

    world_generator->init();
    WORLDINFO *wptr = world_generator->get_world( world );
    if( !wptr ) {
        return false;
    }
    if( wptr->world_saves.empty() ) {
        debugmsg( "world '%s' contains no saves", world );
        return false;
    }

    // Check if pre-warm loaded this world's data (reuse path)
    const auto* prewarm = init::get_prewarm_result();
    const bool reuse_prewarm = prewarm != nullptr
                               && prewarm->world_name == world
                               && prewarm->error.empty();

    try {
        world_generator->set_active_world( wptr );
        g->setup( !reuse_prewarm );
        if( reuse_prewarm ) {
            g->complete_prewarm_reuse( prewarm->mod_ids );
        }
        if( !g->load( wptr->world_saves.front() ) ) {
            return false;
        }
    } catch( const std::exception &err ) {
        debugmsg( "cannot load world '%s': %s", world, err.what() );
        return false;
    }

    return true;
}

void game::complete_prewarm_reuse( const std::vector<mod_id> &mod_ids )
{
    loading_ui ui( true );
    DynamicDataLoader::get_instance().finalize_main_phases( ui );
    DynamicDataLoader::get_instance().check_consistency( ui );
    // Run main Lua scripts using prewarmed Lua state
    auto& loader = DynamicDataLoader::get_instance();
    init::load_main_lua_scripts( *loader.lua, mod_ids );
    cata::clear_mod_being_loaded( *loader.lua );
    refresh_mapgen_postprocess_hook_presence( *loader.lua );
    // Replay post-load steps skipped by setup(false)
    load_artifacts( get_active_world(), SAVE_ARTIFACTS );
    panel_manager::get_manager().reload_widget_layouts();
}

bool game::load( const save_t &name )
{
    auto background = std::unique_ptr<background_pane>();
    auto popup = std::unique_ptr<static_popup>();
    if( !test_mode ) {
        background = std::make_unique<background_pane>();
        popup = std::make_unique<static_popup>();
        popup->message( "%s", _( "Please wait…\nLoading the save…" ) );
    }

    using namespace std::placeholders;

    saving_blocked_by_failed_load = true;
    auto save_json_valid = false;
    const auto validate_save = [&]( std::istream & fin ) { save_json_valid = validate_save_json( fin ); };
    if( !get_active_world()->read_from_file( name.base_path() + SAVE_EXTENSION, validate_save ) ||
        !save_json_valid ) {
        return false;
    }

    // Now load up the master game data; factions (and more?)
    load_master();
    u = avatar();
    u.recalc_hp();
    u.set_save_id( name.decoded_name() );
    u.name = name.decoded_name();
    // Set the correct bubble radius BEFORE unserialize() so the submap_loader
    // request uses the right radius and update_map() does not null active grid slots.
    init_bubble_config();
    reality_bubble_radius_ = g_half_mapsize;
    // If a stale request exists from a previous load in the same session, release
    // it so load_map_at() recreates it with the correct (possibly changed) radius.
    if( reality_bubble_handle_ != 0 ) {
        submap_loader.release_load( reality_bubble_handle_ );
        reality_bubble_handle_ = 0;
    }
    if( lazy_border_handle_ != 0 ) {
        submap_loader.release_load( lazy_border_handle_ );
        lazy_border_handle_ = 0;
    }
    fire_loader.clear( submap_loader );
    auto unserialized = false;
    const auto load_save = [&]( std::istream & fin ) { unserialized = unserialize( fin ); };
    if( !get_active_world()->read_from_file( name.base_path() + SAVE_EXTENSION, load_save ) ||
        !unserialized ) {
        return false;
    }

    // Restore per-dimension data (region type, etc.) for the dimension the player
    // was in when they saved.  travel_to_dimension() normally does this on first
    // visit; replicate it here because travel_to_dimension() is never invoked during
    // a plain game::load().
    load_dimension_data();

    // Reconstruct the dimension_info entry for the current dimension so that
    // get_current_dimension_info() returns a valid pointer.
    // travel_to_dimension() populates loaded_dimensions_ on first visit; on load
    // we must do it explicitly.  The world_type is recovered by matching save_prefix.
    if( !loaded_dimensions_.count( current_dimension_id_ ) ) {
        auto effective_wt = world_types::get_default();
        if( !current_dimension_id_.empty() ) {
            std::ranges::for_each( world_types::get_all(),
            [&]( const world_type & wt ) {
                if( !wt.save_prefix.empty() &&
                    current_dimension_id_.starts_with( wt.save_prefix ) ) {
                    effective_wt = wt.id;
                }
            } );
        }
        const struct world_type *target_type = effective_wt.is_valid() ? &effective_wt.obj() :
                                               nullptr;
        loaded_dimensions_[current_dimension_id_] = dimension_info{
            .dimension_id = current_dimension_id_,
            .world_type   = effective_wt,
            .display_name = target_type ? target_type->name.translated() : current_dimension_id_,
            // Restore bounds so that travel_to_dimension()'s old_is_bounded check
            // returns the correct result when the player subsequently leaves a
            // bounded pocket dimension after reload.  Without this, the loaded_
            // dimensions_ entry has nullopt bounds even though the dimension IS
            // bounded.
            .pocket_info = get_map().get_pocket_info()
        };
    }

    // This needs to be here for some reason for quickload() to work.
    // Prevent underlying game UI from drawing while we're still in the loading popup.
    {
        ui_adaptor ui( ui_adaptor::disable_uis_below {} );
        ui_manager::redraw();
        refresh_display();
    }
    u.load_map_memory();
    u.get_avatar_diary()->load();

    get_weather().nextweather = calendar::turn;

    get_active_world()->read_from_file( name.base_path() + SAVE_EXTENSION_LOG,
                                        std::bind( &memorial_logger::load, &memorial(), _1 ), true );

    // Now that the player's worn items are updated, their sight limits need to be
    // recalculated. (This would be cleaner if u.worn were private.)
    u.recalc_sight_limits();

    if( !gamemode ) {
        gamemode = std::make_unique<special_game>();
    }

    safe_mode = get_option<bool>( "SAFEMODE" ) ? SAFE_MODE_ON : SAFE_MODE_OFF;
    mostseen = 0; // ...and mostseen is 0, we haven't seen any monsters yet.

    init_autosave();
    get_auto_pickup().load_character(); // Load character auto pickup rules
    get_auto_notes_settings().load();   // Load character auto notes settings
    get_safemode().load_character(); // Load character safemode rules
    zone_manager::get_manager().load_zones(); // Load character world zones
    get_active_world()->read_from_file( "uistate.json", []( std::istream & stream ) {
        JsonIn jsin( stream );
        uistate.deserialize( jsin );
    }, true );
    reload_npcs();
    validate_npc_followers();
    validate_mounted_npcs();
    validate_linked_vehicles();
    // Re-read the bubble-size option for the submap-loader request.
    // Do NOT call m.resize() here — the grid is already filled by unserialize().
    // setup() already called init_bubble_config() + m.resize().
    init_bubble_config();
    reality_bubble_radius_ = g_half_mapsize;
    // Old saves can have duplicate authority for in-bubble monsters: one copy in
    // active_monsters and another in overmap monster_map.  Purge the stale overmap
    // buckets before update_map() gets a chance to spawn newly-entered submaps.
    discard_monster_map_for_loaded_bubble( m, current_dimension_id_ );
    // Repair active monsters left outside every loaded submap by older broken saves.
    for( auto &critter : all_monsters() ) {
        if( m.get_submap_at( critter.bub_pos() ) == nullptr ) {
            despawn_monster( critter );
        }
    }
    update_map( u );
    discard_monster_map_for_loaded_bubble( m, current_dimension_id_ );
    m.build_floor_cache( get_levz() );
    for( auto &e : u.inv_dump() ) {
        e->set_owner( g->u );
    }
    // legacy, needs to be here as we access the map.
    if( !u.getID().is_valid() ) {
        // player does not have a real id, so assign a new one,
        u.setID( assign_npc_id() );
        // The vehicle stores the IDs of the boarded players, so update it, too.
        if( u.in_vehicle ) {
            if( const std::optional<vpart_reference> vp = m.veh_at(
                    u.bub_pos() ).part_with_feature( "BOARDABLE", true ) ) {
                vp->part().passenger_id = u.getID();
            }
        }
    }

    // populate calendar caches now, after active world is set, but before we do
    // anything else, to ensure they pick up the correct value from the save's
    // worldoptions
    calendar::set_eternal_season( ::get_option<bool>( "ETERNAL_SEASON" ) );
    calendar::set_season_length( ::get_option<int>( "SEASON_LENGTH" ) );

    u.reset();
    //needs all npcs and stats loaded
    u.activity->init_all_moves( u );

    cata::load_world_lua_state( get_active_world(), "lua_state.json" );

    cata::run_on_game_load_hooks( *DynamicDataLoader::get_instance().lua );

    // Build caches once so any immediate post-load draws don't use uninitialized lighting/visibility,
    // then re-invalidate so the first real in-game draw rebuilds everything again.
    m.invalidate_map_cache( get_levz() );
    m.build_map_cache( get_levz() );
    m.update_visibility_cache( get_levz() );
    m.invalidate_map_cache( get_levz() );

    saving_blocked_by_failed_load = false;
    return true;
}

void game::reset_npc_dispositions()
{
    for( auto elem : follower_ids ) {
        shared_ptr_fast<npc> npc_to_get = get_overmapbuffer( current_dimension_id_ ).find_npc( elem );
        if( !npc_to_get )  {
            continue;
        }
        npc *npc_to_add = npc_to_get.get();
        npc_to_add->chatbin.missions.clear();
        npc_to_add->chatbin.missions_assigned.clear();
        npc_to_add->mission = NPC_MISSION_NULL;
        npc_to_add->chatbin.mission_selected = nullptr;
        npc_to_add->set_attitude( NPCATT_NULL );
        npc_to_add->op_of_u.anger = 0;
        npc_to_add->op_of_u.fear = 0;
        npc_to_add->op_of_u.trust = 0;
        npc_to_add->op_of_u.value = 0;
        npc_to_add->op_of_u.owed = 0;
        npc_to_add->set_fac( faction_id( "no_faction" ) );
        npc_to_add->add_new_mission( mission::reserve_random( ORIGIN_ANY_NPC,
                                     npc_to_add->abs_omt_pos(),
                                     npc_to_add->getID() ) );

    }

}

//Saves all factions and missions and npcs.
bool game::save_factions_missions_npcs()
{
    return get_active_world()->write_to_file( SAVE_MASTER, [&]( std::ostream & fout ) {
        serialize_master( fout );
    }, _( "factions data" ) );
}

//Saves per-dimension data like Weather and overmapbuffer state
bool game::save_dimension_data()
{
    // Use dimension-specific filename
    std::string filename = "dimension_data";
    const std::string dim_prefix = get_dimension_prefix();
    if( !dim_prefix.empty() ) {
        filename += "_" + dim_prefix;
    }
    filename += ".gsav";

    return get_active_world()->write_to_file( filename, [&]( std::ostream & fout ) {
        serialize_dimension_data( fout );
    }, _( "dimension data" ) );
}

bool game::save_artifacts()
{
    return ::save_artifacts( get_active_world(), SAVE_ARTIFACTS );
}

bool game::save_maps()
{
    try {
        // Drain any in-flight load-manager tasks before save so save_omt workers
        // do not race with background workers calling add_submap().
        submap_loader.drain_lazy_loads();
        save_all_overmapbuffers(); // can throw — saves every loaded dimension's overmapbuffer
        // Save mapbuffers for all registered dimensions (active + any kept/non-active).
        // save_all() dispatches dimension saves in parallel; each slot uses
        // notify_tracker=is_primary and show_progress=false (worker-thread safe).
        MAPBUFFER_REGISTRY.save_all(); // can throw
        return true;
    } catch( const std::exception &err ) {
        popup( _( "Failed to save the maps: %s" ), err.what() );
        return false;
    }
}

bool game::save_player_data()
{
    world *world = get_active_world();
    const bool saved_data = world->write_to_player_file( SAVE_EXTENSION, [&]( std::ostream & fout ) {
        serialize( fout );
    }, _( "player data" ) );
    const bool saved_map_memory = u.save_map_memory();
    const bool saved_log = world->write_to_player_file( SAVE_EXTENSION_LOG, [&](
    std::ostream & fout ) {
        fout << memorial().dump();
    }, _( "player memorial" ) );
    const bool saved_diary = u.get_avatar_diary()->store();
    return saved_data && saved_map_memory && saved_log && saved_diary
           ;
}

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

bool game::save_uistate_data() const
{
    return get_active_world()->write_to_file( "uistate.json", [&]( std::ostream & fout ) {
        JsonOut jsout( fout );
        uistate.serialize( jsout );
    }, _( "uistate data" ) );
}

bool game::save( bool quitting )
{
    world *world = get_active_world();
    if( !world ) {
        return false;
    }
    if( saving_blocked_by_failed_load ) {
        return false;
    }

    world->start_save_tx();

    cata::run_on_game_save_hooks( *DynamicDataLoader::get_instance().lua );
    try {
        reset_save_ids( time( nullptr ), quitting );
        if( !save_factions_missions_npcs() ||
            !save_artifacts() ||
            !save_maps() ||
            !save_player_data() ||
            !get_auto_pickup().save_character() ||
            !get_auto_notes_settings().save() ||
            !get_safemode().save_character() ||
            !cata::save_world_lua_state( get_active_world(), "lua_state.json" ) ||
            !save_uistate_data()
          ) {
            return false;
        } else {
            world_generator->last_world_name = world_generator->active_world->info->world_name;
            world_generator->last_character_name = u.name;
            world_generator->save_last_world_info();
            world_generator->active_world->info->add_save( save_t::from_save_id( u.get_save_id() ) );

            auto duration = world->commit_save_tx();
            add_msg( m_info, _( "World Saved (took %dms)." ), duration );
            return true;
        }
    } catch( std::ios::failure &err ) {
        popup( _( "Failed to save game data" ) );
        return false;
    }
}

std::vector<std::string> game::list_active_saves()
{
    std::vector<std::string> saves;
    for( auto &worldsave : world_generator->active_world->info->world_saves ) {
        saves.push_back( worldsave.decoded_name() );
    }
    return saves;
}

/**
 * Writes information about the character out to a text file timestamped with
 * the time of the file was made. This serves as a record of the character's
 * state at the time the memorial was made (usually upon death) and
 * accomplishments in a human-readable format.
 */
void game::write_memorial_file( const std::string &filename, std::string sLastWords )
{
    const std::string &memorial_dir = PATH_INFO::memorialdir();
    const std::string &memorial_active_world_dir = memorial_dir +
        world_generator->active_world->info->world_name + "/";

    //Check if both dirs exist. Nested assure_dir_exist fails if the first dir of the nested dir does not exist.
    if( !assure_dir_exist( memorial_dir ) ) {
        debugmsg( "Could not make '%s' directory", memorial_dir );
        return;
    }

    if( !assure_dir_exist( memorial_active_world_dir ) ) {
        debugmsg( "Could not make '%s' directory", memorial_active_world_dir );
        return;
    }

    std::string path = memorial_active_world_dir + filename + ".txt";

    write_to_file( path, [&]( std::ostream & fout ) {
        memorial().write( fout, sLastWords );
    }, _( "player memorial" ) );
}

void game::disp_NPC_epilogues()
{
    // TODO: This search needs to be expanded to all NPCs
    for( auto elem : follower_ids ) {
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
shared_ptr_fast<ui_adaptor> game::create_or_get_main_ui_adaptor()
{
    shared_ptr_fast<ui_adaptor> ui = main_ui_adaptor.lock();
    if( !ui ) {
        main_ui_adaptor = ui = make_shared_fast<ui_adaptor>();
        ui->on_redraw( []( ui_adaptor & ui ) {
            g->draw( ui );
        } );
        ui->on_screen_resize( [this]( ui_adaptor & ui ) {
            // remove some space for the sidebar, this is the maximal space
            // (using standard font) that the terrain window can have
            const int sidebar_left = panel_manager::get_manager().get_width_left();
            const int sidebar_right = panel_manager::get_manager().get_width_right();
            const int top = sidebar_hud_top_rows();
            const int bottom = sidebar_hud_bottom_rows();

            TERRAIN_WINDOW_HEIGHT = TERMY - top - bottom;
            TERRAIN_WINDOW_WIDTH = TERMX - ( sidebar_left + sidebar_right );
            TERRAIN_WINDOW_TERM_WIDTH = TERRAIN_WINDOW_WIDTH;
            TERRAIN_WINDOW_TERM_HEIGHT = TERRAIN_WINDOW_HEIGHT;

            /**
             * In tiles mode w_terrain can have a different font (with a different
             * tile dimension) or can be drawn by cata_tiles which uses tiles that again
             * might have a different dimension then the normal font used everywhere else.
             *
             * TERRAIN_WINDOW_WIDTH/TERRAIN_WINDOW_HEIGHT defines how many squares can
             * be displayed in w_terrain (using it's specific tile dimension), not
             * including partially drawn squares at the right/bottom. You should
             * use it whenever you want to draw specific squares in that window or to
             * determine whether a specific square is draw on screen (or outside the screen
             * and needs scrolling).
             *
             * TERRAIN_WINDOW_TERM_WIDTH/TERRAIN_WINDOW_TERM_HEIGHT defines the size of
             * w_terrain in the standard font dimension (the font that everything else uses).
             * You usually don't have to use it, expect for positioning of windows,
             * because the window positions use the standard font dimension.
             *
             * The code here calculates size available for w_terrain, caps it at
             * max_view_size (the maximal view range than any character can have at
             * any time).
             * It is stored in TERRAIN_WINDOW_*.
             */
            to_map_font_dimension( TERRAIN_WINDOW_WIDTH, TERRAIN_WINDOW_HEIGHT );

            // Position of the player in the terrain window, it is always in the center
            POSX = TERRAIN_WINDOW_WIDTH / 2;
            POSY = TERRAIN_WINDOW_HEIGHT / 2;

            w_terrain = w_terrain_ptr = catacurses::newwin( TERRAIN_WINDOW_HEIGHT, TERRAIN_WINDOW_WIDTH,
                                        point( sidebar_left, top ) );

            // minimap is always MINIMAP_WIDTH x MINIMAP_HEIGHT in size
            w_minimap = w_minimap_ptr = catacurses::newwin( MINIMAP_HEIGHT, MINIMAP_WIDTH, point_zero );

            // need to init in order to avoid crash. gets updated by the panel code.
            w_pixel_minimap = catacurses::newwin( 1, 1, point_zero );

            ui.position_from_window( catacurses::stdscr );
        } );
        ui->mark_resize();
    }
    return ui;
}

void game::invalidate_main_ui_adaptor() const
{
    shared_ptr_fast<ui_adaptor> ui = main_ui_adaptor.lock();
    if( ui ) {
        ui->invalidate_ui();
    }
}

void game::mark_main_ui_adaptor_resize() const
{
    shared_ptr_fast<ui_adaptor> ui = main_ui_adaptor.lock();
    if( ui ) {
        ui->mark_resize();
    }
}

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

void game::add_draw_callback( const shared_ptr_fast<draw_callback_t> &cb )
{
    draw_callbacks.erase(
        std::remove_if( draw_callbacks.begin(), draw_callbacks.end(),
    []( const weak_ptr_fast<draw_callback_t> &cbw ) {
        return cbw.expired();
    } ),
    draw_callbacks.end()
    );
    draw_callbacks.emplace_back( cb );
    cb->added = true;
    invalidate_main_ui_adaptor();
}


void game::draw( ui_adaptor &ui )
{
    if( test_mode ) {
        return;
    }
    ZoneScopedN( "game_draw" );

    //temporary fix for updating visibility for minimap
    ter_view_p.z() = ( u.bub_pos() + u.view_offset ).z();
    {
        ZoneScopedN( "game_draw_cache" );
        if( is_looking && ter_view_p.z() != u.bub_pos().z() ) {
            // Keep visibility calculations based on the player position while still building the viewed z-level cache.
            m.build_map_cache( ter_view_p.z() );
        }
        const auto cache_z = is_looking ? u.bub_pos().z() : ter_view_p.z();
        m.build_map_cache( cache_z );
        if( m.get_cache_ref( cache_z ).visibility_cache_dirty ) {
            m.update_visibility_cache( cache_z );
        }
    }

    werase( w_terrain );
    draw_ter();
    {
        ZoneScopedN( "game_draw_callbacks" );
        for( auto it = draw_callbacks.begin(); it != draw_callbacks.end(); ) {
            shared_ptr_fast<draw_callback_t> cb = it->lock();
            if( cb ) {
                ( *cb )();
                ++it;
            } else {
                it = draw_callbacks.erase( it );
            }
        }
    }
    {
        ZoneScopedN( "game_draw_wrefresh" );
        wnoutrefresh( w_terrain );
    }

    draw_panels( true );

    // Ensure that the cursor lands on the character when everything is drawn.
    // This allows screen readers to describe the area around the player, making it
    // much easier to play with them
    // (e.g. for blind players)
    ui.set_cursor( w_terrain, -u.view_offset.xy().raw() + point( POSX, POSY ) );
}

void game::draw_panels( bool /* force_draw */ )
{
    ZoneScopedN( "draw_panels" );
    // Tier-10 curses rip-out: the sidebar is rendered entirely by the RmlUi HUD.
    // draw_panels only drives that persistent document — open/sync while enabled,
    // close on toggle-off. The legacy curses panel render path has been removed.
    //
    // Guard: never reopen the HUD during shutdown. cleanup_at_end() closes it at
    // the top, but ui_adaptor redraws fired by the death/quit screen will re-enter
    // draw_panels. If we let sidebar_hud_open() run at that point the RmlUI data
    // model is recreated against a partially-torn-down context → SIGSEGV in
    // DataTypeRegister::GetDefinitionDetail<std::string>.
    if( uquit != QUIT_NO || !sidebar_hud_rmlui_enabled() ) {
        sidebar_hud_close();
        return;
    }
    sidebar_hud_open();
    sidebar_hud_sync( u );
}

void game::draw_pixel_minimap( const catacurses::window &w )
{
    w_pixel_minimap = w;
}

bool game::is_in_viewport( const tripoint_bub_ms &p, int margin ) const
{
    const tripoint_rel_ms diff( u.bub_pos() + u.view_offset - p );

    return ( std::abs( diff.x() ) <= getmaxx( w_terrain ) / 2 - margin ) &&
           ( std::abs( diff.y() ) <= getmaxy( w_terrain ) / 2 - margin );
}

void game::draw_ter( const bool draw_sounds )
{
    draw_ter( u.bub_pos() + u.view_offset, is_looking,
              draw_sounds );
}

void game::draw_ter( const tripoint_bub_ms &center, const bool looking,
                     const bool /* draw_sounds */ )
{
    ZoneScopedN( "draw_ter" );
    ter_view_p = center;

    // Smooth sub-tile view follow. center stays integer (drives the z-loop,
    // cursor, footsteps); the camera only adds a fractional residual that
    // cata_tiles folds into o/op so sprites and lighting scroll together.
    // Snap while looking/aiming to keep a crisp cursor-driven view.
    main_camera_.set_follow_speed( camera_dbg::smooth_speed );
    main_camera_.set_look_ahead( camera_dbg::look_ahead );
    main_camera_.set_dead_zone( camera_dbg::dead_zone );
    main_camera_.update( center.xy().raw(), looking );
    if( tilecontext ) {
        tilecontext->set_subtile_offset( main_camera_.sub_x(), main_camera_.sub_y() );
    }

    // Place the cursor over the player as is expected by screen readers.
    wmove( w_terrain, -center.xy().raw() + g->u.bub_pos().xy().raw() + point( POSX, POSY ) );
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

float game::natural_light_level( const int zlev ) const
{
    // ignore while underground or above limits
    if( zlev > OVERMAP_HEIGHT || zlev < 0 ) {
    return LIGHT_AMBIENT_MINIMAL;
}

if( latest_lightlevels[zlev] > -std::numeric_limits<float>::max() ) {
        // Already found the light level for now?
        return latest_lightlevels[zlev];
    }

    float ret = LIGHT_AMBIENT_MINIMAL;

    // Sunlight/moonlight related stuff
    const weather_manager &weather = get_weather();
    if( !weather.lightning_active ) {
    ret = sunlight( calendar::turn );
    } else {
        // Recent lightning strike has lit the area
        ret = default_daylight_level();
    }

    ret += get_weather().weather_id->light_modifier;

    // Artifact light level changes here. Even though some of these only have an effect
    // aboveground it is cheaper performance wise to simply iterate through the entire
    // list once instead of twice.
    float mod_ret = -1;
    // Each artifact change does std::max(mod_ret, new val) since a brighter end value
    // will trump a lower one.
    if( const timed_event *e = timed_events.get( TIMED_EVENT_DIM ) ) {
        // TIMED_EVENT_DIM slowly dims the natural sky level, then relights it.
        const time_duration left = e->when - calendar::turn;
        // TIMED_EVENT_DIM has an occurrence date of turn + 50, so the first 25 dim it,
        if( left > 25_turns ) {
            mod_ret = std::max( static_cast<double>( mod_ret ), ( ret * ( left - 25_turns ) ) / 25_turns );
            // and the last 25 scale back towards normal.
        } else {
            mod_ret = std::max( static_cast<double>( mod_ret ), ( ret * ( 25_turns - left ) ) / 25_turns );
        }
    }
    if( timed_events.queued( TIMED_EVENT_ARTIFACT_LIGHT ) ) {
        // TIMED_EVENT_ARTIFACT_LIGHT causes everywhere to become as bright as day.
        mod_ret = std::max<float>( ret, default_daylight_level() );
    }
    // If we had a changed light level due to an artifact event then it overwrites
    // the natural light level.
    if( mod_ret > -1 ) {
    ret = mod_ret;
}

// Cap everything to our minimum light level
ret = std::max<float>( LIGHT_AMBIENT_MINIMAL, ret );

latest_lightlevels[zlev] = ret;

return ret;
}

unsigned char game::light_level( const int zlev ) const
{
    const float light = natural_light_level( zlev );
    return LIGHT_RANGE( light );
}

void game::reset_light_level()
{
    for( float &lev : latest_lightlevels ) {
        lev = -std::numeric_limits<float>::max();
    }
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
std::unordered_set<tripoint_bub_ms> game::get_fishable_locations( int radius,
        const tripoint_bub_ms &fish_pos )
{
    // We're going to get the contiguous fishable terrain starting at
    // the provided fishing location (e.g. where a line was cast or a fish
    // trap was set), and then check whether or not fishable monsters are
    // actually in those locations. This will help us ensure that we're
    // getting our fish from the location that we're ACTUALLY fishing,
    // rather than just somewhere in the vicinity.

    std::unordered_set<tripoint_bub_ms> visited;

    const tripoint_bub_ms fishing_boundary_min( fish_pos + point_rel_ms( -radius, -radius ) );
    const tripoint_bub_ms fishing_boundary_max( fish_pos + point_rel_ms( radius, radius ) );

    const inclusive_cuboid<tripoint_bub_ms> fishing_boundaries(
        fishing_boundary_min, fishing_boundary_max );

    const auto get_fishable_terrain = [&]( tripoint_bub_ms starting_point,
    std::unordered_set<tripoint_bub_ms> &fishable_terrain ) {
        std::queue<tripoint_bub_ms> to_check;
        to_check.push( starting_point );
        while( !to_check.empty() ) {
            const auto current_point = to_check.front();
            to_check.pop();

            // We've been here before, so bail.
            if( visited.contains( current_point ) ) {
                continue;
            }

            // This point is out of bounds, so bail.
            if( !fishing_boundaries.contains( current_point ) ) {
                continue;
            }

            // Mark this point as visited.
            visited.emplace( current_point );

            if( m.has_flag( "FISHABLE", current_point ) ) {
                fishable_terrain.emplace( current_point );
                to_check.push( current_point + point_south );
                to_check.push( current_point + point_north );
                to_check.push( current_point + point_east );
                to_check.push( current_point + point_west );
            }
        }
        return;
    };

    // Starting at the provided location, get our fishable terrain
    // and populate a set with those locations which we'll then use
    // to determine if any fishable monsters are in those locations.
    std::unordered_set<tripoint_bub_ms> fishable_points;
    get_fishable_terrain( fish_pos, fishable_points );

    return fishable_points;
}


void game::mon_info_update( )
{
    ZoneScoped;

    int newseen = 0;
    const auto iProxyDist = ( safe_mode_proximity <= 0 ) ? g_max_view_distance : safe_mode_proximity;

    monster_visible_info &mon_visible = u.get_mon_visible();
    auto &new_seen_mon = mon_visible.new_seen_mon;
    auto &unique_types = mon_visible.unique_types;
    auto &unique_mons = mon_visible.unique_mons;
    auto &dangerous = mon_visible.dangerous;

    // 7 0 1    unique_types uses these indices;
    // 6 8 2    0-7 are provide by direction_from()
    // 5 4 3    8 is used for local monsters (for when we explain them below)
    for( auto &t : unique_types ) {
        t.clear();
    }
    for( auto &m : unique_mons ) {
        m.clear();
    }
    std::fill( dangerous, dangerous + 8, false );
    mon_visible.visible_count_by_dir.fill( 0 );
    mon_visible.nearby_hostile_count = 0;
    mon_visible.combat_hostile_count = 0;
    const int combat_bubble_range = SEEX * ( get_option<int>( "COMBAT_BUBBLE_SIZE" ) + 1 );

    const auto view = u.bub_pos() + u.view_offset;
    new_seen_mon.clear();

    // TODO: no reason to have it static here
    static time_point previous_turn = calendar::start_of_cataclysm;
    const time_duration sm_ignored_time = time_duration::from_turns(
            get_option<int>( "SAFEMODEIGNORETURNS" ) );

    const auto direction_index = []( const direction dir ) -> int {
        switch( dir )
    {
            // *INDENT-OFF*
            case direction::ABOVENORTHWEST: case direction::NORTHWEST: case direction::BELOWNORTHWEST: return 7;
            case direction::ABOVENORTH:     case direction::NORTH:     case direction::BELOWNORTH:     return 0;
            case direction::ABOVENORTHEAST: case direction::NORTHEAST: case direction::BELOWNORTHEAST: return 1;
            case direction::ABOVEWEST:      case direction::WEST:      case direction::BELOWWEST:      return 6;
            case direction::ABOVEEAST:      case direction::EAST:      case direction::BELOWEAST:      return 2;
            case direction::ABOVESOUTHWEST: case direction::SOUTHWEST: case direction::BELOWSOUTHWEST: return 5;
            case direction::ABOVESOUTH:     case direction::SOUTH:     case direction::BELOWSOUTH:     return 4;
            case direction::ABOVESOUTHEAST: case direction::SOUTHEAST: case direction::BELOWSOUTHEAST: return 3;
            case direction::ABOVECENTER:    case direction::CENTER:    case direction::BELOWCENTER:    return 8;
            case direction::last: break;
            // *INDENT-ON*
    }
    debugmsg( "invalid direction" );
    abort();
    return 8;
};

const auto compass_direction_index = []( const direction dir ) -> int {
        switch( dir )
    {
            // *INDENT-OFF*
            case direction::ABOVENORTHWEST: case direction::NORTHWEST: case direction::BELOWNORTHWEST: return 7;
            case direction::ABOVENORTH:     case direction::NORTH:     case direction::BELOWNORTH:     return 0;
            case direction::ABOVENORTHEAST: case direction::NORTHEAST: case direction::BELOWNORTHEAST: return 1;
            case direction::ABOVEWEST:      case direction::WEST:      case direction::BELOWWEST:      return 6;
            case direction::ABOVEEAST:      case direction::EAST:      case direction::BELOWEAST:      return 2;
            case direction::ABOVESOUTHWEST: case direction::SOUTHWEST: case direction::BELOWSOUTHWEST: return 5;
            case direction::ABOVESOUTH:     case direction::SOUTH:     case direction::BELOWSOUTH:     return 4;
            case direction::ABOVESOUTHEAST: case direction::SOUTHEAST: case direction::BELOWSOUTHEAST: return 3;
            default: return 8;
            // *INDENT-ON*
    }
};

const auto player_attitude_from = []( const monster_attitude matt ) -> Attitude {
        switch( matt )
    {
        case MATT_FRIEND:
        case MATT_FPASSIVE:
        case MATT_ZLAVE:
            return Attitude::A_FRIENDLY;
        case MATT_ATTACK:
            return Attitude::A_HOSTILE;
        case MATT_FLEE:
        case MATT_FOLLOW:
        case MATT_IGNORE:
        case MATT_NULL:
        case MATT_UNKNOWN:
        case NUM_MONSTER_ATTITUDES:
            return Attitude::A_NEUTRAL;
    }
    return Attitude::A_NEUTRAL;
};

const auto visible_info = [&]( const tripoint_bub_ms & pos ) {
        const auto dir_to_mon = direction_from( view.xy(), point_bub_ms( pos.x(), pos.y() ) );
        const auto mx = POSX + ( pos.x() - view.x() );
        const auto my = POSY + ( pos.y() - view.y() );
        auto index = 8;
        if( !is_valid_in_w_terrain( point( mx, my ) ) ) {
            // for compatibility with old code, see diagram below, it explains the values for index,
            // also might need revisiting one z-levels are in.
            index = direction_index( dir_to_mon );
        }
        const auto compass_dir = direction_from( u.bub_pos().xy(), point_bub_ms( pos.x(), pos.y() ) );
        return std::pair{ index, compass_direction_index( compass_dir ) };
    };

    const auto safemode_empty = get_safemode().empty();
    const auto process_monster = [&]( const shared_ptr_fast<monster> &mon_ptr ) {
        if( !mon_ptr || mon_ptr->is_dead() ) {
            return;
        }
        monster &critter = *mon_ptr;
        const auto mon_dist = rl_dist( u.bub_pos(), critter.bub_pos() );
        if( u.bub_pos() == critter.bub_pos() || mon_dist > g_mapsize_x || !u.sees( critter ) ) {
            return;
        }
        const auto [index, compass_index] = visible_info( critter.bub_pos() );
        mon_visible.visible_count_by_dir[compass_index]++;

        const auto matt = critter.attitude( &u );
        const auto player_attitude = player_attitude_from( matt );

        // Accumulate hostile counts for danger music and combat bubble.
        if( player_attitude == Attitude::A_HOSTILE ) {
            mon_visible.nearby_hostile_count++;
            if( mon_dist <= combat_bubble_range ) {
                mon_visible.combat_hostile_count++;
            }
        }

        //Safemode monster check
        const auto safemode_state = get_safemode().check_monster( critter.name(), player_attitude,
                                    mon_dist );

        if( ( !safemode_empty && safemode_state == RULE_BLACKLISTED ) || ( safemode_empty &&
                ( MATT_ATTACK == matt || MATT_FOLLOW == matt ) ) ) {
            if( index < 8 && critter.sees( g->u ) ) {
                dangerous[index] = true;
            }

            if( !safemode_empty || mon_dist <= iProxyDist ) {
                auto passmon = false;
                if( critter.ignoring > 0 ) {
                    if( safe_mode != SAFE_MODE_ON ) {
                        critter.ignoring = 0;
                    } else if( ( sm_ignored_time == 0_seconds || ( critter.lastseen_turn &&
                                 *critter.lastseen_turn > calendar::turn - sm_ignored_time ) ) &&
                               ( mon_dist > critter.ignoring / 2 || mon_dist < 6 ) ) {
                        passmon = true;
                    }
                    critter.lastseen_turn = calendar::turn;
                }

                if( !passmon ) {
                    newseen++;
                    new_seen_mon.push_back( mon_ptr );
                }
            }
        }

        auto &vec = unique_mons[index];
        const auto mon_it = std::find_if( vec.begin(), vec.end(),
        [&]( const std::pair<const mtype *, int> &elem ) {
            return elem.first == critter.type;
        } );
        if( mon_it == vec.end() ) {
            vec.emplace_back( critter.type, 1 );
        } else {
            mon_it->second++;
        }
    };

    const auto process_npc = [&]( const shared_ptr_fast<npc> &npc_ptr ) {
        if( !npc_ptr || npc_ptr->is_dead() ) {
            return;
        }
        npc &guy = *npc_ptr;
        const auto npc_dist = rl_dist( u.bub_pos(), guy.bub_pos() );
        if( u.bub_pos() == guy.bub_pos() || npc_dist > g_mapsize_x || !u.sees( guy ) ) {
            return;
        }
        const auto [index, compass_index] = visible_info( guy.bub_pos() );
        mon_visible.visible_count_by_dir[compass_index]++;

        // Accumulate hostile counts for danger music and combat bubble.
        if( u.attitude_to( guy ) == Attitude::A_HOSTILE ) {
            mon_visible.nearby_hostile_count++;
            if( npc_dist <= combat_bubble_range ) {
                mon_visible.combat_hostile_count++;
            }
        }

        //Safe mode NPC check
        const auto safemode_state = get_safemode().check_monster( get_safemode().npc_type_name(),
                                    guy.attitude_to( u ), npc_dist );

        if( ( !safemode_empty && safemode_state == RULE_BLACKLISTED ) || ( safemode_empty &&
                guy.get_attitude() == NPCATT_KILL ) ) {
            if( !safemode_empty || npc_dist <= iProxyDist ) {
                newseen++;
            }
        }
        unique_types[index].push_back( &guy );
    };

    for( const shared_ptr_fast<monster> &critter : critter_tracker->get_monsters_list() ) {
        process_monster( critter );
    }
    for( const shared_ptr_fast<npc> &guy : active_npc ) {
        process_npc( guy );
    }

    if( newseen > mostseen ) {
        if( newseen - mostseen == 1 ) {
            if( !new_seen_mon.empty() ) {
                monster &critter = *new_seen_mon.back();
                cancel_activity_or_ignore_query( distraction_type::hostile_spotted_far,
                                                 string_format( _( "%s spotted!" ), critter.name() ) );
                if( u.has_trait( trait_id( "M_DEFENDER" ) ) && critter.type->in_species( PLANT ) ) {
                    add_msg( m_warning, _( "We have detected a %s - an enemy of the Mycus!" ), critter.name() );
                    if( !u.has_effect( effect_adrenaline_mycus ) ) {
                        u.add_effect( effect_adrenaline_mycus, 30_minutes );
                    } else if( u.get_effect_int( effect_adrenaline_mycus ) == 1 ) {
                        // Triffids present.  We ain't got TIME to adrenaline comedown!
                        u.add_effect( effect_adrenaline_mycus, 15_minutes );
                        u.mod_pain( 3 ); // Does take it out of you, though
                        add_msg( m_info, _( "Our fibers strain with renewed wrath!" ) );
                    }
                }
            } else {
                //Hostile NPC
                cancel_activity_or_ignore_query( distraction_type::hostile_spotted_far,
                                                 _( "Hostile survivor spotted!" ) );
            }
        } else {
            cancel_activity_or_ignore_query( distraction_type::hostile_spotted_far, _( "Monsters spotted!" ) );
        }
        turnssincelastmon = 0;
        if( safe_mode == SAFE_MODE_ON ) {
            set_safe_mode( SAFE_MODE_STOP );
        }
    } else if( calendar::turn > previous_turn && get_option<bool>( "AUTOSAFEMODE" ) &&
               newseen == 0 ) { // Auto-safe mode, but only if it's a new turn
        turnssincelastmon += to_turns<int>( calendar::turn - previous_turn );
        if( turnssincelastmon >= get_option<int>( "AUTOSAFEMODETURNS" ) && safe_mode == SAFE_MODE_OFF ) {
            set_safe_mode( SAFE_MODE_ON );
            add_msg( m_info, _( "Safe mode ON!" ) );
        }
    }

    if( newseen == 0 && safe_mode == SAFE_MODE_STOP ) {
        set_safe_mode( SAFE_MODE_ON );
    }

    previous_turn = calendar::turn;
    mostseen = newseen;
}

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
#ifdef COOP_ENABLED
    if( !coop_session::get().is_client() ) {
#endif
        cata::run_hooks( "on_creature_spawn", [&]( sol::table & params ) {
            params["creature"] = temp.get();
        } );
        cata::run_hooks( "on_monster_spawn", [&]( sol::table & params ) {
            params["monster"] = temp.get();
        } );
#ifdef COOP_ENABLED
    }
#endif
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
bool game::spawn_hallucination( const tripoint_bub_ms &p )
{
    if( one_in( 100 ) ) {
        shared_ptr_fast<npc> tmp = make_shared_fast<npc>();
        tmp->randomize( NC_HALLU );
        const auto proj = project_remain<coords::sm>( bub_to_abs( p ) );
        tmp->spawn_at_precise( proj.quotient, proj.remainder_tripoint );
        cata::run_hooks( "on_creature_spawn", [&]( sol::table & params ) {
            params["creature"] = tmp.get();
        } );
        cata::run_hooks( "on_npc_spawn", [&]( sol::table & params ) {
            params["npc"] = tmp.get();
        } );
        if( !critter_at( p, true ) ) {
            get_overmapbuffer( current_dimension_id_ ).insert_npc( tmp );
            load_npcs();
            return true;
        } else {
            return false;
        }
    }

    const mtype_id &mt = MonsterGenerator::generator().get_valid_hallucination();
    const shared_ptr_fast<monster> phantasm = make_shared_fast<monster>( mt );
    phantasm->hallucination = true;
    phantasm->spawn( p );
    cata::run_hooks( "on_creature_spawn", [&]( sol::table & params ) {
        params["creature"] = phantasm.get();
    } );
    cata::run_hooks( "on_monster_spawn", [&]( sol::table & params ) {
        params["monster"] = phantasm.get();
    } );

    //Don't attempt to place phantasms inside of other creatures
    if( !critter_at( phantasm->bub_pos(), true ) ) {
        return critter_tracker->add( phantasm );
    } else {
        return false;
    }
}

bool game::swap_critters( Creature &a, Creature &b )
{
    if( &a == &b ) {
        // No need to do anything, but print a debugmsg anyway
        debugmsg( "Tried to swap %s with itself", a.disp_name() );
        return true;
    }
    if( critter_at( a.bub_pos() ) != &a ) {
        debugmsg( "Tried to swap when it would cause a collision between %s and %s.",
                  b.disp_name(), critter_at( a.bub_pos() )->disp_name() );
        return false;
    }
    if( critter_at( b.bub_pos() ) != &b ) {
        debugmsg( "Tried to swap when it would cause a collision between %s and %s.",
                  a.disp_name(), critter_at( b.bub_pos() )->disp_name() );
        return false;
    }
    // Simplify by "sorting" the arguments
    // Only the first argument can be u
    // If swapping player/npc with a monster, monster is second
    bool a_first = a.is_player() ||
                   ( a.is_npc() && !b.is_player() );
    Creature &first  = a_first ? a : b;
    Creature &second = a_first ? b : a;
    // Possible options:
    // both first and second are monsters
    // second is a monster, first is a player or an npc
    // first is a player, second is an npc
    // both first and second are npcs
    if( first.is_monster() ) {
        monster *m1 = dynamic_cast< monster * >( &first );
        monster *m2 = dynamic_cast< monster * >( &second );
        if( m1 == nullptr || m2 == nullptr || m1 == m2 ) {
            debugmsg( "Couldn't swap two monsters" );
            return false;
        }

        critter_tracker->swap_positions( *m1, *m2 );
        return true;
    }

    Character *u_or_npc = dynamic_cast< Character * >( &first );
    Character *other_npc = dynamic_cast< Character * >( &second );

    if( u_or_npc->in_vehicle ) {
        m.unboard_vehicle( u_or_npc->bub_pos() );
    }

    if( other_npc && other_npc->in_vehicle ) {
        m.unboard_vehicle( other_npc->bub_pos() );
    }

    auto temp = second.bub_pos();
    second.setpos( first.bub_pos() );

    if( first.is_player() ) {
        walk_move( temp );
    } else {
        first.setpos( temp );
        if( m.veh_at( u_or_npc->bub_pos() ).part_with_feature( VPFLAG_BOARDABLE, true ) ) {
            m.board_vehicle( u_or_npc->bub_pos(), u_or_npc );
        }
    }

    if( other_npc && m.veh_at( other_npc->bub_pos() ).part_with_feature( VPFLAG_BOARDABLE, true ) ) {
        m.board_vehicle( other_npc->bub_pos(), other_npc );
    }
    return true;
}

bool game::is_empty( const tripoint_bub_ms &p )
{
    auto &cur_map = get_map();
    return ( cur_map.passable( p ) || cur_map.has_flag( "LIQUID", p ) ) &&
           critter_at( p ) == nullptr;
}

bool game::is_in_sunlight( const tripoint_bub_ms &p )
{
    return weather::is_in_sunlight( m, p, get_weather().weather_id );
}

bool game::is_sheltered( const tripoint_bub_ms &p )
{
    return weather::is_sheltered( m, p );
}

bool game::revive_corpse( const tripoint_bub_ms &p, item &it )
{
    if( !it.is_corpse() ) {
        debugmsg( "Tried to revive a non-corpse." );
        return false;
    }
    // If this is not here, the game may attempt to spawn a monster before the map exists,
    // leading to it querying for furniture, and crashing.
    if( g->new_game || g->swapping_dimensions ) {
        return false;
    }
    shared_ptr_fast<monster> newmon_ptr = make_shared_fast<monster>
                                          ( it.get_mtype()->id );
    monster &critter = *newmon_ptr;
    critter.init_from_item( it );
    if( critter.get_hp() < 1 ) {
        // Failed reanimation due to corpse being too burned
        return false;
    }
    if( it.has_flag( flag_FIELD_DRESS ) || it.has_flag( flag_FIELD_DRESS_FAILED ) ||
        it.has_flag( flag_QUARTERED ) ) {
        // Failed reanimation due to corpse being butchered
        return false;
    }

    critter.no_extra_death_drops = true;
    critter.add_effect( effect_downed, 5_turns );
    if( critter.type->monster_weapon ) {
        critter.add_effect( effect_monster_disarmed, 1_turns );
    }
    for( detached_ptr<item> &component : it.remove_components() ) {
        critter.add_corpse_component( std::move( component ) );
    }

    if( it.get_var( "zlave" ) == "zlave" ) {
        critter.add_effect( effect_pacified, 1_turns );
        critter.add_effect( effect_pet, 1_turns );
    }

    if( it.get_var( "no_ammo" ) == "no_ammo" ) {
        for( auto &ammo : critter.ammo ) {
            ammo.second = 0;
        }
    }

    cata::run_hooks( "on_creature_spawn", [&]( sol::table & params ) {
        params["creature"] = &critter;
    } );
    cata::run_hooks( "on_monster_spawn", [&]( sol::table & params ) {
        params["monster"] = &critter;
    } );
    return place_critter_at( newmon_ptr, p );
}

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

bool game::forced_door_closing( const tripoint_bub_ms &p, const ter_id &door_type, int bash_dmg )
{
    const auto valid_location = [&]( const tripoint_bub_ms & p ) {
        return g->is_empty( p );
    };
    const auto get_random_point = [&]() -> tripoint_bub_ms {
        if( auto pos = random_point( m.points_in_radius( p, 2 ), valid_location ) )
        {
            return  tripoint_bub_ms( p.raw() * 2 - ( *pos ).raw() );
        } else
        {
            return p;
        }
    };

    const std::string &door_name = door_type.obj().name();
    const auto kbp = get_random_point();

    // can't pushback any creatures/items anywhere, that means the door can't close.
    const bool cannot_push = kbp == p;
    const bool can_see = u.sees( p );

    auto *npc_or_player = critter_at<Character>( p, false );
    if( npc_or_player != nullptr ) {
        if( bash_dmg <= 0 ) {
            return false;
        }
        if( npc_or_player->is_npc() && can_see ) {
            add_msg( _( "The %1$s hits the %2$s." ), door_name, npc_or_player->name );
        } else if( npc_or_player->is_player() ) {
            add_msg( m_bad, _( "The %s hits you." ), door_name );
        }
        if( npc_or_player->activity ) {
            npc_or_player->cancel_activity();
        }
        // TODO: make the npc angry?
        npc_or_player->hitall( bash_dmg, 0, nullptr );
        if( cannot_push ) {
            return false;
        }
        // TODO implement who was closing the door and replace nullptr
        knockback( kbp, p, std::max( 1, bash_dmg / 10 ), -1, 1, nullptr );
        // TODO: perhaps damage/destroy the gate
        // if the npc was really big?
    }
    if( monster *const mon_ptr = critter_at<monster>( p ) ) {
        monster &critter = *mon_ptr;
        if( bash_dmg <= 0 ) {
            return false;
        }
        if( can_see ) {
            add_msg( _( "The %1$s hits the %2$s." ), door_name, critter.name() );
        }
        if( critter.type->size <= creature_size::small ) {
            critter.die_in_explosion( nullptr );
        } else {
            critter.apply_damage( nullptr, bodypart_id( "torso" ), bash_dmg );
            critter.check_dead_state();
        }
        if( !critter.is_dead() && critter.type->size >= creature_size::huge ) {
            // big critters simply prevent the gate from closing
            // TODO: perhaps damage/destroy the gate
            // if the critter was really big?
            return false;
        }
        if( !critter.is_dead() ) {
            // Still alive? Move the critter away so the door can close
            if( cannot_push ) {
                return false;
            }
            // TODO implement who was closing the door and replace nullptr
            knockback( kbp, p, std::max( 1, bash_dmg / 10 ), -1, 1, nullptr );
            if( critter_at( p ) ) {
                return false;
            }
        }
    }
    if( const optional_vpart_position vp = m.veh_at( p ) ) {
        if( bash_dmg <= 0 ) {
            return false;
        }
        vp->vehicle().damage( vp->part_index(), bash_dmg );
        if( m.veh_at( p ) ) {
            // Check again in case all parts at the door tile
            // have been destroyed, if there is still a vehicle
            // there, the door can not be closed
            return false;
        }
    }
    if( bash_dmg < 0 && !m.i_at( p ).empty() ) {
        return false;
    }
    if( bash_dmg == 0 ) {
        for( auto &elem : m.i_at( p ) ) {
            if( elem->made_of( LIQUID ) ) {
                // Liquids are OK, will be destroyed later
                continue;
            } else if( elem->volume() < 250_ml ) {
                // Dito for small items, will be moved away
                continue;
            }
            // Everything else prevents the door from closing
            return false;
        }
    }

    m.ter_set( p, door_type );
    if( m.has_flag( "NOITEM", p ) ) {
        map_stack items = m.i_at( p );
        for( map_stack::iterator it = items.begin(); it != items.end(); ) {
            if( ( *it )->made_of( LIQUID ) ) {
                it = items.erase( it );
                continue;
            }
            if( ( ( *it )->can_shatter() ) && one_in( 2 ) ) {
                if( can_see ) {
                    add_msg( m_warning, _( "A %s shatters!" ), ( *it )->tname() );
                } else {
                    add_msg( m_warning, _( "Something shatters!" ) );
                }
                it = items.erase( it );
                continue;
            }
            if( cannot_push ) {
                return false;
            }
            detached_ptr<item> det;
            it = items.erase( it, &det );
            m.add_item_or_charges( kbp, std::move( det ) );
        }
    }
    return true;
}

void game::toggle_gate( const tripoint_bub_ms &p )
{
    gates::toggle_gate( p, u );
}

void game::moving_vehicle_dismount( const tripoint_bub_ms &dest_loc )
{
    const optional_vpart_position vp = m.veh_at( u.bub_pos() );
    if( !vp ) {
        debugmsg( "Tried to exit non-existent vehicle." );
        return;
    }
    vehicle *const veh = &vp->vehicle();
    if( u.bub_pos() == dest_loc ) {
        debugmsg( "Need somewhere to dismount towards." );
        return;
    }
    tileray ray( dest_loc.xy().reinterpret_as<point_rel_ms>() + point_rel_ms( -u.bub_pos().x(),
                 -u.bub_pos().y() ) );
    // TODO:: make dir() const correct!
    const units::angle d = ray.dir();
    add_msg( _( "You dive from the %s." ), veh->name );
    m.unboard_vehicle( u.bub_pos() );
    u.moves -= 200;
    // Dive three tiles in the direction of tox and toy
    fling_creature( &u, d, 30, true );
    // Hit the ground according to vehicle speed
    if( !m.has_flag( "SWIMMABLE", u.bub_pos() ) ) {
        if( veh->velocity > 0 ) {
            fling_creature( &u, veh->face.dir(), veh->velocity / static_cast<float>( 100 ) );
        } else {
            fling_creature( &u, veh->face.dir() + 180_degrees,
                            -( veh->velocity ) / static_cast<float>( 100 ) );
        }
    }
}



// Used to set up the first Hotkey in the display set

bool game::check_safe_mode_allowed( bool repeat_safe_mode_warnings )
{
    if( !repeat_safe_mode_warnings && safe_mode_warning_logged ) {
        // Already warned player since safe_mode_warning_logged is set.
        return false;
    }

    std::string msg_ignore = press_x( ACTION_IGNORE_ENEMY );
    if( !msg_ignore.empty() ) {
        std::wstring msg_ignore_wide = utf8_to_wstr( msg_ignore );
        // Operate on a wide-char basis to prevent corrupted multi-byte string
        msg_ignore_wide[0] = towlower( msg_ignore_wide[0] );
        msg_ignore = wstr_to_utf8( msg_ignore_wide );
    }

    if( u.has_effect( effect_laserlocked ) ) {
        // Automatic and mandatory safemode.  Make BLOODY sure the player notices!
        if( u.get_int_base() < 5 || u.has_trait( trait_id( "PROF_CHURL" ) ) ) {
            add_msg( game_message_params{ m_warning, gmf_bypass_cooldown },
                     _( "There's an angry red dot on your body, %s to brush it off." ), msg_ignore );
        } else {
            add_msg( game_message_params{ m_warning, gmf_bypass_cooldown },
                     _( "You are being laser-targeted, %s to ignore." ), msg_ignore );
        }
        safe_mode_warning_logged = true;
        return false;
    }
    if( safe_mode != SAFE_MODE_STOP ) {
        return true;
    }
    // Currently driving around, ignore the monster, they have no chance against a proper car anyway (-:
    if( u.controlling_vehicle && !get_option<bool>( "SAFEMODEVEH" ) ) {
        return true;
    }
    // Monsters around and we don't want to run
    std::string spotted_creature_name;
    const monster_visible_info &mon_visible = u.get_mon_visible();
    const auto &new_seen_mon = mon_visible.new_seen_mon;

    if( new_seen_mon.empty() ) {
        // naming consistent with code in game::mon_info
        spotted_creature_name = _( "a survivor" );
        get_safemode().lastmon_whitelist = get_safemode().npc_type_name();
    } else {
        spotted_creature_name = new_seen_mon.back()->name();
        get_safemode().lastmon_whitelist = spotted_creature_name;
    }

    std::string whitelist;
    if( !get_safemode().empty() ) {
        whitelist = string_format( _( " or %s to whitelist the monster" ),
                                   press_x( ACTION_WHITELIST_ENEMY ) );
    }

    const std::string msg_safe_mode = press_x( ACTION_TOGGLE_SAFEMODE );
    add_msg( game_message_params{ m_warning, gmf_bypass_cooldown },
             _( "Spotted %1$s--safe mode is on!  (%2$s to turn it off, %3$s to ignore monster%4$s)" ),
             spotted_creature_name, msg_safe_mode, msg_ignore, whitelist );
    safe_mode_warning_logged = true;
    return false;
}

void game::set_safe_mode( safe_mode_type mode )
{
    safe_mode = mode;
    safe_mode_warning_logged = false;
}

bool game::is_dangerous_tile( const tripoint_bub_ms &dest_loc ) const
{
    return !( get_dangerous_tile( dest_loc ).empty() );
}

bool game::prompt_dangerous_tile( const tripoint_bub_ms &dest_loc ) const
{
    static const iexamine_function ledge_examine = iexamine_function_from_string( "ledge" );
    std::vector<std::string> harmful_stuff = get_dangerous_tile( dest_loc );

    if( harmful_stuff.empty() || u.has_effect( dashing_effect ) ) {
        return true;
    }

    if( !( harmful_stuff.size() == 1 && m.tr_at( dest_loc ).loadid == tr_ledge ) ) {
        return query_yn( _( "Really step into %s?" ), enumerate_as_string( harmful_stuff ) ) ;
    }

    if( !u.is_mounted() ) {
        if( character_funcs::can_fly( get_avatar() ) ) {
            return true;
        }
        ledge_examine( u, dest_loc );
        return false;
    } else {
        auto crit = u.mounted_creature.get();
        if( crit->has_flag( MF_MOUNTABLE_LEDGE ) ) {
            return query_yn( _( "Really step into %s?" ),
                             enumerate_as_string( harmful_stuff ) ) ; // mount can climb down ledges
        }
    }


    add_msg( m_warning, _( "Your %s refuses to move over that ledge!" ),
             u.mounted_creature->get_name() );
    return false;
}

std::vector<std::string> game::get_dangerous_tile( const tripoint_bub_ms &dest_loc ) const
{
    std::vector<std::string> harmful_stuff;
    const auto fields_here = m.field_at( u.bub_pos() );
    for( const auto &e : m.field_at( dest_loc ) ) {
        // warn before moving into a dangerous field except when already standing within a similar field
        if( u.is_dangerous_field( e.second ) && fields_here.find_field( e.first ) == nullptr ) {
            harmful_stuff.push_back( e.second.name() );
        }
    }

    const trap &tr = m.tr_at( dest_loc );
    if( !u.is_blind() || u.clairvoyance() < 1 || tr.can_see( dest_loc, u ) ) {
        const bool boardable = static_cast<bool>( m.veh_at( dest_loc ).part_with_feature( "BOARDABLE",
                               true ) );
        // HACK: Hack for now, later ledge should stop being a trap
        // Note: in non-z-level mode, ledges obey different rules and so should be handled as regular traps
        if( tr.loadid == tr_ledge && m.has_zlevels() ) {
            if( !character_funcs::can_fly( get_avatar() ) ) {
                if( !boardable ) {
                    harmful_stuff.emplace_back( tr.name() );
                }
            }
        } else if( tr.can_see( dest_loc, u ) && !tr.is_benign() && !boardable ) {
            harmful_stuff.emplace_back( tr.name() );
        }

        static const std::set< body_part > sharp_bps = {
            bp_eyes, bp_mouth, bp_head, bp_leg_l, bp_leg_r, bp_foot_l, bp_foot_r, bp_arm_l, bp_arm_r,
            bp_hand_l, bp_hand_r, bp_torso
        };

        const auto sharp_bp_check = [this]( body_part bp ) {
            return character_funcs::is_bp_immune_to( u, bp, { DT_CUT, 10 } );
        };

        if( m.has_flag( "ROUGH", dest_loc ) && !m.has_flag( "ROUGH", u.bub_pos() ) && !boardable &&
            ( u.get_armor_bash( bodypart_id( "foot_l" ) ) < 5 ||
              u.get_armor_bash( bodypart_id( "foot_r" ) ) < 5 ) ) {
            harmful_stuff.emplace_back( m.name( dest_loc ) );
        } else if( m.has_flag( "SHARP", dest_loc ) && !m.has_flag( "SHARP", u.bub_pos() ) &&
                   !( u.in_vehicle ||
                      m.veh_at( dest_loc ) ) &&
                   u.dex_cur < 78 && !std::all_of( sharp_bps.begin(), sharp_bps.end(), sharp_bp_check ) ) {
            harmful_stuff.emplace_back( m.name( dest_loc ) );
        }

    }

    return harmful_stuff;
}

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
    load_map( map_sm_pos );
    // update weather now as it could be different on the new location
    get_weather().nextweather = calendar::turn;
    place_player( player_pos );
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


    if( tilecontext ) {
        tilecontext->reset_minimap();
    }
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

void game::on_options_changed()
{
    tilecontext->on_options_changed();
    // Only rebuild distribution grids when an actual game world is loaded.
    // grid_trackers_ hold stale tracked_submaps_ after quitting to the main
    // menu, which would cause make_distribution_grid_at() to dereference a
    // null world::info via overmapbuffer when trying to reload overmap data.
    if( !world_generator || !world_generator->active_world ) {
        return;
    }
    for( auto &[dim_id, tracker_ptr] : grid_trackers_ ) {
        if( tracker_ptr ) {
            tracker_ptr->on_options_changed();
        }
    }
    if( get_option<int>( "REALITY_BUBBLE_SIZE" ) != g_reality_bubble_size ) {
        resize_reality_bubble();
    }
}

const dimension_info *game::get_current_dimension_info() const
{
    auto it = loaded_dimensions_.find( current_dimension_id_ );
    return it != loaded_dimensions_.end() ? &it->second : nullptr;
}

std::string game::get_dimension_prefix() const
{
    return current_dimension_id_;
}

void game::set_active_dimension_id( const std::string &dim_id )
{
    current_dimension_id_ = dim_id;
    g_active_dimension_id = dim_id;
}

void game::activate_dimension_state( const std::string &new_dim_id,
                                     const std::string &old_dim_id )
{
    // Step 1: drain ALL in-flight background work before touching any shared state.
    // Workers capture dimension IDs by value at submission time, so it is safe to
    // drain while the global still names the old dimension.
    submap_loader.drain_lazy_loads();

    // Step 2: release load handles — must happen before bind_dimension() (see caller
    // comment at the bind_dimension() call site for the ordering rationale).
    if( reality_bubble_handle_ != 0 ) {
        submap_loader.release_load( reality_bubble_handle_ );
        reality_bubble_handle_ = 0;
    }
    if( lazy_border_handle_ != 0 ) {
        submap_loader.release_load( lazy_border_handle_ );
        lazy_border_handle_ = 0;
    }

    // Step 3: flush the stale desired set.  Asserts fully drained (lazy + presave).
    submap_loader.flush_prev_desired();

    // Step 4: update both dimension-ID fields atomically.
    set_active_dimension_id( new_dim_id );

    // Step 5: clear the old dimension's distribution-grid tracker.  Safe now that
    // all workers have finished — no on_submap_loaded() callback can fire for
    // old_dim_id after drain_lazy_loads() returns.
    if( grid_trackers_.count( old_dim_id ) ) {
        grid_trackers_[old_dim_id]->clear();
    }
}

bool game::travel_to_dimension( const std::string &dim_id,
                                const world_type_id &world_type,
                                const std::optional<pocket_dimension_data> &pd_info,
                                const std::optional<tripoint_abs_sm> &load_pos,
                                const std::function<void()> &pre_load_callback )
{
    // Flush any items pending deferred deletion before switching dimensions.
    // Without this, zombie item pointers in cata_arena can persist across the
    // dimension transition and cause use-after-free crashes when the new
    // dimension's submaps are actualized (e.g. in remove_rotten_items).
    cleanup_arenas();

    if( dim_id == current_dimension_id_ ) {
        add_msg( m_debug, "[DIM] Already in dimension '%s', no-op", dim_id );
        return true;
    }

    // Resolve effective world_type: use the passed value if valid; otherwise try
    // to look it up from already-loaded dimensions.  The overworld (dim_id == "")
    // is a special case — it has no explicit world_type and that is fine.
    auto effective_wt = world_type;
    if( !effective_wt.is_valid() ) {
        if( auto it = loaded_dimensions_.find( dim_id ); it != loaded_dimensions_.end() ) {
            effective_wt = it->second.world_type;
        }
    }
    if( !effective_wt.is_valid() && !dim_id.empty() ) {
        debugmsg( "travel_to_dimension: cannot resolve world_type for unknown dim '%s'", dim_id );
        return false;
    }

    // For the overworld, effective_wt may still be null; guard all uses below.
    const struct world_type *target_type = effective_wt.is_valid() ? &effective_wt.obj() : nullptr;
    map &here = get_map();
    avatar &player = get_avatar();

    // Each dimension lives in its own MAPBUFFER_REGISTRY slot permanently.
    // There is no "primary slot swap" — we save the current slot, rebind the map
    // to the target slot, and load.  kept_pocket_dimension_id_ only marks which
    // bounded pocket to avoid evicting when memory pressure calls for cleanup.

    // Snapshot the old dimension state before any mutation.
    const std::string old_dim_id = here.get_bound_dimension();
    const tripoint_abs_sm current_abs_sm( here.get_abs_sub() );

    {
        ZoneScopedN( "travel_unload" );
        unload_npcs();
        for( monster &critter : all_monsters() ) {
            despawn_monster( critter );
        }
        if( player.in_vehicle ) {
            here.unboard_vehicle( player.bub_pos() );
        }

        world *active_world = get_active_world();
        try {
            if( active_world ) {
                active_world->start_save_tx();
            }
            get_overmapbuffer( current_dimension_id_ ).save( current_dimension_id_ );
            MAPBUFFER_REGISTRY.get( old_dim_id ).save();
            if( !save_dimension_data() ) {
                if( active_world ) {
                    active_world->commit_save_tx();
                }
                return false;
            }
            if( active_world ) {
                active_world->commit_save_tx();
            }
        } catch( const std::exception &err ) {
            popup( _( "Failed to save map data: %s" ), err.what() );
            return false;
        }
    }

    add_msg( m_debug, "[DIM] Saved dimension '%s' before leaving", old_dim_id );

    player.save_map_memory();

    for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; z++ ) {
        here.clear_vehicle_list( z );
    }
    here.reset_vehicle_cache();

    // Update kept_pocket_dimension_id_: marks which bounded pocket to preserve
    // against memory-pressure eviction; cleared when entering a new pocket.
    {
        const bool old_is_bounded = !old_dim_id.empty() &&
                                    loaded_dimensions_.count( old_dim_id ) &&
                                    loaded_dimensions_.at( old_dim_id ).pocket_info.has_value();
        if( old_is_bounded && !pd_info.has_value() ) {
            // Exiting a bounded pocket → remember it.
            kept_pocket_dimension_id_ = old_dim_id;
            add_msg( m_debug, "[DIM] Marking pocket '%s' as kept", old_dim_id );
        } else if( pd_info.has_value() ) {
            // Entering any pocket → forget the previous kept marker.
            kept_pocket_dimension_id_.clear();
        }
    }

    assert( !swapping_dimensions );
    // Prevent temperature/weather code from accessing the grid while it's
    // being cleared and rebuilt.  Must be set BEFORE activate_dimension_state().
    swapping_dimensions = true;

    // Sequenced critical section: drain → release handles → flush → update ID → clear tracker.
    // load_map() detects dimension changes by comparing get_dimension_prefix() vs
    // m.get_bound_dimension(); activate_dimension_state() must complete before bind_dimension()
    // so that load_map() sees the new ID and correctly re-issues load requests.
    activate_dimension_state( dim_id, old_dim_id );
    {
        auto it = loaded_dimensions_.find( dim_id );
        if( it != loaded_dimensions_.end() ) {
            calendar::set_active_world_type( it->second.world_type.str() );
        }
    }

    add_msg( m_debug, "[DIM] Switched active dimension: '%s' → '%s'", old_dim_id, dim_id );

    // bind_dimension() redirects all subsequent loadn() / generation calls to
    // the target MAPBUFFER_REGISTRY slot.  Submaps for old_dim_id stay in their
    // slot — nothing is lost.
    here.bind_dimension( dim_id );
    // Flush the destination dimension's cached overmaps so they are freshly
    // loaded from disk on demand.  current_dimension_id_ is already the new dim
    // (set in activate_dimension_state()), so get_overmapbuffer(current_dimension_id_)
    // correctly targets the new dimension's buffer.
    get_overmapbuffer( current_dimension_id_ ).clear();
    here.clear_grid();

    if( !loaded_dimensions_.count( dim_id ) ) {
        loaded_dimensions_[dim_id] = dimension_info{
            .dimension_id        = dim_id,
            .world_type          = effective_wt,
            .display_name        = target_type ? target_type->name.translated() : dim_id,
            .pocket_info         = pd_info
        };
    }

    if( target_type && !target_type->region_settings_id.empty() ) {
        get_overmapbuffer( current_dimension_id_ ).current_region_type = target_type->region_settings_id;
    } else if( !target_type ) {
        // Overworld: region type is preserved from initial game setup.
    } else {
        debugmsg( "travel_to_dimension: world_type '%s' has empty region_settings_id!",
                  effective_wt.str() );
    }

    // Load saved dimension-specific data (weather, etc.).  This may override
    // current_region_type if a dimension_data file already exists.
    load_dimension_data();

    // Clear stale bounds then install the new ones before load_map() so that
    // loadn() knows which submaps are out-of-bounds for bounded dimensions.
    here.clear_pocket_info();
    get_overmapbuffer( current_dimension_id_ ).clear_pocket_info();
    if( pd_info ) {
        here.set_pocket_info( *pd_info );
        get_overmapbuffer( current_dimension_id_ ).set_pocket_info( *pd_info );
    }

    // Invoke pre-load callback (e.g. place overmap specials) before loading submaps
    // so that submap generation uses the correct overmap terrain types.
    if( pre_load_callback ) {
        pre_load_callback();
    }

    {
        ZoneScopedN( "travel_load" );
        // Load at the destination position if provided; fall back to the old position.
        // Loading at the destination avoids a costly incremental map shift in update_map()
        // when the destination is far from the current position.
        load_map( load_pos.value_or( current_abs_sm ), false );

        add_msg( m_debug, "[DIM] Loaded new dimension '%s' map", dim_id );

        player.clear_map_memory();
        player.load_map_memory();

        {
            auto const zmin = here.has_zlevels() ? -OVERMAP_DEPTH : here.get_abs_sub().z();
            auto const zmax = here.has_zlevels() ? OVERMAP_HEIGHT : here.get_abs_sub().z();
            for( auto z = zmin; z <= zmax; z++ ) {
                here.access_cache( z ).map_memory_seen_cache.reset();
                here.invalidate_map_cache( z );
            }
        }
        here.build_map_cache( here.get_abs_sub().z() );

        load_npcs();
        here.spawn_monsters( true );

        get_weather().weather_override = weather_type_id::NULL_ID();
        get_weather().set_nextweather( calendar::turn );

        update_overmap_seen();
    }

    if( !save_dimension_data() ) {
        debugmsg( "Failed to save dimension data after dimension travel" );
    }

    return true;
}

void game::start_hauling( const tripoint_bub_ms &pos )
{
    // Find target items and quantities thereof for the new activity
    std::vector<item *> target_items;
    std::vector<int> quantities;

    map_stack items = m.i_at( pos );
    for( item * const &it : items ) {
        // Liquid cannot be picked up
        if( it->made_of( LIQUID ) ) {
            continue;
        }
        target_items.emplace_back( it );
        // Quantity of 0 means move all
        quantities.push_back( 0 );
    }

    if( target_items.empty() ) {
        // Nothing to haul
        u.stop_hauling();
        return;
    }

    // Whether the destination is inside a vehicle (not supported)
    const bool to_vehicle = false;
    // Destination relative to the player
    const tripoint_rel_ms relative_destination{};

    u.assign_activity( std::make_unique<player_activity>( std::make_unique<move_items_activity_actor>(
                           target_items,
                           quantities,
                           to_vehicle,
                           relative_destination
                       ) ) );
}

point_rel_sm game::update_map( Character &who )
{
    int x = who.bub_pos().x();
    int y = who.bub_pos().y();
    return update_map( x, y );
}

point_rel_sm game::update_map( int &x, int &y )
{
    point_rel_sm shift;

    while( x < g_half_mapsize_x ) {
        x += SEEX;
        shift.x()--;
    }
    while( x >= g_half_mapsize_x + SEEX ) {
        x -= SEEX;
        shift.x()++;
    }
    while( y < g_half_mapsize_y ) {
        y += SEEY;
        shift.y()--;
    }
    while( y >= g_half_mapsize_y + SEEY ) {
        y -= SEEY;
        shift.y()++;
    }

    if( shift == point_rel_sm::zero() ) {
        // adjust player position
        u.setpos( tripoint_bub_ms( x, y, get_levz() ) );
        // Update what parts of the world map we can see
        // We need this call because even if the map hasn't shifted we may have changed z-level and can now see farther
        // TODO: only make this call if we changed z-level
        update_overmap_seen();
        // Not actually shifting the submaps, all the stuff below would do nothing
        return point_rel_sm::zero();
    }

    // Sim-stall attribution: a submap shift fires the heavy cache/mapgen work
    // between frames — the prime suspect for the walking "hitch". Time each
    // sub-step; logged once per shift. Diagnostic — remove once pinned.
    using _shclk = std::chrono::steady_clock;
    const _shclk::time_point _sh_t0 = _shclk::now();
    _shclk::time_point _sh_tp = _sh_t0;
    double _sh_shift = 0, _sh_loader = 0, _sh_ent = 0, _sh_npc = 0,
           _sh_cache = 0, _sh_spawn = 0, _sh_om = 0;
    auto _sh_lap = [&]( double &acc ) {
        const _shclk::time_point now = _shclk::now();
        acc = std::chrono::duration<double, std::milli>( now - _sh_tp ).count();
        _sh_tp = now;
    };

    // this handles loading/unloading submaps that have scrolled on or off the viewport
    // NOLINTNEXTLINE(cata-use-named-point-constants)
    inclusive_rectangle<point_rel_sm> size_1( point_rel_sm( -1, -1 ), point_rel_sm( 1, 1 ) );
    auto remaining_shift = shift;
    while( remaining_shift != point_rel_sm::zero() ) {
        auto this_shift = clamp( remaining_shift, size_1 );
        m.shift( this_shift );
        remaining_shift -= this_shift;
    }
    _sh_lap( _sh_shift );

    // Keep the reality bubble request center in sync with the shifted map.
    // Distribution-grid tracker updates are fully incremental via
    // on_submap_loaded/unloaded; the old full-rebuild has been removed.
    if( reality_bubble_handle_ != 0 ) {
        const auto &origin = m.get_abs_sub();
        const tripoint_abs_sm new_center(
            origin.x() + reality_bubble_radius_, origin.y() + reality_bubble_radius_, origin.z() );
        submap_loader.update_request( reality_bubble_handle_, new_center );
        // Dynamically manage lazy border based on cached option.
        if( lazy_border_enabled ) {
            if( lazy_border_handle_ == 0 ) {
                lazy_border_handle_ = submap_loader.request_load(
                                          load_request_source::lazy_border,
                                          m.get_bound_dimension(), new_center,
                                          reality_bubble_radius_ );
            } else {
                submap_loader.update_request( lazy_border_handle_, new_center );
            }
        } else if( lazy_border_handle_ != 0 ) {
            submap_loader.release_load( lazy_border_handle_ );
            lazy_border_handle_ = 0;
        }
        // Ensure trackers exist for all active dimensions before firing events.
        for( const auto &dim_id : submap_loader.active_dimensions() ) {
            ensure_distribution_grid_tracker_for( dim_id );
        }
        submap_loader.update_lazy_border_focus( m.get_bound_dimension(), u.abs_pos() );
        submap_loader.update();
        // Destroy trackers for non-primary dimensions with no remaining tracked submaps.
        for( auto it = grid_trackers_.begin(); it != grid_trackers_.end(); ) {
            if( !it->first.empty() && !it->second->has_tracked_submaps() ) {
                submap_loader.remove_listener( it->second.get() );
                it = grid_trackers_.erase( it );
            } else {
                ++it;
            }
        }
    }
    _sh_lap( _sh_loader );

    // Shift monsters
    shift_monsters( tripoint_rel_sm( shift, 0 ) );
    const auto shift_ms = project_to<coords::ms>( shift );
    u.shift_destination( -shift_ms );

    // Shift NPCs
    // Allow 2 submaps of slop beyond the grid edge so fast-moving NPCs that
    // crossed the boundary this tick are not immediately despawned.
    static constexpr int npc_despawn_margin_sm = 2;
    for( auto it = active_npc.begin(); it != active_npc.end(); ) {
        ( *it )->shift( shift );
        if( ( *it )->bub_pos().x() < 0 - SEEX * npc_despawn_margin_sm ||
            ( *it )->bub_pos().y() < 0 - SEEY * npc_despawn_margin_sm ||
            ( *it )->bub_pos().x() > SEEX * ( g_mapsize + npc_despawn_margin_sm ) ||
            ( *it )->bub_pos().y() > SEEY * ( g_mapsize + npc_despawn_margin_sm ) ) {
            //Remove the npc from the active list. It remains in the overmap list.
            ( *it )->on_unload();
            it = active_npc.erase( it );
        } else {
            it++;
        }
    }

    _sh_lap( _sh_ent );

    // scent.shift() removed — scent values live on per-submap arrays,
    // which move with the submap grid automatically on scroll.

    // Also ensure the player is on current z-level
    // get_levz() should later be removed, when there is no longer such a thing
    // as "current z-level"
    u.setpos( tripoint_bub_ms( x, y, get_levz() ) );

    // Only do the loading after all coordinates have been shifted.

    // Check for overmap saved npcs that should now come into view.
    // Put those in the active list.
    load_npcs();
    _sh_lap( _sh_npc );

    m.build_map_cache( get_levz() );
    _sh_lap( _sh_cache );

    // Spawn monsters only in the strip of submaps that just entered the bubble
    // to avoid duplicating already-active monsters from stale monster_map entries.
    m.spawn_monsters_new_submaps( shift ); // Static monsters
    _sh_lap( _sh_spawn );

    // Update what parts of the world map we can see
    update_overmap_seen();
    _sh_lap( _sh_om );

    const double _sh_total = std::chrono::duration<double, std::milli>(
                                 _shclk::now() - _sh_t0 ).count();
    DebugLogFL( DL::Info, DC::Main )
            << "[shift][perf] total=" << _sh_total << "ms shift=" << _sh_shift
            << " loader=" << _sh_loader << " ent=" << _sh_ent << " npc=" << _sh_npc
            << " cache=" << _sh_cache << " spawn=" << _sh_spawn << " om_seen=" << _sh_om;

    return shift;
}

bool game::display_overlay_state( const action_id action )
{
    return displaying_overlays && *displaying_overlays == action;
}

void game::display_toggle_overlay( const action_id action )
{
    if( display_overlay_state( action ) ) {
        displaying_overlays.reset();
    } else {
        displaying_overlays = action;
    }
}

void game::display_scent()
{
    display_toggle_overlay( ACTION_DISPLAY_SCENT );
}

void game::display_temperature()
{
    display_toggle_overlay( ACTION_DISPLAY_TEMPERATURE );
}

void game::display_vehicle_ai()
{
    display_toggle_overlay( ACTION_DISPLAY_VEHICLE_AI );
}

void game::display_visibility()
{
    display_toggle_overlay( ACTION_DISPLAY_VISIBILITY );
    if( display_overlay_state( ACTION_DISPLAY_VISIBILITY ) ) {
        std::vector<tripoint_bub_ms> locations;
        uilist creature_menu;
        int num_creatures = 0;
        creature_menu.addentry( num_creatures++, true, MENU_AUTOASSIGN, "%s", _( "You" ) );
        locations.emplace_back( g->u.bub_pos() ); // add player first.
        for( const Creature &critter : g->all_creatures() ) {
            if( critter.is_player() ) {
                continue;
            }
            creature_menu.addentry( num_creatures++, true, MENU_AUTOASSIGN, critter.disp_name() );
            locations.emplace_back( critter.bub_pos() );
        }

        pointmenu_cb callback( locations );
        creature_menu.callback = &callback;
        creature_menu.w_y_setup = 0;
        creature_menu.query();
        if( creature_menu.ret >= 0 && static_cast<size_t>( creature_menu.ret ) < locations.size() ) {
            Creature *creature = critter_at<Creature>( locations[creature_menu.ret] );
            displaying_visibility_creature = creature;
        }
    } else {
        displaying_visibility_creature = nullptr;
    }
}

void game::toggle_debug_hour_timer()
{
    debug_hour_timer.toggle();
}

void game::debug_hour_timer::toggle()
{
    enabled = !enabled;
    start_time = std::nullopt;
    add_msg( string_format( "debug timer %s", enabled ? "enabled" : "disabled" ) );
}

void game::toggle_debug_fps()
{
    g_show_fps = !g_show_fps;
    add_msg( string_format( "FPS counter %s", g_show_fps ? "enabled" : "disabled" ) );
}

#ifdef BOX2D_ENABLED
void game::toggle_box2d_debug_draw()
{
    if( auto *pw = m.get_physics_world() ) {
        const auto enabled = pw->toggle_debug_draw();
        add_msg( string_format( "Box2D debug overlay %s", enabled ? "enabled" : "disabled" ) );
    } else {
        add_msg( "Box2D not active (no physics world)" );
    }
}
#endif

void game::debug_hour_timer::print_time()
{
    if( enabled ) {
        if( calendar::once_every( time_duration::from_hours( 1 ) ) ) {
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

void game::display_lighting()
{
    display_toggle_overlay( ACTION_DISPLAY_LIGHTING );
    if( !g->display_overlay_state( ACTION_DISPLAY_LIGHTING ) ) {
        return;
    }
    uilist lighting_menu;
    std::vector<std::string> lighting_menu_strings{
        "Global lighting conditions"
    };

    int count = 0;
    for( const auto &menu_str : lighting_menu_strings ) {
        lighting_menu.addentry( count++, true, MENU_AUTOASSIGN, "%s", menu_str );
    }

    lighting_menu.w_y_setup = 0;
    lighting_menu.query();
    if( ( lighting_menu.ret >= 0 ) &&
        ( static_cast<size_t>( lighting_menu.ret ) < lighting_menu_strings.size() ) ) {
        g->displaying_lighting_condition = lighting_menu.ret;
    }
}

void game::display_radiation()
{
    display_toggle_overlay( ACTION_DISPLAY_RADIATION );
}

void game::display_transparency()
{
    display_toggle_overlay( ACTION_DISPLAY_TRANSPARENCY );
}

void game::display_outside()
{
    display_toggle_overlay( ACTION_DISPLAY_OUTSIDE );
}

void game::display_sound()
{
    display_toggle_overlay( ACTION_DISPLAY_SOUND );
}

void game::display_tiles_no_vfx()
{
    display_toggle_overlay( ACTION_DISPLAY_TILES_NO_VFX );
}

void game::init_autosave()
{
    moves_since_last_save = 0;
    last_save_timestamp = time( nullptr );
}

void game::quicksave()
{
    //Don't autosave if the player hasn't done anything since the last autosave/quicksave,
    if( !moves_since_last_save ) {
        return;
    }
    add_msg( m_info, _( "Saving game, this may take a while" ) );

    static_popup popup;
    popup.message( "%s", _( "Saving game, this may take a while" ) );
    ui_manager::redraw();
    refresh_display();

    time_t now = time( nullptr ); //timestamp for start of saving procedure

    //perform save
    save( false );
    //Now reset counters for autosaving, so we don't immediately autosave after a quicksave or autosave.
    moves_since_last_save = 0;
    last_save_timestamp = now;
}

void game::quickload()
{
    world *active_world = get_active_world();
    if( active_world == nullptr ) {
        return;
    }

    if( active_world->info->save_exists( save_t::from_save_id( u.get_save_id() ) ) ) {
        // Clear all registered dimension slots before reloading (see the same pattern in
        // game::unload_ui_state() for rationale — multiple dimensions may be active).
        MAPBUFFER_REGISTRY.for_each( []( const std::string &, mapbuffer & buf ) {
            buf.clear();
        } );
        get_overmapbuffer( current_dimension_id_ ).clear();
        try {
            // Doesn't need to load mod files again for the same world
            setup( false );
        } catch( const std::exception &err ) {
            debugmsg( "Error: %s", err.what() );
        }
        load( save_t::from_save_id( u.get_save_id() ) );
    } else {
        popup_getkey( _( "No saves for current character yet." ) );
    }
}

void game::autosave()
{
    //Don't autosave if the min-autosave interval has not passed since the last autosave/quicksave.
    if( time( nullptr ) < last_save_timestamp + 60 * get_option<int>( "AUTOSAVE_MINUTES" ) ) {
        return;
    }
    quicksave();    //Driving checks are handled by quicksave()
}

void game::process_artifact( item &it, Character &who )
{
    const bool worn = who.is_worn( it );
    const bool wielded = who.is_wielding( it );
    std::vector<art_effect_passive> effects = it.type->artifact->effects_carried;
    if( worn ) {
        const std::vector<art_effect_passive> &ew = it.type->artifact->effects_worn;
        effects.insert( effects.end(), ew.begin(), ew.end() );
    }
    if( wielded ) {
        const std::vector<art_effect_passive> &ew = it.type->artifact->effects_wielded;
        effects.insert( effects.end(), ew.begin(), ew.end() );
    }

    if( it.is_tool() ) {
        // Recharge it if necessary
        if( it.ammo_remaining() < it.ammo_capacity() && calendar::once_every( 1_minutes ) ) {
            //Before incrementing charge, check that any extra requirements are met
            if( check_art_charge_req( it ) ) {
                switch( it.type->artifact->charge_type ) {
                    case ARTC_NULL:
                    case NUM_ARTCS:
                        break; // dummy entries
                    case ARTC_TIME:
                        // Once per hour
                        if( calendar::once_every( 1_hours ) ) {
                            it.charges++;
                        }
                        break;
                    case ARTC_SOLAR:
                        if( calendar::once_every( 10_minutes ) &&
                            is_in_sunlight( who.bub_pos() ) ) {
                            it.charges++;
                        }
                        break;
                    // Artifacts can inflict pain even on Deadened folks.
                    // Some weird Lovecraftian thing.  ;P
                    // (So DON'T route them through mod_pain!)
                    case ARTC_PAIN:
                        if( calendar::once_every( 1_minutes ) ) {
                            add_msg( m_bad, _( "You suddenly feel sharp pain for no reason." ) );
                            who.mod_pain_noresist( 3 * rng( 1, 3 ) );
                            it.charges++;
                        }
                        break;
                    case ARTC_HP:
                        if( calendar::once_every( 1_minutes ) ) {
                            add_msg( m_bad, _( "You feel your body decaying." ) );
                            who.hurtall( 1, nullptr );
                            it.charges++;
                        }
                        break;
                    case ARTC_FATIGUE:
                        if( calendar::once_every( 1_minutes ) ) {
                            add_msg( m_bad, _( "You feel fatigue seeping into your body." ) );
                            u.mod_fatigue( 3 * rng( 1, 3 ) );
                            u.mod_stamina( -90 * rng( 1, 3 ) * rng( 1, 3 ) * rng( 2, 3 ), false );
                            it.charges++;
                        }
                        break;
                    // Portals are energetic enough to charge the item.
                    // Tears in reality are consumed too, but can't charge it.
                    case ARTC_PORTAL:
                        for( const tripoint_bub_ms &dest : m.points_in_radius( who.bub_pos(), 1 ) ) {
                            m.remove_field( dest, fd_fatigue );
                            if( m.tr_at( dest ).loadid == tr_portal ) {
                                add_msg( m_good, _( "The portal collapses!" ) );
                                m.remove_trap( dest );
                                it.charges++;
                                break;
                            }
                        }
                        break;
                }
            }
        }
    }

    for( const art_effect_passive &i : effects ) {
        switch( i ) {
            case AEP_STR_UP:
                who.mod_str_bonus( +4 );
                break;
            case AEP_DEX_UP:
                who.mod_dex_bonus( +4 );
                break;
            case AEP_PER_UP:
                who.mod_per_bonus( +4 );
                break;
            case AEP_INT_UP:
                who.mod_int_bonus( +4 );
                break;
            case AEP_ALL_UP:
                who.mod_str_bonus( +2 );
                who.mod_dex_bonus( +2 );
                who.mod_per_bonus( +2 );
                who.mod_int_bonus( +2 );
                break;
            case AEP_SPEED_UP:
                // Handled in player::current_speed()
                break;

            case AEP_PBLUE:
                if( who.get_rad() > 0 ) {
                    who.mod_rad( -1 );
                }
                break;

            case AEP_SMOKE:
                if( one_in( 10 ) ) {
                    tripoint_bub_ms pt( who.bub_pos().x() + rng( -1, 1 ),
                                        who.bub_pos().y() + rng( -1, 1 ),
                                        who.bub_pos().z() );
                    m.add_field( pt, fd_smoke, rng( 1, 3 ) );
                }
                break;

            case AEP_SNAKES:
                break; // Handled in player::hit()

            case AEP_EXTINGUISH:
                for( const tripoint_bub_ms &dest : m.points_in_radius( who.bub_pos(), 1 ) ) {
                    m.mod_field_age( dest, fd_fire, -1_turns );
                }
                break;

            case AEP_FUN:
                //Bonus fluctuates, wavering between 0 and 30-ish - usually around 12
                who.add_morale( MORALE_FEELING_GOOD, rng( 1, 2 ) * rng( 2, 3 ), 0, 3_turns, 0_turns, false );
                break;

            case AEP_HUNGER:
                if( one_in( 100 ) ) {
                    who.mod_stored_kcal( -10 );
                }
                break;

            case AEP_THIRST:
                if( one_in( 120 ) ) {
                    who.mod_thirst( 1 );
                }
                break;

            case AEP_EVIL:
                if( one_in( 150 ) ) { // Once every 15 minutes, on average
                    who.add_effect( effect_evil, 30_minutes );
                    if( it.is_armor() ) {
                        if( !worn ) {
                            add_msg( _( "You have an urge to wear the %s." ),
                                     it.tname() );
                        }
                    } else if( !wielded ) {
                        add_msg( _( "You have an urge to wield the %s." ),
                                 it.tname() );
                    }
                }
                break;

            case AEP_SCHIZO:
                break; // Handled in player::suffer()

            case AEP_RADIOACTIVE:
                if( one_in( 4 ) ) {
                    who.irradiate( 1.0f );
                }
                break;

            case AEP_STR_DOWN:
                who.mod_str_bonus( -3 );
                break;

            case AEP_DEX_DOWN:
                who.mod_dex_bonus( -3 );
                break;

            case AEP_PER_DOWN:
                who.mod_per_bonus( -3 );
                break;

            case AEP_INT_DOWN:
                who.mod_int_bonus( -3 );
                break;

            case AEP_ALL_DOWN:
                who.mod_str_bonus( -2 );
                who.mod_dex_bonus( -2 );
                who.mod_per_bonus( -2 );
                who.mod_int_bonus( -2 );
                break;

            case AEP_SPEED_DOWN:
                break; // Handled in player::current_speed()

            default:
                //Suppress warnings
                break;
        }
    }
    // Recalculate, as it might have changed (by mod_*_bonus above)
    who.str_cur = who.get_str();
    who.int_cur = who.get_int();
    who.dex_cur = who.get_dex();
    who.per_cur = who.get_per();
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
                sum_conditions( calendar::turn - 1_turns, calendar::turn, p.abs_pos() ).rain_amount > 0
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

void game::start_calendar()
{
    const bool scen_season = scen->has_flag( "SPR_START" ) || scen->has_flag( "SUM_START" ) ||
                             scen->has_flag( "AUT_START" ) || scen->has_flag( "WIN_START" ) ||
                             scen->has_flag( "SUM_ADV_START" );

    calendar_config &calendar_config = calendar::config;
    if( scen_season ) {
        // Configured starting date overridden by scenario, calendar_config.start is left as Spring 1
        calendar_config._start_of_cataclysm = calendar::turn_zero + 1_hours *
                                              get_option<int>( "INITIAL_TIME" );
        calendar_config._start_of_game = calendar::turn_zero + 1_hours * get_option<int>( "INITIAL_TIME" );
        if( scen->has_flag( "SPR_START" ) ) {
            calendar_config._initial_season = SPRING;
        } else if( scen->has_flag( "SUM_START" ) ) {
            calendar_config._initial_season = SUMMER;
            calendar_config._start_of_game += calendar_config.season_length();
        } else if( scen->has_flag( "AUT_START" ) ) {
            calendar_config._initial_season = AUTUMN;
            calendar_config._start_of_game += calendar_config.season_length() * 2;
        } else if( scen->has_flag( "WIN_START" ) ) {
            calendar_config._initial_season = WINTER;
            calendar_config._start_of_game += calendar_config.season_length() * 3;
        } else if( scen->has_flag( "SUM_ADV_START" ) ) {
            calendar_config._initial_season = SUMMER;
            calendar_config._start_of_game += calendar_config.season_length() * 5;
        } else {
            debugmsg( "The Unicorn" );
        }
    } else {
        // No scenario, so use the starting date+time configured in world options
        int initial_days = get_option<int>( "INITIAL_DAY" );
        if( initial_days == -1 ) {
            // 0 - 363 for a 91 day season
            initial_days = rng( 0, get_option<int>( "SEASON_LENGTH" ) * 4 - 1 );
        }
        calendar_config._start_of_cataclysm = calendar::turn_zero + 1_days * initial_days;

        // Determine the season based off how long the seasons are set to be
        // First take the number of season elapsed up to the starting date, then mod by 4 to get the season of the current year
        const int season_number = ( initial_days / get_option<int>( "SEASON_LENGTH" ) ) % 4;
        if( season_number == 0 ) {
            calendar_config._initial_season = SPRING;
        } else if( season_number == 1 ) {
            calendar_config._initial_season = SUMMER;
        } else if( season_number == 2 ) {
            calendar_config._initial_season = AUTUMN;
        } else {
            calendar_config._initial_season = WINTER;
        }

        calendar_config._start_of_game = calendar_config._start_of_cataclysm
                                         + 1_hours * get_option<int>( "INITIAL_TIME" )
                                         + 1_days * get_option<int>( "SPAWN_DELAY" );
    }

    calendar::turn = calendar_config._start_of_game;
}

void game::add_artifact_messages( const std::vector<art_effect_passive> &effects )
{
    int net_str = 0;
    int net_dex = 0;
    int net_per = 0;
    int net_int = 0;
    int net_speed = 0;

    for( auto &i : effects ) {
        switch( i ) {
            case AEP_STR_UP:
                net_str += 4;
                break;
            case AEP_DEX_UP:
                net_dex += 4;
                break;
            case AEP_PER_UP:
                net_per += 4;
                break;
            case AEP_INT_UP:
                net_int += 4;
                break;
            case AEP_ALL_UP:
                net_str += 2;
                net_dex += 2;
                net_per += 2;
                net_int += 2;
                break;
            case AEP_STR_DOWN:
                net_str -= 3;
                break;
            case AEP_DEX_DOWN:
                net_dex -= 3;
                break;
            case AEP_PER_DOWN:
                net_per -= 3;
                break;
            case AEP_INT_DOWN:
                net_int -= 3;
                break;
            case AEP_ALL_DOWN:
                net_str -= 2;
                net_dex -= 2;
                net_per -= 2;
                net_int -= 2;
                break;

            case AEP_SPEED_UP:
                net_speed += 20;
                break;
            case AEP_SPEED_DOWN:
                net_speed -= 20;
                break;

            case AEP_PBLUE:
                break; // No message

            case AEP_SNAKES:
                add_msg( m_warning, _( "Your skin feels slithery." ) );
                break;

            case AEP_INVISIBLE:
                add_msg( m_good, _( "You fade into invisibility!" ) );
                break;

            case AEP_CLAIRVOYANCE:
            case AEP_CLAIRVOYANCE_PLUS:
                add_msg( m_good, _( "You can see through walls!" ) );
                break;

            case AEP_SUPER_CLAIRVOYANCE:
                add_msg( m_good, _( "You can see through everything!" ) );
                break;

            case AEP_STEALTH:
                add_msg( m_good, _( "Your steps stop making noise." ) );
                break;

            case AEP_GLOW:
                add_msg( _( "A glow of light forms around you." ) );
                break;

            case AEP_PSYSHIELD:
                add_msg( m_good, _( "Your mental state feels protected." ) );
                break;

            case AEP_RESIST_ELECTRICITY:
                add_msg( m_good, _( "You feel insulated." ) );
                break;

            case AEP_CARRY_MORE:
                add_msg( m_good, _( "Your back feels strengthened." ) );
                break;

            case AEP_FUN:
                add_msg( m_good, _( "You feel a pleasant tingle." ) );
                break;

            case AEP_HUNGER:
                add_msg( m_warning, _( "You feel hungry." ) );
                break;

            case AEP_THIRST:
                add_msg( m_warning, _( "You feel thirsty." ) );
                break;

            case AEP_EVIL:
                add_msg( m_warning, _( "You feel an evil presence…" ) );
                break;

            case AEP_SCHIZO:
                add_msg( m_bad, _( "You feel a tickle of insanity." ) );
                break;

            case AEP_RADIOACTIVE:
                add_msg( m_warning, _( "Your skin prickles with radiation." ) );
                break;

            case AEP_MUTAGENIC:
                add_msg( m_bad, _( "You feel your genetic makeup degrading." ) );
                break;

            case AEP_ATTENTION:
                add_msg( m_warning, _( "You feel an otherworldly attention upon you…" ) );
                break;

            case AEP_FORCE_TELEPORT:
                add_msg( m_bad, _( "You feel a force pulling you inwards." ) );
                break;

            case AEP_MOVEMENT_NOISE:
                add_msg( m_warning, _( "You hear a rattling noise coming from inside yourself." ) );
                break;

            case AEP_BAD_WEATHER:
                add_msg( m_warning, _( "You feel storms coming." ) );
                break;

            case AEP_SICK:
                add_msg( m_bad, _( "You feel unwell." ) );
                break;

            case AEP_SMOKE:
                add_msg( m_warning, _( "A cloud of smoke appears." ) );
                break;
            default:
                //Suppress warnings
                break;
        }
    }

    std::string stat_info;
    if( net_str != 0 ) {
        stat_info += string_format( _( "Str %s%d! " ),
                                    ( net_str > 0 ? "+" : "" ), net_str );
    }
    if( net_dex != 0 ) {
        stat_info += string_format( _( "Dex %s%d! " ),
                                    ( net_dex > 0 ? "+" : "" ), net_dex );
    }
    if( net_int != 0 ) {
        stat_info += string_format( _( "Int %s%d! " ),
                                    ( net_int > 0 ? "+" : "" ), net_int );
    }
    if( net_per != 0 ) {
        stat_info += string_format( _( "Per %s%d! " ),
                                    ( net_per > 0 ? "+" : "" ), net_per );
    }

    if( !stat_info.empty() ) {
        add_msg( m_neutral, stat_info );
    }

    if( net_speed != 0 ) {
        add_msg( m_info, _( "Speed %s%d!" ), ( net_speed > 0 ? "+" : "" ), net_speed );
    }
}

void game::add_artifact_dreams( )
{
    //If player is sleeping, get a dream from a carried artifact
    //Don't need to check that player is sleeping here, that's done before calling
    std::vector<item *> art_items = u.items_with( []( const item & it ) -> bool {
        return it.is_artifact();
    } );
    std::vector<item *>      valid_arts;
    std::vector<std::vector<std::string>>
    valid_dreams; // Tracking separately so we only need to check its req once
    //Pull the list of dreams
    add_msg( m_debug, "Checking %s carried artifacts", art_items.size() );
    for( auto &it : art_items ) {
        //Pick only the ones with an applicable dream
        const cata::value_ptr<islot_artifact> &art = it->type->artifact;
        if( art && art->charge_req != ACR_NULL &&
            ( it->ammo_remaining() < it->ammo_capacity() ||
              it->ammo_capacity() == 0 ) ) { //or max 0 in case of wacky mod shenanigans
            add_msg( m_debug, "Checking artifact %s", it->tname() );
            if( check_art_charge_req( *it ) ) {
                add_msg( m_debug, "   Has freq %s,%s", art->dream_freq_met, art->dream_freq_unmet );
                if( art->dream_freq_met   > 0 && x_in_y( art->dream_freq_met,   100 ) ) {
                    add_msg( m_debug, "Adding met dream from %s", it->tname() );
                    valid_arts.push_back( it );
                    valid_dreams.push_back( art->dream_msg_met );
                }
            } else {
                add_msg( m_debug, "   Has freq %s,%s", art->dream_freq_met, art->dream_freq_unmet );
                if( art->dream_freq_unmet > 0 && x_in_y( art->dream_freq_unmet, 100 ) ) {
                    add_msg( m_debug, "Adding unmet dream from %s", it->tname() );
                    valid_arts.push_back( it );
                    valid_dreams.push_back( art->dream_msg_unmet );
                }
            }
        }
    }
    if( !valid_dreams.empty() ) {
        add_msg( m_debug, "Found %s valid artifact dreams", valid_dreams.size() );
        const int selected = rng( 0, valid_arts.size() - 1 );
        auto it = valid_arts[selected];
        auto msg = random_entry( valid_dreams[selected] );
        const std::string &dream = string_format( _( msg ), it->tname() );
        add_msg( dream );
    } else {
        add_msg( m_debug, "Didn't have any dreams, sorry" );
    }
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

bool game::slip_down()
{
    ///\EFFECT_DEX decreases chances of slipping while climbing
    int climb = u.dex_cur;
    // Parkour and Bad Knees affect it too, avoid division by zero
    if( u.mutation_value( "movecost_obstacle_modifier" ) != 0.0f ) {
        climb = climb / u.mutation_value( "movecost_obstacle_modifier" );
    }
    if( one_in( climb ) ) {
        add_msg( m_bad, _( "You slip while climbing and fall down again." ) );
        if( climb <= 1 ) {
            add_msg( m_bad, _( "Climbing is impossible in your current state." ) );
        }
        return true;
    }
    return false;
}
item *game::add_fake_item( detached_ptr<item> &&it )
{
    it->set_flag( flag_TEMPORARY_ITEM );
    fake_items.push_back( std::move( it ) );
    return fake_items.back();
}

void game::remove_fake_item( item *it )
{
    fake_items.remove( it );
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

void game::tick_portal_links()
{
    ZoneScoped;
    // Total upkeep for both sides of a link, charged every PORTAL_UPKEEP_INTERVAL.
    // Matches the timescale of grid power production (steady_consumer consume_every)
    // so voltmeter readings are on the same scale.
    static constexpr int PORTAL_TOTAL_UPKEEP = grid_link_tile::upkeep_kj;
    static const time_duration PORTAL_UPKEEP_INTERVAL = 300_seconds;

    // Collect (tracker_ptr, source_pos) pairs to pause after iteration to
    // avoid modifying export_nodes_ vectors while we're reading from them.
    std::vector<std::pair<distribution_grid_tracker *, tripoint_abs_ms>> to_pause;

    std::ranges::for_each( grid_trackers_, [&]( auto & kv ) {
        const auto &local_dim_id = kv.first;
        distribution_grid_tracker *local_tracker = kv.second.get();
        if( local_tracker == nullptr ) {
            return;
        }

        std::ranges::for_each( local_tracker->get_export_nodes_mut(), [&]( auto & node ) {
            if( node.paused ) {
                return;
            }

            // Canonical ordering: only one side of each pair runs the transfer.
            // Determined by lexicographic (dim_id, x, y, z) comparison.
            const auto local_key = std::make_tuple( local_dim_id,
                                                    node.source_pos.raw().x,
                                                    node.source_pos.raw().y,
                                                    node.source_pos.raw().z );
            const auto remote_key = std::make_tuple( node.target_dim_id,
                                    node.target_pos.raw().x,
                                    node.target_pos.raw().y,
                                    node.target_pos.raw().z );
            if( local_key >= remote_key ) {
                // The other end handles this pair.
                return;
            }

            // Locate the remote tracker and verify the reverse link exists.
            const auto remote_it = grid_trackers_.find( node.target_dim_id );
            if( remote_it == grid_trackers_.end() || !remote_it->second ) {
                to_pause.emplace_back( local_tracker, node.source_pos );
                return;
            }
            distribution_grid_tracker &remote_tracker = *remote_it->second;

            const bool reverse_ok = std::ranges::any_of(
                                        remote_tracker.get_export_nodes(),
            [&]( const cross_dimension_export_node & rn ) {
                return rn.source_pos    == node.target_pos
                       && rn.target_dim_id == local_dim_id
                       && rn.target_pos    == node.source_pos
                       && !rn.paused;
            } );
            if( !reverse_ok ) {
                // Far end doesn't agree — pause this side.
                to_pause.emplace_back( local_tracker, node.source_pos );
                return;
            }

            // Upkeep: charged every PORTAL_UPKEEP_INTERVAL, not every turn.
            if( calendar::turn - node.last_upkeep < PORTAL_UPKEEP_INTERVAL ) {
                return;
            }

            // get_resource() chains to the remote grid via grid_link_tile, so
            // this reflects the combined power of both sides of the portal.
            distribution_grid &local_grid = local_tracker->grid_at( node.source_pos );
            if( local_grid.get_resource() < PORTAL_TOTAL_UPKEEP ) {
                // Not enough combined power — pause both sides.
                to_pause.emplace_back( local_tracker, node.source_pos );
                to_pause.emplace_back( &remote_tracker, node.target_pos );
            } else {
                // mod_resource() also chains to remote, drawing from the
                // unified pool wherever power is available.
                local_grid.mod_resource( -PORTAL_TOTAL_UPKEEP );
                node.last_upkeep = calendar::turn;
            }
        } );
    } );

    // Apply deferred pauses outside the read loops.
    std::ranges::for_each( to_pause, []( const auto & p ) {
        p.first->pause_export_node( p.second );
    } );
}

void game::tick_temporary_pocket_dimensions()
{
    // Visit all pocket dimension items in the player's possession.
    // If a pocket has expired (last_player_exit + lifetime < now), close it.
    u.visit_items( [&]( item * it ) {
        if( !it->pocket_dim.has_value() || !it->pocket_dim->pocket_info.has_value() ) {
            return VisitResponse::NEXT;
        }
        auto &pd = *it->pocket_dim->pocket_info;
        if( !pd.lifetime.has_value() || !pd.last_player_exit.has_value() ) {
            return VisitResponse::NEXT;
        }
        if( *pd.last_player_exit + *pd.lifetime < calendar::turn ) {
            add_msg( m_bad, _( "The %s flickers and goes dark — the pocket dimension has collapsed." ),
                     it->tname() );
            it->pocket_dim = std::nullopt;
        }
        return VisitResponse::NEXT;
    } );
}

void game::tick_vehicle_portal_taps()
{
    constexpr int TAP_KJ_PER_TURN = 5;
    static const std::string flag_portal_tap( "POWER_DRAW_LINKED_PORTAL" );

    std::ranges::for_each( m.get_vehicles(), [&]( wrapped_vehicle & wv ) {
        vehicle &veh = *wv.v;
        if( !veh.has_portal_tap_parts ) {
            return;
        }
        for( int i = 0; i < veh.part_count(); ++i ) {
            vehicle_part &part = veh.part( i );
            if( !part.portal_tap_linked ) {
                continue;
            }
            if( !part.info().has_flag( flag_portal_tap ) ) {
                continue;
            }
            auto *tracker = get_distribution_grid_tracker_for( part.portal_tap_dim_id );
            if( tracker == nullptr ) {
                continue;
            }
            auto grid = tracker->grid_at( part.portal_tap_pos );
            const auto available = grid.get_resource();
            if( available <= 0 ) {
                continue;
            }
            const auto to_draw = std::min( available, TAP_KJ_PER_TURN );
            grid.mod_resource( -to_draw );
            veh.charge_battery( to_draw );
        }
    } );
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

#ifdef COOP_ENABLED
auto game::poll_event() -> input_event
{
    const auto old_delay = inp_mngr.get_timeout();
    inp_mngr.set_timeout( 0 );               // non-blocking
    const auto evt = inp_mngr.get_input_event();
    inp_mngr.set_timeout( old_delay );        // restore
    return evt;
}
#endif // COOP_ENABLED

#ifdef COOP_ENABLED
#endif // COOP_ENABLED

#ifdef COOP_ENABLED

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
#endif // COOP_ENABLED
