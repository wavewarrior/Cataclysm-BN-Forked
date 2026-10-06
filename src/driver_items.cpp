#include "driver_items.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "avatar.h"
#include "avatar_functions.h"
#include "bodypart.h"
#include "calendar.h"
#include "character_functions.h"
#include "crafting.h"
#include "creature.h"
#include "driver_message_delta.h"
#include "effect.h"
#include "enums.h"
#include "flag.h"
#include "item.h"
#include "item_handling_util.h"
#include "item_stack.h"
#include "itype.h"
#include "json.h"
#include "map.h"
#include "mapdata.h"
#include "messages.h"
#include "pickup.h"
#include "pickup_token.h"
#include "player_activity.h"
#include "point.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "ret_val.h"
#include "string_formatter.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"
#include "vpart_position.h"

namespace driver_items
{

namespace
{

using item_ref = safe_reference<item>;

/// Every id reported so far, with the reference that keeps its record alive. An id that is not
/// here was never issued in this Episode: the engine's own lookup is not asked about it, since
/// asking would create a record for an item that does not exist.
/// Never destroyed: a reference released during static destruction would lock the engine's
/// record mutex after it has gone, and hang the process at exit.
auto &issued = *new std::unordered_map<item_ref::id_type, item_ref>();

/// Entries one list carries before it is cut, and the longest name an entry carries. They keep
/// the whole response within about 1.5K tokens even when every list is full.
constexpr size_t max_worn = 14;
constexpr size_t max_carried = 20;
constexpr size_t max_here = 10;
constexpr size_t max_effects = 24;
constexpr size_t max_name_bytes = 40;
/// The body part an effect names when it is not tied to one.
const std::string none_bodypart = "num_bp";

auto shortened( std::string text ) -> std::string
{
    std::vector<std::string> one{ std::move( text ) };
    cap_messages( one, 1, max_name_bytes );
    return one.front();
}

auto carried_in_inventory( const avatar &u, const item &it ) -> bool
{
    return std::ranges::any_of( u.inv_const_slice(), [&it]( const std::vector<item *> *stack ) {
        return std::ranges::find( *stack, &it ) != stack->end();
    } );
}

/// Wielded, worn or in the inventory: everything `u` can use without bending down.
auto is_carried( const avatar &u, const item &it ) -> bool
{
    return u.is_wielding( it ) || u.is_worn( it ) || carried_in_inventory( u, it );
}

/// Items the avatar can reach without moving: those on its tile, and in a vehicle's cargo there.
auto items_here() -> std::vector<item *>
{
    return items_at( get_avatar().bub_pos() );
}

auto is_here( const item &it ) -> bool
{
    const auto here = items_here();
    return std::ranges::find( here, &it ) != here.end();
}

/// What one list of the inventory takes: the member it is written under, the items in it, and
/// how many of them a response may carry.
struct entry_list {
    std::string name;
    std::span<item *const> items;
    size_t limit = 0;
};

/// Writes up to the list's limit of its items as `{id, name}` entries; true when some were left out.
auto write_entries( JsonOut &jo, const entry_list &list ) -> bool
{
    jo.member( list.name );
    jo.start_array();
for( item *it : list.items.first( std::min( list.limit, list.items.size() ) ) ) {
    jo.start_object();
        jo.member( "id", issue_id( *it ) );
        jo.member( "name", shortened( it->display_name() ) );
        jo.end_object();
    }
    jo.end_array();
    return list.items.size() > list.limit;
}

auto write_inventory( JsonOut &jo ) -> bool
{
    const avatar &u = get_avatar();
    bool truncated = false;

    jo.member( "wielded" );
    if( u.is_armed() ) {
        item &weapon = u.primary_weapon();
        jo.start_object();
        jo.member( "id", issue_id( weapon ) );
        jo.member( "name", shortened( weapon.display_name() ) );
        jo.end_object();
    } else {
        jo.write_null();
    }

    std::vector<item *> worn;
    for( item *it : u.worn ) {
        worn.push_back( it );
    }
    truncated |= write_entries( jo, { .name = "worn", .items = worn, .limit = max_worn } );

    std::vector<item *> carried;
    for( const std::vector<item *> *stack : u.inv_const_slice() ) {
        carried.insert( carried.end(), stack->begin(), stack->end() );
    }
    truncated |= write_entries( jo, { .name = "items", .items = carried, .limit = max_carried } );

    truncated |= write_entries( jo, { .name = "here", .items = items_here(), .limit = max_here } );
    return truncated;
}

struct effect_row {
    std::string id;
    std::string bp;
    int intensity = 0;
    std::optional<int> turns;
};

auto write_effects( JsonOut &jo ) -> bool
{
    std::vector<effect_row> rows;
    for( const auto &[type, by_part] : get_avatar().get_effects() ) {
        for( const auto &[part, eff] : by_part ) {
            effect_row row = { .id = type.str(), .bp = part.str(), .intensity = eff.get_intensity() };
            if( !eff.is_permanent() ) {
                row.turns = to_turns<int>( eff.get_duration() );
            }
            rows.push_back( std::move( row ) );
        }
    }
    // The engine keeps effects in an unordered map: sort so that two queries read alike.
    std::ranges::sort( rows, []( const effect_row & a, const effect_row & b ) {
        return std::tie( a.id, a.bp ) < std::tie( b.id, b.bp );
    } );

    jo.member( "effects" );
    jo.start_array();
    for( const effect_row &row : std::span( rows ).first( std::min( max_effects, rows.size() ) ) ) {
        jo.start_object();
        jo.member( "id", shortened( row.id ) );
        if( row.bp != none_bodypart ) {
            jo.member( "bp", row.bp );
        }
        jo.member( "intensity", row.intensity );
        if( row.turns ) {
            jo.member( "turns", *row.turns );
        }
        jo.end_object();
    }
    jo.end_array();
    return rows.size() > max_effects;
}

/// A refusal the driver itself explains: the reason is in `detail` only.
auto refused( std::string detail ) -> command_result
{
    return { .outcome = outcome::refused, .detail = std::move( detail ) };
}

/// A refusal the game's rules gave, in the game's own words. The game says such things through
/// its message log, so this does too, and the message reaches `new_messages` as it would for
/// the player.
auto refused_by_game( const std::string &message ) -> command_result
{
    add_msg( m_info, "%s", message );
    return refused( message );
}

/// A command the game declined with a message of its own: the driver reads the log for it.
auto refused_silently() -> command_result
{
    return { .outcome = outcome::refused };
}

auto not_carried() -> command_result
{
    return refused( "You are not carrying that item." );
}

// *INDENT-OFF*
auto run_wear( avatar &u, item &it ) -> command_result
{
    if( u.is_worn( it ) ) {
        return refused_by_game( _( "You are already wearing that." ) );
    }
    if( !u.is_wielding( it ) && !carried_in_inventory( u, it ) ) {
        return not_carried();
    }
    // The game's own rules and message; wearing logs it too, but that path is skipped on refusal.
    if( const ret_val<bool> can = u.can_wear( it ); !can.success() ) {
        return refused_by_game( can.str() );
    }
    return u.wear_possessed( it, true ) ? command_result{} : refused_silently();
}

auto run_take_off( avatar &u, item &it ) -> command_result
{
    if( const ret_val<bool> can = u.can_takeoff( it ); !can.success() ) {
        return refused_by_game( can.str() );
    }
    // The game would ask whether to drop it instead: an agent drops explicitly.
    if( u.volume_carried() + it.volume() > u.volume_capacity_reduced_by( it.get_storage() ) ) {
        return refused( "No room in inventory for your " + it.tname() + "." );
    }
    return u.takeoff( it ) ? command_result{} : refused_silently();
}

auto run_wield( avatar &u, item &it ) -> command_result
{
    if( u.is_wielding( it ) ) {
        return { .outcome = outcome::no_effect, .detail = "You are already wielding that." };
    }
    if( !is_carried( u, it ) ) {
        return not_carried();
    }
    if( const ret_val<bool> can = u.can_wield( it ); !can.success() ) {
        return refused_by_game( can.str() );
    }
    // The game's own unwield asks, in a menu, where to put the old weapon. The driver takes that
    // menu's first option: into the inventory, if it fits.
    if( u.is_armed() ) {
        item &old = u.primary_weapon();
        if( const ret_val<bool> can = u.can_unwield( old ); !can.success() ) {
            return refused_by_game( can.str() );
        }
        if( u.volume_carried() + old.volume() > u.volume_capacity() ) {
            return refused( "No room in inventory for your " + old.tname() + "." );
        }
        if( const auto *unwield_callbacks = old.type->iwieldable_callbacks ) {
            if( !unwield_callbacks->call_can_unwield( u, old ) ) {
                return refused_silently();
            }
        }
    }
    // The game asks whether to draw from a holster: a question nothing here can answer, and it
    // comes only after the old weapon is already put away.
    if( it.get_use( "holster" ) && !it.contents.empty() ) {
        return { .outcome = outcome::unsupported, .reason = "blocking_read",
                 .detail = "wielding a loaded holster asks a question the driver cannot answer" };
    }
    if( u.is_armed() ) {
        item &old = u.primary_weapon();
        old.on_unwield( u );
        u.moves -= u.item_handling_cost( old );
        u.i_add( u.remove_primary_weapon() );
    }
    return u.wield( it ) ? command_result{} : refused_silently();
}

auto run_drop( avatar &u, item &it, const item_ref &ref ) -> command_result
{
    if( !is_carried( u, it ) ) {
        return not_carried();
    }
    if( u.is_wielding( it ) ) {
        if( const ret_val<bool> can = u.can_unwield( it ); !can.success() ) {
            return refused_by_game( can.str() );
        }
    } else if( u.is_worn( it ) ) {
        if( const ret_val<bool> can = u.can_takeoff( it ); !can.success() ) {
            return refused_by_game( can.str() );
        }
    }
    const tripoint_bub_ms pos = u.bub_pos();
    if( !get_map().can_put_items( pos ) ) {
        return refused_by_game( _( "You can't place items here!" ) );
    }

    // What the game's drop activity does, run now instead of over turns.
    std::list<pickup::act_item> plan = pickup::reorder_for_dropping( u,
    { drop_location( it, it.count() ) } );
    std::vector<detached_ptr<item>> dropped = pickup::obtain_and_tokenize_items( u, plan );
    put_into_vehicle_or_drop( u, item_drop_reason::deliberate, dropped, pos, false );
    get_map().process_falling();

    // A merged or destroyed item is gone from the avatar; otherwise it must have left.
    return !ref || !is_carried( u, *ref.get() ) ? command_result{} : refused_silently();
}

auto run_pickup( avatar &u, item &it, const item_ref &ref ) -> command_result
{
    if( is_carried( u, it ) ) {
        return refused( "You are already carrying that." );
    }
    if( !is_here( it ) ) {
        return refused( "That item is not on your tile." );
    }
    // The checks the game's pickup makes before it takes an item, with its messages. Its quiet
    // pickup, which the driver uses because no prompt can be answered, would just skip these.
    if( it.made_of( LIQUID ) ) {
        return refused_by_game( _( "You can't pick up a liquid!" ) );
    }
    if( !u.can_pick_weight( it.weight(), false ) ) {
        return refused_by_game( string_format( _( "The %s is too heavy!" ), it.display_name() ) );
    }
    if( it.is_bucket() && !it.is_container_empty() ) {
        return refused_by_game( string_format( _( "Can't stash %s while it's not empty" ),
                                               it.display_name() ) );
    }
    if( !u.can_pick_volume( it.volume() ) ) {
        return refused_by_game( string_format( _( "Not enough capacity to stash %s" ),
                                               it.display_name() ) );
    }

    std::vector<pickup::pick_drop_selection> targets;
    targets.push_back( { .target = ref, .quantity = std::nullopt, .children = {} } );
    pickup::do_pickup( targets, true );

    // Picked up, or merged into a stack the avatar already carries (the original is then gone).
    return !ref || is_carried( u, *ref.get() ) ? command_result{} : refused_silently();
}

/// Whether the avatar has an activity under way or is asleep, so that a new one would replace it.
auto is_busy( const avatar &u ) -> bool
{
    return ( u.activity && *u.activity ) || u.in_sleep_state();
}

auto busy() -> command_result
{
    return refused( "You are busy with something else; let it finish first." );
}

auto run_eat( avatar &u, item &it, bool anyway ) -> command_result
{
    if( !is_carried( u, it ) ) {
        return not_carried();
    }
    if( u.is_underwater() && !u.has_trait( trait_id( "WATERSLEEP" ) ) ) {
        return refused_by_game( _( "You can't do that while underwater." ) );
    }
    item &comest = u.get_consumable_from( it );
    if( comest.is_null() || it.is_craft() ) {
        return refused_by_game( string_format( _( "You can't eat your %s." ), it.tname() ) );
    }
    // The game asks "eat it anyway?" about whatever this finds, and nothing here can answer.
    // Medicine and the like have no such question.
    const bool asks = comest.is_food();
    if( asks ) {
        const ret_val<edible_rating> can = anyway ? u.can_eat( comest ) : u.will_eat( comest, false );
        if( !can.success() ) {
            return refused_by_game( can.str() );
        }
    }

    const int moves_before = u.moves;
    if( asks && anyway ) {
        // `eat` with force is the game's own "yes". What `consume_item` does around it follows.
        if( u.eat( comest, true ) ) {
            if( comest.charges <= 0 ) {
                comest.detach();
            }
            if( &comest != &it ) {
                it.on_contents_changed();
            }
        }
    } else {
        u.consume( it );
    }
    return u.moves < moves_before ? command_result{} : refused_silently();
}

auto run_use( avatar &u, item &it, const std::string &method ) -> command_result
{
    if( !is_carried( u, it ) ) {
        return not_carried();
    }
    std::string uses;
    std::vector<std::string> names;
    for( const auto &[name, use] : it.type->use_methods ) {
        names.push_back( name );
        uses += ( uses.empty() ? "" : ", " ) + name;
    }
    if( names.empty() ) {
        return refused( "Your " + it.tname() + " has no use." );
    }
    std::string chosen = method;
    if( chosen.empty() ) {
        if( names.size() > 1 ) {
            return refused( "Your " + it.tname() + " has several uses; name one with method: " + uses );
        }
        chosen = names.front();
    } else if( std::ranges::find( names, chosen ) == names.end() ) {
        return refused( "Your " + it.tname() + " has no use called " + chosen + "; it has: " + uses );
    }

    const item_ref ref( it );
    const itype_id type_before = it.typeId();
    const bool active_before = it.is_active();
    const auto charges_before = it.charges;
    const int moves_before = u.moves;
    const bool used = u.invoke_item( &it, chosen );
    // The game reports a use that cost no charge by returning false, so the answer is what the
    // use did: time, an activity, or a change to the item. A use that did none of those was
    // turned down, and the game's message says why.
    const bool changed = !ref || ref.get()->typeId() != type_before ||
                         ref.get()->is_active() != active_before || ref.get()->charges != charges_before;
    return used || changed || u.moves < moves_before || is_busy( u ) ? command_result{} :
           refused_silently();
}

auto run_read( avatar &u, item &it ) -> command_result
{
    if( !is_carried( u, it ) ) {
        return not_carried();
    }
    if( is_busy( u ) ) {
        return busy();
    }
    // Continuous reading skips the menu the game shows for books that teach a skill, and reads
    // the book once.
    return u.read( &it, true ) ? command_result{} : refused_silently();
}

auto run_reload( avatar &u, item &it ) -> command_result
{
    if( !is_carried( u, it ) ) {
        return not_carried();
    }
    if( is_busy( u ) ) {
        return busy();
    }
    static const flag_id reload_and_shoot( "RELOAD_AND_SHOOT" );
    if( it.has_flag( reload_and_shoot ) ) {
        return refused( "Your " + it.tname() + " takes the ammo it is fired with; it needs no reload." );
    }

    item *target = nullptr;
    if( it.is_reloadable() && u.can_reload( it ) ) {
        target = &it;
    } else {
        for( item *mod : it.gunmods() ) {
            if( mod->is_reloadable() && u.can_reload( *mod ) ) {
                target = mod;
                break;
            }
        }
    }
    if( target == nullptr ) {
        if( ( it.is_ammo_container() || it.is_magazine() ) && it.ammo_remaining() > 0 &&
            it.ammo_remaining() == it.ammo_capacity() ) {
            return refused_by_game( string_format( _( "The %s is already fully loaded!" ), it.tname() ) );
        }
        if( it.is_container() && it.is_container_full() ) {
            return refused_by_game( string_format( _( "The %s is already full!" ), it.tname() ) );
        }
        return refused_by_game( string_format( _( "You can't reload a %s!" ), it.tname() ) );
    }
    if( target->is_holster() || target->is_bandolier() ) {
        return { .outcome = outcome::unsupported, .reason = "blocking_read",
                 .detail = "reloading a holster or bandolier asks what to put in it" };
    }

    std::vector<item_reload_option> options;
    character_funcs::list_ammo( u, *target, options, true, false );
    if( options.empty() ) {
        // With nothing to choose from this asks no menu; it says why there is nothing.
        character_funcs::select_ammo( u, *target, false );
        return refused_silently();
    }
    // The game's own order for the choice it would put to the player: cheapest first, and
    // magazines with something in them before empty ones.
    std::ranges::stable_sort( options, []( const item_reload_option & a, const item_reload_option & b ) {
        return a.ammo->ammo_remaining() > b.ammo->ammo_remaining();
    } );
    std::ranges::stable_sort( options, []( const item_reload_option & a, const item_reload_option & b ) {
        return a.moves() < b.moves();
    } );
    std::ranges::stable_sort( options, []( const item_reload_option & a, const item_reload_option & b ) {
        return ( a.ammo->ammo_remaining() != 0 ) > ( b.ammo->ammo_remaining() != 0 );
    } );
    const item_reload_option &best = options.front();
    u.assign_activity( std::make_unique<player_activity>( std::make_unique<reload_activity_actor>(
                           safe_reference<item>( *target ), safe_reference<item>( *best.ammo ),
                           best.qty() ) ) );
    return {};
}
// *INDENT-ON*


auto craft_allowed( const avatar &u, const recipe &rec ) -> command_result
{
    if( morale_crafting_speed_multiplier( u, rec ) <= 0.0f ) {
        return refused_by_game( _( "Your morale is too low to craft such a difficult thing…" ) );
    }
    if( lighting_crafting_speed_multiplier( u, rec ) <= 0.0f ) {
        return refused_by_game( _( "You can't see to craft!" ) );
    }
    if( rec.category == "CC_BUILDING" ) {
        return refused_by_game( _( "Overmap terrain building recipes are not implemented yet!" ) );
    }
    return {};
}
} // namespace

auto items_at( const tripoint_bub_ms &pos ) -> std::vector<item *>
{
    std::vector<item *> found;
    map &here = get_map();
    if( !here.has_flag( "SEALED", pos ) ) {
        for( item *it : here.i_at( pos ) ) {
            found.push_back( it );
        }
    }
    if( const optional_vpart_position vp = here.veh_at( pos ) ) {
        if( const std::optional<vpart_reference> cargo = vp.part_with_feature( "CARGO", false ) ) {
            for( item *it : cargo->vehicle().get_items( cargo->part_index() ) ) {
                found.push_back( it );
            }
        }
    }
    return found;
}

auto truncate_name( std::string text ) -> std::string
{
    return shortened( std::move( text ) );
}

auto outcome_name( const outcome o ) -> std::string_view
{
    switch( o ) {
    case outcome::completed:
        return "completed";
    case outcome::refused:
        return "refused";
    case outcome::no_effect:
        return "no_effect";
    case outcome::unsupported:
        return "unsupported";
    case outcome::blocked:
        return "blocked";
    case outcome::interrupted:
        return "interrupted";
    case outcome::awaiting_input:
        return "awaiting_input";
}
return "completed";
}

auto query_topic_name( const query_topic topic ) -> std::string_view
{
    return topic == query_topic::inventory ? "inventory" : "effects";
}

auto topic_named( const std::string &name ) -> std::optional<query_topic>
{
    if( name == "inventory" ) {
        return query_topic::inventory;
    }
    if( name == "effects" ) {
        return query_topic::effects;
    }
    return std::nullopt;
}

auto write_query( JsonOut &jo, const query_topic topic ) -> bool
{
    jo.member( "topic", std::string( query_topic_name( topic ) ) );
    return topic == query_topic::inventory ? write_inventory( jo ) : write_effects( jo );
}

auto issue_id( item &it ) -> std::string
{
    const item_ref ref( it );
    // The public route to an id; it assigns one the first time and keeps it afterwards.
    const item_ref::id_type id = ref.serialize();
    issued.try_emplace( id, ref );
    return std::to_string( id );
}

auto find_item( const std::string &id_text ) -> std::expected<safe_reference<item>, std::string>
{
    item_ref::id_type id = 0;
    const char *const end = id_text.data() + id_text.size();
    const auto [stop, error] = std::from_chars( id_text.data(), end, id );
    if( id_text.empty() || error != std::errc() || stop != end ) {
        return std::unexpected( "item must be an item id as reported by query inventory" );
    }
    const auto known = issued.find( id );
    if( known == issued.end() ) {
        return std::unexpected( "unknown item id " + id_text +
                                ": ids come from query inventory and last for one Episode" );
    }
    if( !known->second ) {
        return std::unexpected( "stale item id " + id_text + ": that item no longer exists" );
    }
    return known->second;
}

// *INDENT-OFF*
auto run_command( command kind, const safe_reference<item> &target,
                  const command_options &options ) -> command_result
{
    if( !target ) {
        return refused( "That item no longer exists." );
    }
    avatar &u = get_avatar();
    item &it = *target.get();
    switch( kind ) {
        case command::pickup:
            return run_pickup( u, it, target );
        case command::drop:
            return run_drop( u, it, target );
        case command::wield:
            return run_wield( u, it );
        case command::wear:
            return run_wear( u, it );
        case command::take_off:
            return run_take_off( u, it );
        case command::eat:
            return run_eat( u, it, options.anyway );
        case command::use:
            return run_use( u, it, options.method );
        case command::read:
            return run_read( u, it );
        case command::reload:
            return run_reload( u, it );
    }
    return refused( "Unknown item command." );
}

auto recipe_error( const std::string &recipe_text ) -> std::expected<void, std::string>
{
    if( recipe_text.empty() ) {
        return std::unexpected( "recipe must be a recipe id" );
    }
    if( !recipe_id( recipe_text ).is_valid() ) {
        return std::unexpected( "unknown recipe id " + recipe_text );
    }
    return {};
}

auto run_craft( const std::string &recipe_text ) -> command_result
{
    avatar &u = get_avatar();
    const recipe &rec = *recipe_id( recipe_text );
    if( is_busy( u ) ) {
        return busy();
    }
    if( const command_result allowed = craft_allowed( u, rec ); allowed.outcome != outcome::completed ) {
        return allowed;
    }
    if( u.has_recipe( &rec, u.crafting_inventory(), character_funcs::get_crafting_helpers( u ) ) < 0 ) {
        return refused( "You do not know how to make " + rec.result_name() + "." );
    }
    if( !u.can_make( &rec, 1 ) ) {
        return refused( "You lack something to make " + rec.result_name() + ": " +
                        rec.simple_requirements().list_missing() );
    }
    // The game would ask whether to go on with rotten components.
    if( !u.can_start_craft( &rec, recipe_filter_flags::no_rotten, 1 ) ) {
        return refused( "Making " + rec.result_name() + " would use rotten components." );
    }
    u.make_craft( rec.ident(), 1 );
    return is_busy( u ) ? command_result{} : refused_silently();
}

auto run_sleep() -> command_result
{
    avatar &u = get_avatar();
    if( u.is_mounted() ) {
        return refused_by_game( _( "You cannot sleep while mounted." ) );
    }
    if( u.in_sleep_state() ) {
        return { .outcome = outcome::no_effect, .detail = "You are already asleep or trying to sleep." };
    }
    if( is_busy( u ) ) {
        return busy();
    }
    // The game asks every half hour whether to keep trying; the driver's turn limit ends it.
    u.set_value( "sleep_query", "false" );
    u.moves = 0;
    avatar_funcs::try_to_sleep( u, 24_hours );
    return {};
}
// *INDENT-ON*

} // namespace driver_items
