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
