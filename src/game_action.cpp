// game_action.cpp — extracted from game.cpp (B5-5 action cluster)
// Functions: knockback (both overloads), use_computer, control_vehicle,
//            npc_menu, examine (both overloads), pickup cluster,
//            peek, drop, drop_in_direction, butcher + static helpers,
//            post_action_world_step

#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "action.h"
#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "armor_layers.h"
#include "avatar.h"
#include "avatar_action.h"
#include "avatar_functions.h"
#include "calendar.h"
#include "cached_options.h"
#include "catalua_hooks.h"
#include "character_functions.h"
#include "character_martial_arts.h"
#include "catalua_sol.h"
#include "character_turn.h"
#include "clzones.h"
#include "construction.h"
#include "coordinates.h"
#include "computer_session.h"
#include "creature.h"
#include "creature_tracker.h"
#include "debug.h"
#include "effect.h"
#include "event.h"
#include "event_bus.h"
#include "explosion_queue.h"
#include "field_type.h"
#include "field.h"
#include "flag.h"
#include "fluid_grid.h"
#include "game_constants.h"
#include "game_inventory.h"
#include "gamemode.h"
#include "iexamine.h"
#include "input.h"
#include "item.h"
#include "init.h"
#include "item_functions.h"
#include "itype.h"
#include "map.h"
#include "harvest.h"
#include "iuse_actor.h"
#include "mapdata.h"
#include "messages.h"
#include "monster.h"
#include "mongroup.h"
#include "mtype.h"
#include "npc.h"
#include "options.h"
#include "mission.h"
#include "output.h"
#include "overmapbuffer.h"
#include "monexamine.h"
#include "pickup.h"
#include "player_activity.h"
#include "rng.h"
#include "scent_map.h"
#include "rml_screen.h"
#include "recipe_dictionary.h"
#include "sounds.h"
#include "string_formatter.h"
#include "translations.h"
#include "trap.h"
#include "salvage.h"
#include "timed_event.h"
#include "type_id.h"
#include "ui.h"
#include "ui_manager.h"
#include "units.h"
#include "vehicle.h"
#include "weather.h"
#include "vehicle_part.h"
#include "vpart_position.h"
#include "profile.h"

