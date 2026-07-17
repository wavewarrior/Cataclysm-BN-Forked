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




#include "vitamin.h"
#include "wheel_dimensions.h"


static const ammo_effect_str_id ammo_effect_COOKOFF( "COOKOFF" );
static const ammo_effect_str_id ammo_effect_EXPLOSIVE( "EXPLOSIVE" );
static const ammo_effect_str_id ammo_effect_EXPLOSIVE_BIG( "EXPLOSIVE_BIG" );
static const ammo_effect_str_id ammo_effect_EXPLOSIVE_HUGE( "EXPLOSIVE_HUGE" );
static const ammo_effect_str_id ammo_effect_EXPLOSIVE_SMALL( "EXPLOSIVE_SMALL" );
static const ammo_effect_str_id ammo_effect_FLASHBANG( "FLASHBANG" );
static const ammo_effect_str_id ammo_effect_FRAG( "FRAG" );
static const ammo_effect_str_id ammo_effect_INCENDIARY( "INCENDIARY" );
static const ammo_effect_str_id ammo_effect_NAPALM( "NAPALM" );
static const ammo_effect_str_id ammo_effect_NAPALM_BIG( "NAPALM_BIG" );
static const ammo_effect_str_id ammo_effect_SMOKE( "SMOKE" );
static const ammo_effect_str_id ammo_effect_SMOKE_BIG( "SMOKE_BIG" );
static const ammo_effect_str_id ammo_effect_TOXICGAS( "TOXICGAS" );
static const gun_mode_id gun_mode_REACH( "REACH" );

// Defined in item_factory.cpp
extern DynamicDataLoader::deferred_json deferred;
extern std::set<itype_id> item_blacklist;
auto calc_category( const itype &obj ) -> item_category_id;
void hflesh_to_flesh( itype &item_template );
auto defmode_name( itype &obj ) -> const char *;

