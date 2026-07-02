#include "item.h"

#include <algorithm>
#include <numeric>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_set>

#include "active_tile_data_def.h"
#include "ammo.h"
#include "ascii_art.h"
#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "cached_item_options.h"
#include "catalua_icallback_actor.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "character.h"
#include "character_encumbrance.h"
#include "character_functions.h"
#include "character_id.h"
#include "character_martial_arts.h"
#include "character_stat.h"
#include "clothing_mod.h"
#include "clzones.h"
#include "color.h"
#include "craft_command.h"
#include "damage.h"
#include "debug.h"
#include "dispersion.h"
#include "drop_token.h"
#include "effect.h" // for weed_msg
#include "enums.h"
#include "explosion.h"
#include "faction.h"
#include "fault.h"
#include "field_type.h"
#include "fire.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "gun_mode.h"
#include "iexamine.h"
#include "int_id.h"
#include "inventory.h"
#include "item_category.h"
#include "item_factory.h"
#include "item_group.h"
#include "iteminfo_format_utils.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "line.h"
#include "locations.h"
#include "magic.h"
#include "magic_enchantment.h"
#include "map.h"
#include "martialarts.h"
#include "material.h"
#include "melee.h"
#include "messages.h"
#include "mod_manager.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "pimpl.h"
#include "player.h"
#include "player_activity.h"
#include "pldata.h"
#include "point.h"
#include "projectile.h"
#include "profile.h"
#include "ranged.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "relic.h"
#include "requirements.h"
#include "ret_val.h"
#include "rng.h"
#include "rot.h"
#include "scores_ui.h"
#include "cloning_utils.h"
#include "skill.h"
#include "stomach.h"
#include "string_formatter.h"
#include "string_id_utils.h"
#include "string_utils.h"
#include "text_snippets.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "units_energy.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vitamin.h"
#include "vpart_position.h"
#include "weather.h"
#include "weather_gen.h"
#include "wheel_dimensions.h"

static const std::string GUN_MODE_VAR_NAME( "item::mode" );
static const std::string CLOTHING_MOD_VAR_PREFIX( "clothing_mod_" );


static const ammotype ammo_battery( "battery" );
static const ammotype ammo_plutonium( "plutonium" );

static const item_category_id itemcat_drugs( "drugs" );
static const item_category_id itemcat_food( "food" );
static const item_category_id itemcat_maps( "maps" );

static const efftype_id effect_cig( "cig" );
static const efftype_id effect_shakes( "shakes" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_weed_high( "weed_high" );

static const fault_id fault_gun_blackpowder( "fault_gun_blackpowder" );
static const fault_id fault_bionic_nonsterile( "fault_bionic_nonsterile" );

static const flag_id flag_MARIJUANA( "MARIJUANA" );

static const gun_mode_id gun_mode_REACH( "REACH" );

static const itype_id itype_barrel_small( "barrel_small" );
static const itype_id itype_cig_butt( "cig_butt" );
static const itype_id itype_cig_lit( "cig_lit" );
static const itype_id itype_cigar_butt( "cigar_butt" );
static const itype_id itype_cigar_lit( "cigar_lit" );
static const itype_id itype_joint_roach( "joint_roach" );
static const itype_id itype_plut_cell( "plut_cell" );
static const itype_id itype_tuned_mechanism( "tuned_mechanism" );
static const itype_id itype_stock_small( "stock_small" );
static const itype_id itype_UPS( "UPS" );
static const flag_id flag_genome_drive( "GENOME_DRIVE" );
static const itype_id itype_bio_armor( "bio_armor" );
static const itype_id itype_waterproof_gunmod( "waterproof_gunmod" );
static const itype_id itype_water( "water" );
static const itype_id itype_water_acid( "water_acid" );
static const itype_id itype_water_acid_weak( "water_acid_weak" );

static const skill_id skill_survival( "survival" );
static const skill_id skill_throw( "throw" );
static const skill_id skill_unarmed( "unarmed" );
static const skill_id skill_weapon( "weapon" );

static const species_id ROBOT( "ROBOT" );



static const trait_id trait_LIGHTWEIGHT( "LIGHTWEIGHT" );
static const trait_id trait_TOLERANCE( "TOLERANCE" );
static const trait_id trait_WOOLALLERGY( "WOOLALLERGY" );


static const std::string flag_FLAMMABLE( "FLAMMABLE" );
static const std::string flag_FLAMMABLE_ASH( "FLAMMABLE_ASH" );
static const std::string flag_DEEP_WATER( "DEEP_WATER" );
static const std::string flag_LIQUID( "LIQUID" );
static const std::string flag_LIQUIDCONT( "LIQUIDCONT" );

static const std::string has_thievery_witness( "has_thievery_witness" );
static const activity_id ACT_PICKUP( "ACT_PICKUP" );


class npc_class;

using npc_class_id = string_id<npc_class>;

std::string rad_badge_color( const int rad )
{
    using pair_t = std::pair<const int, const translation>;

    static const std::array<pair_t, 6> values = {{
            pair_t {  0, to_translation( "color", "green" ) },
            pair_t { 30, to_translation( "color", "blue" )  },
            pair_t { 60, to_translation( "color", "yellow" )},
            pair_t {120, to_translation( "color", "orange" )},
            pair_t {240, to_translation( "color", "red" )   },
            pair_t {500, to_translation( "color", "black" ) },
        }
    };

    for( const auto &i : values ) {
        if( rad <= i.first ) {
            return i.second.translated();
        }
    }

    return values.back().second.translated();
}

light_emission nolight = {0, 0, 0};

// Returns the default item type, used for the null item (default constructed),
// the returned pointer is always valid, it's never cleared by the @ref Item_factory.
static const itype *nullitem()
{
    static itype nullitem_m;
    return &nullitem_m;
}

item &null_item_reference()
{
    static item result{};
    // reset it, in case a previous caller has changed it
    result = item();
    return result;
}

namespace item_internal
{
bool goes_bad_temp_cache = false;
const item *goes_bad_temp_cache_for = nullptr;
inline bool goes_bad_cache_fetch()
{
    return goes_bad_temp_cache;
}
inline void goes_bad_cache_set( const item *i )
{
    goes_bad_temp_cache = i->goes_bad();
    goes_bad_temp_cache_for = i;
}
inline void goes_bad_cache_unset()
{
    goes_bad_temp_cache = false;
    goes_bad_temp_cache_for = nullptr;
}
inline bool goes_bad_cache_is_for( const item *i )
{
    return goes_bad_temp_cache_for == i;
}

struct scoped_goes_bad_cache {
    scoped_goes_bad_cache( item *i ) {
        goes_bad_cache_set( i );
    }
    ~scoped_goes_bad_cache() {
        goes_bad_cache_unset();
    }
};
} // namespace item_internal

const int item::INFINITE_CHARGES = INT_MAX;

item::item() : contents( this ),
    components( new component_item_location( this ) ),
    bday( calendar::start_of_cataclysm )
{
    type = nullitem();
    charges = 0;
}

item::item( const itype *type, time_point turn, int qty ) : type( type ),
    contents( this ),
    components( new component_item_location( this ) ), bday( turn )
{
    item_vars_ = type->item_vars;
    corpse = has_flag( flag_CORPSE ) ? &mtype_id::NULL_ID().obj() : nullptr;
    item_counter = type->countdown_interval;

    if( qty >= 0 ) {
        charges = qty;
    } else {
        if( type->tool && type->tool->rand_charges.size() > 1 ) {
            const int charge_roll = rng( 1, type->tool->rand_charges.size() - 1 );
            charges = rng( type->tool->rand_charges[charge_roll - 1], type->tool->rand_charges[charge_roll] );
        } else {
            charges = type->charges_default();
        }
    }

    if( has_flag( flag_NANOFAB_TEMPLATE ) ) {
        // Define all nanofab subgroups from nanofab_recipes.json
        auto all_groups = item_controller->get_all_group_names();

        // Prepare a vector to hold nanofab groups dynamically
        std::vector<item_group_id> nanofab_groups;

        // Populate it dynamically (this is probably pretty performance intensive, but allows for modded templates)
        for( const auto &group : all_groups ) {
            const std::string &name = group.str();
            if( name.starts_with( "nanofab_template_" ) ) {
                nanofab_groups.push_back( group );
            }
        }

        // Pick one subgroup randomly
        const item_group_id &chosen_group = random_entry( nanofab_groups );

        // Store which subgroup we picked
        set_var( "NANOFAB_GROUP_ID", chosen_group.str() );

        // Gather all possible items from this subgroup
        std::set<const itype *> all_items = item_group::every_possible_item_from( chosen_group );
        std::vector<const itype *> all_items_vec( all_items.begin(), all_items.end() );

        // Legacy compatibility: store the first item ID as fallback
        if( !all_items_vec.empty() ) {
            set_var( "NANOFAB_ITEM_ID", all_items_vec.front()->get_id().str() );
        }
    }


    if( type->gun ) {
        for( const itype_id &mod : type->gun->built_in_mods ) {
            detached_ptr<item> it = item::spawn( mod, turn, qty );
            it->set_flag( flag_IRREMOVABLE );
            put_in( std::move( it ) );
        }
        for( const itype_id &mod : type->gun->default_mods ) {
            put_in( item::spawn( mod, turn, qty ) );
        }

    } else if( type->magazine ) {
        if( type->magazine->count > 0 ) {
            put_in( item::spawn( type->magazine->default_ammo, calendar::turn, type->magazine->count ) );
        }

    } else if( goes_bad() ) {
        active = true;
        last_rot_check = bday;

    } else if( type->tool ) {
        if( ammo_remaining() && !ammo_types().empty() ) {
            ammo_set( ammo_default(), ammo_remaining() );
        }
    }

    if( ( type->gun || type->tool ) && !magazine_integral() ) {
        set_var( "magazine_converted", 1 );
    }

    if( !type->snippet_category.empty() ) {
        snip_id = SNIPPET.random_id_from_category( type->snippet_category );
    }

    // item always has any relic properties from itype.
    if( type->relic_data ) {
        relic_data = type->relic_data;
    }

    for( const auto &func : type->use_methods | std::views::values ) {
        const auto actor = func.get_actor_ptr();
        if( actor != nullptr ) {
            actor->on_spawned( *this );
        }
    }
}

item::item( const itype_id &id, time_point turn, int qty )
    : item( & * id, turn, qty ) {}

item::item( const itype *type, time_point turn, default_charges_tag )
    : item( type, turn, type->charges_default() ) {}

item::item( const itype_id &id, time_point turn, default_charges_tag tag )
    : item( & * id, turn, tag ) {}

item::item( const itype *type, time_point turn, solitary_tag )
    : item( type, turn, type->count_by_charges() ? 1 : -1 ) {}

item::item( const itype_id &id, time_point turn, solitary_tag tag )
    : item( & * id, turn, tag ) {}

static const item *get_most_rotten_component( const item &craft )
{
    const item *most_rotten = nullptr;
    for( const item * const &it : craft.get_components() ) {
        if( it->goes_bad() ) {
            if( !most_rotten || it->get_relative_rot() > most_rotten->get_relative_rot() ) {
                most_rotten = it;
            }
        }
    }
    return most_rotten;
}

item::item( const recipe *rec, int qty, std::vector<detached_ptr<item>> &&items,
            std::vector<item_comp> &&selections )
    : item( "craft", calendar::turn, qty )
{
    craft_data_ = cata::make_value<craft_data>();
    craft_data_->making = rec;
    for( detached_ptr<item> &it : items ) {
        components.push_back( std::move( it ) );
    }
    craft_data_->comps_used = std::move( selections );

    if( is_food() ) {
        activate();
        last_rot_check = bday;
        if( goes_bad() ) {
            const item *most_rotten = get_most_rotten_component( *this );
            if( most_rotten ) {
                set_relative_rot( most_rotten->get_relative_rot() );
            }
        }
    }

    for( item * const &component : components ) {
        for( const flag_id &f : component->item_tags ) {
            if( f->craft_inherit() ) {
                set_flag( f );
            }
        }
        for( const flag_id &f : component->type->get_flags() ) {
            if( f->craft_inherit() ) {
                set_flag( f );
            }
        }
    }
    // this extra section is so that in-progress crafts will correctly display expected flags.
    for( const flag_id &flag : rec->flags_to_delete ) {
        unset_flag( flag );
    }
}

item::item( const item &source ) : game_object<item>( source ), contents( this ),
    components( new component_item_location( this ) )
{
    //TODO!: back to defaults
    //Awful copy block, this can be avoided with equally awful inheritance shenanigans but...
    type = source.type;
    faults = source.faults;
    item_tags = source.item_tags;
    curammo = source.curammo;
    item_vars_ = source.item_vars_;
    corpse = source.corpse;
    corpse_name = source.corpse_name;
    techniques = source.techniques;
    craft_data_ = source.craft_data_;
    relic_data = source.relic_data;
    charges = source.charges;
    energy = source.energy;
    recipe_charges = source.recipe_charges;
    burnt = source.burnt;
    poison = source.poison;
    frequency = source.frequency;
    snip_id = source.snip_id;
    irradiation = source.irradiation;
    item_counter = source.item_counter;
    mission_id = source.mission_id;
    player_id = source.player_id;
    encumbrance_update_ = source.encumbrance_update_;
    rot = source.rot;
    last_rot_check = source.last_rot_check;
    bday = source.bday;
    owner = source.owner;
    old_owner = source.old_owner;
    damage_ = source.damage_;
    light = source.light;
    invlet = source.invlet;
    active = source.active;
    activated_by = source.activated_by;
    is_favorite = source.is_favorite;

    for( item * const &it : source.contents.all_items_top() ) {
        contents.insert_item( item::spawn( *it ) );
    }

    for( item * const &it : source.components ) {
        components.push_back( item::spawn( *it ) );
    }

    for( const auto &func : type->use_methods | std::views::values ) {
        const auto actor = func.get_actor_ptr();
        if( actor != nullptr ) {
            actor->on_spawned( *this );
        }
    }
}

item &item::operator=( const item &source )
{
    type = source.type;
    faults = source.faults;
    item_tags = source.item_tags;
    curammo = source.curammo;
    item_vars_ = source.item_vars_;
    corpse = source.corpse;
    corpse_name = source.corpse_name;
    techniques = source.techniques;
    craft_data_ = source.craft_data_;
    relic_data = source.relic_data;
    charges = source.charges;
    energy = source.energy;
    recipe_charges = source.recipe_charges;
    burnt = source.burnt;
    poison = source.poison;
    frequency = source.frequency;
    snip_id = source.snip_id;
    irradiation = source.irradiation;
    item_counter = source.item_counter;
    mission_id = source.mission_id;
    player_id = source.player_id;
    encumbrance_update_ = source.encumbrance_update_;
    rot = source.rot;
    last_rot_check = source.last_rot_check;
    bday = source.bday;
    owner = source.owner;
    old_owner = source.old_owner;
    damage_ = source.damage_;
    light = source.light;
    invlet = source.invlet;
    active = source.active;
    activated_by = source.activated_by;
    is_favorite = source.is_favorite;

    contents.clear_items();

    for( item * const &it : source.contents.all_items_top() ) {
        contents.insert_item( item::spawn( *it ) );
    }

    components.clear();

    for( item * const &it : source.components ) {
        components.push_back( item::spawn( *it ) );
    }

    for( const auto &func : type->use_methods | std::views::values ) {
        const auto actor = func.get_actor_ptr();
        if( actor != nullptr ) {
            actor->on_spawned( *this );
        }
    }

    return *this;
}

void item::on_destroy()
{
    //These are getting left out until it can be deferred better
    //components.on_destroy();
    //contents.on_destroy();
}


item::~item() = default;

detached_ptr<item> item::make_corpse( const mtype_id &mt, time_point turn, const std::string &name,
                                      const int upgrade_time )
{
    if( !mt.is_valid() ) {
        debugmsg( "tried to make a corpse with an invalid mtype id" );
    }

    std::string corpse_type = mt == mtype_id::NULL_ID() ? "corpse_generic_human" : "corpse";

    detached_ptr<item> result = item::spawn( corpse_type, turn );

    result->corpse = &mt.obj();

    if( result->corpse->has_flag( MF_REVIVES ) ) {
        if( one_in( 20 ) ) {
            result->set_flag( flag_REVIVE_SPECIAL );
        }
        result->set_var( "upgrade_time", std::to_string( upgrade_time ) );
    }

    // This is unconditional because the const itemructor above sets result.name to
    // "human corpse".
    result->corpse_name = name;

    return  result;
}

void item::convert( const itype_id &new_type )
{
    type = &*new_type;
    relic_data = type->relic_data;
}

void item::deactivate()
{
    if( !is_active() ) {
        return; // no-op
    }

    active = false;
    if( is_tool() ) {
        type->tool->turns_active = 0;
    }

    // Is not placed in the world, so either a template of some kind or a temporary item.
    if( !has_position() ) {
        return;
    }
    switch( where() ) {
        case item_location_type::map:
            get_map().make_inactive( *this );
            break;
        case item_location_type::vehicle:
            get_map().veh_at( tripoint_bub_ms( position() ) )->vehicle().make_inactive( *this );
            break;
        default:
            break;
    }

}

void item::activate()
{
    if( is_active() ) {
        return; // no-op
    }

    if( type->countdown_interval > 0 ) {
        set_counter( type->countdown_interval );
    }

    active = true;

    // Is not placed in the world, so either a template of some kind or a temporary item.
    if( !has_position() ) {
        return;
    }
    switch( where() ) {
        case item_location_type::map:
            get_map().make_active( *this );
            break;
        case item_location_type::vehicle:
            get_map().veh_at( tripoint_bub_ms( position() ) )->vehicle().make_active( *this );
            break;
        default:
            break;
    }
}

bool item::revert( const Character *ch, bool alert )
{
    const auto &tooldata = type->tool;
    // Can't be reverted, prevents destruction of irrevertable items.
    if( !tooldata->revert_to.has_value() ) {
        return false;
    }
    if( ch && alert && !tooldata->revert_msg.empty() ) {
        ch->add_msg_if_player( m_info, _( tooldata->revert_msg ), tname() );
    }
    convert( *tooldata->revert_to );
    return true;
}

units::energy item::mod_energy( const units::energy &qty )
{
    if( !is_battery() ) {
        debugmsg( "Tried to set energy of non-battery item" );
        return 0_J;
    }

    units::energy val = energy_remaining() + qty;
    if( val < 0_J ) {
        return val;
    } else if( val > type->battery->max_capacity ) {
        energy = type->battery->max_capacity;
    } else {
        energy = val;
    }
    return 0_J;
}

void item::ammo_set( const itype_id &ammo, int qty )
{
    if( qty < 0 ) {
        // completely fill an integral or existing magazine
        if( magazine_integral() || magazine_current() ) {
            qty = ammo_capacity();

            // else try to add a magazine using default ammo count property if set
        } else if( !magazine_default().is_null() ) {
            item mag( magazine_default() );
            if( mag.type->magazine->count > 0 ) {
                qty = mag.type->magazine->count;
            } else {
                qty = mag.ammo_capacity();
            }
        }
    }

    if( qty <= 0 ) {
        ammo_unset();
        return;
    }

    // handle reloadable tools and guns with no specific ammo type as special case
    if( ( ammo.is_null() && ammo_types().empty() ) || is_money() ) {
        if( ( is_tool() || is_gun() ) && magazine_integral() ) {
            curammo = nullptr;
            charges = std::min( qty, ammo_capacity() );
        }
        return;
    }

    // check ammo is valid for the item
    const itype *atype = &*ammo;
    if( !atype->ammo || !ammo_types().contains( atype->ammo->type ) ) {
        debugmsg( "Tried to set invalid ammo %s[%d] for %s", atype->get_id(), qty, typeId() );
        return;
    }

    if( is_magazine() ) {
        ammo_unset();
        detached_ptr<item> set_ammo = item::spawn( ammo, calendar::turn, std::min( qty,
                                      ammo_capacity() ) );
        if( has_flag( flag_NO_UNLOAD ) ) {
            set_ammo->set_flag( flag_NO_DROP );
            set_ammo->set_flag( flag_IRREMOVABLE );
        }
        put_in( std::move( set_ammo ) );

    } else if( magazine_integral() ) {
        curammo = atype;
        charges = std::min( qty, ammo_capacity() );

    } else {
        if( !magazine_current() ) {
            itype_id mag = magazine_default();
            if( !mag->magazine ) {
                debugmsg( "Tried to set ammo %s[%d] without suitable magazine for %s",
                          atype->get_id(), qty, typeId() );
                return;
            }

            // if default magazine too small fetch instead closest available match
            if( mag->magazine->capacity < qty ) {
                // as above call to magazine_default successful can infer minimum one option exists
                auto iter = type->magazines.find( atype->ammo->type );
                if( iter == type->magazines.end() ) {
                    debugmsg( "%s doesn't have a magazine for %s",
                              typeId(), ammo );
                    return;
                }
                std::vector<itype_id> opts( iter->second.begin(), iter->second.end() );
                std::ranges::sort( opts, []( const itype_id & lhs, const itype_id & rhs ) {
                    return lhs->magazine->capacity < rhs->magazine->capacity;
                } );
                mag = opts.back();
                for( const itype_id &e : opts ) {
                    if( e->magazine->capacity >= qty ) {
                        mag = e;
                        break;
                    }
                }
            }
            put_in( item::spawn( mag ) );
        }
        magazine_current()->ammo_set( ammo, qty );
    }

}

void item::ammo_unset()
{
    if( !is_tool() && !is_gun() && !is_magazine() ) {
        // do nothing
    } else if( is_magazine() ) {
        contents.clear_items();
    } else if( magazine_integral() ) {
        curammo = nullptr;
        charges = 0;
    } else if( magazine_current() ) {
        magazine_current()->ammo_unset();
    }

}

int item::damage() const
{
    return damage_;
}

int item::damage_level( int max ) const
{
    if( damage_ == 0 || max <= 0 ) {
        return 0;
    } else if( max_damage() <= 1 ) {
        return damage_ > 0 ? max : damage_;
    } else if( damage_ < 0 ) {
        return -( ( max - 1 ) * ( -damage_ - 1 ) / ( max_damage() - 1 ) + 1 );
    } else {
        return ( max - 1 ) * ( damage_ - 1 ) / ( max_damage() - 1 ) + 1;
    }
}

void item::set_damage( int qty )
{
    on_damage( qty - damage_, DT_TRUE );
    damage_ = std::max( std::min( qty, max_damage() ), min_damage() );
}

detached_ptr<item> item::split( int qty )
{
    const bool split_from_preserving_container = goes_bad() && is_in_preserving_container();
    if( split_from_preserving_container ) {
        mark_rot_checked_now();
    }
    if( qty <= 0 || !count_by_charges() || qty >= charges ) {
        return detach();
    }
    detached_ptr<item> res = item::spawn( *this );
    res->charges = qty;
    charges -= qty;
    if( split_from_preserving_container ) {
        res->mark_rot_checked_now();
    }
    return res;
}

detached_ptr<item> item::unsafe_split( int qty )
{
    if( !count_by_charges() ) {
        debugmsg( "Attempted to unsafe_split a non-count by charges item." );
        return detached_ptr<item>();
    }
    if( qty == 0 || qty >= charges ) {
        qty = charges;
    }
    detached_ptr<item> res = item::spawn( *this );
    res->charges = qty;
    charges -= qty;
    return res;
}

void item::unsafe_rejoin( item &old )
{
    if( old.charges != 0 ) {
        return;
    }

    merge_charges( old.detach(), true );
}

bool item::attempt_detach( std::function < detached_ptr<item>( detached_ptr<item> && ) > cb )
{
    if( is_null() ) {
        return false;
    }
    if( goes_bad() && is_in_preserving_container() ) {
        mark_rot_checked_now();
    }
    if( count_by_charges() ) {
        return attempt_split( 0, cb );
    }
    return game_object<item>::attempt_detach( cb );
}

bool item::attempt_split( int qty,
                          const std::function < detached_ptr<item>( detached_ptr<item> && ) > & cb )
{
    const bool split_from_preserving_container = goes_bad() && is_in_preserving_container();
    if( split_from_preserving_container ) {
        mark_rot_checked_now();
    }
    const bool split_needs_rot_actualization = goes_bad() && has_position() &&
        !split_from_preserving_container;
    const auto split_pos = split_needs_rot_actualization ? position() : tripoint_bub_ms::zero();
    const auto vehicle_loc = dynamic_cast<vehicle_item_location *>( loc );
    const auto split_temperature = !split_needs_rot_actualization ? temperature_flag::TEMP_NORMAL :
                                   vehicle_loc != nullptr ? vehicle_loc->storage_temperature() :
                                   rot::temperature_flag_for_location( get_map(), *this );
    detached_ptr<item> det = unsafe_split( qty );
    if( det && split_from_preserving_container ) {
        det->mark_rot_checked_now();
    }
    if( det && split_needs_rot_actualization ) {
        det = actualize_rot( std::move( det ), split_pos, split_temperature, get_weather() );
    }
    if( !det ) {
        if( charges == 0 && has_position() ) {
            detach().release();
        }
        return false;
    }
    item &after_split = *det;
    int starting_charges = after_split.charges;
    det = cb( std::move( det ) );
    bool ret = true;
    bool changed = false;
    if( det ) {
        if( det->type->get_id() != type->get_id() ) {
            debugmsg( "attempt_split returned the wrong item type" );
        } else {
            changed |= det->charges != starting_charges;
            //Copy any changed properties from the new item, except the charges
            int old_charges = charges;
            *this = *det;
            charges = old_charges;
            merge_charges( std::move( det ), true );
        }
        ret = false;
    } else {
        changed = true;
    }
    if( changed ) {
        contents_item_location *contents_loc = dynamic_cast<contents_item_location *>( &*loc );
        if( contents_loc ) {
            contents_loc->on_changed( this );
        }
    }
    after_split.unsafe_rejoin( *this );
    return ret;
}

bool item::is_null() const
{
    // Actually, type should never by null at all.
    return ( type == nullptr || type == nullitem() || typeId().is_null() );
}

bool item::is_unarmed_weapon() const
{
    return has_flag( flag_UNARMED_WEAPON ) || is_null();
}

bool item::covers( const bodypart_id &bp ) const
{
    return get_covered_body_parts().test( bp.id() );
}

body_part_set item::get_covered_body_parts() const
{
    return get_covered_body_parts( get_side() );
}

body_part_set item::get_covered_body_parts( const side s ) const
{
    body_part_set res;

    if( is_gun() ) {
        // Currently only used for guns with the should strap mod, other guns might
        // go on another bodypart.
        res.set( bodypart_str_id( "torso" ) );
    }

    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return res;
    }

    for( const armor_portion_data &data : armor->data ) {
        res.unify_set( data.covers );
    }

    if( !armor->sided ) {
        return res; // Just ignore the side.
    }

    switch( s ) {
        case side::BOTH:
        case side::num_sides:
            break;

        case side::LEFT:
            res.reset( bodypart_str_id( "arm_r" ) );
            res.reset( bodypart_str_id( "hand_r" ) );
            res.reset( bodypart_str_id( "leg_r" ) );
            res.reset( bodypart_str_id( "foot_r" ) );
            break;

        case side::RIGHT:
            res.reset( bodypart_str_id( "arm_l" ) );
            res.reset( bodypart_str_id( "hand_l" ) );
            res.reset( bodypart_str_id( "leg_l" ) );
            res.reset( bodypart_str_id( "foot_l" ) );
            break;
    }

    return res;
}

bool item::is_sided() const
{
    const islot_armor *armor = find_armor_data();
    return armor ? armor->sided : false;
}

side item::get_side() const
{
    // MSVC complains if directly cast double to enum
    return static_cast<side>( static_cast<int>( get_var( "lateral",
           static_cast<int>( side::BOTH ) ) ) );
}

bool item::set_side( side s )
{
    if( !is_sided() ) {
        return false;
    }

    if( s == side::BOTH ) {
        erase_var( "lateral" );
    } else {
        set_var( "lateral", static_cast<int>( s ) );
    }

    return true;
}

bool item::swap_side()
{
    return set_side( opposite_side( get_side() ) );
}

bool item::is_worn_only_with( const item &it ) const
{
    return( ( has_flag( flag_POWERARMOR_EXTERNAL ) || has_flag( flag_POWERARMOR_MOD ) ) &&
    it.has_flag( flag_POWERARMOR_EXO ) );
}

detached_ptr<item> item::in_its_container( detached_ptr<item> &&self )
{
    return item::in_container( self->type->default_container.value_or( itype_id::NULL_ID() ),
                               std::move( self ) );
}

detached_ptr<item> item::in_container( const itype_id &cont, detached_ptr<item> &&self )
{
    if( !cont.is_null() ) {
        detached_ptr<item> ret = item::spawn( cont, self->birthday() );
        ret->invlet = self->invlet;
        item &obj = *self;
        ret->put_in( std::move( self ) );

        if( obj.made_of( LIQUID ) && ret->is_container() ) {
            // Note: we can't use any of the normal container functions as they check the
            // container being suitable (seals, watertight etc.)
            ret->contents.back().charges = obj.charges_per_volume( ret->get_container_capacity() );
        }
        return ret;
    } else {
        return std::move( self );
    }
}

int item::charges_per_volume( const units::volume &vol ) const
{
    if( count_by_charges() ) {
    if( type->volume == 0_ml ) {
            debugmsg( "Item '%s' with zero volume", tname() );
            return INFINITE_CHARGES;
        }
        // Type cast to prevent integer overflow with large volume containers like the cargo
        // dimension
        return vol * static_cast<int64_t>( type->stack_size ) / type->volume;
    } else {
        units::volume my_volume = volume();
        if( my_volume == 0_ml ) {
            debugmsg( "Item '%s' with zero volume", tname() );
            return INFINITE_CHARGES;
        }
        return vol / my_volume;
    }
}

bool item::display_stacked_with( const item &rhs, bool check_components ) const
{
    return !count_by_charges() && stacks_with( rhs, check_components );
}

