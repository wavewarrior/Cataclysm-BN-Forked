#include "dialogue.h" // IWYU pragma: associated

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "activity_type.h"
#include "auto_pickup.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "catalua_hooks.h"
#include "catalua_sol.h"
#include "cata_utility.h"
#include "character.h"
#include "character_effects.h"
#include "character_functions.h"
#include "character_id.h"
#include "clzones.h"
#include "color.h"
#include "condition.h"
#include "debug.h"
#include "enums.h"
#include "flag.h"
#include "faction.h"
#include "game.h"
#include "game_constants.h"
#include "game_inventory.h"
#include "help.h"
#include "input.h"
#include "item.h"
#include "item_category.h"
#include "item_contents.h"
#include "itype.h"
#include "json.h"
#include "line.h"
#include "make_static.h"
#include "magic.h"
#include "map.h"
#include "mapgen_functions.h"
#include "martialarts.h"
#include "messages.h"
#include "message_types.h"
#include "mission.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "npc_class.h"
#include "npctalk.h"
#include "npctrade.h"
#include "options.h"
#include "output.h"
#include "pimpl.h"
#include "player.h"
#include "player_activity.h"
#include "point.h"
#include "recipe.h"
#include "ret_val.h"
#include "rng.h"
#include "skill.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "text_snippets.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"
#include "units.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "tts_synthesizer.h"
#include "tts_voice_registry.h"


static const efftype_id effect_pacified( "pacified" );
static const efftype_id effect_pet( "pet" );
static const skill_id skill_speech( "speech" );
static const bionic_id bio_armor_eyes( "bio_armor_eyes" );
static const bionic_id bio_deformity( "bio_deformity" );
static const bionic_id bio_face_mask( "bio_face_mask" );
static const bionic_id bio_voice( "bio_voice" );
static const trait_id trait_DEBUG_MIND_CONTROL( "DEBUG_MIND_CONTROL" );

// Defined in npctalk.cpp
auto give_item_to( npc &p, bool allow_use ) -> std::string;
auto parse_mod( const dialogue &d, const std::string &attribute, int factor ) -> int;
std::string talk_trial::name() const
{
    static const std::array<std::string, NUM_TALK_TRIALS> texts = { {
            "", translate_marker( "LIE" ), translate_marker( "PERSUADE" ), translate_marker( "INTIMIDATE" ), ""
        }
    };
    if( static_cast<size_t>( type ) >= texts.size() ) {
        debugmsg( "invalid trial type %d", static_cast<int>( type ) );
        return std::string();
    }
    return texts[type].empty() ? std::string() : _( texts[type] );
}

int talk_trial::calc_chance( const dialogue &d ) const
{
    player &u = *d.alpha;
    if( u.has_trait( trait_DEBUG_MIND_CONTROL ) ) {
        return 100;
    }
    const social_modifiers &u_mods = u.get_mutation_social_mods();

    npc &p = *d.beta;
    int chance = difficulty;
    switch( type ) {
        case NUM_TALK_TRIALS:
            debugmsg( "Called calc_chance with invalid talk_trial value %d", type );
            break;
        case TALK_TRIAL_LIE:
            chance += character_effects::talk_skill( u ) -
                      character_effects::talk_skill( p ) +
                      p.op_of_u.trust * 3;
            chance += u_mods.lie;

            //come on, who would suspect a robot of lying?
            if( u.has_bionic( bio_voice ) ) {
                chance += 10;
            }
            if( u.has_bionic( bio_face_mask ) ) {
                chance += 20;
            }
            break;
        case TALK_TRIAL_PERSUADE:
            chance += character_effects::talk_skill( u ) -
                      character_effects::talk_skill( p ) / 2 +
                      p.op_of_u.trust * 2 + p.op_of_u.value;
            chance += u_mods.persuade;

            if( u.has_bionic( bio_face_mask ) ) {
                chance += 10;
            }
            if( u.has_bionic( bio_deformity ) ) {
                chance -= 50;
            }
            if( u.has_bionic( bio_voice ) ) {
                chance -= 20;
            }
            break;
        case TALK_TRIAL_INTIMIDATE:
            chance += character_effects::intimidation( u ) -
                      character_effects::intimidation( p ) +
                      p.op_of_u.fear * 2 - p.personality.bravery * 2;
            chance += u_mods.intimidate;

            if( u.has_bionic( bio_face_mask ) ) {
                chance += 10;
            }
            if( u.has_bionic( bio_armor_eyes ) ) {
                chance += 10;
            }
            if( u.has_bionic( bio_deformity ) ) {
                chance += 20;
            }
            if( u.has_bionic( bio_voice ) ) {
                chance += 20;
            }
            break;
        case TALK_TRIAL_NONE:
            chance = 100;
            break;
        case TALK_TRIAL_CONDITION:
            chance = condition( d ) ? 100 : 0;
            break;
    }
    for( const auto &this_mod : modifiers ) {
        chance += parse_mod( d, this_mod.first, this_mod.second );
    }

    return std::max( 0, std::min( 100, chance ) );
}

