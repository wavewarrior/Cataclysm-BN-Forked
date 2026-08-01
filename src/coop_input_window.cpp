#include "coop_input_window.h"

#include "coop_proto.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <ranges>

namespace
{
/// Treat a non-finite estimate as "no measurement" rather than poisoning the window.
auto sanitize_ms( double ms ) -> double { return std::isfinite( ms ) ? ms : 0.0; }
} // namespace

auto coop_tick_cost_tracker::sample( double tick_ms ) -> void
{
    // A negative or non-finite sample means the clock jumped; keep the previous estimate.
    if( !std::isfinite( tick_ms ) || tick_ms < 0.0 ) { return; }
    if( ewma_ms == 0.0 ) {
        ewma_ms = tick_ms;
        return;
    }
    ewma_ms = COOP_INPUT_EWMA_ALPHA * tick_ms + ( 1.0 - COOP_INPUT_EWMA_ALPHA ) * ewma_ms;
}

auto coop_input_window_ms( double local_ewma_ms, double remote_ewma_ms ) -> double
{
    const auto slower = std::max( sanitize_ms( local_ewma_ms ), sanitize_ms( remote_ewma_ms ) );
    return std::clamp( slower, COOP_INPUT_WINDOW_MIN_MS, COOP_INPUT_WINDOW_MAX_MS );
}

auto coop_admit_action( std::deque<buffered_action> &q, buffered_action act ) -> std::size_t
{
    namespace ranges = std::ranges;

    q.push_back( std::move( act ) );

    std::size_t evicted = 0;
    while( q.size() > COOP_MAX_QUEUED_ACTIONS ) {
        // Never consider the entry just admitted: the newest intent always survives.
        const auto candidates_end = std::prev( q.end() );
        auto victim = ranges::find_if( q.begin(), candidates_end, &buffered_action::evictable );
        // Nothing evictable ahead of the newest entry — drop the oldest anyway; the bound is hard.
        if( victim == candidates_end ) { victim = q.begin(); }
        q.erase( victim );
        ++evicted;
    }
    return evicted;
}

auto coop_expire_stale_actions( std::deque<buffered_action> &q, double now_ms,
                                double window_ms ) -> std::size_t
{
    namespace ranges = std::ranges;

    if( q.size() <= 1 ) { return 0; }

    // end() - 1 is the most recent entry and is excluded from the scan, so a lone keypress
    // still executes no matter how long the slower side took to resolve the previous tick.
    const auto keep_end = std::prev( q.end() );
    const auto is_stale = [&]( const buffered_action & e ) {
        return e.evictable && ( now_ms - e.enqueued_ms ) > window_ms;
    };
    const auto removed = ranges::remove_if( q.begin(), keep_end, is_stale );
    const auto erased = static_cast<std::size_t>( ranges::distance( removed ) );
    q.erase( removed.begin(), keep_end );
    return erased;
}
