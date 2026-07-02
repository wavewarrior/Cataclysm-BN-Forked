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
#include "vehicle_part.h" // IWYU pragma: associated
#include "vpart_position.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <set>

static const std::string part_location_structure( "structure" );
static const itype_id itype_battery( "battery" );
static const itype_id fuel_type_muscle( "muscle" );

std::string vehicle::disp_name() const { return string_format( _( "the %s" ), name ); }

char vehicle::part_sym( const int p, const bool exact ) const
{
    if( p < 0 || p >= static_cast<int>( parts.size() ) || parts[p].removed ) { return ' '; }

    const int displayed_part = exact ? p : part_displayed_at( parts[p].mount );

    if( part_flag( displayed_part, VPFLAG_OPENABLE ) && parts[displayed_part].open ) {
        // open door
        return '\'';
    } else {
        return parts[displayed_part].is_broken()
               ? part_info( displayed_part ).sym_broken
               : ( parts[displayed_part].proxy_sym == '\0'
                   ? part_info( displayed_part ).sym
                   : parts[displayed_part].proxy_sym );
    }
}

auto vehicle::part_display_direction( const int p, const bool roof ) const -> units::angle
{
    if( p < 0 || p >= static_cast<int>( parts.size() ) || parts[p].removed ) { return face.dir(); }

    int displayed_part = -1;
    if( roof ) { displayed_part = roof_at_part( p ); }
    if( displayed_part < 0 || displayed_part >= static_cast<int>( parts.size() )
        || parts[displayed_part].removed ) {
        displayed_part = part_displayed_at( parts[p].mount );
    }
    if( displayed_part < 0 || displayed_part >= static_cast<int>( parts.size() )
        || parts[displayed_part].removed ) {
        return face.dir();
    }

    return normalize( face.dir() + parts[displayed_part].direction );
}

// similar to part_sym(int p) but for use when drawing SDL tiles. Called only by cata_tiles
// during draw_vpart vector returns at least 1 element, max of 2 elements. If 2 elements the
// second denotes if it is open or damaged
vpart_id vehicle::part_id_string( const int p, bool roof, char &part_mod ) const
{
    part_mod = 0;
    if( p < 0 || p >= static_cast<int>( parts.size() ) || parts[p].removed ) {
        return vpart_id::NULL_ID();
    }

    int displayed_part = -1;

    if( roof ) { displayed_part = roof_at_part( p ); }
    if( displayed_part < 0 || displayed_part >= static_cast<int>( parts.size() )
        || parts[displayed_part].removed ) {
        displayed_part = part_displayed_at( parts[p].mount );
    }
    if( displayed_part < 0 || displayed_part >= static_cast<int>( parts.size() )
        || parts[displayed_part].removed ) {
        return vpart_id::NULL_ID();
    }

    const vpart_id idinfo =
        parts[displayed_part].proxy_part_id == vpart_id::NULL_ID()
        ? parts[displayed_part].id
        : parts[displayed_part].proxy_part_id;

    if( part_flag( displayed_part, VPFLAG_OPENABLE ) && parts[displayed_part].open ) {
        // open
        part_mod = 1;
    } else if( parts[displayed_part].is_broken() ) {
        // broken
        part_mod = 2;
    }

    return idinfo;
}

nc_color vehicle::part_color( const int p, const bool exact ) const
{
    if( p < 0 || p >= static_cast<int>( parts.size() ) ) { return c_black; }

    nc_color col;

    int parm = -1;

    // If armoring is present and the option is set, it colors the visible part
    if( get_option<bool>( "VEHICLE_ARMOR_COLOR" ) ) {
        parm = part_with_feature( p, VPFLAG_ARMOR, false );
    }

    if( parm >= 0 ) {
        col = part_info( parm ).color;
    } else {
        const int displayed_part = exact ? p : part_displayed_at( parts[p].mount );

        if( displayed_part < 0 || displayed_part >= static_cast<int>( parts.size() ) ) {
            return c_black;
        }
        if( parts[displayed_part].blood > 200 ) {
            col = c_red;
        } else if( parts[displayed_part].blood > 0 ) {
            col = c_light_red;
        } else if( parts[displayed_part].is_broken() ) {
            col = part_info( displayed_part ).color_broken;
        } else {
            col = part_info( displayed_part ).color;
        }
    }

    if( exact ) { return col; }

    // curtains turn windshields gray
    int curtains = part_with_feature( p, VPFLAG_CURTAIN, false );
    if( curtains >= 0 ) {
        if( part_with_feature( p, VPFLAG_WINDOW, true ) >= 0 && !parts[curtains].open ) {
            col = part_info( curtains ).color;
        }
    }

    // Invert colors for cargo parts with stuff in them
    int cargo_part = part_with_feature( p, VPFLAG_CARGO, true );
    if( cargo_part > 0 && !get_items( cargo_part ).empty() ) {
        return invert_color( col );
    } else {
        return col;
    }
}


