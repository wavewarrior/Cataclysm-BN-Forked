#pragma once

#include <string>

/// Abstract transport interface for co-op network communication.
///
/// Two implementations exist:
///   coop_net_transport  — wraps a real SDL_net NET_StreamSocket (production)
///   coop_sim_transport  — in-memory simulator with programmable latency/loss
///                         (tests/coop_sim_transport.h, tests only)
///
/// All send/recv/poll semantics match the existing coop_net::send/recv/poll
/// contract so call sites need no logic changes — only the type swaps.
struct coop_transport {
    virtual ~coop_transport() = default;

    coop_transport() = default;
    coop_transport( const coop_transport & ) = delete;
    coop_transport &operator=( const coop_transport & ) = delete;

    /// Send a complete framed payload.  Returns false on socket error.
    virtual auto send( const std::string& payload ) -> bool = 0;

    /// Blocking receive of one framed payload.
    /// timeout_ms < 0 = block indefinitely; 0 = non-blocking peek.
    /// Returns false on disconnect or malformed frame.
    virtual auto recv( std::string& buf, int timeout_ms = 5000 ) -> bool = 0;

    /// Non-blocking check: true if at least one byte is waiting.
    virtual auto poll() -> bool = 0;

    /// Close the underlying connection without sending a protocol-level
    /// disconnect packet — simulates an abrupt client crash for tests.
    /// Default implementation is a no-op; override in coop_net_transport.
    virtual auto close_abruptly() -> void {}
};

