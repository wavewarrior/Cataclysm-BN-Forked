#include "sound_visualization.h"

#include <algorithm>
#include <limits>
#include <queue>
#include <vector>

#include "debug.h"
#include "game.h"
#include "lighting/dev_test_lights.h"
#include "map.h"
#include "point.h"

namespace sfx
{

namespace
{

constexpr auto MAX_ACTIVE_PULSES = std::size_t{ 32 };

} // namespace

auto emit_sound_pulse( const tripoint_bub_ms& source, float volume ) -> void
{
    if( g == nullptr ) {
        return;
    }

    const auto max_r = std::clamp( static_cast<int>( volume ), 1, 24 );

    // Cap total active pulses to prevent unbounded GPU instance growth.
    auto& pulses = dev_test_lights::sound_pulses;
    if( pulses.size() >= MAX_ACTIVE_PULSES ) {
        pulses.erase( pulses.begin() );
    }

    auto &here = get_map();
    const auto side = 2 * max_r + 1;
    const auto idx = [&]( int dx, int dy ) { return ( dy + max_r ) * side + ( dx + max_r ); };
    auto best = std::vector<float>( static_cast<size_t>( side ) * side,
                                    std::numeric_limits<float>::infinity() );
    struct pulse_q {
        int dx, dy;
        float dist;
        auto operator<( const pulse_q &o ) const -> bool { return dist > o.dist; } // min-heap
    };
    auto pq = std::priority_queue<pulse_q> {};
    const auto dirs = std::array<point, 8> { point( 1, 0 ), point( -1, 0 ), point( 0, 1 ),
        point( 0, -1 ), point( 1, 1 ), point( 1, -1 ), point( -1, 1 ), point( -1, -1 )
                                           };
    best[idx( 0, 0 )] = 0.f;
    pq.push( { 0, 0, 0.f } );
    while( !pq.empty() ) {
        const auto cur = pq.top();
        pq.pop();
        if( cur.dist > best[idx( cur.dx, cur.dy )] ) { continue; }
        for( const point &d : dirs ) {
            const auto ndx = cur.dx + d.x;
            const auto ndy = cur.dy + d.y;
            if( ndx < -max_r || ndx > max_r || ndy < -max_r || ndy > max_r ) { continue; }
            const auto np = tripoint_bub_ms( source.x() + ndx, source.y() + ndy, source.z() );
            if( !here.inbounds( np ) ) { continue; }
            const auto step = ( d.x != 0 && d.y != 0 ) ? 1.41421356f : 1.0f;
            const auto nd = cur.dist + step;
            if( nd > static_cast<float>( max_r ) || nd >= best[idx( ndx, ndy )] ) { continue; }
            best[idx( ndx, ndy )] = nd;
            if( here.light_transparency( np ) > LIGHT_TRANSPARENCY_SOLID ) {
                pq.push( { ndx, ndy, nd } );
            }
        }
    }

    auto pulse = dev_test_lights::sound_pulse{ .z = source.z(),
        .volume = volume,
        .spawn_s = dev_test_lights::pulse_now_s() };
    for( int dy = -max_r; dy <= max_r; ++dy ) {
        for( int dx = -max_r; dx <= max_r; ++dx ) {
            const auto d = best[idx( dx, dy )];
            if( std::isinf( d ) ) { continue; }
            pulse.field.push_back( { .tx = source.x() + dx + 0.5f,
                                     .ty = source.y() + dy + 0.5f, .dist = d } );
        }
    }
    pulses.push_back( std::move( pulse ) );
}

} // namespace sfx