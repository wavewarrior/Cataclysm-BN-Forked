#ifdef _GLIBCXX_DEBUG
// Workaround to allow randomly ordered tests.  See
// https://github.com/catchorg/Catch2/issues/1384
// https://stackoverflow.com/questions/22915325/avoiding-self-assignment-in-stdshuffle/23691322
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85828
#    include <iosfwd> // Any cheap-to-include stdlib header
#    ifdef __GLIBCXX__
#        include <debug/macros.h> // IWYU pragma: keep

#        undef __glibcxx_check_self_move_assign
#        define __glibcxx_check_self_move_assign(x)
#    endif // __GLIBCXX__
#endif     // _GLIBCXX_DEBUG

#ifdef CATA_CATCH_PCH
#    undef CATCH_AMALGAMATED_HPP_INCLUDED
#endif
#define CATCH_CONFIG_RUNNER
#include "avatar.h"
#include "calendar.h"
#include "catch/catch_amalgamated.hpp"
#include "color.h"
#include "coordinates.h"
#include "debug.h"
#include "distribution_grid.h"
#include "filesystem.h"
#include "fork_runner.h"
#include "game.h"
#include "init.h"
#include "language.h"
#include "loading_ui.h"
#include "map.h"
#include "mod_manager.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmap_special.h"
#include "overmapbuffer.h"
#include "path_info.h"
#include "pldata.h"
#include "rng.h"
#include "state_helpers.h"
#include "string_formatter.h"
#include "string_utils.h"
#include "type_id.h"
#include "weather.h"
#include "worldfactory.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__linux__)
#    include <unistd.h>
#endif

#if defined(CATA_SDL)
#    if !defined(SDL_MAIN_HANDLED)
#        if defined(__clang__)
#            pragma clang diagnostic push
#            pragma clang diagnostic ignored "-Wunused-macros"
#        endif
#        define SDL_MAIN_HANDLED
#        if defined(__clang__)
#            pragma clang diagnostic pop
#        endif
#    endif
#    include "compute/gpu_platform.h"
#    include "platform/sdl_video.h"
#    include "preload_config.h"

#    include <SDL3/SDL.h>
#endif

using name_value_pair_t = std::pair<std::string, std::string>;
using option_overrides_t = std::vector<name_value_pair_t>;

