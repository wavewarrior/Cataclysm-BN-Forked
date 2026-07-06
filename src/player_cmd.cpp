#include "player_cmd.h"

#include "action.h"
#include "coordinates.h"

player_cmd_t make_player_move_cmd(const action_id act, const iso_rotate rot) {
    const auto delta = get_delta_from_movement_action(act, rot);
    if (delta == point_rel_ms::zero()) { return player_cmd_t{}; }
    return player_cmd_t{
        .kind = player_cmd_kind::move,
        .delta = tripoint_rel_ms{delta.x(), delta.y(), 0},
    };
}
player_cmd_t make_player_smash_cmd(const tripoint_abs_ms abs_target) {
    return player_cmd_t{.kind = player_cmd_kind::smash, .target_abs = abs_target};
}
player_cmd_t make_player_fire_cmd(const tripoint_abs_ms abs_target) {
    return player_cmd_t{.kind = player_cmd_kind::fire, .target_abs = abs_target};
}
player_cmd_t make_player_eat_cmd() {
    return player_cmd_t{.kind = player_cmd_kind::eat};
}
player_cmd_t make_player_reload_cmd() {
    return player_cmd_t{.kind = player_cmd_kind::reload};
}
std::string_view move_cmd_to_dir_string(const player_cmd_t& cmd) {
    if (cmd.kind != player_cmd_kind::move) { return {}; }
    const auto d = cmd.delta.xy();
    if (d == point_rel_ms{0, -1}) { return "MOVE_N"; }
    if (d == point_rel_ms{1, -1}) { return "MOVE_NE"; }
    if (d == point_rel_ms{1, 0}) { return "MOVE_E"; }
    if (d == point_rel_ms{1, 1}) { return "MOVE_SE"; }
    if (d == point_rel_ms{0, 1}) { return "MOVE_S"; }
    if (d == point_rel_ms{-1, 1}) { return "MOVE_SW"; }
    if (d == point_rel_ms{-1, 0}) { return "MOVE_W"; }
    if (d == point_rel_ms{-1, -1}) { return "MOVE_NW"; }
    return {};
}
player_cmd_t parse_move_cmd(const std::string_view key) {
    auto make = [](int dx, int dy) -> player_cmd_t {
        return {.kind = player_cmd_kind::move, .delta = tripoint_rel_ms{dx, dy, 0}};
    };
    if (key == "MOVE_N" || key == "UP") { return make(0, -1); }
    if (key == "MOVE_S" || key == "DOWN") { return make(0, 1); }
    if (key == "MOVE_E" || key == "RIGHT") { return make(1, 0); }
    if (key == "MOVE_W" || key == "LEFT") { return make(-1, 0); }
    if (key == "MOVE_NE") { return make(1, -1); }
    if (key == "MOVE_NW") { return make(-1, -1); }
    if (key == "MOVE_SE") { return make(1, 1); }
    if (key == "MOVE_SW") { return make(-1, 1); }
    return {};
}