bool item::stacks_with( const item &rhs, bool check_components, bool skip_type_check ) const
{
    if( !skip_type_check && type != rhs.type ) {
        return false;
    }
    if( is_relic() && rhs.is_relic() && !( *relic_data == *rhs.relic_data ) ) {
        return false;
    }
    if( is_money() && charges != 0 && rhs.charges != 0 ) {
        // Dealing with nonempty cash cards
        return true;
    }
    // This function is also used to test whether items counted by charges should be merged, for that
    // check the, the charges must be ignored. In all other cases (tools/guns), the charges are important.
    if( !count_by_charges() && charges != rhs.charges ) {
        return false;
    }
    if( is_favorite != rhs.is_favorite ) {
        return false;
    }

    if( is_corpse() || rhs.is_corpse() ) {
        return this->is_corpse() && rhs.is_corpse() && ( *this->get_mtype() == *rhs.get_mtype() );
    }

    if( damage_ != rhs.damage_ ) {
        return false;
    }
    if( burnt != rhs.burnt ) {
        return false;
    }
    if( is_active() != rhs.is_active() ) {
        return false;
    }
    if( item_tags != rhs.item_tags ) {
        return false;
    }
    if( faults != rhs.faults ) {
        return false;
    }
    if( techniques != rhs.techniques ) {
        return false;
    }
    if( item_vars_ != rhs.item_vars_ ) {
        return false;
    }

    if( craft_data_ || rhs.craft_data_ ) {
        // In-progress crafts are always distinct items. Easier to handle for the player,
        // and there shouldn't be that many items of this type around anyway.
        return false;
    }
    if( check_components || is_comestible() || is_craft() ) {
        //Only check if at least one item isn't using the default recipe or is comestible
        if( !components.empty() || !rhs.components.empty() ) {
            if( get_uncraft_components() != rhs.get_uncraft_components() ) {
                return false;
            }
        }
    }
    if( contents.num_item_stacks() != rhs.contents.num_item_stacks() ) {
        return false;
    }

    if( ammo_current() != rhs.ammo_current() ) {
        return false;
    }

    if( goes_bad() && rhs.goes_bad() ) {
        // Stack items that fall into the same "bucket" of freshness.
        // Distant buckets are larger than near ones.

        switch( merge_comestible_mode ) {
            case merge_comestible_t::merge_legacy: {
                std::pair<int, clipped_unit> my_clipped_time_to_rot =
                    clipped_time( get_shelf_life() - rot );
                std::pair<int, clipped_unit> other_clipped_time_to_rot =
                    clipped_time( rhs.get_shelf_life() - rhs.rot );
                if( my_clipped_time_to_rot != other_clipped_time_to_rot ) {
                    return false;
                }
            }
            break;
            case merge_comestible_t::merge_liquid: {
                if( !made_of( LIQUID ) || !rhs.made_of( LIQUID ) ) {
                    return false;
                }
            }
            [[fallthrough]];
            default:
                return std::abs( get_relative_rot() - rhs.get_relative_rot() ) <= similarity_threshold;
        }

        if( rotten() != rhs.rotten() ) {
            // just to be safe that rotten and unrotten food is *never* stacked.
            return false;
        }
    }

    return contents.stacks_with( rhs.contents );
}

namespace
{

time_duration weighted_averaged_rot( const item *a, const item *b )
{
    const int base_charges = a->charges + b->charges;

    return base_charges > 0
           ? ( a->get_rot() * a->charges + b->get_rot() * b->charges ) / base_charges
           : 0_seconds;
}

} // namespace

bool item::merge_charges( detached_ptr<item> &&rhs, bool force )
{
    if( this == &*rhs ) {
        debugmsg( "Attempted to merge %s with itself.", debug_name() );
        return false;
    }
    if( !count_by_charges() || ( !stacks_with( *rhs ) && !force ) ) {
        return false;
    }
    item &obj = *rhs;
    safe_reference<item>::merge( this, &*rhs );
    detached_ptr<item> del = std::move( rhs );

    const auto new_rot = weighted_averaged_rot( this, &obj );

    // Prevent overflow when either item has "near infinite" charges.
    if( charges >= INFINITE_CHARGES / 2 || obj.charges >= INFINITE_CHARGES / 2 ) {
        charges = INFINITE_CHARGES;
        return true;
    }
    // We'll just hope that the item counter represents the same thing for both items
    if( item_counter > 0 || obj.item_counter > 0 ) {
        item_counter = ( static_cast<double>( item_counter ) * charges + static_cast<double>
                         ( obj.item_counter ) * obj.charges ) / ( charges + obj.charges );
    }
    charges += obj.charges;

    rot = new_rot;
    set_age( std::max( age(), obj.age() ) );

    return true;
}

void item::put_in( detached_ptr<item> &&payload )
{
    if( !payload || payload->typeId() == itype_id::NULL_ID() ) {
        debugmsg( "Tried to insert non-item into %s", debug_name() );
        return;
    }
    if( &*payload == this ) {
        debugmsg( "Tried to put %s inside itself", debug_name().c_str() );
        return;
    }
    contents.insert_item( std::move( payload ) );
}

void item::add_item_with_id( const itype_id &itype, int count )
{
    detached_ptr<item> new_item = item::spawn( itype, calendar::turn, count );
    contents.insert_item( std::move( new_item ) );
}

bool item::has_item_with_id( const itype_id &itype ) const
{
    // shouldn't need to check any deeper than top-level
    std::vector<item *> item_contents = contents.all_items_top();
    for( item *itm : item_contents ) {
        if( itm->typeId() == itype ) {
            return true;
        }
    }
    return false;
}




item::sizing item::get_sizing( const Character &who ) const
{
    const islot_armor *armor_data = find_armor_data();
    if( !armor_data ) {
        return sizing::ignore;
    }
    bool to_ignore = true;
    for( const armor_portion_data &piece : armor_data->data ) {
        if( piece.encumber != 0 || piece.max_encumber != 0 ) {
            to_ignore = false;
        }
    }
    if( to_ignore ) {
        return sizing::ignore;
    } else {
        const bool small = who.get_size() == creature_size::tiny;
        const bool big = who.get_size() == creature_size::huge;

        // due to the iterative nature of these features, something can fit and be undersized/oversized
        // but that is fine because we have separate logic to adjust encumberance per each. One day we
        // may want to have fit be a flag that only applies if a piece of clothing is sized for you as there
        // is a bit of cognitive dissonance when something 'fits' and is 'oversized' and the same time
        const bool undersize = has_flag( flag_UNDERSIZE ) || has_flag( flag_resized_small );
        const bool oversize = has_flag( flag_OVERSIZE ) || has_flag( flag_resized_large );

        if( undersize ) {
            if( small ) {
                return sizing::small_sized_small_char;
            } else if( big ) {
                return sizing::small_sized_big_char;
            } else {
                return sizing::small_sized_human_char;
            }
        } else if( oversize ) {
            if( big ) {
                return sizing::big_sized_big_char;
            } else if( small ) {
                return sizing::big_sized_small_char;
            } else {
                return sizing::big_sized_human_char;
            }
        } else {
            if( big ) {
                return sizing::human_sized_big_char;
            } else if( small ) {
                return sizing::human_sized_small_char;
            } else {
                return sizing::human_sized_human_char;
            }
        }
    }
}


bool item::is_owned_by( const Character &c, bool available_to_take ) const
{
    // owner.is_null() implies faction_id( "no_faction" ) which shouldn't happen, or no owner at all.
    // either way, certain situations this means the thing is available to take.
    // in other scenarios we actually really want to check for id == id, even for no_faction
    if( get_owner().is_null() ) {
    return available_to_take;
}
if( !c.get_faction() ) {
    debugmsg( "Character %s has no faction", c.disp_name() );
        return false;
    }
    return c.get_faction()->id == get_owner();
}

bool item::is_old_owner( const Character &c, bool available_to_take ) const
{
    if( get_old_owner().is_null() ) {
    return available_to_take;
}
if( !c.get_faction() ) {
    debugmsg( "Character %s has no faction.", c.disp_name() );
        return false;
    }
    return c.get_faction()->id == get_old_owner();
}

std::string item::get_owner_name() const
{
    if( !g->faction_manager_ptr->get( get_owner() ) ) {
    debugmsg( "item::get_owner_name() item %s has no valid nor null faction id ", tname() );
        return "no owner";
    }
    return g->faction_manager_ptr->get( get_owner() )->name;
}

void item::set_owner( const Character &c )
{
    if( !c.get_faction() ) {
        debugmsg( "item::set_owner() Character %s has no valid faction", c.disp_name() );
        return;
    }
    owner = c.get_faction()->id;
}

faction_id item::get_owner() const
{
    validate_ownership();
    return owner;
}

faction_id item::get_old_owner() const
{
    validate_ownership();
    return old_owner;
}

void item::validate_ownership() const
{
    if( !old_owner.is_null() && !g->faction_manager_ptr->get( old_owner, false ) ) {
    remove_old_owner();
    }
    if( !owner.is_null() && !g->faction_manager_ptr->get( owner, false ) ) {
    remove_owner();
    }
}



std::map<gunmod_location, int> item::get_mod_locations() const
{
    std::map<gunmod_location, int> mod_locations = type->gun->valid_mod_locations;

    for( const item *mod : gunmods() ) {
        if( !mod->type->gunmod->add_mod.empty() ) {
            std::map<gunmod_location, int> add_locations = mod->type->gunmod->add_mod;

            for( const std::pair<const gunmod_location, int> &add_location : add_locations ) {
                mod_locations[add_location.first] += add_location.second;
            }
        }
    }

    return mod_locations;
}

int item::get_free_mod_locations( const gunmod_location &location ) const
{
    if( !is_gun() ) {
    return 0;
}

std::map<gunmod_location, int> mod_locations = get_mod_locations();

const auto loc = mod_locations.find( location );
if( loc == mod_locations.end() ) {
    return 0;
}
int result = loc->second;
for( const item *elem : contents.all_items_top() ) {
    const cata::value_ptr<islot_gunmod> &mod = elem->type->gunmod;
    if( mod && mod->location == location ) {
            result--;
        }
    }
    return result;
}

int item::engine_displacement() const
{
    return type->engine ? type->engine->displacement : 0;
}

const std::string &item::symbol() const
{
    return type->sym;
}

nc_color item::color_in_inventory() const
{
    return item::color_in_inventory( get_avatar() );
}

nc_color item::color_in_inventory( const player &p ) const
{
    // Only item not otherwise colored gets colored as favorite
    nc_color ret = is_favorite ? c_white : c_light_gray;
    if( type->can_use( "learn_spell" ) ) {
        const use_function *iuse = get_use( "learn_spell" );
        const learn_spell_actor *actor_ptr =
            static_cast<const learn_spell_actor *>( iuse->get_actor_ptr() );
        for( const std::string &spell_id_str : actor_ptr->spells ) {
            const spell_id sp_id( spell_id_str );
            if( p.magic->knows_spell( sp_id ) && !p.magic->get_spell( sp_id ).is_max_level() ) {
                ret = c_yellow;
            }
            if( !p.magic->knows_spell( sp_id ) && p.magic->can_learn_spell( p, sp_id ) ) {
                return c_light_blue;
            }
        }
    } else if( has_flag( flag_WET ) ) {
        ret = c_cyan;
    } else if( has_flag( flag_LITCIG ) ) {
        ret = c_red;
    } else if( is_armor() && p.has_trait( trait_WOOLALLERGY ) &&
               ( made_of( material_id( "wool" ) ) || has_own_flag( flag_wooled ) ) ) {
        ret = c_red;
    } else if( has_own_flag( flag_DIRTY ) ) {
        ret = c_brown;
    } else if( is_bionic() ) {
        if( ( !p.has_bionic( type->bionic->id ) &&
              !character_funcs::has_upgraded_bionic( p, type->bionic->id ) ) ||
            type->bionic->id->has_flag( flag_MULTIINSTALL ) ) {
            ret = p.bionic_installation_issues( type->bionic->id ).empty() ? c_green : c_red;
        } else if( !has_fault( fault_bionic_nonsterile ) ) {
            ret = c_dark_gray;
        }
    } else if( has_flag( flag_LEAK_DAM ) && has_flag( flag_RADIOACTIVE ) && damage() > 0 ) {
        ret = c_light_green;
    } else if( is_active() && !is_food() && !is_food_container() && !is_corpse() ) {
        // Active items show up as yellow
        ret = c_yellow;
    } else if( is_corpse() && ( can_revive() || corpse->zombify_into ) && !has_flag( flag_PULPED ) ) {
        // Only reviving corpses are yellow
        ret = c_yellow;
    } else if( const item *food = get_food() ) {
        const bool preserves = type->container && type->container->preserves;

        // Give color priority to allergy (allergy > inedible by freeze or other conditions)
        // TODO: refactor u.will_eat to let this section handle coloring priority without duplicating code.
        if( p.allergy_type( *food ) != morale_type( "morale_null" ) ) {
            return c_red;
        }

        // Default: permafood, drugs
        // Brown: rotten (for non-saprophages) or non-rotten (for saprophages)
        // Dark gray: inedible
        // Red: morale penalty
        // Yellow: will rot soon
        // Cyan: will rot eventually
        const ret_val<edible_rating> rating = p.will_eat( *food );
        // TODO: More colors
        switch( rating.value() ) {
            case edible_rating::edible:
            case edible_rating::too_full:
                if( preserves ) {
                    // Nothing, canned food won't rot
                } else if( food->is_going_bad() ) {
                    ret = c_yellow;
                } else if( food->goes_bad() ) {
                    ret = c_cyan;
                }
                break;
            case edible_rating::inedible:
            case edible_rating::inedible_mutation:
                ret = c_dark_gray;
                break;
            case edible_rating::allergy:
            case edible_rating::allergy_weak:
            case edible_rating::cannibalism:
                ret = c_red;
                break;
            case edible_rating::rotten:
                ret = c_brown;
                break;
            case edible_rating::nausea:
            case edible_rating::bloated:
                ret = c_pink;
                break;
            case edible_rating::no_tool:
                break;
        }
    } else if( is_gun() ) {
        // Guns are green if you are carrying ammo for them
        // ltred if you have ammo but no mags
        // Gun with integrated mag counts as both
        for( const ammotype &at : ammo_types() ) {
            // get_ammo finds uncontained ammo, find_ammo finds ammo in magazines
            bool has_ammo = !character_funcs::get_ammo_items( p, at ).empty() ||
                            !character_funcs::find_ammo_items_or_mags( p, *this, false, -1 ).empty();
            bool has_mag = magazine_integral() ||
                           !character_funcs::find_ammo_items_or_mags( p, *this, true, -1 ).empty();
            if( has_ammo && has_mag ) {
                ret = c_green;
                break;
            } else if( has_ammo || has_mag ) {
                ret = c_light_red;
                break;
            }
        }
    } else if( is_ammo() ) {
        // Likewise, ammo is green if you have guns that use it
        // ltred if you have the gun but no mags
        // Gun with integrated mag counts as both
        bool has_gun = p.has_item_with( [this]( const item & i ) {
            return i.is_gun() && i.ammo_types().contains( ammo_type() );
        } );
        bool has_mag = p.has_item_with( [this]( const item & i ) {
            return ( i.is_gun() && i.magazine_integral() && i.ammo_types().contains( ammo_type() ) ) ||
                   ( i.is_magazine() && i.ammo_types().contains( ammo_type() ) );
        } );
        if( has_gun && has_mag ) {
            ret = c_green;
        } else if( has_gun || has_mag ) {
            ret = c_light_red;
        }
    } else if( is_magazine() ) {
        // Magazines are green if you have guns and ammo for them
        // ltred if you have one but not the other
        bool has_gun = p.has_item_with( [this]( const item & it ) {
            return it.is_gun() && it.magazine_compatible().contains( typeId() );
        } );
        bool has_ammo = !character_funcs::find_ammo_items_or_mags( p, *this, false, -1 ).empty();
        if( has_gun && has_ammo ) {
            ret = c_green;
        } else if( has_gun || has_ammo ) {
            ret = c_light_red;
        }
    } else if( is_book() ) {
        const islot_book &tmp = *type->book;
        // Player doesn't actually interested if NPC has identified book yet.
        // So we check identification for human avatar.
        if( get_avatar().has_identified( typeId() ) ) {
            if( tmp.skill && // Book can improve skill: blue
                p.get_skill_level_object( tmp.skill ).can_train() &&
                p.get_skill_level( tmp.skill ) >= tmp.req &&
                p.get_skill_level( tmp.skill ) < tmp.level ) {
                ret = c_light_blue;
            } else if( type->can_use( "MA_MANUAL" ) &&
                       !p.martial_arts_data->has_martialart( martial_art_learned_from( *type ) ) ) {
                ret = c_light_blue;
            } else if( tmp.skill && // Book can't improve skill right now, but maybe later: pink
                       p.get_skill_level_object( tmp.skill ).can_train() &&
                       p.get_skill_level( tmp.skill ) < tmp.level ) {
                ret = c_pink;
            } else if( !p.studied_all_recipes(
                           *type ) ) { // Book can't improve skill anymore, but has more recipes: yellow
                ret = c_yellow;
            }
        } else if( tmp.skill || type->can_use( "MA_MANUAL" ) ) {
            // Book can teach you something and hasn't been identified yet
            ret = c_red;
        } else {
            // "just for fun" book that they haven't read yet
            ret = c_magenta;
        }
    }
    return ret;
}

void item::on_wear( Character &who )
{
    if( is_sided() && get_side() == side::BOTH ) {
        if( has_flag( flag_SPLINT ) ) {
            set_side( side::LEFT );
            if( ( covers( bodypart_id( "leg_l" ) ) && who.is_limb_broken( bodypart_id( "leg_r" ) ) &&
                  !who.worn_with_flag( flag_SPLINT, bodypart_id( "leg_r" ) ) ) ||
                ( covers( bodypart_id( "arm_l" ) ) && who.is_limb_broken( bodypart_id( "arm_r" ) ) &&
                  !who.worn_with_flag( flag_SPLINT, bodypart_id( "arm_r" ) ) ) ) {
                set_side( side::RIGHT );
            }
        } else if( has_flag( flag_POWERARMOR_MOD ) ) {
            // for power armor mods, wear on side with least mods
            std::vector< std::pair< bodypart_str_id, int > > mod_parts;
            int lhs = 0;
            int rhs = 0;
            const auto &all_bps = who.get_all_body_parts();
            for( const bodypart_id &bp : all_bps ) {
                if( get_covered_body_parts().test( bp.id() ) ) {
                    mod_parts.emplace_back( bp, 0 );
                }
            }
            for( auto &elem : who.worn ) {
                for( std::pair< bodypart_str_id, int > &mod_part : mod_parts ) {
                    const bodypart_str_id &bp = mod_part.first;
                    if( elem->get_covered_body_parts().test( bp ) &&
                        elem->has_flag( flag_POWERARMOR_MOD ) ) {
                        if( elem->is_sided() && elem->get_side() == bp->part_side ) {
                            mod_part.second++;
                            continue;
                        }
                        mod_part.second++;
                    }
                }
            }
            for( std::pair< bodypart_str_id, int > &mod_part : mod_parts ) {
                const bodypart_str_id &bp = mod_part.first;
                if( bp->part_side == side::LEFT && mod_part.second > lhs ) {
                    lhs = mod_part.second;
                } else if( bp->part_side == side::RIGHT && mod_part.second > rhs ) {
                    rhs = mod_part.second;
                }
            }
            set_side( ( lhs > rhs ) ? side::RIGHT : side::LEFT );
        } else {
            // for sided items wear the item on the side which results in least encumbrance
            const auto &all_bps = who.get_all_body_parts();
            int lhs = 0;
            int rhs = 0;
            set_side( side::LEFT );
            const char_encumbrance_data left_enc = who.get_encumbrance();
            for( const bodypart_id &bp : all_bps ) {
                if( get_covered_body_parts().test( bp.id() ) ) {
                    lhs += left_enc.elems.at( bp.id() ).encumbrance;
                }
            }

            set_side( side::RIGHT );
            const char_encumbrance_data right_enc = who.get_encumbrance();
            for( const bodypart_id &bp : all_bps ) {
                if( get_covered_body_parts().test( bp.id() ) ) {
                    rhs += right_enc.elems.at( bp.id() ).encumbrance;
                }
            }

            set_side( lhs <= rhs ? side::LEFT : side::RIGHT );
        }
    }

    if( type->can_use( "set_transformed" ) ) {
        bool transform = false;
        const set_transformed_iuse *actor = dynamic_cast<const set_transformed_iuse *>
                                            ( this->get_use( "set_transformed" )->get_actor_ptr() );
        if( actor == nullptr ) {
            debugmsg( "iuse_actor type descriptor and actual type mismatch" );
            return;
        }
        flag_id transform_flag( actor->dependencies );
        for( const auto &elem : who.worn ) {
            if( elem->has_flag( transform_flag ) && elem->is_active() != is_active() ) {
                transform = true;
            }
        }
        if( transform && actor->restricted ) {
            actor->bypass( *who.as_player(), *this, false, who.bub_pos() );
        }
    }

    // TODO: artifacts currently only work with the player character
    if( &who == &get_avatar() && type->artifact ) {
        g->add_artifact_messages( type->artifact->effects_worn );
    }
    // if game is loaded - don't want ownership assigned during char creation
    if( get_avatar().getID().is_valid() ) {
        handle_pickup_ownership( who );
    }
    who.on_item_wear( *this );

    if( type->iwearable_callbacks ) {
        type->iwearable_callbacks->call_on_wear( who, *this );
    }
}

void item::on_takeoff( Character &who )
{
    who.on_item_takeoff( *this );

    if( is_sided() ) {
        set_side( side::BOTH );
    }

    // if power armor, no power_draw and active, shut down.
    if( type->can_use( "set_transformed" ) && is_active() ) {
        const set_transformed_iuse *actor = dynamic_cast<const set_transformed_iuse *>
                                            ( this->get_use( "set_transformed" )->get_actor_ptr() );
        if( actor == nullptr ) {
            debugmsg( "iuse_actor type descriptor and actual type mismatch" );
            return;
        }
        actor->bypass( *who.as_player(), *this, false, who.bub_pos() );
    }

    if( type->iwearable_callbacks ) {
        type->iwearable_callbacks->call_on_takeoff( who, *this );
    }
}

void item::on_wield( player &p, int mv )
{
    // TODO: artifacts currently only work with the player character
    if( &p == &get_avatar() && type->artifact ) {
        g->add_artifact_messages( type->artifact->effects_wielded );
    }

    // weapons with bayonet/bipod or other generic "unhandiness"
    if( has_flag( flag_SLOW_WIELD ) && !is_gunmod() ) {
        float d = 32.0; // arbitrary linear scaling factor
        if( is_gun() ) {
            d /= std::max( p.get_skill_level( gun_skill() ), 1 );
        } else if( is_melee() ) {
            d /= std::max( p.get_skill_level( melee_skill() ), 1 );
        }

        int penalty = get_var( "volume", volume() / units::legacy_volume_factor ) * d;
        p.moves -= penalty;
        mv += penalty;
    }

    // firearms with a folding stock or tool/melee without collapse/retract iuse
    if( has_flag( flag_NEEDS_UNFOLD ) && !is_gunmod() ) {
        int penalty = 50; // 200-300 for guns, 50-150 for melee, 50 as fallback
        if( is_gun() ) {
            penalty = std::max( 0, 300 - p.get_skill_level( gun_skill() ) * 10 );
        } else if( is_melee() ) {
            penalty = std::max( 0, 150 - p.get_skill_level( melee_skill() ) * 10 );
        }

        p.moves -= penalty;
        mv += penalty;
    }

    std::string msg;

    if( mv > 500 ) {
        msg = _( "It takes you a long time to wield your %s." );
    } else if( mv > 250 ) {
        msg = _( "It takes you several seconds to wield your %s." );
    } else if( mv > 100 ) {
        msg = _( "It takes you a couple seconds to wield your %s." );
    } else if( mv > 50 ) {
        msg = _( "It takes you a moment to wield your %s." );
    } else {
        msg = _( "You wield your %s." );
    }
    // if game is loaded - don't want ownership assigned during char creation
    if( p.getID().is_valid() ) {
        handle_pickup_ownership( p );
    }
    p.add_msg_if_player( m_neutral, msg, tname() );

    if( !p.martial_arts_data->selected_is_none() ) {
        p.martial_arts_data->martialart_use_message( p );
    }

    // Update encumbrance in case we were wearing it
    p.flag_encumbrance();

    if( type->iwieldable_callbacks ) {
        type->iwieldable_callbacks->call_on_wield( p, *this, mv );
    }
}

void item::on_unwield( Character &who )
{
    if( type->iwieldable_callbacks ) {
        type->iwieldable_callbacks->call_on_unwield( who, *this );
    }
}

void item::handle_pickup_ownership( Character &c )
{
    if( is_owned_by( c ) ) {
        return;
    }
    // Add ownership to item if unowned
    if( owner.is_null() ) {
        set_owner( c );
    } else {
        Character &you = get_player_character();
        if( !is_owned_by( c ) && &c == &you ) {
            std::vector<npc *> witnesses;
            for( npc &elem : g->all_npcs() ) {
                // If they already want to murder you, no point in confronting you about theft
                if( rl_dist( elem.bub_pos(), you.bub_pos() ) < g_max_view_distance && elem.get_faction() &&
                    is_owned_by( elem ) && elem.sees( you.bub_pos() ) && !elem.guaranteed_hostile() ) {
                    elem.say( "<witnessed_thievery>", 7 );
                    npc *npc_to_add = &elem;
                    witnesses.push_back( npc_to_add );
                }
            }
            if( !witnesses.empty() ) {
                set_old_owner( get_owner() );
                // Make sure there is only one witness
                for( npc &guy : g->all_npcs() ) {
                    if( guy.get_attitude() == NPCATT_RECOVER_GOODS ) {
                        guy.set_attitude( NPCATT_NULL );
                    }
                }
                random_entry( witnesses )->set_attitude( NPCATT_RECOVER_GOODS );
                // Notify the activity that we got a witness
                if( c.activity && !c.activity->is_null() && c.activity->id() == ACT_PICKUP ) {
                    c.activity->str_values.clear();
                    c.activity->str_values.emplace_back( has_thievery_witness );
                }
            }
            set_owner( c );
        }
    }
}

void item::on_pickup( Character &who )
{
    // Fake characters are used to determine pickup weight and volume
    if( who.is_fake() ) {
        return;
    }
    avatar &you = get_avatar();
    // TODO: artifacts currently only work with the player character
    if( &who == &you && type->artifact ) {
        g->add_artifact_messages( type->artifact->effects_carried );
    }
    // if game is loaded - don't want ownership assigned during char creation
    if( you.getID().is_valid() ) {
        handle_pickup_ownership( who );
    }
    if( is_bucket_nonempty() ) {
        contents.spill_contents( who.bub_pos() );
    }

    who.flag_encumbrance();

    if( type->istate_callbacks ) {
        type->istate_callbacks->call_on_pickup( who, *this );
    }
}

void item::on_contents_changed()
{
    if( is_non_resealable_container() ) {
        convert( type->container->unseals_into );
    }

    encumbrance_update_ = true;
}

void item::on_damage( int qty, damage_type )
{
    if( is_corpse() && qty + damage_ >= max_damage() ) {
        set_flag( flag_PULPED );
    }

    if( type->iequippable_callbacks ) {
        type->iequippable_callbacks->call_on_durability_change(
            get_avatar(), *this, damage_, damage_ + qty );
    }
}

void item::on_map_placement( const map &m, const tripoint_bub_ms &p )
{

    // TODO: Move to reveal_map_actor
    if( is_map() && !has_var( "reveal_map_center_omt" ) ) {
        set_var( "reveal_map_center_omt", project_to<coords::omt>( m.bub_to_abs( p ) ) );
    }

    for( const auto &func : type->use_methods | std::views::values ) {
        const auto actor = func.get_actor_ptr();
        if( actor != nullptr ) {
            actor->on_placed( *this, m, p );
        }
    }
}

