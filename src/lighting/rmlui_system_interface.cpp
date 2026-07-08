#include "rmlui_system_interface.h"

#include "debug.h"

#include <SDL3/SDL.h>

// Lighting/ files must define dbg themselves (not globally available).
#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

double rmlui_system_interface::GetElapsedTime() {
    return static_cast<double>(SDL_GetTicks()) / 1000.0;
}

bool rmlui_system_interface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
    switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            dbg(DL::Error) << "rmlui: " << message;
            break;
        case Rml::Log::LT_WARNING:
            dbg(DL::Warn) << "rmlui: " << message;
            break;
        default:
            dbg(DL::Info) << "rmlui: " << message;
            break;
    }
    // Always continue execution — never break into a debugger from a menu.
    return true;
}

} // namespace lighting
