#include "driver_combat.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "avatar.h"
#include "avatar_action.h"
#include "creature.h"
#include "effect.h"
#include "game.h"
#include "gun_mode.h"
#include "handle_action_helpers.h"
#include "item.h"
#include "itype.h"
#include "json.h"
#include "map.h"
#include "monster.h"
#include "npc.h"
#include "player_activity.h"
#include "point.h"
#include "ranged.h"
#include "translations.h"
#include "type_id.h"

namespace driver_combat
{

namespace
{

using driver_items::command_result;
using driver_items::outcome;

/// How far a shot may be aimed, in tiles: further than the loaded map reaches in any direction.
constexpr int max_fire_offset = 132;

const efftype_id effect_pet( "pet" );

/// A refusal the driver itself explains: the reason is in `detail` only.
auto refused( std::string detail ) -> command_result
{
    return { .outcome = outcome::refused, .detail = std::move( detail ) };
}

/// A command the game declined with a message of its own: the driver reads the log for it.
auto refused_silently() -> command_result
{
    return { .outcome = outcome::refused };
}

/// Whether the avatar has an activity under way or is asleep, so that a new one would replace it.
auto is_busy( const avatar &u ) -> bool
{
    return ( u.activity && *u.activity ) || u.in_sleep_state();
}

/// The tile offset a compass direction names.
auto direction_offset( const std::string &dir ) -> std::optional<tripoint_rel_ms>
{
    static const std::map<std::string, tripoint_rel_ms> known = {
        { "n", tripoint_rel_ms( 0, -1, 0 ) }, { "ne", tripoint_rel_ms( 1, -1, 0 ) },
        { "e", tripoint_rel_ms( 1, 0, 0 ) }, { "se", tripoint_rel_ms( 1, 1, 0 ) },
        { "s", tripoint_rel_ms( 0, 1, 0 ) }, { "sw", tripoint_rel_ms( -1, 1, 0 ) },
        { "w", tripoint_rel_ms( -1, 0, 0 ) }, { "nw", tripoint_rel_ms( -1, -1, 0 ) },
    };
    const auto found = known.find( dir );
    return found == known.end() ? std::nullopt : std::make_optional( found->second );
}

/// The member `index` of `ja` as a whole number; empty when it is fractional or out of range.
auto whole( const JsonArray &ja, size_t index ) -> std::optional<int>
{
    const double value = ja.get_float( index );
    if( value != std::floor( value ) || value < -max_fire_offset || value > max_fire_offset ) {
        return std::nullopt;
    }
    return static_cast<int>( value );
}

/// What the move spent: the avatar's moves, or an activity the command left running.
auto did_something( const avatar &u, int moves_before ) -> bool
{
    return u.moves < moves_before || is_busy( u );
}

auto run_melee( avatar &u, const parsed_target &target ) -> command_result
{
    const tripoint_bub_ms at = u.bub_pos() + target.offset;
    monster *const mon = g->critter_at<monster>( at, true );
    npc *const np = mon ? nullptr : g->critter_at<npc>( at );
    if( mon == nullptr && np == nullptr ) {
        return refused( "There is nothing there to attack." );
    }
    if( mon != nullptr ) {
        // The game's own test for a creature it will not take a swing at.
        const bool hostile = mon->friendly == 0 && !mon->has_effect( effect_pet ) &&
                             mon->attitude( &u ) != MATT_FRIEND;
        if( !hostile && !mon->is_hallucination() ) {
            return refused( "The " + mon->name() + " is your ally; the driver will not attack it." );
        }
    } else if( !np->is_enemy() ) {
        return refused( np->name + " is not hostile; the driver will not attack them." );
    }

    const int moves_before = u.moves;
    Creature &victim = mon != nullptr ? static_cast<Creature &>( *mon ) : *np;
    avatar_action::melee_attack_while_handling_manual_combat_mode( u, victim );
    if( np != nullptr ) {
        np->make_angry();
    } else if( mon->is_hallucination() ) {
        mon->die( &u );
    }
    return did_something( u, moves_before ) ? command_result{} :
           refused_silently();
}

auto run_fire( avatar &u, const parsed_target &target ) -> command_result
{
    item &weapon = u.primary_weapon();
    if( !u.is_armed() || !weapon.is_gun() ) {
        return refused( u.is_armed() ? "Your " + weapon.tname() + " is not a gun." :
                        "You are not wielding a gun." );
    }
    if( weapon.is_gunmod() ) {
        return refused( "The " + weapon.tname() + " must be attached to a gun; it cannot be fired." );
    }
    map &here = get_map();
    if( !avatar_action::can_fire_weapon( u, here, weapon ) ) {
        return refused_silently();
    }
    gun_mode mode = weapon.gun_current_mode();
    if( mode->ammo_required() > 0 && mode->ammo_remaining() < mode->ammo_required() ) {
        return refused( "Your " + weapon.tname() + " is not loaded." );
    }

    tripoint_bub_ms aim = u.bub_pos() + target.offset;
    if( target.by_direction ) {
        // Along the line as far as the gun carries, or as far as the map goes.
        for( int range = std::max( weapon.gun_range( &u ), 1 ); range > 1; --range ) {
            const tripoint_bub_ms far = u.bub_pos() + tripoint_rel_ms( target.offset.x() * range,
                                        target.offset.y() * range, 0 );
            if( here.inbounds( far ) ) {
                aim = far;
                break;
            }
        }
    }

    const int moves_before = u.moves;
    const int shots = ranged::fire_gun( u, aim, mode.qty, *mode, nullptr );
    return shots > 0 || did_something( u, moves_before ) ? command_result{} :
           refused_silently();
}

auto run_smash( avatar &u, const parsed_target &target ) -> command_result
{
    const int moves_before = u.moves;
    action_handlers::smash( u.bub_pos() + target.offset );
    return did_something( u, moves_before ) ? command_result{} :
           refused_silently();
}

} // namespace

auto command_named( const std::string &name ) -> std::optional<command>
{
    if( name == "melee" ) {
        return command::melee;
    }
    if( name == "fire" ) {
        return command::fire;
    }
    if( name == "smash" ) {
        return command::smash;
    }
    return std::nullopt;
}

auto parse_target( const JsonObject &jo, command kind ) -> parsed_target
{
    const auto bad = []( std::string why ) {
        return parsed_target{ .error = std::move( why ) };
    };
    const bool has_dir = jo.has_member( "dir" );
    const bool has_pos = jo.has_member( "pos" );
    if( has_dir == has_pos ) {
        return bad( has_dir ? "name the target with `dir` or `pos`, not both" :
                    "a target is required: `dir` (n, ne, e, se, s, sw, w, nw) or `pos` ([dx, dy] from the avatar)" );
    }

    parsed_target target;
    if( has_dir ) {
        const std::string dir = jo.get_string( "dir" );
        const std::optional<tripoint_rel_ms> offset = direction_offset( dir );
        if( !offset ) {
            return bad( "unknown direction '" + dir + "': use n, ne, e, se, s, sw, w or nw" );
        }
        target.by_direction = true;
        target.offset = *offset;
        return target;
    }

    const JsonArray pos = jo.get_array( "pos" );
    if( pos.size() != 2 ) {
        return bad( "`pos` must be [dx, dy], the tile's offset from the avatar" );
    }
    const std::optional<int> dx = whole( pos, 0 );
    const std::optional<int> dy = whole( pos, 1 );
    if( !dx || !dy ) {
        return bad( "`pos` must hold whole numbers within " + std::to_string( max_fire_offset ) +
                    " tiles" );
    }
    if( *dx == 0 && *dy == 0 ) {
        return bad( "`pos` is the avatar's own tile: name another tile" );
    }
    const int reach = kind == command::fire ? max_fire_offset : 1;
    if( std::abs( *dx ) > reach || std::abs( *dy ) > reach ) {
        return bad( kind == command::fire ? "`pos` is outside the loaded map" :
                    "`pos` must be an adjacent tile: both offsets between -1 and 1" );
    }
    target.offset = tripoint_rel_ms( *dx, *dy, 0 );
    if( kind == command::fire && !get_map().inbounds( get_avatar().bub_pos() + target.offset ) ) {
        return bad( "`pos` is outside the loaded map" );
    }
    return target;
}

auto run_command( command kind, const parsed_target &target ) -> command_result
{
    avatar &u = get_avatar();
    if( is_busy( u ) ) {
        return refused( "You are busy with something else; let it finish first." );
    }
    switch( kind ) {
        case command::melee:
            return run_melee( u, target );
        case command::fire:
            return run_fire( u, target );
        case command::smash:
            return run_smash( u, target );
    }
    return refused( "Unknown combat command." );
}

} // namespace driver_combat