std::string item::tname( unsigned int quantity, bool with_prefix, unsigned int truncate ) const
{
    int dirt_level = get_var( "dirt", 0 ) / 2000;
    std::string dirt_symbol;
    // TODO: MATERIALS put this in json

    // these symbols are unicode square characeters of different heights, representing a rough
    // estimation of fouling in a gun. This appears instead of "faulty"
    // since most guns will have some level of fouling in them, and usually it is not a big deal.
    switch( dirt_level ) {
        case 0:
            dirt_symbol = "";
            break;
        case 1:
            dirt_symbol = "<color_white>\u2581</color>";
            break;
        case 2:
            dirt_symbol = "<color_light_gray>\u2583</color>";
            break;
        case 3:
            dirt_symbol = "<color_light_gray>\u2585</color>";
            break;
        case 4:
            dirt_symbol = "<color_dark_gray>\u2587</color>";
            break;
        case 5:
            dirt_symbol = "<color_brown>\u2588</color>";
            break;
        default:
            dirt_symbol = "";
    }
    std::string damtext;

    // for portions of string that have <color_ etc in them, this aims to truncate the whole string correctly
    unsigned int truncate_override = 0;

    if( ( damage() != 0 || ( get_option<bool>( "ITEM_HEALTH_BAR" ) && is_armor() ) ) && !is_null() &&
        with_prefix ) {
        damtext = durability_indicator();
        if( get_option<bool>( "ITEM_HEALTH_BAR" ) ) {
            // get the utf8 width of the tags
            truncate_override = utf8_width( damtext, false ) - utf8_width( damtext, true );
        }
    }

    if( !faults.empty() ) {
        const bool silent = std::ranges::any_of( faults, []( const fault_id & f ) -> bool { return f->has_flag( "SILENT" ); } );
        if( silent ) {
            damtext.insert( 0, dirt_symbol );
        } else {
            damtext.insert( 0, _( "faulty " ) + dirt_symbol );
        }
    }

    std::string vehtext;
    if( is_engine() && engine_displacement() > 0 ) {
        vehtext = string_format( pgettext( "vehicle adjective", "%2.1fL " ),
                                 engine_displacement() / 100.0f );

    } else if( is_wheel() && type->wheel->diameter > 0 ) {
        vehtext = string_format( pgettext( "vehicle adjective", "%s " ),
                                 wheel_dimensions::format_for_display( type->wheel->diameter ).c_str() );
    }

    std::string burntext;
    if( with_prefix && !made_of( LIQUID ) ) {
        if( volume() >= 1_liter && burnt * 125_ml >= volume() ) {
            burntext = pgettext( "burnt adjective", "badly burnt " );
        } else if( burnt > 0 ) {
            burntext = pgettext( "burnt adjective", "burnt " );
        }
    }

    std::string maintext;
    if( is_corpse() || item_vars_.contains( "name" ) ) {
        maintext = type_name( quantity );
    } else if( is_craft() ) {
        maintext = string_format( _( "in progress %s" ), craft_data_->making->result_name() );
        if( charges > 1 ) {
            maintext += string_format( " (%d)", charges );
        }
        const int percent_progress = item_counter / 100000;
        maintext += string_format( " (%d%%)", percent_progress );
    } else {
        std::string labeltext = label( quantity );

        int modamt = 0;
        if( is_tool() ) {
            modamt += toolmods().size();
        }
        if( is_gun() ) {
            for( const item *mod : gunmods() ) {
                if( !type->gun->built_in_mods.contains( mod->typeId() ) ) {
                    modamt++;
                }
            }
        }
        if( is_armor() && has_clothing_mod() ) {
            modamt++;
        }
        if( modamt ) {
            labeltext += string_format( "+%d", modamt );
        }

        if( is_gun() || is_tool() || is_magazine() ) {
            maintext = labeltext;
        } else if( contents.num_item_stacks() == 1 ) {
            const item &contents_item = contents.front();
            const unsigned contents_count =
                ( ( contents_item.made_of( LIQUID ) || contents_item.is_food() ) &&
                  contents_item.charges > 1 )
                ? contents_item.charges
                : quantity;
            maintext = string_format( pgettext( "item name", "%2$s (%1$s)" ), labeltext,
                                      contents_item.tname( contents_count, with_prefix ) );
        } else if( !contents.empty() ) {
            maintext = string_format( vpgettext( "item name",
                                                 //~ %1$s: item name, %2$zd: content size
                                                 "%1$s with %2$zd item",
                                                 "%1$s with %2$zd items", contents.num_item_stacks() ),
                                      labeltext, contents.num_item_stacks() );
        } else {
            maintext = labeltext;
        }
    }

    avatar &you = get_avatar();
    std::string tagtext;
    if( is_food() ) {
        if( has_flag( flag_HIDDEN_POISON ) && you.get_skill_level( skill_survival ) >= 3 ) {
            tagtext += _( " (poisonous)" );
        } else if( has_flag( flag_HIDDEN_HALLU ) && you.get_skill_level( skill_survival ) >= 5 ) {
            tagtext += _( " (hallucinogenic)" );
        }
    }
    if( is_book() ) {
        if( !you.has_identified( typeId() ) ) {
            tagtext += _( " (unread)" );
        }
    }
    if( has_var( "bionics_scanned_by" ) ) {
        if( has_flag( flag_CBM_SCANNED ) ) {
            tagtext += _( " (bionic detected)" );
        } else {
            tagtext += _( " (scanned)" );
        }
    }
    if( has_flag( flag_ETHEREAL_ITEM ) ) {
        tagtext += string_format( _( " (%s turns)" ), get_var( "ethereal" ) );
    } else if( goes_bad() || is_food() ) {
        if( has_own_flag( flag_DIRTY ) ) {
            tagtext += _( " (dirty)" );
        } else if( rotten() ) {
            tagtext += _( " (rotten)" );
        } else if( is_going_bad() ) {
            tagtext += _( " (old)" );
        } else if( is_fresh() ) {
            tagtext += _( " (fresh)" );
        }
        if( is_loaded() ) {
            const auto temp = rot::temperature_flag_for_location( get_map(), *this );
            if( temp == temperature_flag::TEMP_FREEZER ) {
                tagtext += _( " (frozen)" );
            } else if( temp == temperature_flag::TEMP_FRIDGE || temp == temperature_flag::TEMP_ROOT_CELLAR ) {
                tagtext += _( " (cold)" );
            }
        }
    }

    const sizing sizing_level = get_sizing( you );

    if( sizing_level == sizing::human_sized_small_char ) {
        tagtext += _( " (too big)" );
    } else if( sizing_level == sizing::big_sized_small_char ) {
        tagtext += _( " (huge!)" );
    } else if( sizing_level == sizing::human_sized_big_char ||
               sizing_level == sizing::small_sized_human_char ) {
        tagtext += _( " (too small)" );
    } else if( sizing_level == sizing::small_sized_big_char ) {
        tagtext += _( " (tiny!)" );
    } else if( !has_flag( flag_FIT ) && has_flag( flag_VARSIZE ) ) {
        tagtext += _( " (poor fit)" );
    }

    if( is_bionic() && !has_fault( fault_bionic_nonsterile ) ) {
        tagtext += _( " (sterile)" );
    }

    if( is_tool() && has_flag( flag_USE_UPS ) && !has_flag( flag_NAT_UPS ) ) {
        tagtext += _( " (UPS)" );
    }
    if( is_tool() && has_flag( flag_HEATS_FOOD ) ) {
        tagtext += _( " (heats)" );
    }


    if( has_var( "specimen_sample" ) ) {
        const std::string specimen_name = get_var( "specimen_name" );
        const int progress = get_var( "specimen_sample_progress", 0 );
        const auto specimen_id = mtype_id( get_var( "specimen_sample" ) );
        const auto size = cloning_utils::specimen_required_sample_size( specimen_id );
        if( has_flag( flag_genome_drive ) && size > 0 && progress < size ) {
            tagtext += string_format( " (%s [%d/%d])", specimen_name, progress, size );
        } else {
            tagtext += string_format( " (%s)", specimen_name );
        }
    }

    if( has_var( "place_monster_override" ) ) {
        tagtext += string_format( " (%s)", get_var( "place_monster_override_name" ) );
    }

    if( has_var( "NANOFAB_GROUP_ID" ) ) {
        std::string group_id_str = get_var( "NANOFAB_GROUP_ID" );
        const std::string prefix = "nanofab_template_";

        // Remove prefix if it exists
        if( group_id_str.rfind( prefix, 0 ) == 0 ) {
            group_id_str = group_id_str.substr( prefix.size() );
        }

        // Replace underscores with spaces
        std::replace( group_id_str.begin(), group_id_str.end(), '_', ' ' );

        // Append to tag text
        tagtext += string_format( " (%s)", group_id_str );
    } else if( has_var( "NANOFAB_ITEM_ID" ) ) {
        itype_id item = itype_id( get_var( "NANOFAB_ITEM_ID" ) );
        tagtext += string_format( " (%s [%d])", nname( item ), std::max( 1, item->volume / 250_ml ) * 5 );
    }

    if( already_used_by_player( you ) ) {
        tagtext += _( " (used)" );
    }
    if( has_flag( flag_IS_UPS ) && get_var( "cable" ) == "plugged_in" ) {
        tagtext += _( " (plugged in)" );
    }

    std::string modtext;
    if( gunmod_find( itype_barrel_small ) ) {
        modtext += _( "sawn-off " );
    }
    if( gunmod_find( itype_stock_small ) ) {
        modtext += _( "pistol " );
    }
    if( has_flag( flag_DIAMOND ) ) {
        modtext += std::string( pgettext( "Adjective, as in diamond katana", "diamond" ) ) + " ";
    }

    // Collects all flags from the item and its type, then appends their display tags to the item
    // name. This is used to display tags from both the item and its type, such as (wet) or (XL).
    namespace ranges = std::ranges;
    const auto display_tags = []( const auto & flags ) {
        using namespace std::views;
        const auto get_tag = transform( []( const flag_id & f ) -> const translation & { return f->tag(); } );
        const auto has_tag = filter( []( const translation & tag ) { return !tag.empty(); } );
        const auto translate_tag = transform( []( const translation & tag ) { return tag.translated(); } );
        return flags | get_tag | has_tag | translate_tag;
    };
    std::vector<std::string> flag_tags;
    ranges::copy( display_tags( get_flags() ), std::back_inserter( flag_tags ) );
    ranges::copy( display_tags( type->item_tags ), std::back_inserter( flag_tags ) );
    if( !flag_tags.empty() ) {
        tagtext += " " + enumerate_as_string( flag_tags, enumeration_conjunction::space );
    }

    if( is_favorite ) {
        tagtext += _( " *" ); // Display asterisk for favorite items
    }

    //~ This is a string to construct the item name as it is displayed. This format string has been added for maximum flexibility. The strings are: %1$s: Damage text (e.g. "bruised"). %2$s: burn adjectives (e.g. "burnt"). %3$s: tool modifier text (e.g. "atomic"). %4$s: vehicle part text (e.g. "3.8-Liter"). $5$s: main item text (e.g. "apple"). %6s: tags (e.g. "(wet) (poor fit)").
    std::string ret = string_format( _( "%1$s%2$s%3$s%4$s%5$s%6$s" ), damtext, burntext, modtext,
                                     vehtext, maintext, tagtext );

    if( truncate != 0 ) {
        ret = utf8_truncate( ret, truncate + truncate_override );
    }

    if( item_vars_.contains( "item_note" ) ) {
        //~ %s is an item name. This style is used to denote items with notes.
        return string_format( _( "*%s*" ), ret );
    } else {
        return ret;
    }
}

std::string item::display_money( unsigned int quantity, unsigned int total,
                                 const std::optional<unsigned int> &selected ) const
{
    if( selected ) {
    //~ This is a string to display the selected and total amount of money in a stack of cash cards.
    //~ %1$s is the display name of cash cards.
    //~ %2$s is the total amount of money.
    //~ %3$s is the selected amount of money.
    //~ Example: "cash cards $15.35 of $20.48"
    return string_format( pgettext( "cash card and money", "%1$s %3$s of %2$s" ), tname( quantity ),
                          format_money( total ), format_money( *selected ) );
    } else {
        //~ This is a string to display the total amount of money in a stack of cash cards.
        //~ %1$s is the display name of cash cards.
        //~ %2$s is the total amount of money on the cash cards.
        //~ Example: "cash cards $20.48"
        return string_format( pgettext( "cash card and money", "%1$s %2$s" ), tname( quantity ),
                              format_money( total ) );
    }
}

std::string item::display_name( unsigned int quantity ) const
{
    std::string name = tname( quantity );
    std::string sidetxt;
    std::string amt;

    switch( get_side() ) {
        case side::BOTH:
        case side::num_sides:
            break;
        case side::LEFT:
            sidetxt = string_format( " (%s)", _( "left" ) );
            break;
        case side::RIGHT:
            sidetxt = string_format( " (%s)", _( "right" ) );
            break;
    }
    avatar &you = get_avatar();
    static const itype_id itype_battery( "battery" );
    int amount = 0;
    int max_amount = 0;
    bool has_item = is_container() && contents.num_item_stacks() == 1;
    bool has_ammo = is_ammo_container() && contents.num_item_stacks() == 1;
    bool contains = has_item || has_ammo;
    bool show_amt = false;
    // We should handle infinite charges properly in all cases.
    if( contains ) {
        amount = contents.front().charges;
        max_amount = contents.front().charges_per_volume( get_container_capacity() );
    } else if( is_book() && get_chapters() > 0 ) {
        // a book which has remaining unread chapters
        amount = get_remaining_chapters( you );
    } else if( ammo_capacity() > 0 ) {
        // anything that can be reloaded including tools, magazines, guns and auxiliary gunmods
        // but excluding bows etc., which have ammo, but can't be reloaded
        amount = ammo_remaining();
        max_amount = ammo_capacity();
        show_amt = !has_flag( flag_RELOAD_AND_SHOOT );
    } else if( count_by_charges() && !has_infinite_charges() ) {
        // A chargeable item
        amount = charges;
        max_amount = ammo_capacity();
    } else if( is_battery() ) {
        show_amt = true;
        amount = to_joule( energy_remaining() );
        max_amount = to_joule( type->battery->max_capacity );
    }

    std::string ammotext;
    if( ( ( is_gun() && ammo_required() ) || is_magazine() ) && get_option<bool>( "AMMO_IN_NAMES" ) ) {
        if( !ammo_current().is_null() ) {
            ammotext = ammo_current()->nname( 1 );
        } else {
            ammotext = ammotype( *ammo_types().begin() )->name();
        }
    }

    if( amount || show_amt ) {
        if( is_money() ) {
            amt = string_format( " $%.2f", amount / 100.0 );
        } else {
            if( !ammotext.empty() ) {
                ammotext = " " + ammotext;
            }

            if( max_amount != 0 ) {
                if( !contains ) {
                    int percentage = ( static_cast<float>( amount ) / max_amount ) * 100;
                    nc_color color = c_green;
                    if( percentage >= 100 ) {
                        color = c_green;
                    } else if( percentage >= 75 ) {
                        color = c_light_green;
                    } else if( percentage >= 50 ) {
                        color = c_yellow;
                    } else if( percentage >= 25 ) {
                        color = c_light_red;
                    } else if( percentage > 0 ) {
                        color = c_red;
                    } else if( percentage <= 0 ) {
                        color = c_light_gray;
                    }
                    amt = colorize( string_format( " (%i/%i%s)", amount, max_amount, ammotext ),
                                    color );
                } else {
                    amt = string_format( " (%i/%i%s)", amount, max_amount, ammotext );
                }

            } else {
                amt = string_format( " (%i%s)", amount, ammotext );
            }
        }
    } else if( !ammotext.empty() ) {
        amt = " (" + ammotext + ")";
    }

    // HACK: This is a hack to prevent possible crashing when displaying maps as items during character creation
    if( is_map() && calendar::turn != calendar::turn_zero ) {
        const auto map_pos_omt = get_var( "reveal_map_center_omt", you.abs_omt_pos() );
        const auto map_pos = project_to<coords::sm>( map_pos_omt );
        const city *c = get_overmapbuffer( you.get_dimension() ).closest_city( map_pos ).city;
        if( c != nullptr ) {
            name = string_format( "%s %s", c->name, name );
        }
    }

    return string_format( "%s%s%s", name, sidetxt, amt );
}

std::string item::debug_name() const
{
    return typeId().str();
}

nc_color item::color() const
{
    if( is_null() ) {
    return c_black;
}
if( is_corpse() ) {
    return corpse->color;
}
return type->color;
}

auto item::price( bool practical ) const -> float
{
    float res = 0;

    visit_items( [&res, practical]( const item * e ) {
        if( e->rotten() ) {
            // TODO: Special case things that stay useful when rotten
            return VisitResponse::NEXT;
        }

        float child = units::to_cent( practical ? e->type->price_post : e->type->price );
        if( e->damage() > 0 ) {
            // maximal damage level is 4, maximal reduction is 40% of the value.
            child -= child * static_cast<double>( e->damage_level( 4 ) ) / 10;
        }

        if( e->count_by_charges() || e->made_of( LIQUID ) ) {
            // price from json data is for default-sized stack
            child *= e->charges / static_cast<double>( e->type->stack_size );

        } else if( e->magazine_integral() && e->ammo_remaining() && e->ammo_data() ) {
            // items with integral magazines may contain ammunition which can affect the price
            child += item( e->ammo_data(), calendar::turn, e->charges ).price( practical );

        } else if( e->is_tool() && e->ammo_types().empty() && e->ammo_capacity() ) {
            // if tool has no ammo (e.g. spray can) reduce price proportional to remaining charges
            child *= e->ammo_remaining() / static_cast<double>( std::max( e->type->charges_default(), 1 ) );
        }

        res += child;
        return VisitResponse::NEXT;
    } );

    return res;
}

// TODO: MATERIALS add a density field to materials.json
units::mass item::weight( bool include_contents, bool integral ) const
{
    if( is_null() ) {
    return 0_gram;
}

// Items that don't drop aren't really there, they're items just for ease of implementation
if( has_flag( flag_NO_DROP ) ) {
    return 0_gram;
}

if( is_craft() ) {
    units::mass ret = 0_gram;
    for( const item * const &it : components ) {
            ret += it->weight();
        }
        return ret;
    }

    units::mass ret;
    std::string local_str_mass = integral ? get_var( "integral_weight" ) : get_var( "weight" );
    if( local_str_mass.empty() ) {
    ret = integral ? type->integral_weight : type->weight;
} else {
    ret = units::from_milligram( std::stoll( local_str_mass ) );
    }

    if( has_flag( flag_REDUCED_WEIGHT ) ) {
    ret *= 0.75;
}

// if this is a gun apply all of its gunmods' weight multipliers
if( is_gun() ) {
    for( const item *mod : gunmods() ) {
            ret *= mod->type->gunmod->weight_multiplier;
        }
    }

    if( count_by_charges() ) {
    ret *= charges;

} else if( is_corpse() ) {
    assert( corpse ); // To appease static analysis
        ret = corpse->weight;
        if( has_flag( flag_FIELD_DRESS ) || has_flag( flag_FIELD_DRESS_FAILED ) ) {
            ret *= 0.75;
        }
        if( has_flag( flag_QUARTERED ) ) {
            ret /= 4;
        }
        if( has_flag( flag_GIBBED ) ) {
            ret *= 0.85;
        }
        if( has_flag( flag_SKINNED ) ) {
            ret *= 0.85;
        }

    } else if( magazine_integral() && !is_magazine() ) {
    if( ammo_current() == itype_plut_cell ) {
            units::mass w = ( *ammo_types().begin() )->default_ammotype()->weight;
            ret += ammo_remaining() * w / PLUTONIUM_CHARGES;
        } else if( ammo_data() ) {
            ret += ammo_remaining() * ammo_data()->weight;
        }
    }

    // if this is an ammo belt add the weight of any implicitly contained linkages
    if( is_magazine() ) {
    const auto &linkage = type->magazine->linkage;
    if( linkage ) {
            item links( *linkage );
            links.charges = ammo_remaining();
            ret += links.weight();
        }
    }

    // reduce weight for sawn-off weapons capped to the apportioned weight of the barrel
    if( gunmod_find( itype_barrel_small ) ) {
    const units::volume b = type->gun->barrel_volume;
    const units::mass max_barrel_weight = units::from_gram( to_milliliter( b ) );
        const units::mass barrel_weight = units::from_gram( b.value() * type->weight.value() /
                                          type->volume.value() );
        ret -= std::min( max_barrel_weight, barrel_weight );
    }

    if( is_gun() ) {
    for( const item *elem : gunmods() ) {
            ret += elem->weight( true, true );
        }
        if( !magazine_integral() && magazine_current() ) {
            ret += std::max( magazine_current()->weight(), 0_gram );
        }
    } else if( include_contents ) {
    ret += contents.item_weight_modifier();
    }

    return ret;
}

units::volume item::corpse_volume( const mtype *corpse ) const
{
    units::volume corpse_volume = corpse->volume;
    if( has_flag( flag_QUARTERED ) ) {
        corpse_volume /= 4;
    }
    if( has_flag( flag_FIELD_DRESS ) || has_flag( flag_FIELD_DRESS_FAILED ) ) {
        corpse_volume *= 0.75;
    }
    if( has_flag( flag_GIBBED ) ) {
        corpse_volume *= 0.85;
    }
    if( has_flag( flag_SKINNED ) ) {
        corpse_volume *= 0.85;
    }
    if( corpse_volume > 0_ml ) {
        return corpse_volume;
    }
    debugmsg( "invalid monster volume for corpse" );
    return 0_ml;
}

units::volume item::base_volume() const
{
    if( is_null() ) {
    return 0_ml;
}
if( is_corpse() ) {
    return corpse_volume( corpse );
    }

    if( is_craft() ) {
    units::volume ret = 0_ml;
    for( const item * const &it : components ) {
            ret += it->base_volume();
        }
        return ret;
    }

    if( count_by_charges() ) {
    if( type->volume % type->stack_size == 0_ml ) {
            return type->volume / type->stack_size;
        } else {
            return type->volume / type->stack_size + 1_ml;
        }
    }

    return type->volume;
}

units::volume item::volume( bool integral ) const
{
    if( is_null() ) {
    return 0_ml;
}

if( is_corpse() ) {
    return corpse_volume( corpse );
    }

    if( is_craft() ) {
    units::volume ret = 0_ml;
    for( const item * const &it : components ) {
            ret += it->volume();
        }
        return ret;
    }

    const int local_volume = get_var( "volume", -1 );
    units::volume ret;
    if( local_volume >= 0 ) {
    ret = local_volume * units::legacy_volume_factor;
} else if( integral ) {
    ret = type->integral_volume;
} else {
    ret = type->volume;
}

if( count_by_charges() || made_of( LIQUID ) ) {
    units::quantity<int64_t, units::volume_in_milliliter_tag> num = ret * static_cast<int64_t>
        ( charges );
        if( type->stack_size <= 0 ) {
            debugmsg( "Item type %s has invalid stack_size %d", typeId().str(), type->stack_size );
            ret = num;
        } else {
            ret = num / type->stack_size;
            if( num % type->stack_size != 0_ml ) {
                ret += 1_ml;
            }
        }
    }

    // Non-rigid items add the volume of the content
    if( !type->rigid ) {
    // Disintegrating belts should exactly match contents volume, don't enforce the 1_ml minimum
    if( type->has_flag( flag_MAG_BELT ) && type->has_flag( flag_MAG_DESTROY ) ) {
            ret = 0_ml;
        }
        ret += contents.item_size_modifier();
    }

    // Some magazines sit (partly) flush with the item so add less extra volume
    if( magazine_current() != nullptr ) {
    ret += std::max( magazine_current()->volume() - type->magazine_well, 0_ml );
    }

    if( is_gun() ) {
    for( const item *elem : gunmods() ) {
            ret += elem->volume( true );
        }

        // TODO: implement stock_length property for guns
        if( has_flag( flag_COLLAPSIBLE_STOCK ) ) {
            // consider only the base size of the gun (without mods)
            ret -= ( type->volume / 3 );
        }

        if( gunmod_find( itype_barrel_small ) ) {
            ret -= type->gun->barrel_volume;
        }
    }

    return ret;
}

int item::lift_strength() const
{
    const int mass = units::to_gram( weight() );
    return std::max( mass / 10000, 1 );
}


void item::unset_flags()
{
    item_tags.clear();
}

bool item::has_fault( const fault_id &fault ) const
{
    return faults.contains( fault );
}

bool item::has_own_flag( const flag_id &f ) const
{
    return item_tags.count( f );
}

bool item::has_flag( const flag_id &f ) const
{
    // Check if we have any gun/toolmods with the flag, and if we do
    // check if that flag should be inherited.
    // `json_flag::get` is pretty expensive so it's faster to do it
    // last as frequently there are no gun/toolmods with the flag f
    auto mods = is_gun() ? gunmods() : toolmods();

    const auto flag_in_mods = [&f]( const auto & mods ) -> bool {
        return std::any_of( mods.begin(), mods.end(), [&f]( const item * e )-> bool {
            return ( !e->is_gun() && e->has_flag( f ) );
        } );
    };

    if( f->inherit() && flag_in_mods( mods ) ) {
        return true;
    }

    // other item type flags
    if( type->has_flag( f ) ) {
        return true;
    }

    // now check for item specific flags
    return has_own_flag( f );
}

bool item::has_vitamin( const vitamin_id &v ) const
{
    if( !this->is_comestible() ) {
    return false;
}
// We need this function to get all vitamins including from inheritance.
// But we don't care about calories, so we can just pass a dummy.
npc dummy;
const nutrients food_item = dummy.compute_effective_nutrients( *this );
for( auto const& [vit_id, amount] : food_item.vitamins ) {
    if( vit_id == v ) {
            if( amount > 0 ) {
                return true;
            } else {
                break;
            }
        }
    }
    return false;
}

void item::set_flag( const flag_id &flag )
{
    if( flag.is_valid() ) {
        item_tags.insert( flag );
    } else {
        debugmsg( "Attempted to set invalid flag_id %s", flag.str() );
    }
}

void item::unset_flag( const flag_id &flag )
{
    item_tags.erase( flag );
}

void item::set_flag_recursive( const flag_id &flag )
{
    set_flag( flag );
    for( item * const &comp : components ) {
        comp->set_flag_recursive( flag );
    }
}

const item::FlagsSetType &item::get_flags() const
{
    return item_tags;
}

bool item::has_property( const std::string &prop ) const
{
    return type->properties.contains( prop );
}

std::string item::get_property_string( const std::string &prop, const std::string &def ) const
{
    const auto it = type->properties.find( prop );
    return it != type->properties.end() ? it->second : def;
}

int64_t item::get_property_int64_t( const std::string &prop, int64_t def ) const
{
    const auto it = type->properties.find( prop );
    if( it != type->properties.end() ) {
        char *e = nullptr;
        int64_t r = std::strtoll( it->second.c_str(), &e, 10 );
        if( !it->second.empty() && *e == '\0' ) {
            return r;
        }
        debugmsg( "invalid property '%s' for item '%s'", prop.c_str(), tname() );
    }
    return def;
}

int item::get_quality( const quality_id &id ) const
{
    int return_quality = INT_MIN;

    /**
     * EXCEPTION: Items with quality BOIL only count as such if they are empty,
     * excluding items of their ammo type if they are tools.
     */
    auto block_boil_filter = [this]( const item & itm ) {
        // We want to skip (do not block) only those : correct ammo, correct magazine, correct toolmod.Everything else should block.
        if( &itm == this ) {
            // Do not block if checking itself - we are checking only item contents not item itself.
            return false;
        } else if( itm.is_ammo() ) {
            return !ammo_types().contains( itm.ammo_type() );
        } else if( itm.is_magazine() ) {
            // we want to return "fine for boiling" if any of the ammo types match and "blocks boiling" if none match.
            for( const ammotype &at : ammo_types() ) {
                for( const ammotype &mag_at : itm.ammo_types() ) {
                    if( at == mag_at ) {
                        return false;
                    }
                }
            }
            return true;
        } else if( itm.is_toolmod() ) {
            return false;
        }
        return true;
    };
    // if it's has boil quality and it's empty, it's good to boil. If it's not empty and it's not a tool (it's probably a container), it's not good to boil. If it's a tool, it gets an extra chance: if it's only contents are mods or batteries, it's still good.
    // Also  we are using inverted filter, since we don't care about items that the filter likes, we only care if it find something it doesn't like.
    if( id == quality_id( "BOIL" ) && !contents.empty() &&
        ( !is_tool() || has_item_with( block_boil_filter ) ) ) {
        return INT_MIN;
    }

    for( const std::pair<const quality_id, int> &quality : type->qualities ) {
        if( quality.first == id ) {
            return_quality = quality.second;
        }
    }
    return_quality = std::max( return_quality, contents.best_quality( id ) );

    return return_quality;
}

std::map<quality_id, int> item::get_qualities() const
{
    std::map<quality_id, int> qualities;
    for( const auto &quality : type->qualities ) {
        qualities[quality.first] = get_quality( quality.first );
    }
    return qualities;
}

bool item::has_technique( const matec_id &tech ) const
{
    return type->techniques.contains( tech ) || techniques.contains( tech );
}

void item::add_technique( const matec_id &tech )
{
    techniques.insert( tech );
}

void item::remove_technique( const matec_id &tech )
{
    techniques.erase( tech );
}

std::vector<item *> item::toolmods()
{
    std::vector<item *> res;
    if( is_tool() ) {
        for( item *e : contents.all_items_top() ) {
            if( e->is_toolmod() ) {
                res.push_back( e );
            }
        }
    }
    return res;
}

std::vector<const item *> item::toolmods() const
{
    std::vector<const item *> res;
    if( is_tool() ) {
        for( const item *e : contents.all_items_top() ) {
            if( e->is_toolmod() ) {
                res.push_back( e );
            }
        }
    }
    return res;
}

std::set<matec_id> item::get_techniques() const
{
    std::set<matec_id> result = type->techniques;
    result.insert( techniques.begin(), techniques.end() );
    return result;
}

int item::get_comestible_fun() const
{
    if( !is_comestible() ) {
    return 0;
}
auto fun = get_comestible()->fun;
for( const flag_id &flag : item_tags ) {
    fun += flag->taste_mod();
    }
for( const flag_id &flag : type->get_flags() ) {
    fun += flag->taste_mod();
    }

    return static_cast<int>( get_var( "comestible_fun", static_cast<double>( fun ) ) );
}

bool item::goes_bad() const
{
    if( item_internal::goes_bad_cache_is_for( this ) ) {
    return item_internal::goes_bad_cache_fetch();
    }
    if( has_flag( flag_PROCESSING ) ) {
    return false;
}
if( is_corpse() ) {
    // Corpses rot only if they are made of rotting materials
    return made_of_any( materials::get_rotting() );
    }
    return is_food() && get_comestible()->spoils != 0_turns;
}

bool item::goes_bad_after_opening( bool strict ) const
{
    // check if this item is explicitly a canning-type item: eg, it preserves contents
    if( strict ) {
    if( type->container && type->container->preserves &&
            !contents.empty() && contents.front().goes_bad() ) {
            return true;
        } else {
            return false;
        }
    }

    return goes_bad() || ( type->container && type->container->preserves &&
                           !contents.empty() && contents.front().goes_bad() );
}

auto item::is_in_preserving_container() const -> bool
{
    for( const item *parent = parent_item(); parent != nullptr; parent = parent->parent_item() ) {
        if( parent->type && parent->type->container && parent->type->container->preserves ) {
            return true;
        }
    }
    return false;
}

auto item::mark_rot_checked_now() -> void
{
    last_rot_check = calendar::turn;
}

time_duration item::get_shelf_life() const
{
    if( goes_bad() ) {
    if( is_food() ) {
            return get_comestible()->spoils;
        } else if( is_corpse() ) {
            return 24_hours;
        }
    }
    return 0_turns;
}

