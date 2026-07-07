#pragma once
#ifdef COOP_ENABLED

/**
 * In-memory transport simulator for deterministic co-op unit tests.
 *
 * Replaces the real SDL_net socket with a controllable in-process channel:
 *   - Programmable one-way delivery latency (latency_ms)
 *   - Simulated frame loss (loss_rate 0.0–1.0)
 *   - Optional message reorder (reorder flag — swaps last two on enqueue)
 *   - Manual simulated clock via advance(ms)
 *   - Bidirectional wiring: wire_peer() cross-connects two instances so
 *     send() on one lands in the other's inbox
 *
 * No SDL, no sockets, no threads — runs in-process in milliseconds.
 *
 * Usage:
 *   auto host_tx = std::make_shared<coop_sim_transport>();
 *   auto cli_tx  = std::make_shared<coop_sim_transport>();
 *   host_tx->wire_peer(cli_tx.get());  // cross-connect
 *   cli_tx->wire_peer(host_tx.get());
 *   host_tx->latency_ms = 50;          // 50 ms one-way = 100 ms RTT
 *
 *   // Drive both endpoints in lock-step:
 *   for (int t = 0; t < 300; t += 10) {
 *       host_tx->advance(10);
 *       cli_tx->advance(10);
 *       host_server.coop_world_tick_with(*host_tx);
 *       cli_client.coop_world_tick_with(*cli_tx);
 *   }
 */

#include "../src/coop_transport.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <limits>
#include <random>
#include <string>

struct coop_sim_transport final : coop_transport {
    // --- Configuration (set before wiring) ---
    int   latency_ms = 0;   ///< one-way message delivery delay in sim time
    float loss_rate  = 0.f; ///< fraction of messages dropped  (0.0–1.0)
    bool  reorder    = false; ///< swap last two enqueued messages on each send

    coop_sim_transport() = default;

    /// Cross-connect two transports: sends on `this` arrive in `peer`'s inbox.
    auto wire_peer( coop_sim_transport* peer ) -> void { peer_ = peer; }

    /// Advance the simulated clock by `ms` milliseconds.
    auto advance( int ms ) -> void { sim_time_ms_ += ms; }

    /// Current simulated time (milliseconds).
    auto sim_time_ms() const -> int64_t { return sim_time_ms_; }

    // --- coop_transport interface ---

    auto send( const std::string& payload ) -> bool override {
        if( !peer_ ) { return false; }
        if( loss_rate > 0.f && rng_01_() < loss_rate ) { return true; } // dropped

        queued_msg msg{ .data = payload, .deliver_at_ms = sim_time_ms_ + latency_ms };
        peer_->inbox_.push_back( std::move( msg ) );

        if( reorder && peer_->inbox_.size() >= 2 ) {
            auto& q = peer_->inbox_;
            std::swap( q[q.size() - 1], q[q.size() - 2] );
        }
        return true;
    }

    /// Non-blocking: returns the next deliverable message or false if none ready.
    auto recv( std::string& buf, int /*timeout_ms*/ = 0 ) -> bool override {
        if( inbox_.empty() ) { return false; }
        if( inbox_.front().deliver_at_ms > sim_time_ms_ ) { return false; }
        buf = std::move( inbox_.front().data );
        inbox_.pop_front();
        return true;
    }

    /// True if the next message in the inbox is ready for delivery.
    auto poll() -> bool override {
        return !inbox_.empty() && inbox_.front().deliver_at_ms <= sim_time_ms_;
    }

    /// Abrupt close: drain inbox, disconnect from peer, and clear the reverse
    /// reference so the peer can no longer deliver to this transport's inbox.
    auto close_abruptly() -> void override {
        inbox_.clear();
        if( peer_ && peer_->peer_ == this ) {
            peer_->peer_ = nullptr; // sever reverse reference
        }
        peer_ = nullptr;
    }

    /// Pending message count (for assertions in tests).
    auto inbox_size() const -> std::size_t { return inbox_.size(); }

    /// True if no messages are pending delivery.
    auto inbox_empty() const -> bool { return inbox_.empty(); }

private:
    struct queued_msg {
        std::string data;
        int64_t     deliver_at_ms = 0; ///< sim time when deliverable
    };

    coop_sim_transport*     peer_         = nullptr;
    std::deque<queued_msg>  inbox_;
    int64_t                 sim_time_ms_  = 0;

    // Lightweight LCG RNG for loss simulation — no <random> overhead.
    float rng_01_() {
        rng_state_ = rng_state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<float>( rng_state_ >> 33 ) / static_cast<float>( 1LL << 31 );
    }
    uint64_t rng_state_ = 0xdeadbeefcafe1234ULL;
};

#endif // COOP_ENABLED
