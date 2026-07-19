#ifdef COOP_ENABLED

#include "coop_packets.h"
#include "coop_proto.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>

/// libFuzzer entry point — exercise every packet parse function with
/// arbitrary byte sequences.  No crash or undefined behaviour from any
/// input is the invariant; std::exceptions are expected and swallowed.
extern "C" auto LLVMFuzzerTestOneInput( const uint8_t *data, size_t size ) -> int
{
    const std::string payload( reinterpret_cast<const char *>( data ), size );

    try {
        parse_world_seed_packet( payload );
    } catch( ... ) {
        // expected for malformed input
    }

    try {
        parse_action_packet( payload );
    } catch( ... ) {
    }

    try {
        parse_join_info_packet( payload );
    } catch( ... ) {
    }

    try {
        parse_sync_header( payload );
    } catch( ... ) {
    }

    try {
        parse_vertical_move_ctx( payload );
    } catch( ... ) {
    }

    return 0;
}

#else // !COOP_ENABLED

/// Stub so the file compiles without -DCOOP_ENABLED.
auto main() -> int
{
    return 0;
}

#endif // COOP_ENABLED
