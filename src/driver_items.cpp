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

#include "activity_handlers.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "creature.h"
#include "driver_message_delta.h"
#include "effect.h"
#include "enums.h"
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
#include "point.h"
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
    std::vector<item *> found;
    const tripoint_bub_ms pos = get_avatar().bub_pos();
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

auto is_here( const item &it ) -> bool
{
    const std::vector<item *> here = items_here();
    return std::ranges::find( here, &it ) != here.end();
}

/// Writes up to `limit` of `items` as `{id, name}` entries; true when some were left out.
auto write_entries( JsonOut &jo, const std::string &name, const std::vector<item *> &items,
                    size_t limit ) -> bool
{
    jo.member( name );
    jo.start_array();
for( item *it : std::span( items ).first( std::min( limit, items.size() ) ) ) {
    jo.start_object();
        jo.member( "id", issue_id( *it ) );
        jo.member( "name", shortened( it->display_name() ) );
        jo.end_object();
    }
    jo.end_array();
    return items.size() > limit;
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
    truncated |= write_entries( jo, "worn", worn, max_worn );

    std::vector<item *> carried;
    for( const std::vector<item *> *stack : u.inv_const_slice() ) {
        carried.insert( carried.end(), stack->begin(), stack->end() );
    }
    truncated |= write_entries( jo, "items", carried, max_carried );

    truncated |= write_entries( jo, "here", items_here(), max_here );
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
    return { .outcome = "refused", .detail = std::move( detail ) };
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
    return { .outcome = "refused" };
}

auto not_carried() -> command_result
{
    return refused( "You are not carrying that item." );
}

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
    return u.wear_possessed( it, true ) ? command_result{} :
           refused_silently();
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
    return u.takeoff( it ) ? command_result{} :
           refused_silently();
}

auto run_wield( avatar &u, item &it ) -> command_result
{
    if( u.is_wielding( it ) ) {
    return { .outcome = "no_effect", .detail = "You are already wielding that." };
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
    return { .outcome = "unsupported", .reason = "blocking_read",
             .detail = "wielding a loaded holster asks a question the driver cannot answer" };
}
if( u.is_armed() ) {
    item &old = u.primary_weapon();
        old.on_unwield( u );
        u.moves -= u.item_handling_cost( old );
        u.i_add( u.remove_primary_weapon() );
    }
    return u.wield( it ) ? command_result{} :
           refused_silently();
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
    return !ref || !is_carried( u, *ref.get() ) ? command_result{} :
           refused_silently();
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
    return !ref || is_carried( u, *ref.get() ) ? command_result{} :
           refused_silently();
}

} // namespace

auto is_query_topic( const std::string &topic ) -> bool
{
    return topic == "inventory" || topic == "effects";
}

auto write_query( JsonOut &jo, const std::string &topic ) -> bool
{
    jo.member( "topic", topic );
    return topic == "inventory" ? write_inventory( jo ) : write_effects( jo );
}

auto issue_id( item &it ) -> std::string
{
    const item_ref ref( it );
    // The public route to an id; it assigns one the first time and keeps it afterwards.
    const item_ref::id_type id = ref.serialize();
    issued.try_emplace( id, ref );
    return std::to_string( id );
}

auto find_item( const std::string &id_text ) -> found_item
{
    item_ref::id_type id = 0;
    const char *const end = id_text.data() + id_text.size();
    const auto [stop, error] = std::from_chars( id_text.data(), end, id );
    if( id_text.empty() || error != std::errc() || stop != end ) {
        return { .error = "item must be an item id as reported by query inventory" };
    }
    const auto known = issued.find( id );
    if( known == issued.end() ) {
        return { .error = "unknown item id " + id_text +
                          ": ids come from query inventory and last for one Episode" };
    }
    if( !known->second ) {
        return { .error = "stale item id " + id_text + ": that item no longer exists" };
    }
    return { .ref = known->second };
}

auto run_command( command kind, const safe_reference<item> &target ) -> command_result
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
    }
    return refused( "Unknown item command." );
}

} // namespace driver_items
