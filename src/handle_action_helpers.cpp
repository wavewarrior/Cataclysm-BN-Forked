#include "action.h"
#include "activity_actor_definitions.h"
#include "advanced_inv.h"
#include "animation.h"
#include "armor_layers.h"
#include "auto_note.h"
#include "auto_pickup.h"
#include "avatar.h"
#include "avatar_action.h"
#include "avatar_functions.h"
#include "bionics.h"
#include "bionics_ui.h"
#include "calendar.h"
#include "catacharset.h"
#include "catalua.h"
#include "coop_client.h"
#include "coop_menu.h"
#include "coop_server.h"
#include "coop_session.h"
#include "json.h"
#include "character.h"
#include "character_display.h"
#include "character_martial_arts.h"
#include "character_turn.h"
#include "clzones.h"
#include "color.h"
#include "construction.h"
#include "crafting.h"
#include "cursesdef.h"
#include "damage.h"
#include "debug.h"
#include "debug_menu.h"
#include "diary.h"
#include "distraction_manager.h"
#include "faction.h"
#include "field.h"
#include "field_type.h"
#include "flag.h"
#include "fstream_utils.h"
#include "game.h" // IWYU pragma: associated
#include "game_constants.h"
#include "game_inventory.h"
#include "gamemode.h"
#include "gates.h"
#include "gun_mode.h"
#include "help.h"
#include "input.h"
#include "int_id.h"
#include "item.h"
#include "item_contents.h"
#include "item_group.h"
#include "item_hauling.h"
#include "itype.h"
#include "iuse.h"
#include "lightmap.h"
#include "line.h"
#include "magic.h"
#include "make_static.h"
#include "map.h"
#include "map_selector.h"
#include "mapdata.h"
#include "mapsharing.h"
#include "messages.h"
#include "monster.h"
#include "mtype.h"
#include "mutation.h"
#include "mutation_ui.h"
#include "options.h"
#include "output.h"
#include "overmap_ui.h"
#include "panels.h"
#include "player.h"
#include "player_activity.h"
#include "player_cmd.h"
#include "popup.h"
#include "ranged.h"
#include "rng.h"
#include "safemode_ui.h"
#include "salvage.h"
#include "scores_ui.h"
#include "sdl_lighting_devui.h"
#include "sidebar_anim.h"
#include "sounds.h"
#include "sound_visualization.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"
#include "units.h"
#include "utils/url.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_grab.h"
#include "vehicle_part.h"
#include "vehicle_wait.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "weather.h"
#include "worldfactory.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <set>
#include <unordered_set>
#include <sstream>
#include <utility>


namespace
{
/// Emit a TERRAIN_CHANGE action to the co-op host with the new ter/furn at an abs position.
/// Only called when g->coop_client_ is non-null (client-side only).
inline void coop_emit_terrain_change(
    const tripoint_abs_ms &abs, const ter_id &ter, const furn_id &furn )
{
    g->coop_client_->queue_terrain_change( abs, ter.id().str(), furn.id().str() );
}
} // namespace

static const activity_id ACT_MOVE_LOOT( "ACT_MOVE_LOOT" );
static const activity_id ACT_MULTIPLE_BUTCHER( "ACT_MULTIPLE_BUTCHER" );
static const activity_id ACT_MULTIPLE_CHOP_PLANKS( "ACT_MULTIPLE_CHOP_PLANKS" );
static const activity_id ACT_MULTIPLE_CHOP_TREES( "ACT_MULTIPLE_CHOP_TREES" );
static const activity_id ACT_MULTIPLE_CONSTRUCTION( "ACT_MULTIPLE_CONSTRUCTION" );
static const activity_id ACT_MULTIPLE_FARM( "ACT_MULTIPLE_FARM" );
static const activity_id ACT_MULTIPLE_MINE( "ACT_MULTIPLE_MINE" );
static const activity_id ACT_PULP( "ACT_PULP" );
static const activity_id ACT_SPELLCASTING( "ACT_SPELLCASTING" );
static const activity_id ACT_VEHICLE_DECONSTRUCTION( "ACT_VEHICLE_DECONSTRUCTION" );
static const activity_id ACT_VEHICLE_REPAIR( "ACT_VEHICLE_REPAIR" );
static const activity_id ACT_WAIT( "ACT_WAIT" );
static const activity_id ACT_WAIT_STAMINA( "ACT_WAIT_STAMINA" );
static const activity_id ACT_WAIT_WEATHER( "ACT_WAIT_WEATHER" );

static const efftype_id effect_alarm_clock( "alarm_clock" );
static const efftype_id effect_laserlocked( "laserlocked" );
static const efftype_id effect_relax_gas( "relax_gas" );

static const itype_id itype_shoulder_strap( "shoulder_strap" );
static const itype_id itype_pistol_lanyard( "pistol_lanyard" );

static const skill_id skill_melee( "melee" );

static const quality_id qual_CUT( "CUT" );

static const bionic_id bio_remote( "bio_remote" );

static const trait_id trait_HIBERNATE( "HIBERNATE" );
static const trait_id trait_PROF_CHURL( "PROF_CHURL" );
static const trait_id trait_SHELL2( "SHELL2" );
static const trait_id trait_BRAWLER( "BRAWLER" );

static const std::string flag_LOCKED( "LOCKED" );

#define dbg(x) DebugLogFL((x), DC::Game)

class user_turn
{

    private:
        std::chrono::time_point<std::chrono::steady_clock> user_turn_start;

    public:
        user_turn() { user_turn_start = std::chrono::steady_clock::now(); }

        bool has_timeout_elapsed() { return moves_elapsed() > 100; }

        int moves_elapsed() {
            const float turn_duration = get_option<float>( "TURN_DURATION" );
            // Magic number 0.005 chosen due to option menu's 2 digit precision and
            // the option menu UI rounding <= 0.005 down to "0.00" in the display.
            // This conditional will catch values (e.g. 0.003) that the options menu
            // would round down to "0.00" in the options menu display. This prevents
            // the user from being surprised by floating point rounding near zero.
            if( turn_duration <= 0.005 ) { return 0; }
            auto now = std::chrono::steady_clock::now();
            std::chrono::milliseconds elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>( now - user_turn_start );
            return elapsed_ms.count() / ( 10.0 * turn_duration );
        }
};


#include "handle_action_helpers.h"

