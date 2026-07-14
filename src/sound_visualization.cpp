#include "sound_visualization.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "game.h"
#include "lighting/dev_test_lights.h"
#include "map.h"
#include "point.h"

namespace sfx
{

namespace
{

constexpr auto MAX_ACTIVE_PULSES = std::size_t{ 32 };
constexpr float WAVEFRONT_SPEED = 9.0f; // tiles/sec (must match render loop)
constexpr float BFS_MARGIN = 2.0f; // advance BFS this many tiles ahead of wavefront

const auto DIRS = std::array<point, 8> {
    point( 1, 0 ), point( -1, 0 ), point( 0, 1 ),
    point( 0, -1 ), point( 1, 1 ), point( 1, -1 ), point( -1, 1 ), point( -1, -1 )
};

/**
 * Advance the BFS for a single pulse until it covers the wavefront + margin.
 * Resumes from the persistent PQ — no reconstruction needed.
 */
auto advance_pulse_bfs( dev_test_lights::sound_pulse &p, map &here, double now ) -> void
{
    const auto idx = [&]( int dx, int dy ) { return ( dy + p.max_r ) * ( 2 * p.max_r + 1 ) + ( dx + p.max_r ); };

    const auto radius = static_cast<float>( now - p.spawn_s ) * WAVEFRONT_SPEED;
    const auto target_dist = std::min( radius + BFS_MARGIN, static_cast<float>( p.max_r ) );

    while( !p.pq.empty() ) {
        const auto cur = p.pq.top();
        p.pq.pop();

        if( cur.dist > p.best[idx( cur.dx, cur.dy )] ) { continue; } // stale entry
        if( cur.dist >= target_dist ) {
            p.pq.push( cur ); // put it back — covered enough for this frame
            break;
        }

        for( const auto &d : DIRS ) {
            const auto ndx = cur.dx + d.x;
            const auto ndy = cur.dy + d.y;
            if( ndx < -p.max_r || ndx > p.max_r || ndy < -p.max_r || ndy > p.max_r ) { continue; }

            const auto np = tripoint_bub_ms( p.source.x() + ndx, p.source.y() + ndy, p.source.z() );
            if( !here.inbounds( np ) ) { continue; }

            const auto step = ( d.x != 0 && d.y != 0 ) ? 1.41421356f : 1.0f;
            const auto nd = cur.dist + step;
            if( nd > static_cast<float>( p.max_r ) ) { continue; }

            const auto ni = idx( ndx, ndy );
            if( nd >= p.best[ni] ) { continue; }

            p.best[ni] = nd;
            p.field.push_back( { p.source.x() + ndx + 0.5f, p.source.y() + ndy + 0.5f, nd } );

            if( here.light_transparency( np ) > LIGHT_TRANSPARENCY_SOLID ) {
                p.pq.push( { ndx, ndy, nd } );
            }
        }
    }
}

} // namespace

auto emit_sound_pulse( const tripoint_bub_ms& source, float volume ) -> void
{
    if( g == nullptr ) {
        return;
    }

    const auto max_r = std::clamp( static_cast<int>( volume ), 1, 24 );
    const auto side = 2 * max_r + 1;

    auto& pulses = dev_test_lights::sound_pulses;
    if( pulses.size() >= MAX_ACTIVE_PULSES ) {
        pulses.erase( pulses.begin() );
    }

    auto pulse = dev_test_lights::sound_pulse{
        .z = source.z(),
        .volume = volume,
        .spawn_s = dev_test_lights::pulse_now_s(),
        .source = source,
        .max_r = max_r,
        .best = std::vector<float>( static_cast<size_t>( side ) * side,
                                    std::numeric_limits<float>::infinity() ),
    };

    const auto idx = [&]( int dx, int dy ) { return ( dy + max_r ) * side + ( dx + max_r ); };
    pulse.best[idx( 0, 0 )] = 0.f;
    pulse.field.push_back( { source.x() + 0.5f, source.y() + 0.5f, 0.f } );
    pulse.pq.push( { 0, 0, 0.f } );

    pulses.push_back( std::move( pulse ) );
}

auto advance_all_pulses( double now ) -> void
{
    if( g == nullptr ) {
        return;
    }

    auto& pulses = dev_test_lights::sound_pulses;
    if( pulses.empty() ) {
        return;
    }

    auto &here = get_map();

    for( auto &p : pulses ) {
        advance_pulse_bfs( p, here, now );
    }
}

auto sound_pulses_active() -> bool
{
    return !dev_test_lights::sound_pulses.empty();
}

} // namespace sfx