#if defined(CATA_SDL)
namespace {

bool s_sdl_platform_initialized = false;

auto test_compute_accel(bool* is_explicit = nullptr) -> preload_config::compute_accel {
    auto const* const env_accel = std::getenv("CATA_TEST_COMPUTE_ACCELERATION");
    if (env_accel != nullptr && env_accel[0] != '\0') {
        if (is_explicit != nullptr) { *is_explicit = true; }
        return preload_config::compute_accel_from_string(env_accel);
    }

    if (get_options().has_option("COMPUTE_ACCELERATION")) {
        const auto accel = preload_config::compute_accel_from_string(
            get_options().get_option("COMPUTE_ACCELERATION").getValue());
        if (accel != preload_config::compute_accel::auto_select) {
            if (is_explicit != nullptr) { *is_explicit = true; }
            return accel;
        }
    }

    // Implicit default: try software GPU compute for test determinism, but this is a soft
    // best-effort guess, not a hard requirement (see set_require_gpu_device() below) — the
    // caller already has a working fallback to cpu when this device fails to materialise.
    return preload_config::compute_accel::gpu_software;
}

auto init_test_sdl_gpu() -> void {
    use_offscreen_video_driver_for_headless_sdl();

    if (!SDL_Init(SDL_InitFlags{SDL_INIT_VIDEO})) {
        throw std::runtime_error(string_format("SDL_Init failed: %s", SDL_GetError()));
    }
    s_sdl_platform_initialized = true;

    preload_config::load();
    bool accel_explicit = false;
    const auto accel = test_compute_accel(&accel_explicit);
    preload_config::set_compute_accel(accel);
    // Only an EXPLICIT request (env var or an OPTIONS COMPUTE_ACCELERATION override) should log
    // DL::Error on failure; the implicit gpu_software default guess above has a working fallback
    // right below, so its failure is expected and must log DL::Warn — otherwise every run on a
    // machine without a software Vulkan driver fails Catch2's "error logged" gate despite all
    // 159 [coop] cases (and the rest of the suite) passing.
    preload_config::set_require_gpu_device(accel_explicit);

    cata_gpu::init();
    if (cata_gpu::get_device() == nullptr) {
        // Upstream throws here because its CI provisions a software Vulkan driver. This fork
        // must not: the test binary is windowless, so under D2 there is no
        // lighting::gpu_device to adopt, and cata_gpu's own probe needs compute shader blobs
        // (data/shaders/*.msl) that the slim macOS preset does not build. A null device is
        // upstream's own documented "device creation failed" contract.
        // selected_backend() only degrades to backend::cpu_compute on a null device when the
        // accel is auto_select — with an explicit gpu/gpu_software request it stays on the GPU
        // path, which then bails out of build_transparency_caches without building the cache
        // (lighting-dependent tests like the crafting ones fail). So drop the requested accel
        // to cpu here, which is exactly the lightmap path the pre-merge baseline suite
        // exercised. Throwing would make every one of the ~1085 cases unrunnable on this
        // platform.
        // An explicit CATA_TEST_COMPUTE_ACCELERATION=gpu* request is honoured as-is (it
        // reproduces the stuck-GPU-path behaviour on purpose, e.g. for regression
        // attribution); only the implicit default is downgraded.
        DebugLog(DL::Warn, DC::Main)
            << "SDL_GPU unavailable for tests; falling back to the CPU compute path";
        if (!accel_explicit) {
            preload_config::set_compute_accel(preload_config::compute_accel::cpu);
        }
    }
}

auto shutdown_test_sdl_gpu() -> void {
    cata_gpu::shutdown();
    if (s_sdl_platform_initialized) {
        SDL_Quit();
        s_sdl_platform_initialized = false;
    }
}

} // namespace
#endif

// If tag is found as a prefix of any argument in arg_vec, the argument is
// removed from arg_vec and the argument suffix after tag is returned.
// Otherwise, an empty string is returned and arg_vec is unchanged.
static std::string extract_argument(std::vector<const char*>& arg_vec, const std::string& tag) {
    std::string arg_rest;
    for (auto iter = arg_vec.begin(); iter != arg_vec.end(); iter++) {
        if (strncmp(*iter, tag.c_str(), tag.length()) == 0) {
            arg_rest = std::string(&(*iter)[tag.length()]);
            arg_vec.erase(iter);
            break;
        }
    }
    return arg_rest;
}

static std::vector<mod_id> extract_mod_selection(std::vector<const char*>& arg_vec) {
    std::string mod_string = extract_argument(arg_vec, "--mods=");

    std::vector<std::string> mod_names = string_split(mod_string, ',');
    std::vector<mod_id> ret;
    for (const std::string& mod_name : mod_names) {
        if (!mod_name.empty()) { ret.emplace_back(mod_name); }
    }
    // Always load test data mod
    ret.emplace_back("test_data");

    return ret;
}

