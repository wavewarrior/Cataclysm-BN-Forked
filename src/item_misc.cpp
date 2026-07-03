// Item miscellaneous: explosion, rotten-away, holster, components, artifact effects,
// seed/plant, type_name, label, location, kill tracker, clothing mods, etc.
// — split out of item.cpp. .cpp-only, no API changes.

#include "active_tile_data_def.h"
#include "ammo.h"
#include "ascii_art.h"
#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "cached_item_options.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "catalua_icallback_actor.h"
#include "character.h"
#include "character_encumbrance.h"
#include "character_functions.h"
#include "character_id.h"
#include "character_martial_arts.h"
#include "character_stat.h"
#include "cloning_utils.h"
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
#include "item.h"
#include "item_cable.h"
#include "item_category.h"
#include "item_factory.h"
#include "item_group.h"
#include "iteminfo_format_utils.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "kill_tracker.h"
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
#include "profile.h"
#include "projectile.h"
#include "ranged.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "relic.h"
#include "requirements.h"
#include "ret_val.h"
#include "rng.h"
#include "rot.h"
#include "scores_ui.h"
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

#include <algorithm>
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
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_set>

// File-scope id constants (moved with misc methods; internal linkage).
static const std::string USED_BY_IDS( "USED_BY_IDS" );
static const std::string flag_LIQUIDCONT( "LIQUIDCONT" );
static const skill_id skill_weapon( "weapon" );

bool item::has_rotten_away() const
{
    if( is_corpse() && !can_revive() ) {
    return get_rot() > 10_days;
    } else {
        return is_food() && get_relative_rot() > 2.0;
    }
}

bool item::already_used_by_player( const player& p ) const
{
    const auto it = item_vars_.find( USED_BY_IDS );
    if( it == item_vars_.end() ) { return false; }
    // USED_BY_IDS always starts *and* ends with a ';', the search string
    // ';<id>;' matches at most one part of USED_BY_IDS, and only when exactly that
    // id has been added.
    const std::string needle = string_format( ";%d;", p.getID().get_value() );
    return it->second.find( needle ) != std::string::npos;
}

void item::mark_as_used_by_player( const player& p )
{
    std::string& used_by_ids = item_vars_[USED_BY_IDS];
    if( used_by_ids.empty() ) {
        // *always* start with a ';'
        used_by_ids = ";";
    }
    // and always end with a ';'
    used_by_ids += string_format( "%d;", p.getID().get_value() );
}

bool item::can_holster( const item& obj, bool ignore ) const
{
    if( !type->can_use( "holster" ) ) {
    return false; // item is not a holster
}

const holster_actor* ptr = dynamic_cast<const holster_actor *>(
                               type->get_use( "holster" )->get_actor_ptr() );
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
    for( const item * const& elem : components ) {
        if( !elem->has_flag( flag_BYPRODUCT ) ) {
            const std::string name = elem->display_name();
            counts[name]++;
        }
    }
    return enumerate_as_string(
               counts.begin(), counts.end(),
    []( const std::pair<std::string, int> &entry ) -> std::string {
        if( entry.second != 1 )
        {
            return string_format(
                pgettext( "components count", "%d x %s" ), entry.second, entry.first );
        } else
        {
            return entry.first;
        }
    },
    enumeration_conjunction::none );
}

uint64_t item::make_component_hash() const
{
    // First we need to sort the IDs so that identical ingredients give identical hashes.
    std::multiset<std::string> id_set;
    for( const item * const& it : components ) { id_set.insert( it->typeId().str() ); }

    std::string concatenated_ids;
    for( const std::string& id : id_set ) { concatenated_ids += id; }

    std::hash<std::string> hasher;
    return hasher( concatenated_ids );
}


void item::mod_charges( int mod )
{
    if( has_infinite_charges() ) { return; }

    if( !count_by_charges() ) {
        debugmsg( "Tried to remove %s by charges, but item is not counted by charges.", tname() );
    } else if( mod < 0 && charges + mod < 0 ) {
        debugmsg( "Tried to remove charges that do not exist, removing maximum available charges "
                  "instead." );
        charges = 0;
    } else if( mod > 0 && charges >= INFINITE_CHARGES - mod ) {
        charges = INFINITE_CHARGES - 1; // Highly unlikely, but finite charges should not become
        // infinite.
    } else {
        charges += mod;
    }
}

