#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "tts_synthesizer.h"

/// Piper TTS backend: shells out to a `piper` binary (ONNX voice model) to
/// synthesize NPC dialogue, then feeds the resulting WAV to the dedicated
/// `sfx::channel::tts` track.
///
/// Threading: `synthesize()` is called from the game thread inside the turn
/// loop, so it must never block. It enqueues a single-slot job (latest wins —
/// a new line supersedes the previous) and returns immediately. A dedicated
/// worker thread runs the (slow) subprocess and plays the result.
///
/// Graceful degradation: if the `piper` binary or a voice model is missing,
/// the backend logs once and becomes a no-op. It never throws or blocks the
/// game thread.
class tts_piper_synthesizer : public tts_synthesizer_base
{
    public:
        tts_piper_synthesizer();
        ~tts_piper_synthesizer() override;

        tts_piper_synthesizer( const tts_piper_synthesizer & ) = delete;
        tts_piper_synthesizer &operator=( const tts_piper_synthesizer & ) = delete;

        void synthesize( const std::string &text, const std::string &voice_name ) override;

        /// Signal the worker to stop and join it. Idempotent. Must be called
        /// before the audio mixer is torn down.
        void shutdown();

    private:
        struct job {
            std::string text;
            std::string voice;
        };

        void worker_loop();
        /// Start the worker if not already running. Caller must hold `mtx`.
        bool ensure_worker_locked();
        std::string voice_model_path( const std::string &voice ) const;
        bool synthesize_to_wav( const std::string &bin, const std::string &model,
                                const std::string &text, const std::string &wav_path );
        std::string cache_key( const std::string &text, const std::string &voice ) const;
        bool cache_lookup( const std::string &key, std::string &wav_path ) const;
        void cache_store( const std::string &key, const std::string &wav_path );

        mutable std::mutex mtx;
        std::condition_variable cv;
        std::thread worker;
        bool running = false;
        bool worker_started = false;
        bool bin_probed = false;
        bool bin_found = false;
        std::string piper_bin;
        job pending;
        bool has_pending = false;
        std::unordered_map<std::string, std::string> cache;
};

/// Shut down the global Piper TTS backend (join the worker). No-op if the
/// active backend is not the Piper one or TTS was never initialized.
void shutdown_tts_synthesizer();