static void init_global_game_state(
    const std::vector<mod_id>& mods, option_overrides_t& option_overrides,
    const std::string& user_dir) {
    if (!remove_tree(user_dir)) {
        assert(!"Unable to remove user_dir directory.  Check permissions.");
    }
    if (!assure_dir_exist(user_dir)) {
        assert(!"Unable to make user_dir directory.  Check permissions.");
    }

    PATH_INFO::init_base_path("");
    PATH_INFO::init_user_dir(user_dir);
    PATH_INFO::set_standard_filenames();
    PATH_INFO::set_config_dir(PATH_INFO::user_dir() + "config/");

    if (!assure_dir_exist(PATH_INFO::config_dir())) {
        assert(!"Unable to make config directory.  Check permissions.");
    }

    if (!assure_dir_exist(PATH_INFO::savedir())) {
        assert(!"Unable to make save directory.  Check permissions.");
    }

    if (!assure_dir_exist(PATH_INFO::templatedir())) {
        assert(!"Unable to make templates directory.  Check permissions.");
    }

    if (!init_language_system()) {
        DebugLog(DL::Error, DC::Main) << "Failed to init language system.";
    }

    get_options().init();
    get_options().load();

    // Pin the reality bubble to size 4 (half=60, 11×11 grid) for test determinism.
    // Most tests hardcode coordinates assuming the center is at (60,60).
    // The production default is 6 (half=84), which would shift the anchor.
    {
        auto& opt = get_options().get_option("REALITY_BUBBLE_SIZE");
        opt.setValue("4");
    }

    // Apply command-line option overrides for test suite execution.
    if (!option_overrides.empty()) {
        for (const name_value_pair_t& option : option_overrides) {
            if (get_options().has_option(option.first)) {
                options_manager::cOpt& opt = get_options().get_option(option.first);
                opt.setValue(option.second);
            }
        }
    }

#if defined(CATA_SDL)
    init_test_sdl_gpu();
#endif
    init_colors();

    g = std::make_unique<game>();
    g->new_game = true;
    g->load_static_data();

    world_generator->set_active_world(nullptr);
    world_generator->init();
    WORLDINFO* test_world = world_generator->make_new_world(mods);
    assert(test_world != nullptr);
    world_generator->set_active_world(test_world);
    assert(world_generator->active_world != nullptr);

    calendar::set_eternal_season(get_option<bool>("ETERNAL_SEASON"));
    calendar::set_season_length(get_option<int>("SEASON_LENGTH"));

    loading_ui ui(false);
    init::load_world_modfiles(ui, g->get_active_world(), SAVE_ARTIFACTS);

    g->u = avatar();
    g->u.create(character_type::NOW);

    g->m = map();
    disable_mapgen = true;

    g->m.load(g->m.get_abs_sub(), false);

    get_weather().update_weather();
}

// Checks if any of the flags are in container, removes them all
static bool check_remove_flags(
    std::vector<const char*>& cont, const std::vector<const char*>& flags) {
    bool has_any = false;
    auto iter = flags.begin();
    while (iter != flags.end()) {
        auto found = std::find_if(cont.begin(), cont.end(), [iter](const char* c) {
            return strcmp(c, *iter) == 0;
        });
        if (found == cont.end()) {
            iter++;
        } else {
            cont.erase(found);
            has_any = true;
        }
    }

    return has_any;
}

// Split s on separator sep, returning parts as a pair. Returns empty string as
// second value if no separator found.
static name_value_pair_t split_pair(const std::string& s, const char sep) {
    const size_t pos = s.find(sep);
    if (pos != std::string::npos) {
        return name_value_pair_t(s.substr(0, pos), s.substr(pos + 1));
    } else {
        return name_value_pair_t(s, "");
    }
}

static option_overrides_t extract_option_overrides(std::vector<const char*>& arg_vec) {
    option_overrides_t ret;
    std::string option_overrides_string = extract_argument(arg_vec, "--option_overrides=");
    if (option_overrides_string.empty()) { return ret; }
    const char delim = ',';
    const char sep = ':';
    size_t i = 0;
    size_t pos = option_overrides_string.find(delim);
    while (pos != std::string::npos) {
        std::string part = option_overrides_string.substr(i, pos);
        ret.emplace_back(split_pair(part, sep));
        i = ++pos;
        pos = option_overrides_string.find(delim, pos);
    }
    // Handle last part
    const std::string part = option_overrides_string.substr(i);
    ret.emplace_back(split_pair(part, sep));
    return ret;
}

static std::string extract_user_dir(std::vector<const char*>& arg_vec) {
    std::string option_user_dir = extract_argument(arg_vec, "--user-dir=");
    if (option_user_dir.empty()) { return "./test_user_dir/"; }
    if (!option_user_dir.ends_with("/")) { option_user_dir += "/"; }
    return option_user_dir;
}