void Item_factory::finalize_pre( itype &obj )
{
    // TODO: separate repairing from reinforcing/enhancement
    if( obj.damage_max() == obj.damage_min() ) {
        obj.item_tags.insert( flag_NO_REPAIR );
    }

    if( obj.has_flag( flag_STAB ) ) {
        std::swap( obj.melee[DT_CUT], obj.melee[DT_STAB] );
    }

    // We want to recalculate DEFAULT attack, because it defaults to 0
    // But if it doesn't exist, we want to keep it that way
    if( obj.attacks.empty() || obj.attacks.contains( "DEFAULT" ) ) {
        attack_statblock att;
        att.to_hit = obj.m_to_hit;
        for( size_t i = 0; i < NUM_DT; i++ ) {
            if( obj.melee[i] > 0 ) {
                att.damage.add_damage( static_cast<damage_type>( i ), obj.melee[i] );
            }
        }

        obj.attacks["DEFAULT"] = att;
    }

    // add usage methods (with default values) based upon qualities
    // if a method was already set the specific values remain unchanged
    for( const auto &q : obj.qualities ) {
        for( const auto &u : q.first.obj().usages ) {
            if( q.second >= u.first ) {
                emplace_usage( obj.use_methods, u.second );
            }
        }
    }

    if( obj.mod ) {
        std::string func = obj.gunmod ? "GUNMOD_ATTACH" : "TOOLMOD_ATTACH";
        emplace_usage( obj.use_methods, func );
    } else if( obj.gun ) {
        const std::string func = "detach_gunmods";
        emplace_usage( obj.use_methods, func );
    }

    if( get_option<bool>( "NO_FAULTS" ) ) {
        obj.faults.clear();
    }

    // If no category was forced via JSON automatically calculate one now
    if( !obj.category_force.is_valid() || obj.category_force.is_empty() ) {
        obj.category_force = calc_category( obj );
    }

    // use pre-cataclysm price as default if post-cataclysm price unspecified
    if( obj.price_post < 0_cent ) {
        obj.price_post = obj.price;
    }
    // use base volume if integral volume unspecified
    if( obj.integral_volume < 0_ml ) {
        obj.integral_volume = obj.volume;
    }
    // use base weight if integral weight unspecified
    if( obj.integral_weight < 0_gram ) {
        obj.integral_weight = obj.weight;
    }
    // for ammo and comestibles stack size defaults to count of initial charges
    // Set max stack size to 200 to prevent integer overflow
    if( obj.count_by_charges() ) {
        if( obj.stack_size == 0 ) {
            obj.stack_size = obj.charges_default();
        } else if( obj.stack_size > 200 ) {
            debugmsg( obj.id.str() + " stack size is too large, reducing to 200" );
            obj.stack_size = 200;
        }
    }

    // Items always should have some volume.
    // TODO: handle possible exception software?
    // TODO: make items with 0 volume an error during loading?
    if( obj.volume <= 0_ml ) {
        obj.volume = units::from_milliliter( 1 );
    }

    // set light_emission based on LIGHT_[X] flag
    for( const auto &f : obj.item_tags ) {
        int ll;
        if( sscanf( f.c_str(), "LIGHT_%i", &ll ) == 1 && ll > 0 ) {
            obj.light_emission = ll;
        }
    }
    // remove LIGHT_[X] flags
    erase_if( obj.item_tags, []( const flag_id & f ) {
        return f.str().starts_with( "LIGHT_" );
    } );

    // Set max volume for containers to prevent integer overflow
    if( obj.container && obj.container->contains > 10000_liter ) {
        debugmsg( obj.id.str() + " storage volume is too large, reducing to 10000 liters" );
        obj.container->contains = 10000_liter;
    }

    if( obj.ammo ) {
        // for ammo not specifying loudness (or an explicit zero) derive value from other properties
        if( obj.ammo->loudness < 0 ) {
            obj.ammo->loudness = obj.ammo->range * 2;
            for( const damage_unit &du : obj.ammo->damage ) {
                obj.ammo->loudness += ( du.amount * 2 ) + du.res_pen;
            }
        }

        const auto &mats = obj.materials;
        if( std::find( mats.begin(), mats.end(), material_id( "hydrocarbons" ) ) == mats.end() &&
            std::find( mats.begin(), mats.end(), material_id( "oil" ) ) == mats.end() ) {
            const auto &ammo_effects = obj.ammo->ammo_effects;
            obj.ammo->cookoff = ammo_effects.contains( ammo_effect_INCENDIARY ) ||
                                ammo_effects.contains( ammo_effect_COOKOFF );
            static const std::set<ammo_effect_str_id> special_cookoff_tags = {{
                    ammo_effect_EXPLOSIVE, ammo_effect_EXPLOSIVE_BIG, ammo_effect_EXPLOSIVE_HUGE, ammo_effect_EXPLOSIVE_SMALL, ammo_effect_FLASHBANG, ammo_effect_FRAG, ammo_effect_NAPALM, ammo_effect_NAPALM_BIG, ammo_effect_SMOKE, ammo_effect_SMOKE_BIG, ammo_effect_TOXICGAS,
                }
            };
            obj.ammo->special_cookoff = std::any_of( ammo_effects.begin(), ammo_effects.end(),
            []( const ammo_effect_str_id & ae_id ) {
                return special_cookoff_tags.contains( ae_id );
            } );
        } else {
            obj.ammo->cookoff = false;
            obj.ammo->special_cookoff = false;
        }

        for( auto iter = obj.ammo->ammo_effects.begin(); iter != obj.ammo->ammo_effects.end(); ) {
            const ammo_effect_str_id &ae_id = *iter;
            if( ae_id.is_valid() ) {
                iter++;
            } else {
                int dummy = 0;
                if( sscanf( ae_id.c_str(), "RECOVER_%i", &dummy ) == 1 ) {
                    obj.ammo->dont_recover_one_in *= dummy;
                } else {
                    debugmsg( "%s has unknown ammo_effect %s, removing", obj.id, ae_id );
                }

                iter = obj.ammo->ammo_effects.erase( iter );
            }
        }
    }
    const auto check_ammo_effects = []( const itype_id & id, std::set<ammo_effect_str_id> &effects ) {
        for( auto iter = effects.begin(); iter != effects.end(); ) {
            const ammo_effect_str_id &ae_id = *iter;
            if( ae_id.is_valid() ) {
                iter++;
            } else {
                debugmsg( "%s has unknown ammo_effect %s, removing", id, ae_id );
                iter = effects.erase( iter );
            }
        }
    };
    if( obj.gun ) {
        check_ammo_effects( obj.id, obj.gun->ammo_effects );
    }
    if( obj.gunmod ) {
        check_ammo_effects( obj.id, obj.gunmod->ammo_effects );
    }

    // Helper for ammo migration in following sections
    auto migrate_ammo_set = [&]( std::set<ammotype> &ammoset ) {
        for( auto ammo_type_it = ammoset.begin(); ammo_type_it != ammoset.end(); ) {
            auto maybe_migrated = migrated_ammo.find( ammo_type_it->obj().default_ammotype() );
            if( maybe_migrated != migrated_ammo.end() ) {
                ammo_type_it = ammoset.erase( ammo_type_it );
                ammoset.insert( ammoset.begin(), maybe_migrated->second );
            } else {
                ++ammo_type_it;
            }
        }
    };

    if( obj.magazine ) {
        // ensure default_ammo is set
        if( obj.magazine->default_ammo.is_null() ) {
            obj.magazine->default_ammo = ammotype( *obj.magazine->type.begin() )->default_ammotype();
        }

        // If the magazine has ammo types for which the default ammo has been migrated, we need to
        // replace those ammo types with that of the migrated ammo
        migrate_ammo_set( obj.magazine->type );

        // ensure default_ammo is migrated if need be
        auto maybe_migrated = migrated_ammo.find( obj.magazine->default_ammo );
        if( maybe_migrated != migrated_ammo.end() ) {
            obj.magazine->default_ammo = maybe_migrated->second.obj().default_ammotype();
        }
    }

    // Migrate compataible magazines
    for( auto &kv : obj.magazines ) {
        for( auto mag_it = kv.second.begin(); mag_it != kv.second.end(); ) {
            auto maybe_migrated = migrated_magazines.find( *mag_it );
            if( maybe_migrated != migrated_magazines.end() ) {
                mag_it = kv.second.erase( mag_it );
                kv.second.insert( kv.second.begin(), maybe_migrated->second );
            } else {
                ++mag_it;
            }
        }
    }

    // Migrate default magazines
    for( auto &kv : obj.magazine_default ) {
        auto maybe_migrated = migrated_magazines.find( kv.second );
        if( maybe_migrated != migrated_magazines.end() ) {
            kv.second = maybe_migrated->second;
        }
    }

    if( obj.mod ) {
        // Migrate acceptable ammo and ammo modifiers
        migrate_ammo_set( obj.mod->acceptable_ammo );
        migrate_ammo_set( obj.mod->ammo_modifier );

        for( auto kv = obj.mod->magazine_adaptor.begin(); kv != obj.mod->magazine_adaptor.end(); ) {
            auto maybe_migrated = migrated_ammo.find( kv->first.obj().default_ammotype() );
            if( maybe_migrated != migrated_ammo.end() ) {
                for( const itype_id &compatible_mag : kv->second ) {
                    obj.mod->magazine_adaptor[maybe_migrated->second].insert( compatible_mag );
                }
                kv = obj.mod->magazine_adaptor.erase( kv );
            } else {
                ++kv;
            }
        }
    }

    if( obj.gunmod && !obj.has_flag( flag_MELEE_GUNMOD ) ) {
        for( const std::pair<const gun_mode_id, gun_modifier_data> &pr : obj.gunmod->mode_modifier ) {
            if( pr.first == gun_mode_REACH ) {
                obj.item_tags.insert( flag_MELEE_GUNMOD );
                break;
            }
        }
    }

    if( obj.gun ) {
        // If the gun has ammo types for which the default ammo has been migrated, we need to
        // replace those ammo types with that of the migrated ammo
        for( auto ammo_type_it = obj.gun->ammo.begin(); ammo_type_it != obj.gun->ammo.end(); ) {
            auto maybe_migrated = migrated_ammo.find( ammo_type_it->obj().default_ammotype() );
            if( maybe_migrated == migrated_ammo.end() ) {
                ++ammo_type_it;
                continue;
            }

            const ammotype old_ammo = *ammo_type_it;

            // Remove the old ammotype add the migrated version
            ammo_type_it = obj.gun->ammo.erase( ammo_type_it );
            const ammotype &new_ammo = maybe_migrated->second;
            obj.gun->ammo.insert( obj.gun->ammo.begin(), new_ammo );

            // Migrate the compatible magazines
            auto old_mag_it = obj.magazines.find( old_ammo );
            if( old_mag_it != obj.magazines.end() ) {
                for( const itype_id &old_mag : old_mag_it->second ) {
                    obj.magazines[new_ammo].insert( old_mag );
                }
                obj.magazines.erase( old_ammo );
            }

            // And the default magazines for each magazine type
            auto old_default_mag_it = obj.magazine_default.find( old_ammo );
            if( old_default_mag_it != obj.magazine_default.end() ) {
                const itype_id &old_default_mag = old_default_mag_it->second;
                obj.magazine_default[new_ammo] = old_default_mag;
                obj.magazine_default.erase( old_ammo );
            }
        }



        // if the gun doesn't have a DEFAULT mode then add one now
        obj.gun->modes.emplace( gun_mode_id( "DEFAULT" ),
                                gun_modifier_data( defmode_name( obj ), 1, std::set<std::string>() ) );

        // If a "gun" has a reach attack, give it an additional melee mode.
        if( obj.has_flag( flag_REACH_ATTACK ) ) {
            obj.gun->modes.emplace( gun_mode_id( "MELEE" ),
                                    gun_modifier_data( translate_marker( "melee" ), 1,
            { "MELEE" } ) );
        }
        if( obj.gun->burst > 1 ) {
            // handle legacy JSON format
            obj.gun->modes.emplace( gun_mode_id( "AUTO" ),
                                    gun_modifier_data( translate_marker( "auto" ), obj.gun->burst,
                                            std::set<std::string>() ) );
        }

        if( obj.gun->handling < 0 ) {
            bool burst_only = std::all_of( obj.gun->modes.begin(), obj.gun->modes.end(),
            []( const std::pair<gun_mode_id, gun_modifier_data> &pr ) {
                return pr.second.qty() > 1 || pr.second.flags().contains( "MELEE" );
            } );
            // TODO: specify in JSON via classes
            if( obj.gun->skill_used == skill_id( "shotgun" ) ) {
                obj.gun->handling = 30;
            } else if( obj.gun->skill_used == skill_id( "rifle" ) ||
                       obj.gun->skill_used == skill_id( "smg" ) ) {
                obj.gun->handling = burst_only ? 40 : 20;
            } else {
                obj.gun->handling = burst_only ? 25 : 15;
            }
        }

        obj.gun->reload_noise = _( obj.gun->reload_noise );
    }

    set_allergy_flags( obj );
    hflesh_to_flesh( obj );
    npc_implied_flags( obj );

    if( obj.comestible ) {
        std::map<vitamin_id, int> &vitamins = obj.comestible->default_nutrition.vitamins;
        if( get_option<bool>( "NO_VITAMINS" ) ) {
            for( auto &vit : vitamins ) {
                if( vit.first->type() == vitamin_type::VITAMIN ) {
                    vit.second = 0;
                }
            }
        } else if( vitamins.empty() && obj.comestible->healthy >= 0 ) {
            // Default vitamins of healthy comestibles to their edible base materials if none explicitly specified.
            auto healthy = std::max( obj.comestible->healthy, 1 ) * 10;
            auto &mat = obj.materials;

            // TODO: migrate inedible comestibles to appropriate alternative types.
            mat.erase( std::remove_if( mat.begin(), mat.end(), []( const string_id<material_type> &m ) {
                return !m.obj().edible();
            } ), mat.end() );

            // For comestibles composed of multiple edible materials we calculate the average.
            for( const auto &v : vitamin::all() ) {
                if( !vitamins.contains( v.first ) ) {
                    for( const auto &m : mat ) {
                        double amount = m->vitamin( v.first ) * healthy / mat.size();
                        vitamins[v.first] += std::ceil( amount );
                    }
                }
            }
        }
    }

    if( obj.tool ) {
        if( !obj.tool->subtype.is_empty() && has_template( obj.tool->subtype ) ) {
            tool_subtypes[ obj.tool->subtype ].insert( obj.id );
        }
    }

    // Legacy food heating stuff
    {
        // Needs to be split into 2 functions + flag
        if( obj.use_methods.contains( "HOTPLATE" ) ) {
            obj.use_methods.erase( "HOTPLATE" );
            obj.use_methods["TOGGLE_HEATS_FOOD"] =
                use_function( "TOGGLE_HEATS_FOOD", &iuse::toggle_heats_food );
            cauterize_actor cauterize;
            cauterize.flame = false;
            obj.use_methods["cauterize"] = cauterize.clone();
            obj.item_tags.insert( flag_HEATS_FOOD_USING_CHARGES );
        }

        if( obj.use_methods.contains( "HEAT_FOOD" ) ) {
            obj.use_methods.erase( "HEAT_FOOD" );
            obj.item_tags.insert( flag_HEATS_FOOD_USING_FIRE );
        }

        if( obj.use_methods.contains( "HEATPACK" ) ) {
            obj.use_methods.erase( "HEATPACK" );
            obj.use_methods["TOGGLE_HEATS_FOOD"] =
                use_function( "TOGGLE_HEATS_FOOD", &iuse::toggle_heats_food );
            obj.item_tags.insert( flag_HEATS_FOOD_IS_CONSUMED );
        }
    }

    for( auto &e : obj.use_methods ) {
        e.second.get_actor_ptr()->finalize( obj.id );
    }

    if( obj.drop_action.get_actor_ptr() != nullptr ) {
        obj.drop_action.get_actor_ptr()->finalize( obj.id );
    }

    if( obj.has_flag( flag_PERSONAL ) ) {
        obj.layer = PERSONAL_LAYER;
    } else if( obj.has_flag( flag_SKINTIGHT ) ) {
        obj.layer = UNDERWEAR_LAYER;
    } else if( obj.has_flag( flag_WAIST ) ) {
        obj.layer = WAIST_LAYER;
    } else if( obj.has_flag( flag_OUTER ) ) {
        obj.layer = OUTER_LAYER;
    } else if( obj.has_flag( flag_BELTED ) ) {
        obj.layer = BELTED_LAYER;
    } else if( obj.has_flag( flag_AURA ) ) {
        obj.layer = AURA_LAYER;
    } else {
        obj.layer = REGULAR_LAYER;
    }

    if( obj.can_use( "MA_MANUAL" ) && obj.book && obj.book->martial_art.is_null() &&
        obj.get_id().str().starts_with( "manual_" ) ) {
        // HACK: Legacy martial arts books rely on a hack whereby the name of the
        // martial art is derived from the item id
        obj.book->martial_art = matype_id( "style_" + obj.get_id().str().substr( 7 ) );
    }

    if( obj.armor ) {

        auto set_resist = [&obj]( damage_type dt,
        std::function<int( const material_type & )> resist_getter ) {
            if( obj.armor->resistance.flat.contains( dt ) ) {
                return;
            }
            float resist = 0.0f;
            if( !obj.materials.empty() ) {
                for( const material_id &mat : obj.materials ) {
                    resist += resist_getter( *mat );
                }
                resist /= obj.materials.size();
            }

            obj.armor->resistance.flat[dt] = std::lround( resist * obj.armor->thickness );
        };
        set_resist( DT_BASH, &material_type::bash_resist );
        set_resist( DT_CUT, &material_type::cut_resist );
        set_resist( DT_STAB, []( const material_type & t ) {
            return t.cut_resist() * 0.8f;
        } );
        set_resist( DT_BULLET, &material_type::bullet_resist );
    }
}

