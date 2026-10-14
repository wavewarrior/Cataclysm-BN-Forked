#if defined(__linux__) && defined(BACKTRACE)

#include "catch/catch_amalgamated.hpp"

#include <csignal>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cached_options.h"
#include "crash.h"

namespace {

struct crash_outcome {
    bool timed_out = false;
    bool signalled = false;
    int signal = 0;
    std::string stderr_text;
};

/// Runs `crash` in a forked child with the production crash handlers installed and stderr sent to
/// a file, and waits up to `limit` for it to die. A child that hangs (the old handler did, when
/// the abort came from the allocator) is killed and reported as timed out.
auto crash_in_child(void (*crash)(), std::chrono::seconds limit) -> crash_outcome {
    const auto log = std::filesystem::temp_directory_path()
                     / ("crash_handler_test_" + std::to_string(getpid()) + ".log");
    const auto child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        const auto fd = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0 || dup2(fd, STDERR_FILENO) < 0) { _exit(97); }
        test_mode = true;
        init_crash_handlers();
        crash();
        _exit(0);
    }

    auto outcome = crash_outcome();
    const auto deadline = std::chrono::steady_clock::now() + limit;
    int status = 0;
    while (true) {
        const auto done = waitpid(child, &status, WNOHANG);
        if (done == child) { break; }
        if (std::chrono::steady_clock::now() >= deadline) {
            outcome.timed_out = true;
            kill(child, SIGKILL);
            waitpid(child, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (WIFSIGNALED(status)) {
        outcome.signalled = true;
        outcome.signal = WTERMSIG(status);
    }
    auto in = std::ifstream(log);
    auto text = std::ostringstream();
    text << in.rdbuf();
    outcome.stderr_text = text.str();
    std::filesystem::remove(log);
    return outcome;
}

} // namespace

TEST_CASE("crash_handler_reports_the_signal_and_the_child_dies_by_it", "[crash]") {
    const auto outcome = crash_in_child([] { raise(SIGSEGV); }, std::chrono::seconds(10));
    CHECK_FALSE(outcome.timed_out);
    CHECK(outcome.signalled);
    CHECK(outcome.signal == SIGSEGV);
    CHECK(outcome.stderr_text.find("SIGSEGV: Segmentation fault") != std::string::npos);
    CHECK(outcome.stderr_text.find("STACK TRACE") != std::string::npos);
}

TEST_CASE("crash_handler_turns_an_abort_into_a_report_and_the_abort_status", "[crash]") {
    const auto outcome = crash_in_child([] { abort(); }, std::chrono::seconds(10));
    CHECK_FALSE(outcome.timed_out);
    CHECK(outcome.signalled);
    CHECK(outcome.signal == SIGABRT);
    CHECK(outcome.stderr_text.find("SIGABRT: Abnormal termination") != std::string::npos);
}

#endif
