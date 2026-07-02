#pragma once
#ifdef COOP_ENABLED

namespace catacurses
{
class window;
} // namespace catacurses

namespace coop_hud
{

/// Draw the partner status panel in window `w`.
/// Called from game_ui.cpp when coop_session::get().is_coop().
auto draw( const catacurses::window& w ) -> void;

} // namespace coop_hud

#endif // COOP_ENABLED