// RmlUi (veh_interact slice 4): the parts-at-tile list as a single colour-tagged
// string, parallel to print_part_list (detail=true path). The curses x-positioned
// symbols + the right-aligned Interior/Exterior marker are inlined, and the scroll
// windowing is dropped for native scroll; the highlighted row gets a "> " prefix.
std::string vehicle::part_list_text( int p, int hl ) const
{
    if( p < 0 || p >= static_cast<int>( parts.size() ) ) { return std::string(); }
    const std::vector<int> pl = this->parts_at_relative( parts[p].mount, true );
    std::string out;

    for( size_t i = 0; i < pl.size(); i++ ) {
        const vehicle_part& vp = parts[pl[i]];
        std::string partname = vp.name();

        if( vp.is_fuel_store() && !vp.ammo_current().is_null() ) {
            if( vp.ammo_current() == itype_battery ) {
                partname +=
                    string_format( _( " (%s/%s charge)" ), vp.ammo_remaining(), vp.ammo_capacity() );
            } else {
                const itype* pt_ammo_cur = &*vp.ammo_current();
                auto stack = units::legacy_volume_factor / pt_ammo_cur->stack_size;
                partname += string_format(
                                _( " (%.1fL %s)" ), round_up( units::to_liter( vp.ammo_remaining() * stack ), 1 ),
                                item::nname( vp.ammo_current() ) );
            }
        }
        if( part_flag( pl[i], "CARGO" ) ) {
            //~ used/total volume of a cargo vehicle part
            partname += string_format(
                            _( " (vol: %s/%s %s)" ), format_volume( stored_volume( pl[i] ) ),
                            format_volume( max_volume( pl[i] ) ), volume_units_abbr() );
        }

        std::string left_sym;
        std::string right_sym;
        if( part_flag( pl[i], "ARMOR" ) ) {
            left_sym = "(";
            right_sym = ")";
        } else if( part_info( pl[i] ).location == part_location_structure ) {
            left_sym = "[";
            right_sym = "]";
        } else {
            left_sym = "-";
            right_sym = "-";
        }

        std::string side;
        if( i == 0 ) {
            side = vpart_position( const_cast<vehicle &>( *this ), pl[i] ).is_inside()
                   ? _( "Interior" )
                   : _( "Exterior" );
        }

        std::string row =
            ( static_cast<int>( i ) == hl ? "> " : "  " ) + left_sym + partname + right_sym;
        if( !side.empty() ) { row += "  " + side; }
        out += colorize( row, c_light_gray ) + "\n";
    }

    const std::optional<std::string> label =
        vpart_position( const_cast<vehicle &>( *this ), p ).get_label();
    if( label ) {
        out += colorize( string_format( _( "Label: %s" ), label.value() ), c_light_red ) + "\n";
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
std::string vehicle::parts_descs_text(
    int max_y, int width, int p, int &start_at, int &start_limit ) const
{
    if( p < 0 || p >= static_cast<int>( parts.size() ) ) { return std::string(); }

    std::vector<int> pl = this->parts_at_relative( parts[p].mount, true );
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
    start_at = std::max( 0, std::min( start_at, start_limit ) );
    if( start_at ) {
        msg += std::string( "<color_yellow>" ) + "<  " + _( "More parts here…" ) + "</color>\n";
        lines += 1;
    }
    for( size_t i = start_at; i < pl.size(); i++ ) {
        const vehicle_part& vp = parts[pl[i]];
        std::string possible_msg;
        const nc_color name_color = vp.is_broken() ? c_dark_gray : c_light_green;
        possible_msg += colorize( vp.name(), name_color ) + "\n";
        const nc_color desc_color = vp.is_broken() ? c_dark_gray : c_light_gray;
        // -4 = -2 for left & right padding + -2 for "> "
        int new_lines = 2 + vp.info().format_description( possible_msg, desc_color, width - 4 );
        if( vp.has_flag( vehicle_part::carrying_flag ) ) {
            possible_msg += "  Carrying a vehicle on a rack.\n";
            new_lines += 1;
        }
        if( vp.has_flag( vehicle_part::carried_flag ) ) {
            possible_msg += string_format( "  Part of a %s carried on a rack.\n", vp.carried_name() );
            new_lines += 1;
        }

        possible_msg += "</color>\n";
        if( lines + new_lines <= max_y ) {
            msg += possible_msg;
            lines += new_lines;
            start_limit = start_at;
        } else {
            msg += std::string( "<color_yellow>" ) + _( "More parts here…" ) + "  >" + "</color>\n";
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
std::vector<itype_id> vehicle::get_printable_fuel_types() const
{
    std::set<itype_id> opts;
    for( const auto& pt : parts ) {
        if( pt.is_fuel_store() && !pt.ammo_current().is_null() ) { opts.emplace( pt.ammo_current() ); }
    }

    std::vector<itype_id> res( opts.begin(), opts.end() );

    std::ranges::sort( res, [&]( const itype_id & lhs, const itype_id & rhs ) {
        return basic_consumption( rhs ) < basic_consumption( lhs );
    } );

    return res;
}

// RmlUi (veh_interact slice 4b): the fuel gauges as colour-tagged text lines,
// parallel to print_fuel_indicators (fullsize/verbose path) + print_fuel_indicator.
// The E…F ASCII gauge bar + the gauge windowing ('>' for more) are dropped; each
// printable fuel becomes one line "<fuel>  NN%" + the verbose rate / ETA suffix.
std::vector<std::string> vehicle::fuel_indicator_lines() const
{
    std::vector<std::string> lines;
    const std::vector<itype_id> fuels = get_printable_fuel_types();
    for( const itype_id& fuel_type : fuels ) {
        const int cap = fuel_capacity( fuel_type );
        const int f_left = fuel_left( fuel_type );
        const nc_color f_color = fuel_type->color;
        const int pct = cap > 0 ? f_left * 100 / cap : 0;
        std::string line = string_format( "%s  %d%%", item::nname( fuel_type ), pct );

        // Verbose rate / time-to-goal (parallels print_fuel_indicator's verbose block).
        int rate = 0;
        std::string units;
        const auto fuel_data = fuel_used_last_turn.find( fuel_type );
        if( fuel_data != fuel_used_last_turn.end() ) {
            rate = -consumption_per_hour( fuel_type, fuel_data->second );
            units = _( "mL" );
        }
        if( fuel_type == itype_id( "battery" ) ) {
            rate += power_to_energy_bat( net_battery_charge_rate_w(), 1_hours );
            units = _( "kJ" );
        }
        if( rate != 0 && cap > 0 ) {
            int tank_use = 0;
            std::string tank_goal = _( "full" );
            bool have_use = true;
            if( rate > 0 ) {
                tank_use = cap - f_left;
                if( !tank_use ) { have_use = false; }
            } else {
                if( !f_left ) { have_use = false; }
                tank_use = f_left;
                tank_goal = _( "empty" );
            }
            if( have_use ) {
                item& fitem = *item::spawn_temporary( fuel_type );
                const int charges_per_L = fitem.charges_per_volume( 1_liter );
                if( charges_per_L != 0 && charges_per_L != item::INFINITE_CHARGES ) {
                    const float charges_per_mL = charges_per_L / 1000.0f;
                    tank_use = tank_use / charges_per_mL;
                    const double turns = to_turns<double>( 60_minutes );
                    const time_duration estimate = time_duration::from_turns(
                                                       turns * tank_use / std::abs( rate ) );
                    if( debug_mode ) {
                        line += string_format(
                                    _( ", %d %s(%4.2f%%)/hour, %s until %s" ), rate, units,
                                    100.0 * rate / cap, to_string_clipped( estimate ), tank_goal );
                    } else {
                        line += string_format(
                                    _( ", %3.1f%% / hour, %s until %s" ), 100.0 * rate / cap,
                                    to_string_clipped( estimate ), tank_goal );
                    }
                }
            }
        }
        lines.emplace_back( colorize( line, f_color ) );
    }
    return lines;
}