bool item::has_effect_when_wielded( art_effect_passive effect ) const
{
    if( !type->artifact ) { return false; }
const std::vector<art_effect_passive> &ew = type->artifact->effects_wielded;
return std::ranges::contains( ew, effect );
}

bool item::has_effect_when_worn( art_effect_passive effect ) const
{
    if( !type->artifact ) { return false; }
const std::vector<art_effect_passive> &ew = type->artifact->effects_worn;
return std::ranges::contains( ew, effect );
}

bool item::has_effect_when_carried( art_effect_passive effect ) const
{
    if( !type->artifact ) { return false; }
const std::vector<art_effect_passive> &ec = type->artifact->effects_carried;
if( std::ranges::contains( ec, effect ) ) { return true; }
for( const item * i : contents.all_items_top() ) {
        if( i->has_effect_when_carried( effect ) ) { return true; }
    }
    return false;
}

bool item::is_seed() const { return type->is_seed(); }

time_duration item::get_plant_epoch() const
{
    if( !type->seed ) { return 0_turns; }
return type->seed->get_plant_epoch();
}

std::string item::get_plant_name() const
{
    if( !type->seed ) { return std::string{}; }
return type->seed->plant_name.translated();
}

bool item::is_dangerous() const
{
    if( has_flag( flag_DANGEROUS ) ) { return true; }

// Note: Item should be dangerous regardless of what type of a container is it
// Visitable interface would skip some options
for( const item * it : contents.all_items_top() ) {
    if( it->is_dangerous() ) { return true; }
    }
    return false;
}

bool item::is_tainted() const { return corpse && corpse->has_flag( MF_POISON ); }

bool item::is_soft() const
{
    const std::vector<material_id> mats = made_of();
    return std::ranges::any_of( mats, []( const material_id & mid ) { return mid.obj().soft(); } );
}

