#include "tts_piper_synthesizer.h"

#include <filesystem>
#include <random>

#include <SDL3/SDL.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_process.h>

#include "debug.h"
#include "options.h"
#include "path_info.h"
#include "sounds.h"

namespace
{

constexpr size_t MAX_CACHE_ENTRIES = 64;

/// Active Piper backend (owned by the static in init_tts_synthesizer).
tts_piper_synthesizer *g_piper_backend = nullptr;

/// Locate the `piper` binary: explicit option → bundled under datadir → PATH.
auto find_piper_binary() -> std::string
{
    const std::string explicit_bin = get_option<std::string>( "TTS_PIPER_BIN" );
    if( !explicit_bin.empty() && std::filesystem::exists( explicit_bin ) ) {
        return explicit_bin;
    }
    const std::string bundled = PATH_INFO::datadir() + "tts/piper";
    if( std::filesystem::exists( bundled ) ) {
        return bundled;
    }
    // PATH lookup: SDL_CreateProcess resolves the program via the environment.
    return "piper";
}

/// A unique temp WAV path under the system temp dir.
auto make_temp_wav_path() -> std::string
{
    static std::mt19937_64 rng{ std::random_device{}() };
    const uint64_t nonce = rng();
    return ( std::filesystem::temp_directory_path() /
             ( "cbn_tts_" + std::to_string( nonce ) + ".wav" ) )
        .string();
}

} // namespace

tts_piper_synthesizer::tts_piper_synthesizer()
{
    g_piper_backend = this;
}

tts_piper_synthesizer::~tts_piper_synthesizer()
{
    shutdown();
}

void tts_piper_synthesizer::synthesize( const std::string &text, const std::string &voice_name )
{
    if( text.empty() ) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock( mtx );
        if( !ensure_worker_locked() ) {
            return;
        }
        // Latest-wins: a new line supersedes whatever is still queued.
        pending = job{ .text = text, .voice = voice_name };
        has_pending = true;
    }
    cv.notify_one();
}

bool tts_piper_synthesizer::ensure_worker_locked()
{
    if( worker_started ) {
        return running;
    }
    worker_started = true;
    running = true;
    worker = std::thread( [this]() { worker_loop(); } );
    return true;
}

void tts_piper_synthesizer::shutdown()
{
    {
        std::lock_guard<std::mutex> lock( mtx );
        if( !worker_started ) {
            return;
        }
        running = false;
    }
    cv.notify_all();
    if( worker.joinable() ) {
        worker.join();
    }
    worker_started = false;
}

void tts_piper_synthesizer::worker_loop()
{
    for( ;; ) {
        job cur;
        {
            std::unique_lock<std::mutex> lock( mtx );
            cv.wait( lock, [this]() { return !running || has_pending; } );
            if( !running ) {
                return;
            }
            cur = pending;
            has_pending = false;
        }

        if( !bin_probed ) {
            bin_probed = true;
            piper_bin = find_piper_binary();
            bin_found = std::filesystem::exists( piper_bin );
            if( !bin_found ) {
                DebugLog( DL::Warn, DC::NPC ) << "TTS(piper): binary not found at \""
                                              << piper_bin << "\"; TTS disabled.";
            }
        }
        if( !bin_found ) {
            continue; // degrade to no-op, keep draining
        }

        const std::string key = cache_key( cur.text, cur.voice );
        std::string wav_path;
        if( cache_lookup( key, wav_path ) ) {
            sfx::play_tts_audio( wav_path );
            continue;
        }

        const std::string model = voice_model_path( cur.voice );
        if( model.empty() ) {
            DebugLog( DL::Warn, DC::NPC ) << "TTS(piper): voice model missing for \"" << cur.voice
                                          << "\"; skipping line.";
            continue;
        }

        wav_path = make_temp_wav_path();
        if( !synthesize_to_wav( piper_bin, model, cur.text, wav_path ) ) {
            std::filesystem::remove( wav_path );
            continue;
        }
        cache_store( key, wav_path );
        sfx::play_tts_audio( wav_path );
    }
}

std::string tts_piper_synthesizer::voice_model_path( const std::string &voice ) const
{
    if( voice.empty() ) {
        return "";
    }
    const std::string path = PATH_INFO::datadir() + "tts/voices/" + voice + ".onnx";
    return std::filesystem::exists( path ) ? path : "";
}

bool tts_piper_synthesizer::synthesize_to_wav( const std::string &bin, const std::string &model,
                                               const std::string &text,
                                               const std::string &wav_path )
{
    // piper reads text from stdin and writes a WAV to --output_file.
    const char *args[] = { bin.c_str(), "-m", model.c_str(), "--output_file", wav_path.c_str(),
                           nullptr };

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty( props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                            reinterpret_cast<void *>( const_cast<const char **>( args ) ) );
    SDL_SetNumberProperty( props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                           SDL_PROCESS_STDIO_APP );
    SDL_SetBooleanProperty( props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true );

    SDL_Process *proc = SDL_CreateProcessWithProperties( props );
    SDL_DestroyProperties( props );
    if( !proc ) {
        DebugLog( DL::Warn, DC::NPC ) << "TTS(piper): failed to spawn: " << SDL_GetError();
        return false;
    }

    if( SDL_IOStream *stdin_stream = SDL_GetProcessInput( proc ) ) {
        SDL_WriteIO( stdin_stream, text.data(), text.size() );
        SDL_CloseIO( stdin_stream );
    }

    int exitcode = -1;
    void *out = SDL_ReadProcess( proc, nullptr, &exitcode );
    if( out ) {
        SDL_free( out );
    }
    SDL_DestroyProcess( proc );

    const bool ok = ( exitcode == 0 ) && std::filesystem::exists( wav_path ) &&
                    std::filesystem::file_size( wav_path ) > 44; // non-empty WAV
    if( !ok ) {
        DebugLog( DL::Warn, DC::NPC ) << "TTS(piper): synthesis failed (exit " << exitcode
                                      << ") for voice \"" << model << '"';
    }
    return ok;
}

std::string tts_piper_synthesizer::cache_key( const std::string &text,
                                              const std::string &voice ) const
{
    return voice + "\n" + text;
}

bool tts_piper_synthesizer::cache_lookup( const std::string &key, std::string &wav_path ) const
{
    const auto it = cache.find( key );
    if( it == cache.end() ) {
        return false;
    }
    wav_path = it->second;
    return std::filesystem::exists( wav_path );
}

void tts_piper_synthesizer::cache_store( const std::string &key, const std::string &wav_path )
{
    if( cache.size() >= MAX_CACHE_ENTRIES ) {
        cache.clear(); // crude eviction; TTS lines are cheap to re-synth
    }
    cache.emplace( key, wav_path );
}

// ── Global wiring ─────────────────────────────────────────────────────────────

void shutdown_tts_synthesizer()
{
    if( g_piper_backend ) {
        g_piper_backend->shutdown();
    }
}