bool talk_trial::roll( dialogue &d ) const
{
    player &u = *d.alpha;
    if( type == TALK_TRIAL_NONE || u.has_trait( trait_DEBUG_MIND_CONTROL ) ) {
        return true;
    }
    const int chance = calc_chance( d );
    const bool success = rng( 0, 99 ) < chance;
    if( success ) {
        u.practice( skill_speech, ( 100 - chance ) / 10 );
    } else {
        u.practice( skill_speech, ( 100 - chance ) / 7 );
    }
    return success;
}

dialogue_consequence talk_effect_t::get_consequence( const dialogue &d ) const
{
    if( d.beta->op_of_u.anger + opinion.anger >= d.beta->hostile_anger_level() ) {
        return dialogue_consequence::hostile;
    }
    return guaranteed_consequence;
}

void talk_effect_fun_t::set_add_effect( const JsonObject &jo, const std::string &member,
                                        bool is_npc )
{
    efftype_id new_effect( jo.get_string( member ) );
    time_duration duration = 1_turns;
    bool permanent = false;
    if( jo.has_string( "duration" ) ) {
        const std::string dur_string = jo.get_string( "duration" );
        if( dur_string == "PERMANENT" ) {
            permanent = true;
            if( json_report_strict ) {
                // This is immensely ugly, we need json.just_warn_with_context
                try {
                    jo.throw_error( "Effect permanence has been moved to effect_type.  Set permanence there.",
                                    "duration" );
                } catch( const JsonError &e ) {
                    debugmsg( "\n%s", e.what() );
                }
            }
        } else if( !dur_string.empty() && std::stoi( dur_string ) > 0 ) {
            duration = time_duration::from_turns( std::stoi( dur_string ) );
        }
    } else {
        duration = time_duration::from_turns( jo.get_int( "duration", 1 ) );
    }
    function = [is_npc, new_effect, duration, permanent]( const dialogue & d ) {
        player *actor = d.alpha;
        if( is_npc ) {
            actor = dynamic_cast<player *>( d.beta );
        }
        actor->add_effect( new_effect, duration );
        if( permanent ) {
            actor->get_effect( new_effect ).set_permanent();
        }
    };
}

void talk_effect_fun_t::set_remove_effect( const JsonObject &jo, const std::string &member,
        bool is_npc )
{
    std::string old_effect = jo.get_string( member );
    function = [is_npc, old_effect]( const dialogue & d ) {
        player *actor = d.alpha;
        if( is_npc ) {
            actor = dynamic_cast<player *>( d.beta );
        }
        actor->remove_effect( efftype_id( old_effect ) );
    };
}

void talk_effect_fun_t::set_add_trait( const JsonObject &jo, const std::string &member,
                                       bool is_npc )
{
    std::string new_trait = jo.get_string( member );
    function = [is_npc, new_trait]( const dialogue & d ) {
        player *actor = d.alpha;
        if( is_npc ) {
            actor = dynamic_cast<player *>( d.beta );
        }
        actor->set_mutation( trait_id( new_trait ) );
    };
}

void talk_effect_fun_t::set_remove_trait( const JsonObject &jo, const std::string &member,
        bool is_npc )
{
    std::string old_trait = jo.get_string( member );
    function = [is_npc, old_trait]( const dialogue & d ) {
        player *actor = d.alpha;
        if( is_npc ) {
            actor = dynamic_cast<player *>( d.beta );
        }
        actor->unset_mutation( trait_id( old_trait ) );
    };
}

void talk_effect_fun_t::set_assign_mission( const JsonObject &jo, const std::string &member )
{
    std::string mission_name = jo.get_string( member );
    function = [mission_name]( const dialogue & ) {
        avatar &player_character = get_avatar();

        const mission_type_id &mission_type = mission_type_id( mission_name );
        mission *new_mission = mission::reserve_new( mission_type, character_id() );
        new_mission->assign( player_character );
    };
}

void talk_effect_fun_t::set_finish_mission( const JsonObject &jo, const std::string &member )
{
    std::string mission_name = jo.get_string( member );
    bool success = jo.get_bool( "success" );
    function = [mission_name, success]( const dialogue & ) {
        avatar &player_character = get_avatar();

        const mission_type_id &mission_type = mission_type_id( mission_name );
        std::vector<mission *> missions = player_character.get_active_missions();
        for( mission *mission : missions ) {
            if( mission->mission_id() == mission_type ) {
                if( success ) {
                    mission->wrap_up();
                } else {
                    mission->fail();
                }
                break;
            }
        }
    };
}

void talk_effect_fun_t::set_add_var( const JsonObject &jo, const std::string &member, bool is_npc )
{
    const std::string var_name = get_talk_varname( jo, member );
    const std::string &value = jo.get_string( "value" );
    function = [is_npc, var_name, value]( const dialogue & d ) {
        player *actor = d.alpha;
        if( is_npc ) {
            actor = dynamic_cast<player *>( d.beta );
        }
        actor->set_value( var_name, value );
    };
}

void talk_effect_fun_t::set_remove_var( const JsonObject &jo, const std::string &member,
                                        bool is_npc )
{
    const std::string var_name = get_talk_varname( jo, member, false );
    function = [is_npc, var_name]( const dialogue & d ) {
        player *actor = d.alpha;
        if( is_npc ) {
            actor = dynamic_cast<player *>( d.beta );
        }
        actor->remove_value( var_name );
    };
}

