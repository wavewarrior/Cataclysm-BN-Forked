#pragma once
#ifndef CATA_SRC_LIGHTING_RMLUI_SYSTEM_INTERFACE_H
#define CATA_SRC_LIGHTING_RMLUI_SYSTEM_INTERFACE_H

#include <RmlUi/Core/SystemInterface.h>

// RmlUi SystemInterface for Cataclysm: provides RmlUi with a clock and routes
// its log messages into the game's debug log (DC::SDL). Intentionally minimal —
// translation/clipboard/cursor use RmlUi's defaults until a menu needs them.

namespace lighting {

class rmlui_system_interface: public Rml::SystemInterface {
public:
    // Seconds since process start (SDL_GetTicks based). RmlUi drives
    // animations/transitions off this.
    double GetElapsedTime() override;

    // Map RmlUi log levels onto the game's DebugLog and keep running
    // (returns true; never breaks into a debugger from a styled menu).
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
};

} // namespace lighting

#endif // CATA_SRC_LIGHTING_RMLUI_SYSTEM_INTERFACE_H
