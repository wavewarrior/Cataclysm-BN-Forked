#pragma once

#include "color.h"
#include "type_id.h"
#include "units_angle.h"

#include <string>
#include <vector>

class vehicle;

/**
 * The display-text cluster of the old `class vehicle`: everything the UI layers need to turn a
 * vehicle into text, symbols and colours. Free functions over the class's public accessors, so a
 * consumer of this header does not have to be rebuilt when the vehicle internals change.
 */
namespace vehicle_display {

/// "the <name>", the article-prefixed name used in messages
auto disp_name(const vehicle& veh) -> std::string;

/// get symbol for map
auto part_sym(const vehicle& veh, int p, bool exact = false) -> char;

/// Similar to `part_sym` but for use when drawing tiles: the id of the part to draw plus, in
/// `part_mod`, 1 when open and 2 when broken
auto part_id_string(const vehicle& veh, int p, bool roof, char& part_mod) -> vpart_id;

/// The direction the part at `p` is drawn in, accounting for the roof variant
auto part_display_direction(const vehicle& veh, int p, bool roof = false) -> units::angle;

/// get color for map
auto part_color(const vehicle& veh, int p, bool exact = false) -> nc_color;

/// get text and color of damage summary (e.g. "like new" or "battered")
struct damage_summary {
    std::string text;
    nc_color color;
};
auto vehicle_damage_summary(const vehicle& veh) -> damage_summary;

// RmlUi (veh_interact slice 4): the parts-at-tile list as one colour-tagged
// string (rows joined with \n), parallel to print_part_list. The curses
// x-positioned symbols / right-aligned Interior-Exterior marker are inlined
// and the scroll windowing is dropped; the hl row gets a "> " prefix.
auto part_list_text(const vehicle& veh, int p, int hl = -1) -> std::string;

/// Arguments of `parts_descs_text`. `start_at` and `start_limit` are in/out scroll state that the
/// caller owns: it must reset `start_limit` to 0 when `part` changes.
struct parts_descs_options {
    int max_y;
    int width;
    int part;
    int& start_at;
    int& start_limit;
};

// Colour-tagged descriptions for all parts on a single tile (RmlUi path; call
// with a large max_y to disable the scroll windowing).
auto parts_descs_text(const vehicle& veh, const parts_descs_options& opts) -> std::string;

/// Get all printable fuel types
auto printable_fuel_types(const vehicle& veh) -> std::vector<itype_id>;

// RmlUi (veh_interact slice 4b): the fuel gauges as colour-tagged text
// lines, parallel to print_fuel_indicators/print_fuel_indicator. The E…F
// ASCII gauge bar is dropped (semantic); each line is "<fuel>  NN%" plus
// the verbose rate / time-to-full/empty suffix.
auto fuel_indicator_lines(const vehicle& veh) -> std::vector<std::string>;

} // namespace vehicle_display