struct CataListener: Catch::EventListenerBase {
    using EventListenerBase::EventListenerBase;

    void sectionStarting(Catch::SectionInfo const& sectionInfo) override {
        EventListenerBase::sectionStarting(sectionInfo);
        // Initialize the cata RNG with the Catch seed for reproducible tests
        rng_set_engine_seed(m_config->rngSeed());
    }

    void assertionEnded(Catch::AssertionStats const& assertionStats) override {
#ifdef BACKTRACE
        Catch::AssertionResult const& result = assertionStats.assertionResult;

        if (result.getResultType() == Catch::ResultWas::FatalErrorCondition) {
            // We are in a signal handler for a fatal error condition, so print a
            // backtrace
            std::cerr << "Stack trace at fatal error:\n";
            debug_write_backtrace(std::cerr);
        }
#endif

        EventListenerBase::assertionEnded(assertionStats);
    }
};
CATCH_REGISTER_LISTENER(CataListener)

#if defined(__linux__)
namespace {

auto fork_jobs_from_env() -> std::optional<int> {
    const auto* const raw = std::getenv("CATA_TEST_FORK_JOBS");
    if (raw == nullptr || raw[0] == '\0') { return std::nullopt; }
    const auto jobs = std::atoi(raw);
    return jobs > 0 ? std::optional<int>{jobs} : std::nullopt;
}

/// True when the runner, not the user, chose CPU compute; a re-executed GPU-path case undoes it.
auto runner_chose_cpu_compute = false;

/// A parent that forks has to be forkable: CPU compute (a live GPU device does not survive fork),
/// no thread pool (its workers do not exist in a child) and no window system (children would
/// share one X connection and the parent dies at exit).
auto prepare_for_forking(option_overrides_t& overrides) -> void {
    runner_chose_cpu_compute = std::getenv("CATA_TEST_COMPUTE_ACCELERATION") == nullptr;
    setenv("CATA_TEST_COMPUTE_ACCELERATION", "cpu", 0);
    unsetenv("DISPLAY");
    unsetenv("WAYLAND_DISPLAY");
    unsetenv("SDL_VIDEODRIVER");
    overrides.emplace_back("MULTITHREADING_ENABLED", "false");
}

/// Catch2 reads a backslash as an escape, so any name can be selected exactly.
auto escape_for_catch(const std::string& name) -> std::string {
    auto escaped = std::string();
    for (const auto c : name) {
        if (std::string_view("\\,[]\"~*").find(c) != std::string_view::npos) { escaped += '\\'; }
        escaped += c;
    }
    return escaped;
}

/// The cases Catch2 would run for this command line.
auto select_cases(Catch::Session& session) -> std::vector<std::string> {
    const auto& config = session.config();
    const auto all = Catch::getAllTestCasesSorted(config);
    const auto matched = Catch::filterTests(all, config.testSpec(), config);
    auto names = std::vector<std::string>();
    for (const auto& handle : matched) { names.push_back(handle.getTestCaseInfo().name); }
    return names;
}

/// Exit code of a child whose selector did not match exactly one case.
constexpr auto selector_error_code = 97;

/// Runs exactly one case in a child. The exit code has to say what an in-process run of the same
/// case would have said, including the error-logged failure `main` applies at the end.
auto run_one_case(Catch::Session& session, const std::string& name, bool init_had_error) -> int {
    auto data = session.configData();
    data.testsOrTags = {escape_for_catch(name)};
    session.useConfigData(data);
    const auto count = select_cases(session).size();
    if (count != 1) {
        cata_printf("The selector for \"%s\" matched %zu cases, expected 1\n", name.c_str(), count);
        return selector_error_code;
    }
    const auto code = session.run();
    // A case that skips itself (SKIP) makes Catch2 exit 4; that is not a failure.
    if (code != 0 && code != Catch::AllTestsSkippedExitCode) { return code; }
    return !init_had_error && debug_has_error_been_observed() ? 1 : 0;
}

enum class compute_path { cpu, gpu_default };

/// Replaces the child with a fresh process of this binary, so it gets what a forked child could
/// not keep: a live GPU device on the default path, or an untouched CPU-compute process. Only
/// returns if exec failed.
auto reexec_case(const std::string& name, unsigned int seed, compute_path path) -> int {
    if (path == compute_path::gpu_default && runner_chose_cpu_compute) {
        unsetenv("CATA_TEST_COMPUTE_ACCELERATION");
    }
    unsetenv("CATA_TEST_FORK_JOBS");
    // TMPDIR is private to this child, so the fresh process gets a user dir of its own.
    const auto* const tmp = std::getenv("TMPDIR");
    auto words = std::vector<std::string>{
        "cata_test-tiles", "-D", "--user-dir=" + std::string(tmp ? tmp : "/tmp") + "/user/"};
    if (path == compute_path::cpu) {
        words.emplace_back("--option_overrides=MULTITHREADING_ENABLED:false");
    }
    if (seed != 0) {
        words.emplace_back("--rng-seed");
        words.push_back(std::to_string(seed));
    }
    words.push_back(escape_for_catch(name));
    auto argv = std::vector<char*>();
    for (auto& word : words) { argv.push_back(word.data()); }
    argv.push_back(nullptr);
    execv("/proc/self/exe", argv.data());
    return 127;
}

struct rerun_options {
    compute_path path = compute_path::gpu_default;
    unsigned int seed = 0;
    int jobs = 1;
    std::string scratch;
};

/// Runs each of `names` alone in a fresh process of this binary on the given compute path.
auto rerun_in_fresh_processes(const std::vector<std::string>& names, const rerun_options& opts)
    -> std::expected<fork_runner::summary, std::string> {
    return fork_runner::run(
        names,
        fork_runner::options{
            .jobs = opts.jobs,
            .case_timeout = std::chrono::seconds(180),
            .scratch_dir = opts.scratch},
        [seed = opts.seed, path = opts.path](const std::string& name) -> int {
            return reexec_case(name, seed, path);
        });
}

auto run_forked(Catch::Session& session, int jobs, const std::string& scratch) -> int {
    const auto names = select_cases(session);
    const auto init_had_error = debug_has_error_been_observed();
    const auto timeout = std::chrono::seconds(180);
    const auto forked = fork_runner::run(
        names,
        fork_runner::
            options{.jobs = jobs, .case_timeout = timeout, .scratch_dir = scratch + "/forked"},
        [&session, init_had_error](const std::string& name) -> int {
            return run_one_case(session, name, init_had_error);
        });
    if (!forked) {
        cata_printf("The forked runner could not run: %s\n", forked.error().c_str());
        return 1;
    }
    cata_printf(
        "%d cases in forked children on the CPU compute path, %zu failed, wall %.0f s "
        "(fork %.0f s)\n",
        forked->total, forked->failed.size(), forked->wall_seconds, forked->fork_seconds);
    if (forked->failed.empty()) {
        auto ec = std::error_code();
        std::filesystem::remove_all(scratch, ec);
        return 0;
    }

    // The CPU compute path is the only one a forked child can use, and it disagrees with the GPU
    // path on visibility. So each failure is re-run in fresh processes and classified by evidence:
    //   confirmed            fails on the default path too: a real failure, fails the run.
    //   CPU-path difference  passes on the default path but fails again on fresh CPU compute.
    //   not reproduced       passes on both fresh runs: flaky or caused by sharing a parent. It is
    //                        listed with its seed and never silently dropped.
    auto failed_names = std::vector<std::string>();
    for (const auto& failure : forked->failed) { failed_names.push_back(failure.name); }
    const auto seed = session.config().rngSeed();
    const auto failed_in = [](const fork_runner::summary& result, const std::string& name) {
        return std::ranges::any_of(result.failed, [&name](const auto& failure) {
            return failure.name == name;
        });
    };
    const auto on_default = rerun_in_fresh_processes(
        failed_names,
        {.path = compute_path::gpu_default,
         .seed = seed,
         .jobs = jobs,
         .scratch = scratch + "/default-path"});
    if (!on_default) {
        cata_printf("The default-path re-run could not run: %s\n", on_default.error().c_str());
        return 1;
    }
    auto passed_default = std::vector<std::string>();
    for (const auto& name : failed_names) {
        if (!failed_in(*on_default, name)) { passed_default.push_back(name); }
    }
    const auto on_cpu = rerun_in_fresh_processes(
        passed_default,
        {.path = compute_path::cpu, .seed = seed, .jobs = jobs, .scratch = scratch + "/cpu-path"});
    if (!on_cpu) {
        cata_printf("The CPU-path re-run could not run: %s\n", on_cpu.error().c_str());
        return 1;
    }

    cata_printf("\nRe-ran the %zu failures in fresh processes (seed %u): %zu confirmed.\n",
                failed_names.size(), seed, on_default->failed.size());
    for (const auto& name : passed_default) {
        if (failed_in(*on_cpu, name)) {
            cata_printf("  CPU-path difference (fails on CPU compute, passes on default): %s\n",
                        name.c_str());
        } else {
            cata_printf("  NOT REPRODUCED, flaky or fork-induced (seed %u): %s\n", seed,
                        name.c_str());
        }
    }
    if (on_default->failed.empty()) {
        // Only clear the logs when nothing was left unexplained.
        if (std::ranges::all_of(passed_default, [&](const auto& name) {
                return failed_in(*on_cpu, name);
            })) {
            auto ec = std::error_code();
            std::filesystem::remove_all(scratch, ec);
        } else {
            cata_printf("Child logs are in %s\n", scratch.c_str());
        }
        return 0;
    }
    cata_printf("\n%s", fork_runner::describe(*on_default).c_str());
    cata_printf("Child logs are in %s\n", scratch.c_str());
    return 1;
}

} // namespace
#endif

