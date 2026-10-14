#include "calendar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "color.h"
#include "cursesdef.h"
#include "debug.h"
#include "itype.h"
#include "options.h"
#include "output.h"
#include "string_formatter.h"
#include "translations.h"
#include "units.h"
#include "units_utility.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_display.h"
#include "vehicle_part.h" // IWYU pragma: associated
#include "vpart_position.h"
#include "vpart_range.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <numeric>
#include <optional>
#include <set>

static const std::string part_location_structure("structure");
static const itype_id itype_battery("battery");
static const itype_id fuel_type_muscle("muscle");

namespace vehicle_display {

auto disp_name(const vehicle& veh) -> std::string {
    return string_format(_("the %s"), veh.name);
}

auto part_sym(const vehicle& veh, const int p, const bool exact) -> char {
    if (p < 0 || p >= veh.part_count() || veh.cpart(p).removed) { return ' '; }

    const int displayed_part = exact ? p : veh.part_displayed_at(veh.cpart(p).mount);

    if (veh.part_flag(displayed_part, VPFLAG_OPENABLE) && veh.cpart(displayed_part).open) {
        // open door
        return '\'';
    } else {
        return veh.cpart(displayed_part).is_broken()
                 ? veh.part_info(displayed_part).sym_broken
                 : (veh.cpart(displayed_part).proxy_sym == '\0'
                        ? veh.part_info(displayed_part).sym
                        : veh.cpart(displayed_part).proxy_sym);
    }
}

auto part_display_direction(const vehicle& veh, const int p, const bool roof) -> units::angle {
    if (p < 0 || p >= veh.part_count() || veh.cpart(p).removed) { return veh.face.dir(); }

    int displayed_part = -1;
    if (roof) { displayed_part = veh.roof_at_part(p); }
    if (displayed_part < 0 || displayed_part >= veh.part_count()
        || veh.cpart(displayed_part).removed) {
        displayed_part = veh.part_displayed_at(veh.cpart(p).mount);
    }
    if (displayed_part < 0 || displayed_part >= veh.part_count()
        || veh.cpart(displayed_part).removed) {
        return veh.face.dir();
    }

    return normalize(veh.face.dir() + veh.cpart(displayed_part).direction);
}

// similar to part_sym() but for use when drawing SDL tiles. Called only by cata_tiles
// during draw_vpart vector returns at least 1 element, max of 2 elements. If 2 elements the
// second denotes if it is open or damaged
auto part_id_string(const vehicle& veh, const int p, bool roof, char& part_mod) -> vpart_id {
    part_mod = 0;
    if (p < 0 || p >= veh.part_count() || veh.cpart(p).removed) {
        return vpart_id::NULL_ID();
    }

    int displayed_part = -1;

    if (roof) { displayed_part = veh.roof_at_part(p); }
    if (displayed_part < 0 || displayed_part >= veh.part_count()
        || veh.cpart(displayed_part).removed) {
        displayed_part = veh.part_displayed_at(veh.cpart(p).mount);
    }
    if (displayed_part < 0 || displayed_part >= veh.part_count()
        || veh.cpart(displayed_part).removed) {
        return vpart_id::NULL_ID();
    }

    const vpart_id idinfo =
        veh.cpart(displayed_part).proxy_part_id == vpart_id::NULL_ID()
            ? veh.part_info(displayed_part).id
            : veh.cpart(displayed_part).proxy_part_id;

    if (veh.part_flag(displayed_part, VPFLAG_OPENABLE) && veh.cpart(displayed_part).open) {
        // open
        part_mod = 1;
    } else if (veh.cpart(displayed_part).is_broken()) {
        // broken
        part_mod = 2;
    }

    return idinfo;
}

auto part_color(const vehicle& veh, const int p, const bool exact) -> nc_color {
    if (p < 0 || p >= veh.part_count()) { return c_black; }

    nc_color col;

    int parm = -1;

    // If armoring is present and the option is set, it colors the visible part
    if (get_option<bool>("VEHICLE_ARMOR_COLOR")) {
        parm = veh.part_with_feature(p, VPFLAG_ARMOR, false);
    }

    if (parm >= 0) {
        col = veh.part_info(parm).color;
    } else {
        const int displayed_part = exact ? p : veh.part_displayed_at(veh.cpart(p).mount);

        if (displayed_part < 0 || displayed_part >= veh.part_count()) {
            return c_black;
        }
        if (veh.cpart(displayed_part).blood > 200) {
            col = c_red;
        } else if (veh.cpart(displayed_part).blood > 0) {
            col = c_light_red;
        } else if (veh.cpart(displayed_part).is_broken()) {
            col = veh.part_info(displayed_part).color_broken;
        } else {
            col = veh.part_info(displayed_part).color;
        }
    }

    if (exact) { return col; }

    // curtains turn windshields gray
    int curtains = veh.part_with_feature(p, VPFLAG_CURTAIN, false);
    if (curtains >= 0) {
        if (veh.part_with_feature(p, VPFLAG_WINDOW, true) >= 0 && !veh.cpart(curtains).open) {
            col = veh.part_info(curtains).color;
        }
    }

    // Invert colors for cargo parts with stuff in them
    int cargo_part = veh.part_with_feature(p, VPFLAG_CARGO, true);
    if (cargo_part > 0 && !veh.get_items(cargo_part).empty()) {
        return invert_color(col);
    } else {
        return col;
    }
}

auto vehicle_damage_summary(const vehicle& veh) -> damage_summary {
    const vehicle_part_range vpr = veh.get_all_parts();
    const int total_damage =
        std::accumulate(vpr.begin(), vpr.end(), 0, [](int lhs, const vpart_reference& rhs) {
            return lhs + std::max(rhs.part().damage(), 0);
        });
    const int total_max =
        std::accumulate(vpr.begin(), vpr.end(), 0, [](int lhs, const vpart_reference& rhs) {
            return lhs + rhs.part().max_damage();
        });
    const int pct = total_max ? 100 * total_damage / total_max : 0;

    if (total_damage == 0) {
        return {_("perfect"), c_green};
    } else if (pct < 5) {
        return {_("like new"), c_light_green};
    } else if (pct < 33) {
        return {_("dented"), c_yellow};
    } else if (pct < 66) {
        return {_("battered"), c_magenta};
    } else if (pct < 100) {
        return {_("wrecked"), c_red};
    }
    return {_("destroyed"), c_dark_gray};
}

// RmlUi (veh_interact slice 4): the parts-at-tile list as a single colour-tagged
// string, parallel to print_part_list (detail=true path). The curses x-positioned
// symbols + the right-aligned Interior/Exterior marker are inlined, and the scroll
// windowing is dropped for native scroll; the highlighted row gets a "> " prefix.
auto part_list_text(const vehicle& veh, int p, int hl) -> std::string {
    if (p < 0 || p >= veh.part_count()) { return std::string(); }
    const std::vector<int> pl = veh.parts_at_relative(veh.cpart(p).mount, true);
    std::string out;

    for (size_t i = 0; i < pl.size(); i++) {
        const vehicle_part& vp = veh.cpart(pl[i]);
        std::string partname = vp.name();

        if (vp.is_fuel_store() && !vp.ammo_current().is_null()) {
            if (vp.ammo_current() == itype_battery) {
                partname +=
                    string_format(_(" (%s/%s charge)"), vp.ammo_remaining(), vp.ammo_capacity());
            } else {
                const itype* pt_ammo_cur = &*vp.ammo_current();
                auto stack = units::legacy_volume_factor / pt_ammo_cur->stack_size;
                partname += string_format(
                    _(" (%.1fL %s)"), round_up(units::to_liter(vp.ammo_remaining() * stack), 1),
                    item::nname(vp.ammo_current()));
            }
        }
        if (veh.part_flag(pl[i], "CARGO")) {
            //~ used/total volume of a cargo vehicle part
            partname += string_format(
                _(" (vol: %s/%s %s)"), format_volume(veh.stored_volume(pl[i])),
                format_volume(veh.max_volume(pl[i])), volume_units_abbr());
        }

        std::string left_sym;
        std::string right_sym;
        if (veh.part_flag(pl[i], "ARMOR")) {
            left_sym = "(";
            right_sym = ")";
        } else if (veh.part_info(pl[i]).location == part_location_structure) {
            left_sym = "[";
            right_sym = "]";
        } else {
            left_sym = "-";
            right_sym = "-";
        }

        std::string side;
        if (i == 0) {
            side = vpart_position(const_cast<vehicle&>(veh), pl[i]).is_inside()
                     ? _("Interior")
                     : _("Exterior");
        }

        std::string row =
            (static_cast<int>(i) == hl ? "> " : "  ") + left_sym + partname + right_sym;
        if (!side.empty()) { row += "  " + side; }
        out += colorize(row, c_light_gray) + "\n";
    }

    const std::optional<std::string> label =
        vpart_position(const_cast<vehicle&>(veh), p).get_label();
    if (label) {
        out += colorize(string_format(_("Label: %s"), label.value()), c_light_red) + "\n";
    }

    return out;
}

/**
 * Prints a list of descriptions for all parts to the screen inside of a boxed window
 * @param win The window to draw in.
 * @param max_y Draw no further than this y-coordinate.
 * @param width The width of the window.
 * @param p The index of the part being examined.
 * @param start_at Which vehicle part to start printing at.
 * @param start_limit the part index beyond which the display is full
 */
// Colour-tagged descriptions for all the parts on a single tile (RmlUi path; call
// with a large max_y to disable the scroll windowing).
auto parts_descs_text(const vehicle& veh, const parts_descs_options& opts) -> std::string {
    const int max_y = opts.max_y;
    const int width = opts.width;
    const int p = opts.part;
    int& start_at = opts.start_at;
    int& start_limit = opts.start_limit;
    if (p < 0 || p >= veh.part_count()) { return std::string(); }

    std::vector<int> pl = veh.parts_at_relative(veh.cpart(p).mount, true);
    std::string msg;

    int lines = 0;
    /*
     * start_at and start_limit interaction is little tricky
     * start_at and start_limit start at 0 when moving to a new frame
     * if all the descriptions are displayed in the window, start_limit stays at 0 and
     *    start_at is capped at 0 - so no scrolling at all.
     * if all the descriptions aren't displayed, start_limit jumps to the last displayed part
     *    and the next scrollthrough can start there - so scrolling down happens.
     * when the scroll reaches the point where all the remaining descriptions are displayed in
     *    the window, start_limit is set to start_at again.
     * on the next attempted scrolldown, start_limit is set to the nth item, and start_at is
     *    capped to the nth item, so no more scrolling down.
     * start_at can always go down, but never below 0, so scrolling up is only possible after
     *    some scrolling down has occurred.
     * important! the calling function needs to track p, start_at, and start_limit, and set
     *    start_limit to 0 if p changes.
     */
    start_at = std::max(0, std::min(start_at, start_limit));
    if (start_at) {
        msg += std::string("<color_yellow>") + "<  " + _("More parts…") + "</color>\n";
        lines += 1;
    }
    for (size_t i = start_at; i < pl.size(); i++) {
        const vehicle_part& vp = veh.cpart(pl[i]);
        std::string possible_msg;
        const nc_color name_color = vp.is_broken() ? c_dark_gray : c_light_green;
        possible_msg += colorize(vp.name(), name_color) + "\n";
        const nc_color desc_color = vp.is_broken() ? c_dark_gray : c_light_gray;
        // -4 = -2 for left & right padding + -2 for "> "
        int new_lines = 2 + vp.info().format_description(possible_msg, desc_color, width - 4);
        if (vp.has_flag(vehicle_part::carrying_flag)) {
            possible_msg += "  Carrying a vehicle on a rack.\n";
            new_lines += 1;
        }
        if (vp.has_flag(vehicle_part::carried_flag)) {
            possible_msg += string_format("  Part of a %s carried on a rack.\n", vp.carried_name());
            new_lines += 1;
        }

        possible_msg += "</color>\n";
        if (lines + new_lines <= max_y) {
            msg += possible_msg;
            lines += new_lines;
            start_limit = start_at;
        } else {
            msg += std::string("<color_yellow>") + _("More parts…") + "  >" + "</color>\n";
            start_limit = i;
            break;
        }
    }
    return msg;
}

/**
 * Returns an array of fuel types that can be printed
 * @return An array of printable fuel type ids
 */
auto printable_fuel_types(const vehicle& veh) -> std::vector<itype_id> {
    std::set<itype_id> opts;
    const int count = veh.part_count();
    for (int i = 0; i < count; i++) {
        const vehicle_part& pt = veh.cpart(i);
        if (pt.is_fuel_store() && !pt.ammo_current().is_null()) { opts.emplace(pt.ammo_current()); }
    }

    std::vector<itype_id> res(opts.begin(), opts.end());

    std::ranges::sort(res, [&](const itype_id& lhs, const itype_id& rhs) {
        return veh.basic_consumption(rhs) < veh.basic_consumption(lhs);
    });

    return res;
}

// RmlUi (veh_interact slice 4b): the fuel gauges as colour-tagged text lines,
// parallel to print_fuel_indicators (fullsize/verbose path) + print_fuel_indicator.
// The E…F ASCII gauge bar + the gauge windowing ('>' for more) are dropped; each
// printable fuel becomes one line "<fuel>  NN%" + the verbose rate / ETA suffix.
auto fuel_indicator_lines(const vehicle& veh) -> std::vector<std::string> {
    std::vector<std::string> lines;
    const std::vector<itype_id> fuels = printable_fuel_types(veh);
    for (const itype_id& fuel_type : fuels) {
        const int cap = veh.fuel_capacity(fuel_type);
        const int f_left = veh.fuel_left(fuel_type);
        const nc_color f_color = fuel_type->color;
        const int pct = cap > 0 ? f_left * 100 / cap : 0;
        std::string line = string_format("%s  %d%%", item::nname(fuel_type), pct);

        // Verbose rate / time-to-goal (parallels print_fuel_indicator's verbose block).
        int rate = 0;
        std::string units;
        const auto fuel_data = veh.fuel_used_last_turn.find(fuel_type);
        if (fuel_data != veh.fuel_used_last_turn.end()) {
            rate = -veh.consumption_per_hour(fuel_type, fuel_data->second);
            units = _("mL");
        }
        if (fuel_type == itype_id("battery")) {
            rate += veh.power_to_energy_bat(veh.net_battery_charge_rate_w(), 1_hours);
            units = _("kJ");
        }
        if (rate != 0 && cap > 0) {
            int tank_use = 0;
            std::string tank_goal = _("full");
            bool have_use = true;
            if (rate > 0) {
                tank_use = cap - f_left;
                if (!tank_use) { have_use = false; }
            } else {
                if (!f_left) { have_use = false; }
                tank_use = f_left;
                tank_goal = _("empty");
            }
            if (have_use) {
                item& fitem = *item::spawn_temporary(fuel_type);
                const int charges_per_L = fitem.charges_per_volume(1_liter);
                if (charges_per_L != 0 && charges_per_L != item::INFINITE_CHARGES) {
                    const float charges_per_mL = charges_per_L / 1000.0f;
                    tank_use = tank_use / charges_per_mL;
                    const double turns = to_turns<double>(60_minutes);
                    const time_duration estimate = time_duration::from_turns(
                        turns * tank_use / std::abs(rate));
                    if (debug_mode) {
                        line += string_format(
                            _(", %d %s(%4.2f%%)/hour, %s until %s"), rate, units,
                            100.0 * rate / cap, to_string_clipped(estimate), tank_goal);
                    } else {
                        line += string_format(
                            _(", %3.1f%% / hour, %s until %s"), 100.0 * rate / cap,
                            to_string_clipped(estimate), tank_goal);
                    }
                }
            }
        }
        lines.emplace_back(colorize(line, f_color));
    }
    return lines;
}

} // namespace vehicle_display
