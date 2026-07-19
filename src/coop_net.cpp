
#include "coop_net.h"

#include <SDL3_net/SDL_net.h>
#include <array>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

auto write_be32( uint32_t v, uint8_t* out ) -> void
{
    out[0] = static_cast<uint8_t>( v >> 24 );
    out[1] = static_cast<uint8_t>( v >> 16 );
    out[2] = static_cast<uint8_t>( v >> 8 );
    out[3] = static_cast<uint8_t>( v );
}

auto read_be32( const uint8_t* in ) -> uint32_t
{
    return ( static_cast<uint32_t>( in[0] ) << 24 ) | ( static_cast<uint32_t>( in[1] ) << 16 )
    | ( static_cast<uint32_t>( in[2] ) << 8 ) | static_cast<uint32_t>( in[3] );
}

/// Blocking recv of exactly `len` bytes.
/// timeout_ms < 0 = block indefinitely; 0 = non-blocking (return false immediately if empty).
auto recv_exact( NET_StreamSocket* sock, void* buf, std::size_t len, int timeout_ms ) -> bool
{
    auto* p = static_cast<uint8_t *>( buf );
    std::size_t recvd = 0;
    while( recvd < len ) {
        const int n = NET_ReadFromStreamSocket( sock, p + recvd, static_cast<int>( len - recvd ) );
        if( n < 0 ) {
            return false; // socket error / disconnect
        }
        if( n == 0 ) {
            if( timeout_ms == 0 ) {
                return false; // would-block; caller asked for non-blocking
            }
            if( timeout_ms > 0 ) {
                SDL_Delay( 1 );
                timeout_ms -= 1;
                if( timeout_ms <= 0 ) {
                    return false; // timed out
                }
            }
            // timeout_ms < 0: block indefinitely — keep looping
            continue;
        }
        recvd += static_cast<std::size_t>( n );
    }
    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

namespace coop_net
{

/// NET_WriteToStreamSocket queues the entire buffer internally and returns
/// bool — no loop needed; one call sends everything.
auto send( NET_StreamSocket* sock, const std::string& payload ) -> bool
{
    if( payload.size() > 0xFFFF'FFFFu ) { return false; }
std::array<uint8_t, 4> hdr{};
write_be32( static_cast<uint32_t>( payload.size() ), hdr.data() );
    // Header first, then payload — two separate queuing calls are safe because
    // SDL3_net buffers both before flushing to the OS.
    return NET_WriteToStreamSocket( sock, hdr.data(), 4 )
           && NET_WriteToStreamSocket( sock, payload.data(), static_cast<int>( payload.size() ) );
}

auto recv( NET_StreamSocket* sock, std::string& buf, int timeout_ms ) -> bool
{
    std::array<uint8_t, 4> hdr{};
    if( !recv_exact( sock, hdr.data(), 4, timeout_ms ) ) { return false; }
    const uint32_t len = read_be32( hdr.data() );
    if( len == 0 || len > 64u * 1024u * 1024u ) { return false; }
    buf.resize( len );
    // Once the header arrived we know there's a full frame in flight — block.
    return recv_exact( sock, buf.data(), len, -1 );
}

/// Non-consuming readiness check: true if data is waiting on `sock`.
/// Uses NET_WaitUntilInputAvailable with timeout=0 — does NOT consume
/// any bytes, so the follow-up recv() sees the full unmodified stream.
auto poll( NET_StreamSocket* sock ) -> bool
{
    void *s = sock;
    return NET_WaitUntilInputAvailable( &s, 1, 0 ) > 0;
}

} // namespace coop_net

