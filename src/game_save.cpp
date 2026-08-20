#include "action_time_scale.h"
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
    for( const auto &elem : follower_ids ) {
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

bool game::save_factions_missions_npcs()
{
    return get_active_world()->write_to_file( SAVE_MASTER, [&]( std::ostream & fout ) {
        serialize_master( fout );
    }, _( "factions data" ) );
}

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
        if( it.ammo_remaining() < it.ammo_capacity() && action_time_scale::once_every_this_tick( 1_minutes ) ) {
            //Before incrementing charge, check that any extra requirements are met
            if( check_art_charge_req( it ) ) {
                switch( it.type->artifact->charge_type ) {
                    case ARTC_NULL:
                    case NUM_ARTCS:
                        break; // dummy entries
                    case ARTC_TIME:
                        // Once per hour
                        if( action_time_scale::once_every_this_tick( 1_hours ) ) {
                            it.charges++;
                        }
                        break;
                    case ARTC_SOLAR:
                        if( action_time_scale::once_every_this_tick( 10_minutes ) &&
                            is_in_sunlight( who.bub_pos() ) ) {
                            it.charges++;
                        }
                        break;
                    // Artifacts can inflict pain even on Deadened folks.
                    // Some weird Lovecraftian thing.  ;P
                    // (So DON'T route them through mod_pain!)
                    case ARTC_PAIN:
                        if( action_time_scale::once_every_this_tick( 1_minutes ) ) {
                            add_msg( m_bad, _( "You suddenly feel sharp pain for no reason." ) );
                            who.mod_pain_noresist( 3 * rng( 1, 3 ) );
                            it.charges++;
                        }
                        break;
                    case ARTC_HP:
                        if( action_time_scale::once_every_this_tick( 1_minutes ) ) {
                            add_msg( m_bad, _( "You feel your body decaying." ) );
                            who.hurtall( 1, nullptr );
                            it.charges++;
                        }
                        break;
                    case ARTC_FATIGUE:
                        if( action_time_scale::once_every_this_tick( 1_minutes ) ) {
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

