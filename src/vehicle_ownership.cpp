#include "vehicle_ownership.h"

#include "avatar.h"
#include "avatar_functions.h"
#include "calendar.h"
#include "character.h"
#include "debug.h"
#include "faction.h"
#include "game.h"
#include "output.h"
#include "translations.h"
#include "type_id.h"
#include "vehicle.h"
#include "vehicle_display.h"

namespace vehicle_ownership {

auto is_owned_by(const vehicle& veh, const Character& c, const bool available_to_take) -> bool {
    if (!has_owner(veh)) { return available_to_take; }
    if (!c.get_faction()) {
        debugmsg("vehicle_ownership::is_owned_by() player %s has no faction", c.disp_name());
        return false;
    }
    return c.get_faction()->id() == veh.get_owner();
}

auto is_old_owner(const vehicle& veh, const Character& c, const bool available_to_take) -> bool {
    if (!has_old_owner(veh)) { return available_to_take; }
    if (!c.get_faction()) {
        debugmsg("vehicle_ownership::is_old_owner() player %s has no faction", c.disp_name());
        return false;
    }
    return c.get_faction()->id() == veh.get_old_owner();
}

auto get_owner_name(const vehicle& veh) -> std::string {
    if (!g->faction_manager_ptr->get(veh.get_owner())) {
        debugmsg("vehicle_ownership::get_owner_name() vehicle %s has no valid nor null faction id ",
                 vehicle_display::disp_name(veh));
        return _("no owner");
    }
    return _(g->faction_manager_ptr->get(veh.get_owner())->name());
}

auto has_owner(const vehicle& veh) -> bool { return !veh.get_owner().is_null(); }

auto has_old_owner(const vehicle& veh) -> bool { return !veh.get_old_owner().is_null(); }

auto set_owner(vehicle& veh, const Character& c) -> void {
    const auto faction = c.get_faction();
    if (!faction) {
        debugmsg("vehicle_ownership::set_owner() player %s has no valid faction", c.disp_name());
        return;
    }
    veh.set_owner(faction->id());
}

auto handle_potential_theft(vehicle& veh, avatar& you, const theft_options& opts) -> bool {
    const bool is_owned_by_player = is_owned_by(veh, you);
    bool has_witnesses;
    if (has_owner(veh) && !is_owned_by_player) {
        has_witnesses =
            !avatar_funcs::list_potential_theft_witnesses(you, veh.get_owner()).empty();
    } else {
        has_witnesses = false;
    }
    // the vehicle is yours, that's fine.
    if (is_owned_by_player) {
        return true;
        // if There is no owner
        // handle transfer of ownership
    }

    // if we are just checking if we could continue without problems, then the rest is assumed false
    if (opts.check_only) { return false; }

    if (!has_owner(veh)) {
        veh.set_owner(you.get_faction()->id());
        veh.remove_old_owner();
        return true;
        // if there is a marker for having been stolen, but 15 minutes have passed, then officially
        // transfer ownership
    }

    if (!has_witnesses && has_old_owner(veh)) {
        const auto stolen_at = veh.stolen_at();
        if (!is_old_owner(veh, you) && stolen_at && calendar::turn - *stolen_at > 15_minutes) {
            veh.set_owner(you.get_faction()->id());
            veh.remove_old_owner();
        }
        return true;
        // No witnesses? then don't need to prompt, we assume the player is in process of stealing
        // it. Ownership transfer checking is handled above, and warnings handled below. This is
        // just to perform interaction with the vehicle without a prompt. It will prompt first-time,
        // even with no witnesses, to inform player it is owned by someone else subsequently, no
        // further prompts, the player should know by then.
    }

    // if we got here, there's some theft occurring
    if (opts.prompt) {
        if (!query_yn(
                _("This vehicle belongs to: %s, there may be consequences if you are observed interacting with it, continue?"),
                _(get_owner_name(veh)))) {
            return false;
        }
    }
    // set old owner so that we can restore ownership if there are witnesses.
    veh.set_old_owner(veh.get_owner());
    if (avatar_funcs::handle_theft_witnesses(you, veh.get_owner())) {
        // remove the temporary marker for a successful theft, as it was witnessed.
        veh.remove_old_owner();
    }
    // if we got here, then the action will proceed after the previous warning
    return true;
}

} // namespace vehicle_ownership
