#include "driver_message_delta.h"

#include <cstddef>
#include <cstdint>

auto compute_message_delta( const std::vector<log_entry> &before,
                            const std::vector<log_entry> &after ) -> message_delta
{
    message_delta delta;
    if( after.empty() ) {
        return delta;
    }
    // Nothing was logged before, or the log was cleared or reloaded (its sequence restarted):
    // everything in view is new.
    if( before.empty() || after.back().seq < before.back().seq ) {
        for( const auto &entry : after ) {
            delta.fresh.push_back( entry.text );
        }
        return delta;
    }
    const auto &anchor = before.back();
    for( const auto &entry : after ) {
        // The anchor changed text only when a repeat merged into it.
        const auto repeated = entry.seq == anchor.seq && entry.text != anchor.text;
        if( entry.seq > anchor.seq || repeated ) {
            delta.fresh.push_back( entry.text );
        }
    }
    // Sequence numbers rise by one per entry, so a gap means entries scrolled out of the window.
    delta.lost = after.front().seq > anchor.seq + 1;
    return delta;
}

auto cap_messages( std::vector<std::string> &messages, size_t max_count,
                   size_t max_bytes ) -> bool
{
    auto cut = false;
    if( messages.size() > max_count ) {
        messages.erase( messages.begin(),
                        messages.end() - static_cast<std::ptrdiff_t>( max_count ) );
        cut = true;
    }
    for( auto &message : messages ) {
        if( message.size() <= max_bytes ) {
            continue;
        }
        auto end = max_bytes;
        // Do not split a multi-byte character: back up to the byte that starts one.
        while( end > 0 && ( static_cast<uint8_t>( message[end] ) & 0xC0 ) == 0x80 ) {
            --end;
        }
        message.resize( end );
        cut = true;
    }
    return cut;
}
