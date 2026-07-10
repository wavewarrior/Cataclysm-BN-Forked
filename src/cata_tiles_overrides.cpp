#include "cata_tiles.h"

#include "mtype.h"
#include "coordinates.h"
#include "vehicle_part.h"
#include "vpart_position.h"

#include <utility>

void cata_tiles::init_draw_radiation_override( const tripoint_bub_ms& p, const int rad )
{
    radiation_override.emplace( p, rad );
}
void cata_tiles::init_draw_terrain_override( const tripoint_bub_ms& p, const ter_id& id )
{
    terrain_override.emplace( p, id );
}
void cata_tiles::init_draw_furniture_override( const tripoint_bub_ms& p, const furn_id& id )
{
    furniture_override.emplace( p, id );
}
void cata_tiles::init_draw_graffiti_override( const tripoint_bub_ms& p, const bool has )
{
    graffiti_override.emplace( p, has );
}
void cata_tiles::init_draw_trap_override( const tripoint_bub_ms& p, const trap_id& id )
{
    trap_override.emplace( p, id );
}
void cata_tiles::init_draw_field_override( const tripoint_bub_ms& p, const field_type_id& id )
{
    field_override.emplace( p, id );
}
void cata_tiles::init_draw_item_override(
    const tripoint_bub_ms& p, const itype_id& id, const mtype_id& mid, const bool hilite )
{
    item_override.emplace( p, std::make_tuple( id, mid, hilite ) );
}
void cata_tiles::init_draw_vpart_override(
    const tripoint_bub_ms& p, const vpart_id& id, const int part_mod, const units::angle veh_dir,
    const bool hilite, point mount )
{
    vpart_override.emplace( p, std::make_tuple( id, part_mod, veh_dir, hilite, mount ) );
}
void cata_tiles::init_draw_below_override( const tripoint_bub_ms& p, const bool draw )
{
    draw_below_override.emplace( p, draw );
}
void cata_tiles::init_draw_monster_override(
    const tripoint_bub_ms& p, const mtype_id& id, const int count, const bool more,
    const Attitude att )
{
    monster_override.emplace( p, std::make_tuple( id, count, more, att ) );
}

void cata_tiles::void_radiation_override() { radiation_override.clear(); }
void cata_tiles::void_terrain_override() { terrain_override.clear(); }
void cata_tiles::void_furniture_override() { furniture_override.clear(); }
void cata_tiles::void_graffiti_override() { graffiti_override.clear(); }
void cata_tiles::void_trap_override() { trap_override.clear(); }
void cata_tiles::void_field_override() { field_override.clear(); }
void cata_tiles::void_item_override() { item_override.clear(); }
void cata_tiles::void_vpart_override() { vpart_override.clear(); }
void cata_tiles::void_draw_below_override() { draw_below_override.clear(); }
void cata_tiles::void_monster_override() { monster_override.clear(); }
bool cata_tiles::has_draw_override( const tripoint_bub_ms& p ) const
{
    return radiation_override.contains( p ) || terrain_override.contains( p )
    || furniture_override.contains( p ) || graffiti_override.contains( p )
    || trap_override.contains( p ) || field_override.contains( p ) || item_override.contains( p )
    || vpart_override.contains( p ) || draw_below_override.contains( p )
    || monster_override.contains( p );
}
