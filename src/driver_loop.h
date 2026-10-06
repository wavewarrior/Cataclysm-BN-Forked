#pragma once

/// Line-JSON agent driver: one request line in, one response line out, strictly in order.

/// Serves the driver protocol on an inherited, bidirectional file descriptor.
/// Call after the world is loaded. Returns on `quit` or when the peer closes the descriptor.
void run_driver_loop( int fd );