// File-local string-ID statics
static const efftype_id effect_contacts( "contacts" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_stunned( "stunned" );
static const skill_id skill_firstaid( "firstaid" );
static const skill_id skill_survival( "survival" );
static const trait_id trait_HAS_NEMESIS( "HAS_NEMESIS" );
static const trait_id trait_LEG_TENT_BRACE( "LEG_TENT_BRACE" );
static const trait_id trait_WAYFARER( "WAYFARER" );

void game::knockback( const tripoint_bub_ms &s, const tripoint_bub_ms &t, int force, int stun,
                      int dam_mult,
                      Creature *source )
{
    std::vector<tripoint_bub_ms> traj;
    traj.clear();
    traj = line_to( s, t, 0, 0 );
    traj.insert( traj.begin(), s ); // how annoying, line_to() doesn't include the originating point!
    traj = continue_line( traj, force );
    traj.insert( traj.begin(), t ); // how annoying, continue_line() doesn't either!

    knockback( traj, stun, dam_mult, source );
}

/* Knockback target at traj.front() along line traj; traj should already have considered knockback distance.
   stun > 0 indicates base stun duration, and causes impact stun; stun == -1 indicates only impact stun
   dam_mult multiplies impact damage, bash effect on impact, and sound level on impact */

void game::knockback( std::vector<tripoint_bub_ms> &traj, int stun, int dam_mult,
                      Creature *source = nullptr )
{
    // TODO: make the force parameter actually do something.
    // the header file says higher force causes more damage.
    // perhaps that is what it should do?

    // TODO: refactor this so it's not copy/pasted 3 times
    auto tp = traj.front();
    if( !critter_at( tp ) ) {
        debugmsg( _( "Nothing at (%d,%d,%d) to knockback!" ), tp.x(), tp.y(), tp.z() );
        return;
    }
    std::size_t force_remaining = traj.size();
    if( monster *const targ = critter_at<monster>( tp, true ) ) {
        auto start_pos = targ->bub_pos();

        if( stun > 0 ) {
            targ->add_effect( effect_stunned, 1_turns * stun );
            add_msg( _( "%s was stunned!" ), targ->name() );
        }
        for( size_t i = 1; i < traj.size(); i++ ) {
            if( m.impassable( traj[i].xy() ) || m.obstructed_by_vehicle_rotation( tp, traj[i] ) ) {
                targ->setpos( traj[i - 1] );
                force_remaining = traj.size() - i;
                if( stun != 0 ) {
                    targ->add_effect( effect_stunned, 1_turns * force_remaining );
                    add_msg( _( "%s was stunned!" ), targ->name() );
                    add_msg( _( "%s slammed into an obstacle!" ), targ->name() );
                    targ->apply_damage( source, bodypart_id( "torso" ), dam_mult * force_remaining );
                    targ->check_dead_state();
                }
                m.bash( traj[i], 2 * dam_mult * force_remaining );
                break;
            } else if( critter_at( traj[i] ) ) {
                targ->setpos( traj[i - 1] );
                force_remaining = traj.size() - i;
                if( stun != 0 ) {
                    targ->add_effect( effect_stunned, 1_turns * force_remaining );
                    add_msg( _( "%s was stunned!" ), targ->name() );
                }
                traj.erase( traj.begin(), traj.begin() + i );
                if( critter_at<monster>( traj.front() ) ) {
                    add_msg( _( "%s collided with something else and sent it flying!" ),
                             targ->name() );
                } else if( npc *const guy = critter_at<npc>( traj.front() ) ) {
                    if( guy->male ) {
                        add_msg( _( "%s collided with someone else and sent him flying!" ),
                                 targ->name() );
                    } else {
                        add_msg( _( "%s collided with someone else and sent her flying!" ),
                                 targ->name() );
                    }
                } else if( u.bub_pos() == traj.front() ) {
                    add_msg( m_bad, _( "%s collided with you and sent you flying!" ), targ->name() );
                }
                knockback( traj, stun, dam_mult, source );
                break;
            }
            targ->setpos( traj[i] );
            if( m.has_flag( "LIQUID", targ->bub_pos() ) && targ->can_drown() && !targ->is_dead() ) {
                targ->die( source );
                if( u.sees( *targ ) ) {
                    add_msg( _( "The %s drowns!" ), targ->name() );
                }
            }
            if( !m.has_flag( "LIQUID", targ->bub_pos() ) && targ->has_flag( MF_AQUATIC ) &&
                !targ->is_dead() ) {
                targ->die( source );
                if( u.sees( *targ ) ) {
                    add_msg( _( "The %s flops around and dies!" ), targ->name() );
                }
            }
            tp = traj[i];
            if( start_pos != targ->bub_pos() ) {
                map &here = get_map();
                here.creature_on_trap( *targ );
            }
        }
    } else if( npc *const targ = critter_at<npc>( tp ) ) {
        auto start_pos = targ->bub_pos();

        if( stun > 0 ) {
            targ->add_effect( effect_stunned, 1_turns * stun );
            add_msg( _( "%s was stunned!" ), targ->name );
        }
        for( size_t i = 1; i < traj.size(); i++ ) {
            if( m.impassable( traj[i].xy() ) ||
                m.obstructed_by_vehicle_rotation( tp, traj[i] ) ) {  // oops, we hit a wall!
                targ->setpos( traj[i - 1] );
                force_remaining = traj.size() - i;
                if( stun != 0 ) {
                    targ->add_effect( effect_stunned, 1_turns * force_remaining );
                    if( targ->has_effect( effect_stunned ) ) {
                        add_msg( _( "%s was stunned!" ), targ->name );
                    }

                    std::array<bodypart_id, 8> bps = {{
                            bodypart_id( "head" ),
                            bodypart_id( "arm_l" ), bodypart_id( "arm_r" ),
                            bodypart_id( "hand_l" ), bodypart_id( "hand_r" ),
                            bodypart_id( "torso" ),
                            bodypart_id( "leg_l" ), bodypart_id( "leg_r" )
                        }
                    };
                    for( const bodypart_id &bp : bps ) {
                        if( one_in( 2 ) ) {
                            targ->deal_damage( source, bp, damage_instance( DT_BASH, force_remaining * dam_mult ) );
                        }
                    }
                    targ->check_dead_state();
                }
                m.bash( traj[i], 2 * dam_mult * force_remaining );
                break;
            } else if( critter_at( traj[i] ) ) {
                targ->setpos( traj[i - 1] );
                force_remaining = traj.size() - i;
                if( stun != 0 ) {
                    add_msg( _( "%s was stunned!" ), targ->name );
                    targ->add_effect( effect_stunned, 1_turns * force_remaining );
                }
                traj.erase( traj.begin(), traj.begin() + i );
                const auto &traj_front = traj.front();
                if( critter_at<monster>( traj_front ) ) {
                    add_msg( _( "%s collided with something else and sent it flying!" ),
                             targ->name );
                } else if( npc *const guy = critter_at<npc>( traj_front ) ) {
                    if( guy->male ) {
                        add_msg( _( "%s collided with someone else and sent him flying!" ),
                                 targ->name );
                    } else {
                        add_msg( _( "%s collided with someone else and sent her flying!" ),
                                 targ->name );
                    }
                } else if( u.bub_pos().x() == traj_front.x() && u.bub_pos().y() == traj_front.y() &&
                           ( u.has_trait( trait_LEG_TENT_BRACE ) && ( !u.footwear_factor() ||
                                   ( u.footwear_factor() == .5 && one_in( 2 ) ) ) ) ) {
                    add_msg( _( "%s collided with you, and barely dislodges your tentacles!" ), targ->name );
                } else if( u.bub_pos().x() == traj_front.x() && u.bub_pos().y() == traj_front.y() ) {
                    add_msg( m_bad, _( "%s collided with you and sent you flying!" ), targ->name );
                }
                knockback( traj, stun, dam_mult, source );
                break;
            }
            targ->setpos( traj[i] );
            tp = traj[i];

            if( start_pos != targ->bub_pos() ) {
                map &here = get_map();
                here.creature_on_trap( *targ );
            }
        }
    } else if( u.bub_pos() == tp ) {
        auto start_pos = u.bub_pos();

        if( stun > 0 ) {
            u.add_effect( effect_stunned, 1_turns * stun );
            add_msg( m_bad, vgettext( "You were stunned for %d turn!",
                                      "You were stunned for %d turns!",
                                      stun ),
                     stun );
        }
        for( size_t i = 1; i < traj.size(); i++ ) {
            if( m.impassable( traj[i] ) ||
                m.obstructed_by_vehicle_rotation( tp, traj[i] ) ) { // oops, we hit a wall!
                u.setpos( traj[i - 1] );
                force_remaining = traj.size() - i;
                if( stun != 0 ) {
                    if( u.has_effect( effect_stunned ) ) {
                        add_msg( m_bad, vgettext( "You were stunned AGAIN for %d turn!",
                                                  "You were stunned AGAIN for %d turns!",
                                                  force_remaining ),
                                 force_remaining );
                    } else {
                        add_msg( m_bad, vgettext( "You were stunned for %d turn!",
                                                  "You were stunned for %d turns!",
                                                  force_remaining ),
                                 force_remaining );
                    }
                    u.add_effect( effect_stunned, 1_turns * force_remaining );
                    std::array<bodypart_id, 8> bps = {{
                            bodypart_id( "head" ),
                            bodypart_id( "arm_l" ), bodypart_id( "arm_r" ),
                            bodypart_id( "hand_l" ), bodypart_id( "hand_r" ),
                            bodypart_id( "torso" ),
                            bodypart_id( "leg_l" ), bodypart_id( "leg_r" )
                        }
                    };
                    for( const bodypart_id &bp : bps ) {
                        if( one_in( 2 ) ) {
                            u.deal_damage( source, bp, damage_instance( DT_BASH, force_remaining * dam_mult ) );
                        }
                    }
                    u.check_dead_state();
                }
                m.bash( traj[i], 2 * dam_mult * force_remaining );
                break;
            } else if( critter_at( traj[i] ) ) {
                u.setpos( traj[i - 1] );
                force_remaining = traj.size() - i;
                if( stun != 0 ) {
                    if( u.has_effect( effect_stunned ) ) {
                        add_msg( m_bad, vgettext( "You were stunned AGAIN for %d turn!",
                                                  "You were stunned AGAIN for %d turns!",
                                                  force_remaining ),
                                 force_remaining );
                    } else {
                        add_msg( m_bad, vgettext( "You were stunned for %d turn!",
                                                  "You were stunned for %d turns!",
                                                  force_remaining ),
                                 force_remaining );
                    }
                    u.add_effect( effect_stunned, 1_turns * force_remaining );
                }
                traj.erase( traj.begin(), traj.begin() + i );
                if( critter_at<monster>( traj.front() ) ) {
                    add_msg( _( "You collided with something and sent it flying!" ) );
                } else if( npc *const guy = critter_at<npc>( traj.front() ) ) {
                    if( guy->male ) {
                        add_msg( _( "You collided with someone and sent him flying!" ) );
                    } else {
                        add_msg( _( "You collided with someone and sent her flying!" ) );
                    }
                }
                knockback( traj, stun, dam_mult, source );
                break;
            }
            if( m.has_flag( "LIQUID", u.bub_pos() ) && force_remaining == 0 ) {
                avatar_action::swim( m, u, u.bub_pos() );
            } else {
                u.setpos( traj[i] );
            }
            tp = traj[i];

            if( start_pos != u.bub_pos() ) {
                map &here = get_map();
                here.creature_on_trap( u );
            }
        }
    }
}

void game::use_computer( const tripoint_bub_ms &p )
{
    if( u.has_trait( trait_id( "ILLITERATE" ) ) ) {
        add_msg( m_info, _( "You can not read a computer screen!" ) );
        return;
    }
    if( u.is_blind() ) {
        // we don't have screen readers in game
        add_msg( m_info, _( "You can not see a computer screen!" ) );
        return;
    }
    if( u.has_trait( trait_id( "HYPEROPIC" ) ) && !u.worn_with_flag( flag_FIX_FARSIGHT ) &&
        !u.has_effect( effect_contacts ) && !u.has_bionic( bionic_id( "bio_eye_optic" ) ) ) {
        add_msg( m_info, _( "You'll need to put on reading glasses before you can see the screen." ) );
        return;
    }

    computer *used = m.computer_at( p );

    if( used == nullptr ) {
        if( m.has_flag( "CONSOLE", p ) ) { //Console without map data
            add_msg( m_bad, _( "The console doesn't display anything coherent." ) );
        } else {
            debugmsg( "Tried to use computer at %s - none there", p.to_string() );
        }
        return;
    }

    computer_session( *used ).use();
}

void game::control_vehicle()
{
    static const itype_id fuel_type_animal( "animal" );
    int veh_part = -1;
    vehicle *veh = remoteveh();
    if( veh == nullptr ) {
        if( const optional_vpart_position vp = m.veh_at( u.bub_pos() ) ) {
            veh = &vp->vehicle();
            veh_part = vp->part_index();
        }
    }
    if( veh != nullptr && veh->player_in_control( u ) &&
        veh->avail_part_with_feature( veh_part, "CONTROLS", true ) >= 0 ) {
        veh->use_controls( tripoint_bub_ms( u.bub_pos() ) );
    } else if( veh && veh->player_in_control( u ) &&
               veh->avail_part_with_feature( veh_part, "CONTROL_ANIMAL", true ) >= 0 ) {
        u.controlling_vehicle = false;
        add_msg( m_info, _( "You let go of the reins." ) );
    } else if( veh && ( veh->avail_part_with_feature( veh_part, "CONTROLS", true ) >= 0 ||
                        ( veh->avail_part_with_feature( veh_part, "CONTROL_ANIMAL", true ) >= 0 &&
                          veh->has_engine_type( fuel_type_animal, false ) && veh->has_harnessed_animal() ) ) &&
               u.in_vehicle ) {
        if( u.has_trait( trait_WAYFARER ) ) {
            add_msg( m_info, _( "You refuse to take control of this vehicle." ) );
            return;
        }
        if( !veh->interact_vehicle_locked() ) {
            veh->handle_potential_theft( u );
            return;
        }
        if( veh->engine_on ) {
            if( !veh->handle_potential_theft( u ) ) {
                return;
            }
            u.controlling_vehicle = true;
            add_msg( _( "You take control of the %s." ), veh->name );
        } else {
            if( !veh->handle_potential_theft( u ) ) {
                return;
            }
            veh->start_engines( true );
        }
    } else {    // Start looking for nearby vehicle controls.
        int num_valid_controls = 0;
        std::optional<tripoint_bub_ms> vehicle_position;
        std::optional<vpart_reference> vehicle_controls;
        for( const auto elem : m.points_in_radius( g->u.bub_pos(), 1 ) ) {
            if( const optional_vpart_position vp = m.veh_at( elem ) ) {
                const std::optional<vpart_reference> controls = vp.value().part_with_feature( "CONTROLS", true );
                if( controls ) {
                    num_valid_controls++;
                    vehicle_position = elem;
                    vehicle_controls = controls;
                }
            }
        }
        if( num_valid_controls < 1 ) {
            add_msg( _( "No vehicle controls found." ) );
            return;
        } else if( num_valid_controls > 1 ) {
            vehicle_position = choose_adjacent( _( "Control vehicle where?" ) );
            if( !vehicle_position ) {
                return;
            }
            const optional_vpart_position vp = m.veh_at( *vehicle_position );
            if( vp ) {
                vehicle_controls = vp.value().part_with_feature( "CONTROLS", true );
                if( !vehicle_controls ) {
                    add_msg( _( "The vehicle doesn't have controls there." ) );
                    return;
                }
            } else {
                add_msg( _( "No vehicle there." ) );
                return;
            }
        }
        // If we hit neither of those, there's only one set of vehicle controls, which should already have been found.
        if( vehicle_controls ) {
            veh = &vehicle_controls->vehicle();
            if( !veh->handle_potential_theft( u ) ) {
                return;
            }
            veh->use_controls( tripoint_bub_ms( *vehicle_position ) );
            //May be folded up (destroyed), so need to re-get it
            veh = g->remoteveh();
        }
    }
    if( veh ) {
        // If we reached here, we gained control of a vehicle.
        // Clear vehicle tile memories so occluded tiles fall back to memorized terrain.
        // The terrain_tiles slot already holds correct terrain from first-sight memorization.
        // Ghost-vehicle prevention is handled by draw_vpart clearing while moving.
        std::ranges::for_each( veh->get_points(), [&]( const tripoint_abs_ms & target ) {
            u.clear_memorized_tile( target );
            u.memorize_terrain_tile( target, m.ter( abs_to_bub( target ) ).id().str(), 0, 0 );
        } );
        veh->is_following = false;
        veh->is_patrolling = false;
        veh->autopilot_on = false;
        veh->is_autodriving = false;
    }
}

bool game::npc_menu( npc &who, const bool &force )
{
    if( !force ) {
        const auto allowed = cata::run_hooks( "on_try_npc_interaction",
        [&]( auto & params ) { params["npc"] = &who; }, { .exit_early = true } ).get_or( "allowed", true );
        if( !allowed ) { return false; }
    }
    cata::run_hooks( "on_npc_interaction", [&]( auto & params ) { params["npc"] = &who; } );
    enum choices : int {
        talk = 0,
        swap_pos,
        push,
        examine_wounds,
        use_item,
        sort_armor,
        attack,
        disarm,
        steal,
        control,
        tutorial
    };

    const bool obeys = debug_mode || ( who.is_player_ally() && !who.in_sleep_state() );

    uilist amenu;

    amenu.text = string_format( _( "What to do with %s?" ), who.disp_name() );
    amenu.addentry( talk, true, 't', _( "Talk" ) );
    amenu.addentry( swap_pos, obeys && !who.is_mounted() &&
                    !u.is_mounted(), 's', _( "Swap positions" ) );
    amenu.addentry( push, obeys && !who.is_mounted(), 'p', _( "Push away" ) );
    amenu.addentry( examine_wounds, true, 'w', _( "Examine wounds" ) );
    amenu.addentry( use_item, true, 'i', _( "Use item on" ) );
    amenu.addentry( sort_armor, obeys, 'r', _( "Sort armor" ) );
    amenu.addentry( attack, true, 'a', _( "Attack" ) );
    if( !who.is_player_ally() ) {
        amenu.addentry( disarm, who.is_armed(), 'd', _( "Disarm" ) );
        amenu.addentry( steal, !who.is_enemy(), 'S', _( "Steal" ) );
    }
    if( who.is_player_ally() ) {
        amenu.addentry( control, who.is_player_ally(), 'c', _( "Control" ) );
        amenu.addentry( tutorial, true, 'T', _( "NPC Ally Tutorial" ) );
    }

    amenu.query();

    const int choice = amenu.ret;
    if( choice == talk ) {
        who.talk_to_u();
    } else if( choice == swap_pos ) {
        if( !prompt_dangerous_tile( who.bub_pos() ) ) {
            return true;
        }
        // TODO: Make NPCs protest when displaced onto dangerous crap
        add_msg( _( "You swap places with %s." ), who.name );
        swap_critters( u, who );
        // TODO: Make that depend on stuff
        u.mod_moves( -200 );
    } else if( choice == push ) {
        // TODO: Make NPCs protest when displaced onto dangerous crap
        auto oldpos = who.bub_pos();
        who.move_away_from( u.bub_pos(), true );
        u.mod_moves( -20 );
        if( oldpos != who.bub_pos() ) {
            add_msg( _( "%s moves out of the way." ), who.name );
        } else {
            add_msg( m_warning, _( "%s has nowhere to go!" ), who.name );
        }
    } else if( choice == examine_wounds ) {
        ///\EFFECT_PER slightly increases precision when examining NPCs' wounds

        ///\EFFECT_FIRSTAID increases precision when examining NPCs' wounds
        const bool precise = u.get_skill_level( skill_firstaid ) * 4 + u.per_cur >= 20;
        who.body_window( _( "Limbs of: " ) + who.disp_name(), true, precise, 0, 0, 0, 0.0f, 0.0f, 0.0f,
                         0.0f, 0.0f );
    } else if( choice == use_item ) {
        static const std::string heal_string( "heal" );
        const auto will_accept = []( const item & it ) {
            const auto use_fun = it.get_use( heal_string );
            if( use_fun == nullptr ) {
                return false;
            }

            const auto *actor = dynamic_cast<const heal_actor *>( use_fun->get_actor_ptr() );

            return actor != nullptr &&
                   actor->limb_power >= 0 &&
                   actor->head_power >= 0 &&
                   actor->torso_power >= 0;
        };
        item *loc = game_menus::inv::titled_filter_menu( will_accept, u, _( "Use which item?" ) );

        if( !loc ) {
            add_msg( _( "Never mind" ) );
            return false;
        }
        item &used = *loc;
        bool did_use = u.invoke_item( &used, heal_string, who.bub_pos() );
        if( did_use ) {
            // Note: exiting a body part selection menu counts as use here
            u.mod_moves( -300 );
        }
    } else if( choice == sort_armor ) {
        show_armor_layers_ui( who );
        u.mod_moves( -100 );
    } else if( choice == attack ) {
        if( who.is_enemy() || query_yn( _( "You may be attacked!  Proceed?" ) ) ) {
            u.melee_attack( who, true );
            who.on_attacked( u );
        }
    } else if( choice == disarm ) {
        if( who.is_enemy() || query_yn( _( "You may be attacked!  Proceed?" ) ) ) {
            avatar_funcs::try_disarm_npc( u, who );
        }
    } else if( choice == steal && query_yn( _( "You may be attacked!  Proceed?" ) ) ) {
        avatar_funcs::try_steal_from_npc( u, who );
    } else if( choice == control ) {
        get_avatar().control_npc( who );
    } else if( choice == tutorial ) {
        enum tutorial_opts : int {
            tut_mutiny = 0,
            tut_instructions,
            tut_radio,
            tut_shout,
            tut_combat,
            tut_training,
            tut_inventory,
            tut_medic,
            tut_activities,
            tut_movement,
            tut_horses,
            tut_guard,
            tut_bionics,
            tut_social,
            tut_misc,
            tut_factions
        };

        bool show_tutorial = true;
        while( show_tutorial ) {
            uilist tutorial_menu;
            tutorial_menu.text = _( "NPC Ally Tutorial" );
            tutorial_menu.desc_enabled = true;

            tutorial_menu.addentry_desc( tut_mutiny, true, 'u',
                                         _( "Mutiny" ),
                                         _( "How to keep your allies happy and prevent mutiny." ) );
            tutorial_menu.addentry_desc( tut_instructions, true, 'i',
                                         _( "Giving Instructions" ),
                                         _( "How to communicate with allies through conversation." ) );
            tutorial_menu.addentry_desc( tut_radio, true, 'r',
                                         _( "Radio Communication" ),
                                         _( "Using two-way radios to contact allies remotely." ) );
            tutorial_menu.addentry_desc( tut_shout, true, 's',
                                         _( "Shouted Commands" ),
                                         _( "Quick orders you can shout during emergencies." ) );
            tutorial_menu.addentry_desc( tut_combat, true, 'c',
                                         _( "Combat" ),
                                         _( "How allies behave in combat and weapon usage rules." ) );
            tutorial_menu.addentry_desc( tut_training, true, 't',
                                         _( "Training and Learning" ),
                                         _( "Teaching and learning skills." ) );
            tutorial_menu.addentry_desc( tut_inventory, true, 'n',
                                         _( "Inventory and Equipment" ),
                                         _( "How allies carry items and manage their gear." ) );
            tutorial_menu.addentry_desc( tut_medic, true, 'm',
                                         _( "First Aid" ),
                                         _( "How allies perform medical treatment." ) );
            tutorial_menu.addentry_desc( tut_activities, true, 'a',
                                         _( "Work Activities" ),
                                         _( "Tasks allies can perform: farming, construction, etc." ) );
            tutorial_menu.addentry_desc( tut_movement, true, 'v',
                                         _( "Movement and Travel" ),
                                         _( "Ordering allies to travel to distant locations." ) );
            tutorial_menu.addentry_desc( tut_horses, true, 'h',
                                         _( "Riding Horses" ),
                                         _( "Mounting and riding horses for faster travel." ) );
            tutorial_menu.addentry_desc( tut_guard, true, 'g',
                                         _( "Guard Duty" ),
                                         _( "How allies guard areas and investigate noises." ) );
            tutorial_menu.addentry_desc( tut_bionics, true, 'b',
                                         _( "Bionics" ),
                                         _( "Installing and using bionic augmentations." ) );
            tutorial_menu.addentry_desc( tut_social, true, 'S',
                                         _( "Social Interaction" ),
                                         _( "Chatting, morale, and asking for advice." ) );
            tutorial_menu.addentry_desc( tut_misc, true, 'M',
                                         _( "Miscellaneous Rules" ),
                                         _( "Doors, item pickup, vehicle seats, and other settings." ) );
            tutorial_menu.addentry_desc( tut_factions, true, 'f',
                                         _( "Other Survivors and Factions" ),
                                         _( "Information about other survivors and faction dynamics." ) );

            tutorial_menu.query();

            std::string info_text;
            std::string info_title;

            switch( tutorial_menu.ret ) {
                case tut_mutiny:
                    info_title = _( "Mutiny" );
                    info_text = _(
                                    "Allies follow you to improve their chances of survival, but they have limits. "
                                    "Mistreating allies or failing to keep them cared for will lower their opinion of you. "
                                    "If it gets too bad, they may mutiny.\n\n"
                                    "When an ally mutinies, they will leave your group. If you have other followers, "
                                    "they may convince some to join them. A mutinied ally becomes hostile and may "
                                    "fight you for resources. "
                                    "You can prevent this by keeping them fed and hydrated, avoiding suicidal orders, "
                                    "and keeping them in safe areas while you're away." );
                    break;

                case tut_instructions:
                    info_title = _( "Giving Instructions" );
                    info_text = _(
                                    "DIRECT CONVERSATION:\n"
                                    "Walk into an adjacent ally to start talking. Through conversation you can give "
                                    "detailed instructions about combat, sleep, activities, and more.\n\n"
                                    "SHOUTING (Chat Command):\n"
                                    "Use the Chat command (default: 'C') to shout orders to allies who aren't adjacent. "
                                    "They must be able to hear you.\n\n"
                                    "RADIO:\n"
                                    "If both you and an ally have two-way radios, use the Faction Manager\n"
                                    " (default: '#') to contact them at greater distances.\n\n"
                                    "INSTRUCTION OVERRIDES:\n"
                                    "Some shouted commands create temporary overrides to standing instructions. "
                                    "Allies will inform you which instructions are currently overridden. "
                                    "You can clear overrides through conversation or by shouting the appropriate command." );
                    break;

                case tut_radio:
                    info_title = _( "Radio Communication" );
                    info_text = _(
                                    "REQUIREMENTS:\n"
                                    "You must have a two-way radio\n"
                                    "The ally must have a two-way radio\n"
                                    "The ally must be within radio range\n"
                                    "The signal must not be blocked by terrain\n\n"
                                    "Open the Faction Manager (default keybind: '#') to contact allies by radio.\n"
                                    "While you can have normal conversations via radio, you cannot trade items, "
                                    "receive training, or transfer physical items. "
                                    "Radio is particularly useful for ordering distant allies to travel to your location." );
                    break;

                case tut_shout:
                    info_title = _( "Shouted Commands" );
                    info_text = _(
                                    "The Chat command (default: 'C') allows you to shout quick orders. "
                                    "All allies who hear you will respond to the command. You can also "
                                    "use it to chat at a distance.\n\n"
                                    "Some shouted commands create temporary overrides to standing instructions. "
                                    "For example, telling allies to prepare for danger will override their normal "
                                    "door, sleep, and retreat behaviors until you tell them to relax.\n"
                                    "NOTE: Shouting may attract unwanted attention from nearby enemies." );
                    break;

                case tut_combat:
                    info_title = _( "Combat" );
                    info_text = _(
                                    "Allies will fight to survive and to protect you. "
                                    "If the situation becomes too dangerous, allies will flee. You cannot prevent this, "
                                    "but you can designate retreat points using the Zone Manager (default: 'Y').\n"
                                    "Allies will flee toward the nearest designated safe zone. "
                                    "Zones can be set on vehicles, so allies can retreat to your car.\n\n"
                                    "If you're fleeing but an ally thinks it's safe, they may stay and fight. "
                                    "You can order them to flee with you, and they'll try to stick close.\n\n"
                                    "You can set rules for combat through dialogue. This includes:\n"
                                    "When, how, and what enemies to engage\n"
                                    "How much they care about aiming accuracy\n"
                                    "How close they stick to you\n"
                                    "Grenade usage\n"
                                    "If they try to avoid hitting allies with ranged weapons (unreliable) "
                                    "To hold a chokepoint, but they may leave to fight threats elsewhere\n\n" );
                    break;

                case tut_training:
                    info_title = _( "Training and Learning" );
                    info_text = _(
                                    "If an ally has higher skill than you, they can teach you. Teaching is tedious, "
                                    "so they won't do it often. They won't teach during danger, hunger, tiredness, "
                                    "or while driving\n\n"
                                    "When you read a skill book in a safe location, nearby allies without that skill "
                                    "will listen and learn. You can even read books for skills you already have.\n\n"
                                    "You can give an ally a skill book appropriate for their level, then talk to them "
                                    "about their current activity and tell them to read it. They'll study on their own.\n"
                                    "Note: While reading, they won't follow you - ensure they're in a safe location.\n"
                                    "Allies cannot learn or teach martial arts styles." );
                    break;

                case tut_inventory:
                    info_title = _( "Inventory and Equipment" );
                    info_text = _(
                                    "Allies can carry items and manage their own equipment.\n"
                                    "Give items to allies and they'll carry them. They have limited pocket space "
                                    "and strength - items that are too heavy or bulky will be dropped.\n\n"
                                    "Allies choose what to wear themselves. If they're wearing something inappropriate, "
                                    "bump into them and use 'Sort armor' to help them adjust. "
                                    "Allies dislike wearing bulky or ill-fitting gear and may remove such items.\n\n"
                                    "Friendly allies will give you any item they're carrying without question. "
                                    "Be careful what you give allies food or drink; They may eat or drink it. "
                                    "Allies with bionics will consume bionic fuel they're carrying" );
                    break;

                case tut_medic:
                    info_title = _( "First Aid" );
                    info_text = _(
                                    "Allies can perform first aid if supply them with bandages or antiseptic. "
                                    "Give these supplies to an ally and they'll use them to treat wounds.\n"
                                    "Allies prioritize treatment in the order of themselves, you, and other allies.\n"
                                    "Allies with medical training will be more effective at treating injuries. "
                                    "Check an ally's skills through conversation to see their first aid ability." );
                    break;

                case tut_activities:
                    info_title = _( "Work Activities" );
                    info_text = _(
                                    "Allies can perform various tasks when you designate work areas in "
                                    "the zone manager. (default: 'Y')\n"
                                    "Use the Zone Manager to designate loot sorting zones, construction blueprints, "
                                    "farming areas, tree cutting areas, vehicle disassembly/repair zones, and fishing spots.\n"
                                    "You can assign tasks to allies by talking to them about their current activity. "
                                    "Leave required tools in a loot zone near the work area. "
                                    "Allies will return tools to an unsorted loot zone when finished.\n"
                                    "They can do the following tasks:\n"
                                    "Sort loot\n"
                                    "Building structures\n"
                                    "Cutting down trees (needs axes)\n"
                                    "Repairing or dismantling vehicles (needs wrenches, hacksaws, or toolboxes)\n"
                                    "Farming (needs shovels, seeds, and fertilizer)\n"
                                    "Fishing" );
                    break;
                case tut_movement:
                    info_title = _( "Movement and Travel" );
                    info_text = _(
                                    "Allies can travel to distant locations on command.\n"
                                    "You can contact an ally by radio and tell them to come to you. "
                                    "They'll walk to your location and then guard the area. "
                                    "Travel takes time depending on distance. Allies avoid dangers along the way. "
                                    "Allies on horseback will travel faster.\n\n"
                                    "By default, allies follow you. Use 'Guard' commands to make them stay in place, "
                                    "and 'Follow' commands to resume following." );
                    break;

                case tut_horses:
                    info_title = _( "Riding Horses" );
                    info_text = _(
                                    "Allies can ride horses for faster travel. They need a friendly horse (tamed) with "
                                    "a saddle installed on it.\n\n"
                                    "Feed a feral horse cattlefodder to make it friendly.\n"
                                    "Examine (default: 'e') the horse while you have a saddle in your inventory to install it.\n\n"
                                    "Tell an ally to mount up and they'll find a nearby saddled, friendly horse.\n"
                                    "Tell an ally to dismount, or they can use vehicle controls.\n"
                                    "You can dismount yourself using the vehicle control key (default: '^'). "
                                    "While mounted, you and allies move normally but much faster than on foot." );
                    break;


                case tut_guard:
                    info_title = _( "Guard Duty" );
                    info_text = _(
                                    "Allies can guard locations and protect your belongings. "
                                    "Tell an ally to guard and they'll stay at their current position. "
                                    "If in a vehicle, they'll stay with the vehicle. "
                                    "Guards deter other survivors from stealing your stuff.\n\n"
                                    "Guards who hear suspicious noises will investigate unless told not to. "
                                    "This can be dangerous as they might find enemies, but they won't get ambushed in the dark.\n\n"
                                    "Use zones to control noise investigation.\n"
                                    "NO-INVESTIGATE ZONE: Allies ignore noises from this area\n"
                                    "INVESTIGATE-ONLY ZONE: Allies only investigate noises from this area\n"
                                    "No-investigate zones take priority over investigate-only zones.\n"
                                    "WARNING: If you set an investigate-only zone anywhere, allies won't investigate "
                                    "noises from outside that zone, even if it's far away. Use carefully." );
                    break;

                case tut_bionics:
                    info_title = _( "Bionics" );
                    info_text = _(
                                    "Examine a CBM to see if an ally can use it. Allies can use most passive bionics, but "
                                    "only some active bionics will be used.\n"
                                    "To install buonics in an ally, follow these steps:\n"
                                    "  1. Find a programmable surgical installer or autodoc\n"
                                    "  2. Position the ally on the operating couch\n"
                                    "  3. Activate the installer and select the CBM to install\n"
                                    "If the ally has better installation skill, they'll do the procedure themselves. "
                                    "Similarly, skilled allies can install CBMs into you.\n\n"
                                    "Allies use active bionics sensibly in normal situations. "
                                    "In danger, they'll activate anything that gives an edge. "
                                    "Allies use bionic weapons if they're better than carried weapons. "
                                    "You can set an energy reserve level if you want them to save power for defense/healing. "
                                    "Allies try to keep bionic energy topped off. You can tell them to stop recharging "
                                    "when not full if supplies are low. They'll warn you if they're low on energy or fuel." );
                    break;

                case tut_social:
                    info_title = _( "Social Interaction" );
                    info_text = _(
                                    "Allies are social beings who benefit from interaction. "
                                    "Light conversation improves morale. However, allies don't want to chat constantly. "
                                    "If you've talked recently, they may not want to chat again right away.\n\n"
                                    "You can ask allies about their background, but some may not want to discuss it.\n"
                                    "You can allies for advice. They may share survival tips they've picked up.\n"
                                    "Note: Advice may not always be relevant to your current situation." );
                    break;

                case tut_misc:
                    info_title = _( "Miscellaneous Rules" );
                    info_text = _(
                                    "You can configure various rules for allies that control their behavior.\n\n"
                                    "Allies report danger, injuries, hunger, and other important information.\n"
                                    "You can tell them to be quiet if you don't want these notifications.\n\n"
                                    "Allies can pulp zombie corpses to prevent revival. Most prefer to do this.\n\n"
                                    "Allies investigate suspicious noises by default. You can disable this.\n\n"
                                    "You can decide if they leave doors open while passing through them or not,"
                                    "and if they go through closed doors.\n\n"
                                    "Allies can pick up items automatically.  You can specify what types to collect.\n"
                                    "Tip: If allies have bows or crossbows, tell them to pick up ammunition.\n\n"
                                    "Examine a vehicle and use the crew 'w' command to assign allies to specific seats.\n" );
                    break;

                case tut_factions:
                    info_title = _( "Other Survivors and Factions" );
                    info_text = _(
                                    "You're not the only survivor out there.\n"
                                    "Other survivors scavenge just like you. Unattended loot looks like fair game to them. "
                                    "If you or an ally isn't around to claim your stuff, other survivors may take it. "
                                    "A solution is to have someone guard your base or vehicle.\n\n"
                                    "Survivors form groups for better survival chances. Your group is one such faction.\n\n"
                                    "In the faction manager (default: '#') you can view a list of all your allies, "
                                    "and all factions you've encountered.\n"
                                    "Some factions are lone survivors, others have many members and fortified bases." );
                    break;
                default:
                    show_tutorial = false;
                    break;
            }

            if( show_tutorial && !info_text.empty() ) {
                full_screen_popup( "%s\n\n%s", info_title, info_text );
            }
        }
    }

    return true;
}

void game::examine()
{
    // if we are driving a vehicle, examine the
    // current tile without asking.
    const optional_vpart_position vp = m.veh_at( u.bub_pos() );
    if( vp && vp->vehicle().player_in_control( u ) ) {
        examine( u.bub_pos() );
        return;
    }

    const std::optional<tripoint_bub_ms> examp_ = choose_adjacent_highlight( _( "Examine where?" ),
        _( "There is nothing that can be examined nearby." ),
        ACTION_EXAMINE, false );
    if( !examp_ ) {
        return;
    }
    u.manual_examine = true;
    examine( *examp_ );
    u.manual_examine = false;
}

static std::string get_fire_fuel_string( const tripoint_bub_ms &examp )
{
    map &here = get_map();
    if( here.has_flag( TFLAG_FIRE_CONTAINER, examp ) ) {
        field_entry *fire = here.get_field( examp, fd_fire );
        if( fire ) {
            std::string ss;
            ss += _( "There is a fire here." );
            ss += " ";
            if( fire->get_field_intensity() > 1 ) {
                ss += _( "It's too big and unpredictable to evaluate how long it will last." );
                return ss;
            }
            time_duration fire_age = fire->get_field_age();
            // half-life inclusion
            int mod = 5 - g->u.get_skill_level( skill_survival );
            mod = std::max( mod, 0 );
            if( fire_age >= 0_turns ) {
                if( mod >= 4 ) { // = survival level 0-1
                    ss += _( "It's going to go out soon without extra fuel." );
                    return ss;
                } else {
                    fire_age = 30_minutes - fire_age;
                    if( to_string_approx( fire_age - fire_age * mod / 5 ) == to_string_approx(
                            fire_age + fire_age * mod / 5 ) ) {
                        ss += string_format(
                                  _( "Without extra fuel it might burn yet for maybe %s, but might also go out sooner." ),
                                  to_string_approx( fire_age - fire_age * mod / 5 ) );
                    } else {
                        ss += string_format(
                                  _( "Without extra fuel it might burn yet for between %s to %s, but might also go out sooner." ),
                                  to_string_approx( fire_age - fire_age * mod / 5 ),
                                  to_string_approx( fire_age + fire_age * mod / 5 ) );
                    }
                    return ss;
                }
            } else {
                fire_age = fire_age * -1 + 30_minutes;
                if( mod >= 4 ) { // = survival level 0-1
                    if( fire_age <= 1_hours ) {
                        ss += _( "It's quite decent and looks like it'll burn for a bit without extra fuel." );
                        return ss;
                    } else if( fire_age <= 3_hours ) {
                        ss += _( "It looks solid, and will burn for a few hours without extra fuel." );
                        return ss;
                    } else {
                        ss += _( "It's very well supplied and even without extra fuel might burn for at least a part of a day." );
                        return ss;
                    }
                } else {
                    if( to_string_approx( fire_age - fire_age * mod / 5 ) == to_string_approx(
                            fire_age + fire_age * mod / 5 ) ) {
                        ss += string_format( _( "Without extra fuel it will burn for about %s." ),
                                             to_string_approx( fire_age - fire_age * mod / 5 ) );
                    } else {
                        ss += string_format( _( "Without extra fuel it will burn for between %s to %s." ),
                                             to_string_approx( fire_age - fire_age * mod / 5 ),
                                             to_string_approx( fire_age + fire_age * mod / 5 ) );
                    }
                    return ss;
                }
            }
        }
    }
    return {};
}

void game::examine( const tripoint_bub_ms &examp )
{

    Creature *c = critter_at( examp );
    if( c != nullptr ) {
        monster *mon = dynamic_cast<monster *>( c );
        if( mon != nullptr ) {
            if( mon->get_battery_item() && mon->has_effect( effect_pet ) ) {
                const itype &type = *mon->type->mech_battery;
                int max_charge = type.magazine->capacity;
                float charge_percent;
                if( mon->get_battery_item() ) {
                    charge_percent = static_cast<float>( mon->get_battery_item()->ammo_remaining() ) / max_charge * 100;
                } else {
                    charge_percent = 0.0;
                }
                add_msg( _( "There is a %s.  Battery level: %d%%" ), mon->get_name(),
                         static_cast<int>( charge_percent ) );
            } else {
                add_msg( _( "There is a %s." ), mon->get_name() );
            }

            const auto allowed = cata::run_hooks( "on_try_monster_interaction", [&]( auto & params ) { params["monster"] = mon; },
            { .exit_early = true } ).get_or( "allowed", true );
            if( allowed ) {
                if( mon->has_effect( effect_pet ) && !u.is_mounted() ) {
                    if( monexamine::pet_menu( *mon ) ) {
                        return;
                    }
                } else if( ( mon->has_flag( MF_RIDEABLE_MECH ) || mon->has_flag( MF_CARD_OVERRIDE ) ) &&
                           !mon->has_effect( effect_pet ) && mon->attitude_to( u ) != Attitude::A_HOSTILE ) {
                    if( monexamine::mech_hack( *mon ) ) {
                        return;
                    }
                } else if( mon->has_flag( MF_PAY_BOT ) ) {
                    if( monexamine::pay_bot( *mon ) ) {
                        return;
                    }
                } else if( mon->attitude_to( u ) == Attitude::A_FRIENDLY && !u.is_mounted() ) {
                    if( monexamine::mfriend_menu( *mon ) ) {
                        return;
                    }
                }
            }
        } else if( u.is_mounted() ) {
            add_msg( m_warning, _( "You cannot do that while mounted." ) );
        }
        npc *np = dynamic_cast<npc *>( c );
        if( np != nullptr && !u.is_mounted() ) {
            if( npc_menu( *np ) ) {
                return;
            }
        } else if( np != nullptr && u.is_mounted() ) {
            add_msg( m_warning, _( "You cannot do that while mounted." ) );
        }
    }

    const optional_vpart_position vp = m.veh_at( examp );
    if( vp && u.is_mounted() ) {
        if( !u.mounted_creature->has_flag( MF_RIDEABLE_MECH ) ) {
            add_msg( m_warning, _( "You cannot interact with a vehicle while mounted." ) );
        } else {
            vp->vehicle().interact_with( tripoint_bub_ms( examp ), vp->part_index() );
            return;
        }
    } else if( vp && !u.is_mounted() ) {
        vp->vehicle().interact_with( tripoint_bub_ms( examp ), vp->part_index() );
        return;
    }

    if( m.has_flag( "CONSOLE", examp ) && !u.is_mounted() ) {
        use_computer( examp );
        return;
    } else if( m.has_flag( "CONSOLE", examp ) && u.is_mounted() ) {
        add_msg( m_warning, _( "You cannot use a console while mounted." ) );
    }
    const furn_t &xfurn_t = m.furn( examp ).obj();
    const ter_t &xter_t = m.ter( examp ).obj();

    const auto player_pos = u.bub_pos();

    if( m.has_furn( examp ) && !u.is_mounted() ) {
        xfurn_t.examine( u, examp );
    } else if( m.has_furn( examp ) && u.is_mounted() ) {
        add_msg( m_warning, _( "You cannot do that while mounted." ) );
    } else {
        if( !u.is_mounted() ) {
            xter_t.examine( u, examp );
        } else if( u.is_mounted() && xter_t.examine == &iexamine::none ) {
            xter_t.examine( u, examp );
        } else {
            add_msg( m_warning, _( "You cannot do that while mounted." ) );
        }
    }

    // Did the player get moved? Bail out if so; our examp probably
    // isn't valid anymore.
    if( player_pos != u.bub_pos() ) {
        return;
    }

    bool none = true;
    if( xter_t.examine != &iexamine::none || xfurn_t.examine != &iexamine::none ) {
        none = false;
    }

    if( !m.tr_at( examp ).is_null() && !u.is_mounted() ) {
        iexamine::trap( u, examp );
    } else if( !m.tr_at( examp ).is_null() && u.is_mounted() ) {
        add_msg( m_warning, _( "You cannot do that while mounted." ) );
    }

    // In case of teleport trap or somesuch
    if( player_pos != u.bub_pos() ) {
        return;
    }

    // Feedback for fire lasting time, this can be judged while mounted
    const std::string fire_fuel = get_fire_fuel_string( examp );
    if( !fire_fuel.empty() ) {
        add_msg( fire_fuel );
    }

    if( m.has_flag( "SEALED", examp ) ) {
        if( none ) {
            if( m.has_flag( "UNSTABLE", examp ) ) {
                add_msg( _( "The %s is too unstable to remove anything." ), m.name( examp ) );
            } else {
                add_msg( _( "The %s is firmly sealed." ), m.name( examp ) );
            }
        }
    } else {
        //examp has no traps, is a container and doesn't have a special examination function
        if( m.tr_at( examp ).is_null() && m.i_at( examp ).empty() &&
            m.has_flag( "CONTAINER", examp ) && none ) {
            add_msg( _( "It is empty." ) );
        } else if( ( m.has_flag( TFLAG_FIRE_CONTAINER, examp ) &&
                     xfurn_t.examine == &iexamine::fireplace ) ||
                   xfurn_t.examine == &iexamine::fluid_grid_fixture ||
                   xfurn_t.examine == &iexamine::workbench ||
                   xfurn_t.examine == &iexamine::transform ) {
            return;
        } else {
            sounds::process_sound_markers( &u );
            if( !u.is_mounted() ) {
                pickup::pick_up( examp, 0 );
            }
        }
    }
}

void game::pickup()
{
    const std::optional<tripoint_bub_ms> examp_ = choose_adjacent_highlight( _( "Pickup where?" ),
        _( "There is nothing to pick up nearby." ),
        ACTION_PICKUP, false );
    if( !examp_ ) {
        return;
    }
    pickup( *examp_ );
}


void game::pickup( const tripoint_bub_ms &p )
{
    // TODO(tiles-rip-out): re-add a pickup target highlight via the tiles cursor
    // overlay (init_draw_cursor / init_draw_highlight). The old curses w_terrain
    // drawsq highlight was dead under tiles and has been removed.
    if( get_option<bool>( "NEW_PICKUP_MENU" ) ) {
        std::vector<pickup::pick_drop_selection> pickup_list = game_menus::inv::pickup_from_tile( g->u, p );
        g->u.assign_activity( std::make_unique<player_activity>( std::make_unique<pickup_activity_actor>
                              ( pickup_list, g->u.bub_pos() ) ) );
    } else {
        pickup::pick_up( p, 0 );
    }

}

void game::pickup_all()
{
    if( get_option<bool>( "NEW_PICKUP_MENU" ) ) {
        std::vector<pickup::pick_drop_selection> pickup_list = game_menus::inv::pickup_nearby( g->u );
        g->u.assign_activity( std::make_unique<player_activity>( std::make_unique<pickup_activity_actor>
                              ( pickup_list, g->u.bub_pos() ) ) );
    } else {
        pickup::pick_up_all_nearby();
    }

}

void game::pickup_feet()
{
    if( get_option<bool>( "NEW_PICKUP_MENU" ) ) {
        std::vector<pickup::pick_drop_selection> pickup_list = game_menus::inv::pickup_from_tile( g->u,
            g->u.bub_pos() );
        g->u.assign_activity( std::make_unique<player_activity>( std::make_unique<pickup_activity_actor>
                              ( pickup_list, g->u.bub_pos() ) ) );
    } else {
        pickup::pick_up( u.bub_pos(), 1 );
    }
}

//Shift player by one tile, look_around(), then restore previous position.
//represents carefully peeking around a corner, hence the large move cost.
void game::peek()
{
    const std::optional<tripoint_rel_ms> p = choose_direction( _( "Peek where?" ), true );
    if( !p ) {
        return;
    }

    if( p->z() != 0 ) {
        const auto old_pos = u.bub_pos();
        vertical_move( p->z(), false, true );

        if( old_pos != u.bub_pos() ) {
            vertical_move( p->z() * -1, false, true );
        } else {
            return;
        }
    }

    if( m.impassable( u.bub_pos() + *p ) ||
        m.obstructed_by_vehicle_rotation( u.bub_pos(), u.bub_pos() + *p ) ) {
        return;
    }

    peek( u.bub_pos() + *p );
}

void game::peek( const tripoint_bub_ms &p )
{
    u.moves -= 200;
    auto prev = u.bub_pos();
    u.setpos( p );
    // Force a full cache rebuild from the peek position so look_around renders
    // correct FOV and lighting.  Without this, lightmap_dirty may already be
    // false (built from the pre-peek player position earlier this turn), causing
    // look_around to display stale lighting and visibility.
    m.invalidate_map_cache( p.z() );
    auto center = p;
    const look_around_result result = look_around( /*show_window=*/true, center, center, false, false,
                                      true );
    u.setpos( prev );

    if( result.peek_action && *result.peek_action == PA_BLIND_THROW ) {
        avatar_action::plthrow( u, nullptr, p );
    }
    m.invalidate_map_cache( p.z() );
}
////////////////////////////////////////////////////////////////////////////////////////////

void game::drop()
{
    u.drop( game_menus::inv::multidrop( u ), u.bub_pos() );
}

void game::drop_in_direction()
{
    if( const auto pnt = choose_adjacent( _( "Drop where?" ) ) ) {
        u.drop( game_menus::inv::multidrop( u ), *pnt );
    }
}

static int get_initial_hotkey( const size_t menu_index )
{
    return ( menu_index == 0 ) ? hotkey_for_action( ACTION_BUTCHER ) : -1;
}

// Returns a vector of pairs.
//    Pair.first is the iterator to the first item with a unique tname.
//    Pair.second is the number of equivalent items per unique tname
// There are options for optimization here, but the function is hit infrequently
// enough that optimizing now is not a useful time expenditure.
static std::vector<std::pair<item *, int>> generate_butcher_stack_display(
    const std::vector<item *> &its )
{
    std::vector<std::pair<item *, int>> result;
    std::vector<std::string> result_strings;
    result.reserve( its.size() );
    result_strings.reserve( its.size() );

    for( item * const &it : its ) {
        const std::string tname = it->tname();
        size_t s = 0;
        // Search for the index with a string equivalent to tname
        for( ; s < result_strings.size(); ++s ) {
            if( result_strings[s] == tname ) {
                break;
            }
        }
        // If none is found, this is a unique tname so we need to add
        // the tname to string vector, and make an empty result pair.
        // Has the side effect of making 's' a valid index
        if( s == result_strings.size() ) {
            // make a new entry
            result.emplace_back( it, 0 );
            // Also push new entry string
            result_strings.push_back( tname );
        }
        // Increase count result pair at index s
        ++result[s].second;
    }

    return result;
}

// Corpses are always individual items
// Just add them individually to the menu
static void add_corpses( uilist &menu, const std::vector<item *> &its,
                         size_t &menu_index )
{
    int hotkey = get_initial_hotkey( menu_index );

    for( const item * const &it : its ) {
        const std::string msg_name = it->has_flag( flag_CBM_SCANNED )
                                     ? string_format( _( "%s (bionic detected)" ), it->get_mtype()->nname() )
                                     :  _( it->get_mtype()->nname() );
        menu.addentry( menu_index++, true, hotkey, msg_name );
        hotkey = -1;
    }
}

// Salvagables stack so we need to pass in a stack vector rather than an item index vector
static void add_salvagables( uilist &menu,
                             const std::vector<std::pair<item *, int>> &stacks,
                             size_t &menu_index )
{
    if( !stacks.empty() ) {
        int hotkey = get_initial_hotkey( menu_index );

        for( const auto &stack : stacks ) {
            const item &it = *stack.first;

            //~ Name and number of items listed for cutting up
            const auto &msg = string_format( pgettext( "butchery menu", "Salvage %s (%d)" ),
                                             it.tname(), stack.second );
            menu.addentry_col( menu_index++, true, hotkey, msg,
                               to_string_clipped( time_duration::from_turns( salvage::moves_to_salvage( it ) / 100 ) ) );
            hotkey = -1;
        }
    }
}

// Disassemblables stack so we need to pass in a stack vector rather than an item index vector
static void add_disassemblables( uilist &menu,
                                 const std::vector<std::pair<item *, int>> &stacks, size_t &menu_index )
{
    if( !stacks.empty() ) {
        int hotkey = get_initial_hotkey( menu_index );

        for( const auto &stack : stacks ) {
            const item &it = *stack.first;

            //~ Name, number of items and time to complete disassembling
            const auto &msg = string_format( pgettext( "butchery menu", "%s (%d)" ),
                                             it.tname(), stack.second );
            menu.addentry_col( menu_index++, true, hotkey, msg,
                               to_string_clipped( time_duration::from_turns( recipe_dictionary::get_uncraft(
                                       it.typeId() ).time / 100 ) ) );
            hotkey = -1;
        }
    }
}

// Butchery sub-menu and time calculation
static void butcher_submenu( const std::vector<item *> &corpses, int corpse = -1 )
{
    avatar &you = get_avatar();
    const inventory &inv = you.crafting_inventory();

    const int factor = inv.max_quality( quality_id( "BUTCHER" ) );
    const std::string msg_inv = factor > INT_MIN
                                ? string_format( _( "Your best tool has <color_cyan>%d butchering</color>." ), factor )
                                :  _( "You have no butchering tool." );

    const int factor_diss = inv.max_quality( quality_id( "CUT_FINE" ) );
    const std::string msg_inv_diss = factor_diss > INT_MIN
                                     ? string_format( _( "Your best tool has <color_cyan>%d fine cutting</color>." ), factor_diss )
                                     :  _( "You have no fine cutting tool." );

    auto cut_time = [&]( enum butcher_type bt ) {
        int time_to_cut = 0;
        if( corpse != -1 ) {
            time_to_cut = butcher_time_to_cut( *corpses[corpse], bt );
        } else {
            for( const item * const &it : corpses ) {
                time_to_cut += butcher_time_to_cut( *it, bt );
            }
        }
        return to_string_clipped( time_duration::from_turns( time_to_cut / 100 ) );
    };
    auto info_on_action = [&]( butcher_type type ) {
        int corpse_index = corpse == -1 ? 0 : corpse;
        butchery_setup setup = consider_butchery( *corpses[corpse_index], you, type );
        std::string out;
        for( const std::string &problem : setup.problems ) {
            out += "\n" + colorize( problem, c_red );
        }
        return out;
    };
    const bool enough_light = character_funcs::can_see_fine_details( you );

    bool has_blood = false;
    bool has_skin = false;
    bool has_organs = false;

    // check if either the specific corpse has skin/organs or if any
    // of the corpses do in case of a batch job
    int i = 0;
    for( const item * const &it : corpses ) {
        // only interested in a specific corpse, skip the rest
        if( corpse != -1 && corpse != i ) {
            ++i;
            continue;
        }
        ++i;

        const mtype *dead_mon = it->get_mtype();
        if( dead_mon != nullptr ) {
            for( const harvest_entry &entry : dead_mon->harvest.obj() ) {
                if( entry.type == "blood" ) {
                    has_blood = true;
                }
                if( entry.type == "skin" ) {
                    has_skin = true;
                }
                if( entry.type == "offal" ) {
                    has_organs = true;
                }
            }
        }
    }

    uilist smenu;
    smenu.desc_enabled = true;
    smenu.text = _( "Choose type of butchery:" );

    const std::string cannot_see = colorize( _( "can't see!" ), c_red );

    smenu.addentry_col( BUTCHER, enough_light, 'B', _( "Quick butchery" ),
                        enough_light ? cut_time( BUTCHER ) : cannot_see,
                        string_format( "%s  %s%s",
                                       _( "This technique is used when you are in a hurry, "
                                          "but still want to harvest something from the corpse. "
                                          " Yields are lower as you don't try to be precise, "
                                          "but it's useful if you don't want to set up a workshop.  "
                                          "Prevents zombies from raising." ),
                                       msg_inv, info_on_action( BUTCHER ).c_str() ) );
    smenu.addentry_col( BUTCHER_FULL, enough_light, 'b', _( "Full butchery" ),
                        enough_light ? cut_time( BUTCHER_FULL ) : cannot_see,
                        string_format( "%s  %s%s",
                                       _( "This technique is used to properly butcher a corpse.  "
                                          "For corpses larger than medium size, you will require "
                                          "a rope & a tree, a butchering rack or a flat surface "
                                          "(for ex. a table, a leather tarp, etc.).  "
                                          "Yields are plentiful and varied, but it is time consuming." ),
                                       msg_inv, info_on_action( BUTCHER_FULL ).c_str() ) );
    smenu.addentry_col( BLEED, enough_light &&
                        has_blood, 'l', _( "Bleed corpse" ),
                        enough_light ? ( has_blood ? cut_time( BLEED ) : colorize( _( "has no blood" ),
                                         c_red ) ) : cannot_see,
                        string_format( "%s  %s%s",
                                       _( "Bleeding involves severing the carotid arteries and jugular "
                                          "veins, or the blood vessels from which they arise.  "
                                          "You need skill and an appropriately sharp and precise knife "
                                          "to do a good job." ),
                                       msg_inv, info_on_action( BLEED ).c_str() ) );
    smenu.addentry_col( F_DRESS, enough_light &&
                        has_organs, 'f', _( "Field dress corpse" ),
                        enough_light ? ( has_organs ? cut_time( F_DRESS ) : colorize( _( "has no organs" ),
                                         c_red ) ) : cannot_see,
                        string_format( "%s  %s%s",
                                       _( "Technique that involves removing internal organs and "
                                          "viscera to protect the corpse from rotting from inside.  "
                                          "Yields internal organs.  Carcass will be lighter and will "
                                          "stay fresh longer.  Can be combined with other methods for "
                                          "better effects." ),
                                       msg_inv, info_on_action( F_DRESS ).c_str() ) );
    smenu.addentry_col( SKIN, enough_light &&
                        has_skin, 's', _( "Skin corpse" ),
                        enough_light ? ( has_skin ? cut_time( SKIN ) : colorize( _( "has no skin" ), c_red ) ) : cannot_see,
                        string_format( "%s  %s%s",
                                       _( "Skinning a corpse is an involved and careful process that "
                                          "usually takes some time.  You need skill and an appropriately "
                                          "sharp and precise knife to do a good job.  Some corpses are "
                                          "too small to yield a full-sized hide and will instead produce "
                                          "scraps that can be used in other ways." ),
                                       msg_inv, info_on_action( SKIN ).c_str() ) );
    smenu.addentry_col( QUARTER, enough_light, 'k', _( "Quarter corpse" ),
                        enough_light ? cut_time( QUARTER ) : cannot_see,
                        string_format( "%s  %s%s",
                                       _( "By quartering a previously field dressed corpse you will "
                                          "acquire four parts with reduced weight and volume.  It "
                                          "may help in transporting large game.  This action destroys "
                                          "skin, hide, pelt, etc., so don't use it if you want to "
                                          "harvest them later." ),
                                       msg_inv, info_on_action( QUARTER ).c_str() ) );
    smenu.addentry_col( DISMEMBER, true, 'm', _( "Dismember corpse" ), cut_time( DISMEMBER ),
                        string_format( "%s  %s%s",
                                       _( "If you're aiming to just destroy a body outright and don't "
                                          "care about harvesting it, dismembering it will hack it apart "
                                          "in a very short amount of time but yields little to no usable flesh." ),
                                       msg_inv, info_on_action( DISMEMBER ).c_str() ) );
    smenu.addentry_col( DISSECT, enough_light, 'd', _( "Dissect corpse" ),
                        enough_light ? cut_time( DISSECT ) : cannot_see,
                        string_format( "%s  %s%s",
                                       _( "By careful dissection of the corpse, you will examine it for "
                                          "possible bionic implants, or discrete organs and harvest them "
                                          "if possible.  Requires scalpel-grade cutting tools, and ruins "
                                          "the corpse.  Your medical knowledge is most useful here." ),
                                       msg_inv_diss, info_on_action( DISSECT ).c_str() ) );
    smenu.query();
    switch( smenu.ret ) {
        case BUTCHER:
        case BUTCHER_FULL:
        case F_DRESS:
        case BLEED:
        case SKIN:
        case QUARTER:
        case DISMEMBER:
        case DISSECT: {
            std::vector<item *> targets;
            if( corpse != -1 ) {
                targets.push_back( corpses[corpse] );
            } else {
                targets = corpses;
            }
            you.assign_activity( std::make_unique<player_activity>(
                                     std::make_unique<butchery_activity_actor>(
                                         static_cast<butcher_type>( smenu.ret ), targets,
                                         get_map().bub_to_abs( you.bub_pos() )
                                     )
                                 ) );
            break;
        }
        default:
            return;
    }
}

void game::butcher()
{
    if( u.controlling_vehicle ) {
        add_msg( m_info, _( "You can't butcher while driving!" ) );
        return;
    }

    const int factor = u.max_quality( quality_id( "BUTCHER" ) );
    const int factorD = u.max_quality( quality_id( "CUT_FINE" ) );
    const std::string no_knife_msg = _( "You don't have a butchering tool." );
    const std::string no_corpse_msg = _( "There are no corpses here to butcher." );

    //You can't butcher on sealed terrain- you have to smash/shovel/etc it open first
    if( m.has_flag( "SEALED", u.bub_pos() ) ) {
        if( m.sees_some_items( u.bub_pos(), u ) ) {
            add_msg( m_info, _( "You can't access the items here." ) );
        } else if( factor > INT_MIN || factorD > INT_MIN ) {
            add_msg( m_info, no_corpse_msg );
        } else {
            add_msg( m_info, no_knife_msg );
        }
        return;
    }

    const item *first_item_without_tools = nullptr;
    // Indices of relevant items
    std::vector<item *> corpses;
    std::vector<item *> disassembles;
    std::vector<item *> salvageables;
    std::vector<item *> unloadables;
    map_stack items = m.i_at( u.bub_pos() );
    const inventory &crafting_inv = u.crafting_inventory();
    auto q_cache = u.crafting_inventory().get_quality_cache();

    // Reserve capacity for each to hold entire item set if necessary to prevent
    // reallocations later on
    corpses.reserve( items.size() );
    salvageables.reserve( items.size() );
    disassembles.reserve( items.size() );
    unloadables.reserve( items.size() );

    // Split into corpses, disassemble-able, and salvageable items
    // It's not much additional work to just generate a corpse list and
    // clear it later, but does make the splitting process nicer.
    for( item *const current_item : items ) {
        if( current_item->is_corpse() ) {
            corpses.push_back( current_item );
        } else {
            if( salvage::try_salvage( *current_item, q_cache ).success() ) {
                salvageables.push_back( current_item );
            }
            if( crafting::can_disassemble( u, *current_item, crafting_inv ).success() ) {
                disassembles.push_back( current_item );
            } else if( !first_item_without_tools ) {
                first_item_without_tools = current_item;
            }
            if( item_funcs::can_be_unloaded( *current_item ) ) {
                unloadables.push_back( current_item );
            }
        }
    }

    // Clear corpses if butcher and dissect factors are INT_MIN
    if( factor == INT_MIN && factorD == INT_MIN ) {
        corpses.clear();
    }

    if( corpses.empty() && disassembles.empty() && salvageables.empty() ) {
        if( factor > INT_MIN || factorD > INT_MIN ) {
            add_msg( m_info, no_corpse_msg );
        } else {
            add_msg( m_info, no_knife_msg );
        }

        if( first_item_without_tools ) {
            add_msg( m_info, _( "You don't have the necessary tools to disassemble any items here." ) );
            // Just for the "You need x to disassemble y" messages
            const auto ret = crafting::can_disassemble( u, *first_item_without_tools, crafting_inv );
            if( !ret.success() ) {
                add_msg( m_info, "%s", ret.c_str() );
            }
        }
        return;
    }

    Creature *hostile_critter = is_hostile_very_close();
    if( hostile_critter != nullptr ) {
        if( !query_yn( _( "You see %s nearby!  Start butchering anyway?" ),
                       hostile_critter->disp_name() ) ) {
            return;
        }
    }

    // Magic indices for special butcher options
    enum : int {
        MULTISALVAGE = MAX_ITEM_IN_SQUARE + 1,
        MULTIBUTCHER,
        MULTIDISASSEMBLE_ONE,
        MULTIDISASSEMBLE_ALL,
        MULTIUNLOAD_ALL,
        NUM_BUTCHER_ACTIONS
    };
    // What are we butchering (i.e.. which vector to pick indices from)
    enum {
        BUTCHER_CORPSE,
        BUTCHER_DISASSEMBLE,
        BUTCHER_SALVAGE,
        BUTCHER_OTHER // For multisalvage etc.
    } butcher_select = BUTCHER_CORPSE;
    // Index to std::vector of iterators...
    int indexer_index = 0;

    // Generate the indexed stacks so we can display them nicely
    const auto disassembly_stacks = generate_butcher_stack_display( disassembles );
    const auto salvage_stacks = generate_butcher_stack_display( salvageables );
    // Always ask before cutting up/disassembly, but not before butchery
    size_t ret = 0;
    if( !corpses.empty() || !disassembles.empty() || !salvageables.empty() ) {
        uilist kmenu;
        kmenu.text = _( "Choose corpse to butcher / item to disassemble" );

        size_t i = 0;
        // Add corpses, disassembleables, and salvagables to the UI
        add_corpses( kmenu, corpses, i );
        add_disassemblables( kmenu, disassembly_stacks, i );
        if( !salvageables.empty() ) {
            add_salvagables( kmenu, salvage_stacks, i );
        }

        if( corpses.size() > 1 ) {
            kmenu.addentry( MULTIBUTCHER, true, 'b', _( "Butcher everything" ) );
        }
        if( disassembles.size() > 1 ) {
            int time_to_disassemble_all = 0;
            int time_to_disassemble_rec = 0;
            std::vector<std::pair<itype_id, int>> disassembly_stacks_res;

            for( const auto &stack : disassembly_stacks ) {
                const int time = recipe_dictionary::get_uncraft( stack.first->typeId() ).time;
                time_to_disassemble_all += time * stack.second;
                disassembly_stacks_res.emplace_back( stack.first->typeId(), stack.second );
            }

            for( int i = 0; i < static_cast<int>( disassembly_stacks_res.size() ); i++ ) {
                const auto dis = recipe_dictionary::get_uncraft( disassembly_stacks_res[i].first );
                time_to_disassemble_rec += dis.time * disassembly_stacks_res[i].second;
                //uses default craft materials to estimate recursive disassembly time
                const auto components = dis.disassembly_requirements().get_components();
                for( const auto &subcomps : components ) {
                    if( !subcomps.empty() ) {
                        disassembly_stacks_res.emplace_back( subcomps.front().type,
                                                             subcomps.front().count * disassembly_stacks_res[i].second );
                    }
                }
            }

            kmenu.addentry_col( MULTIDISASSEMBLE_ONE, true, 'D', _( "Disassemble everything" ),
                                to_string_clipped( time_duration::from_turns( time_to_disassemble_all / 100 ) ) );
            kmenu.addentry_col( MULTIDISASSEMBLE_ALL, true, 'd', _( "Disassemble everything recursively" ),
                                to_string_clipped( time_duration::from_turns( time_to_disassemble_rec / 100 ) ) );
        }
        if( salvageables.size() > 1 ) {
            int time_to_salvage = 0;
            for( const auto &stack : salvage_stacks ) {
                time_to_salvage += salvage::moves_to_salvage( *stack.first ) * stack.second;
            }

            kmenu.addentry_col( MULTISALVAGE, true, 'z', _( "Salvage everything" ),
                                to_string_clipped( time_duration::from_turns( time_to_salvage / 100 ) ) );
        }
        if( unloadables.size() > 1 ) {
            kmenu.addentry_col( MULTIUNLOAD_ALL, true, 'u', _( "Unload Everything" ),
                                std::to_string( unloadables.size() ) + + _( " objects" ) );
        }

        kmenu.query();

        if( kmenu.ret < 0 || kmenu.ret >= NUM_BUTCHER_ACTIONS ) {
            return;
        }

        ret = static_cast<size_t>( kmenu.ret );
        if( ret >= MULTISALVAGE && ret < NUM_BUTCHER_ACTIONS ) {
            butcher_select = BUTCHER_OTHER;
            indexer_index = ret;
        } else if( ret < corpses.size() ) {
            butcher_select = BUTCHER_CORPSE;
            indexer_index = ret;
        } else if( ret < corpses.size() + disassembly_stacks.size() ) {
            butcher_select = BUTCHER_DISASSEMBLE;
            indexer_index = ret - corpses.size();
        } else if( ret < corpses.size() + disassembly_stacks.size() + salvage_stacks.size() ) {
            butcher_select = BUTCHER_SALVAGE;
            indexer_index = ret - corpses.size() - disassembly_stacks.size();
        } else {
            debugmsg( "Invalid butchery index: %d", ret );
            return;
        }
    }

    if( !u.has_morale_to_craft() ) {
        if( butcher_select == BUTCHER_CORPSE || indexer_index == MULTIBUTCHER ) {
            add_msg( m_info,
                     _( "You are not in the mood and the prospect of guts and blood on your hands convinces you to turn away." ) );
        } else {
            add_msg( m_info,
                     _( "You are not in the mood and the prospect of work stops you before you begin." ) );
        }
        return;
    }
    const auto helpers = character_funcs::get_crafting_helpers( u );
    for( const npc *np : helpers ) {
        add_msg( m_info, _( "%s helps with this task…" ), np->name );
    }
    switch( butcher_select ) {
        case BUTCHER_OTHER:
            switch( indexer_index ) {
                case MULTISALVAGE:
                    salvage::salvage_all( u );
                    break;
                case MULTIBUTCHER:
                    butcher_submenu( corpses );
                    break;
                case MULTIDISASSEMBLE_ONE:
                    crafting::disassemble_all( u, false );
                    break;
                case MULTIDISASSEMBLE_ALL:
                    crafting::disassemble_all( u, true );
                    break;
                case MULTIUNLOAD_ALL:
                    avatar_action::unload_all( u, false );
                    break;
                default:
                    debugmsg( "Invalid butchery type: %d", indexer_index );
                    return;
            }
            break;
        case BUTCHER_CORPSE: {
            butcher_submenu( corpses, indexer_index );
        }
        break;
        case BUTCHER_DISASSEMBLE: {
            // Pick index of first item in the disassembly stack
            item *const target = disassembly_stacks[indexer_index].first;
            crafting::disassemble( u, *target );
        }
        break;
        case BUTCHER_SALVAGE: {
            item *const target = salvage_stacks[indexer_index].first;
            salvage::salvage_single( u, *target );

        }
        break;
    }
}

#ifdef COOP_ENABLED
auto game::post_action_world_step() -> void
{
    ZoneScopedN( "game::post_action_world_step" );
    // perf probe: per-turn SIM cost (post-input) + the big sub-phases, rolling
    // avg every 120 turns. Renders are ~1ms but frames are ~30ms apart while
    // moving — this finds where the per-turn time actually goes.
    using _perf_clk = std::chrono::steady_clock;
    static double _perf_sim = 0.0, _perf_cache = 0.0, _perf_mon = 0.0, _perf_world = 0.0;
    static int    _perf_n = 0;
    cleanup_arenas();
    if( try_activity_fixed_window_skip() ) {
    return;
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
        if( new_game ) {
            new_game = false;
        } else {
            if( !gamemode ) {
                gamemode = std::make_unique<special_game>();
            }
            gamemode->per_turn();
            calendar::turn += 1_turns;
        }
    }
    // Reset dimension swap flag now that the map is fully loaded and turn is processing
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
    // Reset sound overlay markers from the previous turn so stale sound
    // indicators don't persist into the next world tick (mirrors do_turn()).
    sounds::reset_markers();

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
    if( driving_view_offset.x != 0 || driving_view_offset.y != 0 ) {
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
    DebugLog( DL::Info, DC::Game ) << "[sim][perf] " << _perf_n << " turns avg: sim_total="
                        << ( _perf_sim / _perf_n ) << "ms (build_map_cache=" << ( _perf_cache / _perf_n )
                        << " monmove=" << ( _perf_mon / _perf_n ) << " world_tick="
                        << ( _perf_world / _perf_n ) << ")";
        _perf_sim = _perf_cache = _perf_mon = _perf_world = 0.0;
        _perf_n = 0;
    }
}
#endif // COOP_ENABLED