void Item_factory::register_cached_uses( const itype &obj )
{
    for( auto &e : obj.use_methods ) {
        // can this item function as a repair tool?
        if( repair_actions.contains( e.first ) ) {
            repair_tools.insert( obj.id );
        }

        // can this item be used to repair complex firearms?
        if( e.first == "GUN_REPAIR" ) {
            gun_tools.insert( obj.id );
        }
    }
}

void Item_factory::finalize_post( itype &obj,
                                  const std::unordered_map<material_id, std::set<itype_id>> &repair_mat_index )
{
    erase_if( obj.item_tags, [&]( const flag_id & f ) {
        if( !f.is_valid() ) {
            debugmsg( "itype '%s' uses undefined flag '%s'. Please add corresponding 'json_flag' entry to json.",
                      obj.id.str(), f.str() );
            return true;
        }
        return false;
    } );

    // handle complex firearms as a special case
    if( obj.gun && !obj.has_flag( flag_PRIMITIVE_RANGED_WEAPON ) ) {
        std::copy( gun_tools.begin(), gun_tools.end(), std::inserter( obj.repair, obj.repair.begin() ) );
        return;
    }

    // O(materials) lookup into pre-built index instead of O(tools * actions) nested scan
    for( const material_id &mat : obj.materials ) {
        const auto it = repair_mat_index.find( mat );
        if( it != repair_mat_index.end() ) {
            obj.repair.insert( it->second.begin(), it->second.end() );
        }
    }

    if( obj.comestible ) {
        for( const std::pair<diseasetype_id, int> elem : obj.comestible->contamination ) {
            const diseasetype_id dtype = elem.first;
            if( !dtype.is_valid() ) {
                debugmsg( "contamination in %s contains invalid diseasetype_id %s.",
                          obj.id.str(), dtype.str() );
            }
        }
    }
}