double item::get_relative_rot() const
{
    if( goes_bad() ) {
    const_cast<item *>( this )->update_rot_from_location( temperature_flag::TEMP_NORMAL );
        return rot / get_shelf_life();
    }
    return 0;
}

void item::set_relative_rot( double val )
{
    if( goes_bad() ) {
        rot = get_shelf_life() * val;
        // calc_rot uses last_rot_check (when it's not turn_zero) instead of bday.
        // this makes sure the rotting starts from now, not from bday.
        // if this item is the result of smoking or milling don't do this, we want to start from bday.
        if( !has_flag( flag_PROCESSING_RESULT ) ) {
            last_rot_check = calendar::turn;
        }
    }
}

void item::set_rot( time_duration val )
{
    rot = val;
}

int item::spoilage_sort_order() const
{
    const item *subject;
    constexpr int bottom = std::numeric_limits<int>::max();

    if( type->container && !contents.empty() ) {
        if( type->container->preserves ) {
            return bottom - 3;
        }
        subject = &contents.front();
    } else {
        subject = this;
    }

    if( subject->goes_bad() ) {
        return to_turns<int>( subject->get_shelf_life() - subject->rot );
    }

    if( subject->get_comestible() ) {
        if( subject->get_category().get_id() == itemcat_food ) {
            return bottom - 3;
        } else if( subject->get_category().get_id() == itemcat_drugs ) {
            return bottom - 2;
        } else {
            return bottom - 1;
        }
    }
    return bottom;
}

namespace
{

/**
 * Hardcoded lookup table for food rots per hour calculation.
 *
 * IRL this tends to double every 10c a few degrees above freezing, but past a certain
 * point the rate decreases until even extremophiles find it too hot. Here we just stop
 * further acceleration at 40C.
 *
 * Original formula:
 * @see https://github.com/cataclysmbn/Cataclysm-BN/blob/033901af4b52ad0bfcfd6abfe06bca4e403d44b1/src/item.cpp#L5612-L5640
 */
constexpr auto rot_chart = std::array<int, 44> {
    0, 372, 744, 1118, 1219, 1273, 1388, 1514, 1651, 1800,
    1880, 2050, 2235, 2438, 2658, 2776, 3027, 3301, 3600, 3926,
    4100, 4471, 4875, 5317, 5798, 6054, 6602, 7200, 7852, 8562,
    8941, 9751, 10633, 11595, 12645, 13205, 14400, 15703, 17125, 18674,
    19501,
};

} // namespace

/**
 * Get the hourly rot for a given temperature from the precomputed table.
 * @see rot_chart
 */
auto get_hourly_rotpoints_at_temp( const units::temperature temp ) -> int
{
    /**
     * Precomputed rot lookup table.
     */
    if( temp < temperatures::freezing ) {
    return 0;
}
if( temp > 40_c ) {
    return 21240;
}
// HACK: due to frequent fahrenheit <-> celsius conversion, 18C is actually 17.777C
// remove rounding after most of temperatures passed around are in `units::temperature`
const float temp_c = static_cast<float>( units::to_millidegree_celsius( temp ) ) / 1000;
    return rot_chart[std::round( temp_c )];
}

auto item::calc_rot( time_point time, const units::temperature temp ) const -> time_duration
{
    // Avoid needlessly calculating already rotten things.  Corpses should
    // always rot away and food rots away at twice the shelf life.  If the food
    // is in a sealed container they won't rot away, this avoids needlessly
    // calculating their rot in that case.
    if( !is_corpse() && get_shelf_life() != 0_turns && rot / get_shelf_life() > 2.0 ) {
        return 0_seconds;
    }

    // rot modifier
    float factor = 1.0;
    if( is_corpse() && has_flag( flag_FIELD_DRESS ) ) {
        factor = 0.75;
    }

    time_duration added_rot = 0_seconds;
    // simulation of different age of food at the start of the game and good/bad storage
    // conditions by applying starting variation bonus/penalty of +/- 20% of base shelf-life
    // positive = food was produced some time before calendar::start and/or bad storage
    // negative = food was stored in good conditions before calendar::start
    if( last_rot_check <= calendar::start_of_cataclysm ) {
        time_duration spoil_variation = get_shelf_life() * 0.2f;
        added_rot += rng( -spoil_variation, spoil_variation );
    }
    time_duration time_delta = time - last_rot_check;
    added_rot += factor * time_delta / 1_hours * get_hourly_rotpoints_at_temp( temp ) * 1_turns;
    return added_rot;
}

namespace
{

auto temperature_flag_to_highest_temperature( temperature_flag temperature ) -> units::temperature
{
    switch( temperature ) {
    case temperature_flag::TEMP_NORMAL:
    case temperature_flag::TEMP_HEATER:
        return units::temperature_max;
    case temperature_flag::TEMP_FRIDGE:
        return temperatures::fridge;
    case temperature_flag::TEMP_FREEZER:
        return temperatures::freezer;
    case temperature_flag::TEMP_ROOT_CELLAR:
        return temperatures::root_cellar;
}

return units::temperature_max;
}

} // namespace


time_duration item::minimum_freshness_duration( temperature_flag temperature ) const
{
    if( is_in_preserving_container() ) {
    return calendar::INDEFINITELY_LONG_DURATION;
}
const units::temperature temp = temperature_flag_to_highest_temperature( temperature );
unsigned long long rot_per_hour = get_hourly_rotpoints_at_temp( temp );

if( rot_per_hour <= 0 || !type->comestible ) {
    return calendar::INDEFINITELY_LONG_DURATION;
}

time_duration remaining_rot = type->comestible->spoils - rot;
// Has to be in int64 or it will overflow for long lasting food
unsigned long long duration = to_turns<unsigned long long>( remaining_rot )
                              * to_turns<unsigned long long>( 1_hours )
                              / rot_per_hour;
if( duration > to_turns<unsigned long long>( calendar::INDEFINITELY_LONG_DURATION ) ) {
    return calendar::INDEFINITELY_LONG_DURATION;
}

return time_duration::from_turns( static_cast<int>( duration ) );
}

void item::mod_last_rot_check( time_duration processing_duration )
{
    if( !has_own_flag( flag_PROCESSING ) ) {
        debugmsg( "mod_last_rot_check called on non smoking item: %s", tname() );
        return;
    }

    // Apply no rot while smoking
    last_rot_check += processing_duration;
}

units::volume item::get_storage() const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return is_pet_armor() ? type->pet_armor->storage : 0_ml;
    }
    units::volume storage = armor->storage;
    float mod = get_clothing_mod_val( clothing_mod_type_storage );
    storage += std::lround( mod ) * units::legacy_volume_factor;

    return storage;
}

float item::get_weight_capacity_modifier() const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return 1;
    }
    return armor->weight_capacity_modifier;
}

units::mass item::get_weight_capacity_bonus() const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return 0_gram;
    }
    return armor->weight_capacity_bonus;
}

int item::get_env_resist( int override_base_resist ) const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return is_pet_armor() ? type->pet_armor->env_resist : 0;
    }
    // modify if item is a gas mask and has filter
    int resist_base = armor->env_resist;
    int resist_filter = get_var( "overwrite_env_resist", 0 );
    int resist = std::max( { resist_base, resist_filter, override_base_resist } );

    return std::lround( resist * get_relative_health() );
}

int item::get_base_env_resist_w_filter() const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return is_pet_armor() ? type->pet_armor->env_resist_w_filter : 0;
    }
    return armor->env_resist_w_filter;
}

bool item::is_power_armor() const
{
    return ( has_flag( flag_POWERARMOR_EXO ) || has_flag( flag_POWERARMOR_EXTERNAL ) ||
    has_flag( flag_POWERARMOR_MOD ) );
}

int item::get_avg_encumber( const Character &who ) const
{
    const islot_armor *armor = find_armor_data();
    if( !armor ) {
        // handle wearable guns (e.g. shoulder strap) as special case
        return is_gun() ? volume() / 750_ml : 0;
    }

    int avg_encumber = 0;
    int avg_ctr = 0;

    for( const armor_portion_data &entry : armor->data ) {
        for( const bodypart_str_id &limb : entry.covers ) {
            int encumber = get_encumber( who, limb.id() );
            if( encumber ) {
                avg_encumber += encumber;
                ++avg_ctr;
            }
        }
    }
    if( avg_encumber == 0 ) {
        return 0;
    } else {
        return avg_encumber / avg_ctr;
    }
}

int item::get_encumber( const Character &who, const bodypart_id &bodypart ) const
{

    units::volume contents_volume( 0_ml );

    contents_volume += contents.item_size_modifier();

    if( who.is_worn( *this ) ) {
        const islot_armor *armor = find_armor_data();

        if( armor != nullptr ) {
            for( const armor_portion_data &entry : armor->data ) {
                if( entry.covers.test( bodypart.id() ) ) {
                    if( entry.max_encumber != 0 ) {
                        units::volume char_storage( 0_ml );

                        for( const item * const &e : who.worn ) {
                            char_storage += e->get_storage();
                        }

                        if( char_storage != 0_ml ) {
                            // Cast up to 64 to prevent overflow. Dividing before would prevent this but lose data.
                            contents_volume += units::from_milliliter( static_cast<int64_t>( armor->storage.value() ) *
                                               who.inv_volume().value() / char_storage.value() );
                        }
                    }
                }
            }
        }
    }

    return get_encumber_when_containing( who, contents_volume, bodypart );
}

int item::get_encumber_when_containing(
    const Character &who, const units::volume &contents_volume, const bodypart_id &bodypart ) const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        // handle wearable guns (e.g. shoulder strap) as special case
        return is_gun() ? volume() / 750_ml : 0;
    }

    int encumber = 0;

    for( const armor_portion_data &entry : armor->data ) {
        if( entry.covers.test( bodypart.id() ) ) {
            encumber = entry.encumber;
            // Non-rigid items add additional encumbrance proportional to their volume
            bool any_encumb_increase = std::ranges::any_of( armor->data,
            []( armor_portion_data data ) {
                return data.encumber != data.max_encumber;
            } );
            if( !type->rigid || any_encumb_increase ) {
                const int capacity = get_total_capacity().value();
                if( entry.max_encumber == 0 ) {
                    encumber += contents_volume / 500_ml;
                } else {
                    if( capacity <= 0 ) {
                        debugmsg( "Non-rigid item (%s) without storage capacity.", tname() );
                    } else {
                        // Cast up to 64 to prevent overflow. Dividing before would prevent this but lose data.
                        encumber += static_cast<int64_t>( entry.max_encumber - entry.encumber ) * contents_volume.value() /
                                    capacity;
                    }
                }
            }
        }
    }

    // Fit checked before changes, fitting shouldn't reduce penalties from patching.
    if( has_flag( flag_FIT ) && has_flag( flag_VARSIZE ) ) {
        encumber = std::max( encumber / 2, encumber - 10 );
    }

    // TODO: Should probably have sizing affect coverage
    const sizing sizing_level = get_sizing( who );
    switch( sizing_level ) {
        case sizing::small_sized_human_char:
        case sizing::small_sized_big_char:
            // non small characters have a HARD time wearing undersized clothing
            encumber *= 3;
            break;
        case sizing::human_sized_small_char:
        case sizing::big_sized_small_char:
            // clothes bag up around smol characters and encumber them more
            encumber *= 2;
            break;
        default:
            break;
    }
    encumber += std::lround( get_clothing_mod_val( clothing_mod_type_encumbrance ) );

    return encumber;
}

layer_level item::get_layer() const
{
    if( type->armor ) {
    // We assume that an item will never have per-item flags defining its
    // layer, so we can defer to the itype.
    return type->layer;
}

if( has_flag( flag_PERSONAL ) ) {
        return PERSONAL_LAYER;
    } else if( has_flag( flag_SKINTIGHT ) ) {
        return UNDERWEAR_LAYER;
    } else if( has_flag( flag_WAIST ) ) {
        return WAIST_LAYER;
    } else if( has_flag( flag_OUTER ) ) {
        return OUTER_LAYER;
    } else if( has_flag( flag_BELTED ) ) {
        return BELTED_LAYER;
    } else if( has_flag( flag_AURA ) ) {
        return AURA_LAYER;
    } else {
        return REGULAR_LAYER;
    }
}

int item::get_avg_coverage() const
{
    const islot_armor *armor = find_armor_data();
    if( !armor ) {
        // handle wearable guns (e.g. shoulder strap) as special case
        return is_gun() ? std::min( volume() / 500_ml, 100 ) : 0;
    }
    int avg_coverage = 0;
    int avg_ctr = 0;
    for( const armor_portion_data &entry : armor->data ) {
        for( const bodypart_str_id &limb : entry.covers ) {
            int coverage = get_coverage( limb );
            if( coverage ) {
                avg_coverage += coverage;
                ++avg_ctr;
            }
        }
    }
    if( avg_coverage == 0 ) {
        return 0;
    } else {
        avg_coverage /= avg_ctr;
        return avg_coverage;
    }
}

int item::get_coverage( const bodypart_id &bodypart ) const
{
    if( std::optional<armor_portion_data> portion_data = portion_for_bodypart( bodypart ) ) {
        return portion_data->coverage;
    }
    return 0;
}

std::optional<armor_portion_data> item::portion_for_bodypart( const bodypart_id &bodypart ) const
{
    const islot_armor *armor = find_armor_data();
    if( !armor ) {
        return std::optional<armor_portion_data>();
    }
    for( const armor_portion_data &entry : armor->data ) {
        if( entry.covers.test( bodypart.id() ) ) {
            return entry;
        }
    }
    return std::optional<armor_portion_data>();
}

int item::get_thickness() const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return is_pet_armor() ? type->pet_armor->thickness : 0;
    }
    return armor->thickness;
}

int item::get_warmth() const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return 0;
    }
    int result = armor->warmth;

    result += get_clothing_mod_val( clothing_mod_type_warmth );

    return result;
}

units::volume item::get_pet_armor_max_vol() const
{
    return is_pet_armor() ? type->pet_armor->max_vol : 0_ml;
}

units::volume item::get_pet_armor_min_vol() const
{
    return is_pet_armor() ? type->pet_armor->min_vol : 0_ml;
}

std::string item::get_pet_armor_bodytype() const
{
    return is_pet_armor() ? type->pet_armor->bodytype : "";
}

time_duration item::brewing_time() const
{
    return is_brewable() ? type->brewable->time : 0_turns;
}

const std::vector<itype_id> &item::brewing_results() const
{
    static const std::vector<itype_id> nulresult{};
    return is_brewable() ? type->brewable->results : nulresult;
}

bool item::can_revive() const
{
    return is_corpse() && corpse->has_flag( MF_REVIVES ) && damage() < max_damage() &&
    !( has_flag( flag_FIELD_DRESS ) || has_flag( flag_FIELD_DRESS_FAILED ) ||
    has_flag( flag_QUARTERED ) ||
    has_flag( flag_SKINNED ) || has_flag( flag_PULPED ) );
}

bool item::ready_to_revive( const tripoint_bub_ms &pos ) const
{
    if( !can_revive() ) {
    return false;
}
if( get_map().veh_at( pos ) ) {
    return false;
}
if( !calendar::once_every( 1_seconds ) ) {
    return false;
}
int age_in_hours = to_hours<int>( age() );
age_in_hours -= static_cast<int>( static_cast<float>( burnt ) / ( volume() / 250_ml ) );
    if( damage_level( 4 ) > 0 ) {
    age_in_hours /= ( damage_level( 4 ) + 1 );
    }
    int rez_factor = 48 - age_in_hours;
    if( age_in_hours > 6 && ( rez_factor <= 0 || one_in( rez_factor ) ) ) {
        // If we're a special revival zombie, wait to get up until the player is nearby.
        const bool isReviveSpecial = has_flag( flag_REVIVE_SPECIAL );
        if( isReviveSpecial ) {
            const int distance = rl_dist( pos, get_player_character().bub_pos() );
            if( distance > 3 ) {
                return false;
            }
            if( !one_in( distance + 1 ) ) {
                return false;
            }
        }

        return true;
    }
    return false;
}

bool item::is_money() const
{
    return ammo_types().contains( ammotype( "money" ) );
}

bool item::count_by_charges() const
{
    return type->count_by_charges();
}

int item::count() const
{
    return count_by_charges() ? charges : 1;
}

bool item::craft_has_charges()
{
    if( count_by_charges() ) {
        return true;
    } else if( ammo_types().empty() ) {
        return true;
    }

    return false;
}

#if defined(_MSC_VER)
// Deal with MSVC compiler bug (#17791, #17958)
#pragma optimize( "", off )
#endif


int item::chip_resistance( bool worst ) const
{
    int res = worst ? INT_MAX : INT_MIN;
    for( const material_type *mat : made_of_types() ) {
        const int val = mat->chip_resist();
        res = worst ? std::min( res, val ) : std::max( res, val );
    }

    if( res == INT_MAX || res == INT_MIN ) {
        return 2;
    }

    if( res <= 0 ) {
        return 0;
    }

    return res;
}

std::optional<resistances> item::damage_resistance_override() const
{
    if( is_null() || !type->armor ) {
    return std::optional<resistances>();
    }

    return type->armor->resistance;
}

int item::min_damage() const
{
    return type->damage_min();
}

int item::max_damage() const
{
    return type->damage_max();
}

float item::get_relative_health() const
{
    return ( max_damage() + 1.0f - damage() ) / ( max_damage() + 1.0f );
}

bool item::mod_damage( int qty, damage_type dt )
{
    bool destroy = false;

    if( count_by_charges() ) {
        charges -= std::min( type->stack_size * qty / itype::damage_scale, charges );
        destroy |= charges == 0;
    }

    if( qty > 0 ) {
        on_damage( qty, dt );
    }

    if( !count_by_charges() ) {
        destroy |= damage_ + qty > max_damage();

        damage_ = std::max( std::min( damage_ + qty, max_damage() ), min_damage() );
    }

    if( destroy && type->iequippable_callbacks ) {
        type->iequippable_callbacks->call_on_break( get_avatar(), *this );
    }

    return destroy;
}

bool item::mod_damage( const int qty )
{
    return mod_damage( qty, DT_NULL );
}

bool item::inc_damage( const damage_type dt )
{
    return mod_damage( itype::damage_scale, dt );
}

bool item::inc_damage()
{
    return inc_damage( DT_NULL );
}

nc_color item::damage_color() const
{
    // TODO: unify with veh_interact::countDurability
    switch( damage_level( 4 ) ) {
    default:
        // reinforced
        if( damage() <= min_damage() ) {
                // fully reinforced
                return c_green;
            } else {
                return c_light_green;
            }
        case 0:
            return c_light_green;
        case 1:
            return c_yellow;
        case 2:
            return c_magenta;
        case 3:
            return c_light_red;
        case 4:
            if( damage() >= max_damage() ) {
                return c_dark_gray;
            } else {
                return c_red;
            }
    }
}

std::string item::damage_symbol() const
{
    switch( damage_level( 4 ) ) {
    default:
        // reinforced
        return _( R"(++)" );
        case 0:
            return _( R"(||)" );
        case 1:
            return _( R"(|\)" );
        case 2:
            return _( R"(|.)" );
        case 3:
            return _( R"(\.)" );
        case 4:
            if( damage() >= max_damage() ) {
                return _( R"(XX)" );
            } else {
                return _( R"(..)" );
            }

    }
}

std::string item::durability_indicator( bool include_intact ) const
{
    std::string outputstring;

    if( damage() < 0 )  {
        if( get_option<bool>( "ITEM_HEALTH_BAR" ) ) {
            outputstring = colorize( damage_symbol() + "\u00A0", damage_color() );
        } else if( is_gun() ) {
            outputstring = pgettext( "damage adjective", "accurized " );
        } else {
            outputstring = pgettext( "damage adjective", "reinforced " );
        }
    } else if( has_flag( flag_CORPSE ) ) {
        if( damage() > 0 ) {
            switch( damage_level( 4 ) ) {
                case 1:
                    outputstring = pgettext( "damage adjective", "bruised " );
                    break;
                case 2:
                    outputstring = pgettext( "damage adjective", "damaged " );
                    break;
                case 3:
                    outputstring = pgettext( "damage adjective", "mangled " );
                    break;
                default:
                    outputstring = pgettext( "damage adjective", "pulped " );
                    break;
            }
        }
    } else if( get_option<bool>( "ITEM_HEALTH_BAR" ) ) {
        outputstring = colorize( damage_symbol() + "\u00A0", damage_color() );
    } else {
        outputstring = string_format( "%s ", get_base_material().dmg_adj( damage_level( 4 ) ) );
        if( include_intact && outputstring == " " ) {
            outputstring = _( "fully intact " );
        }
    }

    return  outputstring;
}

const std::set<itype_id> &item::repaired_with() const
{
    static std::set<itype_id> no_repair;
    return has_flag( flag_NO_REPAIR )  ? no_repair : type->repair;
}

void item::mitigate_damage( damage_unit &du ) const
{
    const resistances res = resistances( *this );
    const float mitigation = res.get_effective_resist( du );
    // get_effective_resist subtracts the flat penetration value before multiplying the remaining armor.
    // therefore, res_pen is reduced by the full value of the item's armor value even though mitigation might be smaller (such as an attack with a 0.5 armor multiplier)
    du.res_pen = std::max( 0.0f, du.res_pen - res.type_resist( du.type ) );
    du.amount = std::max( 0.0f, du.amount - mitigation );
}

int item::damage_resist( damage_type dt, bool to_self ) const
{
    switch( dt ) {
    case DT_NULL:
    case NUM_DT:
        return 0;
    case DT_TRUE:
    case DT_BIOLOGICAL:
    case DT_ELECTRIC:
    case DT_COLD:
    case DT_DARK:
    case DT_LIGHT:
    case DT_PSI:
        // Currently hardcoded:
        // Items can never be damaged by those types
        // But they provide 0 protection from them
        return to_self ? INT_MAX : 0;
    case DT_BASH:
        return bash_resist( to_self );
        case DT_CUT:
            return cut_resist( to_self );
        case DT_ACID:
            return acid_resist( to_self );
        case DT_STAB:
            return stab_resist( to_self );
        case DT_HEAT:
            return fire_resist( to_self );
        case DT_BULLET:
            return bullet_resist( to_self );
        default:
            debugmsg( "Invalid damage type: %d", dt );
    }

    return 0;
}

bool item::is_two_handed( const Character &guy ) const
{
    // Big weapons are always two-handed if you're Medium or smaller.
    if( has_flag( flag_ALWAYS_TWOHAND ) && guy.get_size() <= creature_size::medium ) {
        return true;
    }
    // Large characters get a penalty when trying to wield two-handed weapons, Huge characters treat them like normal.
    const float str_factor = has_flag( flag_ALWAYS_TWOHAND ) &&
                             guy.get_size() != creature_size::huge ?  2.0f : 4.0f;
    ///\EFFECT_STR determines which weapons can be wielded with one hand
    return ( ( weight() / 113_gram ) > guy.str_cur * str_factor );
}

const std::vector<material_id> &item::made_of() const
{
    if( is_corpse() ) {
    return corpse->mat;
}
return type->materials;
}

const std::map<quality_id, int> &item::quality_of() const
{
    return type->qualities;
}

std::vector<const material_type *> item::made_of_types() const
{
    std::vector<const material_type *> material_types_composed_of;
    for( const material_id &mat_id : made_of() ) {
        material_types_composed_of.push_back( &mat_id.obj() );
    }
    return material_types_composed_of;
}

bool item::made_of_any( const std::set<material_id> &mat_idents ) const
{
    const std::vector<material_id> &mats = made_of();
    if( mats.empty() ) {
        return false;
    }

    return std::ranges::any_of( mats, [&mat_idents]( const material_id & e ) {
        return mat_idents.count( e );
    } );
}

bool item::only_made_of( const std::set<material_id> &mat_idents ) const
{
    const std::vector<material_id> &mats = made_of();
    if( mats.empty() ) {
        return false;
    }

    return std::ranges::all_of( mats, [&mat_idents]( const material_id & e ) {
        return mat_idents.count( e );
    } );
}

bool item::made_of( const material_id &mat_ident ) const
{
    const std::vector<material_id> &materials = made_of();
    return std::ranges::contains( materials, mat_ident );
}

bool item::contents_made_of( const phase_id phase ) const
{
    return !contents.empty() && contents.front().made_of( phase );
}

bool item::contents_normally_made_of( const phase_id phase ) const
{
    return !contents.empty() && contents.front().type->phase == phase;
}

bool item::made_of( phase_id phase ) const
{
    if( is_null() ) {
    return false;
}
return type->phase == phase;
}

bool item::conductive() const
{
    if( is_null() ) {
    return false;
}

if( has_flag( flag_CONDUCTIVE ) ) {
    return true;
}

if( has_flag( flag_NONCONDUCTIVE ) ) {
    return false;
}

// If any material has electricity resistance equal to or lower than flesh (1) we are conductive.
const std::vector<const material_type *> &mats = made_of_types();
return std::ranges::any_of( mats, []( const material_type * mt ) {
    return mt->elec_resist() <= 1;
    } );
}

bool item::reinforceable() const
{
    if( is_null() || has_flag( flag_NO_REPAIR ) ) {
    return false;
}

// If a material is reinforceable, so are we
const std::vector<const material_type *> &mats = made_of_types();
return std::ranges::any_of( mats, []( const material_type * mt ) {
    return mt->reinforces();
    } );
}

bool item::destroyed_at_zero_charges() const
{
    return ( is_ammo() || is_food() );
}

bool item::is_gun() const
{
    return !!type->gun;
}

bool item::is_firearm() const
{
    return is_gun() && !has_flag( flag_PRIMITIVE_RANGED_WEAPON );
}

int item::get_reload_time() const
{
    if( !is_gun() && !is_magazine() ) {
    return 0;
}

int reload_time = is_gun() ? type->gun->reload_time : type->magazine->reload_time;
for( const item *mod : gunmods() ) {
    reload_time = ( reload_time * ( 100 + mod->type->gunmod->reload_modifier ) / 100 );
    }

    return reload_time;
}

bool item::is_silent() const
{
    // Most guns with a suppressor installed will be under this value, also see item::gun_noise in ranged.cpp
    return gun_noise().volume < 50;
}

bool item::is_gunmod() const
{
    return !!type->gunmod;
}

bool item::is_bionic() const
{
    return !!type->bionic;
}

bool item::is_magazine() const
{
    return !!type->magazine;
}

bool item::is_battery() const
{
    return !!type->battery;
}

bool item::is_ammo_belt() const
{
    return is_magazine() && has_flag( flag_MAG_BELT );
}

bool item::is_bandolier() const
{
    return type->can_use( "bandolier" );
}

bool item::is_holster() const
{
    return type->can_use( "holster" );
}

bool item::is_ammo() const
{
    return !!type->ammo;
}

bool item::is_comestible() const
{
    return !!get_comestible();
}

bool item::is_food() const
{
    return is_comestible() && ( get_comestible()->comesttype == "FOOD" ||
                                get_comestible()->comesttype == "DRINK" );
}

bool item::is_medication() const
{
    return is_comestible() && get_comestible()->comesttype == "MED";
}

bool item::is_brewable() const
{
    return !!type->brewable;
}

bool item::is_food_container() const
{
    return ( !contents.empty() && contents.front().is_food() ) ||
    ( is_craft() &&
    craft_data_->making->create_result()->is_food_container() );
}

bool item::is_med_container() const
{
    return !contents.empty() && contents.front().is_medication();
}

bool item::is_corpse() const
{
    return corpse != nullptr && has_flag( flag_CORPSE );
}

const mtype *item::get_mtype() const
{
    return corpse;
}

template<typename Item>
static Item *get_food_impl( Item *it )
{
    if( it->is_food() ) {
        return it;
    } else if( it->is_food_container() && !it->contents.empty() ) {
        return &it->contents.front();
    } else {
        return nullptr;
    }
}

item *item::get_food()
{
    return get_food_impl( this );
}

const item *item::get_food() const
{
    return get_food_impl( this );
}

void item::set_mtype( const mtype *const m )
{
    // This is potentially dangerous, e.g. for corpse items, which *must* have a valid mtype pointer.
    if( m == nullptr ) {
        debugmsg( "setting item::corpse of %s to NULL", tname() );
        return;
    }
    corpse = m;
}

bool item::is_ammo_container() const
{
    return !is_magazine() && !contents.empty() && contents.front().is_ammo();
}

bool item::is_melee() const
{
    for( int idx = DT_NULL + 1; idx != NUM_DT; ++idx ) {
        if( is_melee( static_cast<damage_type>( idx ) ) ) {
            return true;
        }
    }
    return false;
}

bool item::is_melee( damage_type dt ) const
{
    return damage_melee( dt ) > MELEE_STAT;
}

const islot_armor *item::find_armor_data() const
{
    if( type->armor ) {
    return &*type->armor;
}
// Currently the only way to make a non-armor item into armor is to install a gun mod.
// The gunmods are stored in the items contents, as are the contents of a container, and the
// tools in a tool belt (a container actually), or the ammo in a quiver (container again).
for( const item *mod : gunmods() ) {
        if( mod->type->armor ) {
            return &*mod->type->armor;
        }
    }
    return nullptr;
}

bool item::is_pet_armor( bool on_pet ) const
{
    bool is_worn = on_pet && !get_var( "pet_armor", "" ).empty();
    return has_flag( flag_IS_PET_ARMOR ) && ( is_worn || !on_pet );
}

bool item::is_armor() const
{
    return find_armor_data() != nullptr || has_flag( flag_IS_ARMOR );
}

bool item::is_book() const
{
    return !!type->book;
}

bool item::is_map() const
{
    return get_category().get_id() == itemcat_maps;
}

bool item::is_container() const
{
    return !!type->container;
}

bool item::is_watertight_container() const
{
    return type->container && type->container->watertight && type->container->seals;
}

bool item::is_non_resealable_container() const
{
    return type->container && !type->container->seals && type->container->unseals_into;
}

bool item::is_in_container() const
{
    auto location = static_cast<item_location *>( &*loc );
    return location->where() == item_location_type::container || ( parent_item() &&
            parent_item()->is_container() );
}

