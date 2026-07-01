#pragma once
#ifdef COOP_ENABLED

namespace coop_menu {

/// Show the CO-OP main menu (Host / Join / Back).
/// Called from main_menu.cpp when the user selects "CO-OP".
auto run() -> void;
auto start_host() -> void;
auto start_join() -> void;

} // namespace coop_menu

#endif // COOP_ENABLED