void Item_factory::finalize()
{
    DynamicDataLoader::get_instance().sort_deferred( deferred, "id" );
    DynamicDataLoader::get_instance().load_deferred( deferred );

    finalize_item_blacklist();

    // we can no longer add or adjust static item templates
    frozen = true;

    for( auto &e : m_templates ) {
        finalize_pre( e.second );
        register_cached_uses( e.second );
    }

    // Build material → repair-tools index once (O(tools * actions)) so
    // finalize_post can do O(materials) lookup instead of O(tools * actions)
    // per item — avoids ~600k iterations of dynamic_cast + any_of over materials.
    repair_mat_index_.clear();
    for( const auto &tool : repair_tools ) {
        for( const auto &act : repair_actions ) {
            const use_function *func = m_templates[tool].get_use( act );
            if( func == nullptr ) {
                continue;
            }
            const auto &opts = dynamic_cast<const repair_item_actor *>( func->get_actor_ptr() )->materials;
            for( const material_id &mat : opts ) {
                repair_mat_index_[mat].insert( tool );
            }
        }
    }

    for( auto &e : m_templates ) {
        finalize_post( e.second, repair_mat_index_ );
    }

    // We may actually have some runtimes here - ones loaded from saved game
    // TODO: support for runtimes that repair
    for( auto &e : m_runtimes ) {
        finalize_pre( *e.second );
        finalize_post( *e.second, repair_mat_index_ );
    }

    // Wire Lua callback actor pointers onto itype objects
    resolve_lua_callbacks();

    // for each item register all (non-obsolete) potential recipes
    for( const std::pair<const recipe_id, recipe> &p : recipe_dict ) {
        const recipe &rec = p.second;
        if( rec.obsolete || rec.will_be_blacklisted() ) {
            continue;
        }
        const itype_id &result = rec.result();
        auto it = m_templates.find( result );
        if( it != m_templates.end() ) {
            it->second.recipes.push_back( p.first );
        }
    }
}

