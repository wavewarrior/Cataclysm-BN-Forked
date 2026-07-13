#pragma once

#include <string>

/// Abstract base for TTS synthesis backends.
/// Designed for future ONNX Runtime integration.
class tts_synthesizer_base
{
    public:
        virtual ~tts_synthesizer_base() = default;

        /// Synthesize spoken audio for the given text using the named voice.
        /// Derived implementations produce actual audio; the stub logs the request.
        virtual void synthesize( const std::string &text, const std::string &voice_name ) = 0;
};

/// Stub synthesizer that logs synthesis requests instead of producing audio.
/// Safe to use when no TTS backend is available -- provides graceful degradation.
class tts_stub_synthesizer : public tts_synthesizer_base
{
    public:
        void synthesize( const std::string &text, const std::string &voice_name ) override;
};

/// Global TTS synthesizer pointer. Nullptr means TTS is not initialized.
extern tts_synthesizer_base *g_tts_synthesizer;

/// Initialize the global TTS synthesizer to the stub implementation.
/// Call once at startup; derived backends can replace the pointer later.
void init_tts_synthesizer();