void talk_effect_fun_t::set_adjust_var( const JsonObject &jo, const std::string &member,
                                        bool is_npc )
{
    const std::string var_name = get_talk_varname( jo, member, false );
    const int value = jo.get_int( "adjustment" );
    function = [is_npc, var_name, value]( const dialogue & d ) {
        player *actor = d.alpha;
        if( is_npc ) {
            actor = dynamic_cast<player *>( d.beta );
        }

        int adjusted_value = value;

        const std::string &var = actor->get_value( var_name );
        if( !var.empty() ) {
            adjusted_value += std::stoi( var );
        }

        actor->set_value( var_name, std::to_string( adjusted_value ) );
    };
}

void talk_effect_fun_t::set_u_buy_item( const itype_id &item_name, int cost, int count,
                                        const std::string &container_name )
{
    function = [item_name, cost, count, container_name]( const dialogue & d ) {
        npc &p = *d.beta;
        player &u = *d.alpha;
        if( !npc_trading::pay_npc( p, cost ) ) {
            popup( _( "You can't afford it!" ) );
            return;
        }
        if( container_name.empty() ) {
            detached_ptr<item> new_item = item::spawn( item_name, calendar::turn );
            item &obj = *new_item;
            if( new_item->count_by_charges() ) {
                new_item->mod_charges( count - 1 );
                u.i_add( std::move( new_item ) );
            } else {
                for( int i_cnt = 0; i_cnt < count; i_cnt++ ) {
                    u.i_add( item::spawn( *new_item ) );
                }
            }
            if( count == 1 ) {
                //~ %1%s is the NPC name, %2$s is an item
                popup( _( "%1$s gives you a %2$s." ), p.name, obj.tname() );
            } else {
                //~ %1%s is the NPC name, %2$d is a number of items, %3$s are items
                popup( _( "%1$s gives you %2$d %3$s." ), p.name, count, obj.tname() );
            }
        } else {
            detached_ptr<item> container = item::spawn( container_name, calendar::turn );
            container->put_in( item::spawn( item_name, calendar::turn, count ) );
            //~ %1%s is the NPC name, %2$s is an item
            popup( _( "%1$s gives you a %2$s." ), p.name, container->tname() );
            u.i_add( std::move( container ) );
        }
    };

    // Update structure used by mission descriptions.
    if( cost <= 0 ) {
        likely_rewards.emplace_back( count, item_name );
    }
}

void talk_effect_fun_t::set_u_sell_item( const itype_id &item_name, int cost, int count )
{
    function = [item_name, cost, count]( const dialogue & d ) {
        npc &p = *d.beta;
        player &u = *d.alpha;
        if( item::count_by_charges( item_name ) && u.has_charges( item_name, count ) ) {
            for( detached_ptr<item> &it : u.use_charges( item_name, count ) ) {
                p.i_add( std::move( it ) );
            }
        } else if( u.has_amount( item_name, count ) ) {
            for( detached_ptr<item> &it : u.use_amount( item_name, count ) ) {
                p.i_add( std::move( it ) );
            }
        } else {
            //~ %1$s is a translated item name
            popup( _( "You don't have a %1$s!" ), item::nname( item_name ) );
            return;
        }
        if( count == 1 ) {
            //~ %1%s is the NPC name, %2$s is an item
            popup( _( "You give %1$s a %2$s." ), p.name, item::nname( item_name ) );
        } else {
            //~ %1%s is the NPC name, %2$d is a number of items, %3$s are items
            popup( _( "You give %1$s %2$d %3$s." ), p.name, count, item::nname( item_name, count ) );
        }
        p.op_of_u.owed += cost;
    };
}

void talk_effect_fun_t::set_consume_item( const JsonObject &jo, const std::string &member,
        int count,
        bool is_npc )
{
    itype_id item_name;
    jo.read( member, item_name, true );
    function = [is_npc, item_name, count]( const dialogue & d ) {
        // this is stupid, but I couldn't get the assignment to work
        const auto consume_item = [&]( player & p, const itype_id & item_name, int count ) {
            if( p.has_charges( item_name, count ) ) {
                p.use_charges( item_name, count );
            } else if( p.has_amount( item_name, count ) ) {
                p.use_amount( item_name, count );
            } else {
                item *old_item = item::spawn_temporary( item_name );
                //~ %1%s is the "You" or the NPC name, %2$s are a translated item name
                popup( _( "%1$s doesn't have a %2$s!" ), p.disp_name(), old_item->tname() );
            }
        };
        if( is_npc ) {
            consume_item( *d.beta, item_name, count );
        } else {
            consume_item( *d.alpha, item_name, count );
        }
    };
}

void talk_effect_fun_t::set_remove_item_with( const JsonObject &jo, const std::string &member,
        bool is_npc )
{
    const std::string &item_name = jo.get_string( member );
    function = [is_npc, item_name]( const dialogue & d ) {
        player *actor = d.alpha;
        if( is_npc ) {
            actor = dynamic_cast<player *>( d.beta );
        }
        itype_id item_id = itype_id( item_name );
        actor->remove_items_with( [item_id]( detached_ptr<item> &&it ) {
            if( it->typeId() == item_id ) {
                detached_ptr<item> del = std::move( it ); //This acts as a delete
            }
            return VisitResponse::SKIP;
        } );
    };
}

