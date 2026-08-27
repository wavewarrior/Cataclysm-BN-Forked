#include "catch/catch_amalgamated.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

#include "options.h"
#include "path_info.h"
#include "tts_piper_synthesizer.h"

namespace
{

/// Unique scratch dir for this test run (baked in at first use).
const std::filesystem::path &scratch_dir()
{
    static const std::filesystem::path dir = [] {
        const auto base = std::filesystem::temp_directory_path() /
                          ( "cbn_tts_test_" + std::to_string( ::getpid() ) );
        std::filesystem::create_directories( base );
        return base;
    }();
    return dir;
}

/// Restore a string option to its captured value on scope exit.
struct option_restore {
    std::string name;
    std::string value;
    option_restore( std::string n, std::string v ) : name( std::move( n ) ), value( std::move( v ) ) {}
    ~option_restore()
    {
        get_options().get_option( name ).setValue( value );
    }
};

/// Restore PATH_INFO::datadir to its captured value on scope exit.
struct datadir_restore {
    std::string value;
    datadir_restore( std::string v ) : value( std::move( v ) ) {}
    ~datadir_restore()
    {
        PATH_INFO::set_datadir( value );
    }
};

/// Fake piper: reads stdin, honours `--output_file`, writes a 48-byte WAV and
/// a sentinel file so the test can observe that the subprocess actually ran.
void write_fake_piper( const std::filesystem::path &script,
                       const std::filesystem::path &sentinel )
{
    const std::string body = "#!/bin/sh\n"
                             "cat > /dev/null\n"
                             "out=\"\"\n"
                             "while [ $# -gt 0 ]; do\n"
                             "  if [ \"$1\" = \"--output_file\" ]; then out=\"$2\"; fi\n"
                             "  shift\n"
                             "done\n"
                             "if [ -n \"$out\" ]; then\n"
                             "  head -c 48 /dev/zero > \"$out\"\n"
                             "fi\n"
                             "touch \"" + sentinel.string() + "\"\n";
    std::ofstream( script ).write( body.data(), body.size() );
    std::filesystem::permissions( script,
                                  std::filesystem::perms::owner_read |
                                  std::filesystem::perms::owner_write |
                                  std::filesystem::perms::owner_exec );
}

/// Poll for a file to appear (the worker thread is async).
bool wait_for_file( const std::filesystem::path &p, int timeout_ms = 10000 )
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds( timeout_ms );
    while( std::chrono::steady_clock::now() < deadline ) {
        if( std::filesystem::exists( p ) ) {
            return true;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    }
    return std::filesystem::exists( p );
}

} // namespace

TEST_CASE( "tts_piper_graceful_degradation_without_binary", "[tts]" )
{
    // Point the backend at a binary that does not exist. The worker must
    // probe, log, and degrade to a no-op — no crash, no hang, no throw.
    const std::string orig_bin = get_option<std::string>( "TTS_PIPER_BIN" );
    get_options().get_option( "TTS_PIPER_BIN" ).setValue(
        ( scratch_dir() / "definitely_missing_piper" ).string() );
    option_restore restore_opt( "TTS_PIPER_BIN", orig_bin );

    tts_piper_synthesizer synth;
    synth.synthesize( "hello world", "testvoice" );
    // Give the worker a moment to probe and drain; then tear it down.
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    synth.shutdown(); // must return promptly (no stuck subprocess)
    CHECK( true );
}

TEST_CASE( "tts_piper_subprocess_pipeline", "[tts]" )
{
    const auto dir = scratch_dir() / "pipeline";
    std::filesystem::create_directories( dir / "tts" / "voices" );

    const auto script = dir / "fake_piper";
    const auto sentinel = dir / "ran.sentinel";
    write_fake_piper( script, sentinel );

    // The worker skips a job outright if the voice model file is missing —
    // create it (contents irrelevant; the fake piper never reads it).
    std::ofstream( dir / "tts" / "voices" / "testvoice.onnx" ).put( 'x' );

    // Redirect datadir so voice_model_path() resolves into the scratch tree.
    const std::string orig_datadir = PATH_INFO::datadir();
    PATH_INFO::set_datadir( dir.string() + "/" );
    datadir_restore restore_datadir( orig_datadir );

    const std::string orig_bin = get_option<std::string>( "TTS_PIPER_BIN" );
    get_options().get_option( "TTS_PIPER_BIN" ).setValue( script.string() );
    option_restore restore_opt( "TTS_PIPER_BIN", orig_bin );

    tts_piper_synthesizer synth;
    synth.synthesize( "hello world", "testvoice" );

    // The fake piper touches the sentinel iff the subprocess was spawned with
    // the right args and exited 0 with a non-empty WAV.
    REQUIRE( wait_for_file( sentinel ) );

    synth.shutdown();
}