bool item::is_bucket() const
{
    // That "preserves" part is a hack:
    // Currently all non-empty cans are effectively sealed at all times
    // Making them buckets would cause weirdness
    return type->container &&
           type->container->watertight &&
           !type->container->seals &&
           !type->container->unseals_into;
}

bool item::is_bucket_nonempty() const
{
    return is_bucket() && !is_container_empty();
}

bool item::is_engine() const
{
    return !!type->engine;
}

bool item::is_wheel() const
{
    return !!type->wheel;
}

bool item::is_fuel() const
{
    return !!type->fuel;
}

bool item::is_toolmod() const
{
    return !is_gunmod() && type->mod;
}

bool item::is_faulty() const
{
    return is_engine() ? !faults.empty() : false;
}

bool item::is_irremovable() const
{
    return has_flag( flag_IRREMOVABLE );
}

int item::wind_resist() const
{
    std::vector<const material_type *> materials = made_of_types();
    if( materials.empty() ) {
        debugmsg( "Called item::wind_resist on an item (%s [%s]) made of nothing!", tname(), typeId() );
        return 99;
    }

    int best = -1;
    for( const material_type *mat : materials ) {
        std::optional<int> resistance = mat->wind_resist();
        if( resistance && *resistance > best ) {
            best = *resistance;
        }
    }

    // Default to 99% effective
    if( best == -1 ) {
        return 99;
    }

    return best;
}

std::set<fault_id> item::faults_potential() const
{
    std::set<fault_id> res;
    res.insert( type->faults.begin(), type->faults.end() );
    return res;
}

int item::wheel_area() const
{
    return is_wheel() ? type->wheel->diameter * type->wheel->width : 0;
}

float item::fuel_energy() const
{
    return is_fuel() ? type->fuel->energy : 0.0f;
}

ter_id item::fuel_pump_terrain() const
{
    return is_fuel() ? type->fuel->pump_terrain : t_null;
}

bool item::has_explosion_data() const
{
    return is_fuel() ? type->fuel->has_explode_data : false;
}

struct fuel_explosion item::get_explosion_data()
{
    static struct fuel_explosion null_data;
    return has_explosion_data() ? type->fuel->explosion_data : null_data;
}

float item::get_kcal_mult() const
{
    return get_var( "kcal_mult", 1.0 );
}
void item::set_kcal_mult( float val )
{
    set_var( "kcal_mult", val );
}
bool item::is_container_empty() const
{
    return contents.empty();
}

bool item::is_container_full( bool allow_bucket ) const
{
    if( is_container_empty() ) {
    return false;
}
if( is_watertight_container() ) {
    return get_remaining_capacity_for_liquid( contents.front(), allow_bucket ) == 0;
    } else if( !is_reloadable_with( contents.front().typeId() ) ) {
        return true;
    } else {
        int ammo = contents.front().charges_per_volume( get_container_capacity() ) -
                   contents.front().charges;
        return ammo <= 0;
    }
}

bool item::can_unload_liquid() const
{
    if( is_container_empty() ) {
    return true;
}

const item &cts = contents.front();
bool cts_is_frozen_liquid = cts.made_of( LIQUID ) && cts.made_of( SOLID );
return is_bucket() || !cts_is_frozen_liquid;
}

bool item::can_reload_with( const ammotype &ammo ) const
{
    return is_reloadable_helper( ammo->default_ammotype(), false );
}

bool item::can_reload_with( const itype_id &ammo ) const
{
    return is_reloadable_helper( ammo, false );
}

bool item::is_reloadable_with( const itype_id &ammo ) const
{
    return is_reloadable_helper( ammo, true );
}

bool item::is_reloadable_helper( const itype_id &ammo, bool now ) const
{
    // empty ammo is passed for listing possible ammo apparently, so it needs to return true.
    if( !is_reloadable() ) {
    return false;
} else if( is_watertight_container() ) {
    if( ammo.is_empty() ) {
            return now ? !is_container_full() : true;
        } else {
            return now ? ( is_container_empty() || contents.front().typeId() == ammo ) : true;
        }
    } else if( is_container() ) {
    if( ammo.is_empty() ) {
            return now ? !is_container_full() : true;
        } else if( ammo->phase == LIQUID ) {
            return false;
        } else {
            return now ? ( is_container_empty() || contents.front().typeId() == ammo ) : true;
        }
    } else if( magazine_integral() ) {
    if( !ammo.is_empty() ) {
            if( now && ammo_data() ) {
                if( ammo_current() != ammo ) {
                    return false;
                }
            } else {
                const itype *at = &*ammo;
                if( ( !at->ammo || !ammo_types().contains( at->ammo->type ) ) &&
                    !magazine_compatible().contains( ammo ) ) {
                    return false;
                }
            }
        }
        return now ? ( ammo_remaining() < ammo_capacity() ) : true;
    } else {
        return ammo.is_empty() ? true : magazine_compatible().count( ammo );
    }
}

bool item::is_craft() const
{
    return craft_data_ != nullptr;
}

bool item::is_pocket_dimension_key() const
{
    return pocket_dim.has_value();
}

bool item::is_funnel_container( units::volume &bigger_than ) const
{
    if( !is_bucket() && !is_watertight_container() ) {
    return false;
}
// TODO: consider linking funnel to item or -making- it an active item
if( get_container_capacity() <= bigger_than ) {
    return false; // skip contents check, performance
}
if(
        contents.empty() ||
        contents.front().typeId() == itype_water ||
        contents.front().typeId() == itype_water_acid ||
        contents.front().typeId() == itype_water_acid_weak ) {
    bigger_than = get_container_capacity();
        return true;
    }
    return false;
}

bool item::is_emissive() const
{
    return light.luminance > 0 || type->light_emission > 0;
}

bool item::is_deployable() const
{
    return type->can_use( "deploy_furn" );
}

bool item::is_tool() const
{
    return !!type->tool;
}

bool item::is_transformable() const
{
    return type->use_methods.contains( "transform" );
}

bool item::is_artifact() const
{
    return !!type->artifact;
}

bool item::is_relic() const
{
    return !!relic_data;
}

const std::vector<enchantment> &item::get_enchantments() const
{
    if( !is_relic() ) {
    static const std::vector<enchantment> fallback;
    return fallback;
}
return relic_data->get_enchantments();
}

double item::bonus_from_enchantments( const Character &owner, double base,
                                      enchant_vals::mod value, bool round ) const
{
    double add = 0.0;
    double mul = 0.0;
    for( const enchantment &ench : get_enchantments() ) {
        if( ench.is_active( owner, *this ) ) {
            add += ench.get_value_add( value );
            mul += ench.get_value_multiply( value );
        }
    }
    // TODO: this part duplicates enchantment::calc_bonus()
    double ret = add + base * mul;
    if( round ) {
        ret = trunc( ret );
    }
    return ret;
}

double item::bonus_from_enchantments_wielded( double base, enchant_vals::mod value,
        bool round ) const
{
    double add = 0.0;
    double mul = 0.0;
    for( const enchantment &ench : get_enchantments() ) {
        if( ench.is_active_when_wielded() ) {
            add += ench.get_value_add( value );
            mul += ench.get_value_multiply( value );
        }
    }
    // TODO: this part duplicates enchantment::calc_bonus()
    double ret = add + base * mul;
    if( round ) {
        ret = trunc( ret );
    }
    return ret;
}

const std::vector<relic_recharge> &item::get_relic_recharge_scheme() const
{
    return relic_data->get_recharge_scheme();
}

bool item::can_contain( const item &it ) const
{
    // TODO: Volume check
    return can_contain( *it.type );
}

bool item::can_contain( const itype &tp ) const
{
    if( !type->container ) {
    // TODO: Tools etc.
    return false;
}

if( tp.phase == LIQUID && !type->container->watertight ) {
    return false;
}

// TODO: Acid in waterskins
return true;
}

const item &item::get_contained() const
{
    if( contents.empty() ) {
    return null_item_reference();
    }
    return contents.front();
}

bool item::spill_contents( Character &c )
{
    if( !is_container() || is_container_empty() ) {
        return true;
    }

    if( c.is_npc() ) {
        return spill_contents( c.bub_pos() );
    }

    contents.handle_liquid_or_spill( c );
    on_contents_changed();

    return true;
}

bool item::spill_contents( const tripoint_bub_ms &pos )
{
    if( !is_container() || is_container_empty() ) {
        return true;
    }

    for( detached_ptr<item> &it : contents.clear_items() ) {
        get_map().add_item_or_charges( pos, std::move( it ) );
    }

    return true;
}

int item::get_chapters() const
{
    if( !type->book ) {
    return 0;
}
return type->book->chapters;
}

int item::get_remaining_chapters( const Character &ch ) const
{
    const std::string var = string_format( "remaining-chapters-%d", ch.getID().get_value() );
    return get_var( var, get_chapters() );
}

void item::mark_chapter_as_read( const Character &ch )
{
    const std::string var = string_format( "remaining-chapters-%d", ch.getID().get_value() );
    if( type->book && type->book->chapters == 0 ) {
        // books without chapters will always have remaining chapters == 0, so we don't need to store them
        erase_var( var );
        return;
    }
    const int remain = std::max( 0, get_remaining_chapters( ch ) - 1 );
    set_var( var, remain );
}

std::vector<std::pair<const recipe *, int>> item::get_available_recipes( const Character &u ) const
{
    std::vector<std::pair<const recipe *, int>> recipe_entries;
    if( is_book() ) {
        for( const book_recipe &elem : type->book->recipes ) {
            if( u.get_skill_level( elem.recipe->skill_used ) >= elem.skill_level ) {
                recipe_entries.emplace_back( elem.recipe, elem.skill_level );
            }
        }
    } else if( has_var( "EIPC_RECIPES" ) ) {
        // See einkpc_download_memory_card() in iuse.cpp where this is set.
        const std::string recipes = get_var( "EIPC_RECIPES" );
        // Capture the index one past the delimiter, i.e. start of target string.
        size_t first_string_index = recipes.find_first_of( ',' ) + 1;
        while( first_string_index != std::string::npos ) {
            size_t next_string_index = recipes.find_first_of( ',', first_string_index );
            if( next_string_index == std::string::npos ) {
                break;
            }
            std::string new_recipe = recipes.substr( first_string_index,
                                     next_string_index - first_string_index );
            const recipe *r = &recipe_id( new_recipe ).obj();
            if( u.get_skill_level( r->skill_used ) >= r->difficulty ) {
                recipe_entries.emplace_back( r, r->difficulty );
            }
            first_string_index = next_string_index + 1;
        }
    }
    return recipe_entries;
}

const material_type &item::get_random_material() const
{
    return random_entry( made_of(), material_id::NULL_ID() ).obj();
}

const material_type &item::get_base_material() const
{
    const std::vector<material_id> &mats = made_of();
    return mats.empty() ? material_id::NULL_ID().obj() : mats.front().obj();
}

bool item::operator<( const item &other ) const
{
    const item_category &cat_a = get_category();
    const item_category &cat_b = other.get_category();
    if( cat_a != cat_b ) {
        return cat_a < cat_b;
    } else {
        const item *me = is_container() && !contents.empty() ? &contents.front() : this;
        const item *rhs = other.is_container() &&
                          !other.contents.empty() ? &other.contents.front() : &other;

        const itype *me_type = me->type;
        const itype *rhs_type = rhs->type;
        if( !me_type || !rhs_type ) {
            return !!me_type;
        }

        if( me_type->get_id() == rhs_type->get_id() ) {
            if( me->is_money() ) {
                return me->charges > rhs->charges;
            }
            return me->charges < rhs->charges;
        } else {
            std::string n1 = me_type->nname( 1 );
            std::string n2 = rhs_type->nname( 1 );
            return localized_compare( n1, n2 );
        }
    }
}

skill_id item::gun_skill() const
{
    if( !is_gun() ) {
    return skill_id::NULL_ID();
    }
    return type->gun->skill_used;
}

skill_id item::melee_skill() const
{
    if( !is_melee() ) {
    return skill_id::NULL_ID();
    }

    if( has_flag( flag_UNARMED_WEAPON ) ) {
    return skill_unarmed;
}

int hi = 0;
skill_id res = skill_id::NULL_ID();

for( int idx = DT_NULL + 1; idx != NUM_DT; ++idx ) {
    const int val = damage_melee( static_cast<damage_type>( idx ) );
        const skill_id &sk  = skill_by_dt( static_cast<damage_type>( idx ) );
        if( val > hi && sk ) {
            hi = val;
            res = sk;
        }
    }

    return res;
}

int item::gun_dispersion( bool with_ammo, bool with_scaling ) const
{
    if( !is_gun() ) {
    return 0;
}
int dispersion_sum = type->gun->dispersion;
for( const item *mod : gunmods() ) {
    dispersion_sum += mod->type->gunmod->dispersion;
}
dispersion_sum += get_dispersion_bonus();
int dispPerDamage = get_option< int >( "DISPERSION_PER_GUN_DAMAGE" );
dispersion_sum += damage_level( 4 ) * dispPerDamage;
dispersion_sum = std::max( dispersion_sum, 0 );
if( with_ammo && ammo_data() ) {
    dispersion_sum += ammo_data()->ammo->dispersion;
    }
    if( !with_scaling ) {
    return dispersion_sum;
}

// Dividing dispersion by 15 temporarily as a gross adjustment,
// will bake that adjustment into individual gun definitions in the future.
// Absolute minimum gun dispersion is 1.
double divider = get_option< float >( "GUN_DISPERSION_DIVIDER" );
dispersion_sum = std::max( static_cast<int>( std::round( dispersion_sum / divider ) ), 1 );

    return dispersion_sum;
}

int item::sight_dispersion() const
{
    if( !is_gun() ) {
    return 0;
}

int res = has_flag( flag_DISABLE_SIGHTS ) ? 90 : type->gun->sight_dispersion;

for( const item *e : gunmods() ) {
    const islot_gunmod &mod = *e->type->gunmod;
    if( mod.sight_dispersion < 0 || mod.aim_speed < 0 ) {
            continue; // skip gunmods which don't provide a sight
        }
        res = std::min( res, mod.sight_dispersion );
    }

    return res;
}

damage_instance item::gun_damage( bool with_ammo ) const
{
    if( !is_gun() ) {
    return damage_instance();
    }
    damage_instance ret = type->gun->damage;

for( const item *mod : gunmods() ) {
    ret.add( mod->type->gunmod->damage );
    }

    if( with_ammo && ammo_data() ) {
    ret.add( ammo_data()->ammo->damage );
    }

    ret.add( get_ranged_damage_bonus() );

    int item_damage = damage_level( 4 );
    if( item_damage > 0 ) {
    // TODO: This isn't a good solution for multi-damage guns/ammos
    for( damage_unit &du : ret ) {
            if( du.amount <= 1.0 ) {
                continue;
            }
            du.amount = std::max<float>( 1.0f, du.amount - item_damage * 2 );
        }
    }

    return ret;
}

double item::gun_recoil_multiplier( bool bipod ) const
{
    double handling = type->gun->handling;
    for( const item *mod : gunmods() ) {
        if( bipod || !mod->has_flag( flag_BIPOD ) ) {
            handling += mod->type->gunmod->handling;
        }
    }

    // Rescale from JSON units which are intentionally specified as integral values
    handling /= 10;

    // Handling will almost always be above 1.0
    if( handling > 1.0 ) {
        return 1.0 / handling;
    } else {
        return 2.0 - handling;
    }
}

int item::gun_recoil( bool bipod ) const
{
    if( !is_gun() || ( ammo_required() && !ammo_remaining() ) ) {
    return 0;
}

int qty = type->gun->recoil;
if( ammo_data() ) {
    qty += ammo_data()->ammo->recoil;
    }

    qty += get_recoil_bonus();

    return qty * gun_recoil_multiplier( bipod );
}

int item::gun_range( bool with_ammo ) const
{
    if( !is_gun() ) {
    return 0;
}
int ret = type->gun->range;
for( const item *mod : gunmods() ) {
    ret += mod->type->gunmod->range;
}
if( with_ammo && ammo_data() ) {
    const auto &ammo_shape = ammo_data()->ammo->shape;
        if( ammo_shape ) {
            ret = ammo_shape->get_range();
        } else {
            int ret_thrown = 0;
            if( gun_skill() == skill_throw && ammo_data() ) {
                const itype *curammo = ammo_data();
                item &tmp = *item::spawn_temporary( item( curammo ) );
                ret_thrown += get_avatar().throw_range( tmp );
            }
            ret += std::max( ammo_data()->ammo->range, ret_thrown );
        }
    }
    ret += get_range_bonus();
    return std::min( std::max( 0, ret ), g_max_view_distance );
}

int item::gun_range( const player *p ) const
{
    int ret = gun_range( true );
    if( p == nullptr ) {
        return ret;
    }
    if( !p->meets_requirements( *this ) ) {
        return 0;
    }

    // Reduce bow range if player has less than minimum strength.
    ret *= ranged::str_draw_range_modifier( *this, *p );

    // Apply enchantment bonuses to range
    int ench_range_bonus = p->bonus_from_enchantments( ret, enchant_vals::mod::RANGED_RANGE, true );
    ret = std::max( 1, ret + ench_range_bonus );

    return std::max( 0, ret );
}

int item::gun_speed( bool with_ammo ) const
{
    if( !is_gun() ) {
    return 10;
}
int ret = type->gun->speed;
for( const item *mod : gunmods() ) {
    ret += mod->type->gunmod->speed;
}
if( with_ammo && ammo_data() ) {
    ret += ammo_data()->ammo->speed;
    }
    return std::max( 0, ret );
}

double item::gun_aimed_crit_bonus( bool with_ammo ) const
{
    if( !is_gun() ) {
    return 0;
}
int ret = type->gun->aimedcritbonus;
for( const item *mod : gunmods() ) {
    ret += mod->type->gunmod->aimedcritbonus;
}
if( with_ammo && ammo_data() ) {
    ret += ammo_data()->ammo->aimedcritbonus;
    }
    return std::max( 0, ret );
}

double item::gun_aimed_crit_max_bonus( bool with_ammo ) const
{
    if( !is_gun() ) {
    return 0;
}
int ret = type->gun->aimedcritmaxbonus;
for( const item *mod : gunmods() ) {
    ret += mod->type->gunmod->aimedcritmaxbonus;
}
if( with_ammo && ammo_data() ) {
    ret += ammo_data()->ammo->aimedcritmaxbonus;
    }
    return std::max( 0, ret );
}

units::energy item::energy_remaining() const
{
    if( is_battery() ) {
    return energy;
}

return 0_J;
}

int item::ammo_remaining() const
{
    const item *mag = magazine_current();
    if( mag ) {
        return mag->ammo_remaining();
    }

    if( is_tool() || is_gun() ) {
        // includes auxiliary gunmods
        if( has_flag( flag_USES_BIONIC_POWER ) ) {
            int power = units::to_kilojoule( get_avatar().get_power_level() );
            return power;
        }
        return charges;
    }

    if( is_magazine() || is_bandolier() ) {
        int res = 0;
        for( const item *e : contents.all_items_top() ) {
            res += e->charges;
        }
        return res;
    }

    return 0;
}

int item::ammo_capacity() const
{
    return ammo_capacity( false );
}

int item::ammo_capacity( bool potential_capacity ) const
{
    int res = 0;

    const item *mag = magazine_current();

    if( has_flag( flag_USES_BIONIC_POWER ) ) {
        avatar &you = get_avatar();
        return you.get_max_power_level() / 1_kJ;
    }
    if( mag ) {
        return mag->ammo_capacity();
    }

    if( is_tool() ) {
        res = type->tool->max_charges;
        if( res == 0 && magazine_default() && potential_capacity ) {
            res = magazine_default()->magazine->capacity;
        }
        for( const item *e : toolmods() ) {
            res *= e->type->mod->capacity_multiplier;
        }
    }

    if( is_gun() ) {
        res = type->gun->clip;
        for( const item *e : gunmods() ) {
            res *= e->type->mod->capacity_multiplier;
        }
    }

    if( is_magazine() ) {
        res = type->magazine->capacity;
    }

    if( is_bandolier() ) {
        return dynamic_cast<const bandolier_actor *>
               ( type->get_use( "bandolier" )->get_actor_ptr() )->capacity;
    }

    return res;
}

int item::ammo_required() const
{
    if( is_tool() ) {
    return std::max( type->charges_to_use(), 0 );
    }

    if( is_gun() ) {
    if( ammo_types().empty() ) {
            return 0;
        } else if( has_flag( flag_FIRE_100 ) ) {
            return 100;
        } else if( has_flag( flag_FIRE_50 ) ) {
            return 50;
        } else if( has_flag( flag_FIRE_20 ) ) {
            return 20;
        } else {
            int modifier = 0;
            float multiplier = 1.0f;
            for( const item *mod : gunmods() ) {
                modifier += mod->type->gunmod->ammo_to_fire_modifier;
                multiplier *= mod->type->gunmod->ammo_to_fire_multiplier;
            }
            return ( type->gun->ammo_to_fire * multiplier ) + modifier;
        }
    }

    return 0;
}

bool item::ammo_sufficient( int qty ) const
{
    return ammo_remaining() >= ammo_required() * qty;
}

int item::ammo_consume( int qty, const tripoint_bub_ms &pos )
{
    if( qty < 0 ) {
        debugmsg( "Cannot consume negative quantity of ammo for %s", tname() );
        return 0;
    }

    item *mag = magazine_current();
    if( mag ) {
        const int res = mag->ammo_consume( qty, pos );
        if( res && ammo_remaining() == 0 ) {
            if( mag->has_flag( flag_MAG_DESTROY ) ) {
                remove_item( *mag );
            } else if( mag->has_flag( flag_MAG_EJECT ) ) {
                get_map().add_item( pos, remove_item( *mag ) );
            }
        }
        return res;
    }

    if( is_magazine() ) {
        int need = qty;
        while( !contents.empty() ) {
            item &e = contents.front();
            if( need >= e.charges ) {
                need -= e.charges;
                remove_item( contents.front() );
                e.destroy();
            } else {
                e.charges -= need;
                need = 0;
                break;
            }
        }
        return qty - need;

    } else if( is_tool() || is_gun() ) {
        qty = std::min( qty, charges );
        if( has_flag( flag_USES_BIONIC_POWER ) ) {
            avatar &you = get_avatar();
            charges = units::to_kilojoule( you.get_power_level() );
            you.mod_power_level( units::from_kilojoule( -qty ) );
        }
        charges -= qty;
        if( charges == 0 ) {
            curammo = nullptr;
        }
        return qty;
    }

    return 0;
}

const itype *item::ammo_data() const
{
    const item *mag = magazine_current();
    if( mag ) {
        return mag->ammo_data();
    }

    if( is_ammo() ) {
        return type;
    }

    if( is_magazine() ) {
        return !contents.empty() ? contents.front().ammo_data() : nullptr;
    }

    auto mods = is_gun() ? gunmods() : toolmods();
    for( const item *e : mods ) {
        if( !e->type->mod->ammo_modifier.empty() && e->ammo_current() &&
            e->ammo_current().is_valid() ) {
            return &*e->ammo_current();
        }
    }

    return curammo;
}

itype_id item::ammo_current() const
{
    const itype *ammo = ammo_data();
    return ammo ? ammo->get_id() : itype_id::NULL_ID();
}

const std::set<ammotype> &item::ammo_types( bool conversion ) const
{
    if( conversion ) {
    const std::vector<const item *> &mods = is_gun() ? gunmods() : toolmods();
        for( const item *e : mods ) {
            if( !e->type->mod->ammo_modifier.empty() ) {
                return e->type->mod->ammo_modifier;
            }
        }
    }

    if( is_gun() ) {
        return type->gun->ammo;
    } else if( is_tool() ) {
        return type->tool->ammo_id;
    } else if( is_magazine() ) {
        return type->magazine->type;
    }

    static std::set<ammotype> atypes = {};
    return atypes;
}

ammotype item::ammo_type() const
{
    if( is_ammo() ) {
    return type->ammo->type;
}
return ammotype::NULL_ID();
}

itype_id item::ammo_default( bool conversion ) const
{
    if( is_magazine() ) {
    return type->magazine->default_ammo;
} else if( is_tool() && type->tool->default_ammo != itype_id::NULL_ID() ) {
    return type->tool->default_ammo;
}

const std::set<ammotype> &atypes = ammo_types( conversion );
if( !atypes.empty() ) {
    itype_id res = ammotype( *atypes.begin() )->default_ammotype();
        if( !res.is_empty() ) {
            return res;
        }
    }
    return itype_id::NULL_ID();
}

itype_id item::common_ammo_default( bool conversion ) const
{
    if( !ammo_types( conversion ).empty() ) {
    for( const ammotype &at : ammo_types( conversion ) ) {
            const item *mag = magazine_current();
            if( mag && mag->type->magazine->type.contains( at ) ) {
                itype_id res = at->default_ammotype();
                if( !res.is_empty() ) {
                    return res;
                }
            }
        }
    }
    return itype_id::NULL_ID();
}

std::set<ammo_effect_str_id> item::ammo_effects( bool with_ammo ) const
{
    std::set<ammo_effect_str_id> res;

    if( !type->ammo_effects.empty() ) {
        res.insert( type->ammo_effects.begin(), type->ammo_effects.end() );
    }

    if( !is_gun() ) {
        return res;
    }

    res.insert( type->gun->ammo_effects.begin(), type->gun->ammo_effects.end() );
    if( with_ammo && ammo_data() ) {
        res.insert( ammo_data()->ammo->ammo_effects.begin(), ammo_data()->ammo->ammo_effects.end() );
    }

    for( const item *mod : gunmods() ) {
        res.insert( mod->type->gunmod->ammo_effects.begin(), mod->type->gunmod->ammo_effects.end() );
    }

    return res;
}

std::string item::ammo_sort_name() const
{
    if( is_magazine() || is_gun() || is_tool() ) {
    const std::set<ammotype> &types = ammo_types();
        if( !types.empty() ) {
            return ammotype( *types.begin() )->name();
        }
    }
    if( is_ammo() ) {
    return ammo_type()->name();
    }
    return "";
}

bool item::magazine_integral() const
{
    // If it has a default magazine, it can't have an integral magazine.
    if( magazine_default() ) {
    return false;
} else if( is_gun() ) {
    // We have an integral magazine if we're a gun with an ammo capacity (clip)
    return type->gun->clip;
} else if( is_tool() ) {
    // Or we are a tool with max_charges defined
    return type->tool->max_charges;
}
return true;
}

itype_id item::magazine_default( bool conversion ) const
{
    if( !ammo_types( conversion ).empty() ) {
    if( conversion ) {
            for( const item *m : is_gun() ? gunmods() : toolmods() ) {
                if( !m->type->mod->magazine_adaptor.empty() ) {
                    auto mags = m->type->mod->magazine_adaptor.find( ammotype( *ammo_types( conversion ).begin() ) );
                    if( mags != m->type->mod->magazine_adaptor.end() &&
                        !( *mags->second.begin() )->has_flag( flag_SPEEDLOADER ) ) {
                        return *( mags->second.begin() );
                    }
                }
            }
        }
        auto mag = type->magazine_default.find( ammotype( *ammo_types( conversion ).begin() ) );
        if( mag != type->magazine_default.end() && !mag->second->has_flag( flag_SPEEDLOADER ) ) {
            return mag->second;
        }
    }
    return itype_id::NULL_ID();
}

std::set<itype_id> item::magazine_compatible( bool conversion ) const
{
    std::set<itype_id> mags = {};
    // mods that define magazine_adaptor may override the items usual magazines
    const std::vector<const item *> &mods = is_gun() ? gunmods() : toolmods();
    for( const item *m : mods ) {
        if( !m->type->mod->magazine_adaptor.empty() ) {
            for( const ammotype &atype : ammo_types( conversion ) ) {
                if( m->type->mod->magazine_adaptor.contains( atype ) ) {
                    std::set<itype_id> magazines_for_atype = m->type->mod->magazine_adaptor.find( atype )->second;
                    mags.insert( magazines_for_atype.begin(), magazines_for_atype.end() );
                }
            }
            return mags;
        }
    }

    for( const ammotype &atype : ammo_types( conversion ) ) {
        if( type->magazines.contains( atype ) ) {
            std::set<itype_id> magazines_for_atype = type->magazines.find( atype )->second;
            mags.insert( magazines_for_atype.begin(), magazines_for_atype.end() );
        }
    }
    return mags;
}

item *item::magazine_current()
{
    return contents.get_item_with(
    []( const item & it ) {
        return it.is_magazine();
    } );
}

const item *item::magazine_current() const
{
    return const_cast<item *>( this )->magazine_current();
}

std::vector<item *> item::gunmods()
{
    return contents.gunmods();
}

std::vector<const item *> item::gunmods() const
{
    return contents.gunmods();
}

item *item::gunmod_find( const itype_id &mod )
{
    std::vector<item *> mods = gunmods();
    auto it = std::ranges::find_if( mods, [&mod]( item * e ) {
        return e->typeId() == mod;
    } );
    return it != mods.end() ? *it : nullptr;
}

const item *item::gunmod_find( const itype_id &mod ) const
{
    return const_cast<item *>( this )->gunmod_find( mod );
}