void talk_effect_fun_t::set_u_spend_ecash( int amount )
{
    function = [amount]( const dialogue & d ) {
        d.alpha->cash -= amount;
    };
}

void talk_effect_fun_t::set_npc_change_faction( const std::string &faction_name )
{
    function = [faction_name]( const dialogue & d ) {
        npc &p = *d.beta;
        p.set_fac( faction_id( faction_name ) );
    };
}

void talk_effect_fun_t::set_npc_change_class( const std::string &class_name )
{
    function = [class_name]( const dialogue & d ) {
        npc &p = *d.beta;
        p.myclass = npc_class_id( class_name );
    };
}

void talk_effect_fun_t::set_change_faction_rep( int rep_change )
{
    function = [rep_change]( const dialogue & d ) {
        npc &p = *d.beta;
        if( p.get_faction()->id() != faction_id( "no_faction" ) ) {
            p.get_faction()->set_likes_u( p.get_faction()->likes_u() + rep_change );
            p.get_faction()->set_respects_u( p.get_faction()->respects_u() + rep_change );
        }
    };
}

void talk_effect_fun_t::set_add_debt( const std::vector<trial_mod> &debt_modifiers )
{
    function = [debt_modifiers]( const dialogue & d ) {
        int debt = 0;
        for( const trial_mod &this_mod : debt_modifiers ) {
            if( this_mod.first == "TOTAL" ) {
                debt *= this_mod.second;
            } else {
                debt += parse_mod( d, this_mod.first, this_mod.second );
            }
        }
        d.beta->op_of_u += npc_opinion( 0, 0, 0, 0, debt );
    };
}

void talk_effect_fun_t::set_toggle_npc_rule( const std::string &rule )
{
    function = [rule]( const dialogue & d ) {
        auto toggle = ally_rule_strs.find( rule );
        if( toggle == ally_rule_strs.end() ) {
            return;
        }
        d.beta->rules.toggle_flag( toggle->second.rule );
        d.beta->wield_better_weapon();
    };
}

void talk_effect_fun_t::set_set_npc_rule( const std::string &rule )
{
    function = [rule]( const dialogue & d ) {
        auto flag = ally_rule_strs.find( rule );
        if( flag == ally_rule_strs.end() ) {
            return;
        }
        d.beta->rules.set_flag( flag->second.rule );
        d.beta->wield_better_weapon();
    };
}

void talk_effect_fun_t::set_clear_npc_rule( const std::string &rule )
{
    function = [rule]( const dialogue & d ) {
        auto flag = ally_rule_strs.find( rule );
        if( flag == ally_rule_strs.end() ) {
            return;
        }
        d.beta->rules.clear_flag( flag->second.rule );
        d.beta->wield_better_weapon();
    };
}

void talk_effect_fun_t::set_npc_engagement_rule( const std::string &setting )
{
    function = [setting]( const dialogue & d ) {
        auto rule = combat_engagement_strs.find( setting );
        if( rule != combat_engagement_strs.end() ) {
            d.beta->rules.engagement = rule->second;
            d.beta->invalidate_range_cache();
        }
    };
}

void talk_effect_fun_t::set_npc_aim_rule( const std::string &setting )
{
    function = [setting]( const dialogue & d ) {
        auto rule = aim_rule_strs.find( setting );
        if( rule != aim_rule_strs.end() ) {
            d.beta->rules.aim = rule->second;
            d.beta->invalidate_range_cache();
        }
    };
}

void talk_effect_fun_t::set_npc_cbm_reserve_rule( const std::string &setting )
{
    function = [setting]( const dialogue & d ) {
        auto rule = cbm_reserve_strs.find( setting );
        if( rule != cbm_reserve_strs.end() ) {
            d.beta->rules.cbm_reserve = rule->second;
        }
    };
}

void talk_effect_fun_t::set_npc_cbm_recharge_rule( const std::string &setting )
{
    function = [setting]( const dialogue & d ) {
        auto rule = cbm_recharge_strs.find( setting );
        if( rule != cbm_recharge_strs.end() ) {
            d.beta->rules.cbm_recharge = rule->second;
        }
    };
}

void talk_effect_fun_t::set_mapgen_update( const JsonObject &jo, const std::string &member )
{
    mission_target_params target_params = mission_util::parse_mission_om_target( jo );
    std::vector<std::string> update_ids;

    if( jo.has_string( member ) ) {
        update_ids.emplace_back( jo.get_string( member ) );
    } else if( jo.has_array( member ) ) {
        for( const std::string line : jo.get_array( member ) ) {
            update_ids.emplace_back( line );
        }
    }

    function = [target_params, update_ids]( const dialogue & d ) {
        mission_target_params update_params = target_params;
        update_params.guy = d.beta;
        const tripoint_abs_omt omt_pos = mission_util::get_om_terrain_pos( update_params );
        for( const std::string &mapgen_update_id : update_ids ) {
            run_mapgen_update_func( mapgen_update_id, omt_pos, d.beta->chatbin.mission_selected );
        }
    };
}

