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
