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

static const std::string CLOTHING_MOD_VAR_PREFIX( "clothing_mod_" );


static const ammotype ammo_battery( "battery" );
static const ammotype ammo_plutonium( "plutonium" );






static const itype_id itype_barrel_small( "barrel_small" );
static const itype_id itype_cig_butt( "cig_butt" );
static const itype_id itype_cig_lit( "cig_lit" );
static const itype_id itype_cigar_butt( "cigar_butt" );
static const itype_id itype_cigar_lit( "cigar_lit" );
static const itype_id itype_joint_roach( "joint_roach" );
static const itype_id itype_plut_cell( "plut_cell" );
static const itype_id itype_UPS( "UPS" );
static const itype_id itype_bio_armor( "bio_armor" );

static const skill_id skill_weapon( "weapon" );






static const std::string flag_LIQUIDCONT( "LIQUIDCONT" );



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