void talk_effect_fun_t::set_bulk_trade_accept( bool is_trade, bool is_npc )
{
    function = [is_trade, is_npc]( const dialogue & d ) {
        player *seller = d.alpha;
        player *buyer = dynamic_cast<player *>( d.beta );
        if( is_npc ) {
            seller = dynamic_cast<player *>( d.beta );
            buyer = d.alpha;
        }
        int seller_has = seller->charges_of( d.cur_item );
        //TODO!: check this, I don't think we should be spawning here, just moving
        detached_ptr<item> tmp = item::spawn( d.cur_item );
        tmp->charges = seller_has;
        if( is_trade ) {
            int price = tmp->price( true ) * ( is_npc ? -1 : 1 ) + d.beta->op_of_u.owed;
            if( d.beta->get_faction() && !d.beta->get_faction()->currency().is_empty() ) {
                const itype_id &pay_in = d.beta->get_faction()->currency();
                item *pay = item::spawn_temporary( pay_in );
                if( d.beta->value( *pay ) > 0 ) {
                    int required = price / d.beta->value( *pay );
                    int buyer_has = required;
                    if( is_npc ) {
                        buyer_has = std::min( buyer_has, buyer->charges_of( pay_in ) );
                        buyer->use_charges( pay_in, buyer_has );
                    } else {
                        if( buyer_has == 1 ) {
                            //~ %1%s is the NPC name, %2$s is an item
                            popup( _( "%1$s gives you a %2$s." ), d.beta->disp_name(),
                                   pay->tname() );
                        } else if( buyer_has > 1 ) {
                            //~ %1%s is the NPC name, %2$d is a number of items, %3$s are items
                            popup( _( "%1$s gives you %2$d %3$s." ), d.beta->disp_name(), buyer_has,
                                   pay->tname() );
                        }
                    }
                    for( int i = 0; i < buyer_has; i++ ) {
                        seller->i_add( item::spawn( *pay ) );
                        price -= d.beta->value( *pay );
                    }
                }
                d.beta->op_of_u.owed = price;
            }
        }
        seller->use_charges( d.cur_item, seller_has );
        buyer->i_add( std::move( tmp ) );
    };
}

void talk_effect_fun_t::set_npc_gets_item( bool to_use )
{
    function = [to_use]( const dialogue & d ) {
        d.reason = give_item_to( *( d.beta ), to_use );
    };
}

void talk_effect_fun_t::set_add_mission( const std::string &mission_id )
{
    function = [mission_id]( const dialogue & d ) {
        npc &p = *d.beta;
        mission *miss = mission::reserve_new( mission_type_id( mission_id ), p.getID() );
        miss->assign( get_avatar() );
        p.chatbin.missions_assigned.push_back( miss );
    };
}

void talk_effect_fun_t::set_u_buy_monster( const std::string &monster_type_id, int cost, int count,
        bool pacified, const translation &name )
{
    function = [monster_type_id, cost, count, pacified, name]( const dialogue & d ) {
        npc &p = *d.beta;
        player &u = *d.alpha;
        if( !npc_trading::pay_npc( p, cost ) ) {
            popup( _( "You can't afford it!" ) );
            return;
        }

        const mtype_id mtype( monster_type_id );

        for( int i = 0; i < count; i++ ) {
            monster *const mon_ptr = g->place_critter_around( mtype, u.bub_pos(), 3 );
            if( !mon_ptr ) {
                add_msg( m_debug, "Cannot place u_buy_monster, no valid placement locations." );
                break;
            }
            monster &tmp = *mon_ptr;
            // Our monster is always a pet.
            tmp.friendly = -1;
            tmp.add_effect( effect_pet, 1_turns, bodypart_str_id::NULL_ID() );

            if( pacified ) {
                tmp.add_effect( effect_pacified, 1_turns, bodypart_str_id::NULL_ID() );
            }

            if( !name.empty() ) {
                tmp.unique_name = name.translated();
            }

        }

        if( name.empty() ) {
            popup( _( "%1$s gives you %2$d %3$s." ), p.name, count, mtype.obj().nname( count ) );
        } else {
            popup( _( "%1$s gives you %2$s." ), p.name, name );
        }
    };
}

void talk_effect_fun_t::set_u_learn_recipe( const std::string &learned_recipe_id )
{
    function = [learned_recipe_id]( const dialogue & d ) {
        const recipe &r = recipe_id( learned_recipe_id ).obj();
        d.alpha->learn_recipe( &r );
        popup( _( "You learn how to craft %s." ), r.result_name() );
    };
}

void talk_effect_fun_t::set_npc_first_topic( const std::string &chat_topic )
{
    function = [chat_topic]( const dialogue & d ) {
        d.beta->chatbin.first_topic = chat_topic;
    };
}

void talk_effect_t::set_effect_consequence( const talk_effect_fun_t &fun, dialogue_consequence con )
{
    effects.push_back( fun );
    guaranteed_consequence = std::max( guaranteed_consequence, con );
}

void talk_effect_t::set_effect_consequence( const std::function<void( npc &p )> &ptr,
        dialogue_consequence con )
{
    talk_effect_fun_t npctalk_setter( ptr );
    set_effect_consequence( npctalk_setter, con );
}

void talk_effect_t::set_effect( const talk_effect_fun_t &fun )
{
    effects.push_back( fun );
    guaranteed_consequence = std::max( guaranteed_consequence, dialogue_consequence::none );
}

