#include "fork_runner.h"

#if defined(__linux__)

#    include <algorithm>
#    include <cerrno>
#    include <csignal>
#    include <cstdio>
#    include <cstdlib>
#    include <cstring>
#    include <filesystem>
#    include <fstream>
#    include <map>
#    include <sys/prctl.h>
#    include <sys/wait.h>
#    include <system_error>
#    include <thread>
#    include <unistd.h>
#    include <utility>

namespace fork_runner {

namespace {

using clock_type = std::chrono::steady_clock;

constexpr auto poll_interval = std::chrono::milliseconds(5);
constexpr auto log_tail_lines = size_t{30};

struct running_case {
    size_t index = 0;
    clock_type::time_point deadline;
    bool killed_for_timeout = false;
};

auto child_path(const options& opts, size_t index, const char* suffix) -> std::filesystem::path {
    return std::filesystem::path(opts.scratch_dir) / (std::to_string(index) + suffix);
}

/// What a failure needs to explain itself: up to `max_lines` starting at the first line that marks
/// a failed assertion or a logged error, so the reason is not buried under later log noise. A log
/// with no such marker (a crash, a hang) falls back to its last `max_lines`.
auto read_excerpt(const std::filesystem::path& path, size_t max_lines) -> std::string {
    auto in = std::ifstream(path);
    auto lines = std::vector<std::string>();
    for (auto line = std::string(); std::getline(in, line);) { lines.push_back(std::move(line)); }
    const auto marks_failure = [](const std::string& line) {
        return line.find("FAILED:") != std::string::npos
            || line.find(" ERROR ") != std::string::npos;
    };
    const auto first_mark = std::ranges::find_if(lines, marks_failure);
    const auto from =
        first_mark != lines.end()
            ? static_cast<size_t>(first_mark - lines.begin())
            : lines.size() - std::min(lines.size(), max_lines);
    auto excerpt = std::string();
    for (auto i = from; i < lines.size() && i < from + max_lines; i++) {
        excerpt += lines[i];
        excerpt += '\n';
    }
    return excerpt;
}

/// Never returns: the child's only job is to run one case and leave without tearing down the
/// state it shares copy-on-write with the parent.
[[noreturn]] auto run_child(
    const options& opts, const case_fn& fn, const std::string& name, size_t index) -> void {
    const auto log = child_path(opts, index, ".log").string();
    if (!std::freopen(log.c_str(), "w", stdout) || !std::freopen(log.c_str(), "a", stderr)) {
        _exit(99);
    }
    // A crashed child must not spend minutes handing a core of this whole process to a handler
    // (WSL pipes cores through /wsl-capture-crash, which made a crash look like a hang, and the
    // kernel ignores RLIMIT_CORE=0 for a piped core_pattern). A non-dumpable child leaves none;
    // the crash handler's stack trace is kept, a debugger can still be started on the case alone.
    prctl(PR_SET_DUMPABLE, 0);
    // Tests that build scratch paths from temp_directory_path() get a directory of their own.
    const auto tmp = child_path(opts, index, ".tmp");
    auto ec = std::error_code();
    std::filesystem::create_directories(tmp, ec);
    setenv("TMPDIR", tmp.c_str(), 1);
    const auto code = fn(name);
    std::fflush(nullptr);
    _exit(code);
}

auto elapsed_seconds(clock_type::duration d) -> double {
    return std::chrono::duration<double>(d).count();
}

} // namespace

auto run(const std::vector<std::string>& names, const options& opts, const case_fn& fn)
    -> std::expected<summary, std::string> {
    if (opts.jobs < 1) { return std::unexpected(std::string("jobs must be at least 1")); }
    auto ec = std::error_code();
    std::filesystem::create_directories(opts.scratch_dir, ec);
    if (ec) { return std::unexpected("cannot create " + opts.scratch_dir + ": " + ec.message()); }

    auto result = summary{.total = static_cast<int>(names.size())};
    auto running = std::map<pid_t, running_case>();
    auto fork_time = clock_type::duration::zero();
    const auto start = clock_type::now();

    // Collects one finished child without blocking; false when none has finished.
    const auto reap_one = [&]() -> bool {
        auto status = 0;
        const auto pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) { return false; }
        const auto found = running.find(pid);
        if (found == running.end()) {
            return true; // not one of ours
        }
        const auto entry = found->second;
        running.erase(found);
        const auto exited = WIFEXITED(status);
        if (exited && WEXITSTATUS(status) == 0 && !entry.killed_for_timeout) { return true; }
        result.failed.push_back(failed_case{
            .name = names[entry.index],
            .exit_code = exited ? WEXITSTATUS(status) : -1,
            .signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0,
            .timed_out = entry.killed_for_timeout,
            .log_tail = read_excerpt(child_path(opts, entry.index, ".log"), log_tail_lines),
        });
        return true;
    };

    const auto enforce_timeouts = [&]() -> void {
        const auto now = clock_type::now();
        for (auto& [pid, entry] : running) {
            if (!entry.killed_for_timeout && now >= entry.deadline) {
                entry.killed_for_timeout = true;
                kill(pid, SIGKILL);
            }
        }
    };

    auto next = size_t{0};
    while (next < names.size() || !running.empty()) {
        if (next < names.size() && static_cast<int>(running.size()) < opts.jobs) {
            std::fflush(nullptr);
            const auto fork_started = clock_type::now();
            const auto pid = fork();
            if (pid < 0) {
                const auto reason = std::string(std::strerror(errno));
                for (const auto& [child, entry] : running) { kill(child, SIGKILL); }
                while (!running.empty()) {
                    if (!reap_one()) { std::this_thread::sleep_for(poll_interval); }
                }
                return std::unexpected("fork() failed: " + reason);
            }
            if (pid == 0) { run_child(opts, fn, names[next], next); }
            fork_time += clock_type::now() - fork_started;
            running[pid] = running_case{
                .index = next,
                .deadline = clock_type::now() + opts.case_timeout,
            };
            next++;
            continue;
        }
        if (!reap_one()) {
            enforce_timeouts();
            std::this_thread::sleep_for(poll_interval);
        }
    }

    std::ranges::sort(result.failed, {}, &failed_case::name);
    result.wall_seconds = elapsed_seconds(clock_type::now() - start);
    result.fork_seconds = elapsed_seconds(fork_time);
    return result;
}

auto describe(const summary& result) -> std::string {
    auto text =
        std::to_string(result.total) + " cases in forked children, "
        + std::to_string(result.failed.size()) + " failed, wall "
        + std::to_string(static_cast<int>(result.wall_seconds)) + " s (fork "
        + std::to_string(static_cast<int>(result.fork_seconds)) + " s)\n";
    for (const auto& failure : result.failed) {
        text += "\nFAILED: " + failure.name + "\n";
        if (failure.timed_out) {
            text += "  killed: exceeded the case timeout\n";
        } else if (failure.signal != 0) {
            text += "  died on signal " + std::to_string(failure.signal) + " ("
                  + std::string(strsignal(failure.signal)) + ")\n";
        } else {
            text += "  exit code " + std::to_string(failure.exit_code) + "\n";
        }
        if (!failure.log_tail.empty()) {
            text += "  --- the child's output from its first failure line ---\n" + failure.log_tail;
        }
    }
    return text;
}

} // namespace fork_runner

#endif // __linux__
