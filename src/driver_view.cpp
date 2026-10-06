#include "driver_view.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "avatar.h"
#include "coordinates.h"
#include "creature.h"
#include "driver_items.h"
#include "game.h"
#include "item.h"
#include "json.h"
#include "map.h"
#include "mapdata.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "point.h"
#include "type_id.h"
#include "vpart_position.h"

namespace driver_view
{

namespace
{

/// Entries one list carries before it is cut. With the byte budget below they keep the whole
/// response within about 1.5K tokens even when the lean part of it is as long as it can be.
constexpr size_t max_creatures = 10;
constexpr size_t max_items = 16;
/// What the two lists together may take, estimated at each entry's text plus its fixed members.
constexpr size_t max_entity_bytes = 2000;
constexpr size_t entry_bytes = 56;
constexpr size_t creature_extra_bytes = 18;
/// The longest an item id is: the decimal digits of a 64-bit number.
constexpr size_t max_item_id_bytes = 20;
/// What the members around the grid, the legend and the lists take: their names and brackets.
constexpr size_t framing_bytes = 120;
/// What one grid row takes beyond its characters, and one legend entry beyond its meaning.
constexpr size_t row_framing_bytes = 3;
constexpr size_t legend_framing_bytes = 8;

/// One character of the grid and what it stands for.
struct glyph {
    char symbol;
    std::string_view meaning;
};

constexpr glyph you = { '@', "you" };
constexpr glyph unseen = { '?', "out of sight or off the loaded map" };
constexpr glyph monster_glyph = { 'M', "monster" };
constexpr glyph person_glyph = { 'N', "person" };
constexpr glyph items_glyph = { '*', "items" };
constexpr glyph vehicle_glyph = { 'V', "vehicle part" };
constexpr glyph furniture_glyph = { '&', "furniture" };
constexpr glyph stairs_up = { '<', "stairs up" };
constexpr glyph stairs_down = { '>', "stairs down" };
constexpr glyph closed_door = { '+', "closed door or window" };
constexpr glyph open_door = { '\'', "open door or window" };
constexpr glyph deep_water = { 'W', "deep water" };
constexpr glyph shallow_water = { '~', "shallow water" };
constexpr glyph tree = { 'T', "tree" };
constexpr glyph shrub = { 's', "shrub" };
constexpr glyph open_air = { '_', "open air, no floor" };
constexpr glyph solid = { '#', "wall or solid obstacle" };
constexpr glyph see_through = { '=', "obstacle that can be seen past" };
constexpr glyph ground = { '.', "ground or floor" };

/// What the terrain of a tile is, apart from anything standing or lying on it.
auto terrain_glyph( const map &here, const tripoint_bub_ms &pos ) -> glyph
{
    const ter_t &terrain = here.ter( pos ).obj();
    // Deep water lets the avatar dive down, so it carries the stairs flag too: water comes first.
    if( terrain.has_flag( TFLAG_DEEP_WATER ) ) {
        return deep_water;
    }
    if( terrain.has_flag( TFLAG_SWIMMABLE ) || terrain.has_flag( TFLAG_LIQUID ) ) {
        return shallow_water;
    }
    if( terrain.has_flag( TFLAG_GOES_UP ) ) {
        return stairs_up;
    }
    if( terrain.has_flag( TFLAG_GOES_DOWN ) ) {
        return stairs_down;
    }
    if( terrain.has_flag( TFLAG_TREE ) ) {
        return tree;
    }
    if( terrain.has_flag( TFLAG_SHRUB ) ) {
        return shrub;
    }
    if( terrain.has_flag( TFLAG_NO_FLOOR ) ) {
        return open_air;
    }
    if( !terrain.open.is_null() ) {
        return closed_door;
    }
    if( !terrain.close.is_null() ) {
        return open_door;
    }
    if( here.impassable( pos ) ) {
        return here.is_transparent( pos ) ? see_through : solid;
    }
    return ground;
}

/// What a tile with nothing alive on it looks like: items, a vehicle, furniture, then terrain.
auto tile_glyph( const map &here, const tripoint_bub_ms &pos, bool has_items ) -> glyph
{
    if( has_items ) {
    return items_glyph;
}
if( here.veh_at( pos ) ) {
        return vehicle_glyph;
    }
    if( here.has_furn( pos ) ) {
        return furniture_glyph;
    }
    return terrain_glyph( here, pos );
}

/// A creature or item in view, where it is from the avatar and what the driver calls it. An
/// item carries only the item itself: its id is issued, and its name read, when it is written.
struct located {
    int dx = 0;
    int dy = 0;
    item *source = nullptr;
    std::string id;
    std::string name;
    bool hostile = false;
};

/// Nearest first, as the game measures distance, then north to south, west to east.
auto nearer( const located &a, const located &b ) -> bool
{
    const auto key = []( const located & at ) {
        return std::tuple( std::max( std::abs( at.dx ), std::abs( at.dy ) ), at.dy, at.dx );
    };
    return key( a ) < key( b );
}

/// How a creature on a tile is drawn and listed, if the avatar sees one there.
auto creature_entry( const avatar &u, const tripoint_bub_ms &pos, int dx, int dy ) ->
std::optional<std::pair<glyph, located>>
{
    Creature *const critter = g->critter_at( pos, true );
    if( critter == nullptr || !u.sees( *critter ) ) {
        return std::nullopt;
    }
    if( const monster *const mon = dynamic_cast<const monster *>( critter ) ) {
        return std::pair( monster_glyph, located{ .dx = dx, .dy = dy, .id = mon->type->id.str(),
                          .name = driver_items::short_name( mon->name() ),
                          .hostile = mon->attitude( &u ) == MATT_ATTACK } );
    }
    if( const npc *const person = dynamic_cast<const npc *>( critter ) ) {
        return std::pair( person_glyph, located{ .dx = dx, .dy = dy, .id = "npc",
                          .name = driver_items::short_name( person->name ),
                          .hostile = person->is_enemy() } );
    }
    return std::nullopt;
}

/// Everything the avatar sees within `radius` tiles.
struct window {
    int radius = 0;
    std::vector<std::string> grid;
    std::map<char, std::string_view> legend;
    std::vector<located> creatures;
    std::vector<located> items;