void talk_effect_t::set_effect( talkfunction_ptr ptr )
{
    talk_effect_fun_t npctalk_setter( ptr );
    dialogue_consequence response;
    if( ptr == &talk_function::hostile ) {
        response = dialogue_consequence::hostile;
    } else if( ptr == &talk_function::player_weapon_drop ||
               ptr == &talk_function::player_weapon_away ||
               ptr == &talk_function::start_mugging ) {
        response = dialogue_consequence::helpless;
    } else {
        response = dialogue_consequence::none;
    }
    set_effect_consequence( npctalk_setter, response );
}

talk_topic talk_effect_t::apply( dialogue &d ) const
{
    // Need to get a reference to the mission before effects are applied, because effects can remove the mission
    mission *miss = d.beta->chatbin.mission_selected;

    for( const talk_effect_fun_t &effect : effects ) {
        effect( d );
    }
    d.beta->op_of_u += opinion;
    if( miss && ( mission_opinion.trust || mission_opinion.fear ||
                  mission_opinion.value || mission_opinion.anger ) ) {
        int m_value = npc_trading::cash_to_favor( *d.beta, miss->get_value() );
        npc_opinion mod = npc_opinion( mission_opinion.trust ?
                                       m_value / mission_opinion.trust : 0,
                                       mission_opinion.fear ?
                                       m_value / mission_opinion.fear : 0,
                                       mission_opinion.value ?
                                       m_value / mission_opinion.value : 0,
                                       mission_opinion.anger ?
                                       m_value / mission_opinion.anger : 0, 0 );
        d.beta->op_of_u += mod;
    }
    if( d.beta->turned_hostile() ) {
        d.beta->make_angry();
        return talk_topic( "TALK_DONE" );
    }

    // TODO: this is a hack, it should be in clear_mission or so, but those functions have
    // no access to the dialogue object.
    auto &ma = d.missions_assigned;
    ma.clear();
    // Update the missions we can talk about (must only be current, non-complete ones)
    for( auto &mission : d.beta->chatbin.missions_assigned ) {
        if( mission->get_assigned_player_id() == d.alpha->getID() ) {
            ma.push_back( mission );
        }
    }

    return next_topic;
}

