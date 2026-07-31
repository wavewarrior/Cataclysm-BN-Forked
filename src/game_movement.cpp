// Extracted from game.cpp — B5 decomposition (movement cluster).
// All game:: methods remain declared in game.h (unchanged).
#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "action.h"
#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "avatar.h"
#include "avatar_functions.h"
#include "calendar.h"
#include "cached_options.h"
#include "catalua_hooks.h"
#include "catalua_sol.h"
#include "character_functions.h"
#include "character_martial_arts.h"
#include "character_turn.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "debug.h"
#include "event.h"
#include "event_bus.h"
#include "field_type.h"
#include "flag.h"
#include "game_constants.h"
#include "item.h"
#include "iexamine.h"
#include "input.h"
#include "map.h"
#include "mapdata.h"
#include "messages.h"
#include "monster.h"
#include "npc.h"
#include "map_functions.h"
#include "options.h"
#include "output.h"
#include "overmapbuffer.h"
#include "player_activity.h"
#include "rng.h"
#include "sound_visualization.h"
#include "sounds.h"
#include "string_formatter.h"
#include "scent_map.h"
#include "pickup.h"
#include "timed_event.h"
#include "translations.h"
#include "trap.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "catalua_bindings_coords_common.h"
#include "vehicle_grab.h"
#include "veh_type.h"
#include "vehicle_part.h"
#include "ui_manager.h"

#include "physics/physics_world.h"
#include "coop_client.h"
#include "coop_overmap.h"
#include "coop_server.h"
#include "coop_session.h"
#include "profile.h"

#define dbg(x) DebugLogFL((x),DC::Game)

