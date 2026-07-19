#pragma once

#include <string>

namespace coop_menu
{

/// Show the CO-OP main menu (Host / Join / Back).
/// Called from main_menu.cpp when the user selects "CO-OP".
auto run() -> bool;
auto start_host() -> void;
auto start_join() -> void;

} // namespace coop_menu

/// Show a yes/no popup with the given message.
/// Returns true if the user confirmed, false if dismissed or rejected.
auto show_coop_popup( const std::string& message ) -> bool;