ret_val<bool> item::is_gunmod_compatible( const item &mod ) const
{
    if( !mod.is_gunmod() ) {
    debugmsg( "Tried checking compatibility of non-gunmod" );
        return ret_val<bool>::make_failure();
    }
    const islot_gunmod &g_mod = *mod.type->gunmod;

    if( !is_gun() ) {
    return ret_val<bool>::make_failure( _( "isn't a weapon" ) );

    } else if( is_gunmod() ) {
    return ret_val<bool>::make_failure( _( "is a gunmod and cannot be modded" ) );

    } else if( gunmod_find( mod.typeId() ) ) {
        return ret_val<bool>::make_failure( _( "already has a %s" ), mod.tname( 1 ) );

    } else if( !get_mod_locations().contains( g_mod.location ) ) {
    return ret_val<bool>::make_failure( _( "doesn't have a slot for this mod" ) );

    } else if( get_free_mod_locations( g_mod.location ) <= 0 ) {
    return ret_val<bool>::make_failure( _( "doesn't have enough room for another %s mod" ),
                                        mod.type->gunmod->location.name() );

    } else if( !g_mod.usable.empty() || !g_mod.usable_category.empty() || !g_mod.exclusion.empty() ||
               !g_mod.exclusion_category.empty() ) {
    // First check that it's not explicitly excluded by id.
    bool excluded = g_mod.exclusion.contains( this->typeId() );
        // Then check if it's excluded by category.
        for( const std::unordered_set<weapon_category_id> &mod_cat : g_mod.exclusion_category ) {
            if( excluded ) {
                break;
            }
            if( std::ranges::all_of( mod_cat, [this]( const weapon_category_id & wcid ) {
            return this->type->weapon_category.count( wcid );
            } ) ) {
                excluded = true;
            }
        }

        // Check that it's included by id, if so, override banned so it's allowed.
        // A check is already in item_factory so that explicit inclusion and exclusion of the same id throws errors.
        bool usable = g_mod.usable.contains( this->typeId() );
        if( usable ) {
            excluded = false;
        }
        // Then check that it's included by category. If banned is still true, skip, no point checking.
        for( const std::unordered_set<weapon_category_id> &mod_cat : g_mod.usable_category ) {
            if( usable || excluded ) {
                break;
            }
            if( std::ranges::all_of( mod_cat, [this]( const weapon_category_id & wcid ) {
            return this->type->weapon_category.count( wcid );
            } ) ) {
                usable = true;
            }
        }
        if( !usable || excluded ) {
            return ret_val<bool>::make_failure( _( "cannot have a %s" ), mod.tname() );
        }

    } else if( g_mod.location.str() == "underbarrel" &&
               !mod.has_flag( flag_PUMP_RAIL_COMPATIBLE ) && has_flag( flag_PUMP_ACTION ) ) {
    return ret_val<bool>::make_failure( _( "can only accept small mods on that slot" ) );

    } else if( !mod.type->mod->acceptable_ammo.empty() ) {
    bool compat_ammo = false;
    for( const ammotype &at : mod.type->mod->acceptable_ammo ) {
            if( ammo_types( false ).contains( at ) ) {
                compat_ammo = true;
            }
        }
        if( !compat_ammo ) {
            return ret_val<bool>::make_failure(
                       _( "%1$s cannot be used on item with no compatible ammo types" ), mod.tname( 1 ) );
        }
    } else if( mod.typeId() == itype_waterproof_gunmod && has_flag( flag_WATERPROOF_GUN ) ) {
    return ret_val<bool>::make_failure( _( "is already waterproof" ) );

    } else if( mod.typeId() == itype_tuned_mechanism && has_flag( flag_NEVER_JAMS ) ) {
    return ret_val<bool>::make_failure( _( "is already eminently reliable" ) );

    } else if( mod.has_flag( flag_BRASS_CATCHER ) && has_flag( flag_RELOAD_EJECT ) ) {
    return ret_val<bool>::make_failure( _( "cannot have a brass catcher" ) );

    } else if( ( !mod.type->mod->ammo_modifier.empty() || !mod.type->mod->magazine_adaptor.empty() )
                   && ( ammo_remaining() > 0 || magazine_current() ) ) {
        return ret_val<bool>::make_failure( _( "must be unloaded before installing this mod" ) );
    }

for( const gunmod_location &slot : mod.type->gunmod->blacklist_mod ) {
    if( get_mod_locations().contains( slot ) ) {
            return ret_val<bool>::make_failure( _( "cannot be installed on a weapon with \"%s\"" ),
                                                slot.name() );
        }
    }

    return ret_val<bool>::make_success();
}

std::map<gun_mode_id, gun_mode> item::gun_all_modes() const
{
    std::map<gun_mode_id, gun_mode> res;

    if( !is_gun() || is_gunmod() ) {
        return res;
    }

    std::vector<const item *> opts = gunmods();
    opts.push_back( this );

    for( const item *e : opts ) {

        // handle base item plus any auxiliary gunmods
        if( e->is_gun() ) {
            for( const std::pair<const gun_mode_id, gun_modifier_data> &m : e->type->gun->modes ) {
                // prefix attached gunmods, e.g. M203_DEFAULT to avoid index key collisions
                std::string prefix = e->is_gunmod() ? ( std::string( e->typeId() ) += "_" ) : "";
                std::ranges::transform( prefix, prefix.begin(),
                                        static_cast<int( * )( int )>( toupper ) );

                const int qty = m.second.qty();

                res.emplace( gun_mode_id( prefix + m.first.str() ), gun_mode( m.second.name(),
                             const_cast<item *>( e ),
                             qty, m.second.flags() ) );
            }

            // non-auxiliary gunmods may provide additional modes for the base item
        } else if( e->is_gunmod() ) {
            for( const std::pair<const gun_mode_id, gun_modifier_data> &m : e->type->gunmod->mode_modifier ) {
                //checks for melee gunmod, points to gunmod
                if( m.first == gun_mode_REACH ) {
                    res.emplace( m.first, gun_mode { m.second.name(), const_cast<item *>( e ),
                                                     m.second.qty(), m.second.flags() } );
                    //otherwise points to the parent gun, not the gunmod
                } else {
                    res.emplace( m.first, gun_mode { m.second.name(), const_cast<item *>( this ),
                                                     m.second.qty(), m.second.flags() } );
                }
            }
        }
    }

    return res;
}

gun_mode item::gun_get_mode( const gun_mode_id &mode ) const
{
    if( is_gun() ) {
    for( const std::pair<const gun_mode_id, gun_mode> &e : gun_all_modes() ) {
            if( e.first == mode ) {
                return e.second;
            }
        }
    }
    return gun_mode();
}

gun_mode item::gun_current_mode() const
{
    return gun_get_mode( gun_get_mode_id() );
}

gun_mode_id item::gun_get_mode_id() const
{
    if( !is_gun() || is_gunmod() ) {
    return gun_mode_id();
    }
    return gun_mode_id( get_var( GUN_MODE_VAR_NAME, "DEFAULT" ) );
}

bool item::gun_set_mode( const gun_mode_id &mode )
{
    if( !is_gun() || is_gunmod() || !gun_all_modes().contains( mode ) ) {
        return false;
    }
    set_var( GUN_MODE_VAR_NAME, mode.str() );
    return true;
}

void item::gun_cycle_mode()
{
    if( !is_gun() || is_gunmod() ) {
        return;
    }

    const gun_mode_id cur = gun_get_mode_id();
    const std::map<gun_mode_id, gun_mode> modes = gun_all_modes();

    const auto current_mode = std::ranges::find( modes, cur, []( const auto & pair ) {
        return pair.first;
    } );
    if( current_mode != modes.end() ) {
        const auto next_mode = std::next( current_mode );
        if( next_mode != modes.end() ) {
            gun_set_mode( next_mode->first );
            return;
        }
    }
    gun_set_mode( modes.begin()->first );

    return;
}

bool item::has_use() const
{
    return type->has_use();
}

const use_function *item::get_use( const std::string &use_name ) const
{
    const use_function *fun = nullptr;
    visit_items(
    [&fun, &use_name]( const item * it ) {
        if( it == nullptr ) {
            return VisitResponse::SKIP;
        }
        fun = it->get_use_internal( use_name );
        if( fun != nullptr ) {
            return VisitResponse::ABORT;
        }
        return VisitResponse::NEXT;
    } );

    return fun;
}

const use_function *item::get_use_internal( const std::string &use_name ) const
{
    if( type != nullptr ) {
        return type->get_use( use_name );
    }
    return nullptr;
}

const item *item::get_usable_item( const std::string &use_name ) const
{
    const item *ret = nullptr;
    visit_items(
    [&ret, &use_name]( const item * it ) {
        if( it == nullptr ) {
            return VisitResponse::SKIP;
        }
        if( it->get_use_internal( use_name ) ) {
            ret = it;
            return VisitResponse::ABORT;
        }
        return VisitResponse::NEXT;
    } );

    return ret;
}

item *item::get_usable_item( const std::string &use_name )
{
    return const_cast<item *>( const_cast<const item *>( this )->get_usable_item( use_name ) );
}

int item::units_remaining( const Character &ch, int limit ) const
{
    if( count_by_charges() ) {
    return std::min( static_cast<int>( charges ), limit );
    }

    int res = ammo_remaining();
    if( res < limit && is_power_armor() ) {
    if( character_funcs::can_interface_armor( ch ) && has_flag( flag_USE_UPS ) ) {
            res += std::max( ch.charges_of( itype_UPS, limit - res ), ch.charges_of( itype_bio_armor,
                             limit - res ) );
        } else if( character_funcs::can_interface_armor( ch ) ) {
            res += ch.charges_of( itype_bio_armor, limit - res );
        } else {
            res += ch.charges_of( itype_UPS, limit - res );
        }
    } else if( res < limit && has_flag( flag_USE_UPS ) ) {
    res += ch.charges_of( itype_UPS, limit - res );
    }

    return std::min( res, limit );
}

bool item::units_sufficient( const Character &ch, int qty ) const
{
    if( qty < 0 ) {
    qty = count_by_charges() ? 1 : ammo_required();
    }

    return units_remaining( ch, qty ) == qty;
}

item_reload_option::item_reload_option( const item_reload_option & ) = default;

item_reload_option &item_reload_option::operator=( const item_reload_option & ) = default;

item_reload_option::item_reload_option( const player *who, item *target, const item *parent,
                                        item &ammo ) :
    who( who ), target( target ), ammo( &ammo ), parent( parent )
{
    if( this->target->is_ammo_belt() ) {
        const auto &linkage = this->target->type->magazine->linkage ;
        if( linkage ) {
            max_qty = this->who->charges_of( *linkage );
        }
    }
    qty( max_qty );
}

int item_reload_option::moves() const
{
    int mv = ammo->obtain_cost( *who, qty() ) + who->item_reload_cost( *target, *ammo, qty() );
    if( parent != target ) {
        if( parent->is_gun() ) {
            mv += parent->get_reload_time();
        } else if( parent->is_tool() ) {
            mv += 100;
        }
    }
    return mv;
}

void item_reload_option::qty( int val )
{
    bool ammo_in_ammo_container = ammo->is_ammo_container();
    bool ammo_in_container = ammo->is_container();
    item &ammo_obj = ( ammo_in_ammo_container || ammo_in_container ) ?
                     ammo->contents.front() : *ammo;

    if( ammo_in_ammo_container && !ammo_obj.is_ammo() ) {
        debugmsg( "Invalid reload option: %s", ammo_obj.tname() );
        return;
    }

    // Checking ammo capacity implicitly limits guns with removable magazines to capacity 0.
    // This gets rounded up to 1 later.
    int remaining_capacity = 0;
    if( target->is_watertight_container() && ammo_obj.made_of( LIQUID ) ) {
        remaining_capacity = target->get_remaining_capacity_for_liquid( ammo_obj, true );
    } else if( target->is_container() && ammo_obj.is_comestible() ) {
        remaining_capacity = ammo_obj.charges_per_volume( target->get_container_capacity() );
        if( !target->is_container_empty() ) {
            remaining_capacity -= target->ammo_remaining();
        }
    } else {
        remaining_capacity = target->ammo_capacity() - target->ammo_remaining();
    }
    if( target->has_flag( flag_RELOAD_ONE ) && !ammo->has_flag( flag_SPEEDLOADER ) ) {
        remaining_capacity = 1;
    }
    if( ammo_obj.type->ammo ) {
        if( ammo_obj.ammo_type() == ammo_plutonium ) {
            remaining_capacity = remaining_capacity / PLUTONIUM_CHARGES +
                                 ( remaining_capacity % PLUTONIUM_CHARGES != 0 );
        }
    }

    bool ammo_by_charges = ammo_obj.is_ammo() || ammo_in_container || ammo->is_comestible();
    int available_ammo = ammo_by_charges ? ammo_obj.charges : ammo_obj.ammo_remaining();
    // constrain by available ammo, target capacity and other external factors (max_qty)
    // @ref max_qty is currently set when reloading ammo belts and limits to available linkages
    qty_ = std::min( { val, available_ammo, remaining_capacity, max_qty } );

    // always expect to reload at least one charge
    qty_ = std::max( qty_, 1 );

}

int item::casings_count() const
{
    int res = 0;

    const_cast<item *>( this )->casings_handle( [&res]( detached_ptr<item> &&it ) {
        ++res;
        return std::move( it );
    } );

    return res ;
}

void item::casings_handle( const std::function < detached_ptr<item>( detached_ptr<item> && ) >
                           &func )
{
    if( !is_gun() ) {
        return;
    }

    contents.casings_handle( func );
}

bool item::reload( Character &who, item &loc, int qty )
{
    if( qty <= 0 ) {
        debugmsg( "Tried to reload zero or less charges" );
        return false;
    }
    item *ammo = &loc;
    if( ammo->is_null() ) {
        debugmsg( "Tried to reload using non-existent ammo" );
        return false;
    }

    item *container = nullptr;
    if( ammo->is_ammo_container() || ammo->is_container() ) {
        container = ammo;
        ammo = &ammo->contents.front();
    }

    if( !is_reloadable_with( ammo->typeId() ) ) {
        return false;
    }

    // limit quantity of ammo loaded to remaining capacity
    int limit = 0;
    if( is_watertight_container() && ammo->made_of( LIQUID ) ) {
        limit = get_remaining_capacity_for_liquid( *ammo, true );
    } else if( is_container() && ammo->is_comestible() ) {
        limit = ammo->charges_per_volume( get_container_capacity() );
        if( !is_container_empty() ) {
            limit -= ammo_remaining();
        }
    } else {
        limit = ammo_capacity() - ammo_remaining();
    }

    if( ammo->ammo_type() == ammo_plutonium ) {
        limit = limit / PLUTONIUM_CHARGES + ( limit % PLUTONIUM_CHARGES != 0 );
    }

    qty = std::min( qty, limit );

    // Lua iranged can_reload callback: blocks reloading before ammo is consumed
    if( const auto *iranged_cb = type->iranged_callbacks ) {
        if( !iranged_cb->call_can_reload( who, *this ) ) {
            return false;
        }
    }

    casings_handle( [&who]( detached_ptr<item> &&e ) {
        return who.i_add_or_drop( std::move( e ) );
    } );

    if( is_magazine() ) {
        qty = std::min( qty, ammo->charges );

        if( is_ammo_belt() ) {
            const auto &linkage = type->magazine->linkage;
            if( linkage && !who.use_charges_if_avail( *linkage, qty ) ) {
                debugmsg( "insufficient linkages available when reloading ammo belt" );
            }
        }

        detached_ptr<item> to_reload = ammo->split( qty );
        bool merged = false;
        for( item *it : contents.all_items_top() ) {
            if( it->merge_charges( std::move( to_reload ) ) ) {
                merged = true;
                break;
            }
        }
        if( !merged ) {
            // NOLINTNEXTLINE(bugprone-use-after-move)
            put_in( std::move( to_reload ) );
        }
    } else if( is_container() ) {
        if( container ) {
            container->on_contents_changed();
        }
        item &cur = *this;
        ammo->attempt_split( 0, [&cur, qty]( detached_ptr<item> &&it ) {
            return cur.fill_with( std::move( it ), qty );
        } );
    } else if( !magazine_integral() ) {
        // if we already have a magazine loaded prompt to eject it
        if( magazine_current() ) {
            //~ %1$s: magazine name, %2$s: weapon name
            std::string prompt = string_format( pgettext( "magazine", "Eject %1$s from %2$s?" ),
                                                magazine_current()->tname(), tname() );

            if( !who.dispose_item( *magazine_current(), prompt ) ) {
                return false;
            }
        }

        put_in( ammo->detach() );
        return true;

    } else {
        if( ammo->has_flag( flag_SPEEDLOADER ) ) {
            curammo = ammo->contents.front().type;
            qty = std::min( qty, ammo->ammo_remaining() );
            ammo->ammo_consume( qty, tripoint_bub_ms::zero() );
            charges += qty;
        } else if( ammo->ammo_type() == ammo_plutonium ) {
            curammo = ammo->type;
            ammo->charges -= qty;

            // any excess is wasted rather than overfilling the item
            charges += qty * PLUTONIUM_CHARGES;
            charges = std::min( charges, ammo_capacity() );
        } else {
            curammo = ammo->type;
            qty = std::min( qty, ammo->charges );
            ammo->charges -= qty;
            charges += qty;
        }
        // we have transfered ammo from the container to the item
        // therefore, we erase the 0-charge item inside container
        // TODO: why don't we just remove 0-charge items?
        if( ammo->charges == 0 && !ammo->has_flag( flag_SPEEDLOADER ) ) {
            ammo->detach();
            if( container != nullptr ) {
                who.inv_restack();
            }
        }
    }

    if( type->iranged_callbacks ) {
        type->iranged_callbacks->call_on_reload( who, *this );
    }

    return true;
}

float item::simulate_burn( fire_data &frd ) const
{
    const std::vector<material_id> &mats = made_of();
    float smoke_added = 0.0f;
    float time_added = 0.0f;
    float burn_added = 0.0f;
    const units::volume vol = base_volume();
    const int effective_intensity = frd.contained ? 3 : frd.fire_intensity;
    for( const material_id &m : mats ) {
        const mat_burn_data &bd = m.obj().burn_data( effective_intensity );
        if( bd.immune ) {
            // Made to protect from fire
            return 0.0f;
        }

        // If fire is contained, burn rate is independent of volume
        if( frd.contained || bd.volume_per_turn == 0_ml ) {
            time_added += bd.fuel;
            smoke_added += bd.smoke;
            burn_added += bd.burn;
        } else {
            double volume_burn_rate = to_liter( bd.volume_per_turn ) / to_liter( vol );
            time_added += bd.fuel * volume_burn_rate;
            smoke_added += bd.smoke * volume_burn_rate;
            burn_added += bd.burn * volume_burn_rate;
        }
    }

    // Liquids that don't burn well smother fire well instead
    if( made_of( LIQUID ) && time_added < 200 ) {
        time_added -= rng( 400.0 * to_liter( vol ), 1200.0 * to_liter( vol ) );
    } else if( mats.size() > 1 ) {
        // Average the materials
        time_added /= mats.size();
        smoke_added /= mats.size();
        burn_added /= mats.size();
    } else if( mats.empty() ) {
        // Non-liquid items with no specified materials will burn at moderate speed
        burn_added = 1;
    }

    if( count_by_charges() ) {
        const int stack_burnt = type->stack_size;
        time_added *= stack_burnt;
        smoke_added *= stack_burnt;
        burn_added *= stack_burnt;
    }

    frd.fuel_produced += time_added;
    frd.smoke_produced += smoke_added;
    return burn_added;
}

bool item::burn( fire_data &frd )
{
    float burn_added = simulate_burn( frd );

    if( burn_added <= 0 ) {
        return false;
    }

    if( count_by_charges() ) {
        if( type->volume == 0_ml ) {
            charges = 0;
        } else {
            charges -= roll_remainder( burn_added * units::legacy_volume_factor * type->stack_size /
                                       ( 3.0 * type->volume ) );
        }

        return charges <= 0;
    }

    if( is_corpse() ) {
        const mtype *mt = get_mtype();
        if( is_active() && mt != nullptr && burnt + burn_added > mt->hp &&
            !mt->burn_into.is_null() && mt->burn_into.is_valid() ) {
            corpse = &get_mtype()->burn_into.obj();
            // Delay rezing
            set_age( 0_turns );
            burnt = 0;
            return false;
        }
    }

    burnt += roll_remainder( burn_added );

    const int vol = base_volume() / units::legacy_volume_factor;
    return burnt >= vol * 3;
}

bool item::flammable( int threshold ) const
{
    const std::vector<const material_type *> &mats = made_of_types();
    if( mats.empty() ) {
        // Don't know how to burn down something made of nothing.
        return false;
    }

    int flammability = 0;
    units::volume volume_per_turn = 0_ml;
    for( const material_type *m : mats ) {
        const mat_burn_data &bd = m->burn_data( 1 );
        if( bd.immune ) {
            // Made to protect from fire
            return false;
        }

        flammability += bd.fuel;
        volume_per_turn += bd.volume_per_turn;
    }

    if( threshold == 0 || flammability <= 0 ) {
        return flammability > 0;
    }

    volume_per_turn /= mats.size();
    units::volume vol = base_volume();
    if( volume_per_turn > 0_ml && volume_per_turn < vol ) {
        flammability = flammability * volume_per_turn / vol;
    } else {
        // If it burns well, it provides a bonus here
        flammability *= vol / units::legacy_volume_factor;
    }

    return flammability > threshold;
}

const itype_id &item::typeId() const
{
    return type ? type->get_id() : itype_id::NULL_ID();
}

bool item::getlight( float &luminance, units::angle &width, units::angle &direction ) const
{
    luminance = 0;
    width = 0_degrees;
    direction = 0_degrees;
    if( light.luminance > 0 ) {
        luminance = static_cast<float>( light.luminance );
        if( light.width > 0 ) {  // width > 0 is a light arc
            width = units::from_degrees( light.width );
            direction = units::from_degrees( light.direction );
        }
        return true;
    } else {
        const int lumint = getlight_emit();
        if( lumint > 0 ) {
            luminance = static_cast<float>( lumint );
            return true;
        }
    }
    return false;
}

int item::getlight_emit() const
{
    float lumint = type->light_emission;

    if( lumint == 0 ) {
        return 0;
    }
    if( has_flag( flag_CHARGEDIM ) && is_tool() && !has_flag( flag_USE_UPS ) ) {
        // Falloff starts at 1/5 total charge and scales linearly from there to 0.
        if( ammo_capacity() && ammo_remaining() < ( ammo_capacity() / 5 ) ) {
            lumint *= ammo_remaining() * 5.0 / ammo_capacity();
        }
    }
    return lumint;
}

units::volume item::get_container_capacity() const
{
    if( !is_container() ) {
    return 0_ml;
}
return type->container->contains;
}

units::volume item::get_total_capacity() const
{
    units::volume result = get_storage() + get_container_capacity();

    // Consider various iuse_actors which add containing capability
    // Treating these two as special cases for now; if more appear in the
    // future then this probably warrants a new method on use_function to
    // access this information generically.
    if( is_bandolier() ) {
        result += dynamic_cast<const bandolier_actor *>
                  ( type->get_use( "bandolier" )->get_actor_ptr() )->max_stored_volume();
    }

    if( is_holster() ) {
        result += dynamic_cast<const holster_actor *>
                  ( type->get_use( "holster" )->get_actor_ptr() )->max_stored_volume();
    }

    return result;
}

int item::get_remaining_capacity_for_liquid( const item &liquid, bool allow_bucket,
        std::string *err ) const
{
    const auto error = [ &err ]( const std::string & message ) {
        if( err != nullptr ) {
            *err = message;
        }
        return 0;
    };

    int remaining_capacity = 0;

    // TODO: (sm) is_reloadable_with and this function call each other and can recurse for
    // watertight containers.
    if( !is_container() && is_reloadable_with( liquid.typeId() ) ) {
        if( ammo_remaining() != 0 && ammo_current() != liquid.typeId() ) {
            return error( string_format( _( "You can't mix loads in your %s." ), tname() ) );
        }
        remaining_capacity = ammo_capacity() - ammo_remaining();
    } else if( is_container() ) {
        if( !type->container->watertight && liquid.made_of( LIQUID ) ) {
            return error( string_format( _( "That %s isn't water-tight." ), tname() ) );
        } else if( !type->container->seals && ( !allow_bucket || !is_bucket() ) ) {
            return error( string_format( is_bucket() ?
                                         _( "That %s must be on the ground or held to hold contents!" )
                                         : _( "You can't seal that %s!" ), tname() ) );
        } else if( !contents.empty() && contents.front().typeId() != liquid.typeId() ) {
            return error( string_format( _( "You can't mix loads in your %s." ), tname() ) );
        }
        remaining_capacity = liquid.charges_per_volume( get_container_capacity() );
        if( !contents.empty() ) {
            remaining_capacity -= contents.front().charges;
        }
    } else {
        return error( string_format( _( "That %1$s won't hold %2$s." ), tname(),
                                     liquid.tname() ) );
    }

    if( remaining_capacity <= 0 ) {
        return error( string_format( _( "Your %1$s can't hold any more %2$s." ), tname(),
                                     liquid.tname() ) );
    }

    return remaining_capacity;
}

int item::get_remaining_capacity_for_liquid( const item &liquid, const Character &who,
        std::string *err ) const
{
    const bool allow_bucket = who.is_wielding( *this ) || !who.has_item( *this );
    int res = get_remaining_capacity_for_liquid( liquid, allow_bucket, err );

    if( res > 0 && !type->rigid && who.has_item( *this ) ) {
        const units::volume volume_to_expand = std::max( who.volume_capacity() - who.volume_carried(),
                                               0_ml );

        res = std::min( liquid.charges_per_volume( volume_to_expand ), res );

        if( res == 0 && err != nullptr ) {
            *err = string_format( _( "That %s doesn't have room to expand." ), tname() );
        }
    }

    return res;
}

int item::get_remaining_capacity_for_id( const itype_id &liquid, bool allow_buckets ) const
{
    int rem_cap = 0;
    itype obj = liquid.obj();
    if( !is_container() && is_reloadable_with( liquid ) ) {
        if( ammo_remaining() != 0 && ammo_current() != liquid ) {
            return 0;
        }
        rem_cap = ammo_capacity() - ammo_remaining();
    } else if( is_container() ) {
        if( !type->container->watertight ) {
            return 0;
        } else if( !type->container->seals && ( !allow_buckets || !is_bucket() ) ) {
            return 0;
        } else if( !contents.empty() && contents.front().typeId() != liquid ) {
            return 0;
        }
        rem_cap = obj.charges_per_volume( get_container_capacity() );
        if( !contents.empty() ) {
            rem_cap -= contents.front().charges;
        }
    }
    return rem_cap;
}

detached_ptr<item> item::use_amount( detached_ptr<item> &&self, const itype_id &it, int &quantity,
                                     std::vector<detached_ptr<item>> &used,
                                     const std::function<bool( const item & )> &filter )
{
    // Remember quantity so that we can unseal self
    int old_quantity = quantity;

    self->remove_items_with( [&]( detached_ptr<item> &&a ) {
        if( quantity > 0  && a->typeId() == it && filter( *a ) ) {
            used.push_back( std::move( a ) );
            quantity--;
            return VisitResponse::SKIP;
        }
        return VisitResponse::NEXT;
    } );

    if( quantity != old_quantity ) {
        self->on_contents_changed();
    }

    if( quantity > 0 && self->typeId() == it && filter( *self ) ) {
        used.push_back( std::move( self ) );
        quantity--;
        return detached_ptr<item>();
    }
    return std::move( self );
}


bool item::allow_crafting_component() const
{
    if( is_toolmod() && is_irremovable() ) {
    return false;
}

// vehicle batteries are implemented as magazines of charge
if( is_magazine() && ammo_types().contains( ammo_battery ) ) {
    return true;
}

// fixes #18886 - turret installation may require items with irremovable mods
if( is_gun() ) {
    bool valid = true;
    visit_items( [&]( const item * it ) {
        if( this == it ) {
                return VisitResponse::NEXT;
            }
            if( !( it->is_magazine() || ( it->is_gunmod() && it->is_irremovable() ) ) ) {
                valid = false;
                return VisitResponse::ABORT;
            }
            return VisitResponse::NEXT;
        } );
        return valid;
    }

    return contents.empty();
}

detached_ptr<item> item::fill_with( detached_ptr<item> &&liquid, int amount )
{
    if( amount == -1 ) {
        amount = INT_MAX;
    }
    amount = std::min( get_remaining_capacity_for_liquid( *liquid, true ),
                       std::min( amount, liquid->charges ) );
    if( amount <= 0 ) {
        return std::move( liquid );
    }

    if( !is_container() ) {
        if( !is_reloadable_with( liquid->typeId() ) ) {
            debugmsg( "Tried to fill %s which is not a container and can't be reloaded with %s.",
                      tname(), liquid->tname() );
            return std::move( liquid );
        }
        ammo_set( liquid->typeId(), ammo_remaining() + amount );
    } else if( is_food_container() ) {
        item &cts = contents.front();

        cts.set_rot( weighted_averaged_rot( &cts, &*liquid ) );
        cts.mod_charges( amount );
    } else if( !is_container_empty() ) {
        // if container already has liquid we need to set the amount
        item &cts = contents.front();
        cts.mod_charges( amount );
    } else {
        detached_ptr<item> liquid_copy = item::spawn( *liquid );
        liquid_copy->charges = amount;
        put_in( std::move( liquid_copy ) );
    }

    liquid->mod_charges( -amount );
    on_contents_changed();
    if( liquid->charges > 0 ) {
        return std::move( liquid );
    }
    return detached_ptr<item>();
}

void item::set_counter( const int value )
{
    item_counter = value;
}

int item::get_counter() const
{
    return item_counter;
}

bool item::has_explicit_turn_timer() const
{
    return is_active() && item_counter > 0 && type->countdown_interval > 0;
}

bool item::has_countdown_timer_type() const
{
    return type->countdown_interval > 0;
}

void item::advance_timer( int n )
{
    if( !has_explicit_turn_timer() || n <= 0 ) {
        return;
    }
    // Decrement counter, clamping at 0.  The countdown_action is deliberately
    // NOT fired here — it will trigger on the next normal process_items() call
    // when the submap re-enters the reality bubble, avoiding side-effects in
    // out-of-bubble context (explosions, spawns, etc.).
    item_counter = std::max( 0, item_counter - n );
}

void item::set_charges( int value )
{
    if( value < 0 ) {
        debugmsg( "Tried to set a negative charges value %d.", value );
        return;
    }
    if( !ammo_types().empty() ) {
        debugmsg( "Tried to set charges on an item with ammo." );
        return;
    }
    charges = value;
}