bool item::is_reloadable() const
{
    if( has_flag( flag_NO_RELOAD ) && !has_flag( flag_VEHICLE ) ) {
    return false; // turrets ignore NO_RELOAD flag

} else if( is_bandolier() || is_holster() ) {
    return true;

} else if( is_container() ) {
    // TODO: Make buckets actually reloadable using reload menu
    // This would be done via locking this off by weather or not it was wielded or on dirt most
    // likely
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
    for( const conditional_name& cname : type->conditional_names ) {
        // Lambda for recursively searching for a item ID among all components.
        std::function<bool( std::vector<item*> )> component_id_contains =
        [&]( const std::vector<item*> &components ) {
            for( const item * component : components ) {
                if( component->typeId().str().find( cname.condition ) != std::string::npos
                    || component_id_contains( component->components.as_vector() ) ) {
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
            ret_name = string_format(
                           pgettext( "corpse ownership qualifier", "%1$s of a %2$s" ), ret_name,
                           corpse->nname() );
        } else {
            //~ %1$s: name of corpse with modifiers;  %2$s: proper name;  %3$s: species name
            ret_name = string_format(
                           pgettext( "corpse ownership qualifier", "%1$s of %2$s, %3$s" ), ret_name, corpse_name,
                           corpse->nname() );
        }
    }

    return ret_name;
}

const mtype *item::get_corpse_mon() const { return corpse; }

std::string item::get_corpse_name()
{
    if( corpse_name.empty() ) { return std::string(); }
    return corpse_name;
}

std::string item::nname( const itype_id& id, unsigned int quantity ) { return id->nname( quantity ); }

bool item::count_by_charges( const itype_id& id ) { return id->count_by_charges(); }


bool item::has_label() const { return has_var( "item_label" ); }

std::string item::label( unsigned int quantity ) const
{
    if( has_label() ) { return get_var( "item_label" ); }

    return type_name( quantity );
}

bool item::has_infinite_charges() const { return charges == INFINITE_CHARGES; }

skill_id item::contextualize_skill( const skill_id& id ) const
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



time_duration item::age() const { return calendar::turn - birthday(); }

void item::set_age( const time_duration& age ) { set_birthday( time_point( calendar::turn ) - age ); }

void item::legacy_fast_forward_time()
{
    const time_duration tmp_bday = ( bday - calendar::turn_zero ) * 6;
    bday = calendar::turn_zero + tmp_bday;

    rot *= 6;

    const time_duration tmp_rot = ( last_rot_check - calendar::turn_zero ) * 6;
    last_rot_check = calendar::turn_zero + tmp_rot;
}

bool item::is_active() const { return active; }

time_point item::birthday() const { return bday; }

void item::set_birthday( const time_point& bday )
{
    this->bday = std::max( calendar::turn_zero, bday );
}

bool item::is_upgrade() const
{
    if( !type->bionic ) { return false; }
return type->bionic->is_upgrade;
}

int item::get_min_str() const
{
    if( type->gun ) {
    int min_str = type->min_str;
    for( const item * mod : gunmods() ) { min_str += mod->type->gunmod->min_str_required_mod; }
        return min_str > 0 ? min_str : 0;
    } else {
        return type->min_str;
    }
}

std::vector<item_comp> item::get_uncraft_components() const
{
    std::vector<item_comp> ret;
    if( components.empty() ) {
        // If item wasn't crafted with specific components use default recipe
        std::vector<std::vector<item_comp>> recipe =
            recipe_dictionary::get_uncraft( typeId() ).disassembly_requirements().get_components();
        for( std::vector<item_comp> &component : recipe ) { ret.push_back( component.front() ); }
    } else {
        // Make a new vector of components from the registered components
        for( const item * const& component : components ) {
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

void item::set_favorite( const bool favorite ) { is_favorite = favorite; }

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

item &item::obtain( Character& ch, int qty, bool costs_moves )
{
    if( costs_moves ) { ch.moves -= obtain_cost( ch, qty ); }
    if( ch.is_worn( *this ) || ch.is_wielding( *this ) ) { return *this; }
    return ch.i_add( split( qty ) );
}

int item::obtain_cost( const Character& ch, int qty ) const
{
    if( !loc ) {
    debugmsg( "Tried to find obtain cost of an item without a location" );
        return 0;
    }
    return static_cast<item_location *>( &*loc )->obtain_cost( ch, qty, this );
}

std::string item::describe_location( const Character* ch ) const
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
    auto* cont = dynamic_cast<contents_item_location *>( location );
    if( !cont ) { return nullptr; }
    return cont->parent();
}

std::vector<detached_ptr<item>> item::remove_components() { return components.clear(); }

detached_ptr<item> item::remove_component( item& it )
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

void item::add_component( detached_ptr<item>&& comp ) { components.push_back( std::move( comp ) ); }

const location_vector<item> &item::get_components() const { return components; }

location_vector<item> &item::get_components() { return components; }

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
    if( init_kill_tracker() ) { kills->add_monster( mon ); }
}
void item::add_npc_kill( std::string npc )
{
    if( init_kill_tracker() ) { kills->add_npc( npc ); }
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

bool cable_connection_data::ups_connected( const item* const cable )
{
    return cable && cable->has_flag( flag_CABLE_SPOOL )
           && ( cable_state( cable->get_var( p1_name, 0.0 ) ) == state_UPS
                || cable_state( cable->get_var( p2_name, 0.0 ) ) == state_UPS );
}

std::optional<cable_connection_data> cable_connection_data::make_data( const item& cable )
{
    if( cable.has_flag( flag_CABLE_SPOOL ) ) {
        return cable_connection_data( cable );
    } else {
        return std::nullopt;
    }
}

int item::get_chapters() const
{
    if( !type->book ) { return 0; }
return type->book->chapters;
}

int item::get_remaining_chapters( const Character& ch ) const
{
    const std::string var = string_format( "remaining-chapters-%d", ch.getID().get_value() );
    return get_var( var, get_chapters() );
}

void item::mark_chapter_as_read( const Character& ch )
{
    const std::string var = string_format( "remaining-chapters-%d", ch.getID().get_value() );
    if( type->book && type->book->chapters == 0 ) {
        // books without chapters will always have remaining chapters == 0, so we don't need to
        // store them
        erase_var( var );
        return;
    }
    const int remain = std::max( 0, get_remaining_chapters( ch ) - 1 );
    set_var( var, remain );
}

std::vector<std::pair<const recipe *, int>> item::get_available_recipes( const Character& u ) const
{
    std::vector<std::pair<const recipe *, int>> recipe_entries;
    if( is_book() ) {
        for( const book_recipe& elem : type->book->recipes ) {
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
            if( next_string_index == std::string::npos ) { break; }
            std::string new_recipe =
                recipes.substr( first_string_index, next_string_index - first_string_index );
            const recipe* r = &recipe_id( new_recipe ).obj();
            if( u.get_skill_level( r->skill_used ) >= r->difficulty ) {
                recipe_entries.emplace_back( r, r->difficulty );
            }
            first_string_index = next_string_index + 1;
        }
    }
    return recipe_entries;
}