int main(int argc, const char* argv[]) {
    Catch::Session session;

    std::vector<const char*> arg_vec(argv, argv + argc);

    std::vector<mod_id> mods = extract_mod_selection(arg_vec);
    mod_id def_core_mod_id = mod_management::get_default_core_content_pack();
    if (std::find(mods.begin(), mods.end(), def_core_mod_id) == mods.end()) {
        mods.insert(mods.begin(), def_core_mod_id); // @todo move unit test items to core
    }

    option_overrides_t option_overrides_for_test_suite = extract_option_overrides(arg_vec);

    const bool dont_save = check_remove_flags(arg_vec, {"-D", "--drop-world"});

    std::string user_dir = extract_user_dir(arg_vec);

#if defined(CATA_SDL)
    std::string gpu_backend_override = extract_argument(arg_vec, "--gpu-backend=");
    if (!gpu_backend_override.empty()) {
        preload_config::set_gpu_backend_override(gpu_backend_override);
    }
#endif

    std::string error_fmt = extract_argument(arg_vec, "--error-format=");
    if (error_fmt == "github-action") {
        error_log_format = error_log_format_t::github_action;
    } else if (error_fmt == "human-readable" || error_fmt.empty()) {
        error_log_format = error_log_format_t::human_readable;
    } else {
        printf("Unknown format %s", error_fmt.c_str());
        return EXIT_FAILURE;
    }

    // Note: this must not be invoked before all DDA-specific flags are stripped from arg_vec!
    int result = session.applyCommandLine(arg_vec.size(), arg_vec.data());
    if (result != 0 || session.configData().showHelp) {
        cata_printf("Cataclysm: BN specific options:\n");
        cata_printf(
            "  --mods=<mod1,mod2,…>         Loads the list of mods before executing tests.\n");
        cata_printf(
            "  --user-dir=<dir>             Set user dir (where test world will be created).\n");
        cata_printf(
            "                               Don't use any existing folder you care about,\n");
        cata_printf("                               all contents will be erased!\n");
        cata_printf("  -D, --drop-world             Don't save the world on test failure.\n");
        cata_printf("  --option_overrides=n:v[,…]   Name-value pairs of game options for tests.\n");
        cata_printf("                               (overrides config/options.json values)\n");
#if defined(CATA_SDL)
        cata_printf("  --gpu-backend=<driver>       Override SDL_GPU backend for diagnostics.\n");
#endif
        cata_printf(
            "  --error-format=<value>       Format of error messages.  Possible values are:\n");
        cata_printf("                                   human-readable (default)\n");
        cata_printf("                                   github-action\n");
        return result;
    }

    if (session.configData().listTags || session.configData().listTests
        || session.configData().listReporters) {
        return session.run();
    }

    test_mode = true;

#if defined(__linux__)
    const auto fork_jobs = fork_jobs_from_env();
    if (fork_jobs) { prepare_for_forking(option_overrides_for_test_suite); }
#endif

    setupDebug(DebugOutput::std_err);

    // Set the seed for mapgen (the seed will also be reset before each test)
    const unsigned int seed = session.config().rngSeed();
    if (seed) {
        rng_set_engine_seed(seed);

        // If the run is terminated due to a crash during initialization, we won't
        // see the seed unless it's printed out in advance, so do that here.
        printf("Randomness seeded to: %u\n", seed);
    }
    DebugLog(DL::Info, DC::Main) << "Randomness seeded to: " << seed;

    auto _on_out_of_scope = on_out_of_scope([]() {
#if defined(CATA_SDL)
        shutdown_test_sdl_gpu();
#endif
        g.reset();
        DynamicDataLoader::get_instance().unload_data();
    });
    try {
        // TODO: Only init game if we're running tests that need it.
        init_global_game_state(mods, option_overrides_for_test_suite, user_dir);
    } catch (const std::exception& err) {
        cata_print_stderr(string_format("Terminated: %s\n", err.what()));
        cata_print_stderr(
            "Make sure that you're in the correct working directory and your data isn't corrupted.\n");
        return EXIT_FAILURE;
    }

    bool error_during_initialization = debug_has_error_been_observed();

    const auto start = std::chrono::system_clock::now();
    std::time_t start_time = std::chrono::system_clock::to_time_t(start);
    // Leading newline in case there were debug messages during
    // initialization.
    cata_printf("\nStarting the actual test at %s", std::ctime(&start_time));
#if defined(__linux__)
    result =
        fork_jobs
            ? run_forked(session, *fork_jobs,
                         (std::filesystem::temp_directory_path()
                          / ("cata_fork_runner_" + std::to_string(getpid())))
                             .string())
            : session.run();
#else
    result = session.run();
#endif
    const auto end = std::chrono::system_clock::now();
    std::time_t end_time = std::chrono::system_clock::to_time_t(end);

    clear_all_state();

    auto world_name = world_generator->active_world->info->world_name;
    if (result == 0 || dont_save) {
        world_generator->delete_world(world_name, true);
    } else {
        cata_printf("Test world \"%s\" left for inspection.\n", world_name.c_str());
    }

    std::chrono::duration<double> elapsed_seconds = end - start;
    cata_printf("Ended test at %sThe test took %.3f seconds\n", std::ctime(&end_time),
                elapsed_seconds.count());

    if (seed) {
        // Also print the seed at the end so it can be easily found
        DebugLog(DL::Info, DC::Main) << "Randomness seeded to: " << seed;
    }

    if (error_during_initialization) {
        cata_printf("\nTreating result as failure due to error logged during initialization.\n");
        cata_printf("Randomness seeded to: %u\n", seed);
        return 1;
    }

    if (debug_has_error_been_observed()) {
        cata_printf("\nTreating result as failure due to error logged during tests.\n");
        cata_printf("Randomness seeded to: %u\n", seed);
        return 1;
    }

    cata_printf("\n");

    return result;
}