void Item_factory::finalize_item_blacklist()
{
    for( const itype_id &blackout : item_blacklist ) {
        std::unordered_map<itype_id, itype>::iterator candidate = m_templates.find( blackout );
        if( candidate == m_templates.end() ) {
            debugmsg( "item on blacklist %s does not exist", blackout.c_str() );
            continue;
        }

        for( std::pair<const item_group_id, std::unique_ptr<Item_spawn_data>> &g : m_template_groups ) {
            g.second->remove_item( candidate->first );
        }

        // remove any blacklisted items from requirements
        for( const std::pair<const requirement_id, requirement_data> &r : requirement_data::all() ) {
            const_cast<requirement_data &>( r.second ).blacklist_item( candidate->first );
        }

        // remove any recipes used to craft the blacklisted item
        recipe_dictionary::delete_if( [&candidate]( const recipe & r ) {
            return r.result() == candidate->first;
        } );
    }
    for( vproto_id &vid : vehicle_prototype::get_all() ) {
        vehicle_prototype &prototype = const_cast<vehicle_prototype &>( vid.obj() );
        for( vehicle_item_spawn &vis : prototype.item_spawns ) {
            auto &vec = vis.item_ids;
            const auto iter = std::remove_if( vec.begin(), vec.end(), item_is_blacklisted );
            vec.erase( iter, vec.end() );
        }
    }

    // Validate migrations and collect metadata side-effects (ammo/magazine) in one
    // pass before the expensive group/requirement sweeps.
    std::unordered_map<itype_id, itype_id> valid_migrations;
    for( const std::pair<const itype_id, migration> &migrate : migrations ) {
        if( !m_templates.contains( migrate.second.replace ) ) {
            debugmsg( "Replacement item for migration %s does not exist", migrate.first.c_str() );
            continue;
        }
        valid_migrations.emplace( migrate.first, migrate.second.replace );

        // If the default ammo of an ammo_type gets migrated, we migrate all guns using that ammo
        // type to the ammo type of whatever that default ammo was migrated to.
        // To do that we need to store a map of ammo to the migration replacement thereof.
        auto maybe_ammo = m_templates.find( migrate.first );
        // If the itype_id is valid and the itype has ammo data
        if( maybe_ammo != m_templates.end() && maybe_ammo->second.ammo ) {
            auto replacement = m_templates.find( migrate.second.replace );
            if( replacement->second.ammo ) {
                migrated_ammo.emplace( migrate.first, replacement->second.ammo->type );
            } else {
                debugmsg( "Replacement item %s for migrated ammo %s is not ammo.",
                          migrate.second.replace.str(), migrate.first.str() );
            }
        }

        // migrate magazines as well
        auto maybe_mag = m_templates.find( migrate.first );
        if( maybe_mag != m_templates.end() && maybe_mag->second.magazine ) {
            auto replacement = m_templates.find( migrate.second.replace );
            if( replacement->second.magazine ) {
                migrated_magazines.emplace( migrate.first, migrate.second.replace );
            } else {
                debugmsg( "Replacement item %s for migrated magazine %s is not a magazine.",
                          migrate.second.replace.str(), migrate.first.str() );
            }
        }
    }

    // ONE pass per group across all migrations — O(groups) calls to replace_items
    // instead of O(migrations × groups) calls to replace_item.
    // replace_items(map) walks each group tree once with a map lookup per node.
    for( std::pair<const item_group_id, std::unique_ptr<Item_spawn_data>> &g : m_template_groups ) {
        g.second->replace_items( valid_migrations );
    }

    // replace migrated items in requirements — O(migrations × requirements), ~15ms,
    // not worth adding batch method to requirements.h (>10-usage header) here.
    for( const std::pair<const itype_id, migration> &migrate : migrations ) {
        if( !valid_migrations.contains( migrate.first ) ) {
            continue;
        }
        for( const std::pair<const requirement_id, requirement_data> &r : requirement_data::all() ) {
            const_cast<requirement_data &>( r.second ).replace_item( migrate.first,
                    migrate.second.replace );
        }
    }

    // ONE delete_if pass for all migrated recipes together — O(recipes + migrations)
    // instead of O(migrations × recipes).
    // remove any recipes used to craft the migrated item;
    // if there's a valid recipe, it will be for the replacement.
    recipe_dictionary::delete_if( [&valid_migrations]( const recipe & r ) {
        return !r.obsolete && valid_migrations.contains( r.result() );
    } );

    for( vproto_id &vid : vehicle_prototype::get_all() ) {
        vehicle_prototype &prototype = const_cast<vehicle_prototype &>( vid.obj() );
        for( vehicle_item_spawn &vis : prototype.item_spawns ) {
            for( itype_id &type_to_spawn : vis.item_ids ) {
                std::map<itype_id, migration>::iterator replacement =
                    migrations.find( type_to_spawn );
                if( replacement != migrations.end() ) {
                    type_to_spawn = replacement->second.replace;
                }
            }
        }
    }
}

