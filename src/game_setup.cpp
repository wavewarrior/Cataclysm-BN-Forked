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
// Visibility threshold at max view distance. Computed as 1/exp(LIGHT_TRANSPARENCY_OPEN_AIR * g_max_view_distance).
// Default matches the old hardcoded 0.1 threshold for g_max_view_distance=60.

/// Update all runtime globals from an explicit bubble size value.

/// Read REALITY_BUBBLE_SIZE from options and update all runtime globals.
/// Must be called before map construction (game::setup) and after each load.


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


//The one and only uistate instance



// This is the main game set-up process.


// Load everything that will not depend on any mods

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

static void init_bubble_config()
{
    init_bubble_config( get_option<int>( "REALITY_BUBBLE_SIZE" ) );
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

static void update_faction_api( npc *guy )
{
    if( guy->get_faction_ver() < 2 ) {
        guy->set_fac( your_followers );
        guy->set_faction_ver( 2 );
    }
}

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

void game::set_npcs_dirty()
{
    npcs_dirty = true;
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

