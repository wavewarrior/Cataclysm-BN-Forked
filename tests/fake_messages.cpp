#include "enums.h"
#include "messages.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

class JsonObject;
class JsonOut;

/**
 * Stubs to turn all Messages calls into no-ops for unit testing.
 */

std::vector<std::pair<std::string, std::string>> Messages::recent_messages(size_t) {
    return std::vector<std::pair<std::string, std::string>>();
}
std::vector<std::pair<std::string, std::string>> Messages::recent_messages_colored(size_t) {
    return std::vector<std::pair<std::string, std::string>>();
}
auto Messages::recent_messages_rich(size_t) -> std::vector<Messages::rich_message> { return {}; }
void Messages::add_msg(std::string) {}
void Messages::add_msg(const game_message_params&, std::string) {}
void Messages::clear_messages() {}
void Messages::deactivate() {}
size_t Messages::size() { return 0; }
bool Messages::has_undisplayed_messages() { return false; }
void Messages::display_messages() {}
void Messages::serialize(JsonOut&) {}
void Messages::deserialize(const JsonObject&) {}

void add_msg(std::string) {}
void add_msg(const game_message_params&, std::string) {}

bool& messages_rmlui_enabled() {
    static bool enabled = false;
    return enabled;
}