    /// What `radius`, the grid and the legend take as text, at most.
    auto fixed_bytes() const -> size_t {
        size_t bytes = framing_bytes;
        for( const std::string &row : grid ) {
            bytes += row.size() + row_framing_bytes;
        }
        for( const auto &[symbol, meaning] : legend ) {
            bytes += meaning.size() + legend_framing_bytes;
        }
        return bytes;
    }
};

auto look_around( int radius ) -> window
{
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms centre = u.bub_pos();

    window seen{ .radius = radius };
    for( int dy = -radius; dy <= radius; ++dy ) {
        std::string row;
        for( int dx = -radius; dx <= radius; ++dx ) {
            const tripoint_bub_ms pos = centre + tripoint_rel_ms( dx, dy, 0 );
            const bool own_tile = dx == 0 && dy == 0;
            glyph shown = unseen;
            if( own_tile || ( here.inbounds( pos ) && u.sees( pos ) ) ) {
                const std::vector<item *> lying = driver_items::items_at( pos );
                for( item *it : lying ) {
                    seen.items.push_back( { .dx = dx, .dy = dy, .source = it } );
                }
                if( own_tile ) {
                    shown = you;
                } else if( auto creature = creature_entry( u, pos, dx, dy ) ) {
                    shown = creature->first;
                    seen.creatures.push_back( std::move( creature->second ) );
                } else {
                    shown = tile_glyph( here, pos, !lying.empty() );
                }
            }
            seen.legend.emplace( shown.symbol, shown.meaning );
            row.push_back( shown.symbol );
        }
        seen.grid.push_back( std::move( row ) );
    }
    return seen;
}

/// How one list of entries is written.
struct list_spec {
    std::string name;
    size_t limit = 0;
    /// Creatures carry `hostile`, items do not.
    bool creatures = false;
};

/// Writes lists into a response, until their shared byte budget is used up.
struct list_writer {
    JsonOut &jo;
    size_t budget = 0;

    /// Writes the nearest of `entries` that fit the spec's limit and what is left of the
    /// budget. Returns true when some were left out.
    auto write( const list_spec &spec, std::vector<located> entries ) -> bool;
};

/// What an entry is called: an item's name is read from the item now.
auto name_of( const located &entry ) -> std::string
{
    return entry.source != nullptr ? driver_items::short_name( entry.source->display_name() ) :
           entry.name;
}

// *INDENT-OFF*
auto list_writer::write( const list_spec &spec, std::vector<located> entries ) -> bool
{
    std::ranges::sort( entries, nearer );
    jo.member( spec.name );
    jo.start_array();
    size_t written = 0;
    for( const located &entry : entries ) {
        const std::string name = name_of( entry );
        const size_t id_bytes = entry.source != nullptr ? max_item_id_bytes : entry.id.size();
        const size_t cost = entry_bytes + id_bytes + name.size() +
                            ( spec.creatures ? creature_extra_bytes : 0 );
        if( written == spec.limit || cost > budget ) {
            break;
        }
        budget -= cost;
        ++written;
        jo.start_object();
        jo.member( "id", entry.source != nullptr ? driver_items::issue_id( *entry.source ) : entry.id );
        jo.member( "name", name );
        jo.member( "dx", entry.dx );
        jo.member( "dy", entry.dy );
        if( spec.creatures ) {
            jo.member( "hostile", entry.hostile );
        }
        jo.end_object();
    }
    jo.end_array();
    return written < entries.size();
}
// *INDENT-ON*

} // namespace

auto write_view( JsonOut &jo, int radius, size_t room ) -> bool
{
    window seen = look_around( radius );
    // Too little room for the whole window: look at a smaller one, down to the avatar's neighbours.
    bool shrunk = false;
    while( seen.radius > 1 && seen.fixed_bytes() > room ) {
        seen = look_around( seen.radius - 1 );
        shrunk = true;
    }
    const size_t fixed = seen.fixed_bytes();

    jo.member( "radius", seen.radius );
    jo.member( "grid" );
    jo.start_array();
    for( const std::string &row : seen.grid ) {
        jo.write( row );
    }
    jo.end_array();
    jo.member( "legend" );
    jo.start_object();
    for( const auto &[symbol, meaning] : seen.legend ) {
        jo.member( std::string( 1, symbol ), std::string( meaning ) );
    }
    jo.end_object();

    list_writer lists{ .jo = jo, .budget = std::min( max_entity_bytes, room > fixed ? room - fixed : 0 ) };
    bool cut = shrunk;
    cut |= lists.write( { .name = "creatures", .limit = max_creatures, .creatures = true },
                        std::move( seen.creatures ) );
    cut |= lists.write( { .name = "items", .limit = max_items }, std::move( seen.items ) );
    return cut;
}

} // namespace driver_view