void talk_effect_t::parse_sub_effect( const JsonObject &jo )
{
    talk_effect_fun_t subeffect_fun;
    const bool is_npc = true;
    if( jo.has_string( "u_add_effect" ) ) {
        subeffect_fun.set_add_effect( jo, "u_add_effect" );
    } else if( jo.has_string( "npc_add_effect" ) ) {
        subeffect_fun.set_add_effect( jo, "npc_add_effect", is_npc );
    } else if( jo.has_string( "u_lose_effect" ) ) {
        subeffect_fun.set_remove_effect( jo, "u_lose_effect" );
    } else if( jo.has_string( "npc_lose_effect" ) ) {
        subeffect_fun.set_remove_effect( jo, "npc_lose_effect", is_npc );
    } else if( jo.has_string( "u_add_var" ) ) {
        subeffect_fun.set_add_var( jo, "u_add_var" );
    } else if( jo.has_string( "npc_add_var" ) ) {
        subeffect_fun.set_add_var( jo, "npc_add_var", is_npc );
    } else if( jo.has_string( "u_lose_var" ) ) {
        subeffect_fun.set_remove_var( jo, "u_lose_var" );
    } else if( jo.has_string( "npc_lose_var" ) ) {
        subeffect_fun.set_remove_var( jo, "npc_lose_var", is_npc );
    } else if( jo.has_string( "u_adjust_var" ) ) {
        subeffect_fun.set_adjust_var( jo, "u_adjust_var" );
    } else if( jo.has_string( "npc_adjust_var" ) ) {
        subeffect_fun.set_adjust_var( jo, "npc_adjust_var", is_npc );
    } else if( jo.has_string( "u_add_trait" ) ) {
        subeffect_fun.set_add_trait( jo, "u_add_trait" );
    } else if( jo.has_string( "npc_add_trait" ) ) {
        subeffect_fun.set_add_trait( jo, "npc_add_trait", is_npc );
    } else if( jo.has_string( "u_lose_trait" ) ) {
        subeffect_fun.set_remove_trait( jo, "u_lose_trait" );
    } else if( jo.has_string( "npc_lose_trait" ) ) {
        subeffect_fun.set_remove_trait( jo, "npc_lose_trait", is_npc );
    } else if( jo.has_int( "u_spend_ecash" ) ) {
        int cash_change = jo.get_int( "u_spend_ecash" );
        subeffect_fun.set_u_spend_ecash( cash_change );
    } else if( jo.has_string( "u_sell_item" ) || jo.has_string( "u_buy_item" ) ||
               jo.has_string( "u_consume_item" ) || jo.has_string( "npc_consume_item" ) ||
               jo.has_string( "u_remove_item_with" ) || jo.has_string( "npc_remove_item_with" ) ) {
        int cost = 0;
        if( jo.has_int( "cost" ) ) {
            cost = jo.get_int( "cost" );
        }
        int count = 1;
        if( jo.has_int( "count" ) ) {
            count = jo.get_int( "count" );
        }
        std::string container_name;
        if( jo.has_string( "container" ) ) {
            container_name = jo.get_string( "container" );
        }
        if( jo.has_string( "u_sell_item" ) ) {
            itype_id item_name;
            jo.read( "u_sell_item", item_name, true );
            subeffect_fun.set_u_sell_item( item_name, cost, count );
        } else if( jo.has_string( "u_buy_item" ) ) {
            itype_id item_name;
            jo.read( "u_buy_item", item_name, true );
            subeffect_fun.set_u_buy_item( item_name, cost, count, container_name );
        } else if( jo.has_string( "u_consume_item" ) ) {
            subeffect_fun.set_consume_item( jo, "u_consume_item", count );
        } else if( jo.has_string( "npc_consume_item" ) ) {
            subeffect_fun.set_consume_item( jo, "npc_consume_item", count, is_npc );
        } else if( jo.has_string( "u_remove_item_with" ) ) {
            subeffect_fun.set_remove_item_with( jo, "u_remove_item_with" );
        } else if( jo.has_string( "npc_remove_item_with" ) ) {
            subeffect_fun.set_remove_item_with( jo, "npc_remove_item_with", is_npc );
        }
    } else if( jo.has_string( "npc_change_class" ) ) {
        std::string class_name = jo.get_string( "npc_change_class" );
        subeffect_fun.set_npc_change_class( class_name );
    } else if( jo.has_string( "add_mission" ) ) {
        std::string mission_id = jo.get_string( "add_mission" );
        subeffect_fun.set_add_mission( mission_id );
    } else if( jo.has_string( "npc_change_faction" ) ) {
        std::string faction_name = jo.get_string( "npc_change_faction" );
        subeffect_fun.set_npc_change_faction( faction_name );
    } else if( jo.has_int( "u_faction_rep" ) ) {
        int faction_rep = jo.get_int( "u_faction_rep" );
        subeffect_fun.set_change_faction_rep( faction_rep );
    } else if( jo.has_array( "add_debt" ) ) {
        std::vector<trial_mod> debt_modifiers;
        for( JsonArray jmod : jo.get_array( "add_debt" ) ) {
            trial_mod this_modifier;
            this_modifier.first = jmod.next_string();
            this_modifier.second = jmod.next_int();
            debt_modifiers.push_back( this_modifier );
        }
        subeffect_fun.set_add_debt( debt_modifiers );
    } else if( jo.has_string( "toggle_npc_rule" ) ) {
        const std::string rule = jo.get_string( "toggle_npc_rule" );
        subeffect_fun.set_toggle_npc_rule( rule );
    } else if( jo.has_string( "set_npc_rule" ) ) {
        const std::string rule = jo.get_string( "set_npc_rule" );
        subeffect_fun.set_set_npc_rule( rule );
    } else if( jo.has_string( "clear_npc_rule" ) ) {
        const std::string rule = jo.get_string( "clear_npc_rule" );
        subeffect_fun.set_clear_npc_rule( rule );
    } else if( jo.has_string( "set_npc_engagement_rule" ) ) {
        const std::string setting = jo.get_string( "set_npc_engagement_rule" );
        subeffect_fun.set_npc_engagement_rule( setting );
    } else if( jo.has_string( "set_npc_aim_rule" ) ) {
        const std::string setting = jo.get_string( "set_npc_aim_rule" );
        subeffect_fun.set_npc_aim_rule( setting );
    } else if( jo.has_string( "set_npc_cbm_reserve_rule" ) ) {
        const std::string setting = jo.get_string( "set_npc_cbm_reserve_rule" );
        subeffect_fun.set_npc_cbm_reserve_rule( setting );
    } else if( jo.has_string( "set_npc_cbm_recharge_rule" ) ) {
        const std::string setting = jo.get_string( "set_npc_cbm_recharge_rule" );
        subeffect_fun.set_npc_cbm_recharge_rule( setting );
    } else if( jo.has_member( "mapgen_update" ) ) {
        subeffect_fun.set_mapgen_update( jo, "mapgen_update" );
    } else if( jo.has_string( "u_buy_monster" ) ) {
        const std::string &monster_type_id = jo.get_string( "u_buy_monster" );
        const int cost = jo.get_int( "cost", 0 );
        const int count = jo.get_int( "count", 1 );
        const bool pacified = jo.get_bool( "pacified", false );
        translation name;
        jo.read( "name", name );
        subeffect_fun.set_u_buy_monster( monster_type_id, cost, count, pacified, name );
    } else if( jo.has_string( "u_learn_recipe" ) ) {
        const std::string recipe_id = jo.get_string( "u_learn_recipe" );
        subeffect_fun.set_u_learn_recipe( recipe_id );
    } else if( jo.has_string( "npc_first_topic" ) ) {
        const std::string chat_topic = jo.get_string( "npc_first_topic" );
        subeffect_fun.set_npc_first_topic( chat_topic );
    } else if( jo.has_member( "assign_mission" ) ) {
        subeffect_fun.set_assign_mission( jo, "assign_mission" );
    } else if( jo.has_string( "finish_mission" ) ) {
        subeffect_fun.set_finish_mission( jo, "finish_mission" );
    } else {
        jo.throw_error( "invalid sub effect syntax: " + jo.str() );
    }
    set_effect( subeffect_fun );
}

