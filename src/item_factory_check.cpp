#include "item_factory.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <iuse.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

#include "addiction.h"
#include "catalua_icallback_actor.h"
#include "ammo.h"
#include "artifact.h"
#include "assign.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "color.h"
#include "coordinates.h"
#include "damage.h"
#include "debug.h"
#include "debug_menu.h"
#include "enum_conversions.h"
#include "enums.h"
#include "explosion.h"
#include "flag.h"
#include "flat_set.h"
#include "game_constants.h"
#include "generic_factory.h"
#include "init.h"
#include "input.h"
#include "item.h"
#include "item_contents.h"
#include "item_group.h"
#include "iuse_actor.h"
#include "json.h"
#include "light_emission.h"


#include "material.h"
#include "options.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "relic.h"
#include "requirements.h"
#include "skill.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_utils.h"
#include "text_snippets.h"
#include "translations.h"
#include "ui.h"
#include "units.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vitamin.h"
#include "wheel_dimensions.h"





void Item_factory::check_definitions() const
{
for( const auto &elem : m_templates ) {
    std::string msg;
    const itype *type = &elem.second;

    if( !type->category_force.is_valid() ) {
            msg += "undefined category " + type->category_force.str() + "\n";
        }

        if( type->armor ) {
            cata::flat_set<bodypart_str_id> observed_bps;
            for( const armor_portion_data &portion : type->armor->data ) {
                if( portion.covers.none() ) {
                    continue;
                }
                for( const body_part &bp : all_body_parts ) {
                    if( portion.covers.test( convert_bp( bp ) ) ) {
                        if( observed_bps.count( convert_bp( bp ) ) ) {
                            msg += "multiple portions with same body_part defined\n";
                        }
                        observed_bps.insert( convert_bp( bp ) );
                    }
                }
            }
        }

        if( type->weight < 0_gram ) {
            msg += "negative weight\n";
        }
        if( type->volume < 0_ml ) {
            msg += "negative volume\n";
        }
        if( type->count_by_charges() || type->phase == LIQUID ) {
            if( type->stack_size <= 0 ) {
                msg += string_format( "invalid stack_size %d on type using charges\n", type->stack_size );
            }
        }
        if( type->price < 0_cent ) {
            msg += "negative price\n";
        }
        if( type->damage_min() > 0 || type->damage_max() < 0 || type->damage_min() > type->damage_max() ) {
            msg += "invalid damage range\n";
        }
        if( type->description.empty() ) {
            msg += "empty description\n";
        }

        for( const material_id &mat_id : type->materials ) {
            if( mat_id.str() == "null" || !mat_id.is_valid() ) {
                msg += string_format( "invalid material %s\n", mat_id.c_str() );
            }
        }

        if( type->sym.empty() ) {
            msg += "symbol not defined\n";
        } else if( utf8_width( type->sym ) != 1 ) {
            msg += "symbol must be exactly one console cell width\n";
        }

        for( const auto &_a : type->techniques ) {
            if( !_a.is_valid() ) {
                msg += string_format( "unknown technique %s\n", _a.c_str() );
            }
        }
        if( !type->snippet_category.empty() ) {
            if( !SNIPPET.has_category( type->snippet_category ) ) {
                msg += string_format( "item %s: snippet category %s without any snippets\n", type->id.c_str(),
                                      type->snippet_category.c_str() );
            }
        }
        for( auto &q : type->qualities ) {
            if( !q.first.is_valid() ) {
                msg += string_format( "item %s has unknown quality %s\n", type->id.c_str(), q.first.c_str() );
            }
        }
        if( type->default_container && !has_template( *type->default_container ) ) {
            if( !type->default_container->is_null() ) {
                msg += string_format( "invalid container property %s\n", type->default_container->c_str() );
            }
        }

        for( const auto &e : type->emits ) {
            if( !e.is_valid() ) {
                msg += string_format( "item %s has unknown emit source %s\n", type->id.c_str(), e.c_str() );
            }
        }

        for( const auto &f : type->faults ) {
            if( !f.is_valid() ) {
                msg += string_format( "invalid item fault %s\n", f.c_str() );
            }
        }

        for( const weapon_category_id &cat_id : type->weapon_category ) {
            if( !cat_id.is_valid() ) {
                msg += string_format( "invalid weapon category: %s\n", cat_id.c_str() );
            }
        }

        if( type->has_flag( flag_FIRESTARTER ) &&
            !type->can_have_charges() &&
            !type->get_use( "firestarter" ) ) {
            msg += string_format( "has 'FIRESTARTER' flag, but neither can have charges nor defines 'firestarter' use func" );
        }

        if( type->comestible ) {
            if( !type->comestible->tool.is_null() ) {
                auto req_tool = find_template( type->comestible->tool );
                if( !req_tool->tool ) {
                    msg += string_format( "invalid tool property %s\n", type->comestible->tool.c_str() );
                }
            }
        }
        if( type->brewable ) {
            if( type->brewable->time < 1_turns ) {
                msg += "brewable time is less than 1 turn\n";
            }

            if( type->brewable->results.empty() ) {
                msg += "empty product list\n";
            }

            for( auto &b : type->brewable->results ) {
                if( !has_template( b ) ) {
                    msg += string_format( "invalid result id %s\n", b.c_str() );
                }
            }
        }
        if( type->seed ) {
            if( type->seed->grow < 1_turns ) {
                msg += "seed growing time is less than 1 turn\n";
            }
            if( !has_template( type->seed->fruit_id ) ) {
                msg += string_format( "invalid fruit id %s\n", type->seed->fruit_id.c_str() );
            }
            for( auto &b : type->seed->byproducts ) {
                if( !has_template( b ) ) {
                    msg += string_format( "invalid byproduct id %s\n", b.c_str() );
                }
            }
        }
        if( type->book ) {
            if( type->book->skill && !type->book->skill.is_valid() ) {
                msg += "uses invalid book skill.\n";
            }
            if( type->book->martial_art && !type->book->martial_art.is_valid() ) {
                msg += string_format( "trains invalid martial art '%s'.\n", type->book->martial_art.str() );
            }
            if( type->can_use( "MA_MANUAL" ) && !type->book->martial_art ) {
                msg += "has use_action MA_MANUAL but does not specify a martial art\n";
            }
        }
        if( type->can_use( "MA_MANUAL" ) && !type->book ) {
            msg += "has use_action MA_MANUAL but is not a book\n";
        }
        if( type->milling_data ) {
            if( !has_template( type->milling_data->into_ ) ) {
                msg += "type to mill into is invalid: " + type->milling_data->into_.str() + "\n";
            }
            if( !type->milling_data->into_->count_by_charges() ) {
                msg += "type to mill into must be counted by charges: " + type->milling_data->into_.str() + "\n";
            }
        }
        if( type->ammo ) {
            if( !type->ammo->type && type->ammo->type != ammotype( "NULL" ) ) {
                msg += "must define at least one ammo type\n";
            }
            check_ammo_type( msg, type->ammo->type );
            if( type->ammo->casing && ( !has_template( *type->ammo->casing ) ||
                                        type->ammo->casing->is_null() ) ) {
                msg += string_format( "invalid casing property %s\n", type->ammo->casing->c_str() );
            }
            if( !type->ammo->drop.is_null() && !has_template( type->ammo->drop ) ) {
                msg += string_format( "invalid drop item %s\n", type->ammo->drop.c_str() );
            }
            if( type->ammo->range != 0 && type->ammo->shape ) {
                msg += string_format( "shape is set, but range is %d != 0", type->ammo->range );
            }
            if( type->ammo->shot ) {
                if( type->ammo->shot->count <= 0 ) {
                    msg += string_format( "shot.count must be positive, but is %d\n",
                                          type->ammo->shot->count );
                }
                if( type->ammo->shot->half_angle < 0 ) {
                    msg += string_format( "shot.half_angle must be non-negative, but is %.2f\n",
                                          type->ammo->shot->half_angle );
                }
                if( type->ammo->shape && type->ammo->shot->count > 1 ) {
                    msg += "shape and shot.count > 1 cannot be combined\n";
                }
            }
        }
        if( type->battery ) {
            if( type->battery->max_capacity < 0_J ) {
                msg += "battery cannot have negative maximum charge\n";
            }
        }
        if( type->gun ) {
            for( const ammotype &at : type->gun->ammo ) {
                check_ammo_type( msg, at );
            }
            if( type->gun->ammo.empty() ) {
                // if gun doesn't use ammo forbid both integral or detachable magazines
                if( static_cast<bool>( type->gun->clip ) || !type->magazines.empty() ) {
                    msg += "cannot specify clip_size or magazine without ammo type\n";
                }

                if( type->has_flag( flag_RELOAD_AND_SHOOT ) ) {
                    msg += "RELOAD_AND_SHOOT requires an ammo type to be specified\n";
                }

            } else {
                if( type->has_flag( flag_RELOAD_AND_SHOOT ) && !type->magazines.empty() ) {
                    msg += "RELOAD_AND_SHOOT cannot be used with magazines\n";
                }
                for( const ammotype &at : type->gun->ammo ) {
                    if( !type->gun->clip && !type->magazines.empty() && !type->magazine_default.contains( at ) ) {
                        msg += string_format( "specified magazine but none provided for ammo type %s\n", at.str() );
                    }
                }
            }
            if( type->gun->barrel_volume < 0_ml ) {
                msg += "gun barrel volume cannot be negative\n";
            }

            if( !type->gun->skill_used ) {
                msg += "uses no skill\n";
            } else if( !type->gun->skill_used.is_valid() ) {
                msg += string_format( "uses an invalid skill %s\n", type->gun->skill_used.str() );
            }
            for( auto &gm : type->gun->default_mods ) {
                if( !has_template( gm ) ) {
                    msg += "invalid default mod.\n";
                }
            }
            for( auto &gm : type->gun->built_in_mods ) {
                if( !has_template( gm ) ) {
                    msg += "invalid built-in mod.\n";
                }
            }
        }
        if( type->gunmod ) {
            if( type->gunmod->location.str().empty() ) {
                msg += "gunmod does not specify location\n";
            }
            if( ( type->gunmod->sight_dispersion < 0 ) != ( type->gunmod->aim_speed < 0 ) ) {
                msg += "gunmod must have both sight_dispersion and aim_speed set or neither of them set\n";
            }
            for( const itype_id &t : type->gunmod->usable ) {
                if( !t.is_valid() ) {
                    msg += string_format( "gunmod is usable for invalid item %s\n", t.c_str() );
                    continue;
                }

                const itype *target = &*t;
                if( !target->gun->valid_mod_locations.contains( type->gunmod->location ) ) {
                    msg += string_format( "gunmod is usable for gun %s which doesn't have a slot of type %s\n",
                                          t.c_str(), type->gunmod->location.str() );
                }

                if( type->mod != nullptr && !type->mod->ammo_modifier.empty() ) {
                    auto &acceptable_ammo = type->mod->ammo_modifier;
                    for( const auto &pr : type->mod->magazine_adaptor ) {
                        acceptable_ammo.insert( pr.first );
                    }
                    auto &acceptable_magazines = !type->mod->magazine_adaptor.empty()
                                                 ? type->mod->magazine_adaptor
                                                 : target->magazines;
                    for( const ammotype &ammo : acceptable_ammo ) {
                        if( !acceptable_magazines.contains( ammo ) ) {
                            msg += string_format( "gunmod can be applied to %s, which has no magazines for ammo %s\n",
                                                  t.c_str(), ammo.str() );
                        }
                    }
                }
            }
            for( const itype_id &t : type->gunmod->exclusion ) {
                if( !t.is_valid() ) {
                    msg += string_format( "gunmod excludes for invalid item %s\n", t.c_str() );
                }
                if( type->gunmod->usable.contains( t ) ) {
                    msg += string_format( "gunmod includes and excludes same item %s\n", t.c_str() );
                }
            }
            for( const std::unordered_set<weapon_category_id> &wv : type->gunmod->usable_category ) {
                for( const weapon_category_id &wid : wv ) {
                    if( !wid.is_valid() ) {
                        msg += string_format( "gunmod is usable for invalid weapon category %s\n", wid.c_str() );
                    }
                }
            }
            for( const std::unordered_set<weapon_category_id> &wv : type->gunmod->exclusion_category ) {
                for( const weapon_category_id &wid : wv ) {
                    if( !wid.is_valid() ) {
                        msg += string_format( "gunmod excludes for invalid weapon category %s\n", wid.c_str() );
                    }
                }
                for( const std::unordered_set<weapon_category_id> &test_wv : type->gunmod->usable_category ) {
                    if( wv == test_wv ) {
                        std::string group_format = ( "[" ) + enumerate_as_string( wv.begin(),
                        wv.end(), []( const weapon_category_id & wcid ) {
                            return string_format( "%s", wcid.c_str() );
                        }, enumeration_conjunction::none ) + ( "]" );
                        msg += string_format( "gunmod includes and excludes weapon category group %s\n", group_format );
                    }
                }
            }
        }
        if( type->mod ) {
            for( const ammotype &at : type->mod->ammo_modifier ) {
                check_ammo_type( msg, at );
            }

            for( const auto &e : type->mod->acceptable_ammo ) {
                check_ammo_type( msg, e );
            }

            for( const auto &e : type->mod->magazine_adaptor ) {
                check_ammo_type( msg, e.first );
                if( e.second.empty() ) {
                    msg += string_format( "no magazines specified for ammo type %s\n", e.first.str() );
                }
                for( const itype_id &opt : e.second ) {
                    const itype *mag = find_template( opt );
                    if( !mag->magazine || !mag->magazine->type.contains( e.first ) ) {
                        msg += string_format( "invalid magazine %s in magazine adapter\n", opt.str() );
                    }
                }
            }
        }
        if( type->magazine ) {
            for( const ammotype &at : type->magazine->type ) {
                check_ammo_type( msg, at );
            }
            if( type->magazine->type.empty() ) {
                msg += "magazine did not specify ammo type\n";
            }
            if( type->magazine->capacity < 0 ) {
                msg += string_format( "invalid capacity %i\n", type->magazine->capacity );
            }
            if( type->magazine->count < 0 || type->magazine->count > type->magazine->capacity ) {
                msg += string_format( "invalid count %i\n", type->magazine->count );
            }
            const itype *da = find_template( type->magazine->default_ammo );
            if( !( da->ammo && type->magazine->type.contains( da->ammo->type ) ) ) {
                msg += string_format( "invalid default_ammo %s\n", type->magazine->default_ammo.str() );
            }
            if( type->magazine->reliability < 0 || type->magazine->reliability > 100 ) {
                msg += string_format( "invalid reliability %i\n", type->magazine->reliability );
            }
            if( type->magazine->reload_time < 0 ) {
                msg += string_format( "invalid reload_time %i\n", type->magazine->reload_time );
            }
            if( type->magazine->linkage && ( !has_template( *type->magazine->linkage ) ||
                                             type->magazine->linkage->is_null() ) ) {
                msg += string_format( "invalid linkage property %s\n", type->magazine->linkage->c_str() );
            }
        }

        for( const std::pair<const string_id<ammunition_type>, std::set<itype_id>> &ammo_variety :
             type->magazines ) {
            if( ammo_variety.second.empty() ) {
                msg += string_format( "no magazine specified for %s\n", ammo_variety.first.str() );
            }
            for( const itype_id &magazine : ammo_variety.second ) {
                const itype *mag_ptr = find_template( magazine );
                if( mag_ptr == nullptr ) {
                    msg += string_format( "magazine \"%s\" specified for \"%s\" does not exist\n",
                                          magazine.str(), ammo_variety.first.str() );
                } else if( !mag_ptr->magazine ) {
                    msg += string_format(
                               "magazine \"%s\" specified for \"%s\" is not a magazine\n", magazine.str(),
                               ammo_variety.first.str() );
                } else if( !mag_ptr->magazine->type.contains( ammo_variety.first ) ) {
                    msg += string_format( "magazine \"%s\" does not take compatible ammo\n", magazine );
                } else if( mag_ptr->has_flag( flag_SPEEDLOADER ) &&
                           mag_ptr->magazine->capacity > type->gun->clip ) {
                    msg += string_format(
                               "speedloader %s capacity (%d) is bigger than gun capacity (%d).\n",
                               magazine.str(), mag_ptr->magazine->capacity, type->gun->clip );
                }
            }
        }

        if( type->tool ) {
            for( const ammotype &at : type->tool->ammo_id ) {
                check_ammo_type( msg, at );
            }
            if( type->tool->revert_to && ( !has_template( *type->tool->revert_to ) ||
                                           type->tool->revert_to->is_null() ) ) {
                msg += string_format( "invalid revert_to property %s\n", type->tool->revert_to->c_str() );
            }
            if( !type->tool->revert_msg.empty() && !type->tool->revert_to ) {
                msg += "cannot specify revert_msg without revert_to\n";
            }
            if( !type->tool->subtype.is_empty() && !has_template( type->tool->subtype ) ) {
                msg += string_format( "invalid tool subtype %s\n", type->tool->subtype.str() );
            }
        }
        if( type->bionic ) {
            if( !type->bionic->id.is_valid() ) {
                msg += string_format( "there is no bionic with id %s\n", type->bionic->id.c_str() );
            }
        }

        if( type->container ) {
            if( type->container->seals && !type->container->unseals_into.is_null() ) {
                msg += string_format( "resealable container unseals_into %s\n",
                                      type->container->unseals_into.c_str() );
            }
            if( type->container->contains <= 0_ml ) {
                msg += string_format( "\"contains\" (%d) must be >0\n", type->container->contains.value() );
            }
            if( !has_template( type->container->unseals_into ) ) {
                msg += string_format( "unseals_into invalid id %s\n", type->container->unseals_into.c_str() );
            }
        }

        if( type->relic_data ) {
            type->relic_data->check();
        }

        for( const auto &elem : type->use_methods ) {
            const iuse_actor *actor = elem.second.get_actor_ptr();

            assert( actor );
            if( !actor->is_valid() ) {
                msg += string_format( "item action \"%s\" was not described.\n", actor->type.c_str() );
            }

            if( actor->type == "CABLE_ATTACH" && !vpart_id( type->id.str() ).is_valid() ) {
                msg += string_format( "no valid vehicle part for CABLE_ATTACH action\n" );
            }
        }

        if( type->fuel && !type->count_by_charges() ) {
            msg += "fuel value set, but item isn't count_by_charges.\n";
        }

        if( msg.empty() ) {
            continue;
        }
        debugmsg( "warnings for type %s:\n%s", type->id.c_str(), msg );
    }
for( const auto &e : migrations ) {
    if( !m_templates.contains( e.second.replace ) ) {
            debugmsg( "Invalid migration target: %s", e.second.replace.c_str() );
        }
        for( const auto &c : e.second.contents ) {
            if( !m_templates.contains( c ) ) {
                debugmsg( "Invalid migration contents: %s", c.c_str() );
            }
        }
    }
for( const auto &elem : m_template_groups ) {
    elem.second->check_consistency( elem.first.str() );
        inp_mngr.pump_events();
    }
}