detached_ptr<item> item::use_charges( detached_ptr<item> &&self, const itype_id &what, int &qty,
                                      std::vector<detached_ptr<item>> &used,
                                      const tripoint_bub_ms &pos, const std::function<bool( const item & )> &filter )
{


    auto handle_item = [&qty, &used, &pos, &what]( detached_ptr<item> &&e ) {
        if( e->is_tool() ) {
            if( e->typeId() == what || ( what == itype_UPS && e->has_flag( flag_IS_UPS ) ) ) {
                int ups_eff_mult = e->type->tool->ups_eff_mult;
                int n = std::min( e->ammo_remaining() * ups_eff_mult, qty );
                int rand_increase = x_in_y( n % ups_eff_mult, ups_eff_mult );
                int really_used = ( n / ups_eff_mult ) + rand_increase;
                qty -= n;

                if( n == e->ammo_remaining() ) {
                    used.push_back( item::spawn( *e ) );
                    e->ammo_consume( really_used, pos );
                } else {
                    detached_ptr<item> split = item::spawn( *e );
                    split->ammo_set( e->ammo_current(), really_used );
                    e->ammo_consume( really_used, pos );
                    used.push_back( std::move( split ) );
                }
            }
        } else if( e->count_by_charges() ) {
            if( e->typeId() == what ) {
                if( e->charges > qty ) {
                    e->charges -= qty;
                    detached_ptr<item> split = item::spawn( *e );
                    split->charges = qty;
                    used.push_back( std::move( split ) );
                    qty = 0;
                } else {
                    qty -= e->charges;
                    used.push_back( std::move( e ) );
                    return detached_ptr<item>();
                }
            }
        }
        return std::move( e );
    };

    item &obj = *self;

    if( qty > 0 && filter( *self ) && self->typeId() == what ) {
        self = handle_item( std::move( self ) );
    }

    obj.remove_items_with( [&qty, &filter, &handle_item]( detached_ptr<item> &&e ) {
        if( qty == 0 ) {
            // found sufficient charges
            return VisitResponse::ABORT;
        }
        if( !filter( *e ) ) {
            return VisitResponse::NEXT;
        }
        item &obj = *e;
        e = handle_item( std::move( e ) );
        if( obj.is_tool() || obj.count_by_charges() ) {
            return VisitResponse::SKIP;
        }
        return VisitResponse::NEXT;
    } );
    return std::move( self );
}

void item::set_snippet( const snippet_id &id )
{
    if( is_null() ) {
        return;
    }
    if( !id.is_valid() ) {
        debugmsg( "there's no snippet with id %s", id.str() );
        return;
    }
    snip_id = id;
}

const std::string &item::get_category_id() const
{
    if( is_container() && !contents.empty() ) {
    return contents.front().get_category().get_id().str();
    }

    static item_category null_category;
    return type->category_force.is_valid() ? type->category_force.obj().get_id().str() :
           null_category.get_id().str();
}

const item_category &item::get_category() const
{
    if( is_container() && !contents.empty() ) {
    return contents.front().get_category();
    }

    static item_category null_category;
    return type->category_force.is_valid() ? type->category_force.obj() : null_category;
}

iteminfo::iteminfo( const std::string &Type, const std::string &Name, const std::string &Fmt,
                    flags Flags, double Value )
{
    sType = Type;
    sName = replace_colors( Name );
    sFmt = replace_colors( Fmt );
    is_int = !( Flags & is_decimal || Flags & is_three_decimal );
    three_decimal = ( Flags & is_three_decimal );
    dValue = Value;
    bShowPlus = static_cast<bool>( Flags & show_plus );
    std::stringstream convert;
    if( bShowPlus ) {
        convert << std::showpos;
    }
    if( is_int ) {
        convert << std::setprecision( 0 );
    } else if( three_decimal ) {
        convert << std::setprecision( 3 );
    } else {
        convert << std::setprecision( 2 );
    }
    convert << std::fixed << Value;
    sValue = convert.str();
    bNewLine = !( Flags & no_newline );
    bLowerIsBetter = static_cast<bool>( Flags & lower_is_better );
    bDrawName = !( Flags & no_name );
}

iteminfo::iteminfo( const std::string &Type, const std::string &Name, double Value )
    : iteminfo( Type, Name, "", no_flags, Value )
{
}

bool item::will_explode_in_fire() const
{
    if( type->explode_in_fire ) {
    return true;
}

if( type->ammo && ( type->ammo->special_cookoff || type->ammo->cookoff ) ) {
        return true;
    }

    // Most containers do nothing to protect the contents from fire
    if( !is_magazine() || !type->magazine->protects_contents ) {
        return has_item_with( [&]( const item & it ) {
            return this != &it && it.will_explode_in_fire();
        } );
    }

    return false;
}

detached_ptr<item> item::detonate( detached_ptr<item> &&self, const tripoint_bub_ms &p,
                                   std::vector<detached_ptr<item>> &drops )
{
    if( self->type->explosion ) {
        explosion_handler::explosion( p, self->type->explosion, self->activated_by );
        return detached_ptr<item>();
    } else if( self->type->ammo && ( self->type->ammo->special_cookoff ||
                                     self->type->ammo->cookoff ) ) {
        int charges_remaining = self->charges;
        const int rounds_exploded = rng( 1, charges_remaining );
        // Yank the exploding item off the map for the duration of the explosion
        // so it doesn't blow itself up.
        const islot_ammo &ammo_type = *self->type->ammo;

        if( ammo_type.special_cookoff ) {
            // If it has a special effect just trigger it.
            apply_ammo_effects( p, ammo_type.ammo_effects, self->activated_by );
        }
        charges_remaining -= rounds_exploded;
        if( charges_remaining > 0 ) {
            detached_ptr<item> temp_item = item::spawn( *self );
            temp_item->charges = charges_remaining;
            drops.push_back( std::move( temp_item ) );
        }

        return detached_ptr<item>();
    } else if( !self->contents.empty() && ( !self->type->magazine ||
                                            !self->type->magazine->protects_contents ) ) {
        bool detonated = false;
        self->contents.remove_top_items_with( [&p, &drops, &detonated]( detached_ptr<item> &&it ) {
            it = detonate( std::move( it ), p, drops );
            if( !it ) {
                detonated = true;
            }
            return std::move( it );
        } );
        if( detonated ) {
            return detached_ptr<item>();
        } else {
            return std::move( self );
        }
    }

    return std::move( self );
}
bool item::has_rotten_away() const
{
    if( is_corpse() && !can_revive() ) {
    return get_rot() > 10_days;
    } else {
        return is_food() && get_relative_rot() > 2.0;
    }
}

detached_ptr<item> item::actualize_rot( detached_ptr<item> &&self, const tripoint_bub_ms &pnt,
                                        temperature_flag temperature,
                                        const weather_manager &weather )
{
    // Guard against null or invalid items that can survive save/load cycles
    // during dimension transitions (e.g. zombie items from deferred arena cleanup).
    if( !self || !self->type || self->type == nullitem() ) {
        if( self ) {
            debugmsg( "actualize_rot: skipping item with %s type at %s",
                      self->type ? "null-type" : "null", pnt.to_string() );
        }
        return std::move( self );
    }
    if( self->goes_bad() ) {
        return process_rot( std::move( self ), false, pnt, nullptr, temperature, weather );
    } else if( self->type->container && self->type->container->preserves ) {
        // Containers like tin cans preserves all items inside, they do not rot at all.
        return std::move( self );
    } else if( self->type->container && self->type->container->seals ) {
        // Items inside rot but do not vanish as the container seals them in.
        self->contents.remove_top_items_with( [&pnt, &temperature, &weather]( detached_ptr<item> &&it ) {
            if( !it || !it->type || it->type == nullitem() ) {
                return std::move( it );
            }
            if( it->goes_bad() ) {
                it = process_rot( std::move( it ), true, pnt, nullptr, temperature, weather );
            }
            return std::move( it );
        } );
        return std::move( self );
    } else {
        // Check and remove rotten contents, but always keep the container.
        self->contents.remove_top_items_with( [&pnt, &temperature, &weather]( detached_ptr<item> &&it ) {
            return actualize_rot( std::move( it ), pnt, temperature, weather );
        } );
        return std::move( self );
    }
}

bool item_ptr_compare_by_charges( const item *left, const item *right )
{
    if( left->contents.empty() ) {
        return false;
    } else if( right->contents.empty() ) {
        return true;
    } else {
        return right->contents.front().charges < left->contents.front().charges;
    }
}

bool item_compare_by_charges( const item &left, const item &right )
{
    return item_ptr_compare_by_charges( &left, &right );
}

static const std::string USED_BY_IDS( "USED_BY_IDS" );
bool item::already_used_by_player( const player &p ) const
{
    const auto it = item_vars_.find( USED_BY_IDS );
    if( it == item_vars_.end() ) {
        return false;
    }
    // USED_BY_IDS always starts *and* ends with a ';', the search string
    // ';<id>;' matches at most one part of USED_BY_IDS, and only when exactly that
    // id has been added.
    const std::string needle = string_format( ";%d;", p.getID().get_value() );
    return it->second.find( needle ) != std::string::npos;
}

void item::mark_as_used_by_player( const player &p )
{
    std::string &used_by_ids = item_vars_[ USED_BY_IDS ];
    if( used_by_ids.empty() ) {
        // *always* start with a ';'
        used_by_ids = ";";
    }
    // and always end with a ';'
    used_by_ids += string_format( "%d;", p.getID().get_value() );
}

bool item::can_holster( const item &obj, bool ignore ) const
{
    if( !type->can_use( "holster" ) ) {
    return false; // item is not a holster
}

const holster_actor *ptr = dynamic_cast<const holster_actor *>
                           ( type->get_use( "holster" )->get_actor_ptr() );
if( !ptr->can_holster( obj ) ) {
    return false; // item is not a suitable holster for obj
}

if( !ignore && static_cast<int>( contents.num_item_stacks() ) >= ptr->multi ) {
        return false; // item is already full
    }

    return true;
}

std::string item::components_to_string() const
{
    using t_count_map = std::map<std::string, int>;
    t_count_map counts;
    for( const item * const &elem : components ) {
        if( !elem->has_flag( flag_BYPRODUCT ) ) {
            const std::string name = elem->display_name();
            counts[name]++;
        }
    }
    return enumerate_as_string( counts.begin(), counts.end(),
    []( const std::pair<std::string, int> &entry ) -> std::string {
        if( entry.second != 1 )
        {
            return string_format( pgettext( "components count", "%d x %s" ), entry.second, entry.first );
        } else
        {
            return entry.first;
        }
    }, enumeration_conjunction::none );
}

uint64_t item::make_component_hash() const
{
    // First we need to sort the IDs so that identical ingredients give identical hashes.
    std::multiset<std::string> id_set;
    for( const item * const &it : components ) {
        id_set.insert( it->typeId().str() );
    }

    std::string concatenated_ids;
    for( const std::string &id : id_set ) {
        concatenated_ids += id;
    }

    std::hash<std::string> hasher;
    return hasher( concatenated_ids );
}

bool item::needs_processing() const
{
    return is_active() || has_flag( flag_RADIO_ACTIVATION ) || has_flag( flag_ETHEREAL_ITEM ) ||
    ( !contents.empty() && is_container() && contents.front().needs_processing() ) ||
    ( magazine_current() && magazine_current()->needs_processing() ) ||
    is_artifact() || is_relic() || goes_bad();
}

int item::processing_speed() const
{
    if( is_corpse() || is_food() || is_food_container() ) {
    return to_turns<int>( 10_minutes );
    }
    // Unless otherwise indicated, update every turn.
    return 1;
}

detached_ptr<item> item::process_rot( detached_ptr<item> &&self, const tripoint_bub_ms &pos )
{
    return process_rot( std::move( self ), false, pos, nullptr, temperature_flag::TEMP_NORMAL,
                        get_weather() );
}

static units::temperature clip_by_temperature_flag( units::temperature temperature,
        temperature_flag flag )
{
    switch( flag ) {
        case temperature_flag::TEMP_NORMAL:
            // Just use the temperature normally
            return temperature;
        case temperature_flag::TEMP_FRIDGE:
            return std::min( temperature, temperatures::fridge );
        case temperature_flag::TEMP_FREEZER:
            return std::min( temperature, temperatures::freezer );
        case temperature_flag::TEMP_HEATER:
            return std::max( temperature, temperatures::normal );
        case temperature_flag::TEMP_ROOT_CELLAR:
            return temperatures::root_cellar;
        default:
            debugmsg( "Temperature flag enum not valid: %d.  Using current temperature.",
                      static_cast<int>( flag ) );
            break;
    }
    return temperature;
}

void item::update_rot_from_location( const temperature_flag temperature )
{
    if( !goes_bad() || last_rot_check == calendar::turn ) {
        return;
    }
    if( is_in_preserving_container() ) {
        mark_rot_checked_now();
        return;
    }

    auto pos = tripoint_bub_ms::zero();
    auto flag = temperature;
    if( is_loaded() && has_position() ) {
        pos = position();
        flag = rot::temperature_flag_for_location( get_map(), *this );
    }
    update_rot( pos, flag, get_weather() );
}

void item::update_rot( const tripoint_bub_ms &pos, const temperature_flag flag,
                       const weather_manager &weather )
{
    const time_point now = calendar::turn;

    // if player debug menu'd the time backward it breaks stuff, just reset the
    // last_temp_check and last_rot_check in this case
    if( now - last_rot_check < 0_turns ) {
        last_rot_check = now;
        return;
    }

    // process rot at most once every 100_turns (10 min)
    // note we're also gated by item::processing_speed
    constexpr time_duration smallest_interval = 10_minutes;

    units::temperature temp = weather.get_temperature( bub_to_abs( pos ) );
    temp = clip_by_temperature_flag( temp, flag );

    time_point time = last_rot_check;
    item_internal::scoped_goes_bad_cache _cache( this );

    if( now - time > 1_hours ) {
        // This code is for items that were left out of reality bubble for long time

        const weather_generator &wgen = weather.get_cur_weather_gen();
        const unsigned int seed = g->get_seed();
        // It's a modifier, so we need to subtract 0_f
        units::temperature local_mod = units::from_fahrenheit( g->new_game
                                       ? 0
                                       : get_map().get_temperature( pos ) ) - 0_f;

        // Process the past of this item since the last time it was processed
        while( now - time > 1_hours ) {
            // Get the environment temperature
            time_duration time_delta = std::min( 1_hours, now - 1_hours - time );
            time += time_delta;

            //Use weather if above ground, use map temp if below
            units::temperature env_temperature_raw;
            if( pos.z() >= 0 ) {
                tripoint_abs_ms location = tripoint_abs_ms( get_map().bub_to_abs( pos ) );
                units::temperature weather_temperature = wgen.get_weather_temperature( location, time,
                    calendar::config, seed );
                env_temperature_raw = weather_temperature + local_mod;
            } else {
                env_temperature_raw = temperatures::annual_average + local_mod;
            }

            units::temperature env_temperature_clipped = clip_by_temperature_flag( env_temperature_raw, flag );

            // Calculate item rot
            rot += calc_rot( time, env_temperature_clipped );
            last_rot_check = time;
        }
    }

    // Remaining <1 h from above
    // and items that are held near the player
    if( now - time > smallest_interval ) {
        rot += calc_rot( now, temp );
        last_rot_check = now;
    }
}

detached_ptr<item>  item::process_rot( detached_ptr<item> &&self, const bool seals,
                                       const tripoint_bub_ms &pos,
                                       player *carrier, const temperature_flag flag,
                                       const weather_manager &weather )
{
    if( !self ) {
        return std::move( self );
    }
    if( self->is_in_preserving_container() ) {
        self->mark_rot_checked_now();
        return std::move( self );
    }

    self->update_rot( pos, flag, weather );

    if( self->has_rotten_away() && carrier == nullptr && !seals ) {
        return detached_ptr<item>();
    }
    return std::move( self );
}

void item::process_artifact( player *carrier, const tripoint_bub_ms & /*pos*/ )
{
    if( !is_artifact() ) {
        return;
    }
    // Artifacts are currently only useful for the player character, the messages
    // don't consider npcs. Also they are not processed when laying on the ground.
    // TODO: change game::process_artifact to work with npcs,
    // TODO: consider moving game::process_artifact here.
    if( carrier == &get_avatar() ) {
        g->process_artifact( *this, *carrier );
    }
}

std::vector<trait_id> item::mutations_from_wearing( const Character &guy ) const
{
    if( !is_relic() ) {
    return std::vector<trait_id> {};
}
std::vector<trait_id> muts;

for( const enchantment &ench : relic_data->get_enchantments() ) {
    for( const trait_id &mut : ench.get_mutations() ) {
            // this may not be perfectly accurate due to conditions
            muts.push_back( mut );
        }
    }

for( const trait_id &char_mut : guy.get_mutations() ) {
    for( auto iter = muts.begin(); iter != muts.end(); ) {
            if( char_mut == *iter ) {
                iter = muts.erase( iter );
            } else {
                ++iter;
            }
        }
    }

    return muts;
}

void item::process_relic( Character *carrier )
{
    if( !is_relic() ) {
        return;
    }
    std::vector<enchantment> active_enchantments;

    if( carrier ) {
        for( const enchantment &ench : get_enchantments() ) {
            if( ench.is_active( *carrier, *this ) ) {
                active_enchantments.emplace_back( ench );
            }
        }
    }

    relic_funcs::process_recharge( *this, carrier );
}

detached_ptr<item> item::process_corpse( detached_ptr<item> &&self, player *carrier,
        const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    // some corpses rez over time
    if( self->corpse == nullptr || self->damage() >= self->max_damage() ) {
        return std::move( self );
    }
    if( self->corpse->zombify_into && self->rotten() && !self->has_flag( flag_PULPED ) ) {
        self->rot -= self->get_shelf_life();
        self->corpse = &*self->corpse->zombify_into;
        return std::move( self );
    }
    if( !self->ready_to_revive( pos ) ) {
        return std::move( self );
    }
    if( rng( 0, self->volume() / units::legacy_volume_factor ) > self->burnt &&
        g->revive_corpse( pos, *self ) ) {
        if( carrier == nullptr ) {
            if( get_avatar().sees( pos ) ) {
                if( self->corpse->in_species( ROBOT ) ) {
                    add_msg( m_warning, _( "A nearby robot has repaired itself and stands up!" ) );
                } else {
                    add_msg( m_warning, _( "A nearby corpse rises and moves towards you!" ) );
                }
            }
        } else {
            if( self->corpse->in_species( ROBOT ) ) {
                carrier->add_msg_if_player( m_warning,
                                            _( "Oh dear god, a robot you're carrying has started moving!" ) );
            } else {
                carrier->add_msg_if_player( m_warning,
                                            _( "Oh dear god, a corpse you're carrying has started moving!" ) );
            }
        }
        // Destroy this corpse item
        return detached_ptr<item>();
    }

    return std::move( self );
}

detached_ptr<item> item::process_fake_mill( detached_ptr<item> &&self, player * /*carrier*/,
        const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    map &here = get_map();
    if( here.furn( pos ) != furn_str_id( "f_wind_mill_active" ) &&
        here.furn( pos ) != furn_str_id( "f_water_mill_active" ) ) {
        self->set_counter( 0 );
        return detached_ptr<item>(); //destroy fake mill
    }
    if( self->age() >= 6_hours || self->item_counter == 0 ) {
        iexamine::mill_finalize( get_avatar(), pos,
                                 self->birthday() ); //activate effects when timers goes to zero
        return detached_ptr<item>(); //destroy fake mill item
    }

    return std::move( self );
}

detached_ptr<item> item::process_fake_cloning_vat( detached_ptr<item> &&self, player * /*carrier*/,
        const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    map &here = get_map();
    if( here.furn( pos ) != furn_str_id( "f_cloning_vat_active" ) ) {
        self->item_counter = 0;
        return detached_ptr<item>(); //destroy fake smoke
    }

    if( self->item_counter == 0 ) {
        iexamine::cloning_vat_finalize( pos, self->birthday() ); //activate effects when timers goes to zero
        return detached_ptr<item>(); //destroy fake smoke when it 'burns out'
    }

    return std::move( self );
}

detached_ptr<item> item::process_fake_smoke( detached_ptr<item> &&self, player * /*carrier*/,
        const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    map &here = get_map();
    if( here.furn( pos ) != furn_str_id( "f_smoking_rack_active" ) &&
        here.furn( pos ) != furn_str_id( "f_metal_smoking_rack_active" ) ) {
        self->set_counter( 0 );
        return detached_ptr<item>(); //destroy fake smoke
    }

    if( self->age() >= 6_hours || self->item_counter == 0 ) {
        iexamine::on_smoke_out( pos, self->birthday() ); //activate effects when timers goes to zero
        return detached_ptr<item>(); //destroy fake smoke when it 'burns out'
    }

    return std::move( self );
}

detached_ptr<item> item::process_litcig( detached_ptr<item> &&self, player *carrier,
        const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    if( !one_in( 10 ) ) {
        return std::move( self );
    }
    self = self->process_extinguish( std::move( self ), carrier, pos );
    // process_extinguish might have extinguished the item already
    if( !self->is_active() ) {
        return std::move( self );
    }
    item &it = *self;
    map &here = get_map();
    // if carried by someone:
    if( carrier != nullptr ) {
        time_duration duration = 15_seconds;
        if( carrier->has_trait( trait_TOLERANCE ) ) {
            duration = 7_seconds;
        } else if( carrier->has_trait( trait_LIGHTWEIGHT ) ) {
            duration = 30_seconds;
        }
        carrier->add_msg_if_player( m_neutral, _( "You take a puff of your %s." ), it.tname() );

        // we need to figure out a way to get the item before this got converted,
        // but i don't think that's going to be very easy...
        if( it.has_flag( flag_TOBACCO ) ) {
            carrier->add_effect( effect_cig, duration );
        }
        if( it.has_flag( flag_MARIJUANA ) ) {
            carrier->add_effect( effect_weed_high, duration );
        }

        carrier->moves -= 15;

        if( ( carrier->has_effect( effect_shakes ) && one_in( 10 ) ) ) {
            carrier->add_msg_if_player( m_bad, _( "Your shaking hand causes you to drop your %s." ),
                                        it.tname() );
            here.add_item_or_charges( pos + point_rel_ms( rng( -1, 1 ), rng( -1, 1 ) ),
                                      std::move( self ) );
            return detached_ptr<item>(); // removes the item that has just been added to the map
        }

        if( carrier->has_effect( effect_sleep ) ) {
            carrier->add_msg_if_player( m_bad, _( "You fall asleep and drop your %s." ),
                                        it.tname() );
            here.add_item_or_charges( pos + point_rel_ms( rng( -1, 1 ), rng( -1, 1 ) ),
                                      std::move( self ) );
            self = detached_ptr<item>();
        }
    } else {
        // If not carried by someone, but laying on the ground:
        if( it.item_counter % 5 == 0 ) {
            // lit cigarette can start fires
            if( here.flammable_items_at( pos ) ||
                here.has_flag( flag_FLAMMABLE, pos ) ||
                here.has_flag( flag_FLAMMABLE_ASH, pos ) ) {
                here.add_field( pos, fd_fire, 1 );
            }
        }
    }

    // cig dies out
    if( it.item_counter == 0 ) {
        if( carrier != nullptr ) {
            carrier->add_msg_if_player( m_neutral, _( "You finish your %s." ), it.tname() );
        }
        it.convert( dynamic_cast<const iuse_transform *>
                    ( it.type->get_use( "transform" )->get_actor_ptr() )->target );
        if( it.has_flag( flag_MARIJUANA ) ) {
            if( carrier != nullptr ) {
                carrier->add_effect( effect_weed_high, 1_minutes ); // one last puff
                here.add_field( pos + point_rel_ms( rng( -1, 1 ), rng( -1, 1 ) ), fd_weedsmoke,
                                2 );
                weed_msg( *carrier );
            }
        }
        it.deactivate();
    }
    // Item remains
    return std::move( self );
}

detached_ptr<item> item::process_extinguish( detached_ptr<item> &&self, player *carrier,
        const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    // checks for water
    bool extinguish = false;
    bool in_inv = carrier != nullptr && carrier->has_item( *self );
    bool submerged = false;
    bool precipitation = false;
    bool windtoostrong = false;
    bool in_veh = carrier != nullptr && carrier->in_vehicle;
    int windpower = get_weather().windspeed;
    switch( get_weather().weather_id->precip ) {
        case precip_class::very_light:
            precipitation = one_in( 100 );
            break;
        case precip_class::light:
            precipitation = one_in( 50 );
            break;
        case precip_class::medium:
            precipitation = one_in( 25 );
            break;
        case precip_class::heavy:
            precipitation = one_in( 10 );
            break;
        default:
            break;
    }
    map &here = get_map();
    if( in_inv && !in_veh && here.has_flag( flag_DEEP_WATER, pos ) ) {
        extinguish = true;
        submerged = true;
    }
    if( ( !in_inv && here.has_flag( flag_LIQUID, pos ) &&
          !here.veh_at( pos ) ) ||
        ( precipitation && !g->is_sheltered( pos ) ) ) {
        extinguish = true;
    }
    if( in_inv && windpower > 5 && !g->is_sheltered( pos ) &&
        self->has_flag( flag_WIND_EXTINGUISH ) ) {
        windtoostrong = true;
        extinguish = true;
    }
    if( !extinguish ||
        ( in_inv && precipitation && carrier->primary_weapon().has_flag( flag_RAIN_PROTECT ) ) ) {
        return std::move( self ); //nothing happens
    }
    if( carrier != nullptr ) {
        if( submerged ) {
            carrier->add_msg_if_player( m_neutral, _( "Your %s is quenched by water." ), self->tname() );
        } else if( precipitation ) {
            carrier->add_msg_if_player( m_neutral, _( "Your %s is quenched by precipitation." ),
                                        self->tname() );
        } else if( windtoostrong ) {
            carrier->add_msg_if_player( m_neutral, _( "Your %s is blown out by the wind." ),
                                        self->tname() );
        }
    }

    // cig dies out
    if( self->has_flag( flag_LITCIG ) ) {
        self->convert( dynamic_cast<const iuse_transform *>
                       ( self->type->get_use( "transform" )->get_actor_ptr() )->target );
    } else { // transform (lit) items
        if( !self->revert( carrier ) ) {
            self->type->invoke( carrier != nullptr ? *carrier : get_avatar(), *self, pos, "transform" );
        }

    }
    self->deactivate();
    // Item remains
    return std::move( self );
}

detached_ptr<item> item::process_cable( detached_ptr<item> &&self, player *carrier,
                                        const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    //No need to process cable if it' has no map connections's only connected to character
    auto data = cable_connection_data::make_data( self.ptr );
    if( !data || data->empty() ) {
        self->reset_cable( carrier );
        return std::move( self );
    }
    if( data->character_only() || data->intermap_connection() ) {
        return std::move( self );
    }
    if( !data->get_nonchar_connection() ) {
        self->reset_cable( carrier );
        return std::move( self );
    }

    int distance = 0;
    map &here = get_map();
    //At this point we are sure that non_char is not empty
    auto nonchar = *data->get_nonchar_connection();

    //Caharacter connected to smth
    if( data->complete() ) {
        if( !carrier ) {
            if( data->get_map_connection() ) {
                data->unset_other_con( self.get(), nonchar );
            } else {
                self->reset_cable( carrier );
            }
            return std::move( self );
        }
        switch( nonchar.state ) {
            case state_solar_pack:
                if( !carrier->has_item( *self ) || !carrier->worn_with_flag( flag_SOLARPACK_ON ) ) {
                    carrier->add_msg_if_player( m_bad, _( "You notice the cable has come loose!" ) );
                    self->reset_cable( carrier );
                }
                return std::move( self );
            case state_UPS: {
                static const item_filter used_ups = [&]( const item & itm ) {
                    return itm.get_var( "cable" ) == "plugged_in";
                };

                if( !carrier->has_item( *self ) || !carrier->has_item_with( used_ups ) ) {
                    for( item *used : carrier->items_with( used_ups ) ) {
                        used->erase_var( "cable" );
                    }
                    carrier->add_msg_if_player( m_bad, _( "You notice the cable has come loose!" ) );
                    self->reset_cable( carrier );
                }
                return std::move( self );
            }
            case state_vehicle: {
                if( !here.veh_at( nonchar.point ) ) {
                    carrier->add_msg_if_player( m_bad, _( "You notice the cable has disconnected from a vehicle!" ) );
                    data->unset_con( self.get(), nonchar );
                    return std::move( self );
                }
                //Sitting in vehicle shenenigans
                //If character is sitting at vehicle - no need(and no way) to figure out cable distance and if it's the same vehicle we previously connected
                //So we just ignore distance
                const auto vp_pos = here.veh_at( pos );
                if( vp_pos ) {
                    const auto seat = vp_pos.part_with_feature( "BOARDABLE", true );
                    if( seat && carrier == seat->vehicle().get_passenger( seat->part_index() ) ) {
                        return std::move( self );
                    }
                }
                break;
            }
            case state_grid: {
                auto s = nonchar.point;
                auto *grid_connector = active_tiles::furn_at<vehicle_connector_tile>( s );
                if( !grid_connector ) {
                    if( carrier->has_item( *self ) ) {
                        carrier->add_msg_if_player( m_bad, _( "You notice the cable has come loose!" ) );
                    }
                    self->reset_cable( carrier );
                    return std::move( self );
                }
                break;
            }
            case state_none:
            case state_self:
            default:
                debugmsg( "Unexpected cable state %s", nonchar.state );
                self->reset_cable( carrier );
                return std::move( self );
        }
    }
    if( nonchar.map_point() ) {
        distance = rl_dist( pos, here.abs_to_bub( nonchar.point ) );
        self->charges = self->type->maximum_charges() - distance;
        if( self->charges < 1 ) {
            if( carrier ) {
                carrier->add_msg_if_player( m_bad, _( "The over-extended cable breaks loose!" ) );
            }
            self->reset_cable( carrier );
        }
    }

    return std::move( self );
}