void talk_effect_t::parse_string_effect( const std::string &effect_id, const JsonObject &jo )
{
    static const std::unordered_map<std::string, void( * )( npc & )> static_functions_map = {
        {
#define WRAP( function ) { #function, &talk_function::function }
            WRAP( assign_mission ),
            WRAP( mission_success ),
            WRAP( mission_failure ),
            WRAP( clear_mission ),
            WRAP( mission_reward ),
            WRAP( start_trade ),
            WRAP( sort_loot ),
            WRAP( find_mount ),
            WRAP( dismount ),
            WRAP( do_chop_plank ),
            WRAP( do_vehicle_deconstruct ),
            WRAP( do_vehicle_repair ),
            WRAP( do_chop_trees ),
            WRAP( do_fishing ),
            WRAP( do_construction ),
            WRAP( do_mining ),
            WRAP( do_read ),
            WRAP( do_butcher ),
            WRAP( do_farming ),
            WRAP( do_craft ),
            WRAP( assign_guard ),
            WRAP( stop_guard ),
            WRAP( buy_cow ),
            WRAP( buy_chicken ),
            WRAP( buy_horse ),
            WRAP( wake_up ),
            WRAP( control_npc ),
            WRAP( reveal_stats ),
            WRAP( end_conversation ),
            WRAP( insult_combat ),
            WRAP( give_equipment ),
            WRAP( give_aid ),
            WRAP( give_all_aid ),
            WRAP( barber_beard ),
            WRAP( barber_hair ),
            WRAP( buy_haircut ),
            WRAP( buy_shave ),
            WRAP( morale_chat ),
            WRAP( morale_chat_activity ),
            WRAP( buy_10_logs ),
            WRAP( buy_100_logs ),
            WRAP( bionic_install ),
            WRAP( bionic_remove ),
            WRAP( follow ),
            WRAP( follow_only ),
            WRAP( deny_follow ),
            WRAP( deny_lead ),
            WRAP( deny_equipment ),
            WRAP( deny_train ),
            WRAP( deny_personal_info ),
            WRAP( hostile ),
            WRAP( flee ),
            WRAP( leave ),
            WRAP( stop_following ),
            WRAP( revert_activity ),
            WRAP( goto_location ),
            WRAP( stranger_neutral ),
            WRAP( start_mugging ),
            WRAP( player_leaving ),
            WRAP( drop_weapon ),
            WRAP( drop_stolen_item ),
            WRAP( remove_stolen_status ),
            WRAP( player_weapon_away ),
            WRAP( player_weapon_drop ),
            WRAP( lead_to_safety ),
            WRAP( start_training ),
            WRAP( copy_npc_rules ),
            WRAP( set_npc_pickup ),
            WRAP( npc_die ),
            WRAP( npc_thankful ),
            WRAP( clear_overrides ),
            WRAP( nothing )
#undef WRAP
        }
    };
    const auto iter = static_functions_map.find( effect_id );
    if( iter != static_functions_map.end() ) {
        set_effect( iter->second );
        return;
    }

    talk_effect_fun_t subeffect_fun;
    if( effect_id == "u_bulk_trade_accept" || effect_id == "npc_bulk_trade_accept" ||
        effect_id == "u_bulk_donate" || effect_id == "npc_bulk_donate" ) {
        bool is_npc = effect_id == "npc_bulk_trade_accept" || effect_id == "npc_bulk_donate";
        bool is_trade = effect_id == "u_bulk_trade_accept" || effect_id == "npc_bulk_trade_accept";
        subeffect_fun.set_bulk_trade_accept( is_trade, is_npc );
        set_effect( subeffect_fun );
        return;
    }

    if( effect_id == "npc_gets_item" || effect_id == "npc_gets_item_to_use" ) {
        bool to_use = effect_id == "npc_gets_item_to_use";
        subeffect_fun.set_npc_gets_item( to_use );
        set_effect( subeffect_fun );
        return;
    }

    jo.throw_error( "unknown effect string", effect_id );
}

void talk_effect_t::load_effect( const JsonObject &jo )
{
    if( jo.has_member( "opinion" ) ) {
        JsonIn *ji = jo.get_raw( "opinion" );
        // Same format as when saving a game (-:
        opinion.deserialize( *ji );
    }
    if( jo.has_member( "mission_opinion" ) ) {
        JsonIn *ji = jo.get_raw( "mission_opinion" );
        // Same format as when saving a game (-:
        mission_opinion.deserialize( *ji );
    }
    static const std::string member_name( "effect" );
    if( !jo.has_member( member_name ) ) {
        return;
    } else if( jo.has_string( member_name ) ) {
        const std::string type = jo.get_string( member_name );
        parse_string_effect( type, jo );
    } else if( jo.has_object( member_name ) ) {
        JsonObject sub_effect = jo.get_object( member_name );
        parse_sub_effect( sub_effect );
    } else if( jo.has_array( member_name ) ) {
        for( const JsonValue entry : jo.get_array( member_name ) ) {
            if( entry.test_string() ) {
                const std::string type = entry.get_string();
                parse_string_effect( type, jo );
            } else if( entry.test_object() ) {
                JsonObject sub_effect = entry.get_object();
                parse_sub_effect( sub_effect );
            } else {
                jo.throw_error( "invalid effect array syntax", member_name );
            }
        }
    } else {
        jo.throw_error( "invalid effect syntax", member_name );
    }
}

