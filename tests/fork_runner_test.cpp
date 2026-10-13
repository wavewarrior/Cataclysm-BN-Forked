#if defined(__linux__)

#    include "catch/catch_amalgamated.hpp"
#    include "fork_runner.h"

#    include <chrono>
#    include <csignal>
#    include <cstdio>
#    include <cstdlib>
#    include <filesystem>
#    include <fstream>
#    include <string>
#    include <thread>
#    include <unistd.h>
#    include <vector>

namespace {

auto scratch_for(const std::string& label) -> std::filesystem::path {
    auto dir = std::filesystem::temp_directory_path()
             / ("fork_runner_test_" + label + "_" + std::to_string(getpid()));
    std::filesystem::remove_all(dir);
    return dir;
}

struct case_run {
    std::vector<std::string> names;
    int jobs = 1;
    std::string label;
    std::chrono::seconds timeout{30};
};

/// Every name in `spec.names` is its own case; `fn` is run once per name inside its own child.
auto run_cases(const case_run& spec, const fork_runner::case_fn& fn) -> fork_runner::summary {
    const auto scratch = scratch_for(spec.label);
    auto result = fork_runner::run(
        spec.names,
        fork_runner::options{
            .jobs = spec.jobs,
            .case_timeout = spec.timeout,
            .scratch_dir = scratch.string(),
        },
        fn);
    REQUIRE(result.has_value());
    std::filesystem::remove_all(scratch);
    return *result;
}

auto failure_named(const fork_runner::summary& result, const std::string& name)
    -> const fork_runner::failed_case* {
    for (const auto& failure : result.failed) {
        if (failure.name == name) { return &failure; }
    }
    return nullptr;
}

} // namespace

TEST_CASE("fork_runner_reports_each_failure_with_its_reason", "[fork_runner]") {
    const auto result = run_cases(
        {.names = {"passes", "exits_3", "crashes", "hangs", "writes_then_fails"},
         .jobs = 4,
         .label = "reasons",
         .timeout = std::chrono::seconds(1)},
        [](const std::string& name) -> int {
            if (name == "exits_3") { return 3; }
            if (name == "crashes") {
                std::signal(SIGSEGV, SIG_DFL);
                std::raise(SIGSEGV);
            }
            if (name == "hangs") {
                while (true) { std::this_thread::sleep_for(std::chrono::seconds(1)); }
            }
            if (name == "writes_then_fails") {
                std::fprintf(stderr, "the reason is in this line\n");
                return 1;
            }
            return 0;
        });

    CHECK(result.total == 5);
    CHECK(result.failed.size() == 4);
    CHECK(failure_named(result, "passes") == nullptr);

    const auto* exited = failure_named(result, "exits_3");
    REQUIRE(exited != nullptr);
    CHECK(exited->exit_code == 3);
    CHECK(exited->signal == 0);
    CHECK_FALSE(exited->timed_out);

    const auto* crashed = failure_named(result, "crashes");
    REQUIRE(crashed != nullptr);
    CHECK(crashed->exit_code == -1);
    CHECK(crashed->signal == SIGSEGV);

    const auto* hung = failure_named(result, "hangs");
    REQUIRE(hung != nullptr);
    CHECK(hung->timed_out);

    const auto* noisy = failure_named(result, "writes_then_fails");
    REQUIRE(noisy != nullptr);
    CHECK(noisy->log_tail.find("the reason is in this line") != std::string::npos);
}

TEST_CASE("fork_runner_keeps_a_cases_state_out_of_the_parent", "[fork_runner]") {
    static auto leaked = 0;
    const auto result = run_cases(
        {.names = {"writes_a_global"}, .jobs = 1, .label = "isolation"},
        [](const std::string&) -> int {
            leaked = 42;
            return 0;
        });
    CHECK(result.failed.empty());
    CHECK(leaked == 0);
}

TEST_CASE("fork_runner_gives_each_case_its_own_temp_directory", "[fork_runner]") {
    const auto scratch = scratch_for("tmpdir");
    const auto names = std::vector<std::string>{"a", "b", "c"};
    const auto result = fork_runner::run(
        names,
        fork_runner::options{
            .jobs = 3,
            .case_timeout = std::chrono::seconds(30),
            .scratch_dir = scratch.string(),
        },
        [](const std::string& name) -> int {
            auto marker = std::ofstream(std::filesystem::temp_directory_path() / "marker");
            marker << name;
            return marker ? 0 : 1;
        });
    REQUIRE(result.has_value());
    CHECK(result->failed.empty());

    auto contents = std::vector<std::string>();
    for (size_t i = 0; i < names.size(); i++) {
        auto in = std::ifstream(scratch / (std::to_string(i) + ".tmp") / "marker");
        auto text = std::string();
        std::getline(in, text);
        contents.push_back(text);
    }
    CHECK(contents == names);
    std::filesystem::remove_all(scratch);
}

TEST_CASE("fork_runner_runs_jobs_children_at_the_same_time", "[fork_runner]") {
    // Each child waits until all four have arrived. That only completes if four are running at
    // once, so no timing assumption is involved; the wait is capped so a failure cannot hang.
    const auto barrier = scratch_for("barrier");
    std::filesystem::create_directories(barrier);
    const auto result = run_cases(
        {.names = {"p", "q", "r", "s"}, .jobs = 4, .label = "concurrent"},
        [&barrier](const std::string& name) -> int {
            std::ofstream(barrier / ("arrived_" + name)) << "x";
            for (auto waited = 0; waited < 500; waited++) {
                auto arrived = 0;
                for (const auto& entry : std::filesystem::directory_iterator(barrier)) {
                    arrived += entry.path().filename().string().starts_with("arrived_") ? 1 : 0;
                }
                if (arrived == 4) { return 0; }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return 1;
        });
    CHECK(result.failed.empty());
    std::filesystem::remove_all(barrier);
}

TEST_CASE("fork_runner_refuses_options_it_cannot_run_with", "[fork_runner]") {
    const auto fn = [](const std::string&) -> int { return 0; };
    const auto no_jobs = fork_runner::run(
        {"x"},
        fork_runner::options{
            .jobs = 0,
            .scratch_dir = scratch_for("nojobs").string(),
        },
        fn);
    CHECK_FALSE(no_jobs.has_value());

    // A regular file where the scratch directory should be.
    const auto blocker = scratch_for("blocker");
    std::ofstream(blocker) << "not a directory";
    const auto bad_dir = fork_runner::run(
        {"x"},
        fork_runner::options{
            .jobs = 1,
            .scratch_dir = (blocker / "child").string(),
        },
        fn);
    CHECK_FALSE(bad_dir.has_value());
    std::filesystem::remove_all(blocker);
}

TEST_CASE("fork_runner_shows_the_failing_block_not_the_noise_after_it", "[fork_runner]") {
    const auto result = run_cases(
        {.names = {"noisy_failure"}, .jobs = 1, .label = "excerpt"}, [](const std::string&) -> int {
            std::fprintf(stderr, "early noise\n");
            std::fprintf(stderr, "x_test.cpp:1: FAILED:\n");
            std::fprintf(stderr, "the real reason\n");
            for (auto i = 0; i < 200; i++) { std::fprintf(stderr, "late noise %d\n", i); }
            return 1;
        });
    REQUIRE(result.failed.size() == 1);
    const auto& tail = result.failed.front().log_tail;
    CHECK(tail.find("the real reason") != std::string::npos);
    CHECK(tail.find("early noise") == std::string::npos);
    CHECK(tail.find("late noise 199") == std::string::npos);
}

#endif // __linux__
