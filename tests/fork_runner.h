#pragma once

#if defined(__linux__)

#    include <chrono>
#    include <expected>
#    include <functional>
#    include <string>
#    include <vector>

/// Runs each named test case in its own forked child of an already initialised process, so a
/// crash, a hang or state leaked by one case stays inside that case. Linux only: fork() does not
/// exist on the other shipping platforms, which keep the in-process Catch2 runner (ADR-0004).
namespace fork_runner {

/// Runs one case inside the child and returns its exit code. It must not return into the parent.
using case_fn = std::function<auto(const std::string&)->int>;

struct options {
    int jobs = 1;
    std::chrono::seconds case_timeout{180};
    /// Holds each child's log (`<n>.log`) and its private TMPDIR (`<n>.tmp`). Created if missing.
    std::string scratch_dir;
};

struct failed_case {
    std::string name;
    /// The child's exit code; -1 when it died on a signal or was killed for running too long.
    int exit_code = 0;
    int signal = 0;
    bool timed_out = false;
    /// The part of the child's output that explains the failure: from its first failed assertion
    /// or logged error, or its last lines when it died without either.
    std::string log_tail;
};

struct summary {
    int total = 0;
    std::vector<failed_case> failed;
    double wall_seconds = 0.0;
    /// Time the parent spent inside fork(), which is serial and overlaps with running children.
    double fork_seconds = 0.0;
};

/// Runs `names` with up to `opts.jobs` children at a time. Fails only when the runner itself
/// cannot work (cannot create the scratch directory, fork() refused); a failing case is data in
/// the summary, not an error.
auto run(const std::vector<std::string>& names, const options& opts, const case_fn& fn)
    -> std::expected<summary, std::string>;

/// One line of totals followed by every failure with its reason and log tail.
auto describe(const summary& result) -> std::string;

} // namespace fork_runner

#endif // __linux__