// File-local string-IDs — mirror the declarations in game.cpp
static const efftype_id effect_blind( "blind" );
static const efftype_id effect_bouldering( "bouldering" );
static const efftype_id effect_contacts( "contacts" );
static const efftype_id effect_downed( "downed" );
static const efftype_id effect_drunk( "drunk" );
static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_incorporeal( "incorporeal" );
static const efftype_id effect_jetpack( "jetpack" );
static const efftype_id effect_mending( "mending" );
static const efftype_id effect_no_sight( "no_sight" );
static const efftype_id effect_onfire( "onfire" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_ridden( "ridden" );
static const efftype_id effect_riding( "riding" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_stunned( "stunned" );
static const efftype_id effect_tied( "tied" );

static const trait_id trait_BOULDERING( "BOULDERING" );
static const trait_id trait_BURROW( "BURROW" );
static const trait_id trait_INFIMMUNE( "INFIMMUNE" );
static const trait_id trait_LEG_TENT_BRACE( "LEG_TENT_BRACE" );
static const trait_id trait_NOPAIN( "NOPAIN" );
static const trait_id trait_PROF_FERAL( "PROF_FERAL" );
static const trait_id trait_SHELL2( "SHELL2" );
static const trait_id trait_ILLITERATE( "ILLITERATE" );
static const trait_id trait_M_IMMUNE( "M_IMMUNE" );
static const trait_id trait_THICKSKIN( "THICKSKIN" );
static const trait_id trait_WEB_ROPE( "WEB_ROPE" );

static const trait_flag_str_id trait_flag_MUTATION_FLIGHT( "MUTATION_FLIGHT" );
static const trait_flag_str_id trait_flag_MUTATION_SWIM( "MUTATION_SWIM" );

static const bionic_id bio_probability_travel( "bio_probability_travel" );

static const itype_id itype_manhole_cover( "manhole_cover" );
static const itype_id itype_rm13_armor_on( "rm13_armor_on" );
static const itype_id itype_swim_fins( "swim_fins" );

static const skill_id skill_dodge( "dodge" );
static const skill_id skill_melee( "melee" );

static const trait_id trait_VINES2( "VINES2" );
static const trait_id trait_VINES3( "VINES3" );

static const itype_id itype_grapnel( "grapnel" );
static const itype_id itype_rope_30( "rope_30" );


// ——— walk_move ———
bool game::walk_move( const tripoint_bub_ms &dest_loc, const bool via_ramp )
{
    if( m.has_flag_ter( TFLAG_SMALL_PASSAGE, dest_loc ) ) {
        if( u.get_size() > creature_size::medium ) {
            add_msg( m_warning, _( "You can't fit there." ) );
            return false; // character too large to fit through a tight passage
        }
        if( u.is_mounted() ) {
            monster *mount = u.mounted_creature.get();
            if( mount->get_size() > creature_size::medium ) {
                add_msg( m_warning, _( "Your mount can't fit there." ) );
                return false; // char's mount is too large for tight passages
            }
        }
    }

    if( u.is_mounted() ) {
        auto mons = u.mounted_creature.get();
        if( mons->has_flag( MF_RIDEABLE_MECH ) ) {
            if( !mons->check_mech_powered() ) {
                add_msg( m_bad, _( "Your %s refuses to move as its batteries have been drained." ),
                         mons->get_name() );
                return false;
            }
        }
        if( !mons->move_effects( false ) ) {
            add_msg( m_bad, _( "You cannot move as your %s isn't able to move." ), mons->get_name() );
            return false;
        }
    }
    const optional_vpart_position vp_here = m.veh_at( u.bub_pos() );
    const optional_vpart_position vp_there = m.veh_at( dest_loc );

    bool pushing = false; // moving -into- grabbed tile; skip check for move_cost > 0
    bool pulling = false; // moving -away- from grabbed tile; check for move_cost > 0
    bool shifting_furniture = false; // moving furniture and staying still; skip check for move_cost > 0

    const auto dp = dest_loc - u.bub_pos();
    const auto grabbed_furn_pos = tripoint_bub_ms( u.bub_pos() + u.grab_point );

    bool grabbed = u.get_grab_type() != OBJECT_NONE;
    auto grabbed_vehicle_target = std::optional<vehicle_grab_target> {};
    if( grabbed && u.get_grab_type() == OBJECT_VEHICLE ) {
        grabbed_vehicle_target = vehicle_grab_target_at( m, u.bub_pos() + u.grab_point );
        if( grabbed_vehicle_target ) {
            u.grab_point = grabbed_vehicle_target->pos - u.bub_pos();
        }
    }
    if( grabbed ) {
        if( u.get_grab_type() == OBJECT_VEHICLE || u.get_grab_type() == OBJECT_FURNITURE ) {
            const auto horizontal_dp = tripoint_rel_ms( dp.xy(), 0 );
            const auto horizontal_grab = tripoint_rel_ms( u.grab_point.xy(), 0 );
            pushing = horizontal_dp == horizontal_grab;
            pulling = horizontal_dp == -horizontal_grab;
        } else {
            pushing = dp ==  u.grab_point;
            pulling = dp == -u.grab_point;
        }
    }
    const auto ramp_entry = tripoint_bub_ms( dest_loc.xy(), u.bub_pos().z() );
    const auto allow_furniture_z_move = grabbed && u.get_grab_type() == OBJECT_FURNITURE &&
                                        ( pushing || pulling ) && dp.z() != 0 && via_ramp &&
                                        ( ( dp.z() > 0 && m.has_flag( TFLAG_RAMP_UP, ramp_entry ) ) ||
                                          ( dp.z() < 0 && m.has_flag( TFLAG_RAMP_DOWN, ramp_entry ) ) );
    if( grabbed && u.get_grab_type() != OBJECT_VEHICLE && dest_loc.z() != u.bub_pos().z() &&
        !allow_furniture_z_move ) {
        add_msg( m_warning, _( "You let go of the grabbed object." ) );
        grabbed = false;
        u.grab( OBJECT_NONE );
    }

    // Now make sure we're actually holding something
    const vehicle *grabbed_vehicle = nullptr;
    if( grabbed && u.get_grab_type() == OBJECT_FURNITURE ) {
        // We only care about shifting, because it's the only one that can change our destination
        if( m.has_furn( grabbed_furn_pos ) ) {
            shifting_furniture = !pushing && !pulling;
        } else {
            // We were grabbing a furniture that isn't there
            grabbed = false;
        }
    } else if( grabbed && u.get_grab_type() == OBJECT_VEHICLE ) {
        if( grabbed_vehicle_target ) {
            grabbed_vehicle = &grabbed_vehicle_target->vp.vehicle();
        } else {
            // We were grabbing a vehicle that isn't there anymore
            grabbed = false;
        }
    } else if( grabbed ) {
        // We were grabbing something WEIRD, let's pretend we weren't
        grabbed = false;
    }
    if( u.grab_point != tripoint_rel_ms::zero() && !grabbed ) {
        add_msg( m_warning, _( "Can't find grabbed object." ) );
        u.grab( OBJECT_NONE );
        pushing = false;
        pulling = false;
        shifting_furniture = false;
        grabbed_vehicle = nullptr;
    }

    if( ( m.impassable( dest_loc ) && !character_funcs::can_noclip( u ) ) && !pushing &&
        !shifting_furniture ) {
        if( vp_there && u.mounted_creature && u.mounted_creature->has_flag( MF_RIDEABLE_MECH ) &&
            vp_there->vehicle().handle_potential_theft( u ) ) {
            auto diff = dest_loc - u.bub_pos();
            if( diff.x() < 0 ) {
                diff.x() -= 2;
            } else if( diff.x() > 0 ) {
                diff.x() += 2;
            }
            if( diff.y() < 0 ) {
                diff.y() -= 2;
            } else if( diff.y() > 0 ) {
                diff.y() += 2;
            }
            u.mounted_creature->shove_vehicle( dest_loc + diff.xy(),
                                               dest_loc );
        }
        return false;
    }
    if( vp_there && !vp_there->vehicle().handle_potential_theft( u ) ) {
        return false;
    }
    if( u.is_mounted() && !pushing && vp_there ) {
        add_msg( m_warning, _( "You cannot board a vehicle whilst riding." ) );
        return false;
    }
    u.set_underwater( false );

    const auto hook_results = cata::run_hooks(
                                  "on_player_try_move",
    [ &, this]( sol::table & params ) {
        params["player"] = &u;
        params["from"] = cata::detail::lua_coords::to_lua( u.bub_pos() );
        params["to"] = cata::detail::lua_coords::to_lua( dest_loc );
        params["movement_mode"] = u.get_movement_mode();
        params["via_ramp"] = via_ramp;
        if( u.is_mounted() ) {
            params["mounted"] = true;
            params["mount"] = u.mounted_creature.get();
        } else {
            params["mounted"] = false;
        }
    } );

    const auto char_hook_results = cata::run_hooks(
                                       "on_character_try_move",
    [ &, this]( sol::table & params ) {
        params["char"] = static_cast<Character *>( &u );
        params["from"] = cata::detail::lua_coords::to_lua( u.bub_pos() );
        params["to"] = cata::detail::lua_coords::to_lua( dest_loc );
        params["movement_mode"] = u.get_movement_mode();
        params["via_ramp"] = via_ramp;
        if( u.is_mounted() ) {
            params["mounted"] = true;
            params["mount"] = u.mounted_creature.get();
        } else {
            params["mounted"] = false;
        }
    } );

    if( !hook_results.get_or( "allowed", true ) ||
        !char_hook_results.get_or( "allowed", true ) ) {
        return false;
    }

    if( !shifting_furniture && !pushing && is_dangerous_tile( dest_loc ) ) {
        std::vector<std::string> harmful_stuff = get_dangerous_tile( dest_loc );
        const auto dangerous_terrain_opt = get_option<std::string>( "DANGEROUS_TERRAIN_WARNING_PROMPT" );
        const auto harmful_text = enumerate_as_string( harmful_stuff );
        const auto looks_risky = _( "Stepping into that %1$s looks risky.  %2$s" );

        const auto warn_msg = [&]( std::string_view action ) {
            add_msg( m_warning, looks_risky, harmful_text, action.data() );
        };

        if( dangerous_terrain_opt == "IGNORE" ) {
            warn_msg( _( "But you enter anyway." ) );
        } else if( dangerous_terrain_opt == "ALWAYS" && !prompt_dangerous_tile( dest_loc ) ) {
            return true;
        } else if( dangerous_terrain_opt == "RUNNING" &&
                   ( !u.movement_mode_is( CMM_RUN ) || !prompt_dangerous_tile( dest_loc ) ) ) {
            warn_msg( _( "Run into it if you wish to enter anyway." ) );
            return true;
        } else if( dangerous_terrain_opt == "CROUCHING" &&
                   ( !u.is_crouching() || !prompt_dangerous_tile( dest_loc ) ) ) {
            warn_msg( _( "Crouch and move into it if you wish to enter anyway." ) );
            return true;
        } else if( dangerous_terrain_opt == "NEVER" && !u.movement_mode_is( CMM_RUN ) ) {
            warn_msg( _( "Run into it if you wish to enter anyway." ) );
            return true;
        }
    }
    // Used to decide whether to print a 'moving is slow message
    const int mcost_from = m.move_cost( u.bub_pos() ); //calculate this _before_ calling grabbed_move

    int modifier = 0;
    if( grabbed && u.get_grab_type() == OBJECT_FURNITURE && u.bub_pos() + u.grab_point == dest_loc ) {
        modifier = -m.furn( dest_loc ).obj().movecost;
    }

    int multiplier = 1;
    if( u.is_on_ground() ) {
        multiplier *= 3;
    }

    const int mcost = m.combined_movecost( u.bub_pos(), dest_loc, grabbed_vehicle, modifier,
                                           via_ramp ) * multiplier;
    if( grabbed_move( dp, allow_furniture_z_move ) ) {
        return true;
    } else if( mcost == 0 && !character_funcs::can_noclip( u ) ) {
        return false;
    }

    bool diag = trigdist && u.bub_pos().x() != dest_loc.x() && u.bub_pos().y() != dest_loc.y();
    const int previous_moves = u.moves;
    if( u.is_mounted() ) {
        auto crit = u.mounted_creature.get();
        if( !crit->has_flag( MF_MOUNTABLE_OBSTACLES ) &&
            ( m.has_flag_ter_or_furn( "MOUNTABLE", dest_loc ) ||
              m.has_flag_ter_or_furn( "BARRICADABLE_DOOR", dest_loc ) ||
              m.has_flag_ter_or_furn( "OPENCLOSE_INSIDE", dest_loc ) ||
              m.has_flag_ter_or_furn( "BARRICADABLE_DOOR_DAMAGED", dest_loc ) ||
              m.has_flag_ter_or_furn( "BARRICADABLE_DOOR_REINFORCED", dest_loc ) ) ) {
            add_msg( m_warning, _( "You cannot pass obstacles whilst mounted." ) );
            return false;
        }

        // u.run_cost(mcost, diag) while mounted just returns mcost itself
        const double base_moves = mcost * 100.0 / crit->get_speed();
        units::mass carried_weight = crit->get_carried_weight() + u.get_weight();
        units::mass max_carry_weight = crit->weight_capacity();
        units::mass weight_overload = std::max( 0_gram, carried_weight - max_carry_weight );
        const double encumb_moves = weight_overload / 5_kilogram;

        u.moves -= static_cast<int>( std::ceil( base_moves + encumb_moves ) );
        if( u.movement_mode_is( CMM_WALK ) ) {
            crit->use_mech_power( -2 );
        } else if( u.is_crouching() ) {
            crit->use_mech_power( -1 );
        } else if( u.movement_mode_is( CMM_RUN ) ) {
            crit->use_mech_power( -3 );
        }
    } else {
        u.moves -= u.run_cost( mcost, diag );
        /**
        TODO:
        This should really use the mounted creatures stamina, if mounted.
        Monsters don't currently have stamina however.
        For the time being just don't burn players stamina when mounted.
        */
        if( grabbed_vehicle == nullptr || grabbed_vehicle->wheelcache.empty() ) {
            //Burn normal amount of stamina if no vehicle grabbed or vehicle lacks wheels
            if( character_funcs::can_fly( get_avatar() ) &&
                get_map().ter( u.bub_pos() ).id().str() == "t_open_air" ) {
                // add flying flavor text here
                for( const trait_id &tid : u.get_mutations() ) {
                    const mutation_branch &mdata = tid.obj();
                    if( mdata.flags.contains( trait_flag_MUTATION_FLIGHT ) ) {
                        u.mutation_spend_resources( tid );
                    }
                }

            }
            u.burn_move_stamina( previous_moves - u.moves );
        } else {
            //Burn half as much stamina if vehicle has wheels, without changing move time
            u.burn_move_stamina( 0.50 * ( previous_moves - u.moves ) );
        }
    }
    // Max out recoil & reset aim point
    u.recoil = MAX_RECOIL;
    u.last_target_pos = std::nullopt;

    // Print a message if movement is slow
    const int mcost_to = m.move_cost( dest_loc ); //calculate this _after_ calling grabbed_move
    const bool fungus = m.has_flag_ter_or_furn( "FUNGUS", u.bub_pos() ) ||
                        m.has_flag_ter_or_furn( "FUNGUS",
                            dest_loc ); //fungal furniture has no slowing effect on mycus characters
    const bool slowed = ( ( u.mutation_value( "movecost_obstacle_modifier" ) > 0.5f && ( mcost_to > 2 ||
                            mcost_from > 2 ) ) ||
                          mcost_to > 4 || mcost_from > 4 ) &&
                        !( u.has_trait( trait_M_IMMUNE ) && fungus );
    if( slowed && !u.is_mounted() ) {
        // Unless u.bub_pos() has a higher movecost than dest_loc, state that dest_loc is the cause
        if( mcost_to >= mcost_from ) {
            if( auto displayed_part = vp_there.part_displayed() ) {
                add_msg( m_warning, _( "Moving onto this %s is slow!" ),
                         displayed_part->part().name() );
                sfx::do_obstacle( displayed_part->part().info().get_id().str() );
            } else {
                add_msg( m_warning, _( "Moving onto this %s is slow!" ), m.name( dest_loc ) );
                sfx::do_obstacle( m.ter( dest_loc ).id().str() );
            }
        } else {
            if( auto displayed_part = vp_here.part_displayed() ) {
                add_msg( m_warning, _( "Moving off of this %s is slow!" ),
                         displayed_part->part().name() );
                sfx::do_obstacle( displayed_part->part().info().get_id().str() );
            } else {
                add_msg( m_warning, _( "Moving off of this %s is slow!" ), m.name( u.bub_pos() ) );
                sfx::do_obstacle( m.ter( u.bub_pos() ).id().str() );
            }
        }
    }
    if( !u.is_mounted() && u.has_trait( trait_id( "LEG_TENT_BRACE" ) ) &&
        ( !u.footwear_factor() ||
          ( u.footwear_factor() == .5 && one_in( 2 ) ) ) ) {
        // DX and IN are long suits for Cephalopods,
        // so this shouldn't cause too much hardship
        // Presumed that if it's swimmable, they're
        // swimming and won't stick
        ///\EFFECT_DEX decreases chance of tentacles getting stuck to the ground

        ///\EFFECT_INT decreases chance of tentacles getting stuck to the ground
        if( !m.has_flag( "SWIMMABLE", dest_loc ) && one_in( 80 + u.dex_cur + u.int_cur ) ) {
            add_msg( _( "Your tentacles stick to the ground, but you pull them free." ) );
            u.mod_fatigue( 1 );
        }
    }
    if( !u.has_artifact_with( AEP_STEALTH ) && !u.has_trait( trait_id( "DEBUG_SILENT" ) ) ) {
        int volume = u.is_stealthy() ? 3 : 6;
        volume *= u.mutation_value( "noise_modifier" );
        if( volume > 0 ) {
            if( u.is_wearing( itype_rm13_armor_on ) ) {
                volume = 2;
            } else if( u.has_bionic( bionic_id( "bio_ankles" ) ) ) {
                volume = 12;
            }
            if( u.movement_mode_is( CMM_RUN ) ) {
                volume *= 1.5;
            } else if( u.is_crouching() ) {
                volume /= 2;
            }
            if( u.is_mounted() ) {
                auto mons = u.mounted_creature.get();
                switch( mons->get_size() ) {
                    case creature_size::tiny:
                        volume = 0; // No sound for the tinies
                        break;
                    case creature_size::small:
                        volume /= 3;
                        break;
                    case creature_size::medium:
                        break;
                    case creature_size::large:
                        volume *= 1.5;
                        break;
                    case creature_size::huge:
                        volume *= 2;
                        break;
                    default:
                        break;
                }
                if( mons->has_flag( MF_LOUDMOVES ) ) {
                    volume += 6;
                }
                sounds::sound( dest_loc, volume, sounds::sound_t::movement, mons->type->get_footsteps(), true,
                               "none", "none" );
            } else {
                sounds::sound( dest_loc, volume, sounds::sound_t::movement, _( "footsteps" ), true,
                               "none", "none" );    // Sound of footsteps may awaken nearby monsters
            }
            sfx::do_footstep();
            sfx::emit_sound_pulse( u.bub_pos(), 3.0f );
        }

        if( one_in( 20 ) && u.has_artifact_with( AEP_MOVEMENT_NOISE ) ) {
            sounds::sound( u.bub_pos(), 40, sounds::sound_t::movement, _( "a rattling sound." ), true,
                           "misc", "rattling" );
        }
    }

    if( m.has_flag_ter_or_furn( TFLAG_HIDE_PLACE, dest_loc ) &&
        u.get_size() <= creature_size::medium ) {
        add_msg( m_good, _( "You are hiding in the %s." ), m.name( dest_loc ) );
    }

    if( dest_loc != u.bub_pos() ) {
        //cata_event_dispatch::avatar_moves( u, m, dest_loc );
    }

    auto oldpos = u.bub_pos();
    const auto keep_grab = u.get_grab_type() == OBJECT_VEHICLE || allow_furniture_z_move;
    auto submap_shift = place_player( dest_loc, keep_grab );
    auto ms_shift = project_to<coords::ms>( submap_shift );
    oldpos = oldpos - ms_shift;
    // Sprite-animation move trigger (both endpoints now in the post-shift frame).
    u.anim_on_move( oldpos, u.bub_pos() );

    if( pulling && u.get_grab_type() == OBJECT_FURNITURE ) {
        const auto shifted_furn_pos = grabbed_furn_pos - ms_shift;
        const auto shifted_furn_dest = tripoint_bub_ms( u.bub_pos() + u.grab_point );
        const auto fire_age = m.get_field_age( shifted_furn_pos, fd_fire );
        const auto fire_intensity = m.get_field_intensity( shifted_furn_pos, fd_fire );
        m.remove_field( shifted_furn_pos, fd_fire );
        m.set_field_intensity( shifted_furn_dest, fd_fire, fire_intensity );
        m.set_field_age( shifted_furn_dest, fd_fire, fire_age );
    }

    if( u.is_hauling() ) {
        start_hauling( oldpos );
    }

    on_move_effects();

    return true;
}


// ——— place_player ———
auto game::place_player( const tripoint_bub_ms &dest_loc, const bool keep_grab ) -> point_rel_sm
{
    const optional_vpart_position vp1 = m.veh_at( dest_loc );
    if( const std::optional<std::string> label = vp1.get_label() ) {
        add_msg( m_info, _( "Label here: %s" ), *label );
    }
    std::string signage = m.get_signage( dest_loc );
    if( !signage.empty() ) {
        if( !u.has_trait( trait_ILLITERATE ) ) {
            add_msg( m_info, _( "The sign says: %s" ), signage );
        } else {
            add_msg( m_info, _( "There is a sign here, but you are unable to read it." ) );
        }
    }
    if( m.has_graffiti_at( dest_loc ) ) {
        if( !u.has_trait( trait_ILLITERATE ) ) {
            add_msg( m_info, _( "Written here: %s" ), m.graffiti_at( dest_loc ) );
        } else {
            add_msg( m_info, _( "Something is written here, but you are unable to read it." ) );
        }
    }
    // TODO: Move the stuff below to a Character method so that NPCs can reuse it
    if( m.has_flag( "ROUGH", dest_loc ) && ( !u.in_vehicle ) && ( !u.is_mounted() ) ) {
        if( one_in( 5 ) && u.get_armor_bash( bodypart_id( "foot_l" ) ) < rng( 2, 5 ) ) {
            add_msg( m_bad, _( "You hurt your left foot on the %s!" ),
                     m.has_flag_ter( "ROUGH", dest_loc ) ? m.tername( dest_loc ) : m.furnname(
                         dest_loc ) );
            u.deal_damage( nullptr, bodypart_id( "foot_l" ), damage_instance( DT_CUT, 1 ) );
        }
        if( one_in( 5 ) && u.get_armor_bash( bodypart_id( "foot_r" ) ) < rng( 2, 5 ) ) {
            add_msg( m_bad, _( "You hurt your right foot on the %s!" ),
                     m.has_flag_ter( "ROUGH", dest_loc ) ? m.tername( dest_loc ) : m.furnname(
                         dest_loc ) );
            u.deal_damage( nullptr, bodypart_id( "foot_l" ), damage_instance( DT_CUT, 1 ) );
        }
    }
    ///\EFFECT_DEX increases chance of avoiding cuts on sharp terrain
    if( m.has_flag( "SHARP", dest_loc ) && !one_in( 3 ) && !x_in_y( 1 + u.dex_cur / 2.0, 40 ) &&
        ( !u.in_vehicle && !m.veh_at( dest_loc ) ) &&
        ( u.mutation_value( "movecost_obstacle_modifier" ) > 0.5f ||
          one_in( 4 ) ) && ( u.has_trait( trait_THICKSKIN ) ? !one_in( 8 ) : true ) ) {
        if( u.is_mounted() ) {
            add_msg( _( "Your %s gets cut!" ), u.mounted_creature->get_name() );
            u.mounted_creature->apply_damage( nullptr, bodypart_id( "torso" ), rng( 1, 10 ) );
        } else {
            const bodypart_id bp = u.get_random_body_part();
            if( u.deal_damage( nullptr, bp, damage_instance( DT_CUT, rng( 1, 10 ) ) ).total_damage() > 0 ) {
                //~ 1$s - bodypart name in accusative, 2$s is terrain name.
                add_msg( m_bad, _( "You cut your %1$s on the %2$s!" ),
                         body_part_name_accusative( bp->token ),
                         m.has_flag_ter( "SHARP", dest_loc ) ? m.tername( dest_loc ) : m.furnname(
                             dest_loc ) );
            }
        }
    }
    if( m.has_flag( "UNSTABLE", dest_loc ) && !u.is_mounted() ) {
        u.add_effect( effect_bouldering, 1_turns, bodypart_str_id::NULL_ID() );
    } else if( u.has_effect( effect_bouldering ) ) {
        u.remove_effect( effect_bouldering );
    }
    if( m.has_flag_ter_or_furn( TFLAG_NO_SIGHT, dest_loc ) ) {
        u.add_effect( effect_no_sight, 1_turns, bodypart_str_id::NULL_ID() );
    } else if( u.has_effect( effect_no_sight ) ) {
        u.remove_effect( effect_no_sight );
    }

    // If we moved out of the nonant, we need update our map data
    if( m.has_flag( "SWIMMABLE", dest_loc ) && u.has_effect( effect_onfire ) ) {
        add_msg( _( "The water puts out the flames!" ) );
        u.remove_effect( effect_onfire );
        if( u.is_mounted() ) {
            monster *mon = u.mounted_creature.get();
            if( mon->has_effect( effect_onfire ) ) {
                mon->remove_effect( effect_onfire );
            }
        }
    }

    if( monster *const mon_ptr = critter_at<monster>( dest_loc ) ) {
        // We displaced a monster. It's probably a bug if it wasn't a friendly mon...
        // Immobile monsters can't be displaced.
        monster &critter = *mon_ptr;
        // TODO: handling for ridden creatures other than players mount.
        if( !critter.has_effect( effect_ridden ) ) {
            if( u.is_mounted() ) {
                std::vector<tripoint_bub_ms> maybe_valid;
                for( const tripoint_bub_ms &jk : m.points_in_radius( critter.bub_pos(), 1 ) ) {
                    if( is_empty( jk ) ) {
                        maybe_valid.push_back( jk );
                    }
                }
                bool moved = false;
                while( !maybe_valid.empty() ) {
                    if( critter.move_to( random_entry_removed( maybe_valid ) ) ) {
                        add_msg( _( "You push the %s out of the way." ), critter.name() );
                        moved = true;
                    }
                }
                if( !moved ) {
                    add_msg( _( "There is no room to push the %s out of the way." ), critter.name() );
                    return point_rel_sm::zero();
                }
            } else {
                // Force the movement even though the player is there right now.
                const bool moved = critter.move_to( u.bub_pos(), /*force=*/false, /*step_on_critter=*/true );
                if( moved ) {
                    add_msg( _( "You displace the %s." ), critter.name() );
                } else {
                    add_msg( _( "You cannot move the %s out of the way." ), critter.name() );
                    return point_rel_sm::zero();;
                }
            }
        } else if( !u.has_effect( effect_riding ) ) {
            add_msg( _( "You cannot move the %s out of the way." ), critter.name() );
            return point_rel_sm::zero();
        }
    }

    // If the player is in a vehicle, unboard them from the current part
    if( u.in_vehicle ) {
        m.unboard_vehicle( u.bub_pos() );
    }
    // Move the player
    // Start with z-level, to make it less likely that old functions (2D ones) freak out
    if( m.has_zlevels() && dest_loc.z() != get_levz() ) {
        vertical_shift( dest_loc.z(), keep_grab );
    }

    if( u.is_hauling() && ( !m.can_put_items( dest_loc ) ||
                            m.has_flag( TFLAG_DEEP_WATER, dest_loc ) ||
                            vp1 ) ) {
        u.stop_hauling();
    }
    u.setpos( dest_loc );
    if( u.is_mounted() ) {
        monster *mon = u.mounted_creature.get();
        mon->setpos( dest_loc );
        mon->process_triggers();
        m.creature_in_field( *mon );
    }
    point_rel_sm submap_shift = update_map( u );
    // Mark the player's submap dirty so the per-submap lightmap rebuild
    // picks up the character light and any static sources at the new position.
    // For shifts, update_map+loadn already marks the 14 new-edge submaps —
    // this handles within-submap moves that don't trigger a shift.
    m.mark_lightmap_dirty( u.bub_pos() );
    // Important: don't use dest_loc after this line. `update_map` may have shifted the map
    // and dest_loc was not adjusted and therefore is still in the un-shifted system and probably wrong.
    // If you must use it you can calculate the position in the new, shifted system with
    // adjusted_pos = ( old_pos.x - submap_shift.x * SEEX, old_pos.y - submap_shift.y * SEEY, old_pos.z )

    //Auto pulp or butcher and Auto foraging
    if( get_option<bool>( "AUTO_FEATURES" ) && mostseen == 0  && !u.is_mounted() ) {
        static const direction adjacentDir[8] = { direction::NORTH, direction::NORTHEAST, direction::EAST, direction::SOUTHEAST, direction::SOUTH, direction::SOUTHWEST, direction::WEST, direction::NORTHWEST };

        const std::string forage_type = get_option<std::string>( "AUTO_FORAGING" );
        if( forage_type != "off" ) {
            const auto forage = [&]( const tripoint_bub_ms & pos ) {
                const auto &xter_t = m.ter( pos ).obj().examine;
                const auto &xfurn_t = m.furn( pos ).obj().examine;
                const bool forage_everything = forage_type == "both";
                const bool forage_bushes = forage_everything || forage_type == "bushes";
                const bool forage_trees = forage_everything || forage_type == "trees";
                const bool forage_flowers = forage_everything || forage_type == "flowers";
                if( xter_t == &iexamine::none && xfurn_t == &iexamine::none ) {
                    return;
                } else if( ( forage_bushes && xter_t == &iexamine::shrub_marloss ) ||
                           ( forage_bushes && xter_t == &iexamine::shrub_wildveggies ) ||
                           ( forage_bushes && xter_t == &iexamine::harvest_ter_nectar ) ||
                           ( forage_trees && xter_t == &iexamine::tree_marloss ) ||
                           ( forage_trees && xter_t == &iexamine::harvest_ter ) ||
                           ( forage_trees && xter_t == &iexamine::harvest_ter_nectar )
                         ) {
                    xter_t( u, pos );
                } else if( ( ( forage_flowers && xfurn_t == &iexamine::harvest_furn ) ||
                             ( forage_flowers && xfurn_t == &iexamine::harvest_furn_nectar ) ||
                             ( forage_everything && xfurn_t == &iexamine::harvest_furn ) ||
                             ( forage_everything && xfurn_t == &iexamine::harvest_furn_nectar )
                           ) ) {
                    xfurn_t( u, pos );
                }
            };

            for( auto &elem : adjacentDir ) {
                forage( u.bub_pos() + displace_XY( elem ) );
            }
        }

        const std::string pulp_butcher = get_option<std::string>( "AUTO_PULP_BUTCHER" );
        if( pulp_butcher == "butcher" && u.max_quality( quality_id( "BUTCHER" ) ) > INT_MIN ) {
            std::vector<item *> corpses;

            for( item * const &it : m.i_at( u.bub_pos() ) ) {
                corpses.push_back( it );
            }

            if( !corpses.empty() ) {
                u.assign_activity( std::make_unique<player_activity>(
                                       std::make_unique<butchery_activity_actor>(
                                           BUTCHER, corpses, m.bub_to_abs( u.bub_pos() )
                                       )
                                   ) );
            }
        } else if( pulp_butcher == "pulp" || pulp_butcher == "pulp_adjacent" ) {
            const auto pulp = [&]( const tripoint_bub_ms & pos ) {
                for( const auto &maybe_corpse : m.i_at( pos ) ) {
                    if( maybe_corpse->is_corpse() && maybe_corpse->can_revive() &&
                        !maybe_corpse->get_mtype()->bloodType().obj().has_acid ) {
                        u.assign_activity( std::make_unique<player_activity>(
                                               std::make_unique<pulp_activity_actor>(
                                                   m.bub_to_abs( pos ), "auto_pulp_no_acid" ) ) );
                        u.activity->moves_left = calendar::INDEFINITELY_LONG;
                        u.activity->auto_resume = true;
                        return;
                    }
                }
            };

            if( pulp_butcher == "pulp_adjacent" ) {
                for( auto &elem : adjacentDir ) {
                    pulp( u.bub_pos() + displace_XY( elem ) );
                }
            } else {
                pulp( u.bub_pos() );
            }
        }
    }

    //Autopickup
    if( !u.is_mounted() && get_option<bool>( "AUTO_PICKUP" ) && !u.is_hauling() &&
        ( !get_option<bool>( "AUTO_PICKUP_SAFEMODE" ) || mostseen == 0 ) &&
        ( m.has_items( u.bub_pos() ) || get_option<bool>( "AUTO_PICKUP_ADJACENT" ) ) ) {
        pickup::pick_up( u.bub_pos(), -1 );
    }

    // If the new tile is a boardable part, board it
    if( vp1.part_with_feature( "BOARDABLE", true ) && !u.is_mounted() ) {
        m.board_vehicle( u.bub_pos(), &u );
    }

    // Traps!
    // Try to detect.
    character_funcs::search_surroundings( u );
    if( u.is_mounted() ) {
        m.creature_on_trap( *u.mounted_creature );
    } else {
        m.creature_on_trap( u );
    }
    // Drench the player if swimmable
    if( m.has_flag( "SWIMMABLE", u.bub_pos() ) &&
        !( u.is_mounted() || ( u.in_vehicle && vp1->vehicle().can_float() ) ) ) {
        u.drench( 40, { { bodypart_str_id( "foot_l" ), bodypart_str_id( "foot_r" ), bodypart_str_id( "leg_l" ), bodypart_str_id( "leg_r" ) } },
        false );
    }

    // List items here
    if( !m.has_flag( "SEALED", u.bub_pos() ) ) {
        if( get_option<bool>( "NO_AUTO_PICKUP_ZONES_LIST_ITEMS" ) ||
            !check_zone( zone_type_id( "NO_AUTO_PICKUP" ), u.bub_pos() ) ) {
            if( u.is_blind() && !m.i_at( u.bub_pos() ).empty() && u.clairvoyance() < 1 ) {
                add_msg( _( "There's something here, but you can't see what it is." ) );
            } else if( m.has_items( u.bub_pos() ) ) {
                std::vector<std::string> names;
                std::vector<size_t> counts;
                std::vector<item *> items;
                for( auto &tmpitem : m.i_at( u.bub_pos() ) ) {

                    std::string next_tname = tmpitem->tname();
                    std::string next_dname = tmpitem->display_name();
                    bool by_charges = tmpitem->count_by_charges();
                    bool got_it = false;
                    for( size_t i = 0; i < names.size(); ++i ) {
                        if( by_charges && next_tname == names[i] ) {
                            counts[i] += tmpitem->charges;
                            got_it = true;
                            break;
                        } else if( next_dname == names[i] ) {
                            counts[i] += 1;
                            got_it = true;
                            break;
                        }
                    }
                    if( !got_it ) {
                        if( by_charges ) {
                            names.push_back( tmpitem->tname( tmpitem->charges ) );
                            counts.push_back( tmpitem->charges );
                        } else {
                            names.push_back( tmpitem->display_name( 1 ) );
                            counts.push_back( 1 );
                        }
                        items.push_back( tmpitem );
                    }
                    if( names.size() > 10 ) {
                        break;
                    }
                }
                for( size_t i = 0; i < names.size(); ++i ) {
                    if( !items[i]->count_by_charges() ) {
                        names[i] = items[i]->display_name( counts[i] );
                    } else {
                        names[i] = items[i]->tname( counts[i] );
                    }
                }
                int and_the_rest = 0;
                for( size_t i = 0; i < names.size(); ++i ) {
                    //~ number of items: "<number> <item>"
                    std::string fmt = vgettext( "%1$d %2$s", "%1$d %2$s", counts[i] );
                    names[i] = string_format( fmt, counts[i], names[i] );
                    // Skip the first two.
                    if( i > 1 ) {
                        and_the_rest += counts[i];
                    }
                }
                if( names.size() == 1 ) {
                    add_msg( _( "You see here %s." ), names[0] );
                } else if( names.size() == 2 ) {
                    add_msg( _( "You see here %s and %s." ), names[0], names[1] );
                } else if( names.size() == 3 ) {
                    add_msg( _( "You see here %s, %s, and %s." ), names[0], names[1], names[2] );
                } else if( and_the_rest < 7 ) {
                    add_msg( vgettext( "You see here %s, %s and %d more item.",
                                       "You see here %s, %s and %d more items.",
                                       and_the_rest ),
                             names[0], names[1], and_the_rest );
                } else {
                    add_msg( _( "You see here %s and many more items." ), names[0] );
                }
            }
        }
    }

    if( ( vp1.part_with_feature( "CONTROL_ANIMAL", true ) ||
          vp1.part_with_feature( "CONTROLS", true ) ) && u.in_vehicle && !u.is_mounted() ) {
        add_msg( _( "There are vehicle controls here." ) );
        if( !u.has_trait( trait_id( "WAYFARER" ) ) ) {
            add_msg( m_info, _( "%s to drive." ), press_x( ACTION_CONTROL_VEHICLE ) );
        }
    } else if( vp1.part_with_feature( "CONTROLS", true ) && u.in_vehicle &&
               u.is_mounted() ) {
        add_msg( _( "There are vehicle controls here but you cannot reach them whilst mounted." ) );
    }
    return submap_shift;
}


// ——— phasing_move ———
bool game::phasing_move( const tripoint_bub_ms &dest_loc, const bool via_ramp )
{
    if( dest_loc.z() != u.bub_pos().z() && !via_ramp ) {
        // No vertical phasing yet
        return false;
    }

    //probability travel through walls but not water
    auto dest = dest_loc;
    // tile is impassable
    int tunneldist = 0;
    const point d( sgn( dest.x() - u.bub_pos().x() ), sgn( dest.y() - u.bub_pos().y() ) );
    while( m.impassable( dest ) ||
           ( critter_at( dest ) != nullptr && tunneldist > 0 ) ) {
        //add 1 to tunnel distance for each impassable tile in the line
        tunneldist += 1;
        //Being dimensionally anchored prevents quantum shenanigans.
        if( u.worn_with_flag( flag_DIMENSIONAL_ANCHOR ) ||
            u.has_effect_with_flag( flag_DIMENSIONAL_ANCHOR ) ) {
            u.add_msg_if_player( m_info,
                                 _( "You try to quantum tunnel through the barrier, but something holds you back!" ) );
            return false;
        }
        if( tunneldist > 24 ) {
            add_msg( m_info, _( "It's too dangerous to tunnel that far!" ) );
            return false;
        }

        dest.x() += d.x;
        dest.y() += d.y;
    }

    units::energy power_cost = bio_probability_travel->power_activate;

    if( tunneldist != 0 ) {
        // -1 because power_cost for the first tile was already taken up by the bionic's activation
        if( ( tunneldist - 1 ) * power_cost > u.get_power_level() ) {
            // oops, not enough energy! Tunneling costs set amount of bionic power per impassable tile
            if( tunneldist * power_cost > u.get_max_power_level() ) {
                add_msg( _( "You try to quantum tunnel through the barrier but bounce off!  You don't have enough bionic power capacity to travel that far." ) );
            } else {
                add_msg( _( "You try to quantum tunnel through the barrier but are reflected!  You need %s of bionic power to travel that thickness of material." ),
                         units::display( power_cost * tunneldist ) );
            }
            return false;
        }

        if( u.in_vehicle ) {
            m.unboard_vehicle( u.bub_pos() );
        }

        add_msg( _( "You quantum tunnel through the %d-tile wide barrier!" ), tunneldist );
        //tunneling costs 100 bionic power per impassable tile, but the first 100 was already drained by activation.
        u.mod_power_level( -( ( tunneldist - 1 ) * power_cost ) );
        //tunneling costs 100 moves baseline, 50 per extra tile up to a cap of 500 moves
        u.moves -= ( 50 + ( tunneldist * 50 ) );
        u.setpos( dest );
        m.invalidate_lightmap_caches();

        if( m.veh_at( u.bub_pos() ).part_with_feature( "BOARDABLE", true ) ) {
            m.board_vehicle( u.bub_pos(), &u );
        }

        u.grab( OBJECT_NONE );
        on_move_effects();
        m.creature_on_trap( u );
        return true;
    }

    return false;
}


// Furniture movement types (defined here with their helpers)
enum class furniture_vertical_direction {
    none,
    up,
    down,
};

struct furniture_move_effort {
    int str_req = 0;
    int adjusted_str = 0;
    int move_cost = 0;
    int stamina_cost = 0;
};

struct furniture_move_effort_options {
    const avatar &you;
    map &here;
    tripoint_bub_ms from;
    tripoint_bub_ms to;
};

// ——— furniture_statics ———

static auto multiply_ratio_round_up( const int value, const int numerator,
                                     const int denominator ) -> int
{
    const auto safe_denominator = std::max( 1, denominator );
    return ( value * numerator + safe_denominator - 1 ) / safe_denominator;
}

static auto furniture_vertical_direction_for( const tripoint_bub_ms &from,
        const tripoint_bub_ms &to ) -> furniture_vertical_direction
{
    if( to.z() > from.z() ) {
    return furniture_vertical_direction::up;
}

if( to.z() < from.z() ) {
    return furniture_vertical_direction::down;
}

return furniture_vertical_direction::none;
}

static auto ramp_adjusted_furniture_destination( map &here, const tripoint_bub_ms &from,
        const tripoint_rel_ms &horizontal_dp ) -> tripoint_bub_ms
{
    auto dest = tripoint_bub_ms( from + horizontal_dp );

    // Only cross when `dest` carries a flag `from` didn't already have.  A
    // dragged object's own from/to pair is the only ramp-crossing memory it
    // has (unlike the player, who is guaranteed by construction to already
    // be standing on the tile *before* the flagged one, so a fresh
    // destination-only check never double-fires for them).  Guarding on
    // `from` here gives dragged furniture that same "already consumed this
    // step" awareness instead of blindly re-deriving the crossing from
    // `dest` alone.
    if( here.has_flag( TFLAG_RAMP_UP, dest ) && !here.has_flag( TFLAG_RAMP_UP, from ) &&
        here.inbounds_z( dest.z() + 1 ) ) {
        dest.z() += 1;
    } else if( here.has_flag( TFLAG_RAMP_DOWN, dest ) && !here.has_flag( TFLAG_RAMP_DOWN, from ) &&
               here.inbounds_z( dest.z() - 1 ) ) {
        dest.z() -= 1;
    }

    return dest;
}

static auto is_ramp_tile_or_mate( const map &here, const tripoint_bub_ms &pos ) -> bool
{
    if( here.has_flag( TFLAG_RAMP, pos ) || here.has_flag( TFLAG_RAMP_UP, pos ) ||
    here.has_flag( TFLAG_RAMP_DOWN, pos ) ) {
    return true;
}

const auto above = pos + tripoint_above;
const auto below = pos + tripoint_below;
return ( here.inbounds_z( above.z() ) && here.has_flag( TFLAG_RAMP_DOWN, above ) ) ||
       ( here.inbounds_z( below.z() ) && here.has_flag( TFLAG_RAMP_UP, below ) );
}

static auto furniture_drag_strength( const avatar &you ) -> int
{
    auto adjusted_str = you.get_str();
    if( you.is_mounted() ) {
        auto *mons = you.mounted_creature.get();
        if( mons->has_flag( MF_RIDEABLE_MECH ) && mons->mech_str_addition() != 0 ) {
            adjusted_str = mons->mech_str_addition();
        }
    }

    return adjusted_str;
}

static auto furniture_contents_strength_req( map &here, const tripoint_bub_ms &pos ) -> int
{
    auto furniture_contents_weight = 0_gram;
    for( const auto &contained_item : here.i_at( pos ) ) {
        furniture_contents_weight += contained_item->weight();
    }

    return furniture_contents_weight / 4_kilogram;
}

static auto furniture_move_effort_for(
    const furniture_move_effort_options &options ) -> furniture_move_effort
{
    const auto vertical_direction = furniture_vertical_direction_for( options.from, options.to );
    const auto &furntype = options.here.furn( options.from ).obj();
    auto str_req = std::max( 0, furntype.move_str_req +
                             furniture_contents_strength_req( options.here, options.from ) );

    if( vertical_direction == furniture_vertical_direction::up ) {
        str_req = multiply_ratio_round_up( str_req, 3, 2 );
    }

    const auto adjusted_str = furniture_drag_strength( options.you );
    auto move_cost = str_req * 10;
    constexpr auto dresser_strength_anchor = 8;
    auto stamina_cost = multiply_ratio_round_up(
                            get_option<int>( "PLAYER_BASE_STAMINA_BURN_RATE" ) * 7,
                            str_req, dresser_strength_anchor );
    stamina_cost = multiply_ratio_round_up( stamina_cost, std::max( 1, str_req ),
                                            std::max( std::max( 1, str_req ), adjusted_str ) );

    if( vertical_direction != furniture_vertical_direction::none ) {
        move_cost *= 2;
        stamina_cost = multiply_ratio_round_up( stamina_cost, 3, 2 );
    }

    return furniture_move_effort{
        .str_req = str_req,
        .adjusted_str = adjusted_str,
        .move_cost = move_cost,
        .stamina_cost = stamina_cost,
    };
}


// ——— grabbed_furn_move ———
auto game::grabbed_furn_move( const tripoint_rel_ms &dp ) -> bool
{
    // Furniture: pull, push, or standing still and nudging object around.
    // Can push furniture out of reach.
    const auto fpos = tripoint_bub_ms( u.bub_pos() + u.grab_point );
    // supposed position of grabbed furniture
    if( !m.has_furn( fpos ) ) {
        // Where did it go? We're grabbing thin air so reset.
        add_msg( m_info, _( "No furniture at grabbed point." ) );
        u.grab( OBJECT_NONE );
        return false;
    }

    const auto horizontal_dp = tripoint_rel_ms( dp.xy(), 0 );
    const auto horizontal_grab = tripoint_rel_ms( u.grab_point.xy(), 0 );
    const auto pushing_furniture = horizontal_dp ==  horizontal_grab;
    const auto pulling_furniture = horizontal_dp == -horizontal_grab;
    const auto shifting_furniture = !pushing_furniture && !pulling_furniture;

    const auto furniture_dp = pushing_furniture || pulling_furniture ? horizontal_dp : dp;
    const auto fdest = ramp_adjusted_furniture_destination( m, fpos, furniture_dp );
    const auto ramp_drag = fdest.z() != fpos.z() || dp.z() != 0 ||
                           is_ramp_tile_or_mate( m, fdest );
    // Check floor: floorless tiles don't need to be flat and have no traps
    const auto has_floor = m.has_floor( fdest );
    // Unfortunately, game::is_empty fails for tiles we're standing on,
    // which will forbid pulling, so:
    const auto canmove = (
                             m.passable( fdest ) &&
                             critter_at<npc>( fdest ) == nullptr &&
                             critter_at<monster>( fdest ) == nullptr &&
                             ( !pulling_furniture || is_empty( u.bub_pos() + dp ) ) &&
                             ( !has_floor || m.has_flag( "FLAT", fdest ) || ramp_drag ) &&
                             !m.has_furn( fdest ) &&
                             !m.veh_at( fdest ) &&
                             ( !has_floor || m.tr_at( fdest ).is_null() )
                         );

    const auto &furntype = m.furn( fpos ).obj();
    if( furntype.move_str_req < 0 ) {
        add_msg( _( "You can't move the %s." ), furntype.name() );
        u.grab( OBJECT_NONE );
        return false;
    }

    const auto src_items = m.i_at( fpos ).size();
    const auto dst_items = m.i_at( fdest ).size();

    const auto only_liquid_items = std::all_of( m.i_at( fdest ).begin(), m.i_at( fdest ).end(),
    [&]( const auto & liquid_item ) {
        return liquid_item->made_of( LIQUID );
    } );

    const auto dst_item_ok = !m.has_flag( "NOITEM", fdest ) &&
                             !m.has_flag( "SWIMMABLE", fdest ) &&
                             !m.has_flag( "DESTROY_ITEM", fdest );

    const auto src_item_ok = m.furn( fpos ).obj().has_flag( "CONTAINER" ) ||
                             m.furn( fpos ).obj().has_flag( "FIRE_CONTAINER" ) ||
                             m.furn( fpos ).obj().has_flag( "SEALED" );

    const auto fire_intensity = m.get_field_intensity( fpos, fd_fire );
    auto fire_age = m.get_field_age( fpos, fd_fire );

    const auto effort = furniture_move_effort_for( {
        .you = u,
        .here = m,
        .from = fpos,
        .to = fdest,
    } );
    if( !canmove ) {
        // TODO: What is something?
        add_msg( _( "The %s collides with something." ), furntype.name() );
        u.moves -= 50;
        return true;
        ///\EFFECT_STR determines ability to drag furniture
    } else if( effort.str_req > effort.adjusted_str &&
               one_in( std::max( 20 - effort.str_req - effort.adjusted_str, 2 ) ) ) {
        add_msg( m_bad, _( "You strain yourself trying to move the heavy %s!" ),
                 furntype.name() );
        u.moves -= 100;
        u.mod_pain( 1 ); // Hurt ourselves.
        return true; // furniture and or obstacle wins.
    } else if( !src_item_ok && !only_liquid_items && dst_items > 0 ) {
        add_msg( _( "There's stuff in the way." ) );
        u.moves -= 50;
        return true;
    }

    u.moves -= effort.move_cost;
    u.mod_stamina( -effort.stamina_cost, false );
    // Additional penalty if we can't comfortably move it.
    if( effort.str_req > effort.adjusted_str ) {
        auto move_penalty = static_cast<int>( std::pow( effort.str_req, 2.0 ) + 100.0 );
        if( move_penalty <= 1000 ) {
            if( effort.adjusted_str >= effort.str_req - 3 ) {
                u.moves -= std::max( 3000, move_penalty * 10 );
                add_msg( m_bad, _( "The %s is really heavy!" ), furntype.name() );
                if( one_in( 3 ) ) {
                    add_msg( m_bad, _( "You fail to move the %s." ), furntype.name() );
                    return true;
                }
            } else {
                u.moves -= 100;
                add_msg( m_bad, _( "The %s is too heavy for you to budge." ), furntype.name() );
                return true;
            }
        }
        u.moves -= move_penalty;
        if( move_penalty > 500 ) {
            add_msg( _( "Moving the heavy %s is taking a lot of time!" ),
                     furntype.name() );
        } else if( move_penalty > 200 ) {
            if( one_in( 3 ) ) { // Nag only occasionally.
                add_msg( _( "It takes some time to move the heavy %s." ),
                         furntype.name() );
            }
        }
    }
    sounds::sound( fdest, effort.str_req * 2, sounds::sound_t::movement,
                   _( "a scraping noise." ), true, "misc", "scraping" );

    auto *atd = active_tiles::furn_at<active_tile_data>
                ( tripoint_abs_ms( m.bub_to_abs( fpos ) ) );

    // Swap furniture vars between tiles beforehand
    // because the furn_set call will clear the vars
    // when furniture is set to f_null
    const auto dstVars = m.furn_vars( fdest );
    const auto srcVars = m.furn_vars( fpos );
    std::swap( *srcVars, *dstVars );

    // Actually move the furniture.
    // Ignore grab destroy checks
    m.furn_set( fdest, m.furn( fpos ), atd ? atd->clone() : nullptr, true );
    m.furn_set( fpos, f_null, nullptr, true );
    u.clear_memorized_overlay( m.bub_to_abs( tripoint_bub_ms( fpos ) ) );

    if( fire_intensity == 1 && !pulling_furniture ) {
        m.remove_field( fpos, fd_fire );
        m.set_field_intensity( fdest, fd_fire, fire_intensity );
        m.set_field_age( fdest, fd_fire, fire_age );
    }

    // Is there is only liquids on the ground, remove them after moving furniture.
    if( dst_items > 0 && only_liquid_items ) {
        m.i_clear( fdest );
    }

    if( src_items > 0 ) { // Move the stuff inside.
        if( dst_item_ok && src_item_ok ) {
            // Assume contents of both cells are legal, so we can just swap contents.
            auto temp = m.i_clear( fpos );
            auto temp2 = m.i_clear( fdest );
            for( auto &it : temp ) {
                m.i_at( fdest ).insert( std::move( it ) );
            }
            for( auto &it : temp2 ) {
                m.i_at( fpos ).insert( std::move( it ) );
            }
        } else {
            add_msg( _( "Stuff spills from the %s!" ), furntype.name() );
        }
    }

    if( shifting_furniture ) {
        // We didn't move
        auto d_sum = fdest - u.bub_pos();
        if( std::abs( d_sum.x() ) < 2 && std::abs( d_sum.y() ) < 2 ) {
            u.grab_point = d_sum; // furniture moved relative to us
        } else { // we pushed furniture out of reach
            add_msg( _( "You let go of the %s." ), furntype.name() );
            u.grab( OBJECT_NONE );
        }
        return true; // We moved furniture but stayed still.
    }

    const auto player_next_pos = tripoint_bub_ms( u.bub_pos() + dp );
    u.grab_point = fdest - player_next_pos;

    if( pushing_furniture && m.impassable( fpos ) ) {
        // Not sure how that chair got into a wall, but don't let player follow.
        add_msg( _( "You let go of the %1$s as it slides past %2$s." ),
                 furntype.name(), m.tername( fdest ) );
        u.grab( OBJECT_NONE );
        return true;
    }

    return false;
}


// ——— grabbed_move ———
auto game::grabbed_move( const tripoint_rel_ms &dp, const bool allow_furniture_z_move ) -> bool
{
    if( u.get_grab_type() == OBJECT_NONE ) {
        return false;
    }

    if( dp.z() != 0 && u.get_grab_type() != OBJECT_VEHICLE &&
        !( u.get_grab_type() == OBJECT_FURNITURE && allow_furniture_z_move ) ) {
        // Furniture can only follow z-level movement through explicit ramp dragging.
        return false;
    }

    // vehicle: pulling, pushing, or moving around the grabbed object.
    if( u.get_grab_type() == OBJECT_VEHICLE ) {
        return grabbed_veh_move( dp );
    }

    if( u.get_grab_type() == OBJECT_FURNITURE ) {
        return grabbed_furn_move( dp );
    }

    add_msg( m_info, _( "Nothing at grabbed point %d,%d,%d or bad grabbed object type." ),
             u.grab_point.x(), u.grab_point.y(), u.grab_point.z() );
    u.grab( OBJECT_NONE );
    return false;
}


// ——— on_move_effects ———
void game::on_move_effects()
{
    // TODO: Move this to a character method
    if( !u.is_mounted() ) {
        const item &muscle = *item::spawn_temporary( "muscle" );
        for( const bionic_id &bid : u.get_bionic_fueled_with( muscle ) ) {
            if( u.has_active_bionic( bid ) ) {// active power gen
                u.mod_power_level( units::from_kilojoule( muscle.fuel_energy() ) * bid->fuel_efficiency );
            } else if( u.has_bionic( bid ) ) {// passive power gen
                u.mod_power_level( units::from_kilojoule( muscle.fuel_energy() ) * bid->passive_fuel_efficiency );
            }
        }
        const bionic_id bio_jointservo( "bio_jointservo" );
        if( u.has_active_bionic( bio_jointservo ) ) {
            if( u.movement_mode_is( CMM_RUN ) ) {
                u.mod_power_level( -bio_jointservo->power_trigger * 1.55 );
            } else {
                u.mod_power_level( -bio_jointservo->power_trigger );
            }
        }
    }

    if( u.movement_mode_is( CMM_RUN ) ) {
        if( !u.can_run() ) {
            u.toggle_run_mode();
        }
    }

    // apply martial art move bonuses
    u.martial_arts_data->ma_onmove_effects( u );

    sfx::do_ambient();
}


// ——— fling_creature ———
void game::fling_creature( Creature *c, const units::angle &dir, float flvel, bool controlled )
{
    if( c == nullptr ) {
        debugmsg( "game::fling_creature invoked on null target" );
        return;
    }

    if( c->is_dead_state() ) {
        // Flinging a corpse causes problems, don't enable without testing
        return;
    }

    if( c->is_hallucination() ) {
        // Don't fling hallucinations
        return;
    }

    bool thru = true;
    const bool is_u = ( c == &u );
    // Don't animate critters getting bashed if animations are off
    const bool animate = is_u || get_option<bool>( "ANIMATIONS" );

    player *p = dynamic_cast<player *>( c );

    tileray tdir( dir );
    int range = flvel / 10;
    auto pt = c->bub_pos();
    auto prev_point = pt;
    bool force_next = false;
    tripoint_bub_ms next_forced;
    while( range > 0 ) {
        c->set_underwater( false );
        // TODO: Check whenever it is actually in the viewport
        // or maybe even just redraw the changed tiles
        bool seen = is_u || u.sees( *c ); // To avoid redrawing when not seen
        if( force_next ) {
            pt = next_forced;
            force_next = false;
        } else {
            tdir.advance();
            pt.x() = c->bub_pos().x() + tdir.dx();
            pt.y() = c->bub_pos().y() + tdir.dy();
        }
        float force = 0;

        if( m.obstructed_by_vehicle_rotation( prev_point, pt ) ) {
            //We process the intervening tile on this iteration and then the current tile on the next
            next_forced = pt;
            force_next = true;
            if( one_in( 2 ) ) {
                pt.x() = prev_point.x();
            } else {
                pt.y() = prev_point.y();
            }
        }


        if( monster *const mon_ptr = critter_at<monster>( pt ) ) {
            monster &critter = *mon_ptr;
            // Approximate critter's "stopping power" with its max hp
            force = std::min<float>( 1.5f * critter.type->hp, flvel );
            const int damage = rng( force, force * 2.0f ) / 6;
            c->impact( damage, pt );
            // Multiply zed damage by 6 because no body parts
            const int zed_damage = std::max( 0,
                                             ( damage - critter.get_armor_bash( bodypart_id( "torso" ) ) ) * 6 );
            // TODO: Pass the "flinger" here - it's not the flung critter that deals damage
            critter.apply_damage( c, bodypart_id( "torso" ), zed_damage );
            critter.check_dead_state();
            if( !critter.is_dead() ) {
                thru = false;
            }
        } else if( m.impassable( pt ) ) {
            if( !m.veh_at( pt ).obstacle_at_part() ) {
                force = std::min<float>( m.bash_strength( pt ), flvel );
            } else {
                // No good way of limiting force here
                // Keep it 1 less than maximum to make the impact hurt
                // but to keep the target flying after it
                force = flvel - 1;
            }
            const int damage = rng( force, force * 2.0f ) / 9;
            c->impact( damage, pt );
            if( m.is_bashable( pt ) ) {
                // Only go through if we successfully make the tile passable
                m.bash( pt, flvel );
                thru = m.passable( pt );
            } else {
                thru = false;
            }
        }

        // If the critter dies during flinging, moving it around causes debugmsgs
        if( c->is_dead_state() ) {
            return;
        }

        flvel -= force;
        if( thru ) {
            if( p != nullptr ) {
                if( p->in_vehicle ) {
                    m.unboard_vehicle( p->bub_pos() );
                }
                // If we're flinging the player around, make sure the map stays centered on them.
                if( is_u ) {
                    update_map( pt.x(), pt.y() );
                } else {
                    p->setpos( pt );
                }
            } else if( !critter_at( pt ) ) {
                // Dying monster doesn't always leave an empty tile (blob spawning etc.)
                // Just don't setpos if it happens - next iteration will do so
                // or the monster will stop a tile before the unpassable one
                c->setpos( pt );
            }
        } else {
            // Don't zero flvel - count this as slamming both the obstacle and the ground
            // although at lower velocity
            break;
        }
        //Vehicle wall tiles don't count for range
        if( !force_next ) {
            range--;
        }
        prev_point = pt;
        if( animate && ( seen || u.sees( *c ) ) ) {
            invalidate_main_ui_adaptor();
            inp_mngr.pump_events();
            ui_manager::redraw_invalidated();
            refresh_display();
        }
    }

    // Fall down to the ground - always on the last reached tile
    if( !m.has_flag( "SWIMMABLE", c->bub_pos() ) ) {
        const trap_id trap_under_creature = m.tr_at( c->bub_pos() ).loadid;
        // Didn't smash into a wall or a floor so only take the fall damage
        if( thru && trap_under_creature == tr_ledge ) {
            m.creature_on_trap( *c, false );
        } else {
            // Fall on ground
            int force = rng( flvel, flvel * 2 ) / 9;
            if( controlled ) {
                force = std::max( force / 2 - 5, 0 );
            }
            if( force > 0 ) {
                int dmg = c->impact( force, c->bub_pos() );
                // TODO: Make landing damage the floor
                m.bash( c->bub_pos(), dmg / 4, false, false, false );
            }
            // Always apply traps to creature i.e. bear traps, tele traps etc.
            m.creature_on_trap( *c, false );
        }
    } else {
        c->set_underwater( true );
        if( is_u ) {
            if( controlled ) {
                add_msg( _( "You dive into water." ) );
            } else {
                add_msg( m_warning, _( "You fall into water." ) );
            }
        }
    }
}


// ——— point_selection_menu_static ———
static std::optional<tripoint_bub_ms> point_selection_menu( const std::vector<tripoint_bub_ms>
        &pts )
{
    if( pts.empty() ) {
        debugmsg( "point_selection_menu called with empty point set" );
        return std::nullopt;
    }

    if( pts.size() == 1 ) {
        return pts[0];
    }

    const auto &upos = g->u.bub_pos();
    uilist pmenu;
    pmenu.title = _( "Climb where?" );
    int num = 0;
    for( const tripoint_bub_ms &pt : pts ) {
        // TODO: Sort the menu so that it can be used with numpad directions
        const std::string &direction = direction_name( direction_from( upos.xy(), pt.xy() ) );
        // TODO: Inform player what is on said tile
        // But don't just print terrain name (in many cases it will be "open air")
        pmenu.addentry( num++, true, MENU_AUTOASSIGN, _( "Climb %s" ), direction );
    }

    pmenu.query();
    const int ret = pmenu.ret;
    if( ret < 0 || ret >= num ) {
        return std::nullopt;
    }

    return pts[ret];
}


// ——— find_empty_spot_nearby_static ———
static std::optional<tripoint_bub_ms> find_empty_spot_nearby( const tripoint_bub_ms &pos )
{
    map &here = get_map();
    for( const tripoint_bub_ms &p : here.points_in_radius( pos, 1 ) ) {
        if( p == pos ) {
            continue;
        }
        if( here.impassable( p ) ) {
            continue;
        }
        if( g->critter_at( p ) ) {
            continue;
        }
        return p;
    }
    return std::nullopt;
}


// ——— vertical_move ———
void game::vertical_move( int movez, bool force, bool peeking )
{
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

    // Force means we're going down, even if there's no staircase, etc.
    bool climbing = false;
    const bool can_fly = character_funcs::can_fly( get_avatar() );
    const bool can_noclip = character_funcs::can_noclip( get_avatar() );
    int move_cost = 100;
    tripoint_bub_ms stairs( u.bub_pos().x(), u.bub_pos().y(), u.bub_pos().z() + movez );
    if( m.has_zlevels() && !force && movez == 1 && !m.has_flag( "GOES_UP", u.bub_pos() ) &&
        !u.is_underwater() && !can_fly ) {

        // Climbing
        if( m.has_floor_or_support( stairs ) ) {
            add_msg( m_info, _( "You can't climb here - there's a ceiling above your head." ) );
            // Don't prompt the player if they're already standing on stairs, they might've just hit the wrong key
            if( !m.has_flag( "GOES_DOWN", u.bub_pos() ) ) {
                suggest_auto_walk_to_stairs( u, m, "up" );
            }
            return;
        }

        std::vector<tripoint_bub_ms> pts;
        for( const auto &pt : m.points_in_radius( stairs, 1 ) ) {
            if( m.passable( pt ) &&
                m.has_floor_or_support( pt ) ) {
                pts.push_back( pt );
            }
        }

        const auto cost = map_funcs::climbing_cost( m, u.bub_pos(), stairs );

        if( !cost.has_value() ) {
            if( u.has_trait( trait_WEB_ROPE ) )  {
                if( pts.empty() ) {
                    add_msg( m_info, _( "There is nothing above you that you can attach a web to." ) );
                } else if( can_use_mutation_warn( trait_WEB_ROPE, u ) ) {
                    if( m.move_cost( u.bub_pos() ) != 2 && m.move_cost( u.bub_pos() ) != 3 ) {
                        add_msg( m_info, _( "You can't spin a web rope there." ) );
                    } else if( m.has_furn( u.bub_pos() ) ) {
                        add_msg( m_info, _( "There is already furniture at that location." ) );
                    } else {
                        if( query_yn( "Spin a rope and climb?" ) ) {
                            add_msg( m_good, _( "You spin a rope of web." ) );
                            m.furn_set( u.bub_pos(), furn_str_id( "f_rope_up_web" ) );
                            u.mod_moves( to_turns<int>( 2_seconds ) );
                            u.mutation_spend_resources( trait_WEB_ROPE );
                            vertical_move( movez, force, peeking );
                        }
                    }
                }

            } else {
                add_msg( m_info, _( "You can't climb here - you need walls and/or furniture to brace against." ) );
                if( !m.has_flag( "GOES_DOWN", u.bub_pos() ) ) {
                    suggest_auto_walk_to_stairs( u, m, "up" );
                }
            }
            return;

        }

        if( pts.empty() ) {
            add_msg( m_info,
                     _( "You can't climb here - there is no terrain above you that would support your weight." ) );
            if( !m.has_flag( "GOES_DOWN", u.bub_pos() ) ) {
                suggest_auto_walk_to_stairs( u, m, "up" );
            }
            return;
        } else {
            // TODO: Make it an extended action
            climbing = true;
            move_cost = cost.value();

            const std::optional<tripoint_bub_ms> pnt = point_selection_menu( pts );
            if( !pnt ) {
                return;
            }
            stairs = *pnt;
        }
    }

    if( !climbing && !force && movez == 1 && !m.has_flag( "GOES_UP", u.bub_pos() ) &&
        !u.is_underwater() ) {

        const auto dest = u.bub_pos() + tripoint_above;
        const ter_id dest_terrain = m.ter( dest );
        const bool dest_is_air = dest_terrain == t_open_air;

        const auto &mutations = get_avatar().get_mutations();

        if( !can_fly ) {
            add_msg( m_info, _( "You can't go up here!" ) );
            return;
        }

        if( m.impassable( dest ) || !dest_is_air ) {
            if( !can_noclip ) {
                for( const trait_id &tid : mutations ) {
                    const auto &mdata = tid.obj();
                    if( mdata.flags.contains( trait_flag_MUTATION_FLIGHT ) ) {
                        get_avatar().mutation_spend_resources( tid );
                    }
                }
                add_msg( m_info, _( "There is something above blocking your way." ) );
                return;
            } else {
                if( dest.z() > OVERMAP_HEIGHT ) {
                    add_msg( m_info, _( "Tried to move outside of zlevel world bounds." ) );
                    return;
                }
            }
        }

        // dest is air and no noclip: spend resources
        if( dest_is_air && !can_noclip ) {
            for( const trait_id &tid : mutations ) {
                const auto &mdata = tid.obj();
                if( mdata.flags.contains( trait_flag_MUTATION_FLIGHT ) ) {
                    get_avatar().mutation_spend_resources( tid );
                }
            }
            // add flying flavor text here
        }

    } else if( !force && movez == -1 && !m.has_flag( "GOES_DOWN", u.bub_pos() ) &&
               !u.is_underwater() ) {

        const auto dest = u.bub_pos() + tripoint_below;

        // Check if player is standing on open air
        const ter_id here_terrain = m.ter( u.bub_pos() );
        const bool standing_on_air = here_terrain == t_open_air;

        if( !can_fly ) {
            add_msg( m_info, _( "You can't go down here!" ) );
            if( !m.has_flag( "GOES_UP", u.bub_pos() ) ) {
                suggest_auto_walk_to_stairs( u, m, "down" );
            }
            return;
        }

        if( m.impassable( dest ) || !standing_on_air ) {
            if( !can_noclip ) {
                add_msg( m_info, _( "You can't go down here!" ) );
                if( !m.has_flag( "GOES_UP", u.bub_pos() ) ) {
                    suggest_auto_walk_to_stairs( u, m, "down" );
                }
                return;
            } else {
                if( dest.z() < -OVERMAP_DEPTH ) {
                    add_msg( m_info, _( "Tried to move outside of zlevel world bounds." ) );
                    return;
                }
            }
        }
    }

    if( force ) {
        // Let go of a grabbed cart.
        u.grab( OBJECT_NONE );
    } else if( u.grab_point != tripoint_rel_ms::zero() ) {
        add_msg( m_info, _( "You can't drag things up and down stairs." ) );
        return;
    }

    // Because get_levz takes z-value from the map, it will change when vertical_shift (m.has_zlevels() == true)
    // is called or when the map is loaded on new z-level (== false).
    // This caches the z-level we start the movement on (current) and the level we're want to end.
    const int z_before = get_levz();
    const int z_after = get_levz() + movez;
    if( z_after < -OVERMAP_DEPTH || z_after > OVERMAP_HEIGHT ) {
        debugmsg( "Tried to move outside allowed range of z-levels" );
        return;
    }

    if( !u.move_effects( false ) ) {
        return;
    }

    // Check if there are monsters are using the stairs.
    bool slippedpast = false;
    if( !m.has_zlevels() && !coming_to_stairs.empty() && !force ) {
        // TODO: Allow travel if zombie couldn't reach stairs, but spawn him when we go up.
        add_msg( m_warning, _( "You try to use the stairs.  Suddenly you are blocked by a %s!" ),
                 coming_to_stairs[0]->name() );
        // Roll.
        ///\EFFECT_DEX increases chance of moving past monsters on stairs

        ///\EFFECT_DODGE increases chance of moving past monsters on stairs
        int dexroll = dice( 6, u.dex_cur + u.get_skill_level( skill_dodge ) * 2 );
        ///\EFFECT_STR increases chance of moving past monsters on stairs

        ///\EFFECT_MELEE increases chance of moving past monsters on stairs
        int strroll = dice( 3, u.str_cur + u.get_skill_level( skill_melee ) * 1.5 );
        if( coming_to_stairs.size() > 4 ) {
            add_msg( _( "The are a lot of them on the %s!" ), m.tername( u.bub_pos() ) );
            dexroll /= 4;
            strroll /= 2;
        } else if( coming_to_stairs.size() > 1 ) {
            add_msg( m_warning, _( "There's something else behind it!" ) );
            dexroll /= 2;
        }

        if( dexroll < 14 || strroll < 12 ) {
            update_stair_monsters();
            u.moves -= 100;
            return;
        }

        add_msg( _( "You manage to slip past!" ) );
        slippedpast = true;
        u.moves -= 100;
    }

    // Shift the map up or down

    std::unique_ptr<map> tmp_map_ptr;
    if( !m.has_zlevels() ) {
        tmp_map_ptr = std::make_unique<map>();
    }

    map &maybetmp = m.has_zlevels() ? m : *( tmp_map_ptr );
    if( m.has_zlevels() ) {
        // We no longer need to shift the map here! What joy
    } else {
        maybetmp.load( tripoint_abs_sm( get_levx(), get_levy(), z_after ), false );
    }

    bool swimming = false;
    bool surfacing = false;
    bool submerging = false;
    // > and < are used for diving underwater.
    if( m.has_flag( TFLAG_SWIMMABLE, u.bub_pos() ) ) {
        swimming = true;
        const ter_id &target_ter = m.ter( u.bub_pos() + tripoint_rel_ms( 0, 0, movez ) );

        // If we're in a water tile that has both air above and deep enough water to submerge in...
        if( m.has_flag( TFLAG_DEEP_WATER, u.bub_pos() ) &&
            !m.has_flag( TFLAG_WATER_CUBE, u.bub_pos() ) ) {
            // ...and we're trying to swim down
            if( movez == -1 ) {
                // ...and we're already submerged
                if( u.is_underwater() ) {
                    // ...and there's more water beneath us.
                    if( target_ter->has_flag( TFLAG_WATER_CUBE ) ) {
                        // Then go ahead and move down.
                        add_msg( _( "You swim down." ) );
                    } else {
                        // There's no more water beneath us.
                        add_msg( m_info,
                                 _( "You are already underwater and there is no more water beneath you to swim down!" ) );
                        return;
                    }
                }
                // ...and we're not already submerged.
                else {
                    // Check for a flotation device first before allowing us to submerge.
                    if( u.worn_with_flag( flag_FLOTATION ) ) {
                        add_msg( m_info, _( "You can't dive while wearing a flotation device." ) );
                        return;
                    }

                    // Then dive under the surface.
                    u.oxygen = 30 + 2 * u.str_cur;
                    u.set_underwater( true );
                    add_msg( _( "You dive underwater!" ) );
                    submerging = true;
                }
            }
            // ...and we're trying to surface
            else if( movez == 1 ) {
                // ... and we're already submerged
                if( u.is_underwater() ) {
                    if( u.swim_speed() < 500 || u.shoe_type_count( itype_swim_fins ) ||
                        u.has_trait_flag( trait_flag_MUTATION_SWIM ) ) {
                        u.set_underwater( false );
                        add_msg( _( "You surface." ) );
                        surfacing = true;
                    } else {
                        add_msg( m_info, _( "You try to surface but can't!" ) );
                        return;
                    }
                }
            }
        }
        // If we're in a water tile that is entirely water
        else if( m.has_flag( TFLAG_WATER_CUBE, u.bub_pos() ) ) {
            // If you're at this point, you should already be underwater, but force that to be the case.
            if( !u.is_underwater() ) {
                u.oxygen = 30 + 2 * u.str_cur;
                u.set_underwater( true );
            }

            // ...and we're trying to swim down
            if( movez == -1 ) {
                // ...and there's more water beneath us.
                if( target_ter->has_flag( TFLAG_WATER_CUBE ) ) {
                    // Then go ahead and move down.
                    add_msg( _( "You swim down." ) );
                } else {
                    add_msg( m_info,
                             _( "You are already underwater and there is no more water beneath you to swim down!" ) );
                    return;
                }
            }
            // ...and we're trying to move up
            else if( movez == 1 ) {
                const std::optional<vpart_reference> vp = get_map().veh_at( u.bub_pos() + tripoint_rel_ms( 0, 0,
                    movez ) ).part_with_feature( VPFLAG_BOARDABLE,
                                                 true );
                if( vp ) {
                    add_msg( m_info, _( "You can't board a boat from underneath it!" ) );
                    return;
                    // ...and there's more water above us, but no boats blocking the way.
                } else if( target_ter->has_flag( TFLAG_WATER_CUBE ) ||
                           target_ter->has_flag( TFLAG_DEEP_WATER ) ) {
                    // Then go ahead and move up.
                    add_msg( _( "You swim up." ) );
                } else {
                    add_msg( m_info, _( "You are already underwater and there is no water above you to swim up!" ) );
                    return;
                }
            }
        }
    }

    // Find the corresponding staircase
    bool rope_ladder = false;
    // TODO: Remove the stairfinding, make the mapgen gen aligned maps
    const bool special_move = climbing || swimming || can_fly;

    if( !force && !special_move ) {
        const std::optional<tripoint_bub_ms> pnt = find_or_make_stairs( maybetmp, z_after, rope_ladder,
            peeking );
        if( !pnt ) {
            return;
        }
        stairs = *pnt;
    }

    if( !force ) {
        monstairz = z_before;
    }
    // Save all monsters that can reach the stairs, remove them from the tracker,
    // then despawn the remaining monsters. Because it's a vertical shift, all
    // monsters are out of the bounds of the map and will despawn.
    shared_ptr_fast<monster> stored_mount;
    if( u.is_mounted() && !m.has_zlevels() ) {
        // Store a *copy* of the mount, so we can remove the original monster instance
        // from the tracker before the map shifts.
        // Map shifting would otherwise just despawn the mount and would later respawn it.
        stored_mount = make_shared_fast<monster>( *u.mounted_creature );
        critter_tracker->remove( *u.mounted_creature );
    }
    if( !m.has_zlevels() ) {
        const auto to = u.bub_pos();
        for( monster &critter : all_monsters() ) {
            // if its a ladder instead of stairs - most zombies can't climb that.
            // unless that have a special flag to allow them to do so.
            if( ( m.has_flag( "DIFFICULT_Z", u.bub_pos() ) && !critter.climbs() ) ||
                critter.has_effect( effect_ridden ) ||
                critter.has_effect( effect_tied ) ) {
                continue;
            }
            int turns = critter.turns_to_reach( to.xy() );
            if( turns < 10 && coming_to_stairs.size() < 8 && critter.will_reach( to.xy() )
                && !slippedpast ) {
                critter.staircount = 10 + turns;
                critter.on_unload();
                coming_to_stairs.push_back( make_shared_fast<monster>( critter ) );
                remove_zombie( critter );
            }
        }
        auto mons = critter_tracker->find( g->u.bub_pos() );
        if( mons != nullptr ) {
            critter_tracker->remove( *mons );
        }
        shift_monsters( tripoint_rel_sm( 0, 0, movez ) );
    }

    std::vector<shared_ptr_fast<npc>> npcs_to_bring;
    std::vector<monster *> monsters_following;
    if( !m.has_zlevels() && std::abs( movez ) == 1 ) {
        std::copy_if( active_npc.begin(), active_npc.end(), back_inserter( npcs_to_bring ),
        [this]( const shared_ptr_fast<npc> &np ) {
            return np->is_walking_with() && !np->is_mounted() && !np->in_sleep_state() &&
                   rl_dist( np->bub_pos(), u.bub_pos() ) < 2;
        } );
    }

    if( m.has_zlevels() && std::abs( movez ) == 1 ) {
        bool ladder = m.has_flag( "DIFFICULT_Z", u.bub_pos() );
        for( monster &critter : all_monsters() ) {
            if( ladder && !critter.climbs() ) {
                continue;
            }
            if( critter.attack_target() == &g->u || ( !critter.has_effect( effect_ridden ) &&
                    critter.has_effect( effect_pet ) && critter.friendly == -1 &&
                    !critter.has_effect( effect_tied ) ) ) {
                monsters_following.push_back( &critter );
            }
        }
    }

    if( u.is_mounted() ) {
        monster *crit = u.mounted_creature.get();
        if( crit->has_flag( MF_RIDEABLE_MECH ) ) {
            crit->use_mech_power( -1 );
            if( u.movement_mode_is( CMM_WALK ) ) {
                crit->use_mech_power( -2 );
            } else if( u.is_crouching() ) {
                crit->use_mech_power( -1 );
            } else if( u.movement_mode_is( CMM_RUN ) ) {
                crit->use_mech_power( -3 );
            }
        }
    } else {
        u.moves -= move_cost;
        // Risk of failing, simple stuff like ladders are exempt
        if( climbing && movez == 1 && m.climb_difficulty( u.bub_pos() ) > 1 ) {
            if( g->slip_down() ) {
                return;
            }
        }
    }
    for( const auto &np : npcs_to_bring ) {
        if( np->in_vehicle ) {
            m.unboard_vehicle( np->bub_pos() );
        }
    }

    if( surfacing || submerging ) {
        // Surfacing and submerging don't actually move us anywhere, and just
        // toggle our underwater state in the same location.
        return;
    }

    const auto old_pos = g->u.bub_pos();
    point_rel_sm submap_shift;
    vertical_shift( z_after );
    if( !force ) {
        submap_shift = update_map( stairs.x(), stairs.y() );
    }

    // if an NPC or monster is on the stiars when player ascends/descends
    // they may end up merged on th esame tile, do some displacement to resolve that.
    // if, in the weird case of it not being possible to displace;
    // ( how did the player even manage to approach the stairs, if so? )
    // then nothing terrible happens, its just weird.
    if( critter_at<npc>( u.bub_pos(), true ) || critter_at<monster>( u.bub_pos(), true ) ) {
        std::string crit_name;
        bool player_displace = false;
        std::optional<tripoint_bub_ms> displace = find_empty_spot_nearby( u.bub_pos() );
        if( displace.has_value() ) {
            npc *guy = g->critter_at<npc>( u.bub_pos(), true );
            if( guy ) {
                crit_name = guy->get_name();
                auto old_pos = guy->bub_pos();
                if( !guy->is_enemy() ) {
                    guy->move_away_from( u.bub_pos(), true );
                    if( old_pos != guy->bub_pos() ) {
                        add_msg( _( "%s moves out of the way for you." ), guy->get_name() );
                    }
                } else {
                    player_displace = true;
                }
            }
            monster *mon = g->critter_at<monster>( u.bub_pos(), true );
            // if the monster is ridden by the player or an NPC:
            // Dont displace them. If they are mounted by a friendly NPC,
            // then the NPC will already have been displaced just above.
            // if they are ridden by the player, we want them to coexist on same tile
            if( mon && !mon->mounted_player ) {
                crit_name = mon->get_name();
                if( mon->friendly == -1 ) {
                    mon->setpos( *displace );
                    add_msg( _( "Your %s moves out of the way for you." ), mon->get_name() );
                } else {
                    player_displace = true;
                }
            }
            if( player_displace ) {
                u.setpos( *displace );
                u.moves -= 20;
                add_msg( _( "You push past %s blocking the way." ), crit_name );
            }
        } else {
            debugmsg( "Failed to find a spot to displace into." );
        }
    }

    // Now that we know the player's destination position, we can move their mount as well
    if( u.is_mounted() ) {
        if( stored_mount ) {
            assert( !m.has_zlevels() );
            stored_mount->spawn( g->u.bub_pos() );
            if( critter_tracker->add( stored_mount ) ) {
                u.mounted_creature = stored_mount;
            }
        } else {
            u.mounted_creature->setpos( g->u.bub_pos() );
        }
    }

    if( !npcs_to_bring.empty() ) {
        // Would look nicer randomly scrambled
        std::vector<tripoint_bub_ms> candidates = closest_points_first( u.bub_pos(), 1 );
        candidates.erase( std::remove_if( candidates.begin(), candidates.end(),
        [this]( const tripoint_bub_ms & c ) {
            return !is_empty( c );
        } ), candidates.end() );

        for( const auto &np : npcs_to_bring ) {
            const auto found = std::find_if( candidates.begin(), candidates.end(),
            [this, np]( const tripoint_bub_ms & c ) {
                return !np->is_dangerous_fields( m.field_at( c ) ) && m.tr_at( c ).is_benign();
            } );
            if( found != candidates.end() ) {
                // TODO: De-uglify
                np->setpos( *found );
                np->place_on_map();
                np->setpos( *found );
                candidates.erase( found );
            }

            if( candidates.empty() ) {
                break;
            }
        }

        reload_npcs();
    }

    // This ugly check is here because of stair teleport bullshit
    // TODO: Remove stair teleport bullshit
    if( rl_dist( g->u.bub_pos(), old_pos ) <= 1 ) {
        for( monster *m : monsters_following ) {
            m->set_dest( g->u.bub_pos() );
        }
    }

    if( rope_ladder ) {
        m.ter_set( u.bub_pos(), t_rope_up );
    }

    if( m.ter( stairs ) == t_manhole_cover ) {
        m.spawn_item( stairs + point( rng( -1, 1 ), rng( -1, 1 ) ), itype_manhole_cover );
        m.ter_set( stairs, t_manhole );
    }

    // Wouldn't work and may do strange things
    if( u.is_hauling() && !m.has_zlevels() ) {
        add_msg( _( "You cannot haul items here." ) );
        u.stop_hauling();
    }

    if( u.is_hauling() ) {
        const auto adjusted_pos = old_pos - project_to<coords::ms>( submap_shift );
        start_hauling( adjusted_pos );
    }
    if( m.has_flag( "UNSTABLE", u.bub_pos() ) && !u.is_mounted() ) {
        u.add_effect( effect_bouldering, 1_turns, bodypart_str_id::NULL_ID() );
    } else if( u.has_effect( effect_bouldering ) ) {
        u.remove_effect( effect_bouldering );
    }

    m.invalidate_map_cache( g->get_levz() );
    // Upon force movement, traps can not be avoided.
    m.creature_on_trap( u, !force );

    cata_event_dispatch::avatar_moves( u, m, u.abs_pos() );
}


// ——— find_stairs ———
std::optional<tripoint_bub_ms> game::find_stairs( map &mp, const int z_after, bool peeking )
{
    const auto bub_pos = mp.abs_to_bub( u.abs_pos() );
    const auto movez = tripoint_rel_ms( 0, 0, z_after - get_levz() );
    // If there are stairs on the same x and y as we currently are, use those
    if( movez.z() == -1 && mp.has_flag( TFLAG_GOES_UP, bub_pos + movez ) ) {
        return bub_pos + movez;
    }
    if( movez.z() == 1 && mp.has_flag( TFLAG_GOES_DOWN, bub_pos + movez ) &&
        !mp.has_flag( TFLAG_DEEP_WATER, bub_pos + movez ) ) {
        return bub_pos + movez;
    }
    // We did not find stairs directly above or below, so search the map for them
    // This was named as though overmap, but that'd be *absurd* so I made this omt.
    const auto omt_start = project_to<coords::omt>( u.abs_pos() );

    // Try to find the stairs.
    std::optional<tripoint_bub_ms> stairs;
    int best = INT_MAX;
    if( !stairs.has_value() ) {
        for( const auto &rel : overmap_terrain_tiles() ) {
            const auto dest = mp.abs_to_bub( project_combine( omt_start, rel ) ) + movez;
            if( rl_dist( bub_pos, dest ) <= best &&
                ( ( movez.z() == -1 && mp.has_flag( TFLAG_GOES_UP, dest ) ) ||
                  ( ( movez.z() == 1 && ( mp.has_flag( TFLAG_GOES_DOWN, dest ) &&
                                          !mp.has_flag( TFLAG_DEEP_WATER, dest ) ) ) ||
                    mp.ter( dest ) == t_manhole_cover )   ||
                  ( ( movez.z() == 2 || movez.z() == -2 ) && mp.ter( dest ) == t_elevator ) ) ) {
                stairs.emplace( dest );
                best = rl_dist( bub_pos, dest );
            }
        }
    }

    if( stairs.has_value() ) {
        if( Creature *blocking_creature = critter_at( stairs.value() ) ) {
            npc *guy = dynamic_cast<npc *>( blocking_creature );
            monster *mon = dynamic_cast<monster *>( blocking_creature );
            bool would_move = ( guy && !guy->is_enemy() ) || ( mon && mon->friendly == -1 );
            bool can_displace = find_empty_spot_nearby( *stairs ).has_value();
            std::string cr_name = blocking_creature->get_name();
            std::string msg;
            if( guy ) {
                //~ %s is the name of hostile NPC
                msg = string_format( _( "%s is in the way!" ), cr_name );
            } else {
                //~ %s is some monster
                msg = string_format( _( "There's a %s in the way!" ), cr_name );
            }

            if( ( peeking && !would_move ) || !can_displace || ( !would_move && !query_yn(
                        //~ %s is a warning about monster/hostile NPC in the way, e.g. "There's a zombie in the way!"
                        _( "%s  Attempt to push past?  You may have to fight your way back up." ), msg ) ) ) {
                add_msg( msg );
                return std::nullopt;
            }
        }
    }

    return stairs;
}


// ——— find_or_make_stairs ———
std::optional<tripoint_bub_ms> game::find_or_make_stairs( map &mp, const int z_after,
        bool &rope_ladder,
        bool peeking )
{
    const auto bub_pos = mp.abs_to_bub( u.abs_pos() );
    const int movez = z_after - bub_pos.z();

    // Try to find the stairs.
    std::optional<tripoint_bub_ms> stairs = find_stairs( mp, z_after, peeking );
    // Check the destination area for lava.
    if( stairs.has_value() ) {
        // Defensive: should never happen, but bail out safely
        return stairs;
    }

    // No stairs found! Try to make some
    rope_ladder = false;
    stairs.emplace( bub_pos );
    stairs->z() = z_after;

    if( mp.ter( *stairs ) == t_lava ) {
        if( movez < 0 &&
            !query_yn(
                _( "There is a LOT of heat coming out of there, even the stairs have melted away.  Jump down?  You won't be able to get back up." ) ) ) {
            return std::nullopt;
        } else if( movez > 0 &&
                   !query_yn(
                       _( "There is a LOT of heat coming out of there.  Push through the half-molten rocks and ascend?  You will not be able to get back down." ) ) ) {
            return std::nullopt;
        }

        return stairs;
    }

    const bool can_fly = character_funcs::can_fly( get_avatar() );

    if( movez > 0 ) {
        if( mp.has_flag( "DEEP_WATER", *stairs ) ) {
            if( !query_yn(
                    _( "There is a huge blob of water!  You may be unable to return back down these stairs.  Continue up?" ) ) ) {
                return std::nullopt;
            }
        } else if( !mp.has_flag( "GOES_DOWN", *stairs ) && !can_fly ) {
            if( !query_yn( _( "You may be unable to return back down these stairs.  Continue up?" ) ) ) {
                return std::nullopt;
            }
        }
        // Manhole covers need this to work
        // Maybe require manhole cover here and fail otherwise?
        return stairs;
    }

    if( mp.impassable( *stairs ) ) {
        popup( _( "Halfway down, the way down becomes blocked off." ) );
        return std::nullopt;
    }

    if( u.has_trait( trait_id( "WEB_RAPPEL" ) ) ) {
        if( query_yn( _( "There is a sheer drop halfway down.  Web-descend?" ) ) ) {
            rope_ladder = true;
            if( ( rng( 4, 8 ) ) < u.get_skill_level( skill_dodge ) ) {
                add_msg( _( "You attach a web and dive down headfirst, flipping upright and landing on your feet." ) );
            } else {
                add_msg( _( "You securely web up and work your way down, lowering yourself safely." ) );
            }
        } else {
            return std::nullopt;
        }
    } else if( u.has_trait( trait_VINES2 ) || u.has_trait( trait_VINES3 ) ) {
        if( query_yn( _( "There is a sheer drop halfway down.  Use your vines to descend?" ) ) ) {
            if( u.has_trait( trait_VINES2 ) ) {
                if( query_yn( _( "Detach a vine?  It'll hurt, but you'll be able to climb back up…" ) ) ) {
                    rope_ladder = true;
                    add_msg( m_bad, _( "You descend on your vines, though leaving a part of you behind stings." ) );
                    u.mod_pain( 5 );
                    u.apply_damage( nullptr, bodypart_id( "torso" ), 5 );
                    u.mod_stored_nutr( 10 );
                    u.mod_thirst( 10 );
                } else {
                    add_msg( _( "You gingerly descend using your vines." ) );
                }
            } else {
                add_msg( _( "You effortlessly lower yourself and leave a vine rooted for future use." ) );
                rope_ladder = true;
                u.mod_stored_nutr( 10 );
                u.mod_thirst( 10 );
            }
        } else {
            return std::nullopt;
        }
    } else if( u.has_amount( itype_grapnel, 1 ) ) {
        if( query_yn( _( "There is a sheer drop halfway down.  Climb your grappling hook down?" ) ) ) {
            rope_ladder = true;
            u.use_amount( itype_grapnel, 1 );
        } else {
            return std::nullopt;
        }
    } else if( u.has_amount( itype_rope_30, 1 ) ) {
        if( query_yn( _( "There is a sheer drop halfway down.  Climb your rope down?" ) ) ) {
            rope_ladder = true;
            u.use_amount( itype_rope_30, 1 );
        } else {
            return std::nullopt;
        }
    } else if( !can_fly ) {
        if( !query_yn( _( "There is a sheer drop halfway down.  Jump?" ) ) ) {
            return std::nullopt;
        }
    }

    return stairs;
}


// ——— vertical_shift ———
auto game::vertical_shift( const int z_after, const bool keep_grab ) -> void
{
    if( z_after < -OVERMAP_DEPTH || z_after > OVERMAP_HEIGHT ) {
    debugmsg( "Tried to get z-level %d outside allowed range of %d-%d",
              z_after, -OVERMAP_DEPTH, OVERMAP_HEIGHT );
        return;
    }

    if( !keep_grab ) {
    u.grab( OBJECT_NONE );
    }

    scent.reset();

    u.setpos( tripoint_bub_ms( u.bub_pos().xy(), z_after ) );
    const int z_before = get_levz();
    if( !m.has_zlevels() ) {
        m.clear_vehicle_cache( );
        m.access_cache( z_before ).vehicle_list.clear();
        m.access_cache( z_before ).zone_vehicles.clear();
        m.access_cache( z_before ).map_memory_seen_cache.reset();
        m.set_transparency_cache_dirty( z_before );
        m.set_outside_cache_dirty( z_before );
        m.load( tripoint_abs_sm( get_levx(), get_levy(), z_after ), true );
        shift_monsters( tripoint_rel_sm( 0, 0, z_after - z_before ) );
        reload_npcs();
    } else {
        // Adjust the map's z-reference so get_levz() returns the new z-level.
        // All z-levels are loaded simultaneously in z-level builds; no map load
        // or unload is required for vertical movement.
        m.set_abs_sub( tripoint_abs_sm( m.get_abs_sub().xy(), z_after ) );
        if( auto *pw = m.get_physics_world(); pw && z_before != z_after ) {
            pw->on_zlevel_changed( m, z_before, z_after );
        }
    }

    m.spawn_monsters( true );
    // this may be required after a vertical shift if z-levels are not enabled
    // the critter is unloaded/loaded, and it needs to reconstruct its rider data after being reloaded.
    validate_mounted_npcs();
    vertical_notes( z_before, z_after );
}


// ——— vertical_notes ———
void game::vertical_notes( int z_before, int z_after )
{
    if( z_before == z_after || !get_option<bool>( "AUTO_NOTES" ) ||
        !get_option<bool>( "AUTO_NOTES_STAIRS" ) ) {
        return;
    }

    if( !m.inbounds_z( z_before ) || !m.inbounds_z( z_after ) ) {
        debugmsg( "game::vertical_notes invalid arguments: z_before == %d, z_after == %d",
                  z_before, z_after );
        return;
    }
    // Figure out where we know there are up/down connectors
    // Fill in all the tiles we know about (e.g. subway stations)
    static const int REVEAL_RADIUS = 40;
    for( const tripoint_abs_omt &p : points_in_radius( u.abs_omt_pos(), REVEAL_RADIUS ) ) {
        const tripoint_abs_omt cursp_before( p.xy(), z_before );
        const tripoint_abs_omt cursp_after( p.xy(), z_after );

        if( !get_overmapbuffer( current_dimension_id_ ).seen( cursp_before ) ) {
            continue;
        }
        if( get_overmapbuffer( current_dimension_id_ ).has_note( cursp_after ) ) {
            // Already has a note -> never add an AUTO-note
            continue;
        }
        const oter_id &ter = get_overmapbuffer( current_dimension_id_ ).ter( cursp_before );
        const oter_id &ter2 = get_overmapbuffer( current_dimension_id_ ).ter( cursp_after );
        if( z_after > z_before && ter->has_flag( oter_flags::known_up ) &&
            !ter2->has_flag( oter_flags::known_down ) ) {
            get_overmapbuffer( current_dimension_id_ ).set_seen( cursp_after, true );
            get_overmapbuffer( current_dimension_id_ ).add_note( cursp_after, string_format( ">:W;%s",
                    _( "AUTO: goes down" ) ) );
        } else if( z_after < z_before && ter->has_flag( oter_flags::known_down ) &&
                   !ter2->has_flag( oter_flags::known_up ) ) {
            get_overmapbuffer( current_dimension_id_ ).set_seen( cursp_after, true );
            get_overmapbuffer( current_dimension_id_ ).add_note( cursp_after, string_format( "<:W;%s",
                    _( "AUTO: goes up" ) ) );
        }
    }
}


// ——— update_overmap_seen ———
void game::update_overmap_seen()
{
    const tripoint_abs_omt ompos = u.abs_omt_pos();
    const int dist = u.overmap_sight_range( light_level( u.bub_pos().z() ) );
    const int dist_squared = dist * dist;
    overmapbuffer &omb = get_overmapbuffer( current_dimension_id_ );

    // Snapshot pre-loop seen state so we can detect newly-revealed tiles.
    // Only allocate when actually in a coop session.
    const bool in_coop = coop_session::get().is_coop();
    std::vector<tripoint_abs_omt> newly_seen;
    // For each tile we're about to process, record whether it was already seen
    // BEFORE the sight-line loop runs.  We use a flat vector of bools indexed
    // the same way points_in_radius iterates; however, since that iteration
    // order is opaque, we instead re-check after the loop — the set_seen()
    // call is idempotent so checking .seen() post-loop and comparing to a
    // pre-loop snapshot is the simplest correct approach.
    //
    // Strategy: build a set of tiles that were already seen before the loop,
    // then after the loop collect any tile that is now seen but wasn't before.
    std::vector<std::pair<tripoint_abs_omt, bool>> pre_seen;
    if( in_coop ) {
        // Reserve for all tiles in the sight radius (generous upper bound).
        const int side = 2 * dist + 1;
        pre_seen.reserve( side * side );
        for( const tripoint_abs_omt &p : points_in_radius( ompos, dist ) ) {
            // Check all z-levels down to 0 for each column, matching the loop below.
            for( int z = p.z(); z >= 0; --z ) {
                const tripoint_abs_omt tp{ p.xy(), z };
                pre_seen.emplace_back( tp, omb.seen( tp ) );
            }
        }
        // Also include the standing tile itself (set_seen'd before the loop).
        for( int z = ompos.z(); z >= 0; --z ) {
            const tripoint_abs_omt tp{ ompos.xy(), z };
            pre_seen.emplace_back( tp, omb.seen( tp ) );
        }
    }

    // We can always see where we're standing
    omb.set_seen( ompos, true );
    for( const tripoint_abs_omt &p : points_in_radius( ompos, dist ) ) {
        const point_rel_omt delta = p.xy() - ompos.xy();
        const int h_squared = delta.x() * delta.x() + delta.y() * delta.y();
        if( trigdist && h_squared > dist_squared ) {
            continue;
        }
        if( delta == point_rel_omt() ) {
            // 1. This case is already handled outside of the loop
            // 2. Calculating multiplier would cause division by zero
            continue;
        }
        // If circular distances are enabled, scale overmap distances by the diagonality of the sight line.
        point abs_delta = delta.raw().abs();
        int max_delta = std::max( abs_delta.x, abs_delta.y );
        const float multiplier = trigdist ? std::sqrt( h_squared ) / max_delta : 1;
        const std::vector<tripoint_abs_omt> line = line_to( ompos, p );
        float sight_points = dist;
        for( auto it = line.begin();
             it != line.end() && sight_points >= 0; ++it ) {
            const oter_id &ter = omb.ter( *it );
            sight_points -= static_cast<int>( ter->get_see_cost() ) * multiplier;
        }
        if( sight_points >= 0 ) {
            tripoint_abs_omt seen( p );
            do {
                omb.set_seen( seen, true );
                --seen.z();
            } while( seen.z() >= 0 );
        }
    }

    // Collect tiles that were not seen before but are now, and send to partner.
    if( in_coop ) {
        for( const auto &[tp, was_seen] : pre_seen ) {
            if( !was_seen && omb.seen( tp ) ) {
                newly_seen.push_back( tp );
            }
        }
        if( !newly_seen.empty() ) {
            const std::string pkt = build_overmap_sync_packet( newly_seen );
            if( coop_server_ ) {
                coop_server_->send_raw( pkt );
            } else if( coop_client_ ) {
                coop_client_->send_raw( pkt );
            }
        }
    }
}

