#pragma once

#include <cstddef>
#include <string>
#include <vector>

/// One entry of the message log as the driver sees it.
struct log_entry {
    /// Identity of the entry within the session: assigned once, in increasing steps of one, and
    /// kept when a repeat merges into the entry.
    unsigned seq = 0;
    /// The text including its repeat count ("... x 4").
    std::string text;
};

/// What a driver request added to the message log, as a content-based delta.
struct message_delta {
    /// New messages, oldest first. A message that merged into an existing entry ("... x 4")
    /// is reported again in its merged form.
    std::vector<std::string> fresh;
    /// More was added than the compared window could hold, so older additions are missing.
    bool lost = false;
};

/// Compares the log before and after one request. Both windows hold the newest entries of the
/// same log, oldest first. Anchors on the newest entry before the request: entries with a later
/// `seq` are new, and the anchor itself is reported again when its text changed (a repeat merged
/// into it). Compares content, not counts: a repeat leaves the log length unchanged, a capped
/// log drops entries from the front while it grows at the back, and a log with a repeating
/// pattern ("A, B, A, B") cannot be mistaken for an unchanged one.
auto compute_message_delta( const std::vector<log_entry> &before,
                            const std::vector<log_entry> &after ) -> message_delta;

/// Keeps the newest `max_count` messages, each cut to `max_bytes` on a UTF-8 boundary.
/// Returns true when anything was cut.
auto cap_messages( std::vector<std::string> &messages, size_t max_count,
                   size_t max_bytes ) -> bool;