namespace action_handlers
{

bool init_weather_anim( const weather_type_id& wtype, weather_printable& wPrint )
{
    const weather_animation_t &anim = wtype->animation;

    wPrint.colGlyph = anim.color;
    wPrint.cGlyph = anim.symbol;
    wPrint.wtype = wtype;
    wPrint.vdrops.clear();

    return anim.symbol != NULL_UNICODE;
}

void generate_weather_anim_frame( const weather_type_id& wtype, weather_printable& wPrint )
{
    map& m = get_map();
    avatar& u = get_avatar();

    const visibility_variables& cache = m.get_visibility_variables_cache();
    const level_cache& map_cache = m.get_cache_ref( u.bub_pos().z() );
    const auto& visibility_cache = map_cache.visibility_cache;

    const int TOTAL_VIEW = g_max_view_distance * 2 + 1;
    point
    iStart( ( TERRAIN_WINDOW_WIDTH > TOTAL_VIEW ) ? ( TERRAIN_WINDOW_WIDTH - TOTAL_VIEW ) / 2 : 0,
            ( TERRAIN_WINDOW_HEIGHT > TOTAL_VIEW ) ? ( TERRAIN_WINDOW_HEIGHT - TOTAL_VIEW ) / 2 : 0 );
    point iEnd(
        ( TERRAIN_WINDOW_WIDTH > TOTAL_VIEW )
        ? TERRAIN_WINDOW_WIDTH - ( TERRAIN_WINDOW_WIDTH - TOTAL_VIEW ) / 2
        : TERRAIN_WINDOW_WIDTH,
        ( TERRAIN_WINDOW_HEIGHT > TOTAL_VIEW )
        ? TERRAIN_WINDOW_HEIGHT - ( TERRAIN_WINDOW_HEIGHT - TOTAL_VIEW ) / 2
        : TERRAIN_WINDOW_HEIGHT );

    if( g->fullscreen ) {
        iStart.x = 0;
        iStart.y = 0;
        iEnd.x = TERMX;
        iEnd.y = TERMY;
    }

    const weather_animation_t &anim = wtype->animation;
    point offset(
        u.view_offset.xy().raw()
        + point( -getmaxx( g->w_terrain ) / 2 + u.bub_pos().x(),
                 -getmaxy( g->w_terrain ) / 2 + u.bub_pos().y() ) );

    if( tile_iso ) {
        iStart.x = 0;
        iStart.y = 0;
        iEnd.x = g_mapsize_x;
        iEnd.y = g_mapsize_y;
        offset.x = 0;
        offset.y = 0;
    }

    wPrint.vdrops.clear();

    const int dropCount = static_cast<int>( iEnd.x * iEnd.y * anim.factor );
    for( int i = 0; i < dropCount; i++ ) {
        const point iRand{rng( iStart.x, iEnd.x - 1 ), rng( iStart.y, iEnd.y - 1 )};
        const point_bub_ms map( iRand + offset );

        if( !map_cache.inbounds( map ) ) { continue; }

        const tripoint_bub_ms mapp( map, u.bub_pos().z() );

        const lit_level lighting = visibility_cache[map_cache.idx( mapp.x(), mapp.y() )];

        if( m.is_outside( mapp ) && m.get_visibility( lighting, cache ) == VIS_CLEAR
            && !g->critter_at( mapp, true ) ) {
            // Suppress if a critter is there
            wPrint.vdrops.emplace_back( iRand.x, iRand.y );
        }
    }
}

void rcdrive( point_rel_ms d )
{
    player& u = g->u;
    map& here = get_map();
    std::string car_location_string = u.get_value( "remote_controlling" );

    if( car_location_string.empty() ) {
        u.add_msg_if_player( m_warning, _( "No radio car connected." ) );
        return;
    }

    tripoint_bub_ms c;
    deserialize_wrapper( [&]( JsonIn & jsin ) { c.deserialize( jsin ); }, car_location_string );

    map_cursor mc( c );
    std::vector<item *> rc_items = mc.items_with( [&]( const item & it ) {
        return it.has_flag( flag_RADIO_CONTROLLED );
    } );

    if( rc_items.empty() ) {
        u.add_msg_if_player( m_warning, _( "No radio car connected." ) );
        u.remove_value( "remote_controlling" );
        return;
    }
    // TODO: keep track of which car is being controlled
    item* rc_car = rc_items[0];

    auto dest = c + d;
    if( here.impassable( dest ) || !here.can_put_items_ter_furn( dest ) || here.has_furn( dest ) ) {
        sound_event se;
        se.origin = dest;
        se.volume = 65;
        se.category = sounds::sound_t::combat;
        se.description = _( "sound of a collision with an obstacle." );
        se.id = "misc";
        se.variant = "rc_car_hits_obstacle";

        sounds::sound( se );
        return;
    } else {
        tripoint_bub_ms src( c );
        detached_ptr<item> det_car = here.i_rem( src, rc_car );
        here.add_item_or_charges( dest, std::move( det_car ) );
        //~ Sound of moving a remote controlled car
        sound_event se;
        se.origin = src;
        se.volume = 50;
        se.category = sounds::sound_t::movement;
        se.movement_noise = true;
        se.description = _( "zzz…" );
        se.id = "misc";
        se.variant = "rc_car_drives";

        sounds::sound( se );
        u.moves -= 50;

        u.set_value( "remote_controlling",
        serialize_wrapper( [&]( JsonOut & jo ) { dest.serialize( jo ); } ) );
        return;
    }
}

void pldrive( const tripoint_rel_veh& p )
{
    if( !g->check_safe_mode_allowed() ) { return; }
    player& u = g->u;
    vehicle* veh = g->remoteveh();
    bool remote = true;
    int part = -1;
    map& here = get_map();
    if( !veh ) {
        if( const optional_vpart_position vp = here.veh_at( u.bub_pos() ) ) {
            veh = &vp->vehicle();
            part = vp->part_index();
        }
        remote = false;
    }
    if( !veh ) {
        debugmsg( "game::pldrive error: can't find vehicle!  Drive mode is now off." );
        u.in_vehicle = false;
        return;
    }
    if( !remote ) {
        static const itype_id fuel_type_animal( "animal" );
        const bool has_animal_controls = veh->part_with_feature( part, "CONTROL_ANIMAL", true ) >= 0;
        const bool has_controls = veh->part_with_feature( part, "CONTROLS", true ) >= 0;
        const bool has_animal =
            veh->has_engine_type( fuel_type_animal, false ) && veh->has_harnessed_animal();
        if( !has_controls && !has_animal_controls ) {
            add_msg( m_info, _( "You can't drive the vehicle from here.  You need controls!" ) );
            u.controlling_vehicle = false;
            return;
        } else if( !has_controls && has_animal_controls && !has_animal ) {
            add_msg( m_info, _( "You can't drive this vehicle without an animal to pull it." ) );
            u.controlling_vehicle = false;
            return;
        }
    } else {
        if( veh->get_avail_parts( "REMOTE_CONTROLS" ).empty() ) {
            add_msg( m_info, _( "Can't drive this vehicle remotely.  It has no working controls." ) );
            return;
        }
    }
    if( p.z() != 0 && !here.has_zlevels() ) {
        u.add_msg_if_player( m_info, _( "This vehicle doesn't look very airworthy." ) );
        return;
    }
    if( p.z() == -1 ) {
        if( veh->check_heli_descend( u ) ) {
            u.add_msg_if_player( m_info, _( "You steer the vehicle into a descent." ) );
        } else {
            return;
        }
    } else if( p.z() == 1 ) {
        if( veh->check_heli_ascend( u ) ) {
            u.add_msg_if_player( m_info, _( "You steer the vehicle into an ascent." ) );
        } else {
            return;
        }
    }
    veh->pldrive( get_avatar(), p );
}

void pldrive( point_rel_veh d ) { return pldrive( tripoint_rel_veh( d, 0 ) ); }

void open()
{
    player& u = g->u;
    const std::optional<tripoint_bub_ms> openp_ = choose_adjacent_highlight(
            _( "Open where?" ),
            pgettext( "no door, gate, curtain, etc.", "There is nothing that can be opened nearby." ),
            ACTION_OPEN, false );

    if( !openp_ ) { return; }
    const auto openp = *openp_;
    map& here = get_map();

    if( const optional_vpart_position vp = here.veh_at( openp ) ) {
        const vehicle* const veh = &vp->vehicle();
        const int openable = veh->next_part_to_open( vp->part_index() );
        if( openable >= 0 ) {
            const vehicle* player_veh = veh_pointer_or_null( here.veh_at( u.bub_pos() ) );
            const bool outside = !player_veh || player_veh != veh;
            if( here.open_door_veh( &get_avatar(), vp, openp, !outside ) ) {
                u.moves -= 100;
                if( g->coop_client_ ) {
                    g->coop_client_->queue_terrain_change( here.bub_to_abs( openp ),
                                                           here.ter( openp ).id().str(), here.furn( openp ).id().str() );
                }
            }
        } else {
            // If there are any OPENABLE parts here, they must be already open
            if( const std::optional<vpart_reference> already_open =
                    vp.part_with_feature( "OPENABLE", true ) ) {
                const std::string name = already_open->info().name();
                add_msg( m_info, _( "That %s is already open." ), name );
            }
        }
    } else if( here.open_door( &u, openp, !here.is_outside( u.bub_pos() ) ) ) {
        u.moves -= 100;
        if( g->coop_client_ ) {
            coop_emit_terrain_change( here.bub_to_abs( openp ), here.ter( openp ), here.furn( openp ) );
        }
    } else {
        const ter_str_id tid = here.ter( openp ).id();

        if( here.has_flag( flag_LOCKED, openp ) ) {
            add_msg( m_info, _( "The door is locked!" ) );
            return;
        } else if( tid.obj().close ) {
            // if the following message appears unexpectedly, the prior check was for t_door_o
            add_msg( m_info, _( "That door is already open." ) );
            return;
        }
        add_msg( m_info, _( "No door there." ) );
    }
}

void close()
{
    if( const std::optional<tripoint_bub_ms> pnt = choose_adjacent_highlight(
            _( "Close where?" ),
            pgettext( "no door, gate, etc.", "There is nothing that can be closed nearby." ),
            ACTION_CLOSE, false ) ) {
        map &here_c = get_map();
        const auto ter_before = here_c.ter( *pnt );
        const auto furn_before = here_c.furn( *pnt );
        doors::close_door( get_map(), g->u, *pnt );
        if( g->coop_client_ &&
            ( here_c.ter( *pnt ) != ter_before || here_c.furn( *pnt ) != furn_before ) ) {
            coop_emit_terrain_change( here_c.bub_to_abs( *pnt ), here_c.ter( *pnt ), here_c.furn( *pnt ) );
        }
    }
}

void grab()
{
    avatar& you = g->u;
    map& here = get_map();

    if( you.get_grab_type() != OBJECT_NONE ) {
        if( const auto target = vehicle_grab_target_at( here, you.bub_pos() + you.grab_point ) ) {
            add_msg( _( "You release the %s." ), target->vp.vehicle().name );
        } else if( here.has_furn( you.bub_pos() + you.grab_point ) ) {
            add_msg( _( "You release the %s." ), here.furnname( you.bub_pos() + you.grab_point ) );
        }

        you.grab( OBJECT_NONE );
        return;
    }

    const std::optional<tripoint_bub_ms> grabp_ = choose_adjacent( _( "Grab where?" ) );
    if( !grabp_ ) {
        add_msg( _( "Never mind." ) );
        return;
    }
    const auto grabp = *grabp_;

    if( grabp == you.bub_pos() ) {
        add_msg( _( "You get a hold of yourself." ) );
        you.grab( OBJECT_NONE );
        return;
    }
    if( const auto target = vehicle_grab_target_at( here, grabp ) ) {
        if( !target->vp.vehicle().handle_potential_theft( get_avatar() ) ) { return; }
        you.grab( OBJECT_VEHICLE, target->pos - you.bub_pos() );
        add_msg( _( "You grab the %s." ), target->vp.vehicle().name );
    } else if( here.has_furn( grabp ) ) { // If not, grab furniture if present
        if( !here.furn( grabp ).obj().is_movable() ) {
            add_msg( _( "You can not grab the %s" ), here.furnname( grabp ) );
            return;
        }
        you.grab( OBJECT_FURNITURE, grabp - you.bub_pos() );
        if( !here.can_move_furniture( grabp, &you ) ) {
            add_msg( _( "You grab the %s. It feels really heavy." ), here.furnname( grabp ) );
        } else {
            add_msg( _( "You grab the %s." ), here.furnname( grabp ) );
        }
    } else { // TODO: grab mob? Captured squirrel = pet (or meat that stays fresh longer).
        add_msg( m_info, _( "There's nothing to grab there!" ) );
    }
}

void haul()
{
    player& u = g->u;
    map& here = get_map();

    if( u.is_hauling() ) {
        u.stop_hauling();
    } else {
        if( here.veh_at( u.bub_pos() ) ) {
            add_msg( m_info, _( "You cannot haul inside vehicles." ) );
        } else if( here.has_flag( TFLAG_DEEP_WATER, u.bub_pos() ) ) {
            add_msg( m_info, _( "You cannot haul while in deep water." ) );
        } else if( !here.can_put_items( u.bub_pos() ) ) {
            add_msg( m_info, _( "You cannot haul items here." ) );
        } else if( !has_haulable_items( u.bub_pos() ) ) {
            add_msg( m_info, _( "There are no items to haul here." ) );
        } else {
            u.start_hauling();
        }
    }
}

void smash()
{
    player& u = g->u;
    map& here = get_map();
    if( u.is_mounted() ) {
        auto mons = u.mounted_creature.get();
        if( mons->has_flag( MF_RIDEABLE_MECH ) ) {
            if( !mons->check_mech_powered() ) {
                add_msg( m_bad, _( "Your %s refuses to move as its batteries have been drained." ),
                         mons->get_name() );
                return;
            }
        }
    }
    item& weapon = u.primary_weapon();
    if( weapon.can_shatter()
        && !query_yn( _( "Are you sure you want to smash with an item that might shatter?" ) ) ) {
        return;
    }
    const int move_cost = !u.is_armed() ? 80 : weapon.attack_cost() * 0.8;

    bool didit = false;
    bool mech_smash = false;
    int smashskill;
    ///\EFFECT_STR increases smashing capability
    if( u.is_mounted() ) {
        auto mon = u.mounted_creature.get();
        smashskill =
            u.str_cur + mon->mech_str_addition() + mon->type->melee_dice * mon->type->melee_sides;
        mech_smash = true;
    } else {
        smashskill = u.str_cur + weapon.damage_melee( DT_BASH );
    }

    const bool allow_floor_bash = here.has_zlevels();
    const std::optional<tripoint_bub_ms> smashp_ =
        choose_adjacent( _( "Smash where?" ), allow_floor_bash );
    if( !smashp_ ) { return; }
    auto smashp = *smashp_;

    bool smash_floor = false;
    if( smashp.z() != u.bub_pos().z() ) {
        if( smashp.z() > u.bub_pos().z() ) {
            // TODO: Knock on the ceiling
            return;
        }

        smashp.z() = u.bub_pos().z();
        smash_floor = true;
    }
    if( u.is_mounted() ) {
        monster* crit = u.mounted_creature.get();
        if( crit->has_flag( MF_RIDEABLE_MECH ) ) { crit->use_mech_power( -3 ); }
    }
    for( std::pair<const field_type_id, field_entry> &fd_to_smsh : here.field_at( smashp ) ) {
        const map_bash_info& bash_info = fd_to_smsh.first->bash_info;
        if( bash_info.str_min == -1 ) { continue; }
        if( smashskill < bash_info.str_min ) {
            add_msg( m_neutral, _( "You don't seem to be damaging the %s." ),
                     fd_to_smsh.first->get_name() );
            return;
        } else if( smashskill >= rng( bash_info.str_min, bash_info.str_max ) ) {
            sound_event se;
            se.origin = smashp;
            se.volume = bash_info.sound_vol.value_or( 0 );
            se.category = sounds::sound_t::combat;
            se.description = bash_info.sound.translated();
            se.id = "smash";
            se.variant = "field";

            sounds::sound( se );
            here.remove_field( smashp, fd_to_smsh.first );
            here.spawn_items( smashp, item_group::items_from( bash_info.drop_group, calendar::turn ) );
            u.mod_moves( -bash_info.fd_bash_move_cost );
            add_msg( m_info, bash_info.field_bash_msg_success.translated() );
            return;
        } else {
            sound_event se;
            se.origin = smashp;
            se.volume = bash_info.sound_fail_vol.value_or( 0 );
            se.category = sounds::sound_t::combat;
            se.description = bash_info.sound_fail.translated();
            se.id = "smash";
            se.variant = "field";

            sounds::sound( se );
            return;
        }
    }

    bool should_pulp = false;
    for( const item * const& it : here.i_at( smashp ) ) {
        if( it->is_corpse() && it->damage() < it->max_damage()
            && ( it->can_revive() || it->get_mtype()->zombify_into ) ) {
            if( it->get_mtype()->bloodType()->has_acid ) {
                if( query_yn( _( "Are you sure you want to pulp an acid filled corpse?" ) ) ) {
                    should_pulp = true;
                    break; // Don't prompt for the same thing multiple times
                } else {
                    return; // Player doesn't want an acid bath
                }
            }
            should_pulp = true; // There is at least one corpse to pulp
        }
    }

    if( should_pulp ) {
        // do activity forever. ACT_PULP stops itself
        u.assign_activity(
            std::make_unique<player_activity>(
                std::make_unique<pulp_activity_actor>( here.bub_to_abs( smashp ) ) ),
            calendar::INDEFINITELY_LONG );
        return; // don't smash terrain if we've smashed a corpse
    }

    vehicle* veh = veh_pointer_or_null( g->m.veh_at( smashp ) );
    if( veh != nullptr ) {
        if( !veh->handle_potential_theft( get_avatar() ) ) { return; }
    }
    // C2c: snapshot terrain + ground items before bash so we can diff what changed.
    ter_id   smash_ter_before;
    furn_id  smash_furn_before;
    std::unordered_set<const item *> smash_items_before;
    if( g->coop_client_ ) {
        smash_ter_before  = here.ter( smashp );
        smash_furn_before = here.furn( smashp );
        for( const tripoint_bub_ms &p : here.points_in_radius( smashp, 1 ) ) {
            for( const item *it : here.i_at( p ) ) { smash_items_before.insert( it ); }
        }
    }
    const auto bash = bash_params{
        .strength = smashskill,
        .silent = false,
        .destroy = false,
        .bash_floor = smash_floor,
        .roll = static_cast<float>( rng_float( 0, 1.0f ) ),
        .bashing_from_above = false,
        .do_recurse = true,
        .caused_by_player = true
    };
    didit = here.bash( smashp, bash ).did_bash;
    if( didit ) {
        u.anim_on_attack( smashp, false ); // sprite lunge toward the smashed tile
        if( !mech_smash ) {
            u.handle_melee_wear( weapon );
            const int mod_sta =
                ( ( weapon.weight() / 10_gram ) + 200
                  + static_cast<int>( get_option<float>( "PLAYER_BASE_STAMINA_REGEN_RATE" ) ) )
                * -1;
            u.mod_stamina( mod_sta );
            if( u.get_skill_level( skill_melee ) == 0 ) {
                u.practice( skill_melee, rng( 0, 1 ) * rng( 0, 1 ) );
            }
            const int vol = weapon.volume() / units::legacy_volume_factor;
            if( weapon.can_shatter() && rng( 0, vol + 3 ) < vol ) {
                add_msg( m_bad, _( "Your %s shatters!" ), weapon.tname() );
                weapon.spill_contents( u.bub_pos() );
                sound_event se;
                se.origin = u.bub_pos();
                se.volume = 70;
                se.category = sounds::sound_t::combat;
                se.description = _( "CRACK!" );
                se.id = "smash";
                se.variant = "glass";

                sounds::sound( se );
                u.deal_damage( nullptr, bodypart_id( "hand_r" ), damage_instance( DT_CUT, rng( 0, vol ) ) );
                if( vol > 20 ) {
                    // Hurt left arm too, if it was big
                    u.deal_damage( nullptr, bodypart_id( "hand_l" ),
                                   damage_instance( DT_CUT, rng( 0, static_cast<int>( vol * .5 ) ) ) );
                }
                u.remove_primary_weapon();
                u.check_dead_state();
            }
        }
        u.moves -= move_cost;
        if( g->coop_client_ ) {
            // Emit terrain change if the bash broke through.
            if( here.ter( smashp ) != smash_ter_before || here.furn( smashp ) != smash_furn_before ) {
                coop_emit_terrain_change(
                    here.bub_to_abs( smashp ), here.ter( smashp ), here.furn( smashp ) );
            }
            // Emit debris items that appeared on the ground (glass, rubble, etc.).
            // Without this the host tile diverges and items are lost on next full resync.
            std::ostringstream drop_oss;
            JsonOut drop_jout( drop_oss );
            drop_jout.start_object();
            drop_jout.member( "items" );
            drop_jout.start_array();
            bool has_debris = false;
            for( const tripoint_bub_ms &p : here.points_in_radius( smashp, 1 ) ) {
                const tripoint_abs_ms abs = here.bub_to_abs( p );
                for( const item *it : here.i_at( p ) ) {
                    if( smash_items_before.count( it ) ) { continue; }
                    drop_jout.start_object();
                    drop_jout.member( "tx", abs.x() );
                    drop_jout.member( "ty", abs.y() );
                    drop_jout.member( "tz", abs.z() );
                    std::ostringstream item_oss;
                    JsonOut jitem( item_oss );
                    it->serialize( jitem );
                    drop_jout.member( "data", item_oss.str() );
                    drop_jout.end_object();
                    has_debris = true;
                }
            }
            drop_jout.end_array();
            drop_jout.end_object();
            if( has_debris ) { g->coop_client_->queue_action( "DROP", drop_oss.str() ); }
            // Proxy melee: queue SMASH with the absolute target position so the proxy
            // swings at the correct tile on the host side.  Terrain changes are already
            // propagated via coop_emit_terrain_change above; this is for the melee animation.
            const tripoint_abs_ms abs_smashp = here.bub_to_abs( smashp );
            // CL-MELEE-WEAPON: embed weapon at smash time (same pattern as FIRE ctx_json).
            std::ostringstream smash_ctx_oss;
            JsonOut smash_ctx_jout( smash_ctx_oss );
            smash_ctx_jout.start_object();
            smash_ctx_jout.member( "tx", abs_smashp.x() );
            smash_ctx_jout.member( "ty", abs_smashp.y() );
            smash_ctx_jout.member( "tz", abs_smashp.z() );
            if( g->u.is_armed() ) {
                smash_ctx_jout.member( "weapon_id", g->u.primary_weapon().typeId().str() );
            }
            smash_ctx_jout.end_object();
            g->coop_client_->queue_action( "SMASH", smash_ctx_oss.str() );
        }

        if( smashskill < here.bash_resistance( smashp ) && one_in( 10 ) ) {
            if( here.has_furn( smashp ) && here.furn( smashp ).obj().bash.str_min != -1 ) {
                // %s is the smashed furniture
                add_msg( m_neutral, _( "You don't seem to be damaging the %s." ),
                         here.furnname( smashp ) );
            } else {
                // %s is the smashed terrain
                add_msg( m_neutral, _( "You don't seem to be damaging the %s." ),
                         here.tername( smashp ) );
            }
        }

        if( !here.has_floor_or_support( u.bub_pos() )
            && !here.has_flag_ter( "GOES_DOWN", u.bub_pos() ) ) {
            std::optional<tripoint_rel_ms> to_safety;
            while( true ) {
                to_safety = choose_direction( _( "Floor below destroyed!  Move where?" ) );
                if( to_safety && *to_safety == tripoint_rel_ms::zero() ) { to_safety.reset(); }
                if( !to_safety && query_yn( _( "Fall down?" ) ) ) { break; }

                if( to_safety ) {
                    auto oldpos = u.bub_pos();
                    auto newpos = u.bub_pos() + *to_safety;
                    // game::walk_move will return true even if you don't move
                    if( g->walk_move( newpos ) && u.bub_pos() != oldpos ) { break; }
                }
            }
            if( !to_safety ) {
                // HACK! We should have a "fall down" function instead of invoking ledge trap
                here.creature_on_trap( u, false );
            }
        }
    } else {
        add_msg( _( "There's nothing there to smash!" ) );
    }
}

int try_set_alarm()
{
    uilist as_m;
    const bool already_set = g->u.has_effect( effect_alarm_clock );

    as_m.text =
        already_set ? _( "You already have an alarm set.  What do you want to do?" )
        : _( "You have an alarm clock.  What do you want to do?" );

    as_m.entries.emplace_back(
        0, true, 'w', already_set ? _( "Keep the alarm and wait a while" ) : _( "Wait a while" ) );
    as_m.entries.emplace_back(
        1, true, 'a', already_set ? _( "Change your alarm" ) : _( "Set an alarm for later" ) );
    as_m.query();

    return as_m.ret;
}

auto parse_custom_wait_duration( const std::string& value ) -> std::optional<time_duration>
{
    const auto trimmed_value = trim_whitespaces( value );
    if( trimmed_value.empty() ) { return std::nullopt; }
    auto digits_end = decltype( trimmed_value.size() ) {0};
    while( digits_end < trimmed_value.size()
           && std::isdigit( static_cast<unsigned char>( trimmed_value[digits_end] ) ) ) {
        ++digits_end;
    }
    if( digits_end == 0 ) { return std::nullopt; }
    const auto numeric_text = trimmed_value.substr( 0, digits_end );
    auto amount = std::int64_t( 0 );
    const auto parse_result =
        std::from_chars( numeric_text.data(), numeric_text.data() + numeric_text.size(), amount );
    if( parse_result.ec != std::errc() ) { return std::nullopt; }
    auto suffix_pos = digits_end;
    while( suffix_pos < trimmed_value.size()
           && std::isspace( static_cast<unsigned char>( trimmed_value[suffix_pos] ) ) ) {
        ++suffix_pos;
    }
    auto unit = 'm';
    auto has_unit = false;
    if( suffix_pos < trimmed_value.size() ) {
        unit = trimmed_value[suffix_pos];
        ++suffix_pos;
        has_unit = true;
        while( suffix_pos < trimmed_value.size() ) {
            if( !std::isspace( static_cast<unsigned char>( trimmed_value[suffix_pos] ) ) ) {
                return std::nullopt;
            }
            ++suffix_pos;
        }
    }
    const auto normalized_unit = static_cast<char>( std::tolower( static_cast<unsigned char>
                                 ( unit ) ) );
    switch( normalized_unit ) {
        case 's':
            return amount * 1_seconds;
        case 'm':
            return amount * 1_minutes;
        case 'h':
            return amount * 1_hours;
        case 'd':
            return amount * 1_days;
        default:
            if( !has_unit ) { return amount * 1_minutes; }
            return std::nullopt;
    }
}

void wait()
{
    std::map<int, time_duration> durations;
    uilist as_m;
    player& u = g->u;
    bool setting_alarm = false;
    map& here = get_map();
    const auto player_vehicle = here.veh_at( u.bub_pos() );

    if( u.controlling_vehicle && player_vehicle
        && vehicle_wait::is_wait_blocked_by_movement( player_vehicle->vehicle() ) ) {
        popup( _( "You can't pass time while controlling a moving vehicle." ) );
        return;
    }

    if( u.has_alarm_clock() ) {
        int alarm_query = try_set_alarm();
        if( alarm_query == UILIST_CANCEL ) { return; }
        setting_alarm = alarm_query == 1;
    }

    const bool has_watch = u.has_watch() || setting_alarm;

    const auto add_menu_item =
        [&as_m, &durations](
            int retval, int hotkey, const std::string& caption = "",
    const time_duration& duration = time_duration::from_turns( calendar::INDEFINITELY_LONG ) ) {
        std::string text( caption );

        if( duration != time_duration::from_turns( calendar::INDEFINITELY_LONG ) ) {
            const std::string dur_str( to_string( duration ) );
            text += ( text.empty() ? dur_str : string_format( " (%s)", dur_str ) );
        }
        as_m.addentry( retval, true, hotkey, text );
        durations.emplace( retval, duration );
    };

    if( setting_alarm ) {

        add_menu_item( 0, '0', "", 30_minutes );

        for( int i = 1; i <= 9; ++i ) { add_menu_item( i, '0' + i, "", i * 1_hours ); }

    } else {
        if( g->u.get_stamina() < g->u.get_stamina_max() ) {
            as_m.addentry( 12, true, 'w', _( "Wait until you catch your breath" ) );
            durations.emplace( 12, 15_minutes ); // to hide it from showing
        }
        if( u.controlling_vehicle && player_vehicle
            && vehicle_wait::should_offer_flying_wait_durations( player_vehicle->vehicle() ) ) {
            add_menu_item( 14, 'x', "", 10_seconds );
            add_menu_item( 15, 'y', "", 30_seconds );
            add_menu_item( 16, 'z', "", 1_minutes );
        }
        add_menu_item( 1, '1', "", 5_minutes );
        add_menu_item( 2, '2', "", 30_minutes );
        add_menu_item( 3, '3', "", 1_hours );
        add_menu_item( 4, '4', "", 2_hours );
        add_menu_item( 5, '5', "", 3_hours );
        add_menu_item( 6, '6', "", 6_hours );
        as_m.addentry( 13, true, 'c', _( "Custom input" ) );
    }

    if( g->get_levz() >= 0 || has_watch ) {
        const time_point last_midnight = calendar::turn - time_past_midnight( calendar::turn );
        const auto diurnal_time_before = []( const time_point & p ) {
            // Either the given time is in the future (e.g. waiting for sunset while it's early
            // morning), than use it directly. Otherwise (in the past), add a single day to get the
            // same time tomorrow (e.g. waiting for sunrise while it's noon).
            const time_point target_time = p > calendar::turn ? p : p + 1_days;
            return target_time - calendar::turn;
        };

        add_menu_item( 7, 'd', setting_alarm ? _( "Set alarm for dawn" ) : _( "Wait till daylight" ),
                       diurnal_time_before( daylight_time( calendar::turn ) ) );
        add_menu_item( 8, 'n', setting_alarm ? _( "Set alarm for noon" ) : _( "Wait till noon" ),
                       diurnal_time_before( last_midnight + 12_hours ) );
        add_menu_item( 9, 'k', setting_alarm ? _( "Set alarm for dusk" ) : _( "Wait till night" ),
                       diurnal_time_before( night_time( calendar::turn ) ) );
        add_menu_item(
            10, 'm', setting_alarm ? _( "Set alarm for midnight" ) : _( "Wait till midnight" ),
            diurnal_time_before( last_midnight + 0_hours ) );
        if( setting_alarm ) {
            if( u.has_effect( effect_alarm_clock ) ) {
                add_menu_item( 11, 'x', _( "Cancel the currently set alarm." ), 0_turns );
            }
        } else {
            add_menu_item( 11, 'W', _( "Wait till weather changes" ) );
        }
    }

    as_m.text =
        ( has_watch ) ? string_format( _( "It's %s now. " ), to_string_time_of_day( calendar::turn ) ) : "";
    as_m.text += setting_alarm ? _( "Set alarm for when?" ) : _( "Wait for how long?" );
    as_m.query(); /* calculate key and window variables, generate window, and loop until we get a
                     valid answer */

    time_duration time_to_wait;
    if( as_m.ret == 13 ) {
        const auto duration_input =
            string_input_popup()
            .title( _( "How long?  (e.g. 10s, 15m, 1h)" ) )
            .identifier( "wait_duration" )
            .query_string();
        if( duration_input.empty() ) { return; }
        const auto parsed_duration = parse_custom_wait_duration( duration_input );
        if( !parsed_duration ) {
            add_msg( m_bad, _( "Invalid duration. Use s, m, h, or d suffixes." ) );
            return;
        }
        time_to_wait = *parsed_duration;
    } else {

        const auto dur_iter = durations.find( as_m.ret );
        if( dur_iter == durations.end() ) { return; }
        time_to_wait = dur_iter->second;
    }

    if( setting_alarm ) {
        // Setting alarm
        u.remove_effect( effect_alarm_clock );
        if( as_m.ret == 11 ) {
            add_msg( _( "You cancel your alarm." ) );
        } else {
            u.add_effect( effect_alarm_clock, time_to_wait );
            add_msg( _( "You set your alarm." ) );
        }

    } else {
        // Waiting
        if( as_m.ret == 11 ) {
            u.assign_activity(
                std::make_unique<player_activity>(
                    std::make_unique<wait_activity_actor>( wait_type::WAIT_WEATHER ) ),
                false );
        } else if( as_m.ret == 12 ) {
            auto act = std::make_unique<player_activity>(
                           std::make_unique<wait_stamina_activity_actor>( 0 ) );
            act->moves_left = to_moves<int>( time_to_wait );
            u.assign_activity( std::move( act ), false );
        } else {
            u.assign_activity(
                std::make_unique<player_activity>(
                    std::make_unique<wait_activity_actor>( wait_type::WAIT ) ),
                false );
        }
    }
}

void sleep()
{
    avatar& u = get_avatar();
    if( u.is_mounted() ) {
        u.add_msg_if_player( m_info, _( "You cannot sleep while mounted." ) );
        return;
    }
    uilist as_m;
    as_m.text = _( "<color_white>Are you sure you want to sleep?</color>" );
    // (Y)es/(S)ave before sleeping/(N)o
    as_m.entries.emplace_back( 0, true, get_option<bool>( "FORCE_CAPITAL_YN" ) ? 'Y' : 'y',
                               _( "Yes." ) );
    as_m.entries.emplace_back(
        1, g->get_moves_since_last_save(), get_option<bool>( "FORCE_CAPITAL_YN" ) ? 'S' : 's',
        _( "Yes, and save game before sleeping." ) );
    as_m.entries.emplace_back( 2, true, get_option<bool>( "FORCE_CAPITAL_YN" ) ? 'N' : 'n',
                               _( "No." ) );

    // List all active items, bionics or mutations so player can deactivate them
    std::vector<std::string> active;
    for( auto& it : u.inv_dump() ) {
        if( it->has_flag( flag_LITCIG )
            || ( it->is_active() && ( it->charges > 0 || it->units_remaining( u ) > 0 ) && it->is_tool()
                 && !it->has_flag( flag_SLEEP_IGNORE ) ) ) {
            active.push_back( it->tname() );
        }
    }
    for( const bionic& bio : *u.my_bionics ) {
        if( !bio.powered ) { continue; }

        // some bionics
        // bio_alarm is useful for waking up during sleeping
        // turning off bio_leukocyte has 'unpleasant side effects'
        if( bio.info().has_flag( STATIC( flag_id( "BIONIC_SLEEP_FRIENDLY" ) ) ) ) { continue; }

        const auto& info = bio.info();
        if( info.power_over_time > 0_kJ ) { active.push_back( info.name.translated() ); }
    }
    for( auto& mut : u.get_mutations() ) {
        const auto& mdata = mut.obj();
        if( mdata.cost > 0 && u.has_active_mutation( mut ) ) { active.push_back( mdata.name() ); }
    }

    // check for deactivating any currently played music instrument.
    for( auto& item : u.inv_dump() ) {
        if( item->is_active() && item->get_use( "musical_instrument" ) != nullptr ) {
            u.add_msg_if_player(
                _( "You stop playing your %s before trying to sleep." ), item->tname() );
            // deactivate instrument
            item->deactivate();
        }
    }

    // ask for deactivation
    std::stringstream data;
    if( !active.empty() ) {
        as_m.selected = 2;
        data << as_m.text << '\n';
        data << _( "You may want to extinguish or turn off:" ) << '\n';
        data << " " << '\n';
        for( auto& a : active ) { data << "<color_red>" << a << "</color>" << '\n'; }
        as_m.text = data.str();
    }

    /* Calculate key and window variables, generate window,
       and loop until we get a valid answer. */
    as_m.query();

    if( as_m.ret == 1 ) {
        g->quicksave();
    } else if( as_m.ret == 2 || as_m.ret < 0 ) {
        return;
    }

    time_duration try_sleep_dur = 24_hours;
    std::string deaf_text;
    // Infolink alarm is silent and works even if deaf
    if( g->u.is_deaf() && !g->u.has_bionic( bionic_id( "bio_infolink" ) ) ) {
        deaf_text = _( "<color_c_red> (DEAF!)</color>" );
    }
    if( u.has_alarm_clock() ) {
        /* Reuse menu to ask player whether they want to set an alarm. */
        bool can_hibernate = u.get_kcal_percent() > 0.95 && u.has_active_mutation( trait_HIBERNATE );

        as_m.reset();
        as_m.text =
            can_hibernate
            ? _( "You're engorged to hibernate.  The alarm would only attract attention.  "
             "Set an alarm anyway?" )
            : _( "You have an alarm clock.  Set an alarm?" );
        as_m.text += deaf_text;

        as_m.entries.emplace_back(
            0, true, get_option<bool>( "FORCE_CAPITAL_YN" ) ? 'N' : 'n',
            _( "No, don't set an alarm." ) );

        for( int i = 3; i <= 9; ++i ) {
            as_m.entries.emplace_back(
                i, true, '0' + i,
                string_format( _( "Set alarm to wake up in %i hours." ), i ) + deaf_text );
        }

        as_m.query();
        if( as_m.ret >= 3 && as_m.ret <= 9 ) {
            u.add_effect( effect_alarm_clock, 1_hours * as_m.ret );
            try_sleep_dur = 1_hours * as_m.ret + 1_turns;
        } else if( as_m.ret < 0 ) {
            return;
        }
    }

    u.moves = 0;
    avatar_funcs::try_to_sleep( u, try_sleep_dur );
}

void loot()
{
    enum ZoneFlags {
        None = 1,
        SortLoot = 2,
        FertilizePlots = 16,
        ConstructPlots = 64,
        MultiFarmPlots = 128,
        Multichoptrees = 256,
        Multichopplanks = 512,
        Multideconvehicle = 1024,
        Multirepairvehicle = 2048,
        MultiButchery = 4096,
        MultiMining = 8192
    };

    player& u = g->u;
    int flags = 0;
    auto& mgr = zone_manager::get_manager();
    const bool has_fertilizer = u.has_item_with_flag( flag_FERTILIZER );

    // Manually update vehicle cache.
    // In theory this would be handled by the related activity (activity_on_turn_move_loot())
    // but with a stale cache we never get that far.
    mgr.cache_vzones();

    flags |= g->check_near_zone( zone_type_id( "LOOT_UNSORTED" ), u.bub_pos() ) ? SortLoot : 0;
    if( g->check_near_zone( zone_type_id( "FARM_PLOT" ), u.bub_pos() ) ) {
        flags |= FertilizePlots;
        flags |= MultiFarmPlots;
    }
    flags |=
        g->check_near_zone( zone_type_id( "CONSTRUCTION_BLUEPRINT" ), u.bub_pos() ) ? ConstructPlots : 0;

    flags |= g->check_near_zone( zone_type_id( "CHOP_TREES" ), u.bub_pos() ) ? Multichoptrees : 0;
    flags |= g->check_near_zone( zone_type_id( "LOOT_WOOD" ), u.bub_pos() ) ? Multichopplanks : 0;
    flags |=
        g->check_near_zone( zone_type_id( "VEHICLE_DECONSTRUCT" ), u.bub_pos() ) ? Multideconvehicle : 0;
    flags |=
        g->check_near_zone( zone_type_id( "VEHICLE_REPAIR" ), u.bub_pos() ) ? Multirepairvehicle : 0;
    flags |= g->check_near_zone( zone_type_id( "LOOT_CORPSE" ), u.bub_pos() ) ? MultiButchery : 0;
    flags |= g->check_near_zone( zone_type_id( "MINING" ), u.bub_pos() ) ? MultiMining : 0;
    if( flags == 0 ) {
        add_msg( m_info, _( "There is no compatible zone nearby." ) );
        add_msg( m_info, _( "Compatible zones are %s and %s" ),
                 mgr.get_name_from_type( zone_type_id( "LOOT_UNSORTED" ) ),
                 mgr.get_name_from_type( zone_type_id( "FARM_PLOT" ) ) );
        return;
    }

    uilist menu;
    menu.text = _( "Pick action:" );
    menu.desc_enabled = true;

    if( flags & SortLoot ) {
        menu.addentry_desc(
            SortLoot, true, 'o', _( "Sort out my loot" ),
            _( "Sorts out the loot from Loot: Unsorted zone to nearby appropriate Loot zones.  Uses "
               "empty space in your inventory or utilizes a cart, if you are holding one." ) );
    }

    if( flags & FertilizePlots ) {
        menu.addentry_desc(
            FertilizePlots, has_fertilizer, 'f',
            !has_fertilizer
            ? _( "Fertilize plots… you don't have any fertilizer" )
            : _( "Fertilize plots" ),
            _( "Fertilize any nearby Farm: Plot zones." ) );
    }

    if( flags & ConstructPlots ) {
        menu.addentry_desc(
            ConstructPlots, true, 'c', _( "Construct plots" ),
            _( "Work on any nearby Blueprint: construction zones." ) );
    }
    if( flags & MultiFarmPlots ) {
        menu.addentry_desc(
            MultiFarmPlots, true, 'm', _( "Farm plots" ),
            _( "Till and plant on any nearby farm plots - auto-fetch seeds and tools." ) );
    }
    if( flags & Multichoptrees ) {
        menu.addentry_desc(
            Multichoptrees, true, 'C', _( "Chop trees" ),
            _( "Chop down any trees in the designated zone - auto-fetch tools." ) );
    }
    if( flags & Multichopplanks ) {
        menu.addentry_desc(
            Multichopplanks, true, 'P', _( "Chop planks" ),
            _( "Auto-chop logs in wood loot zones into planks - auto-fetch tools." ) );
    }
    if( flags & Multideconvehicle ) {
        menu.addentry_desc(
            Multideconvehicle, true, 'v', _( "Deconstruct vehicle" ),
            _( "Auto-deconstruct vehicle in designated zone - auto-fetch tools." ) );
    }
    if( flags & Multirepairvehicle ) {
        menu.addentry_desc(
            Multirepairvehicle, true, 'V', _( "Repair vehicle" ),
            _( "Auto-repair vehicle in designated zone - auto-fetch tools." ) );
    }
    if( flags & MultiButchery ) {
        menu.addentry_desc(
            MultiButchery, true, 'B', _( "Butcher corpses" ),
            _( "Auto-butcher anything in corpse loot zones - auto-fetch tools." ) );
    }
    if( flags & MultiMining ) {
        menu.addentry_desc(
            MultiMining, true, 'M', _( "Mine Area" ),
            _( "Auto-mine anything in mining zone - auto-fetch tools." ) );
    }

    menu.query();
    flags = ( menu.ret >= 0 ) ? menu.ret : None;

    switch( flags ) {
        case None:
            add_msg( _( "Never mind." ) );
            break;
        case SortLoot:
            u.assign_activity( std::make_unique<player_activity>(
                                   std::make_unique<move_loot_activity_actor>() ) );
            break;
        case FertilizePlots:
            u.assign_activity( std::make_unique<player_activity>(
                                   std::make_unique<fertilize_plot_activity_actor>() ) );
            break;
        case ConstructPlots:
            u.assign_activity( std::make_unique<player_activity>(
                                   std::make_unique<generic_multi_activity_actor>( ACT_MULTIPLE_CONSTRUCTION ) ) );
            break;
        case MultiFarmPlots:
            u.assign_activity( std::make_unique<player_activity>(
                                   std::make_unique<generic_multi_activity_actor>( ACT_MULTIPLE_FARM ) ) );
            break;
        case Multichoptrees:
            u.assign_activity( std::make_unique<player_activity>(
                                   std::make_unique<generic_multi_activity_actor>( ACT_MULTIPLE_CHOP_TREES ) ) );
            break;
        case Multichopplanks:
            u.assign_activity( std::make_unique<player_activity>(
                                   std::make_unique<generic_multi_activity_actor>( ACT_MULTIPLE_CHOP_PLANKS ) ) );
            break;
        case Multideconvehicle:
            u.assign_activity( std::make_unique<player_activity>(
                                   std::make_unique<generic_multi_activity_actor>( ACT_VEHICLE_DECONSTRUCTION ) ) );
            break;
        case Multirepairvehicle:
            u.assign_activity( std::make_unique<player_activity>(
                                   std::make_unique<generic_multi_activity_actor>( ACT_VEHICLE_REPAIR ) ) );
            break;
        case MultiButchery:
            u.assign_activity( std::make_unique<player_activity>(
                                   std::make_unique<generic_multi_activity_actor>( ACT_MULTIPLE_BUTCHER ) ) );
            break;
        case MultiMining:
            u.assign_activity( std::make_unique<player_activity>(
                                   std::make_unique<generic_multi_activity_actor>( ACT_MULTIPLE_MINE ) ) );
            break;
        default:
            debugmsg( "Unsupported flag" );
            break;
    }
}

void wear()
{
    avatar& u = g->u;
    item* loc = game_menus::inv::wear( u );

    if( loc ) {
        loc->obtain( u );
        u.wear_possessed( *loc );
    } else {
        add_msg( _( "Never mind." ) );
    }
}

void takeoff()
{
    avatar& u = g->u;
    item* loc = game_menus::inv::take_off( u );

    if( loc ) {
        loc->obtain( u );
        u.takeoff( *loc );
    } else {
        add_msg( _( "Never mind." ) );
    }
}

void read()
{
    avatar& u = g->u;
    // Can read items from inventory or within one tile (including in vehicles)
    item* loc = game_menus::inv::read( u );

    if( loc ) {
        if( loc->type->can_use( "learn_spell" ) ) {
            item& spell_book = *loc;
            spell_book.get_use( "learn_spell" )
                      ->call( u, spell_book, spell_book.is_active(), u.bub_pos() );
        } else {
            u.read( loc );
        }
    } else {
        add_msg( _( "Never mind." ) );
    }
}

void reach_attack( avatar& you )
{
    g->temp_exit_fullscreen();

    target_handler::trajectory traj = target_handler::mode_reach( you, you.primary_weapon() );

    if( !traj.empty() ) { you.reach_attack( traj.back() ); }
    g->reenter_fullscreen();
}

void fire()
{
    avatar& u = g->u;
    map& here = get_map();

    // Use vehicle turret or draw a pistol from a holster if unarmed
    if( !u.is_armed() ) {

        const optional_vpart_position vp = here.veh_at( u.abs_pos() );

        turret_data turret;
        if( vp && ( turret = vp->vehicle().turret_query( u.abs_pos() ) ) ) {
            avatar_action::fire_turret_manual( u, here, turret );
            return;
        }

        if( vp.part_with_feature( "CONTROLS", true ) && vp->vehicle().has_part( "TURRET" ) ) {
            if( vp->vehicle().turrets_aim_and_fire_mult( u, turret_filter_types::MANUAL, true ) ) {
                return;
            }
        }

        std::vector<std::string> options;
        std::vector<std::function<void()>> actions;

        bool do_autofire = false;
        for( auto& w : u.worn ) {
            if( w->type->can_use( "holster" ) && !w->has_flag( flag_NO_QUICKDRAW )
                && !w->contents.empty() && w->contents.front().is_gun() ) {
                //~ draw (first) gun contained in holster
                //~ %1$s: weapon name, %2$s: container name, %3$d: remaining ammo count
                options.push_back(
                    "Draw: "
                    + string_format(
                        pgettext( "holster", "%1$s from %2$s (%3$d)" ), w->contents.front().tname(),
                        w->type_name(), w->contents.front().ammo_remaining() ) );

                actions.emplace_back( [&] { u.invoke_item( w, "holster" ); } );

            } else if( w->is_gun() && w->has_flag( flag_WORN_GUN ) ) {
                options.push_back( "Fire: " + w->display_name() );
                actions.emplace_back( [&] { avatar_action::fire_ranged_gear( u, w ); } );
                do_autofire = true;
            } else if( w->is_gun() && w->gunmod_find( itype_shoulder_strap ) ) {
                // wield item currently worn using shoulder strap
                options.push_back( "Wield: " + w->display_name() );
                actions.emplace_back( [&] { u.wield( *w ); } );
            } else if( w->is_gun() && w->gunmod_find( itype_pistol_lanyard ) ) {
                // wield item currently worn using pistol lanyard
                options.push_back( "Wield: " + w->display_name() );
                actions.emplace_back( [&] { u.wield( *w ); } );
            }
        }
        if( !options.empty() ) {
            if( options.size() == 1 && do_autofire ) {
                actions[0]();
            } else {
                int sel = uilist( _( "Do what?" ), options );
                if( sel >= 0 ) { actions[sel](); }
            }
        }
    }

    item& weapon = u.primary_weapon();
    if( weapon.is_gun() && !weapon.gun_current_mode().melee() ) {
        avatar_action::fire_wielded_weapon( u );
    } else if( weapon.reach_range( u ) > 1 ) {
        if( u.has_effect( effect_relax_gas ) ) {
            if( one_in( 8 ) ) {
                add_msg( m_good, _( "Your willpower asserts itself, and so do you!" ) );
                reach_attack( u );
            } else {
                u.moves -= rng( 2, 8 ) * 10;
                add_msg( m_bad, _( "You're too pacified to strike anything…" ) );
            }
        } else {
            reach_attack( u );
        }
    }
}

void open_movement_mode_menu()
{
    avatar& u = g->u;
    uilist as_m;

    as_m.text = _( "Change to which movement mode?" );

    as_m.entries.emplace_back( CMM_RUN, true, 'r', _( "Run" ) );
    as_m.entries.emplace_back( CMM_WALK, true, 'w', _( "Walk" ) );
    as_m.entries.emplace_back( CMM_CROUCH, true, 'c', _( "Crouch" ) );
    as_m.entries.emplace_back( CMM_STEALTH, true, 's', _( "Stealth" ) );
    as_m.entries.emplace_back( CMM_COUNT, true, '"', _( "Cycle move mode (run/walk/crouch/stealth)" ) );
    as_m.selected = 1;
    as_m.query();

    if( as_m.ret != UILIST_CANCEL ) {
        if( as_m.ret == CMM_COUNT ) {
            u.cycle_move_mode();
        } else {
            u.set_movement_mode( static_cast<character_movemode>( as_m.ret ) );
        }
    }
}

auto start_spellcasting_activity( player& u, spell& sp ) -> void
{
    auto cast_spell = std::make_unique<player_activity>(
                          std::make_unique <
                          spellcasting_activity_actor > ( sp.id().str(), -1, false, true, sp.casting_time( u ) ) );
    if( u.magic->casting_ignore ) {
        const auto ignored_distractions = std::vector<distraction_type> {
            distraction_type::alert,
            distraction_type::noise,
            distraction_type::pain,
            distraction_type::attacked,
            distraction_type::hostile_spotted_near,
            distraction_type::hostile_spotted_far,
            distraction_type::talked_to,
            distraction_type::asthma,
            distraction_type::weather_change
        };
        for( const auto ignored : ignored_distractions ) { cast_spell->ignore_distraction( ignored ); }
    }

    u.magic->set_last_cast_spell( sp.id() );
    u.assign_activity( std::move( cast_spell ), false );
}

auto try_cast_spell( player& u, spell& sp ) -> bool
{
    if( !( sp.has_flag( spell_flag::BRAWL ) || sp.has_flag( spell_flag::PHYSICAL ) )
    && u.has_trait( trait_BRAWLER ) ) {
    add_msg( game_message_params{m_bad, gmf_bypass_cooldown}, _( "Pfft, that spell is for "
             "COWARDS, and a Brawler like "
             "you is no coward!" ) );
        return false;
    }

    const auto blockers = sp.get_blocker_muts();
    if( !blockers.empty() ) {
    for( const auto& blocker : blockers ) {
            if( u.has_trait( blocker ) ) {
                add_msg( game_message_params{m_bad, gmf_bypass_cooldown},
                         _( "Your %s mutation prevents you from casting this spell!" ),
                         blocker->name() );
                return false;
            }
        }
    }

    if( u.is_armed() && !( sp.has_flag( spell_flag::NO_HANDS ) || sp.has_flag( spell_flag::PHYSICAL ) )
        && !u.primary_weapon().has_flag( flag_MAGIC_FOCUS ) && u.primary_weapon().is_two_handed( u ) ) {
    add_msg( game_message_params{m_bad, gmf_bypass_cooldown}, _( "You need at least one hand "
             "free to cast this spell!" ) );
        return false;
    }

    if( !u.magic->has_enough_energy( u, sp ) ) {
    add_msg( game_message_params{m_bad, gmf_bypass_cooldown},
             _( "You don't have enough %s to cast the spell." ), sp.energy_string() );
        return false;
    }

    if( sp.energy_source() == hp_energy && !u.has_quality( qual_CUT ) ) {
    add_msg( game_message_params{m_bad, gmf_bypass_cooldown}, _( "You cannot cast Blood Magic "
             "without a cutting implement." ) );
        return false;
    }

    start_spellcasting_activity( u, sp );
    return true;
}

auto cast_spell() -> void
{
    auto& u = g->u;

    auto spells = u.magic->spells();

    if( spells.empty() ) {
        add_msg( game_message_params{m_bad, gmf_bypass_cooldown}, _( "You don't know any spells to "
                 "cast." ) );
        return;
    }

    auto can_cast_spells = false;
    auto has_brawler_spell = false;
    for( const auto& sp : spells ) {
        auto temp_spell = u.magic->get_spell( sp );
        if( temp_spell.can_cast( u ) ) { can_cast_spells = true; }
        if( temp_spell.has_flag( spell_flag::BRAWL ) || temp_spell.has_flag( spell_flag::PHYSICAL ) ) {
            has_brawler_spell = true;
        }
    }

    if( !can_cast_spells ) {
        add_msg( game_message_params{m_bad, gmf_bypass_cooldown}, _( "You can't cast any of the "
                 "spells you know!" ) );
        return;
    }
    if( !has_brawler_spell && u.has_trait( trait_BRAWLER ) ) {
        add_msg( game_message_params{m_bad, gmf_bypass_cooldown}, _( "You don't know any spells you "
                 "can cast as a Brawler!" ) );
        return;
    }

    const auto spell_index = u.magic->select_spell( u );
    if( spell_index < 0 ) { return; }

    auto& sp = *u.magic->get_spells()[spell_index];

    try_cast_spell( u, sp );
}

auto cast_last_spell() -> void
{
    auto& u = g->u;

    const auto last_cast_spell = u.magic->last_cast_spell();
    if( !last_cast_spell ) {
        add_msg( game_message_params{m_bad, gmf_bypass_cooldown}, _( "You haven't cast any spells "
                 "yet." ) );
        return;
    }

    auto& sp = u.magic->get_spell( *last_cast_spell );
    try_cast_spell( u, sp );
}


} // namespace action_handlers