void item::reset_cable( Character *who )
{
    int max_charges = type->maximum_charges();
    //erase legacy info
    erase_var( "state" );
    erase_var( "source_x" );
    erase_var( "source_y" );
    erase_var( "source_z" );
    //erase info
    cable_connection_data::unset_vars( this );
    deactivate();
    charges = max_charges;

    if( who != nullptr ) {
        who->add_msg_if_player( m_info, _( "You reel in the cable." ) );
        who->mod_moves( -10 * charges );
    }
}

detached_ptr<item> item::process_UPS( detached_ptr<item> &&self, player *carrier,
                                      const tripoint_bub_ms & /*pos*/ )
{
    if( !self ) {
        return std::move( self );
    }
    if( carrier == nullptr ) {
        self->erase_var( "cable" );
        self->deactivate();
        return std::move( self );
    }
    bool has_connected_cable = carrier->has_item_with( []( const item & it ) {
        return it.is_active() && cable_connection_data::ups_connected( &it );
    } );
    if( !has_connected_cable ) {
        self->erase_var( "cable" );
        self->deactivate();
    }
    return std::move( self );
}

bool item::process_wet( player * /*carrier*/, const tripoint_bub_ms & /*pos*/ )
{
    if( item_counter == 0 ) {
        if( is_tool() && type->tool->revert_to ) {
            convert( *type->tool->revert_to );
        }
        unset_flag( flag_WET );
        deactivate();
    }
    // Always return true so our caller will bail out instead of processing us as a tool.
    return true;
}

detached_ptr<item> item::process_tool( detached_ptr<item> &&self, player *carrier,
                                       const tripoint_bub_ms &pos )
{
    if( !self ) {
        return std::move( self );
    }
    avatar &you = get_avatar();
    // items with iuse set_transformed which are restricted turn off if not attached to their dependency.
    if( self->type->can_use( "set_transformed" ) ) {
        const set_transformed_iuse *actor = dynamic_cast<const set_transformed_iuse *>
                                            ( self->get_use( "set_transformed" )->get_actor_ptr() );
        if( actor == nullptr ) {
            debugmsg( "iuse_actor type descriptor and actual type mismatch" );
            return std::move( self );
        }
        if( actor->restricted ) {
            if( !carrier ) {
                actor->bypass( carrier != nullptr ? *carrier : you, *self, false, pos );
                return std::move( self );
            } else {
                bool active = false;
                flag_id transform_flag( actor->dependencies );
                for( const auto &elem : carrier->worn ) {
                    if( elem->is_active() && elem->has_flag( transform_flag ) ) {
                        active = true;
                        break;
                    }
                }
                if( !active ) {
                    actor->bypass( carrier != nullptr ? *carrier : you, *self, false, pos );
                    return std::move( self );
                }
            }
        }
    }

    int energy = 0;
    const bool uses_UPS = self->has_flag( flag_USE_UPS );
    bool revert_destroy = false;
    if( self->type->tool->turns_per_charge > 0 ) {
        if( self->type->tool->turns_active >= self->type->tool->turns_per_charge ) {
            energy = std::max( self->ammo_required(), 1 );
            self->type->tool->turns_active = 0;
        }
        self->type->tool->turns_active += 1;
    } else if( self->type->tool->power_draw > 0 ) {
        // power_draw in mW / 1000000 to give kJ (battery unit) per second
        energy = self->type->tool->power_draw / 1000000;
        // energy_bat remainder results in chance at additional charge/discharge
        energy += x_in_y( self->type->tool->power_draw % 1000000, 1000000 ) ? 1 : 0;
    }

    // If ammo_required is 0 we just skip over this and go to tick processing.
    if( energy || self->ammo_required() > 0 ) {
        // No need to look for charges if energy is 0
        if( energy ) {
            energy -= self->ammo_consume( energy, pos );

            // for power armor pieces, try to use power armor interface first.
            if( carrier && self->is_power_armor() && character_funcs::can_interface_armor( *carrier ) ) {
                if( carrier->use_charges_if_avail( itype_bio_armor, energy ) ) {
                    energy = 0;
                }
            }

            // for items in player possession if insufficient charges within tool try UPS
            if( carrier && uses_UPS ) {
                if( carrier->use_charges_if_avail( itype_UPS, energy ) ) {
                    energy = 0;
                }
            }
        }

        // HACK: this means that UPS items will last one more check longer than they should since they don't trigger when
        // their ammo_remaining is 0, since that doesn't check the UPS "stock" available (which is an expensive check)
        // It's done like this cause grenades must be destroyed when charge reaches 0, or it will linger an extra turn.
        if( ( self->ammo_remaining() == 0 && !uses_UPS ) || energy > 0 ) {
            revert_destroy = true;
            if( carrier ) {
                if( self->is_power_armor() ) {
                    if( uses_UPS ) {
                        carrier->add_msg_if_player( m_info, _( "You need a UPS or Bionic Power Interface to run the %s!" ),
                                                    self->tname() );
                    } else {

                    }
                } else if( uses_UPS ) {
                    carrier->add_msg_if_player( m_info, _( "You need a UPS to run the %s!" ), self->tname() );
                }
            }
            if( carrier && self->type->can_use( "set_transform" ) ) {
                const set_transform_iuse *actor = dynamic_cast<const set_transform_iuse *>
                                                  ( self->get_use( "set_transform" )->get_actor_ptr() );
                if( actor == nullptr ) {
                    debugmsg( "iuse_actor type descriptor and actual type mismatch." );
                    return std::move( self );
                }
                flag_id transformed_flag( actor->flag );
                for( auto &elem : carrier->worn ) {
                    if( elem->is_active() && elem->has_flag( transformed_flag ) ) {
                        if( !elem->type->can_use( "set_transformed" ) ) {
                            debugmsg( "Expected set_transformed function" );
                            return std::move( self );
                        }
                        const set_transformed_iuse *actor = dynamic_cast<const set_transformed_iuse *>
                                                            ( elem->get_use( "set_transformed" )->get_actor_ptr() );
                        if( actor == nullptr ) {
                            debugmsg( "iuse_actor type descriptor and actual type mismatch" );
                            return std::move( self );
                        }
                        actor->bypass( *carrier, *elem, false, pos );
                    }
                }
            }
        }
    }

    // Process tick even if it's to be destroyed/reverted later, more for grenades
    // It technically gives an extra turn of action, but before the rework items functioned at 0 charges for a bit anyway.
    // Calls all use functions if active
    if( ( self->get_use( "REMOTEVEH" ) || self->get_use( "RADIOCONTROL" ) ) && self->is_active() ) {
        const use_function *method = nullptr;
        if( g->remoteveh() != nullptr && self->get_use( "REMOTEVEH" ) ) {
            method = &self->type->use_methods.find( "REMOTEVEH" )->second;
        } else if( !g->u.get_value( "remote_controlling" ).empty() && self->get_use( "RADIOCONTROL" ) ) {
            method = &self->type->use_methods.find( "RADIOCONTROL" )->second;
        }
        if( method != nullptr ) {
            method->call( carrier != nullptr ? *carrier : you, *self, true, pos );
        } else {
            self->type->tick( carrier != nullptr ? *carrier : you, *self, pos );
        }
    } else {
        self->type->tick( carrier != nullptr ? *carrier : you, *self, pos );
    }

    if( revert_destroy ) {
        // If no revert is defined, destroy it (candles and the like).
        if( self->is_active() && self->revert( carrier ) ) {
            self->deactivate();
            return std::move( self );
        } else {
            return detached_ptr<item>();
        }
    }

    return std::move( self );
}

detached_ptr<item> item::process_blackpowder_fouling( detached_ptr<item> &&self, player *carrier )
{
    if( !self ) {
        return std::move( self );
    }
    if( self->damage() < self->max_damage() && one_in( 2000 ) ) {
        self->inc_damage( DT_ACID );
        if( carrier ) {
            carrier->add_msg_if_player( m_bad, _( "Your %s rusts due to blackpowder fouling." ),
                                        self->tname() );
        }
    }
    return std::move( self );
}

detached_ptr<item> item::process( detached_ptr<item> &&self, player *carrier,
                                  const tripoint_bub_ms &pos,
                                  bool activate,
                                  temperature_flag flag )
{
    return process( std::move( self ), carrier, pos, activate, flag, get_weather() );
}

detached_ptr<item> item::process( detached_ptr<item> &&self, player *carrier,
                                  const tripoint_bub_ms &pos,
                                  bool activate,
                                  temperature_flag flag, const weather_manager &weather_generator )
{
    if( !self ) {
        return std::move( self );
    }
    const bool preserves = self->type->container && self->type->container->preserves;
    const bool seals = self->type->container && self->type->container->seals;
    item &obj = *self;

    obj.remove_items_with( [&]( detached_ptr<item> &&it ) {
        if( preserves ) {
            it->last_rot_check = calendar::turn;
        }
        it = it->process_internal( std::move( it ), carrier, pos, activate, seals, flag,
                                   weather_generator );
        return VisitResponse::NEXT;
    } );
    detached_ptr<item> res = process_internal( std::move( self ), carrier, pos, activate, seals, flag,
                             weather_generator );
    return res;
}

detached_ptr<item> item::process_internal( detached_ptr<item> &&self, player *carrier,
        const tripoint_bub_ms &pos, bool activate,
        const bool seals, const temperature_flag flag,
        const weather_manager &weather_generator )
{
    ZoneScopedN( "item_process_internal" );
    if( !self ) {
        return std::move( self );
    }
    if( self->has_flag( flag_ETHEREAL_ITEM ) ) {
        ZoneScopedN( "item_process_ethereal" );
        if( !self->has_var( "ethereal" ) ) {
            return detached_ptr<item>();
        }
        self->set_var( "ethereal", std::stoi( self->get_var( "ethereal" ) ) - 1 );
        const bool processed = std::stoi( self->get_var( "ethereal" ) ) <= 0;
        if( processed && carrier != nullptr ) {
            carrier->add_msg_if_player( _( "Your %s disappears!" ), self->tname() );
        }
        if( processed ) {
            return detached_ptr<item>();
        } else {
            return std::move( self );
        }
    }

    {
        ZoneScopedN( "item_process_artifact_relic" );
        self->process_artifact( carrier, pos );
        self->process_relic( carrier );
    }

    if( self->faults.contains( fault_gun_blackpowder ) ) {
        ZoneScopedN( "item_process_faults" );
        return process_blackpowder_fouling( std::move( self ), carrier );
    }

    avatar &you = get_avatar();
    if( activate ) {
        ZoneScopedN( "item_process_activate" );
        if( self->type->invoke( carrier != nullptr ? *carrier : you, *self, pos ) > 0 ) {
            return detached_ptr<item>();
        }
        return std::move( self );
    }
    // How this works: it checks what kind of processing has to be done
    // (e.g. for food, for drying towels, lit cigars), and if that matches,
    // call the processing function. If that function returns true, the item
    // has been destroyed by the processing, so no further processing has to be
    // done.
    // Otherwise processing continues. This allows items that are processed as
    // food and as litcig and as ...

    // Remaining stuff is only done for active items.
    if( !self->is_active() ) {
        ZoneScopedN( "item_process_inactive_return" );
        return std::move( self );
    }

    if( !self->is_food() && self->item_counter > 0 ) {
        ZoneScopedN( "item_process_counter" );
        self->item_counter--;
    }

    if( self->item_counter == 0 && self->type->countdown_action ) {
        ZoneScopedN( "item_process_countdown" );
        self->type->countdown_action.call( carrier ? *carrier : you, *self, false, pos );
        if( self->type->countdown_destroy ) {
            return detached_ptr<item>();
        }
    }

    map &here = get_map();
    if( !self->type->emits.empty() ) {
        ZoneScopedN( "item_process_emits" );
        for( const emit_id &e : self->type->emits ) {
            here.emit_field( pos, e );
        }
    }

    if( self->has_flag( flag_FAKE_SMOKE ) ) {
        ZoneScopedN( "item_process_fake_smoke" );
        self = process_fake_smoke( std::move( self ), carrier, pos );
        if( !self ) {
            return std::move( self );
        }
    }
    if( self->has_flag( flag_FAKE_CLONING_VAT ) ) {
        ZoneScopedN( "item_process_fake_cloning_vat" );
        self = process_fake_cloning_vat( std::move( self ), carrier, pos );
        if( !self ) {
            return std::move( self );
        }
    }
    if( self->has_flag( flag_FAKE_MILL ) ) {
        ZoneScopedN( "item_process_fake_mill" );
        self = process_fake_mill( std::move( self ), carrier, pos );
        if( !self ) {
            return std::move( self );
        }
    }
    if( self->is_corpse() ) {
        ZoneScopedN( "item_process_corpse" );
        self = process_corpse( std::move( self ), carrier, pos );
        if( !self ) {
            return std::move( self );
        }
    }
    if( self->has_flag( flag_WET ) ) {
        ZoneScopedN( "item_process_wet" );
        if( self->process_wet( carrier, pos ) ) {
            // Drying items are never destroyed, but we want to exit so they don't get processed as tools.
            return std::move( self );
        }
    }
    if( self->has_flag( flag_LITCIG ) ) {
        ZoneScopedN( "item_process_litcig" );
        self = process_litcig( std::move( self ), carrier, pos );
        if( !self ) {
            return std::move( self );
        }
    }
    if( ( self->has_flag( flag_WATER_EXTINGUISH ) || self->has_flag( flag_WIND_EXTINGUISH ) ) ) {
        ZoneScopedN( "item_process_extinguish" );
        self = process_extinguish( std::move( self ), carrier, pos );
        if( !self ) {
            return std::move( self );
        }
    }
    if( self->has_flag( flag_WATER_DISABLE ) && carrier->is_underwater() ) {
        ZoneScopedN( "item_process_water_disable" );
        carrier->add_msg_if_player( "Your %s gurgles and splutters.", self->tname() );
        self->revert( carrier );
        self->deactivate();
        return std::move( self );
    }
    if( self->has_flag( flag_CABLE_SPOOL ) ) {
        ZoneScopedN( "item_process_cable" );
        // DO NOT process this as a tool! It really isn't!
        return process_cable( std::move( self ), carrier, pos );
    }
    if( self->has_flag( flag_IS_UPS ) ) {
        ZoneScopedN( "item_process_ups" );
        // DO NOT process this as a tool! It really isn't!
        return process_UPS( std::move( self ), carrier, pos );
    }
    if( self->is_tool() ) {
        ZoneScopedN( "item_process_tool" );
        return process_tool( std::move( self ), carrier, pos );
    }
    // All foods that go bad have temperature
    if( ( self->is_food() || self->is_corpse() ) ) {
        ZoneScopedN( "item_process_rot" );
        item &obj = *self;
        self = process_rot( std::move( self ), seals, pos, carrier, flag, weather_generator );
        // If the item has rotted away, then self becomes a null pointer.
        if( !self ) {
            if( obj.is_comestible() ) {
                here.rotten_item_spawn( obj, pos );
            } else if( obj.is_corpse() ) {
                here.handle_decayed_corpse( obj, pos );
            }
        }
    }
    return std::move( self );
}

void item::mod_charges( int mod )
{
    if( has_infinite_charges() ) {
        return;
    }

    if( !count_by_charges() ) {
        debugmsg( "Tried to remove %s by charges, but item is not counted by charges.", tname() );
    } else if( mod < 0 && charges + mod < 0 ) {
        debugmsg( "Tried to remove charges that do not exist, removing maximum available charges instead." );
        charges = 0;
    } else if( mod > 0 && charges >= INFINITE_CHARGES - mod ) {
        charges = INFINITE_CHARGES - 1; // Highly unlikely, but finite charges should not become infinite.
    } else {
        charges += mod;
    }
}

bool item::has_effect_when_wielded( art_effect_passive effect ) const
{
    if( !type->artifact ) {
    return false;
}
const std::vector<art_effect_passive> &ew = type->artifact->effects_wielded;
return std::ranges::contains( ew, effect );
}

bool item::has_effect_when_worn( art_effect_passive effect ) const
{
    if( !type->artifact ) {
    return false;
}
const std::vector<art_effect_passive> &ew = type->artifact->effects_worn;
return std::ranges::contains( ew, effect );
}

bool item::has_effect_when_carried( art_effect_passive effect ) const
{
    if( !type->artifact ) {
    return false;
}
const std::vector<art_effect_passive> &ec = type->artifact->effects_carried;
if( std::ranges::contains( ec, effect ) ) {
        return true;
    }
for( const item *i : contents.all_items_top() ) {
        if( i->has_effect_when_carried( effect ) ) {
            return true;
        }
    }
    return false;
}

bool item::is_seed() const
{
    return type->is_seed();
}

time_duration item::get_plant_epoch() const
{
    if( !type->seed ) {
    return 0_turns;
}
return type->seed->get_plant_epoch();
}

std::string item::get_plant_name() const
{
    if( !type->seed ) {
    return std::string{};
}
return type->seed->plant_name.translated();
}

bool item::is_dangerous() const
{
    if( has_flag( flag_DANGEROUS ) ) {
    return true;
}

// Note: Item should be dangerous regardless of what type of a container is it
// Visitable interface would skip some options
for( const item *it : contents.all_items_top() ) {
    if( it->is_dangerous() ) {
            return true;
        }
    }
    return false;
}

bool item::is_tainted() const
{
    return corpse && corpse->has_flag( MF_POISON );
}

bool item::is_soft() const
{
    const std::vector<material_id> mats = made_of();
    return std::ranges::any_of( mats, []( const material_id & mid ) {
        return mid.obj().soft();
    } );
}

bool item::is_reloadable() const
{
    if( has_flag( flag_NO_RELOAD ) && !has_flag( flag_VEHICLE ) ) {
    return false; // turrets ignore NO_RELOAD flag

} else if( is_bandolier() || is_holster() ) {
    return true;

} else if( is_container() ) {
    // TODO: Make buckets actually reloadable using reload menu
    // This would be done via locking this off by weather or not it was wielded or on dirt most likely
    return type->container->seals;

} else if( !is_gun() && !is_tool() && !is_magazine() ) {
    return false;

} else if( ammo_types().empty() ) {
    return false;
}

return true;
}

std::string item::type_name( unsigned int quantity ) const
{
    const auto iter = item_vars_.find( "name" );
    std::string ret_name;
    if( iter != item_vars_.end() ) {
        return iter->second;
    } else {
        ret_name = type->nname( quantity );
    }

    // Apply conditional names, in order.
    for( const conditional_name &cname : type->conditional_names ) {
        // Lambda for recursively searching for a item ID among all components.
        std::function<bool ( std::vector<item *> )> component_id_contains =
        [&]( const std::vector<item *> &components ) {
            for( const item *component : components ) {
                if( component->typeId().str().find( cname.condition ) != std::string::npos ||
                    component_id_contains( component->components.as_vector() ) ) {
                    return true;
                }
            }
            return false;
        };
        switch( cname.type ) {
            case condition_type::FLAG:
                if( has_flag( flag_id( cname.condition ) ) ) {
                    ret_name = string_format( cname.name.translated( quantity ), ret_name );
                }
                break;
            case condition_type::VITAMIN:
                if( has_vitamin( vitamin_id( cname.condition ) ) ) {
                    ret_name = string_format( cname.name.translated( quantity ), ret_name );
                }
                break;
            case condition_type::COMPONENT_ID:
                if( component_id_contains( components.as_vector() ) ) {
                    ret_name = string_format( cname.name.translated( quantity ), ret_name );
                }
                break;
            case condition_type::num_condition_types:
                break;
        }
    }

    // Identify who this corpse belonged to, if applicable.
    if( corpse != nullptr && has_flag( flag_CORPSE ) ) {
        if( corpse_name.empty() ) {
            //~ %1$s: name of corpse with modifiers;  %2$s: species name
            ret_name = string_format( pgettext( "corpse ownership qualifier", "%1$s of a %2$s" ),
                                      ret_name, corpse->nname() );
        } else {
            //~ %1$s: name of corpse with modifiers;  %2$s: proper name;  %3$s: species name
            ret_name = string_format( pgettext( "corpse ownership qualifier", "%1$s of %2$s, %3$s" ),
                                      ret_name, corpse_name, corpse->nname() );
        }
    }

    return ret_name;
}

const mtype *item::get_corpse_mon() const
{
    return corpse;
}

std::string item::get_corpse_name()
{
    if( corpse_name.empty() ) {
        return std::string();
    }
    return corpse_name;
}

std::string item::nname( const itype_id &id, unsigned int quantity )
{
    return id->nname( quantity );
}

bool item::count_by_charges( const itype_id &id )
{
    return id->count_by_charges();
}

int item::get_gun_ups_drain() const
{
    int draincount = 0;
    if( type->gun ) {
        int modifier = 0;
        float multiplier = 1.0f;
        for( const item *mod : gunmods() ) {
            modifier += mod->type->gunmod->ups_charges_modifier;
            multiplier *= mod->type->gunmod->ups_charges_multiplier;
        }
        draincount = ( type->gun->ups_charges * multiplier ) + modifier;
    }
    return draincount;
}

bool item::has_label() const
{
    return has_var( "item_label" );
}

std::string item::label( unsigned int quantity ) const
{
    if( has_label() ) {
    return get_var( "item_label" );
    }

    return type_name( quantity );
}

bool item::has_infinite_charges() const
{
    return charges == INFINITE_CHARGES;
}

skill_id item::contextualize_skill( const skill_id &id ) const
{
    if( id->is_contextual_skill() ) {
    if( id == skill_weapon ) {
            if( is_gun() ) {
                return gun_skill();
            } else if( is_melee() ) {
                return melee_skill();
            }
        }
    }

    return id;
}


bool item::on_drop( const tripoint_bub_ms &pos )
{
    return on_drop( pos, get_map() );
}

bool item::on_drop( const tripoint_bub_ms &pos, map &m )
{
    avatar &you = get_avatar();

    if( type->istate_callbacks ) {
        bool prevented = type->istate_callbacks->call_on_drop( you, *this, pos );
        if( prevented ) {
            return true;
        }
    }

    // dropping liquids, even currently frozen ones, on the ground makes them
    // dirty
    if( made_of( LIQUID ) && !m.has_flag( flag_LIQUIDCONT, pos ) &&
        !has_own_flag( flag_DIRTY ) ) {
        set_flag( flag_DIRTY );
    }
    you.flag_encumbrance();

    return type->drop_action && type->drop_action.call( you, *this, false, pos );
}

time_duration item::age() const
{
    return calendar::turn - birthday();
}

void item::set_age( const time_duration &age )
{
    set_birthday( time_point( calendar::turn ) - age );
}

void item::legacy_fast_forward_time()
{
    const time_duration tmp_bday = ( bday - calendar::turn_zero ) * 6;
    bday = calendar::turn_zero + tmp_bday;

    rot *= 6;

    const time_duration tmp_rot = ( last_rot_check - calendar::turn_zero ) * 6;
    last_rot_check = calendar::turn_zero + tmp_rot;
}

bool item::is_active() const
{
    return active;
}

time_point item::birthday() const
{
    return bday;
}

void item::set_birthday( const time_point &bday )
{
    this->bday = std::max( calendar::turn_zero, bday );
}

bool item::is_upgrade() const
{
    if( !type->bionic ) {
    return false;
}
return type->bionic->is_upgrade;
}

int item::get_min_str() const
{
    if( type->gun ) {
    int min_str = type->min_str;
    for( const item *mod : gunmods() ) {
            min_str += mod->type->gunmod->min_str_required_mod;
        }
        return min_str > 0 ? min_str : 0;
    } else {
        return type->min_str;
    }
}

std::vector<item_comp> item::get_uncraft_components() const
{
    std::vector<item_comp> ret;
    if( components.empty() ) {
        //If item wasn't crafted with specific components use default recipe
        std::vector<std::vector<item_comp>> recipe = recipe_dictionary::get_uncraft(
                typeId() ).disassembly_requirements().get_components();
        for( std::vector<item_comp> &component : recipe ) {
            ret.push_back( component.front() );
        }
    } else {
        //Make a new vector of components from the registered components
        for( const item * const &component : components ) {
            auto iter = std::ranges::find_if( ret, [component]( item_comp & obj ) {
                return obj.type == component->typeId();
            } );

            if( iter != ret.end() ) {
                iter->count += component->count();
            } else {
                ret.emplace_back( component->typeId(), component->count() );
            }
        }
    }
    return ret;
}

void item::set_favorite( const bool favorite )
{
    is_favorite = favorite;
}

const recipe &item::get_making() const
{
    if( !craft_data_ ) {
    debugmsg( "'%s' is not a craft or has a null recipe", tname() );
        static const recipe dummy{};
        return dummy;
    }
    assert( craft_data_->making );
    return *craft_data_->making;
}

void item::set_tools_to_continue( bool value )
{
    assert( craft_data_ );
    craft_data_->tools_to_continue = value;
}

bool item::has_tools_to_continue() const
{
    assert( craft_data_ );
    return craft_data_->tools_to_continue;
}

void item::set_cached_tool_selections( const std::vector<comp_selection<tool_comp>> &selections )
{
    assert( craft_data_ );
    craft_data_->cached_tool_selections = selections;
}

const std::vector<comp_selection<tool_comp>> &item::get_cached_tool_selections() const
{
    assert( craft_data_ );
    return craft_data_->cached_tool_selections;
}

const cata::value_ptr<islot_comestible> &item::get_comestible() const
{
    if( is_craft() ) {
    return craft_data_->making->result()->comestible;
    } else {
        return type->comestible;
    }
}

bool item::has_clothing_mod() const
{
for( const clothing_mod &cm : clothing_mods::get_all() ) {
    if( has_own_flag( cm.flag ) ) {
            return true;
        }
    }
    return false;
}

namespace
{
const std::string &get_clothing_mod_val_key( clothing_mod_type type )
{
    const static auto cache = ( []() {
        std::array<std::string, clothing_mods::all_clothing_mod_types.size()> res;
        for( const clothing_mod_type &type : clothing_mods::all_clothing_mod_types ) {
            res[type] = CLOTHING_MOD_VAR_PREFIX
                        + clothing_mods::string_from_clothing_mod_type( clothing_mods::all_clothing_mod_types[type] );
        }
        return res;
    } )();

    return cache[ type ];
}
} // namespace

float item::get_clothing_mod_val( clothing_mod_type type ) const
{
    return get_var( get_clothing_mod_val_key( type ), 0.0f );
}

void item::update_clothing_mod_val()
{
    for( const clothing_mod_type &type : clothing_mods::all_clothing_mod_types ) {
        float tmp = 0.0;
        for( const clothing_mod &cm : clothing_mods::get_all_with( type ) ) {
            if( has_own_flag( cm.flag ) ) {
                tmp += cm.get_mod_val( type, *this );
            }
        }
        set_var( get_clothing_mod_val_key( type ), tmp );
    }
}

item_location_type item::where() const
{
    if( !loc ) {
    if( !saved_loc ) {
            debugmsg( "Tried to find where of an item without a location" );
            return item_location_type::invalid;
        }
        return static_cast<item_location *>( &*saved_loc )->where();
    }
    return static_cast<item_location *>( &*loc )->where();
}

item &item::obtain( Character &ch, int qty, bool costs_moves )
{
    if( costs_moves ) {
        ch.moves -= obtain_cost( ch, qty );
    }
    if( ch.is_worn( *this ) || ch.is_wielding( *this ) ) {
        return *this;
    }
    return ch.i_add( split( qty ) );
}

int item::obtain_cost( const Character &ch, int qty ) const
{
    if( !loc ) {
    debugmsg( "Tried to find obtain cost of an item without a location" );
        return 0;
    }
    return static_cast<item_location *>( &*loc )->obtain_cost( ch, qty, this );
}

std::string item::describe_location( const Character *ch ) const
{
    if( !loc ) {
    if( !saved_loc ) {
            debugmsg( "Tried to describe the location of an item without a location" );
            return "nowhere";
        }
        return saved_loc->describe( ch, this );
    }
    return loc->describe( ch, this );
}

item *item::parent_item() const
{
    const auto location = loc ? loc : saved_loc;
    auto *cont = dynamic_cast<contents_item_location *>( location );
    if( !cont ) {
        return nullptr;
    }
    return cont->parent();
}

std::vector<detached_ptr<item>> item::remove_components()
{
    return components.clear();
}

detached_ptr<item> item::remove_component( item &it )
{
    const auto iter = std::ranges::find( components, &it );
    if( iter != components.end() ) {
        detached_ptr<item> ret;
        components.erase( iter, &ret );
        return ret;
    }
    debugmsg( "Could not find component for removal" );
    return detached_ptr<item>();
}

void item::add_component( detached_ptr<item> &&comp )
{
    components.push_back( std::move( comp ) );
}

const location_vector<item> &item::get_components() const
{
    return components;
}

location_vector<item> &item::get_components()
{
    return components;
}

bool item::init_kill_tracker()
{
    if( kills ) {
        return true;
    } else if( get_option<bool>( "ENABLE_EVENTS" ) ) {
        kills = std::make_unique<kill_tracker>( false );
        return true;
    } else {
        return false;
    }
}
void item::add_monster_kill( mtype_id mon )
{
    if( init_kill_tracker() ) {
        kills->add_monster( mon );
    }
}
void item::add_npc_kill( std::string npc )
{
    if( init_kill_tracker() ) {
        kills->add_npc( npc );
    }
}
void item::show_kill_list()
{
    if( !kills ) {
        debugmsg( "Tried to display empty kill list" );
        return;
    }
    show_kills( *kills );
}
int item::kill_count()
{
    if( !kills ) {
        return 0;
    } else {
        return kills->monster_kill_count() + kills->npc_kill_count();
    }
}

bool cable_connection_data::ups_connected( const item *const cable )
{
    return cable && cable->has_flag( flag_CABLE_SPOOL ) &&
           ( cable_state( cable->get_var( p1_name, 0.0 ) ) == state_UPS ||
             cable_state( cable->get_var( p2_name, 0.0 ) ) == state_UPS );
}

std::optional<cable_connection_data> cable_connection_data::make_data( const item &cable )
{
    if( cable.has_flag( flag_CABLE_SPOOL ) ) {
        return cable_connection_data( cable );
    } else {
        return std::nullopt;
    }
}
