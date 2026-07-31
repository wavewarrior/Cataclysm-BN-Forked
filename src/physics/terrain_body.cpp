#include "terrain_body.h"
#include "vehicle_shape.h"   // TILE_M
#include "filter_bits.h"
#include "map.h"
#include "game_constants.h"  // SEEX, SEEY
#include <ranges>

namespace physics {

auto classify_tile( const map &m, tripoint_bub_ms bub ) -> tile_body_class
{
    if( !m.inbounds( bub ) ) { return tile_body_class::passable; }

    // Passability decides whether a tile gets a collider at all; bashability only
    // decides how bouncy it is once it has one.
    //
    // The previous order tested is_bashable_ter_furn() before passability, so any
    // tile that could be smashed got a body even when it was open ground you drive
    // on.  t_pavement is bashable — measured 144 of 144 tiles in a pavement submap
    // classified `bashable` — so every road tile became a collider and vehicles
    // ground to a halt on the road surface (measured: velocity 2000 -> 317 in a
    // single turn once colliders existed).
    if( !m.impassable_ter_furn( bub ) ) { return tile_body_class::passable; }

    // Impassable, so it needs a body.  Bashable impassables (windows, fences) get
    // the softer restitution; solid ones (walls) get the hard value.
    if( m.is_bashable_ter_furn( bub, false ) ) { return tile_body_class::bashable; }
    return tile_body_class::solid;
}

auto build_submap_terrain_bodies( b2WorldId         world,
                                   const map        &m,
                                   tripoint_bub_ms   bub_origin ) -> std::vector<b2BodyId>
{
    namespace views = std::views;

    const auto z     = bub_origin.z();
    // Single z-bit shared by all body types; groupIndex separates categories.
    const auto z_bit = physics::z_category_bit( z );

    std::vector<b2BodyId> bodies;
    bodies.reserve( 64 );

    for( const auto lx : views::iota( 0, SEEX ) ) {
        for( const auto ly : views::iota( 0, SEEY ) ) {
            const auto bub = tripoint_bub_ms{ bub_origin.x() + lx, bub_origin.y() + ly, z };
            const auto cls = classify_tile( m, bub );
            if( cls == tile_body_class::passable ) { continue; }

            // Anchor the body at the tile's INTEGER coordinate, with the box shape
            // centred on it below - so this collider spans [x-0.5, x+0.5] in tile
            // units. That is NOT the renderer's convention (there (x,y) is the
            // tile's top-left), but every physics body uses this same anchor -
            // vehicles, creatures, the per-turn syncs and the readback - and only
            // relative geometry decides contacts, so simulation is unaffected.
            // Anything that draws or compares these against render coordinates
            // must add the half-tile (see physics_debug_draw.cpp::to_tile).
            const auto cx = static_cast<float>( bub.x() ) * TILE_M;
            const auto cy = static_cast<float>( bub.y() ) * TILE_M;

            auto bdef     = b2DefaultBodyDef();
            bdef.type     = b2_staticBody;
            bdef.position = { cx, cy };
            const auto bid = b2CreateBody( world, &bdef );

            auto sdef                = b2DefaultShapeDef();
            sdef.friction            = 0.5f;
            sdef.restitution         = ( cls == tile_body_class::solid ) ? 0.1f : 0.4f;
            sdef.filter.categoryBits = z_bit;
            sdef.filter.maskBits     = z_bit;
            sdef.filter.groupIndex   = physics::terrain_group;
            sdef.enableContactEvents = true;

            // Half-extent slightly less than TILE_M/2 to avoid ghost-vertex contacts
            // between adjacent solid tiles sharing an edge.
            const auto poly = b2MakeBox( TILE_M * 0.499f, TILE_M * 0.499f );
            b2CreatePolygonShape( bid, &sdef, &poly );

            // Tag bashable bodies so on_submap_unloaded / on_tile_bashed can identify them.
            if( cls == tile_body_class::bashable ) {
                b2Body_SetUserData( bid,
                    reinterpret_cast<void *>( encode_tile_pos( bub ) ) );
            }

            bodies.push_back( bid );
        }
    }
    return bodies;
}

} // namespace physics
