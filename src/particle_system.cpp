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
        if( !p.alive || p.path.size() < 2 || p.duration <= 0.f ) {
            p.alive = false;
            continue;
        }

        const auto elapsed = wall_now - p.start_wall;
        const auto progress = std::clamp( elapsed / static_cast<double>( p.duration ), 0.0, 1.0 );

        if( progress >= 1.0 ) {
            // Flight complete — snap to final waypoint, mark for removal.
            p.tile = p.path.back();
            p.off_x = 0.f;
            p.off_y = 0.f;
            p.alive = false;
            continue;
        }

        // Map progress to a position between two waypoints.
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
    }

    std::erase_if( particles_, []( const particle & p ) {
        return !p.alive;
    } );
}
