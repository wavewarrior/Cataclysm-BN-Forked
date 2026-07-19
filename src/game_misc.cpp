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
#endif // BOX2D_ENABLED

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

void game::remove_fake_item( item *it )
{
    fake_items.remove( it );
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

