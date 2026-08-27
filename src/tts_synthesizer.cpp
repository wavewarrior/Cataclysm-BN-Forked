#include "tts_synthesizer.h"

#include "debug.h"
#include "tts_piper_synthesizer.h"

tts_synthesizer_base *g_tts_synthesizer = nullptr;

void tts_stub_synthesizer::synthesize( const std::string &text, const std::string &voice_name )
{
    // Stub: log the request and return immediately.
    DebugLog( DL::Info, DC::NPC ) << "TTS(stub): synthesize text=\"" << text
                                  << "\" voice=\"" << voice_name << '"';
}

void init_tts_synthesizer()
{
    // The Piper backend degrades to a logged no-op when the binary is absent,
    // so it is always the right choice over the stub.
    static tts_piper_synthesizer piper;
    g_tts_synthesizer = &piper;
}
