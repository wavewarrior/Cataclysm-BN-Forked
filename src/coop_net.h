#pragma once

#include <SDL3_net/SDL_net.h>
#include <string>

class JsonIn;
class JsonOut;

namespace coop_net
{

/// Send a complete JSON payload with a 4-byte big-endian length prefix.
/// The caller builds the payload into `payload` before calling.
/// Returns false on socket error.
auto send( NET_StreamSocket* sock, const std::string& payload ) -> bool;

/// Blocking receive: read 4-byte header then payload into `buf`.
/// timeout_ms < 0 = block indefinitely; 0 = non-blocking peek.
/// Returns false on disconnect or malformed frame.
auto recv( NET_StreamSocket* sock, std::string& buf, int timeout_ms = 5000 ) -> bool;

/// Non-blocking check: true if at least one byte is waiting on sock.
auto poll( NET_StreamSocket* sock ) -> bool;

} // namespace coop_net

