#pragma once

#include "coop_net.h"
#include "coop_transport.h"

#include <SDL3_net/SDL_net.h>

/// Real SDL_net transport: wraps a NET_StreamSocket* with coop_net framing.
/// Takes ownership of the socket and destroys it on destruction.
struct coop_net_transport final : coop_transport {
        explicit coop_net_transport( NET_StreamSocket* sock ) : sock_( sock ) {}

        ~coop_net_transport() override {
            if( sock_ ) {
            NET_DestroyStreamSocket( sock_ );
                sock_ = nullptr;
            }
        }

        auto send( const std::string& payload ) -> bool override {
            return coop_net::send( sock_, payload );
        }

        auto recv( std::string& buf, int timeout_ms = 5000 ) -> bool override {
            return coop_net::recv( sock_, buf, timeout_ms );
        }

        auto poll() -> bool override { return coop_net::poll( sock_ ); }

        /// Abruptly destroy the socket without sending a FIN/disconnect packet.
        /// Simulates a client crash; the remote side detects EOF on its next read.
        auto close_abruptly() -> void override {
            if( sock_ ) {
            NET_DestroyStreamSocket( sock_ );
                sock_ = nullptr;
            }
        }

    private:
        NET_StreamSocket *sock_ = nullptr;
};

