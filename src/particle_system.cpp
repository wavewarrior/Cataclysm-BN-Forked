#include "particle_system.h"

#include <algorithm>
#include <cmath>

void particle_system::emit( particle p )
{
    // Seed initial render state from the first waypoint.
    if( !p.path.empty() ) {
        p.tile = p.path.front();
    }
    p.alive = true;
    particles_.push_back( std::move( p ) );
}

void particle_system::update( const double wall_now )
{
    for( auto &p : particles_ ) {
        if( !p.alive || p.duration <= 0.f ) {
            p.alive = false;
            continue;
        }

        const auto elapsed = wall_now - p.start_wall;
        const auto progress = std::clamp( elapsed / static_cast<double>( p.duration ), 0.0, 1.0 );

        if( progress >= 1.0 ) {
            p.alive = false;
            continue;
        }

        // Stationary particle (impact effect): hold position, fade alpha.
        if( p.path.size() < 2 ) {
            p.alpha = 1.f - static_cast<float>( progress );
            continue;
        }

        // Moving particle: interpolate along waypoints.
        const auto segments = static_cast<double>( p.path.size() - 1 );
        const auto tile_progress = progress * segments;
        const auto idx = static_cast<size_t>(
                             std::min( std::floor( tile_progress ), segments - 1.0 ) );
        const auto frac = static_cast<float>( tile_progress - static_cast<double>( idx ) );

        const auto &from = p.path[idx];
        const auto &to = p.path[idx + 1];
        p.tile = to;
        p.off_x = -static_cast<float>( to.x() - from.x() ) * ( 1.f - frac );
        p.off_y = -static_cast<float>( to.y() - from.y() ) * ( 1.f - frac );

        // Thrown-item tumble: cycle rotation through 8 cardinal/diagonal values.
        if( p.tumble ) {
            constexpr int tumble_rots[] = { 0, 5, 1, 6, 2, 7, 3, 8 };
            p.rotation = tumble_rots[idx % 8];
        }
    }

    std::erase_if( particles_, []( const particle & p ) {
        return !p.alive;
    } );
}
