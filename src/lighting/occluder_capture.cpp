#include "occluder_capture.h"

#include <algorithm>

namespace lighting
{

auto occluder_capture::resize( int w, int h ) -> void
{
    if( w == w_ && h == h_ ) {
        return;
    }
    w_ = std::max( 0, w );
    h_ = std::max( 0, h );
    captured_.assign( static_cast<std::size_t>( w_ ) * static_cast<std::size_t>( h_ ), 0u );
}

auto occluder_capture::begin() -> void
{
    quads_.clear();
    std::ranges::fill( captured_, std::uint8_t{0} );
}

auto occluder_capture::push( const occluder_quad &q ) -> void
{
    const int tx = static_cast<int>( q.tile_x );
    const int ty = static_cast<int>( q.tile_y );
    if( tx < 0 || ty < 0 || tx >= w_ || ty >= h_ ) {
        return;
    }
    quads_.push_back( q );
    captured_[static_cast<std::size_t>( tx ) * static_cast<std::size_t>( h_ ) +
                                    static_cast<std::size_t>( ty )] = 1u;
}

} // namespace lighting
