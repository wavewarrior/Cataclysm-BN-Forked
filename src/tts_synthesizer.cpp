#include "tts_synthesizer.h"

#include "debug.h"

tts_synthesizer_base *g_tts_synthesizer = nullptr;

void tts_stub_synthesizer::synthesize( const std::string &text, const std::string &voice_name )
{
    // Stub: log the request and return immediately.
    // Future ONNX Runtime backend will replace this with actual audio synthesis.
    DebugLog( DL::Info, DC::NPC ) << "TTS(stub): synthesize text=\"" << text
                                  << "\" voice=\"" << voice_name << '"';
}

void init_tts_synthesizer()
{
    static tts_stub_synthesizer stub;
    g_tts_synthesizer = &stub;
}
