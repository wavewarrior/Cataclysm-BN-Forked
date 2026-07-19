
#include "coop_checksum.h"

#include "avatar.h"
#include "coop_session.h"
#include "game.h"
#include "item.h"
#include "map.h"
#include "mapdata.h"
#include "mapbuffer.h"
#include "npc.h"
#include "submap.h"
#include "vehicle.h"

auto coop_world_checksum( int radius ) -> uint64_t
{
    auto h = COOP_FNV_OFFSET;
    const tripoint_abs_sm abs_sub = g->m.get_abs_sub();

    // 1. Terrain + furniture in the sync radius (submap grid).
    for( int dy = -radius; dy <= radius; ++dy ) {
        for( int dx = -radius; dx <= radius; ++dx ) {
            const tripoint_abs_sm sm_pos{
                abs_sub.x() + dx, abs_sub.y() + dy, abs_sub.z()};
            const submap *sm = MAPBUFFER.lookup_submap( sm_pos );
            if( !sm ) {
                continue;
            }
            // Hash submap coordinates for ordering.
            h = coop_fnv1a_mix( h, static_cast<uint64_t>( sm_pos.x() ) );
            h = coop_fnv1a_mix( h, static_cast<uint64_t>( sm_pos.y() ) );
            h = coop_fnv1a_mix( h, static_cast<uint64_t>( sm_pos.z() ) );
            // Terrain + furniture per tile in 12×12 submap.
            for( int ty = 0; ty < SEEY; ++ty ) {
                for( int tx = 0; tx < SEEX; ++tx ) {
                    const point_sm_ms p{tx, ty};
                    h = coop_fnv1a_mix( h,
                                        static_cast<uint64_t>( sm->get_ter( p ).to_i() ) );
                    h = coop_fnv1a_mix( h,
                                        static_cast<uint64_t>( sm->get_furn( p ).to_i() ) );
                }
            }
        }
    }

    // 2. Items on ground in the sync radius (bubble-relative scan).
    //    Hash item count + type per tile — full item serialization is too expensive.
    for( int dy = -radius; dy <= radius; ++dy ) {
        for( int dx = -radius; dx <= radius; ++dx ) {
            const tripoint_abs_sm sm_pos{
                abs_sub.x() + dx, abs_sub.y() + dy, abs_sub.z()};
            for( int ty = 0; ty < SEEY; ++ty ) {
                for( int tx = 0; tx < SEEX; ++tx ) {
                    const tripoint_abs_ms abs_tile{
                        sm_pos.x() * SEEX + tx,
                        sm_pos.y() * SEEY + ty,
                        sm_pos.z()};
                    const tripoint_bub_ms bub = g->m.abs_to_bub( abs_tile );
                    if( !g->m.inbounds( bub ) ) {
                        continue;
                    }
                    const auto &stack = g->m.i_at( bub );
                    h = coop_fnv1a_mix( h, static_cast<uint64_t>( stack.size() ) );
                    for( const auto *it : stack ) {
                        if( it ) {
                            h = coop_fnv1a_mix( h,
                                                static_cast<uint64_t>(
                                                    std::hash<std::string> {}( it->typeId().str() ) ) );
                        }
                    }
                }
            }
        }
    }

    // 3. Proxy NPC position.
    const auto &sess = coop_session::get();
    if( sess.proxy_npc_id.is_valid() ) {
        const npc *proxy = g->critter_by_id<npc>( sess.proxy_npc_id );
        if( proxy ) {
            const auto pp = proxy->abs_pos();
            h = coop_fnv1a_mix( h, static_cast<uint64_t>( pp.x() ) );
            h = coop_fnv1a_mix( h, static_cast<uint64_t>( pp.y() ) );
            h = coop_fnv1a_mix( h, static_cast<uint64_t>( pp.z() ) );
        }
    }

    // 4. Host avatar position.
    {
        const auto hp = g->u.abs_pos();
        h = coop_fnv1a_mix( h, static_cast<uint64_t>( hp.x() ) );
        h = coop_fnv1a_mix( h, static_cast<uint64_t>( hp.y() ) );
        h = coop_fnv1a_mix( h, static_cast<uint64_t>( hp.z() ) );
    }

    return h;
}

