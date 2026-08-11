#include "avatar.h" // IWYU pragma: associated
#include "newcharacter.h" // IWYU pragma: associated

#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <functional>
#include <iosfwd>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "addiction.h"
#include "bionics.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "character.h"
#include "character_martial_arts.h"
#include "color.h"
#include "cursesdef.h"
#include "filesystem.h"
#include "fstream_utils.h"
#include "game.h"
#include "game_constants.h"
#include "ime.h"
#include "input.h"
#include "int_id.h"
#include "inventory.h"
#include "json.h"
#include "lightmap.h"
#include "npc_class.h"
#include "magic.h"
#include "magic_enchantment.h"
#include "make_static.h"
#include "mapsharing.h"
#include "martialarts.h"
#include "monster.h"
#include "mutation.h"
#include "name.h"
#include "options.h"
#include "output.h"
#include "path_info.h"
#include "pimpl.h"
#include "pldata.h"
#include "profession.h"
#include "ranged.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include <RmlUi/Core.h>
#include "newchar_balance.h"
#include "rml_length.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "rng.h"
#include "scenario.h"
#include "sdltiles.h"
#include "skill.h"
#include "start_location.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "translations.h"
#include "type_id.h"
#include "ui.h"
#include "ui_manager.h"
#include "units_utility.h"
#include "veh_type.h"
#include "worldfactory.h"
#include "character_preview.h"


static const flag_id json_flag_no_auto_equip( "no_auto_equip" );
static const flag_id json_flag_auto_wield( "auto_wield" );

static const trait_id trait_XS( "XS" );
static const trait_id trait_XXXL( "XXXL" );

static const trait_flag_str_id flag_MALE_EXCLUSIVE( "MALE_EXCLUSIVE" );
static const trait_flag_str_id flag_FEMALE_EXCLUSIVE( "FEMALE_EXCLUSIVE" );
static const trait_flag_str_id flag_MALE_PREFERRED( "MALE_PREFERRED" );
static const trait_flag_str_id flag_FEMALE_PREFERRED( "FEMALE_PREFERRED" );

static auto can_pick_prof( const profession &prof, const Character &u, int points ) -> bool
{
    return prof.point_cost() - u.prof->point_cost() <= points;
}

struct {
    bool sort_by_points = true;
    bool male = false;
    auto operator()( const profession_id &a, const profession_id &b ) -> bool {
        const profession_id &gen = profession::generic();
        if( b == gen ) {
            return false;
        } else if( a == gen ) {
            return true;
        }
        if( sort_by_points ) {
            return a->point_cost() < b->point_cost();
        } else {
            return localized_compare( a->gender_appropriate_name( male ),
                                      b->gender_appropriate_name( male ) );
        }
    }
} profession_sorter;

struct {
    bool sort_by_points = true;
    bool male = false;
    bool cities_enabled = false;
    auto operator()( const scenario *a, const scenario *b ) -> bool {
        if( cities_enabled ) {
        const scenario *gen = scenario::generic();
            if( b == gen ) {
                return false;
            } else if( a == gen ) {
                return true;
            }
        }
        if( !cities_enabled && a->has_flag( "CITY_START" ) != b->has_flag( "CITY_START" ) ) {
            return a->has_flag( "CITY_START" ) < b->has_flag( "CITY_START" );
        } else if( sort_by_points ) {
        return a->point_cost() < b->point_cost();
        } else {
            return localized_compare( a->gender_appropriate_name( male ),
                                      b->gender_appropriate_name( male ) );
        }
    }
} scenario_sorter;

namespace char_creation
{
enum description_selector {
    NAME,
    HEIGHT,
    AGE
};
} // namespace char_creation


#define COL_STAT_ACT        c_white   // Selected stat
#define COL_STAT_BONUS      c_light_green // Bonus
#define COL_STAT_NEUTRAL    c_white   // Neutral Property
#define COL_STAT_PENALTY    c_light_red   // Penalty
#define COL_TR_GOOD         c_green   // Good trait descriptive text
#define COL_TR_GOOD_OFF_ACT c_light_gray  // A toggled-off good trait
#define COL_TR_GOOD_ON_ACT  c_light_green // A toggled-on good trait
#define COL_TR_GOOD_OFF_PAS c_dark_gray  // A toggled-off good trait
#define COL_TR_GOOD_ON_PAS  c_green   // A toggled-on good trait
#define COL_TR_BAD          c_red     // Bad trait descriptive text
#define COL_TR_BAD_OFF_ACT  c_light_gray  // A toggled-off bad trait
#define COL_TR_BAD_ON_ACT   c_light_red   // A toggled-on bad trait
#define COL_TR_BAD_OFF_PAS  c_dark_gray  // A toggled-off bad trait
#define COL_TR_BAD_ON_PAS   c_red     // A toggled-on bad trait
#define COL_TR_NEUT         c_brown     // Neutral trait descriptive text
#define COL_TR_NEUT_OFF_ACT c_dark_gray  // A toggled-off neutral trait
#define COL_TR_NEUT_ON_ACT  c_yellow   // A toggled-on neutral trait
#define COL_TR_NEUT_OFF_PAS c_dark_gray  // A toggled-off neutral trait
#define COL_TR_NEUT_ON_PAS  c_brown     // A toggled-on neutral trait
#define COL_SKILL_USED      c_green   // A skill with at least one point
#define COL_HEADER          c_white   // Captions, like "Profession items"

static auto profession_age_limits_enabled() -> bool
{
    if( world_generator && world_generator->active_world ) {
    return world_generator->active_world->info->WORLD_OPTIONS["ENFORCE_PROFESSION_AGE_RANGE"]
               .value_as<bool>();
    }
    return false;
}

static auto profession_age_bounds( const profession &prof ) -> std::pair<int, int>
{
    if( profession_age_limits_enabled() ) {
    if( const auto range = prof.starting_age_range() ) {
            return { range->min, range->max };
        }
    }
    return { profession::min_age, profession::max_age };
}

static auto random_age_for_profession( const profession &prof ) -> int
{
    const auto [min_age, max_age] = profession_age_bounds( prof );
    if( min_age == max_age ) {
        return min_age;
    }
    return rng( min_age, max_age );
}

// Colors used in this file: (Most else defaults to c_light_gray)
#define COL_STAT_ACT        c_white   // Selected stat
#define COL_STAT_BONUS      c_light_green // Bonus
#define COL_STAT_NEUTRAL    c_white   // Neutral Property
#define COL_STAT_PENALTY    c_light_red   // Penalty
#define COL_TR_GOOD         c_green   // Good trait descriptive text
#define COL_TR_GOOD_OFF_ACT c_light_gray  // A toggled-off good trait
#define COL_TR_GOOD_ON_ACT  c_light_green // A toggled-on good trait
#define COL_TR_GOOD_OFF_PAS c_dark_gray  // A toggled-off good trait
#define COL_TR_GOOD_ON_PAS  c_green   // A toggled-on good trait
#define COL_TR_BAD          c_red     // Bad trait descriptive text
#define COL_TR_BAD_OFF_ACT  c_light_gray  // A toggled-off bad trait
#define COL_TR_BAD_ON_ACT   c_light_red   // A toggled-on bad trait
#define COL_TR_BAD_OFF_PAS  c_dark_gray  // A toggled-off bad trait
#define COL_TR_BAD_ON_PAS   c_red     // A toggled-on bad trait
#define COL_TR_NEUT         c_brown     // Neutral trait descriptive text
#define COL_TR_NEUT_OFF_ACT c_dark_gray  // A toggled-off neutral trait
#define COL_TR_NEUT_ON_ACT  c_yellow   // A toggled-on neutral trait
#define COL_TR_NEUT_OFF_PAS c_dark_gray  // A toggled-off neutral trait
#define COL_TR_NEUT_ON_PAS  c_brown     // A toggled-on neutral trait
#define COL_SKILL_USED      c_green   // A skill with at least one point
#define COL_HEADER          c_white   // Captions, like "Profession items"

enum {
    HIGH_STAT = 14 // The point after which stats cost double
};

enum {
    NEWCHAR_TAB_MAX = 7 // The ID of the rightmost tab
};

/// `label` plus its trailing space, as ONE colour run.
///
/// The space MUST sit INSIDE the colour tag. cata_text_to_rml emits one `<span>`
/// per colour run, and RmlUi trims a lone U+0020 that ends up BETWEEN two spans at
/// parse time — so `colorize( "Name:" ) + " " + colorize( value )` rendered welded
/// as `Name:Lon 'Electric' Woody`, and likewise for Height/Age/Starting location.
/// `Scenario: ` and `Profession: ` never showed the bug because they already
/// carried the space inside the tag; this makes the rest match them.
///
/// The translated string is NOT changed, so no PO churn.
auto nc_label( const std::string &label, const nc_color &col ) -> std::string
{
    return colorize( label + " ", col );
}

/// Placeholder section/tab glyph as an RCSS `decorator` value.
///
/// `?proc:runic-icon:<size>:<seed>:<hex>` is generated in C++ (see
/// rmlui_render_interface.cpp -> gen_runic_frame) rather than loaded from disk, so
/// these need no art to ship and are deterministic per seed. Real art replaces the
/// decorator one row at a time — the seed/size/intent table lives in
/// plans/charcreation-visual-overhaul.md.
///
/// Two golds only, matching the main-menu nav rail: active vs inactive.
auto nc_icon_dec( unsigned seed, int size, bool active ) -> std::string
{
    return string_format( "image( ?proc:runic-icon:%d:%u:%s none contain ) border-box",
           size, seed, active ? "c4a832" : "a1885f" );
}

/// Deterministic seeds for the eight tab glyphs, in tab order (POINTS .. OVERVIEW).
/// Stable by contract: changing one reshuffles a placeholder the artist is matching.
constexpr unsigned NC_TAB_ICON_SEEDS[8] = {
    0x5031, 0x5343, 0x5052, 0x5354, 0x5452, 0x4249, 0x534b, 0x4f56,
};

/// Labelled points budget for the RmlUi tabs.
///
/// `points_left::to_string()` renders MULTI_POOL as a bare formula — `Points left:
/// 1-1+0=0` — which is unreadable at a glance: three sub-pools with no names and the
/// arithmetic exposed. Same numbers, named, same red-when-overspent colouring. The
/// other pool modes already read fine, so they fall through unchanged.
///
/// `points_left::to_string()` itself is left alone: the curses path still uses it
/// for the A/B comparison.
///
/// Gaps live INSIDE a colour run that also has visible text. A run containing only
/// spaces is dropped entirely, and a lone U+0020 between two runs is trimmed at
/// parse time; a trailing space on a run with content survives (proven by nc_label).
auto nc_points_line( points_left &pts ) -> std::string
{
    if( pts.limit != points_left::MULTI_POOL ) {
        return pts.to_string();
    }
    const auto part = [&]( const std::string & label, int v, bool ok ) -> std::string {
        return colorize( label + " ", c_dark_gray ) +
        colorize( string_format( "%d    ", v ), ok ? c_light_gray : c_red );
    };
    std::string s = colorize( std::string( _( "Points left" ) ) + ":    ", c_dark_gray );
    s += part( _( "stats" ), pts.stat_points, pts.stat_points_left() >= 0 );
    s += part( _( "traits" ), pts.trait_points, pts.trait_points_left() >= 0 );
    s += part( _( "skills" ), pts.skill_points, pts.skill_points_left() >= 0 );
    s += colorize( std::string( _( "total" ) ) + " ", c_dark_gray );
    s += colorize( string_format( "%d", pts.stat_points + pts.trait_points + pts.skill_points ),
                   pts.is_valid() ? c_white : c_red );
    return s;
}

/// Everything the tipping trait/bionic balance scale needs, derived from the
/// good/bad point counters.
///
/// Replaces the old `budget_rml` formula ("7/12 0/-12"), which stated the numbers
/// without conveying the thing that matters — whether you are leaning on advantages
/// or disadvantages.
///
/// The beam rotates; the pans do NOT ride it. If the pans were children of the beam
/// they would inherit its rotation and the numbers would render tilted, so each pan
/// gets its own vertical offset computed from the same balance value. `num_bad` is
/// accumulated as a NEGATIVE total, so its magnitude is the weight on the bad pan.
struct nc_balance {
    bool show = false;
    // Valid neutral defaults, not empty strings: these feed data-style-transform /
    // data-style-top, which RmlUi applies on the first frame BEFORE sync_rml() has
    // populated the model. Empty produced `Syntax error parsing inline property
    // declaration 'transform: ;'` in debug.log (SDL debug class) on every open.
    Rml::String rotate = "rotate(0deg)";   //< beam angle
    Rml::String good_top = "0dp";          //< good pan offset (positive sinks)
    Rml::String bad_top = "0dp";
    Rml::String good_rml;
    Rml::String bad_rml;
    Rml::String good_icon;
    Rml::String bad_icon;
    Rml::String fulcrum_icon;
};

auto nc_make_balance( int num_good, int num_bad, int maxp, bool freeform ) -> nc_balance
{
    nc_balance b;
    // Freeform has no budget, so there is nothing to balance.
    b.show = !freeform;
    if( !b.show ) {
        return b;
    }
    const nc_scale::geometry g = nc_scale::compute( num_good, num_bad, maxp );
    // Tilt and pan travel both come from nc_scale::compute so they cannot disagree in
    // sign or magnitude; see src/newchar_balance.h for why that is derived rather
    // than hand-matched here.
    b.rotate = string_format( "rotate(%.2fdeg)", g.tilt_deg );
    b.good_top = rml::dp( g.good_dp );
    b.bad_top = rml::dp( g.bad_dp );
    const int cap = std::max( 1, maxp );
    const int good_w = num_good;
    const int bad_w = std::abs( num_bad );
    b.good_rml = cata_text_to_rml( colorize( string_format( "%d/%d", good_w, cap ), c_light_green ) );
    b.bad_rml = cata_text_to_rml( colorize( string_format( "%d/-%d", num_bad, cap ), c_light_red ) );
    b.good_icon = nc_icon_dec( 0x4744, 20, good_w > 0 );
    b.bad_icon = nc_icon_dec( 0x4244, 20, bad_w > 0 );
    b.fulcrum_icon = nc_icon_dec( 0x464c, 16, true );
    return b;
}

/// Width of `.nc-panel` in the creator stylesheets, as a fraction. Must match the
/// `width:` in data/gui/newchar*.rcss — the preview's whole reason for being visible
/// is that it sits outside this panel.
constexpr int nc_panel_pct = 72;

/// Rightmost column the centred creator panel paints, for a given terminal width.
/// A centred panel of width W has its right edge at (100 + W) / 2.
constexpr auto nc_panel_right_col( int termx ) -> int
{
    return ( 100 + nc_panel_pct ) * termx / 200;
}

/// Shared preview-box geometry for every creator tab that shows one.
///
/// Each tab used to compute its own ncols/nlines, so the box landed somewhere
/// different on each — and because the avatar is a GPU sprite drawn UNDERNEATH the
/// RmlUi document, any opaque panel overlapping the box hides it. TRAITS and BIONICS
/// only worked by luck (their 80% panel's right edge cleared the box by 0.2% of the
/// screen); OVERVIEW's 92% panel covered it entirely and PROFESSION's wider box
/// reached back under its panel.
///
/// One geometry for all four, pinned hard right so the box sits in the strip the
/// panel leaves clear.
///
/// The hide threshold is DERIVED, not a constant. The box is a fixed number of
/// *cells* wide (character_preview_window::prepare sizes it from the tile pixel size,
/// not from the requested ncols), while the panel is a *percentage* — so as the
/// terminal narrows the box occupies a larger fraction and eventually slides back
/// under the panel. A hard-coded threshold only holds for one box size: at the
/// measured 20 cells a constant 150 is safe at every terminal size, but from 24 cells
/// up (a larger tileset, or a smaller font making a tile span more cells) it silently
/// lets the panel cover the preview again. Comparing against the panel edge instead
/// keeps the invariant true for any tileset and font.
void nc_prepare_preview( character_preview_window &pv )
{
    const int ncols = std::max( 10, TERMX * 13 / 100 );
    const int nlines = std::max( 7, TERMY * 22 / 100 );
    constexpr auto orient = character_preview_window::Orientation{
        character_preview_window::TOP_RIGHT,
        character_preview_window::Margin{ 0, 1, 4, 0 }
    };
    // prepare() hides the preview when TERMX - box_ncols < hide_below, so passing the
    // panel's right edge (plus a column of slack) drops it exactly when it would start
    // to collide, rather than at an unrelated fixed width.
    pv.prepare( nlines, ncols, &orient, nc_panel_right_col( TERMX ) + 3 );
}

int skill_increment_cost( const Character &u, const skill_id &skill );

enum struct tab_direction {
    NONE,
    FORWARD,
    BACKWARD,
    QUIT
};

tab_direction set_points( avatar &u, points_left &points );
tab_direction set_stats( avatar &u, points_left &points );
tab_direction set_traits( avatar &u, points_left &points );
tab_direction set_bionics( avatar &u, points_left &points );
tab_direction set_scenario( avatar &u, points_left &points, tab_direction direction );
tab_direction set_profession( avatar &u, points_left &points, tab_direction direction );
tab_direction set_skills( avatar &u, points_left &points );
tab_direction set_description( avatar &you, bool allow_reroll, points_left &points );

static std::optional<std::string> query_for_template_name();
void reset_scenario( avatar &u, const scenario *scen );

namespace
{
/// The shell around every creator step: the two navigators and the exit line.
///
/// Plain scalars rather than a registered struct — a struct would have to be
/// registered against eight different data models, and `c.Bind` on scalars needs no
/// type registration at all.
struct nc_shell {
    bool has_prev = false;
    bool has_next = false;
    Rml::String prev_key_rml;
    Rml::String prev_name_rml;
    Rml::String next_key_rml;
    Rml::String next_name_rml;
    Rml::String exit_rml;
};

struct nc_rml_tab {
    Rml::String name_rml;
    Rml::String icon_dec;   //< placeholder tab glyph
    bool selected = false;
    bool done = false;   //< step already passed
};
/// One point-pool card. The pool choice is the first thing creation asks, so it gets
/// cards rather than a list row: an art slot on top (placeholder rune until real art
/// arrives) and an info slot beneath carrying the name and what the pool means.
struct nc_points_opt {
    Rml::String name_rml;
    Rml::String info_rml;   //< the pool's explanation, shown in the card's lower slot
    Rml::String rune_dec;   //< placeholder art for the card's upper slot
    bool selected = false;  //< cursor is on this card
    bool chosen = false;    //< this is the pool actually in force
};
struct nc_points_session {
    Rml::Vector<nc_rml_tab> tabs;
    nc_shell shell;
    Rml::String points_rml;
    Rml::Vector<nc_points_opt> opts;
    Rml::DataModelHandle handle;
};

bool g_nc_points_types_registered = false;

void register_nc_points_rml_types( Rml::DataModelConstructor &c )
{
    if( g_nc_points_types_registered ) {
        return;
    }
    Rml::StructHandle<nc_rml_tab> th = c.RegisterStruct<nc_rml_tab>();
    th.RegisterMember( "name_rml", &nc_rml_tab::name_rml );
    th.RegisterMember( "icon_dec", &nc_rml_tab::icon_dec );
    th.RegisterMember( "selected", &nc_rml_tab::selected );
    th.RegisterMember( "done", &nc_rml_tab::done );
    c.RegisterArray<Rml::Vector<nc_rml_tab>>();
    Rml::StructHandle<nc_points_opt> oh = c.RegisterStruct<nc_points_opt>();
    oh.RegisterMember( "name_rml", &nc_points_opt::name_rml );
    oh.RegisterMember( "info_rml", &nc_points_opt::info_rml );
    oh.RegisterMember( "rune_dec", &nc_points_opt::rune_dec );
    oh.RegisterMember( "selected", &nc_points_opt::selected );
    oh.RegisterMember( "chosen", &nc_points_opt::chosen );
    c.RegisterArray<Rml::Vector<nc_points_opt>>();
    g_nc_points_types_registered = true;
}

/// The eight creation steps, in order. Index 0 is POINTS.
inline auto nc_step_captions() -> const std::vector<std::string> &
{
    static const std::vector<std::string> caps = {
        _( "POINTS" ), _( "SCENARIO" ), _( "PROFESSION" ), _( "STATS" ),
        _( "TRAITS" ), _( "BIONICS" ), _( "SKILLS" ), _( "OVERVIEW" ),
    };
    return caps;
}

// Builds the wizard step rail. `active` is the index of the current step. These are
// STEPS IN A PROCESS, not tabs: each carries `done` (already passed) alongside
// `selected` (current), and the stylesheet gives the three states distinct
// brightness so the rail shows how far along you are.
//
// Templated on the step struct so each tab's data-model uses its OWN registered C++
// type (RegisterStruct is context-global — distinct types avoid re-registering one
// type on two models; the worldfactory precedent). Every step struct has
// {name_rml, icon_dec, selected, done}.
template<typename TabT>
Rml::Vector<TabT> build_nc_char_tabs( int active )
{
    const std::vector<std::string> &caps = nc_step_captions();
    Rml::Vector<TabT> tabs;
    for( int i = 0; i < static_cast<int>( caps.size() ); i++ ) {
        TabT t;
        t.name_rml = cata_text_to_rml( caps[i] );
        t.selected = ( i == active );
        t.done = ( i < active );
        t.icon_dec = nc_icon_dec( NC_TAB_ICON_SEEDS[i], 24, t.selected || t.done );
        tabs.push_back( t );
    }
    return tabs;
}

/// Shortcut labels come from the REAL bindings via `input_context::get_desc`, not
/// from hard-coded letters: the reference art shows `[Q]`/`[E]`, but this creator
/// binds PREV_TAB/NEXT_TAB (TAB/BACKTAB by default) and those are rebindable and
/// translated. Printing an invented key would be a lie the moment anyone rebinds.
auto fill_nc_shell( int active, const input_context &ctxt ) -> nc_shell
{
    const std::vector<std::string> &caps = nc_step_captions();
    const int last = static_cast<int>( caps.size() ) - 1;
    nc_shell s;
    s.has_prev = active > 0;
    s.has_next = active < last;
    if( s.has_prev ) {
        s.prev_key_rml = cata_text_to_rml(
                             colorize( string_format( "[%s]", ctxt.get_desc( "PREV_TAB" ) ), c_yellow ) );
        s.prev_name_rml = cata_text_to_rml( colorize( caps[active - 1], c_light_gray ) );
    }
    if( s.has_next ) {
        s.next_key_rml = cata_text_to_rml(
                             colorize( string_format( "[%s]", ctxt.get_desc( "NEXT_TAB" ) ), c_yellow ) );
        s.next_name_rml = cata_text_to_rml( colorize( caps[active + 1], c_light_gray ) );
    }
    // On the FIRST step PREV_TAB is what leaves creation, so name that instead of a
    // step; the loops already treat PREV_TAB and QUIT identically there.
    // max_limit 1: QUIT lists four bindings ("ESC, q, Q or SPACE"), which swamps a
    // one-line footer. One key is enough to teach the gesture.
    const std::string exit_key = active > 0 ? ctxt.get_desc( "QUIT", 1 )
                                 : ctxt.get_desc( "PREV_TAB", 1 );
    // The gap after the key sits INSIDE the yellow run. A bare space between two
    // coloured spans is trimmed at parse time (it rendered "[BACKTAB]main menu"), and a
    // run whose content is only whitespace is dropped entirely; a trailing space on a
    // run that has text survives. Same rule as nc_label().
    s.exit_rml = cata_text_to_rml( string_format( ":: %s%s ::",
                                   colorize( string_format( "[%s] ", exit_key ), c_yellow ),
                                   colorize( _( "main menu" ), c_dark_gray ) ) );
    return s;
}

/// Binds the shell's scalars. Mirrors the field names the shared markup expects.
void bind_nc_shell( Rml::DataModelConstructor &c, nc_shell &s )
{
    c.Bind( "has_prev", &s.has_prev );
    c.Bind( "has_next", &s.has_next );
    c.Bind( "prev_key_rml", &s.prev_key_rml );
    c.Bind( "prev_name_rml", &s.prev_name_rml );
    c.Bind( "next_key_rml", &s.next_key_rml );
    c.Bind( "next_name_rml", &s.next_name_rml );
    c.Bind( "exit_rml", &s.exit_rml );
}

/// Marks the shell's variables dirty. Called from each step's sync_rml.
void dirty_nc_shell( Rml::DataModelHandle &h )
{
    h.DirtyVariable( "has_prev" );
    h.DirtyVariable( "has_next" );
    h.DirtyVariable( "prev_key_rml" );
    h.DirtyVariable( "prev_name_rml" );
    h.DirtyVariable( "next_key_rml" );
    h.DirtyVariable( "next_name_rml" );
    h.DirtyVariable( "exit_rml" );
}
} // namespace

bool &newcharacter_rmlui_enabled()
{
    static bool enabled = true;
    return enabled;
}

tab_direction set_points( avatar &, points_left &points )
{
    tab_direction retval = tab_direction::NONE;

    ui_adaptor ui;
    catacurses::window w;
    catacurses::window w_description;
    const auto init_windows = [&]( ui_adaptor & ui ) {
        w = catacurses::newwin( TERMY, TERMX, point_zero );
        w_description = catacurses::newwin( TERMY - 6, TERMX - 35, point( 31, 5 ) );
        ui.position_from_window( w );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    input_context ctxt( "NEW_CHAR_POINTS" );
    ctxt.register_cardinal();
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "QUIT" );
    // Required for the navigator/card clicks to reach this loop at all.
    // MOUSE_LEFT binds to action id SELECT (keybindings.json:1175). An UNREGISTERED
    // mouse action resolves to CATA_ERROR, and input.cpp:894-897 `continue`s on it
    // BEFORE the registered_any_input check at :912 — so ANY_INPUT cannot rescue a
    // mouse event, only registering the action it maps to can. Without this the
    // click callback fires but handle_input() never returns, leaving the loop parked
    // until an unrelated keypress, which then got hijacked into a step change.
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "CONFIRM" );

    const std::string point_pool = get_option<std::string>( "CHARACTER_POINT_POOLS" );

    using point_limit_tuple = std::tuple<points_left::point_limit, std::string, std::string>;
    std::vector<point_limit_tuple> opts;

    const point_limit_tuple multi_pool = std::make_tuple( points_left::MULTI_POOL,
                                         _( "Multiple pools" ),
                                         _( "Stats, traits and skills have separate point pools.\n"
                                            "Putting stat points into traits and skills is allowed and putting trait points into skills is allowed.\n"
                                            "Scenarios and professions affect skill point pool." ) );

    const point_limit_tuple one_pool = std::make_tuple( points_left::ONE_POOL, _( "Single pool" ),
                                       _( "Stats, traits and skills share a single point pool." ) );

    const point_limit_tuple freeform = std::make_tuple( points_left::FREEFORM, _( "Freeform" ),
                                       _( "No point limits are enforced." ) );

    if( point_pool == "multi_pool" ) {
        opts = {{ multi_pool }};
    } else if( point_pool == "no_freeform" ) {
        opts = {{ multi_pool, one_pool }};
    } else {
        opts = {{ multi_pool, one_pool, freeform }};
    }

    int highlighted = 0;

    // RmlUi render path (render-only; keyboard still owns nav/confirm below).
    auto data = std::make_unique<nc_points_session>();
    rml_doc rml;
    // Set by the arrow click callbacks, consumed by the input loop below. Not a
    // tab_direction: it is translated into an action string so the existing
    // keyboard handling stays the single place navigation is decided.
    int nc_nav = 0;
    const auto sync_rml = [&]() {
        if( !data->handle ) {
            return;
        }
        const int sel = std::max( 0, std::min( highlighted,
                                               static_cast<int>( opts.size() ) - 1 ) );
        data->tabs = build_nc_char_tabs<nc_rml_tab>( 0 );  // POINTS tab active
        data->shell = fill_nc_shell( 0, ctxt );
        data->points_rml = cata_text_to_rml( nc_points_line( points ) );
        data->opts.clear();
        // Seeds are arbitrary but STABLE: each pool keeps its own glyph across runs, so
        // the shape becomes recognisable. Replace rune_dec with image(...) when art
        // lands — see plans/charcreation-wizard-flow.md.
        static constexpr unsigned POOL_RUNE_SEEDS[3] = { 0x504F, 0x4F4C, 0x4650 };
        for( int i = 0; i < static_cast<int>( opts.size() ); i++ ) {
            nc_points_opt o;
            o.chosen = ( points.limit == std::get<0>( opts[i] ) );
            // c_light_green, not COL_SKILL_USED (c_green): on the card's dark fill a
            // dark green measured only 2.55:1, under the 3:1 large-text floor. Same
            // meaning ("this pool is the one in force"), one luminance step up.
            o.name_rml = cata_text_to_rml( colorize( std::get<1>( opts[i] ),
                                           o.chosen ? c_light_green : c_light_gray ) );
            o.info_rml = cata_text_to_rml( colorize( std::get<2>( opts[i] ), c_light_gray ) );
            o.selected = ( sel == i );
            // Art brightens for the focused card as well as the chosen one — at card
            // scale the rune is the largest thing on it, so it has to carry the cursor.
            o.rune_dec = nc_icon_dec( POOL_RUNE_SEEDS[i % 3], 96, o.selected || o.chosen );
            data->opts.push_back( o );
        }
        data->handle.DirtyVariable( "tabs" );
        dirty_nc_shell( data->handle );
        data->handle.DirtyVariable( "points_rml" );
        data->handle.DirtyVariable( "opts" );
    };

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    rml.open( newcharacter_rmlui_enabled(), "newcharpoints", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        register_nc_points_rml_types( c );
        c.Bind( "tabs", &data->tabs );
        bind_nc_shell( c, data->shell );
        // Arrow clicks are translated into the SAME action strings the keyboard
        // produces, so each step's existing PREV_TAB/NEXT_TAB handling — including
        // the "Return to main menu?" confirm on step 0 — is reused unchanged.
        c.BindEventCallback( "on_prev",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = -1; } );
        c.BindEventCallback( "on_next",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = 1; } );
        c.Bind( "points_rml", &data->points_rml );
        c.Bind( "opts", &data->opts );
        // Clicking a card both moves the cursor to it and chooses it, which is the whole
        // point of a card over a list row: one gesture instead of arrow-then-Enter.
        // Requires SELECT to be registered above, or the click never reaches this loop.
        c.BindEventCallback( "on_card",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) {
                args[0].GetInto( idx );
            }
            if( idx >= 0 && idx < static_cast<int>( opts.size() ) ) {
                highlighted = idx;
                points.limit = std::get<0>( opts[idx] );
            }
        } );
        data->handle = c.GetModelHandle();
    } );
    if( rml_doc_unavailable( rml, _( "Character creation (POINTS tab)" ) ) ) {
        return tab_direction::QUIT;
    }

    do {
        if( highlighted < 0 ) {
            highlighted = opts.size() - 1;
        } else if( highlighted >= static_cast<int>( opts.size() ) ) {
            highlighted = 0;
        }
        ui_manager::redraw();
        nc_nav = 0;
        std::string action = ctxt.handle_input();
        if( nc_nav != 0 ) {
            action = nc_nav < 0 ? "PREV_TAB" : "NEXT_TAB";
        }
        if( action == "DOWN" ) {
            highlighted++;
        } else if( action == "UP" ) {
            highlighted--;
        } else if( action == "PREV_TAB" && query_yn( _( "Return to main menu?" ) ) ) {
            retval = tab_direction::BACKWARD;
        } else if( action == "NEXT_TAB" ) {
            retval = tab_direction::FORWARD;
        } else if( action == "QUIT" && query_yn( _( "Return to main menu?" ) ) ) {
            retval = tab_direction::QUIT;
        } else if( action == "CONFIRM" ) {
            const auto &cur_opt = opts[highlighted];
            points.limit = std::get<0>( cur_opt );
        }
    } while( retval == tab_direction::NONE );

    return retval;
}

namespace
{
struct nc_stats_tab {
    Rml::String name_rml;
    Rml::String icon_dec;   //< placeholder tab glyph
    bool selected = false;
    bool done = false;   //< step already passed
};
struct nc_stat_row {
    Rml::String name_rml;
    Rml::String val_rml;
    bool selected = false;
};
struct nc_stats_session {
    Rml::Vector<nc_stats_tab> tabs;
    nc_shell shell;
    Rml::String points_rml;
    Rml::Vector<nc_stat_row> stats;
    Rml::String cost_rml;   // red "Increasing X further costs 2 points." or empty
    Rml::String desc_rml;   // selected stat's effects + blurb
    Rml::String hints_rml;
    Rml::DataModelHandle handle;
};

bool g_nc_stats_types_registered = false;

void register_nc_stats_rml_types( Rml::DataModelConstructor &c )
{
    if( g_nc_stats_types_registered ) {
        return;
    }
    Rml::StructHandle<nc_stats_tab> th = c.RegisterStruct<nc_stats_tab>();
    th.RegisterMember( "name_rml", &nc_stats_tab::name_rml );
    th.RegisterMember( "icon_dec", &nc_stats_tab::icon_dec );
    th.RegisterMember( "selected", &nc_stats_tab::selected );
    th.RegisterMember( "done", &nc_stats_tab::done );
    c.RegisterArray<Rml::Vector<nc_stats_tab>>();
    Rml::StructHandle<nc_stat_row> sh = c.RegisterStruct<nc_stat_row>();
    sh.RegisterMember( "name_rml", &nc_stat_row::name_rml );
    sh.RegisterMember( "val_rml", &nc_stat_row::val_rml );
    sh.RegisterMember( "selected", &nc_stat_row::selected );
    c.RegisterArray<Rml::Vector<nc_stat_row>>();
    g_nc_stats_types_registered = true;
}

// Build the selected stat's effects + blurb as one colour-tagged string (mirrors
// the per-stat curses block in set_stats' on_redraw). `u` is mutated as the
// curses path does (recalc_hp for Str).
std::string nc_stat_desc( avatar &u, int sel )
{
    std::vector<std::string> lines;
    switch( sel ) {
        case 1:
            u.recalc_hp();
            lines.push_back( colorize( string_format( _( "Base HP: %d" ),
                                       u.get_part_hp_max( bodypart_id( "head" ) ) ), COL_STAT_NEUTRAL ) );
            lines.push_back( colorize( string_format( _( "Carry weight: %.1f %s" ),
                                       convert_weight( u.weight_capacity() ), weight_units() ), COL_STAT_NEUTRAL ) );
            lines.push_back( colorize( string_format( _( "Melee damage bonus: +%.1f" ),
                                       u.bonus_damage( false ) ), COL_STAT_BONUS ) );
            lines.emplace_back();
            lines.push_back( colorize(
                                 _( "Strength also makes you more resistant to many diseases and poisons, and makes actions which require brute force more effective." ),
                                 COL_STAT_NEUTRAL ) );
            break;
        case 2:
            lines.push_back( colorize( string_format( _( "Melee to-hit bonus: +%.2f" ),
                                       u.get_hit_base() ), COL_STAT_BONUS ) );
            lines.push_back( colorize( string_format( _( "Throwing penalty per target's dodge: +%d" ),
                                       ranged::throw_dispersion_per_dodge( u, false ) ), COL_STAT_BONUS ) );
            if( u.ranged_dex_mod() != 0 ) {
                lines.push_back( colorize( string_format( _( "Ranged penalty: -%d" ),
                                           std::abs( u.ranged_dex_mod() ) ), COL_STAT_PENALTY ) );
            }
            lines.emplace_back();
            lines.push_back( colorize( _( "Dexterity also enhances many actions which require finesse." ),
                                       COL_STAT_NEUTRAL ) );
            break;
        case 3: {
            const int read_spd = u.read_speed( false );
            lines.push_back( colorize( string_format( _( "Read times: %d%%" ), read_spd ),
                                       ( read_spd == 100 ? COL_STAT_NEUTRAL :
                                         ( read_spd < 100 ? COL_STAT_BONUS : COL_STAT_PENALTY ) ) ) );
            lines.push_back( colorize( string_format( _( "Skill rust: %d%%" ), u.rust_rate() ),
                                       COL_STAT_PENALTY ) );
            lines.push_back( colorize( string_format( _( "Crafting bonus: +%d%%" ), u.get_int() ),
                                       COL_STAT_BONUS ) );
            lines.emplace_back();
            lines.push_back( colorize(
                                 _( "Intelligence is also used when crafting, installing bionics, and interacting with NPCs." ),
                                 COL_STAT_NEUTRAL ) );
            break;
        }
        case 4:
            if( u.ranged_per_mod() > 0 ) {
                lines.push_back( colorize( string_format( _( "Aiming penalty: -%d" ),
                                           u.ranged_per_mod() ), COL_STAT_PENALTY ) );
            }
            lines.push_back( colorize( string_format( _( "Night vision bonus: +%.1f" ),
                                       vision::nv_range_from_per( u.per_max ) ), COL_STAT_BONUS ) );
            lines.emplace_back();
            lines.push_back( colorize(
                                 _( "Perception is also used for detecting traps and other things of interest." ),
                                 COL_STAT_NEUTRAL ) );
            break;
    }
    return join( lines, "\n" );
}
} // namespace

tab_direction set_stats( avatar &u, points_left &points )
{
    const int max_stat_points = points.is_freeform() ? 20 : MAX_STAT;

    unsigned char sel = 1;
    const int iSecondColumn = std::max( 27, utf8_width( points.to_string(), true ) + 9 );
    input_context ctxt( "NEW_CHAR_STATS" );
    ctxt.register_cardinal();
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "RANDOMIZE" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "QUIT" );
    // Required for the navigator/card clicks to reach this loop at all.
    // MOUSE_LEFT binds to action id SELECT (keybindings.json:1175). An UNREGISTERED
    // mouse action resolves to CATA_ERROR, and input.cpp:894-897 `continue`s on it
    // BEFORE the registered_any_input check at :912 — so ANY_INPUT cannot rescue a
    // mouse event, only registering the action it maps to can. Without this the
    // click callback fires but handle_input() never returns, leaving the loop parked
    // until an unrelated keypress, which then got hijacked into a step change.
    ctxt.register_action( "SELECT" );

    ui_adaptor ui;
    catacurses::window w;
    catacurses::window w_description;
    const auto init_windows = [&]( ui_adaptor & ui ) {
        w = catacurses::newwin( TERMY, TERMX, point_zero );
        w_description = catacurses::newwin( 8, TERMX - iSecondColumn - 1,
                                            point( iSecondColumn, 6 ) );
        ui.position_from_window( w );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    // There is no map loaded currently, so any access to the map will
    // fail (player::suffer, called from player::reset_stats), might access
    // the map:
    // There are traits that check/change the radioactivity on the map,
    // that check if in sunlight...
    // Setting the position to -1 ensures that the INBOUNDS check in
    // map.cpp is triggered. This check prevents access to invalid position
    // on the map (like -1,0) and instead returns a dummy default value.
    auto old_pos = u.bub_pos();
    old_pos.x() = -1;
    u.setpos( old_pos );
    u.reset();
    // set position back to 0 to prevent out-of-bound access to lightmap
    // array in map::build_seen_cache()
    old_pos.x() = 0;
    u.setpos( old_pos );

    // RmlUi render path (render-only; keyboard still owns nav/inc/dec below).
    auto data = std::make_unique<nc_stats_session>();
    rml_doc rml;
    // Set by the arrow click callbacks, consumed by the input loop below. Not a
    // tab_direction: it is translated into an action string so the existing
    // keyboard handling stays the single place navigation is decided.
    int nc_nav = 0;
    const auto sync_rml = [&]() {
        if( !data->handle ) {
            return;
        }
        data->tabs = build_nc_char_tabs<nc_stats_tab>( 3 );  // STATS tab active
        data->shell = fill_nc_shell( 3, ctxt );
        data->points_rml = cata_text_to_rml( nc_points_line( points ) );

        data->stats.clear();
        const auto add_stat = [&]( const std::string & label, int val, int idx ) {
            nc_stat_row r;
            const bool active = ( sel == idx );
            const nc_color col = active ? COL_STAT_ACT : c_light_gray;
            r.name_rml = cata_text_to_rml( colorize( label, col ) );
            r.val_rml = cata_text_to_rml( colorize( string_format( "%2d", val ), col ) );
            r.selected = active;
            data->stats.push_back( r );
        };
        add_stat( _( "Strength:" ), u.str_max, 1 );
        add_stat( _( "Dexterity:" ), u.dex_max, 2 );
        add_stat( _( "Intelligence:" ), u.int_max, 3 );
        add_stat( _( "Perception:" ), u.per_max, 4 );

        // HIGH_STAT cost warning for the selected stat (exact curses strings).
        std::string cost;
        if( sel == 1 && u.str_max >= HIGH_STAT ) {
            cost = _( "Increasing Str further costs 2 points." );
        } else if( sel == 2 && u.dex_max >= HIGH_STAT ) {
            cost = _( "Increasing Dex further costs 2 points." );
        } else if( sel == 3 && u.int_max >= HIGH_STAT ) {
            cost = _( "Increasing Int further costs 2 points." );
        } else if( sel == 4 && u.per_max >= HIGH_STAT ) {
            cost = _( "Increasing Per further costs 2 points." );
        }
        data->cost_rml = cost.empty() ? std::string()
                         : cata_text_to_rml( colorize( cost, c_light_red ) );

        data->desc_rml = cata_text_to_rml( nc_stat_desc( u, sel ) );

        data->hints_rml = cata_text_to_rml( string_format(
                                                _( "<color_light_green>%s</color> / <color_light_green>%s</color> to select a statistic.\n"
                                                    "<color_light_green>%s</color> to increase the statistic.\n"
                                                    "<color_light_green>%s</color> to decrease the statistic.\n"
                                                    "\n"
                                                    "<color_light_green>%s</color> lets you view and alter keybindings.\n"
                                                    "<color_light_green>%s</color> takes you to the next tab.\n"
                                                    "<color_light_green>%s</color> returns you to the main menu." ),
                                                ctxt.get_desc( "UP" ), ctxt.get_desc( "DOWN" ),
                                                ctxt.get_desc( "RIGHT" ), ctxt.get_desc( "LEFT" ),
                                                ctxt.get_desc( "HELP_KEYBINDINGS" ), ctxt.get_desc( "NEXT_TAB" ),
                                                ctxt.get_desc( "PREV_TAB" ) ) );

        data->handle.DirtyVariable( "tabs" );
        dirty_nc_shell( data->handle );
        data->handle.DirtyVariable( "points_rml" );
        data->handle.DirtyVariable( "stats" );
        data->handle.DirtyVariable( "cost_rml" );
        data->handle.DirtyVariable( "desc_rml" );
        data->handle.DirtyVariable( "hints_rml" );
    };

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    rml.open( newcharacter_rmlui_enabled(), "newcharstats", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        register_nc_stats_rml_types( c );
        c.Bind( "tabs", &data->tabs );
        bind_nc_shell( c, data->shell );
        // Arrow clicks are translated into the SAME action strings the keyboard
        // produces, so each step's existing PREV_TAB/NEXT_TAB handling — including
        // the "Return to main menu?" confirm on step 0 — is reused unchanged.
        c.BindEventCallback( "on_prev",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = -1; } );
        c.BindEventCallback( "on_next",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = 1; } );
        c.Bind( "points_rml", &data->points_rml );
        c.Bind( "stats", &data->stats );
        c.Bind( "cost_rml", &data->cost_rml );
        c.Bind( "desc_rml", &data->desc_rml );
        c.Bind( "hints_rml", &data->hints_rml );
        data->handle = c.GetModelHandle();
    } );
    if( rml_doc_unavailable( rml, _( "Character creation (STATS tab)" ) ) ) {
        return tab_direction::QUIT;
    }

    do {
        ui_manager::redraw();
        nc_nav = 0;
        std::string action = ctxt.handle_input();
        if( nc_nav != 0 ) {
            action = nc_nav < 0 ? "PREV_TAB" : "NEXT_TAB";
        }
        if( action == "DOWN" ) {
            if( sel < 4 ) {
                sel++;
            } else {
                sel = 1;
            }
        } else if( action == "UP" ) {
            if( sel > 1 ) {
                sel--;
            } else {
                sel = 4;
            }
        } else if( action == "RANDOMIZE" ) {
            sel = rng( 1, 4 );
        } else if( action == "LEFT" ) {
            if( sel == 1 && u.str_max > 4 ) {
                if( u.str_max > HIGH_STAT ) {
                    points.stat_points++;
                }
                u.str_max--;
                points.stat_points++;
            } else if( sel == 2 && u.dex_max > 4 ) {
                if( u.dex_max > HIGH_STAT ) {
                    points.stat_points++;
                }
                u.dex_max--;
                points.stat_points++;
            } else if( sel == 3 && u.int_max > 4 ) {
                if( u.int_max > HIGH_STAT ) {
                    points.stat_points++;
                }
                u.int_max--;
                points.stat_points++;
            } else if( sel == 4 && u.per_max > 4 ) {
                if( u.per_max > HIGH_STAT ) {
                    points.stat_points++;
                }
                u.per_max--;
                points.stat_points++;
            }
        } else if( action == "RIGHT" ) {
            if( sel == 1 && u.str_max < max_stat_points ) {
                points.stat_points--;
                if( u.str_max >= HIGH_STAT ) {
                    points.stat_points--;
                }
                u.str_max++;
            } else if( sel == 2 && u.dex_max < max_stat_points ) {
                points.stat_points--;
                if( u.dex_max >= HIGH_STAT ) {
                    points.stat_points--;
                }
                u.dex_max++;
            } else if( sel == 3 && u.int_max < max_stat_points ) {
                points.stat_points--;
                if( u.int_max >= HIGH_STAT ) {
                    points.stat_points--;
                }
                u.int_max++;
            } else if( sel == 4 && u.per_max < max_stat_points ) {
                points.stat_points--;
                if( u.per_max >= HIGH_STAT ) {
                    points.stat_points--;
                }
                u.per_max++;
            }
        } else if( action == "PREV_TAB" ) {
            return tab_direction::BACKWARD;
        } else if( action == "NEXT_TAB" ) {
            return tab_direction::FORWARD;
        } else if( action == "QUIT" && query_yn( _( "Return to main menu?" ) ) ) {
            return tab_direction::QUIT;
        }
    } while( true );
}

namespace
{
struct nc_traits_tab {
    Rml::String name_rml;
    Rml::String icon_dec;   //< placeholder tab glyph
    bool selected = false;
    bool done = false;   //< step already passed
};
struct nc_traits_session {
    Rml::Vector<nc_traits_tab> tabs;
    nc_shell shell;
    Rml::String points_rml;
    nc_balance balance;
    Rml::String cost_rml;     // "<trait> costs/earns N points" for the working trait
    Rml::String col0_html;    // good column (baked rows)
    Rml::String col1_html;    // bad column
    Rml::String col2_html;    // neutral column (empty unless used_pages==3)
    bool show_col2 = false;
    Rml::String desc_rml;     // working trait description
    Rml::DataModelHandle handle;
};

bool g_nc_traits_types_registered = false;

void register_nc_traits_rml_types( Rml::DataModelConstructor &c )
{
    if( g_nc_traits_types_registered ) {
        return;
    }
    Rml::StructHandle<nc_traits_tab> th = c.RegisterStruct<nc_traits_tab>();
    th.RegisterMember( "name_rml", &nc_traits_tab::name_rml );
    th.RegisterMember( "icon_dec", &nc_traits_tab::icon_dec );
    th.RegisterMember( "selected", &nc_traits_tab::selected );
    th.RegisterMember( "done", &nc_traits_tab::done );
    c.RegisterArray<Rml::Vector<nc_traits_tab>>();
    Rml::StructHandle<nc_balance> bh = c.RegisterStruct<nc_balance>();
    bh.RegisterMember( "show", &nc_balance::show );
    bh.RegisterMember( "rotate", &nc_balance::rotate );
    bh.RegisterMember( "good_top", &nc_balance::good_top );
    bh.RegisterMember( "bad_top", &nc_balance::bad_top );
    bh.RegisterMember( "good_rml", &nc_balance::good_rml );
    bh.RegisterMember( "bad_rml", &nc_balance::bad_rml );
    bh.RegisterMember( "good_icon", &nc_balance::good_icon );
    bh.RegisterMember( "bad_icon", &nc_balance::bad_icon );
    bh.RegisterMember( "fulcrum_icon", &nc_balance::fulcrum_icon );
    g_nc_traits_types_registered = true;
}
} // namespace

tab_direction set_traits( avatar &u, points_left &points )
{
    const int max_trait_points = get_option<int>( "MAX_TRAIT_POINTS" );

    // Track how many good / bad POINTS we have; cap both at MAX_TRAIT_POINTS
    int num_good = 0;
    int num_bad = 0;

    struct trait_entry {
        trait_id id;
        bool avatar_has;
        bool conflicts;
        bool forbidden;
    };
    std::vector<trait_entry> vStartingTraits[3];

    for( auto &bio_iter : bionic_data::get_all() ) {
        if( bio_iter.points > 0 ) {
            if( u.has_bionic( bio_iter.id ) ) {
                num_good += bio_iter.points;
            }
        } else if( bio_iter.points < 0 ) {
            if( u.has_bionic( bio_iter.id ) ) {
                num_bad += bio_iter.points;
            }
        }
    }

    for( auto &traits_iter : mutation_branch::get_all() ) {
        // Don't list blacklisted traits
        if( mutation_branch::trait_is_blacklisted( traits_iter.id ) ) {
            continue;
        }

        // Hide exclusive traits for the wrong gender
        if( u.male ) {
            if( traits_iter.flags.contains( flag_FEMALE_EXCLUSIVE ) ) {
                continue;
            }
        } else {
            if( traits_iter.flags.contains( flag_MALE_EXCLUSIVE ) ) {
                continue;
            }
        }

        // Always show profession locked traits, regardless of if they are forbidden
        const std::vector<trait_id> proftraits = u.prof->get_locked_traits();
        const bool is_proftrait = std::find( proftraits.begin(), proftraits.end(),
                                             traits_iter.id ) != proftraits.end();
        // We show all starting traits, even if we can't pick them, to keep the interface consistent.
        if( traits_iter.startingtrait || g->scen->traitquery( traits_iter.id ) ||
            u.prof->is_allowed_trait( traits_iter.id ) || is_proftrait ) {
            size_t page;
            if( traits_iter.points > 0 ) {
                page = 0;
                if( u.has_trait( traits_iter.id ) ) {
                    num_good += traits_iter.points;
                }
            } else if( traits_iter.points < 0 ) {
                page = 1;
                if( u.has_trait( traits_iter.id ) ) {
                    num_bad += traits_iter.points;
                }
            } else {
                page = 2;
            }
            vStartingTraits[page].push_back( { traits_iter.id, false, false, g->scen->is_forbidden_trait( traits_iter.id ) } );
        }
    }
    //If the third page is empty, only use the first two.
    const int used_pages = vStartingTraits[2].empty() ? 2 : 3;

    for( auto &vStartingTrait : vStartingTraits ) {
        std::sort( vStartingTrait.begin(), vStartingTrait.end(), []( const trait_entry & a,
        const trait_entry & b ) {
            return trait_display_nocolor_sort( a.id, b.id );
        } );
    }

    const auto recalc_display_cache = [&]() {
        for( int page = 0; page < used_pages; page++ ) {
            for( trait_entry &entry : vStartingTraits[page] ) {
                entry.conflicts = newcharacter::has_conflicting_trait( u, entry.id );
                entry.avatar_has = u.has_trait( entry.id );
            }
        }
    };
    recalc_display_cache();

    int iCurWorkingPage = 0;
    int iStartPos[3] = { 0, 0, 0 };
    int iCurrentLine[3] = { 0, 0, 0 };
    size_t traits_size[3];
    for( int i = 0; i < 3; i++ ) {
        traits_size[i] = vStartingTraits[i].size();
    }

    size_t iContentHeight;
    size_t page_width;

    ui_adaptor ui;
    catacurses::window w;
    catacurses::window w_description;

    character_preview_window character_preview;
    character_preview.init( &u );
    const bool use_character_preview = get_option<bool>( "USE_CHARACTER_PREVIEW" );

    const auto init_windows = [&]( ui_adaptor & ui ) {
        w = catacurses::newwin( TERMY, TERMX, point_zero );
        w_description = catacurses::newwin( 3, TERMX - 2, point( 1, TERMY - 4 ) );
        page_width = std::min( ( TERMX - 4 ) / used_pages, 38 );


        if( use_character_preview ) {
            nc_prepare_preview( character_preview );
        }

        ui.position_from_window( w );

        iContentHeight = TERMY - 9;

        for( int i = 0; i < 3; i++ ) {
            // Shift start position to avoid iterating beyond end
            int total = static_cast<int>( traits_size[i] );
            int heigth = static_cast<int>( iContentHeight );
            iStartPos[i] = std::min( iStartPos[i], std::max( 0, total - heigth ) );
        }
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    input_context ctxt( "NEW_CHAR_TRAITS" );
    ctxt.register_cardinal();
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "RANDOMIZE" );
    ctxt.register_action( "REROLL_CHARACTER" );
    ctxt.register_action( "REROLL_CHARACTER_WITH_SCENARIO" );
    ctxt.register_action( "REROLL_APPEARANCE" );
    ctxt.register_action( "QUIT" );
    // Required for the navigator/card clicks to reach this loop at all.
    // MOUSE_LEFT binds to action id SELECT (keybindings.json:1175). An UNREGISTERED
    // mouse action resolves to CATA_ERROR, and input.cpp:894-897 `continue`s on it
    // BEFORE the registered_any_input check at :912 — so ANY_INPUT cannot rescue a
    // mouse event, only registering the action it maps to can. Without this the
    // click callback fires but handle_input() never returns, leaving the loop parked
    // until an unrelated keypress, which then got hijacked into a step change.
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "zoom_in" );
    ctxt.register_action( "zoom_out" );
    ctxt.register_action( "TOGGLE_CHARACTER_PREVIEW_CLOTHES" );

    // RmlUi render path (render-only; keyboard owns nav/confirm/reroll below).
    auto data = std::make_unique<nc_traits_session>();
    rml_doc rml;
    // Set by the arrow click callbacks, consumed by the input loop below. Not a
    // tab_direction: it is translated into an action string so the existing
    // keyboard handling stays the single place navigation is decided.
    int nc_nav = 0;
    const auto sync_rml = [&]() {
        if( !data->handle ) {
            return;
        }
        data->tabs = build_nc_char_tabs<nc_traits_tab>( 4 );  // TRAITS tab active
        data->shell = fill_nc_shell( 4, ctxt );
        data->points_rml = cata_text_to_rml( nc_points_line( points ) );
        data->balance = nc_make_balance( num_good, num_bad, max_trait_points,
                                         points.is_freeform() );
        const auto build_col = [&]( int page ) -> std::string {
            nc_color on_act;
            nc_color off_act;
            nc_color on_pas;
            nc_color off_pas;
            switch( page )
            {
                case 0:
                    on_act = COL_TR_GOOD_ON_ACT;
                    off_act = COL_TR_GOOD_OFF_ACT;
                    on_pas = COL_TR_GOOD_ON_PAS;
                    off_pas = COL_TR_GOOD_OFF_PAS;
                    break;
                case 1:
                    on_act = COL_TR_BAD_ON_ACT;
                    off_act = COL_TR_BAD_OFF_ACT;
                    on_pas = COL_TR_BAD_ON_PAS;
                    off_pas = COL_TR_BAD_OFF_PAS;
                    break;
                default:
                    on_act = COL_TR_NEUT_ON_ACT;
                    off_act = COL_TR_NEUT_OFF_ACT;
                    on_pas = COL_TR_NEUT_ON_PAS;
                    off_pas = COL_TR_NEUT_OFF_PAS;
                    break;
            }
            const int cur = iCurrentLine[page];
            std::string html;
            for( size_t i = 0; i < vStartingTraits[page].size(); i++ )
            {
                const trait_entry &e = vStartingTraits[page][i];
                nc_color col;
                if( iCurWorkingPage == page ) {
                    if( e.avatar_has ) {
                        col = on_act;
                    } else if( e.conflicts || e.forbidden ) {
                        col = c_dark_gray;
                    } else {
                        col = off_act;
                    }
                } else {
                    if( e.avatar_has ) {
                        col = on_pas;
                    } else if( e.conflicts || e.forbidden ) {
                        col = c_light_gray;
                    } else {
                        col = off_pas;
                    }
                }
                const bool sel = ( iCurWorkingPage == page && static_cast<int>( i ) == cur );
                html += sel ? "<div class=\"item nc-trait-row selected\">"
                        : "<div class=\"item nc-trait-row\">";
                html += cata_text_to_rml( colorize( e.id.obj().name(), col ) );
                html += "</div>";
            }
            return html;
        };
        data->col0_html = build_col( 0 );
        data->col1_html = build_col( 1 );
        data->show_col2 = ( used_pages == 3 );
        data->col2_html = data->show_col2 ? build_col( 2 ) : std::string();

        const int wp = iCurWorkingPage;
        if( !vStartingTraits[wp].empty() ) {
            const int wl = std::min( iCurrentLine[wp],
                                     static_cast<int>( vStartingTraits[wp].size() ) - 1 );
            const trait_entry &we = vStartingTraits[wp][wl];
            const mutation_branch &wmd = we.id.obj();
            const nc_color col_tr = wp == 0 ? COL_TR_GOOD : ( wp == 1 ? COL_TR_BAD : COL_TR_NEUT );
            int pts = wmd.points;
            const bool neg = pts < 0;
            if( neg ) {
                pts *= -1;
            }
            data->cost_rml = cata_text_to_rml( colorize( string_format(
                                                   vgettext( "%s %s %d point", "%s %s %d points", pts ),
                                                   wmd.name(), neg ? _( "earns" ) : _( "costs" ), pts ), col_tr ) );
            data->desc_rml = cata_text_to_rml( colorize( wmd.desc(), col_tr ) );
        } else {
            data->cost_rml.clear();
            data->desc_rml.clear();
        }

        data->handle.DirtyVariable( "tabs" );
        dirty_nc_shell( data->handle );
        data->handle.DirtyVariable( "points_rml" );
        data->handle.DirtyVariable( "balance" );
        data->handle.DirtyVariable( "cost_rml" );
        data->handle.DirtyVariable( "col0_html" );
        data->handle.DirtyVariable( "col1_html" );
        data->handle.DirtyVariable( "col2_html" );
        data->handle.DirtyVariable( "show_col2" );
        data->handle.DirtyVariable( "desc_rml" );
    };

    rml.open( newcharacter_rmlui_enabled(), "newchartraits", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        register_nc_traits_rml_types( c );
        c.Bind( "tabs", &data->tabs );
        bind_nc_shell( c, data->shell );
        // Arrow clicks are translated into the SAME action strings the keyboard
        // produces, so each step's existing PREV_TAB/NEXT_TAB handling — including
        // the "Return to main menu?" confirm on step 0 — is reused unchanged.
        c.BindEventCallback( "on_prev",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = -1; } );
        c.BindEventCallback( "on_next",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = 1; } );
        c.Bind( "points_rml", &data->points_rml );
        c.Bind( "balance", &data->balance );
        c.Bind( "cost_rml", &data->cost_rml );
        c.Bind( "col0_html", &data->col0_html );
        c.Bind( "col1_html", &data->col1_html );
        c.Bind( "col2_html", &data->col2_html );
        c.Bind( "show_col2", &data->show_col2 );
        c.Bind( "desc_rml", &data->desc_rml );
        data->handle = c.GetModelHandle();
    } );
    if( rml_doc_unavailable( rml, _( "Character creation (TRAITS tab)" ) ) ) {
        return tab_direction::QUIT;
    }

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            sync_rml();
            // display() is RmlUi-aware (cp_rml_open/cp_rml_position + a GPU sprite
            // for the avatar), so it composes over this document rather than
            // fighting it. It was skipped only because this branch returned early.
            if( use_character_preview ) {
                character_preview.display();
            }
            return;
        }
    } );

    do {
        ui_manager::redraw();
        nc_nav = 0;
        std::string action = ctxt.handle_input();
        if( nc_nav != 0 ) {
            action = nc_nav < 0 ? "PREV_TAB" : "NEXT_TAB";
        }
        if( action == "zoom_in" && use_character_preview ) {
            character_preview.zoom_in();
        }
        if( action == "zoom_out" && use_character_preview ) {
            character_preview.zoom_out();
        }
        if( action == "TOGGLE_CHARACTER_PREVIEW_CLOTHES" && use_character_preview ) {
            character_preview.toggle_clothes();
        }
        if( action == "LEFT" ) {
            iCurWorkingPage--;
            if( iCurWorkingPage < 0 ) {
                iCurWorkingPage = used_pages - 1;
            }
        } else if( action == "RIGHT" ) {
            iCurWorkingPage++;
            if( iCurWorkingPage > used_pages - 1 ) {
                iCurWorkingPage = 0;
            }
        } else if( action == "UP" ) {
            if( iCurrentLine[iCurWorkingPage] == 0 ) {
                iCurrentLine[iCurWorkingPage] = traits_size[iCurWorkingPage] - 1;
            } else {
                iCurrentLine[iCurWorkingPage]--;
            }
        } else if( action == "REROLL_CHARACTER" ) {
            points.init_from_options();
            u.randomize( false, points );
            // Return tab_direction::NONE so we re-enter this tab again, but it forces a complete redrawing of it.
            return tab_direction::NONE;
        } else if( action == "REROLL_CHARACTER_WITH_SCENARIO" ) {
            points.init_from_options();
            u.randomize( true, points );
            // Return tab_direction::NONE so we re-enter this tab again, but it forces a complete redrawing of it.
            return tab_direction::NONE;
        } else if( action == "REROLL_APPEARANCE" ) {
            u.randomize_cosmetics();
            //u.set_body();
            // Return tab_direction::NONE so we re-enter this tab again, but it forces a complete redrawing of it.
            return tab_direction::NONE;
        } else if( action == "DOWN" ) {
            iCurrentLine[iCurWorkingPage]++;
            if( static_cast<size_t>( iCurrentLine[iCurWorkingPage] ) >= traits_size[iCurWorkingPage] ) {
                iCurrentLine[iCurWorkingPage] = 0;
            }
        } else if( action == "RANDOMIZE" ) {
            iCurrentLine[iCurWorkingPage] = rng( 0, traits_size[iCurWorkingPage] - 1 );
        } else if( action == "CONFIRM" ) {
            int inc_type = 0;
            const trait_id cur_trait = vStartingTraits[iCurWorkingPage][iCurrentLine[iCurWorkingPage]].id;
            const mutation_branch &mdata = cur_trait.obj();

            // Look through the profession bionics, and see if any of them conflict with this trait
            std::vector<bionic_id> cbms_blocking_trait = bionics_cancelling_trait( u.prof->CBMs(), cur_trait );
            std::vector<bionic_id> cbms_blocking_trait2 = bionics_cancelling_trait( u.get_bionics(),
                cur_trait );
            for( auto cbm : cbms_blocking_trait2 ) {
                cbms_blocking_trait.push_back( cbm );
            }
            const bool has_trait = u.has_trait( cur_trait );

            if( has_trait ) {

                inc_type = -1;

                if( g->scen->is_locked_trait( cur_trait ) ) {
                    inc_type = 0;
                    popup( _( "Your scenario of %s prevents you from removing this trait." ),
                           g->scen->gender_appropriate_name( u.male ) );
                } else if( u.prof->is_locked_trait( cur_trait ) ) {
                    inc_type = 0;
                    popup( _( "Your profession of %s prevents you from removing this trait." ),
                           u.prof->gender_appropriate_name( u.male ) );
                } else {
                    const auto mandatory_type = std::ranges::find_if( cur_trait.obj().types,
                    []( const auto & t ) { return mutation_type_is_mandatory( t ); } );
                    if( mandatory_type != cur_trait.obj().types.end() ) {
                        inc_type = 0;
                        popup( _( "You need to select 1 %s." ), mutation_type_display_name( *mandatory_type ) );
                    }
                }
            } else if( newcharacter::has_conflicting_trait( u, cur_trait ) ) {
                const auto &new_types = cur_trait.obj().types;
                const bool do_swap = std::ranges::any_of( new_types,
                []( const auto & t ) { return mutation_type_swaps_on_conflict( t ); } );
                if( do_swap ) {
                    const auto base_traits = u.get_base_traits();
                    auto it = std::ranges::find_if( base_traits, [&]( const trait_id & tr ) {
                        return tr != cur_trait && std::ranges::any_of( tr.obj().types,
                        [&]( const auto & t ) { return new_types.contains( t ); } );
                    } );
                    if( it != base_traits.end() ) {
                        inc_type = 1;
                        u.toggle_trait( *it );
                    } else {
                        popup( _( "You already picked a conflicting trait!" ) );
                    }
                } else {
                    popup( _( "You already picked a conflicting trait!" ) );
                }
            } else if( g->scen->is_forbidden_trait( cur_trait ) ) {
                popup( _( "The scenario you picked prevents you from taking this trait!" ) );
            } else if( u.prof->is_forbidden_trait( cur_trait ) ) {
                popup( _( "Your profession of %s prevents you from taking this trait." ),
                       u.prof->gender_appropriate_name( u.male ) );
            } else if( !cbms_blocking_trait.empty() ) {
                // Grab a list of the names of the bionics that block this trait
                // So that the player know what is preventing them from taking it
                std::vector<std::string> conflict_names;
                conflict_names.reserve( cbms_blocking_trait.size() );
                for( const bionic_id &conflict : cbms_blocking_trait ) {
                    conflict_names.emplace_back( conflict->name.translated() );
                }
                popup( _( "The following bionics prevent you from taking this trait: %s." ),
                       enumerate_as_string( conflict_names ) );
            } else if( iCurWorkingPage == 0 && num_good + mdata.points >
                       max_trait_points && !points.is_freeform() ) {
                popup( vgettext( "Sorry, but you can only take %d point of advantages.",
                                 "Sorry, but you can only take %d points of advantages.", max_trait_points ),
                       max_trait_points );

            } else if( iCurWorkingPage != 0 && num_bad + mdata.points <
                       -max_trait_points && !points.is_freeform() ) {
                popup( vgettext( "Sorry, but you can only take %d point of disadvantages.",
                                 "Sorry, but you can only take %d points of disadvantages.", max_trait_points ),
                       max_trait_points );

            } else {
                inc_type = 1;
            }

            //inc_type is either -1 or 1, so we can just multiply by it to invert
            if( inc_type != 0 ) {
                u.toggle_trait( cur_trait );
                // If character had trait - it's now removed. Trait could blocked some clothes, need to retoggle
                if( has_trait && character_preview.clothes_showing() ) {
                    character_preview.toggle_clothes();
                    character_preview.toggle_clothes();
                }
                points.trait_points -= mdata.points * inc_type;
                if( iCurWorkingPage == 0 ) {
                    num_good += mdata.points * inc_type;
                } else {
                    num_bad += mdata.points * inc_type;
                }
            }

            recalc_display_cache();
        } else if( action == "PREV_TAB" ) {
            character_preview.clear();
            return tab_direction::BACKWARD;
        } else if( action == "NEXT_TAB" ) {
            character_preview.clear();
            return tab_direction::FORWARD;
        } else if( action == "QUIT" && query_yn( _( "Return to main menu?" ) ) ) {
            character_preview.clear();
            return tab_direction::QUIT;
        }
    } while( true );
}

namespace
{
struct nc_bionics_tab {
    Rml::String name_rml;
    Rml::String icon_dec;   //< placeholder tab glyph
    bool selected = false;
    bool done = false;   //< step already passed
};
struct nc_bionics_session {
    Rml::Vector<nc_bionics_tab> tabs;
    nc_shell shell;
    Rml::String points_rml;
    nc_balance balance;
    Rml::String cost_rml;
    Rml::String col0_html;
    Rml::String col1_html;
    Rml::String col2_html;
    bool show_col2 = false;
    Rml::String desc_rml;
    Rml::DataModelHandle handle;
};

bool g_nc_bionics_types_registered = false;

void register_nc_bionics_rml_types( Rml::DataModelConstructor &c )
{
    if( g_nc_bionics_types_registered ) {
        return;
    }
    Rml::StructHandle<nc_bionics_tab> th = c.RegisterStruct<nc_bionics_tab>();
    th.RegisterMember( "name_rml", &nc_bionics_tab::name_rml );
    th.RegisterMember( "icon_dec", &nc_bionics_tab::icon_dec );
    th.RegisterMember( "selected", &nc_bionics_tab::selected );
    th.RegisterMember( "done", &nc_bionics_tab::done );
    c.RegisterArray<Rml::Vector<nc_bionics_tab>>();
    Rml::StructHandle<nc_balance> bh = c.RegisterStruct<nc_balance>();
    bh.RegisterMember( "show", &nc_balance::show );
    bh.RegisterMember( "rotate", &nc_balance::rotate );
    bh.RegisterMember( "good_top", &nc_balance::good_top );
    bh.RegisterMember( "bad_top", &nc_balance::bad_top );
    bh.RegisterMember( "good_rml", &nc_balance::good_rml );
    bh.RegisterMember( "bad_rml", &nc_balance::bad_rml );
    bh.RegisterMember( "good_icon", &nc_balance::good_icon );
    bh.RegisterMember( "bad_icon", &nc_balance::bad_icon );
    bh.RegisterMember( "fulcrum_icon", &nc_balance::fulcrum_icon );
    g_nc_bionics_types_registered = true;
}
} // namespace

tab_direction set_bionics( avatar &u, points_left &points )
{
    const int max_trait_points = get_option<int>( "MAX_TRAIT_POINTS" );

    // Track how many good / bad POINTS we have; cap both at MAX_TRAIT_POINTS
    int num_good = 0;
    int num_bad = 0;

    struct bionic_entry {
        bionic_id id;
        bool avatar_has;
        bool conflicts;
        bool forbidden;
    };
    std::vector<bionic_entry> vStartingBionics[3];

    for( auto &traits_iter : mutation_branch::get_all() ) {
        if( traits_iter.points > 0 ) {
            if( u.has_trait( traits_iter.id ) ) {
                num_good += traits_iter.points;
            }
        } else if( traits_iter.points < 0 ) {
            if( u.has_trait( traits_iter.id ) ) {
                num_bad += traits_iter.points;
            }
        }
    }

    for( auto &bio_iter : bionic_data::get_all() ) {

        // Always show profession locked traits, regardless of if they are forbidden
        const std::vector<bionic_id> profbionics = u.prof->CBMs();
        const bool is_profbionic = std::find( profbionics.begin(), profbionics.end(),
                                              bio_iter.id ) != profbionics.end();
        // We show all starting traits, even if we can't pick them, to keep the interface consistent.
        if( bio_iter.starting_bionic || g->scen->bionicquery( bio_iter.id ) ||
            u.prof->is_allowed_bionic( bio_iter.id ) || is_profbionic ) {
            size_t page;
            if( bio_iter.points > 0 ) {
                page = 0;
                if( u.has_bionic( bio_iter.id ) ) {
                    num_good += bio_iter.points;
                }
            } else if( bio_iter.points < 0 ) {
                page = 1;
                if( u.has_bionic( bio_iter.id ) ) {
                    num_bad += bio_iter.points;
                }
            } else {
                page = 2;
            }
            vStartingBionics[page].push_back( { bio_iter.id, false, false, g->scen->is_forbidden_bionic( bio_iter.id ) } );
        }
    }
    //If the third page is empty, only use the first two.
    const int used_pages = vStartingBionics[2].empty() ? 2 : 3;

    for( auto &vStartingBionic : vStartingBionics ) {
        std::sort( vStartingBionic.begin(), vStartingBionic.end(), []( const bionic_entry & a,
        const bionic_entry & b ) {
            return localized_compare( a.id->name.translated(), b.id->name.translated() );
        } );
    }

    const auto recalc_display_cache = [&]() {
        auto cbms = u.prof->CBMs();
        for( int page = 0; page < used_pages; page++ ) {
            for( bionic_entry &entry : vStartingBionics[page] ) {
                entry.conflicts = newcharacter::bionic_has_conflict( u, entry.id );
                entry.avatar_has = u.has_bionic( entry.id ) ||
                                   std::find( cbms.begin(), cbms.end(), entry.id ) != cbms.end();
            }
        }
    };
    recalc_display_cache();

    int iCurWorkingPage = 0;
    int iStartPos[3] = { 0, 0, 0 };
    int iCurrentLine[3] = { 0, 0, 0 };
    size_t bionics_size[3];
    for( int i = 0; i < 3; i++ ) {
        bionics_size[i] = vStartingBionics[i].size();
    }

    size_t iContentHeight;
    size_t page_width;

    ui_adaptor ui;
    catacurses::window w;
    catacurses::window w_description;

    character_preview_window character_preview;
    character_preview.init( &u );
    const bool use_character_preview = get_option<bool>( "USE_CHARACTER_PREVIEW" );

    const auto init_windows = [&]( ui_adaptor & ui ) {
        w = catacurses::newwin( TERMY, TERMX, point_zero );
        w_description = catacurses::newwin( 3, TERMX - 2, point( 1, TERMY - 4 ) );
        page_width = std::min( ( TERMX - 4 ) / used_pages, 38 );


        if( use_character_preview ) {
            nc_prepare_preview( character_preview );
        }

        ui.position_from_window( w );

        iContentHeight = TERMY - 9;

        for( int i = 0; i < 3; i++ ) {
            // Shift start position to avoid iterating beyond end
            int total = static_cast<int>( bionics_size[i] );
            int heigth = static_cast<int>( iContentHeight );
            iStartPos[i] = std::min( iStartPos[i], std::max( 0, total - heigth ) );
        }
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    input_context ctxt( "NEW_CHAR_TRAITS" );
    ctxt.register_cardinal();
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "RANDOMIZE" );
    ctxt.register_action( "REROLL_CHARACTER" );
    ctxt.register_action( "REROLL_CHARACTER_WITH_SCENARIO" );
    ctxt.register_action( "REROLL_APPEARANCE" );
    ctxt.register_action( "QUIT" );
    // Required for the navigator/card clicks to reach this loop at all.
    // MOUSE_LEFT binds to action id SELECT (keybindings.json:1175). An UNREGISTERED
    // mouse action resolves to CATA_ERROR, and input.cpp:894-897 `continue`s on it
    // BEFORE the registered_any_input check at :912 — so ANY_INPUT cannot rescue a
    // mouse event, only registering the action it maps to can. Without this the
    // click callback fires but handle_input() never returns, leaving the loop parked
    // until an unrelated keypress, which then got hijacked into a step change.
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "zoom_in" );
    ctxt.register_action( "zoom_out" );
    ctxt.register_action( "TOGGLE_CHARACTER_PREVIEW_CLOTHES" );

    // RmlUi render path (render-only; keyboard owns nav/confirm/reroll below).
    // Structurally the TRAITS tab with bionic data. Tile preview not drawn in rml.
    auto data = std::make_unique<nc_bionics_session>();
    rml_doc rml;
    // Set by the arrow click callbacks, consumed by the input loop below. Not a
    // tab_direction: it is translated into an action string so the existing
    // keyboard handling stays the single place navigation is decided.
    int nc_nav = 0;
    const auto sync_rml = [&]() {
        if( !data->handle ) {
            return;
        }
        data->tabs = build_nc_char_tabs<nc_bionics_tab>( 5 );  // BIONICS tab active
        data->shell = fill_nc_shell( 5, ctxt );
        data->points_rml = cata_text_to_rml( nc_points_line( points ) );
        data->balance = nc_make_balance( num_good, num_bad, max_trait_points,
                                         points.is_freeform() );
        const auto build_col = [&]( int page ) -> std::string {
            nc_color on_act;
            nc_color off_act;
            nc_color on_pas;
            nc_color off_pas;
            switch( page )
            {
                case 0:
                    on_act = COL_TR_GOOD_ON_ACT;
                    off_act = COL_TR_GOOD_OFF_ACT;
                    on_pas = COL_TR_GOOD_ON_PAS;
                    off_pas = COL_TR_GOOD_OFF_PAS;
                    break;
                case 1:
                    on_act = COL_TR_BAD_ON_ACT;
                    off_act = COL_TR_BAD_OFF_ACT;
                    on_pas = COL_TR_BAD_ON_PAS;
                    off_pas = COL_TR_BAD_OFF_PAS;
                    break;
                default:
                    on_act = COL_TR_NEUT_ON_ACT;
                    off_act = COL_TR_NEUT_OFF_ACT;
                    on_pas = COL_TR_NEUT_ON_PAS;
                    off_pas = COL_TR_NEUT_OFF_PAS;
                    break;
            }
            const int cur = iCurrentLine[page];
            std::string html;
            for( size_t i = 0; i < vStartingBionics[page].size(); i++ )
            {
                const bionic_entry &e = vStartingBionics[page][i];
                nc_color col;
                if( iCurWorkingPage == page ) {
                    if( e.avatar_has ) {
                        col = on_act;
                    } else if( e.conflicts || e.forbidden ) {
                        col = c_dark_gray;
                    } else {
                        col = off_act;
                    }
                } else {
                    if( e.avatar_has ) {
                        col = on_pas;
                    } else if( e.conflicts || e.forbidden ) {
                        col = c_light_gray;
                    } else {
                        col = off_pas;
                    }
                }
                const bool sel = ( iCurWorkingPage == page && static_cast<int>( i ) == cur );
                html += sel ? "<div class=\"item nc-trait-row selected\">"
                        : "<div class=\"item nc-trait-row\">";
                html += cata_text_to_rml( colorize( e.id.obj().name.translated(), col ) );
                html += "</div>";
            }
            return html;
        };
        data->col0_html = build_col( 0 );
        data->col1_html = build_col( 1 );
        data->show_col2 = ( used_pages == 3 );
        data->col2_html = data->show_col2 ? build_col( 2 ) : std::string();

        const int wp = iCurWorkingPage;
        if( !vStartingBionics[wp].empty() ) {
            const int wl = std::min( iCurrentLine[wp],
                                     static_cast<int>( vStartingBionics[wp].size() ) - 1 );
            const bionic_data &wb = vStartingBionics[wp][wl].id.obj();
            const nc_color col_tr = wp == 0 ? COL_TR_GOOD : ( wp == 1 ? COL_TR_BAD : COL_TR_NEUT );
            int pts = wb.points;
            const bool neg = pts < 0;
            if( neg ) {
                pts *= -1;
            }
            data->cost_rml = cata_text_to_rml( colorize( string_format(
                                                   vgettext( "%s %s %d point", "%s %s %d points", pts ),
                                                   wb.name.translated(), neg ? _( "earns" ) : _( "costs" ), pts ), col_tr ) );
            data->desc_rml = cata_text_to_rml( colorize( wb.description.translated(), col_tr ) );
        } else {
            data->cost_rml.clear();
            data->desc_rml.clear();
        }

        data->handle.DirtyVariable( "tabs" );
        dirty_nc_shell( data->handle );
        data->handle.DirtyVariable( "points_rml" );
        data->handle.DirtyVariable( "balance" );
        data->handle.DirtyVariable( "cost_rml" );
        data->handle.DirtyVariable( "col0_html" );
        data->handle.DirtyVariable( "col1_html" );
        data->handle.DirtyVariable( "col2_html" );
        data->handle.DirtyVariable( "show_col2" );
        data->handle.DirtyVariable( "desc_rml" );
    };

    rml.open( newcharacter_rmlui_enabled(), "newcharbionics", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        register_nc_bionics_rml_types( c );
        c.Bind( "tabs", &data->tabs );
        bind_nc_shell( c, data->shell );
        // Arrow clicks are translated into the SAME action strings the keyboard
        // produces, so each step's existing PREV_TAB/NEXT_TAB handling — including
        // the "Return to main menu?" confirm on step 0 — is reused unchanged.
        c.BindEventCallback( "on_prev",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = -1; } );
        c.BindEventCallback( "on_next",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = 1; } );
        c.Bind( "points_rml", &data->points_rml );
        c.Bind( "balance", &data->balance );
        c.Bind( "cost_rml", &data->cost_rml );
        c.Bind( "col0_html", &data->col0_html );
        c.Bind( "col1_html", &data->col1_html );
        c.Bind( "col2_html", &data->col2_html );
        c.Bind( "show_col2", &data->show_col2 );
        c.Bind( "desc_rml", &data->desc_rml );
        data->handle = c.GetModelHandle();
    } );
    if( rml_doc_unavailable( rml, _( "Character creation (BIONICS tab)" ) ) ) {
        return tab_direction::QUIT;
    }

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            sync_rml();
            // See the TRAITS branch: display() is RmlUi-aware; it was skipped only
            // because this branch returned early.
            if( use_character_preview ) {
                character_preview.display();
            }
            return;
        }
    } );

    do {
        ui_manager::redraw();
        nc_nav = 0;
        std::string action = ctxt.handle_input();
        if( nc_nav != 0 ) {
            action = nc_nav < 0 ? "PREV_TAB" : "NEXT_TAB";
        }
        if( action == "zoom_in" && use_character_preview ) {
            character_preview.zoom_in();
        }
        if( action == "zoom_out" && use_character_preview ) {
            character_preview.zoom_out();
        }
        if( action == "TOGGLE_CHARACTER_PREVIEW_CLOTHES" && use_character_preview ) {
            character_preview.toggle_clothes();
        }
        if( action == "LEFT" ) {
            iCurWorkingPage--;
            if( iCurWorkingPage < 0 ) {
                iCurWorkingPage = used_pages - 1;
            }
        } else if( action == "RIGHT" ) {
            iCurWorkingPage++;
            if( iCurWorkingPage > used_pages - 1 ) {
                iCurWorkingPage = 0;
            }
        } else if( action == "UP" ) {
            if( iCurrentLine[iCurWorkingPage] == 0 ) {
                iCurrentLine[iCurWorkingPage] = bionics_size[iCurWorkingPage] - 1;
            } else {
                iCurrentLine[iCurWorkingPage]--;
            }
        } else if( action == "REROLL_CHARACTER" ) {
            points.init_from_options();
            u.randomize( false, points );
            // Return tab_direction::NONE so we re-enter this tab again, but it forces a complete redrawing of it.
            return tab_direction::NONE;
        } else if( action == "REROLL_CHARACTER_WITH_SCENARIO" ) {
            points.init_from_options();
            u.randomize( true, points );
            // Return tab_direction::NONE so we re-enter this tab again, but it forces a complete redrawing of it.
            return tab_direction::NONE;
        } else if( action == "REROLL_APPEARANCE" ) {
            u.randomize_cosmetics();
            //u.set_body();
            // Return tab_direction::NONE so we re-enter this tab again, but it forces a complete redrawing of it.
            return tab_direction::NONE;
        } else if( action == "DOWN" ) {
            iCurrentLine[iCurWorkingPage]++;
            if( static_cast<size_t>( iCurrentLine[iCurWorkingPage] ) >= bionics_size[iCurWorkingPage] ) {
                iCurrentLine[iCurWorkingPage] = 0;
            }
        } else if( action == "RANDOMIZE" ) {
            iCurrentLine[iCurWorkingPage] = rng( 0, bionics_size[iCurWorkingPage] - 1 );
        } else if( action == "CONFIRM" ) {
            int inc_type = 0;
            const bionic_id cur_bionic = vStartingBionics[iCurWorkingPage][iCurrentLine[iCurWorkingPage]].id;
            const bionic_data &bio = cur_bionic.obj();

            std::vector<trait_id> conflicting_traits;
            // Look through the profession bionics, and see if any of them conflict with this trait
            for( trait_id id : bio.canceled_mutations ) {
                if( u.has_trait( id ) ) {
                    conflicting_traits.push_back( id );
                }
            }
            std::vector<bionic_id> missing_bionics;
            if( !bio.required_bionics.empty() ) {
                for( const bionic_id &req_bid : bio.required_bionics ) {
                    if( !u.has_bionic( req_bid ) ) {
                        missing_bionics.push_back( req_bid );
                    }
                }
            }
            bionic_id has_downgrade = bionic_id::NULL_ID();
            if( cur_bionic->upgraded_bionic != bionic_id::NULL_ID() ) {
                bionic_id downgrade = cur_bionic->upgraded_bionic;
                if( u.has_bionic( downgrade ) ) {
                    has_downgrade = downgrade;
                }
                if( has_downgrade == bionic_id::NULL_ID() ) {
                    while( downgrade->upgraded_bionic != bionic_id::NULL_ID() ) {
                        downgrade = cur_bionic->upgraded_bionic;
                        if( u.has_bionic( downgrade ) ) {
                            has_downgrade = downgrade;
                            break;
                        }
                    }
                }
            }
            bionic_id has_upgrade = bionic_id::NULL_ID();
            for( bionic_id bio : cur_bionic->available_upgrades ) {
                if( u.has_bionic( bio ) ) {
                    has_upgrade = bio;
                    break;
                }
            }
            const bool has_bionic = u.has_bionic( cur_bionic );
            if( has_bionic ) {
                std::vector<std::string> dependent_bionics;
                for( const bionic &i : u.get_bionic_collection() ) {
                    const bionic_id &bid = i.id;
                    // look at required bionics for every installed bionic
                    for( const bionic_id &req_bid : bid->required_bionics ) {
                        if( req_bid == cur_bionic ) {
                            dependent_bionics.push_back( bid->name.translated() );
                        }
                    }
                }
                if( !dependent_bionics.empty() ) {
                    popup( _( "These bionics are dependent on the bionic you are trying to uninstall %s." ),
                           enumerate_as_string( dependent_bionics ) );
                } else {
                    inc_type = - 1;
                }
            } else if( g->scen->forbids_bionics() ) {
                popup( _( "The scenario you picked prevents you from taking any bionics!" ) );
            } else if( u.prof->forbids_bionics() ) {
                popup( _( "The profession you picked prevents you from taking any bionics!" ) );
            } else if( g->scen->is_forbidden_bionic( cur_bionic ) ) {
                popup( _( "The scenario you picked prevents you from taking this bionic!" ) );
            } else if( u.prof->is_forbidden_bionic( cur_bionic ) ) {
                popup( _( "Your profession of %s prevents you from taking this bionic." ),
                       u.prof->gender_appropriate_name( u.male ) );
            } else if( g->scen->is_locked_bionic( cur_bionic ) ) {
                inc_type = 0;
                popup( _( "Your scenario of %s prevents you from removing this bionic." ),
                       g->scen->gender_appropriate_name( u.male ) );
            } else if( u.prof->is_locked_bionic( cur_bionic ) ) {
                inc_type = 0;
                popup( _( "Your profession of %s prevents you from removing this bionic." ),
                       u.prof->gender_appropriate_name( u.male ) );
            } else if( !conflicting_traits.empty() ) {
                // Grab a list of the names of the bionics that block this trait
                // So that the player know what is preventing them from taking it
                std::vector<std::string> conflict_names;
                conflict_names.reserve( conflicting_traits.size() );
                for( const trait_id &conflict : conflicting_traits ) {
                    conflict_names.emplace_back( conflict.obj().name() );
                }
                popup( _( "The following traits prevent you from taking this bionic: %s." ),
                       enumerate_as_string( conflict_names ) );
            } else if( iCurWorkingPage == 0 && num_good + bio.points >
                       max_trait_points && !points.is_freeform() ) {
                popup( vgettext( "Sorry, but you can only take %d point of advantages.",
                                 "Sorry, but you can only take %d points of advantages.", max_trait_points ),
                       max_trait_points );

            } else if( iCurWorkingPage != 0 && num_bad + bio.points <
                       -max_trait_points && !points.is_freeform() ) {
                popup( vgettext( "Sorry, but you can only take %d point of disadvantages.",
                                 "Sorry, but you can only take %d points of disadvantages.", max_trait_points ),
                       max_trait_points );

            } else if( !u.bionic_installation_issues( cur_bionic ).empty() ) {
                const auto &issues = u.bionic_installation_issues( cur_bionic );
                std::string detailed_info;
                for( auto &elem : issues ) {
                    //~ <Body part name>: <number of slots> more slot(s) needed.
                    detailed_info += string_format( _( "\n%s: %i more slot(s) needed." ),
                                                    body_part_name_as_heading( elem.first->token, 1 ),
                                                    elem.second );
                }
                popup( _( "Not enough space for bionic installation!%s" ), detailed_info );
            } else if( !missing_bionics.empty() ) {
                // Grab a list of the names of the bionics that block this trait
                // So that the player know what is preventing them from taking it
                std::vector<std::string> conflict_names;
                conflict_names.reserve( missing_bionics.size() );
                for( const bionic_id &conflict : missing_bionics ) {
                    conflict_names.emplace_back( conflict->name.translated() );
                }
                popup( _( "The lack of the following bionics are prevent you from taking this bionic: %s." ),
                       enumerate_as_string( conflict_names ) );
            } else if( has_downgrade != bionic_id::NULL_ID() ) {
                popup( _( "You already have the downgraded version of the bionic: %s" ), has_downgrade->name );
            } else if( has_upgrade != bionic_id::NULL_ID() ) {
                popup( _( "You already have the upgraded version of the bionic: %s" ), has_upgrade->name );
            } else {
                inc_type = 1;
            }

            //inc_type is either -1 or 1, so we can just multiply by it to invert
            if( inc_type != 0 ) {
                u.toggle_bionic( cur_bionic );
                // If character had trait - it's now removed. Trait could blocked some clothes, need to retoggle
                if( has_bionic && character_preview.clothes_showing() ) {
                    character_preview.toggle_clothes();
                    character_preview.toggle_clothes();
                }
                points.trait_points -= bio.points * inc_type;

                if( iCurWorkingPage == 0 ) {
                    num_good += bio.points * inc_type;
                } else {
                    num_bad += bio.points * inc_type;
                }
            }

            recalc_display_cache();
        } else if( action == "PREV_TAB" ) {
            character_preview.clear();
            return tab_direction::BACKWARD;
        } else if( action == "NEXT_TAB" ) {
            character_preview.clear();
            return tab_direction::FORWARD;
        } else if( action == "QUIT" && query_yn( _( "Return to main menu?" ) ) ) {
            character_preview.clear();
            return tab_direction::QUIT;
        }
    } while( true );
}

namespace
{
struct nc_prof_tab {
    Rml::String name_rml;
    Rml::String icon_dec;   //< placeholder tab glyph
    bool selected = false;
    bool done = false;   //< step already passed
};
struct nc_prof_row {
    Rml::String text_rml;
    bool selected = false;
};
struct nc_prof_session {
    Rml::Vector<nc_prof_tab> tabs;
    nc_shell shell;
    Rml::String points_rml;
    Rml::String cost_rml;
    Rml::Vector<nc_prof_row> rows;
    Rml::String desc_rml;
    Rml::String info_rml;
    Rml::String sort_rml;
    Rml::String gender_rml;
    Rml::String filter_rml;
    Rml::DataModelHandle handle;
};

bool g_nc_prof_types_registered = false;

void register_nc_prof_rml_types( Rml::DataModelConstructor &c )
{
    if( g_nc_prof_types_registered ) {
        return;
    }
    Rml::StructHandle<nc_prof_tab> th = c.RegisterStruct<nc_prof_tab>();
    th.RegisterMember( "name_rml", &nc_prof_tab::name_rml );
    th.RegisterMember( "icon_dec", &nc_prof_tab::icon_dec );
    th.RegisterMember( "selected", &nc_prof_tab::selected );
    th.RegisterMember( "done", &nc_prof_tab::done );
    c.RegisterArray<Rml::Vector<nc_prof_tab>>();
    Rml::StructHandle<nc_prof_row> rh = c.RegisterStruct<nc_prof_row>();
    rh.RegisterMember( "text_rml", &nc_prof_row::text_rml );
    rh.RegisterMember( "selected", &nc_prof_row::selected );
    c.RegisterArray<Rml::Vector<nc_prof_row>>();
    g_nc_prof_types_registered = true;
}
} // namespace

tab_direction set_profession( avatar &u, points_left &points,
                              const tab_direction direction )
{
    int cur_id = 0;
    tab_direction retval = tab_direction::NONE;
    int desc_offset = 0;
    int iContentHeight = 0;

    ui_adaptor ui;
    catacurses::window w;
    catacurses::window w_description;
    catacurses::window w_sorting;
    catacurses::window w_genderswap;
    catacurses::window w_items;
    character_preview_window character_preview;
    character_preview.init( &u );
    const bool use_character_preview = get_option<bool>( "USE_CHARACTER_PREVIEW" );
    const auto init_windows = [&]( ui_adaptor & ui ) {
        iContentHeight = TERMY - 10;
        w = catacurses::newwin( TERMY, TERMX, point_zero );
        w_description = catacurses::newwin( 4, TERMX - 2, point( 1, TERMY - 5 ) );
        w_sorting = catacurses::newwin( 1, 55, point( TERMX / 2, 5 ) );
        w_genderswap = catacurses::newwin( 1, 55, point( TERMX / 2, 6 ) );
        w_items = catacurses::newwin( iContentHeight - 2, 55, point( TERMX / 2, 7 ) );
        if( use_character_preview ) {
            nc_prepare_preview( character_preview );
        }
        ui.position_from_window( w );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    input_context ctxt( "NEW_CHAR_PROFESSIONS" );
    ctxt.register_cardinal();
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "CHANGE_GENDER" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "SORT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "RANDOMIZE" );
    ctxt.register_action( "FILTER" );
    ctxt.register_action( "QUIT" );
    // Required for the navigator/card clicks to reach this loop at all.
    // MOUSE_LEFT binds to action id SELECT (keybindings.json:1175). An UNREGISTERED
    // mouse action resolves to CATA_ERROR, and input.cpp:894-897 `continue`s on it
    // BEFORE the registered_any_input check at :912 — so ANY_INPUT cannot rescue a
    // mouse event, only registering the action it maps to can. Without this the
    // click callback fires but handle_input() never returns, leaving the loop parked
    // until an unrelated keypress, which then got hijacked into a step change.
    ctxt.register_action( "SELECT" );

    bool recalc_profs = true;
    int profs_length = 0;
    std::string filterstring;
    std::vector<string_id<profession>> sorted_profs;

    if( direction == tab_direction::FORWARD ) {
        points.skill_points -= u.prof->point_cost();
    }

    int iheight = 0;

    // RmlUi render path (render-only; keyboard owns nav/scroll/confirm/sort/gender/
    // filter below). Tile character_preview not drawn in rml mode this slice.
    auto data = std::make_unique<nc_prof_session>();
    rml_doc rml;
    // Set by the arrow click callbacks, consumed by the input loop below. Not a
    // tab_direction: it is translated into an action string so the existing
    // keyboard handling stays the single place navigation is decided.
    int nc_nav = 0;
    bool rml_scroll_pending = false;
    const auto sync_rml = [&]() {
        if( !data->handle ) {
            return;
        }
        data->tabs = build_nc_char_tabs<nc_prof_tab>( 2 );  // PROFESSION tab active
        data->shell = fill_nc_shell( 2, ctxt );
        const bool valid = cur_id >= 0 && static_cast<size_t>( cur_id ) < sorted_profs.size();

        std::string pmsg = nc_points_line( points );
        if( valid ) {
            const int netPointCost = sorted_profs[cur_id]->point_cost() - u.prof->point_cost();
            if( netPointCost > 0 ) {
                pmsg += colorize( string_format( " (-%d)", std::abs( netPointCost ) ), c_red );
            } else if( netPointCost < 0 ) {
                pmsg += colorize( string_format( " (+%d)", std::abs( netPointCost ) ), c_green );
            }
        }
        data->points_rml = cata_text_to_rml( pmsg );

        if( valid ) {
            const string_id<profession> &pid = sorted_profs[cur_id];
            const bool can_pick = can_pick_prof( *pid, u, points.skill_points_left() );
            int pts = pid->point_cost();
            const bool neg = pts < 0;
            if( neg ) {
                pts *= -1;
            }
            const std::string msg = neg
                                    ? vgettext( "Profession %1$s earns %2$d point",
                                                "Profession %1$s earns %2$d points", pts )
                                    : vgettext( "Profession %1$s costs %2$d point",
                                                "Profession %1$s costs %2$d points", pts );
            data->cost_rml = cata_text_to_rml( colorize( string_format( msg,
                                               pid->gender_appropriate_name( u.male ), pts ),
                                               can_pick ? c_green : c_light_red ) );
            data->desc_rml = cata_text_to_rml( colorize( pid->description( u.male ), c_green ) );

            // The big info buffer (mirrors the curses w_items buffer verbatim).
            std::string buf;
            const auto prof_addictions = pid->addictions();
            if( !prof_addictions.empty() ) {
                buf += colorize( _( "Addictions:" ), c_light_blue ) + "\n";
                for( const auto &a : prof_addictions ) {
                    buf += string_format( pgettext( "set_profession_addictions", "%1$s (%2$d)" ),
                                          addiction_name( a ), a.intensity ) + "\n";
                }
            }
            const auto prof_traits = pid->get_locked_traits();
            buf += colorize( _( "Traits:" ), c_light_blue ) + "\n";
            if( prof_traits.empty() ) {
                buf += pgettext( "set_profession_trait", "None" ) + std::string( "\n" );
            } else {
                for( const auto &t : prof_traits ) {
                    buf += mutation_branch::get_name( t ) + "\n";
                }
            }
            std::vector<std::pair<skill_id, int>> prof_skills = pid->skills();
            std::stable_sort( prof_skills.begin(), prof_skills.end(),
            []( const std::pair<skill_id, int> &a, const std::pair<skill_id, int> &b ) {
                return localized_compare( std::make_pair( a.first->display_category(), a.first->name() ),
                                          std::make_pair( b.first->display_category(), b.first->name() ) );
            } );
            buf += colorize( _( "Skills:" ), c_light_blue ) + "\n";
            if( prof_skills.empty() ) {
                buf += pgettext( "set_profession_skill", "None" ) + std::string( "\n" );
            } else {
                skill_displayType_id cur_category = skill_displayType_id::NULL_ID();
                for( const auto &sl : prof_skills ) {
                    if( cur_category != sl.first->display_category() ) {
                        cur_category = sl.first->display_category();
                        buf += colorize( string_format( sl.first->display_category()->display_string() ),
                                         c_yellow ) + "\n";
                    }
                    buf += "  " + string_format( pgettext( "set_profession_skill", "%1$s (%2$d)" ),
                                                 sl.first.obj().name(), sl.second ) + "\n";
                }
            }
            const auto prof_items = pid->items( u.male, u.get_mutations() );
            buf += colorize( _( "Items:" ), c_light_blue ) + "\n";
            if( prof_items.empty() ) {
                buf += pgettext( "set_profession_item", "None" ) + std::string( "\n" );
            } else {
                std::string buffer_wielded;
                std::string buffer_worn;
                std::string buffer_inventory;
                for( const auto &it : prof_items ) {
                    if( it->has_flag( json_flag_no_auto_equip ) ) {
                        buffer_inventory += it->display_name() + "\n";
                    } else if( it->has_flag( json_flag_auto_wield ) ) {
                        buffer_wielded += it->display_name() + "\n";
                    } else if( it->is_armor() ) {
                        buffer_worn += it->display_name() + "\n";
                    } else {
                        buffer_inventory += it->display_name() + "\n";
                    }
                }
                buf += colorize( _( "Wielded:" ), c_cyan ) + "\n";
                buf += !buffer_wielded.empty() ? buffer_wielded
                       : pgettext( "set_profession_item_wielded", "None\n" );
                buf += colorize( _( "Worn:" ), c_cyan ) + "\n";
                buf += !buffer_worn.empty() ? buffer_worn
                       : pgettext( "set_profession_item_worn", "None\n" );
                buf += colorize( _( "Inventory:" ), c_cyan ) + "\n";
                buf += !buffer_inventory.empty() ? buffer_inventory
                       : pgettext( "set_profession_item_inventory", "None\n" );
            }
            auto prof_CBMs = pid->CBMs();
            std::sort( begin( prof_CBMs ), end( prof_CBMs ), []( const bionic_id & a,
            const bionic_id & b ) {
                return a->activated && !b->activated;
            } );
            buf += colorize( _( "Bionics:" ), c_light_blue ) + "\n";
            if( prof_CBMs.empty() ) {
                buf += pgettext( "set_profession_bionic", "None" ) + std::string( "\n" );
            } else {
                for( const auto &b : prof_CBMs ) {
                    const auto &cbm = b.obj();
                    if( cbm.activated && cbm.has_flag( STATIC( flag_id( "BIONIC_TOGGLED" ) ) ) ) {
                        buf += string_format( _( "%s (toggled)" ), cbm.name ) + "\n";
                    } else if( cbm.activated ) {
                        buf += string_format( _( "%s (activated)" ), cbm.name ) + "\n";
                    } else {
                        buf += cbm.name + "\n";
                    }
                }
            }
            if( !pid->pets().empty() ) {
                buf += colorize( _( "Pets:" ), c_light_blue ) + "\n";
                for( auto elem : pid->pets() ) {
                    monster mon( elem );
                    buf += mon.get_name() + "\n";
                }
            }
            if( pid->vehicle() ) {
                buf += colorize( _( "Vehicle:" ), c_light_blue ) + "\n";
                vproto_id veh_id = pid->vehicle();
                buf += veh_id->name + "\n";
            }
            if( !pid->spells().empty() ) {
                buf += colorize( _( "Spells:" ), c_light_blue ) + "\n";
                for( const std::pair<spell_id, int> spell_pair : pid->spells() ) {
                    buf += string_format( _( "%s level %d" ), spell_pair.first->name,
                                          spell_pair.second ) + "\n";
                }
            }
            std::optional<int> cash = pid->starting_cash();
            if( cash.has_value() ) {
                buf += colorize( _( "Money:" ), c_light_blue ) + "\n";
                buf += format_money( cash.value() ) + "\n";
            }
            std::vector<npc_class_id> npcs = pid->npcs();
            if( !npcs.empty() ) {
                buf += "\n" + colorize( _( "Companions:" ), c_light_blue ) + "\n";
                for( const npc_class_id &id : npcs ) {
                    if( id.is_valid() ) {
                        buf += id.obj().get_name() + "\n";
                    }
                }
            }
            data->info_rml = cata_text_to_rml( buf );

            data->sort_rml = cata_text_to_rml( string_format(
                                                   _( "<color_white>Sort by:</color> %1$s (Press <color_light_green>%2$s</color> to change sorting.)" ),
                                                   profession_sorter.sort_by_points ? _( "points" ) : _( "name" ),
                                                   ctxt.get_desc( "SORT" ) ) );
            const std::string g_switch_msg = u.male ?
                                             _( "Press <color_light_green>%1$s</color> to switch to <color_magenta>%2$s</color> (<color_pink>female</color>)." )
                                             :
                                             _( "Press <color_light_green>%1$s</color> to switch to <color_magenta>%2$s</color> (<color_light_cyan>male</color>)." );
            data->gender_rml = cata_text_to_rml( string_format( g_switch_msg,
                                                 ctxt.get_desc( "CHANGE_GENDER" ),
                                                 pid->gender_appropriate_name( !u.male ) ) );
        } else {
            data->cost_rml.clear();
            data->desc_rml.clear();
            data->info_rml.clear();
            data->sort_rml.clear();
            data->gender_rml.clear();
        }

        data->rows.clear();
        for( int i = 0; i < static_cast<int>( sorted_profs.size() ); i++ ) {
            const nc_color col = ( u.prof != sorted_profs[i] ) ? c_light_gray : COL_SKILL_USED;
            nc_prof_row r;
            r.text_rml = cata_text_to_rml( colorize(
                                               sorted_profs[i]->gender_appropriate_name( u.male ), col ) );
            r.selected = ( i == cur_id );
            data->rows.push_back( r );
        }

        data->filter_rml = cata_text_to_rml( string_format( "<%s>",
                                             filterstring.empty() ? _( "no filter" ) : filterstring ) );

        data->handle.DirtyVariable( "tabs" );
        dirty_nc_shell( data->handle );
        data->handle.DirtyVariable( "points_rml" );
        data->handle.DirtyVariable( "cost_rml" );
        data->handle.DirtyVariable( "rows" );
        data->handle.DirtyVariable( "desc_rml" );
        data->handle.DirtyVariable( "info_rml" );
        data->handle.DirtyVariable( "sort_rml" );
        data->handle.DirtyVariable( "gender_rml" );
        data->handle.DirtyVariable( "filter_rml" );

        if( rml_scroll_pending && valid ) {
            rml_scroll_pending = false;
            if( Rml::Element *list = rml.document()->GetElementById( "nc-prof-list" ) ) {
                if( cur_id < list->GetNumChildren() ) {
                    list->GetChild( cur_id )->ScrollIntoView(
                        Rml::ScrollIntoViewOptions( Rml::ScrollAlignment::Nearest ) );
                }
            }
        }
    };
    const auto scroll_info = [&]( int dir ) {
        if( !rml ) {
            return;
        }
        if( Rml::Element *e = rml.document()->GetElementById( "nc-prof-info" ) ) {
            const float page = e->GetClientHeight();
            const float maxtop = std::max( 0.0f, e->GetScrollHeight() - page );
            e->SetScrollTop( std::clamp( e->GetScrollTop() + dir * page * 0.15f, 0.0f, maxtop ) );
        }
    };

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            sync_rml();
            if( use_character_preview ) {
                character_preview.display();
            }
            return;
        }
    } );

    rml.open( newcharacter_rmlui_enabled(), "newcharprofession", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        register_nc_prof_rml_types( c );
        c.Bind( "tabs", &data->tabs );
        bind_nc_shell( c, data->shell );
        // Arrow clicks are translated into the SAME action strings the keyboard
        // produces, so each step's existing PREV_TAB/NEXT_TAB handling — including
        // the "Return to main menu?" confirm on step 0 — is reused unchanged.
        c.BindEventCallback( "on_prev",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = -1; } );
        c.BindEventCallback( "on_next",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = 1; } );
        c.Bind( "points_rml", &data->points_rml );
        c.Bind( "cost_rml", &data->cost_rml );
        c.Bind( "rows", &data->rows );
        c.Bind( "desc_rml", &data->desc_rml );
        c.Bind( "info_rml", &data->info_rml );
        c.Bind( "sort_rml", &data->sort_rml );
        c.Bind( "gender_rml", &data->gender_rml );
        c.Bind( "filter_rml", &data->filter_rml );
        data->handle = c.GetModelHandle();
    } );
    if( rml_doc_unavailable( rml, _( "Character creation (PROFESSION tab)" ) ) ) {
        return tab_direction::QUIT;
    }

    do {
        if( recalc_profs ) {
            sorted_profs = g->scen->permitted_professions();
            const auto new_end = std::remove_if( sorted_profs.begin(),
            sorted_profs.end(), [&]( const string_id<profession> &arg ) {
                return !lcmatch( arg->gender_appropriate_name( u.male ), filterstring );
            } );
            sorted_profs.erase( new_end, sorted_profs.end() );
            profs_length = sorted_profs.size();
            if( profs_length == 0 ) {
                popup( _( "Nothing found." ) ); // another case of black box in tiles
                filterstring.clear();
                continue;
            }

            // Sort professions by points.
            // profession_display_sort() keeps "unemployed" at the top.
            profession_sorter.male = u.male;
            std::stable_sort( sorted_profs.begin(), sorted_profs.end(), profession_sorter );

            // Select the current profession, if possible.
            for( int i = 0; i < profs_length; ++i ) {
                if( sorted_profs[i] == u.prof ) {
                    cur_id = i;
                    break;
                }
            }
            if( cur_id > profs_length - 1 ) {
                cur_id = 0;
            }

            recalc_profs = false;
        }

        ui_manager::redraw();
        nc_nav = 0;
        std::string action = ctxt.handle_input();
        if( nc_nav != 0 ) {
            action = nc_nav < 0 ? "PREV_TAB" : "NEXT_TAB";
        }
        if( action == "DOWN" ) {
            cur_id++;
            if( cur_id > profs_length - 1 ) {
                cur_id = 0;
            }
            desc_offset = 0;
            rml_scroll_pending = true;
            // Update preview immediately when moving selection
            if( use_character_preview ) {
                ui_manager::redraw();
            }
        } else if( action == "UP" ) {
            cur_id--;
            if( cur_id < 0 ) {
                cur_id = profs_length - 1;
            }
            desc_offset = 0;
            rml_scroll_pending = true;
            if( use_character_preview ) {
                ui_manager::redraw();
            }
        } else if( action == "LEFT" ) {
            if( rml ) {
                scroll_info( -1 );
            } else if( desc_offset > 0 ) {
                desc_offset--;
            }
        } else if( action == "RANDOMIZE" ) {
            cur_id = rng( 0, profs_length - 1 );
            rml_scroll_pending = true;
        } else if( action == "RIGHT" ) {
            if( rml ) {
                scroll_info( +1 );
            } else if( desc_offset < iheight ) {
                desc_offset++;
            }
        } else if( action == "CONFIRM" ) {
            // Remove traits from the previous profession
            for( const trait_id &old_trait : u.prof->get_locked_traits() ) {
                u.toggle_trait( old_trait );
            }
            const int netPointCost = sorted_profs[cur_id]->point_cost() - u.prof->point_cost();
            u.prof = sorted_profs[cur_id];
            u.set_base_age( random_age_for_profession( *u.prof ) );
            // Add traits for the new profession (and perhaps scenario, if, for example,
            // both the scenario and old profession require the same trait)
            newcharacter::add_traits( u, points );
            points.skill_points -= netPointCost;
        } else if( action == "CHANGE_GENDER" ) {
            u.male = !u.male;
            profession_sorter.male = u.male;
            if( !profession_sorter.sort_by_points ) {
                std::sort( sorted_profs.begin(), sorted_profs.end(), profession_sorter );
            }
        } else if( action == "PREV_TAB" ) {
            retval = tab_direction::BACKWARD;
        } else if( action == "NEXT_TAB" ) {
            retval = tab_direction::FORWARD;
        } else if( action == "SORT" ) {
            profession_sorter.sort_by_points = !profession_sorter.sort_by_points;
            recalc_profs = true;
        } else if( action == "FILTER" ) {
            string_input_popup()
            .title( _( "Search:" ) )
            .width( 60 )
            .description( _( "Search by profession name." ) )
            .edit( filterstring );
            recalc_profs = true;
        } else if( action == "QUIT" && query_yn( _( "Return to main menu?" ) ) ) {
            retval = tab_direction::QUIT;
        }

    } while( retval == tab_direction::NONE );

    return retval;
}

int skill_increment_cost( const Character &u, const skill_id &skill )
{
    return std::max( 1, ( u.get_skill_level( skill ) + 1 ) / 2 );
}

namespace
{
struct nc_skills_tab {
    Rml::String name_rml;
    Rml::String icon_dec;   //< placeholder tab glyph
    bool selected = false;
    bool done = false;   //< step already passed
};
struct nc_skill_row {
    Rml::String text_rml;
    bool is_header = false;
    bool selected = false;
};
struct nc_skills_session {
    Rml::Vector<nc_skills_tab> tabs;
    nc_shell shell;
    Rml::String points_rml;
    Rml::String cost_rml;
    Rml::Vector<nc_skill_row> rows;
    Rml::String desc_rml;
    Rml::DataModelHandle handle;
};

bool g_nc_skills_types_registered = false;

void register_nc_skills_rml_types( Rml::DataModelConstructor &c )
{
    if( g_nc_skills_types_registered ) {
        return;
    }
    Rml::StructHandle<nc_skills_tab> th = c.RegisterStruct<nc_skills_tab>();
    th.RegisterMember( "name_rml", &nc_skills_tab::name_rml );
    th.RegisterMember( "icon_dec", &nc_skills_tab::icon_dec );
    th.RegisterMember( "selected", &nc_skills_tab::selected );
    th.RegisterMember( "done", &nc_skills_tab::done );
    c.RegisterArray<Rml::Vector<nc_skills_tab>>();
    Rml::StructHandle<nc_skill_row> rh = c.RegisterStruct<nc_skill_row>();
    rh.RegisterMember( "text_rml", &nc_skill_row::text_rml );
    rh.RegisterMember( "is_header", &nc_skill_row::is_header );
    rh.RegisterMember( "selected", &nc_skill_row::selected );
    c.RegisterArray<Rml::Vector<nc_skill_row>>();
    g_nc_skills_types_registered = true;
}

// The selected skill's description + the recipes it unlocks, as one colour-tagged
// string. Mirrors the recipe-gathering block in set_skills' curses on_redraw
// verbatim (curses path left intact for the A/B; this is a parallel builder, the
// armor_layers precedent). Brown for the current skill's own recipes, gray for
// recipes that merely require it.
std::string nc_skill_recipes_desc( avatar &u, const Skill *currentSkill,
                                   const std::map<skill_id, int> &prof_skills )
{
    SkillLevelMap with_prof_skills = u.get_all_skills();
    for( const auto &sk : prof_skills ) {
        with_prof_skills.mod_skill_level( sk.first, sk.second );
    }
    std::map<std::string, std::vector<std::pair<std::string, int>>> recipes;
    for( const auto &e : recipe_dict ) {
        const auto &r = e.second;
        if( r.has_flag( "SECRET" ) ) {
            continue;
        }
        auto req_skill = r.required_skills.find( currentSkill->ident() );
        int skill = req_skill != r.required_skills.end() ? req_skill->second : 0;
        bool would_autolearn_recipe =
            recipe_dict.all_autolearn().contains( &r ) &&
            with_prof_skills.meets_skill_requirements( r.autolearn_requirements );
        if( !would_autolearn_recipe && !r.never_learn &&
            ( r.skill_used == currentSkill->ident() || skill > 0 ) &&
            with_prof_skills.has_recipe_requirements( r ) ) {
            recipes[r.skill_used->name()].emplace_back(
                r.result_name( /*decorated=*/true ),
                ( skill > 0 ) ? skill : r.difficulty );
        }
    }
    std::string rec_disp;
    for( auto &elem : recipes ) {
        std::sort( elem.second.begin(), elem.second.end(),
                   []( const std::pair<std::string, int> &lhs,
        const std::pair<std::string, int> &rhs ) {
            return localized_compare( std::make_pair( lhs.second, lhs.first ),
                                      std::make_pair( rhs.second, rhs.first ) );
        } );
        const std::string rec_temp = enumerate_as_string( elem.second.begin(), elem.second.end(),
        []( const std::pair<std::string, int> &rec ) {
            return string_format( "%s (%d)", rec.first, rec.second );
        } );
        if( elem.first == currentSkill->name() ) {
            rec_disp = "\n\n" + colorize( rec_temp, c_brown ) + rec_disp;
        } else {
            rec_disp += "\n\n" + colorize( "[" + elem.first + "]\n" + rec_temp, c_light_gray );
        }
    }
    rec_disp = currentSkill->description() + rec_disp;
    return rec_disp;
}
} // namespace

tab_direction set_skills( avatar &u, points_left &points )
{
    ui_adaptor ui;
    catacurses::window w;
    catacurses::window w_description;
    int iContentHeight = 0;
    const auto init_windows = [&]( ui_adaptor & ui ) {
        iContentHeight = TERMY - 6;
        w = catacurses::newwin( TERMY, TERMX, point_zero );
        w_description = catacurses::newwin( iContentHeight, TERMX - 35, point( 31, 5 ) );
        ui.position_from_window( w );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    auto sorted_skills = Skill::get_skills_sorted_by( []( const Skill & a, const Skill & b ) {
        return localized_compare( std::make_pair( a.display_category(), a.name() ),
                                  std::make_pair( b.display_category(), b.name() ) );
    } );

    skill_displayType_id current_category = skill_displayType_id::NULL_ID();
    // Actual line that the skill takes up.
    int display_line = 0;
    std::vector<std::pair<const Skill *, int>> skill_list;
    for( const Skill *skl : sorted_skills ) {
        if( current_category != skl->display_category() ) {
            current_category = skl->display_category();
            display_line++;
        }
        skill_list.emplace_back( skl, display_line );
        display_line++;
    }

    const int num_skills = skill_list.size();
    int cur_pos = 0;
    const Skill *currentSkill = skill_list[cur_pos].first;
    int selected = 0;

    input_context ctxt( "NEW_CHAR_SKILLS" );
    ctxt.register_cardinal();
    ctxt.register_action( "SCROLL_DOWN" );
    ctxt.register_action( "SCROLL_UP" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "RANDOMIZE" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "QUIT" );
    // Required for the navigator/card clicks to reach this loop at all.
    // MOUSE_LEFT binds to action id SELECT (keybindings.json:1175). An UNREGISTERED
    // mouse action resolves to CATA_ERROR, and input.cpp:894-897 `continue`s on it
    // BEFORE the registered_any_input check at :912 — so ANY_INPUT cannot rescue a
    // mouse event, only registering the action it maps to can. Without this the
    // click callback fires but handle_input() never returns, leaving the loop parked
    // until an unrelated keypress, which then got hijacked into a step change.
    ctxt.register_action( "SELECT" );

    std::map<skill_id, int> prof_skills;
    const auto &pskills = u.prof->skills();

    std::copy( pskills.begin(), pskills.end(),
               std::inserter( prof_skills, prof_skills.begin() ) );


    // RmlUi render path (render-only; keyboard owns nav/inc/dec/scroll below).
    auto data = std::make_unique<nc_skills_session>();
    rml_doc rml;
    // Set by the arrow click callbacks, consumed by the input loop below. Not a
    // tab_direction: it is translated into an action string so the existing
    // keyboard handling stays the single place navigation is decided.
    int nc_nav = 0;
    int rml_sel_child = -1;       // flattened-row index of the cursor skill
    bool rml_scroll_pending = false; // follow the keyboard cursor in the list
    const auto sync_rml = [&]() {
        if( !data->handle ) {
            return;
        }
        data->tabs = build_nc_char_tabs<nc_skills_tab>( 6 );  // SKILLS tab active
        data->shell = fill_nc_shell( 6, ctxt );
        data->points_rml = cata_text_to_rml( nc_points_line( points ) );

        const int cost = skill_increment_cost( u, currentSkill->ident() );
        const int level = u.get_skill_level( currentSkill->ident() );
        const int upgrade_levels = level == 0 ? 2 : 1;
        const std::string upgrade_levels_s = string_format(
                vgettext( "%d level", "%d levels", upgrade_levels ), upgrade_levels );
        const nc_color ccol = points.skill_points_left() >= cost ? COL_SKILL_USED : c_light_red;
        data->cost_rml = cata_text_to_rml( colorize( string_format(
                                               vgettext( "Upgrading %s by %s costs %d point",
                                                   "Upgrading %s by %s costs %d points", cost ),
                                               currentSkill->name(), upgrade_levels_s, cost ), ccol ) );

        data->rows.clear();
        rml_sel_child = -1;
        skill_displayType_id cat = skill_displayType_id::NULL_ID();
        for( int i = 0; i < num_skills; i++ ) {
            const Skill *sk = skill_list[i].first;
            const skill_displayType_id &dt = sk->display_category();
            if( cat != dt ) {
                cat = dt;
                nc_skill_row h;
                h.is_header = true;
                h.text_rml = cata_text_to_rml( colorize( dt->display_string(), c_yellow ) );
                data->rows.push_back( h );
            }
            const int lvl = u.get_skill_level( sk->ident() );
            const nc_color col = lvl > 0 ? COL_SKILL_USED : c_light_gray;
            std::string line = colorize( sk->name(), col );
            if( lvl > 0 ) {
                line += colorize( string_format( " (%d)", lvl ), col );
            }
            for( const auto &ps : u.prof->skills() ) {
                if( ps.first == sk->ident() ) {
                    line += colorize( string_format( " (+%d)",
                                                     static_cast<int>( ps.second ) ), c_white );
                    break;
                }
            }
            nc_skill_row r;
            r.text_rml = cata_text_to_rml( line );
            r.selected = ( i == cur_pos );
            if( i == cur_pos ) {
                rml_sel_child = static_cast<int>( data->rows.size() );
            }
            data->rows.push_back( r );
        }

        data->desc_rml = cata_text_to_rml( nc_skill_recipes_desc( u, currentSkill, prof_skills ) );

        data->handle.DirtyVariable( "tabs" );
        dirty_nc_shell( data->handle );
        data->handle.DirtyVariable( "points_rml" );
        data->handle.DirtyVariable( "cost_rml" );
        data->handle.DirtyVariable( "rows" );
        data->handle.DirtyVariable( "desc_rml" );

        // Follow the keyboard cursor with native scroll (keyboard nav only).
        if( rml_scroll_pending && rml_sel_child >= 0 ) {
            rml_scroll_pending = false;
            if( Rml::Element *list = rml.document()->GetElementById( "nc-skill-list" ) ) {
                if( rml_sel_child < list->GetNumChildren() ) {
                    list->GetChild( rml_sel_child )->ScrollIntoView(
                        Rml::ScrollIntoViewOptions( Rml::ScrollAlignment::Nearest ) );
                }
            }
        }
    };
    // SCROLL_UP/DOWN scroll the description pane (vs the curses fold offset).
    const auto scroll_desc = [&]( int dir ) {
        if( !rml ) {
            return;
        }
        if( Rml::Element *e = rml.document()->GetElementById( "nc-skill-desc" ) ) {
            const float page = e->GetClientHeight();
            const float maxtop = std::max( 0.0f, e->GetScrollHeight() - page );
            e->SetScrollTop( std::clamp( e->GetScrollTop() + dir * page * 0.15f, 0.0f, maxtop ) );
        }
    };

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    rml.open( newcharacter_rmlui_enabled(), "newcharskills", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        register_nc_skills_rml_types( c );
        c.Bind( "tabs", &data->tabs );
        bind_nc_shell( c, data->shell );
        // Arrow clicks are translated into the SAME action strings the keyboard
        // produces, so each step's existing PREV_TAB/NEXT_TAB handling — including
        // the "Return to main menu?" confirm on step 0 — is reused unchanged.
        c.BindEventCallback( "on_prev",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = -1; } );
        c.BindEventCallback( "on_next",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = 1; } );
        c.Bind( "points_rml", &data->points_rml );
        c.Bind( "cost_rml", &data->cost_rml );
        c.Bind( "rows", &data->rows );
        c.Bind( "desc_rml", &data->desc_rml );
        data->handle = c.GetModelHandle();
    } );
    if( rml_doc_unavailable( rml, _( "Character creation (SKILLS tab)" ) ) ) {
        return tab_direction::QUIT;
    }

    do {
        ui_manager::redraw();
        nc_nav = 0;
        std::string action = ctxt.handle_input();
        if( nc_nav != 0 ) {
            action = nc_nav < 0 ? "PREV_TAB" : "NEXT_TAB";
        }
        if( action == "DOWN" ) {
            cur_pos = modulo( cur_pos + 1, num_skills );
            currentSkill = skill_list[cur_pos].first;
            rml_scroll_pending = true;
        } else if( action == "UP" ) {
            cur_pos = modulo( cur_pos - 1, num_skills );
            currentSkill = skill_list[cur_pos].first;
            rml_scroll_pending = true;
        } else if( action == "RANDOMIZE" ) {
            cur_pos = modulo( rng( 0, num_skills - 1 ), num_skills );
            rml_scroll_pending = true;
        } else if( action == "LEFT" ) {
            const int level = u.get_skill_level( currentSkill->ident() );
            if( level > 0 ) {
                // For balance reasons, increasing a skill from level 0 gives 1 extra level for free, but
                // decreasing it from level 2 forfeits the free extra level (thus changes it to 0)
                u.mod_skill_level( currentSkill->ident(), level == 2 ? -2 : -1 );
                // Done *after* the decrementing to get the original cost for incrementing back.
                points.skill_points += skill_increment_cost( u, currentSkill->ident() );
            }
        } else if( action == "RIGHT" ) {
            const int level = u.get_skill_level( currentSkill->ident() );
            if( level < MAX_SKILL ) {
                points.skill_points -= skill_increment_cost( u, currentSkill->ident() );
                // For balance reasons, increasing a skill from level 0 gives 1 extra level for free
                u.mod_skill_level( currentSkill->ident(), level == 0 ? +2 : +1 );
            }
        } else if( action == "SCROLL_DOWN" ) {
            if( rml ) {
                scroll_desc( +1 );
            } else {
                selected++;
            }
        } else if( action == "SCROLL_UP" ) {
            if( rml ) {
                scroll_desc( -1 );
            } else {
                selected--;
            }
        } else if( action == "PREV_TAB" ) {
            return tab_direction::BACKWARD;
        } else if( action == "NEXT_TAB" ) {
            return tab_direction::FORWARD;
        } else if( action == "QUIT" && query_yn( _( "Return to main menu?" ) ) ) {
            return tab_direction::QUIT;
        }
    } while( true );
}

namespace
{
struct nc_scen_tab {
    Rml::String name_rml;
    Rml::String icon_dec;   //< placeholder tab glyph
    bool selected = false;
    bool done = false;   //< step already passed
};
struct nc_scen_row {
    Rml::String text_rml;
    bool selected = false;
};
struct nc_scen_session {
    Rml::Vector<nc_scen_tab> tabs;
    nc_shell shell;
    Rml::String points_rml;
    Rml::String cost_rml;
    Rml::Vector<nc_scen_row> rows;
    Rml::String desc_rml;
    Rml::String info_rml;
    Rml::String sort_rml;
    Rml::String filter_rml;
    Rml::DataModelHandle handle;
};

bool g_nc_scen_types_registered = false;

void register_nc_scen_rml_types( Rml::DataModelConstructor &c )
{
    if( g_nc_scen_types_registered ) {
        return;
    }
    Rml::StructHandle<nc_scen_tab> th = c.RegisterStruct<nc_scen_tab>();
    th.RegisterMember( "name_rml", &nc_scen_tab::name_rml );
    th.RegisterMember( "icon_dec", &nc_scen_tab::icon_dec );
    th.RegisterMember( "selected", &nc_scen_tab::selected );
    th.RegisterMember( "done", &nc_scen_tab::done );
    c.RegisterArray<Rml::Vector<nc_scen_tab>>();
    Rml::StructHandle<nc_scen_row> rh = c.RegisterStruct<nc_scen_row>();
    rh.RegisterMember( "text_rml", &nc_scen_row::text_rml );
    rh.RegisterMember( "selected", &nc_scen_row::selected );
    c.RegisterArray<Rml::Vector<nc_scen_row>>();
    g_nc_scen_types_registered = true;
}
} // namespace

tab_direction set_scenario( avatar &u, points_left &points,
                            const tab_direction direction )
{
    int cur_id = 0;
    tab_direction retval = tab_direction::NONE;
    int iContentHeight = 0;

    ui_adaptor ui;
    catacurses::window w;
    catacurses::window w_description;
    catacurses::window w_sorting;
    catacurses::window w_profession;
    catacurses::window w_location;
    catacurses::window w_vehicle;
    catacurses::window w_flags;
    const auto init_windows = [&]( ui_adaptor & ui ) {
        iContentHeight = TERMY - 10;
        w = catacurses::newwin( TERMY, TERMX, point_zero );
        w_description = catacurses::newwin( 4, TERMX - 2, point( 1, TERMY - 5 ) );
        w_sorting = catacurses::newwin( 2, ( TERMX / 2 ) - 1, point( TERMX / 2, 5 ) );
        w_profession = catacurses::newwin( 4, ( TERMX / 2 ) - 1, point( TERMX / 2, 7 ) );
        w_location = catacurses::newwin( 3, ( TERMX / 2 ) - 1, point( TERMX / 2, 11 ) );
        w_vehicle = catacurses::newwin( 3, ( TERMX / 2 ) - 1, point( TERMX / 2, 14 ) );
        // 11 = 2 + 4 + 3 + 3, so we use rest of space for flags
        w_flags = catacurses::newwin( iContentHeight - 14, ( TERMX / 2 ) - 1,
                                      point( TERMX / 2, 17 ) );
        ui.position_from_window( w );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    input_context ctxt( "NEW_CHAR_SCENARIOS" );
    ctxt.register_cardinal();
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "SORT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "RANDOMIZE" );
    ctxt.register_action( "FILTER" );
    ctxt.register_action( "QUIT" );
    // Required for the navigator/card clicks to reach this loop at all.
    // MOUSE_LEFT binds to action id SELECT (keybindings.json:1175). An UNREGISTERED
    // mouse action resolves to CATA_ERROR, and input.cpp:894-897 `continue`s on it
    // BEFORE the registered_any_input check at :912 — so ANY_INPUT cannot rescue a
    // mouse event, only registering the action it maps to can. Without this the
    // click callback fires but handle_input() never returns, leaving the loop parked
    // until an unrelated keypress, which then got hijacked into a step change.
    ctxt.register_action( "SELECT" );

    bool recalc_scens = true;
    int scens_length = 0;
    std::string filterstring;
    std::vector<const scenario *> sorted_scens;

    if( direction == tab_direction::BACKWARD ) {
        points.skill_points += u.prof->point_cost();
    }

    // RmlUi render path (render-only; keyboard owns nav/confirm/sort/filter below).
    auto data = std::make_unique<nc_scen_session>();
    rml_doc rml;
    // Set by the arrow click callbacks, consumed by the input loop below. Not a
    // tab_direction: it is translated into an action string so the existing
    // keyboard handling stays the single place navigation is decided.
    int nc_nav = 0;
    bool rml_scroll_pending = false;
    const auto sync_rml = [&]() {
        if( !data->handle ) {
            return;
        }
        data->tabs = build_nc_char_tabs<nc_scen_tab>( 1 );  // SCENARIO tab active
        data->shell = fill_nc_shell( 1, ctxt );
        const bool valid = cur_id >= 0 && static_cast<size_t>( cur_id ) < sorted_scens.size();

        std::string pmsg = nc_points_line( points );
        if( valid ) {
            const int netPointCost = sorted_scens[cur_id]->point_cost() - g->scen->point_cost();
            if( netPointCost > 0 ) {
                pmsg += colorize( string_format( " (-%d)", std::abs( netPointCost ) ), c_red );
            } else if( netPointCost < 0 ) {
                pmsg += colorize( string_format( " (+%d)", std::abs( netPointCost ) ), c_green );
            }
        }
        data->points_rml = cata_text_to_rml( pmsg );

        if( valid ) {
            const scenario *s = sorted_scens[cur_id];
            const bool can_pick = s->can_pick( *g->scen, points.skill_points_left() );
            int pts = s->point_cost();
            const bool neg = pts < 0;
            if( neg ) {
                pts *= -1;
            }
            const std::string msg = neg
                                    ? vgettext( "Scenario %1$s earns %2$d point",
                                                "Scenario %1$s earns %2$d points", pts )
                                    : vgettext( "Scenario %1$s costs %2$d point",
                                                "Scenario %1$s cost %2$d points", pts );
            data->cost_rml = cata_text_to_rml( colorize( string_format( msg,
                                               s->gender_appropriate_name( u.male ), pts ),
                                               can_pick ? c_green : c_light_red ) );

            std::string desc;
            if( s->has_flag( "CITY_START" ) && !scenario_sorter.cities_enabled ) {
                desc = colorize(
                           _( "This scenario is not available in this world due to city size settings." ),
                           c_red ) + "\n" + colorize( s->description( u.male ), c_green );
            } else {
                desc = colorize( s->description( u.male ), c_green );
            }
            data->desc_rml = cata_text_to_rml( desc );

            std::string info;
            info += colorize( _( "Professions:" ), COL_HEADER );
            info += string_format( _( "\n%s" ), s->prof_count_str() );
            info += _( ", default:\n" );
            auto psorter = profession_sorter;
            psorter.sort_by_points = true;
            const auto permitted = s->permitted_professions();
            const auto default_prof = *std::min_element( permitted.begin(), permitted.end(), psorter );
            const int prof_points = default_prof->point_cost();
            info += default_prof->gender_appropriate_name( u.male );
            if( prof_points > 0 ) {
                info += colorize( string_format( " (-%d)", prof_points ), c_red );
            } else if( prof_points < 0 ) {
                info += colorize( string_format( " (+%d)", -prof_points ), c_green );
            }
            info += "\n\n";
            info += colorize( _( "Scenario Location:" ), COL_HEADER );
            info += "\n";
            info += string_format( _( "%s (%d locations, %d variants)" ), s->start_name(),
                                   s->start_location_count(), s->start_location_targets_count() );
            info += "\n\n";
            info += colorize( _( "Scenario Vehicle:" ), COL_HEADER );
            info += "\n";
            // A header with nothing under it reads as a rendering fault rather than
            // as "empty", so name the empty state.
            info += s->vehicle() ? s->vehicle()->name : std::string( _( "None" ) );
            info += "\n\n";
            info += colorize( _( "Scenario Flags:" ), COL_HEADER );
            info += "\n";
            std::vector<std::string> flags;
            if( s->has_flag( "SPR_START" ) ) {
                flags.emplace_back( _( "Spring start" ) );
            } else if( s->has_flag( "SUM_START" ) ) {
                flags.emplace_back( _( "Summer start" ) );
            } else if( s->has_flag( "AUT_START" ) ) {
                flags.emplace_back( _( "Autumn start" ) );
            } else if( s->has_flag( "WIN_START" ) ) {
                flags.emplace_back( _( "Winter start" ) );
            } else if( s->has_flag( "SUM_ADV_START" ) ) {
                flags.emplace_back( _( "Next summer start" ) );
            }
            if( s->has_flag( "INFECTED" ) ) {
                flags.emplace_back( _( "Infected player" ) );
            }
            if( s->has_flag( "BAD_DAY" ) ) {
                flags.emplace_back( _( "Drunk and sick player" ) );
            }
            if( s->has_flag( "FIRE_START" ) ) {
                flags.emplace_back( _( "Fire nearby" ) );
            }
            if( s->has_flag( "SUR_START" ) ) {
                flags.emplace_back( _( "Zombies nearby" ) );
            }
            if( s->has_flag( "HELI_CRASH" ) ) {
                flags.emplace_back( _( "Various limb wounds" ) );
            }
            if( get_option<std::string>( "STARTING_NPC" ) == "scenario" &&
                s->has_flag( "LONE_START" ) ) {
                flags.emplace_back( _( "No starting NPC" ) );
            }
            if( s->has_flag( "BORDERED" ) ) {
                flags.emplace_back( _( "Starting location is bordered by an immense wall" ) );
            }
            if( flags.empty() ) {
                flags.emplace_back( _( "None" ) );
            }
            for( const std::string &f : flags ) {
                info += f + "\n";
            }
            data->info_rml = cata_text_to_rml( info );
        } else {
            data->cost_rml.clear();
            data->desc_rml.clear();
            data->info_rml.clear();
        }

        data->rows.clear();
        for( int i = 0; i < static_cast<int>( sorted_scens.size() ); i++ ) {
            const scenario *s = sorted_scens[i];
            nc_color col;
            if( g->scen != s ) {
                if( s->has_flag( "CITY_START" ) && !scenario_sorter.cities_enabled ) {
                    col = c_dark_gray;
                } else {
                    col = c_light_gray;
                }
            } else {
                col = COL_SKILL_USED;
            }
            nc_scen_row r;
            r.text_rml = cata_text_to_rml( colorize( s->gender_appropriate_name( u.male ), col ) );
            r.selected = ( i == cur_id );
            data->rows.push_back( r );
        }

        data->sort_rml = cata_text_to_rml( string_format(
                                               _( "<color_white>Sort by:</color> %1$s (Press <color_light_green>%2$s</color> to change sorting.)" ),
                                               scenario_sorter.sort_by_points ? _( "points" ) : _( "name" ),
                                               ctxt.get_desc( "SORT" ) ) );
        data->filter_rml = cata_text_to_rml( string_format( "<%s>",
                                             filterstring.empty() ? _( "no filter" ) : filterstring ) );

        data->handle.DirtyVariable( "tabs" );
        dirty_nc_shell( data->handle );
        data->handle.DirtyVariable( "points_rml" );
        data->handle.DirtyVariable( "cost_rml" );
        data->handle.DirtyVariable( "rows" );
        data->handle.DirtyVariable( "desc_rml" );
        data->handle.DirtyVariable( "info_rml" );
        data->handle.DirtyVariable( "sort_rml" );
        data->handle.DirtyVariable( "filter_rml" );

        if( rml_scroll_pending && valid ) {
            rml_scroll_pending = false;
            if( Rml::Element *list = rml.document()->GetElementById( "nc-scen-list" ) ) {
                if( cur_id < list->GetNumChildren() ) {
                    list->GetChild( cur_id )->ScrollIntoView(
                        Rml::ScrollIntoViewOptions( Rml::ScrollAlignment::Nearest ) );
                }
            }
        }
    };

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    rml.open( newcharacter_rmlui_enabled(), "newcharscenario", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        register_nc_scen_rml_types( c );
        c.Bind( "tabs", &data->tabs );
        bind_nc_shell( c, data->shell );
        // Arrow clicks are translated into the SAME action strings the keyboard
        // produces, so each step's existing PREV_TAB/NEXT_TAB handling — including
        // the "Return to main menu?" confirm on step 0 — is reused unchanged.
        c.BindEventCallback( "on_prev",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = -1; } );
        c.BindEventCallback( "on_next",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = 1; } );
        c.Bind( "points_rml", &data->points_rml );
        c.Bind( "cost_rml", &data->cost_rml );
        c.Bind( "rows", &data->rows );
        c.Bind( "desc_rml", &data->desc_rml );
        c.Bind( "info_rml", &data->info_rml );
        c.Bind( "sort_rml", &data->sort_rml );
        c.Bind( "filter_rml", &data->filter_rml );
        data->handle = c.GetModelHandle();
    } );
    if( rml_doc_unavailable( rml, _( "Character creation (SCENARIO tab)" ) ) ) {
        return tab_direction::QUIT;
    }

    do {
        if( recalc_scens ) {
            sorted_scens.clear();
            auto &wopts = world_generator->active_world->info->WORLD_OPTIONS;
            for( const auto &scen : scenario::get_all() ) {
                if( scen.scen_is_blacklisted() ) {
                    continue;
                }
                if( !lcmatch( scen.gender_appropriate_name( u.male ), filterstring ) ) {
                    continue;
                }
                sorted_scens.push_back( &scen );
            }
            scens_length = sorted_scens.size();
            if( scens_length == 0 ) {
                popup( _( "Nothing found." ) ); // another case of black box in tiles
                filterstring.clear();
                continue;
            }

            // Sort scenarios by points.
            // scenario_display_sort() keeps "Evacuee" at the top.
            scenario_sorter.male = u.male;
            scenario_sorter.cities_enabled = wopts["CITY_SIZE"].getValue() != "0";
            std::stable_sort( sorted_scens.begin(), sorted_scens.end(), scenario_sorter );

            // If city size is 0 but the current scenario requires cities reset the scenario
            if( !scenario_sorter.cities_enabled && g->scen->has_flag( "CITY_START" ) ) {
                reset_scenario( u, sorted_scens[0] );
                points.init_from_options();
                points.skill_points -= sorted_scens[cur_id]->point_cost();
            }

            // Select the current scenario, if possible.
            for( int i = 0; i < scens_length; ++i ) {
                if( sorted_scens[i]->ident() == g->scen->ident() ) {
                    cur_id = i;
                    break;
                }
            }
            if( cur_id > scens_length - 1 ) {
                cur_id = 0;
            }

            recalc_scens = false;
        }

        ui_manager::redraw();
        nc_nav = 0;
        std::string action = ctxt.handle_input();
        if( nc_nav != 0 ) {
            action = nc_nav < 0 ? "PREV_TAB" : "NEXT_TAB";
        }
        if( action == "DOWN" ) {
            cur_id++;
            if( cur_id > scens_length - 1 ) {
                cur_id = 0;
            }
            rml_scroll_pending = true;
        } else if( action == "UP" ) {
            cur_id--;
            if( cur_id < 0 ) {
                cur_id = scens_length - 1;
            }
            rml_scroll_pending = true;
        } else if( action == "RANDOMIZE" ) {
            cur_id = rng( 0, scens_length - 1 );
            rml_scroll_pending = true;
        } else if( action == "CONFIRM" ) {
            if( sorted_scens[cur_id]->has_flag( "CITY_START" ) && !scenario_sorter.cities_enabled ) {
                continue;
            }
            reset_scenario( u, sorted_scens[cur_id] );
            points.init_from_options();
            points.skill_points -= sorted_scens[cur_id]->point_cost();
        } else if( action == "PREV_TAB" ) {
            retval = tab_direction::BACKWARD;
        } else if( action == "NEXT_TAB" ) {
            retval = tab_direction::FORWARD;
        } else if( action == "SORT" ) {
            scenario_sorter.sort_by_points = !scenario_sorter.sort_by_points;
            recalc_scens = true;
        } else if( action == "FILTER" ) {
            string_input_popup()
            .title( _( "Search:" ) )
            .width( 60 )
            .description( _( "Search by scenario name." ) )
            .edit( filterstring );
            recalc_scens = true;
        } else if( action == "QUIT" && query_yn( _( "Return to main menu?" ) ) ) {
            retval = tab_direction::QUIT;
        }
    } while( retval == tab_direction::NONE );

    return retval;
}

namespace
{
struct nc_desc_tab {
    Rml::String name_rml;
    Rml::String icon_dec;   //< placeholder tab glyph
    bool selected = false;
    bool done = false;   //< step already passed
};
/// One rendered row of an OVERVIEW pane.
///
/// `label` and `value` are separate spans so the label can carry its trailing space
/// INSIDE its own colour run — `colorize(label) + " " + colorize(value)` puts U+0020
/// between two spans, where RmlUi trims it at parse time (this is what rendered
/// `Traits:None!` and `Wielded:None!`). Either may be empty: a plain list entry is
/// value-only, a bare group heading is label-only.
struct nc_desc_row {
    Rml::String label_rml;
    Rml::String value_rml;
    bool heading = false;   //< styled as a group heading, not a data row
};

/// One OVERVIEW column. `title_rml` may be empty (the misc column has no title of
/// its own and opens on its first heading). `icon_dec` is the section's placeholder
/// glyph — see plans/charcreation-visual-overhaul.md for the art hand-off table.
struct nc_desc_pane {
    Rml::String title_rml;
    Rml::String icon_dec;
    Rml::Vector<nc_desc_row> rows;
};

struct nc_desc_session {
    Rml::Vector<nc_desc_tab> tabs;
    nc_shell shell;
    Rml::String points_rml;
    Rml::String name_rml;
    Rml::String gender_rml;
    Rml::String height_rml;
    Rml::String age_rml;
    Rml::String location_rml;
    Rml::String scenario_rml;
    Rml::String profession_rml;
    Rml::Vector<nc_desc_pane> panes;
    Rml::String guide_rml;
    Rml::DataModelHandle handle;
};

bool g_nc_desc_types_registered = false;

void register_nc_desc_rml_types( Rml::DataModelConstructor &c )
{
    if( g_nc_desc_types_registered ) {
        return;
    }
    Rml::StructHandle<nc_desc_tab> th = c.RegisterStruct<nc_desc_tab>();
    th.RegisterMember( "name_rml", &nc_desc_tab::name_rml );
    th.RegisterMember( "icon_dec", &nc_desc_tab::icon_dec );
    th.RegisterMember( "selected", &nc_desc_tab::selected );
    th.RegisterMember( "done", &nc_desc_tab::done );
    c.RegisterArray<Rml::Vector<nc_desc_tab>>();

    Rml::StructHandle<nc_desc_row> rh = c.RegisterStruct<nc_desc_row>();
    rh.RegisterMember( "label_rml", &nc_desc_row::label_rml );
    rh.RegisterMember( "value_rml", &nc_desc_row::value_rml );
    rh.RegisterMember( "heading", &nc_desc_row::heading );
    c.RegisterArray<Rml::Vector<nc_desc_row>>();

    Rml::StructHandle<nc_desc_pane> ph = c.RegisterStruct<nc_desc_pane>();
    ph.RegisterMember( "title_rml", &nc_desc_pane::title_rml );
    ph.RegisterMember( "icon_dec", &nc_desc_pane::icon_dec );
    ph.RegisterMember( "rows", &nc_desc_pane::rows );
    c.RegisterArray<Rml::Vector<nc_desc_pane>>();

    g_nc_desc_types_registered = true;
}

/// Accumulates an OVERVIEW column. Every append keeps the label's trailing space
/// inside the label's own colour run, so nothing welds.
class pane_builder
{
    public:
        pane_builder( const std::string &title, const nc_color &title_col, unsigned icon_seed ) {
            if( !title.empty() ) {
                p.title_rml = cata_text_to_rml( colorize( title, title_col ) );
            }
            p.icon_dec = nc_icon_dec( icon_seed, 20, false );
        }

        /// A group heading, optionally with an inline value ("Vehicle:  None!").
        void heading( const std::string &label, const nc_color &label_col,
                      const std::string &value = std::string(),
                      const nc_color &value_col = c_white ) {
            nc_desc_row r;
            r.heading = true;
            r.label_rml = cata_text_to_rml( nc_label( label, label_col ) );
            if( !value.empty() ) {
                r.value_rml = cata_text_to_rml( colorize( value, value_col ) );
            }
            p.rows.push_back( r );
        }

        /// A `label: value` data row.
        void field( const std::string &label, const nc_color &label_col,
                    const std::string &value, const nc_color &value_col ) {
            nc_desc_row r;
            r.label_rml = cata_text_to_rml( nc_label( label, label_col ) );
            r.value_rml = cata_text_to_rml( colorize( value, value_col ) );
            p.rows.push_back( r );
        }

        /// A bare list entry.
        void value( const std::string &value, const nc_color &value_col ) {
            nc_desc_row r;
            r.value_rml = cata_text_to_rml( colorize( value, value_col ) );
            p.rows.push_back( r );
        }

        /// The "None!" marker, as its own row when a group has no inline value.
        void none() { value( _( "None!" ), c_light_red ); }

        auto take() -> nc_desc_pane { return std::move( p ); }

    private:
        nc_desc_pane p;
};
} // namespace

tab_direction set_description( avatar &you, const bool allow_reroll,
                               points_left &points )
{
    static constexpr int RANDOM_START_LOC_ENTRY = INT_MIN;
    const std::string RANDOM_START_LOC_TEXT_TEMPLATE =
        _( "<color_red>* Random location *</color> (<color_white>%d</color> variants)" );
    const std::string START_LOC_TEXT_TEMPLATE = _( "%s (<color_white>%d</color> variants)" );

    ui_adaptor ui;
    catacurses::window w;
    catacurses::window w_name;
    catacurses::window w_gender;
    catacurses::window w_location;
    catacurses::window w_stats;
    catacurses::window w_traits;
    catacurses::window w_bionics;
    catacurses::window w_misc;
    catacurses::window w_gear;
    catacurses::window w_scenario;
    catacurses::window w_profession;
    catacurses::window w_skills;
    catacurses::window w_guide;
    catacurses::window w_height;
    catacurses::window w_age;

    character_preview_window character_preview;
    character_preview.init( &you );
    const bool use_character_preview = get_option<bool>( "USE_CHARACTER_PREVIEW" );

    const auto init_windows = [&]( ui_adaptor & ui ) {
        // Row 1
        w = catacurses::newwin( TERMY, TERMX, point_zero );
        w_name = catacurses::newwin( 3, 42, point( 2, 5 ) );
        w_gender = catacurses::newwin( 2, 33, point( 46, 5 ) );
        w_height = catacurses::newwin( 1, 20, point( 80, 5 ) );
        w_location = catacurses::newwin( 2, 60, point( 100, 5 ) );
        w_scenario = catacurses::newwin( 1, std::max( 1, TERMX - 161 ), point( 160, 5 ) );
        w_profession = catacurses::newwin( 1, std::max( 1, TERMX - 161 ), point( 160, 6 ) );

        // Row 2
        w_age = catacurses::newwin( 1, 12, point( 80, 6 ) );

        // Big Row
        w_stats = catacurses::newwin( 6, 20, point( 2, 9 ) );
        w_skills = catacurses::newwin( std::max( 1, TERMY - 17 ), 25, point( 2, 16 ) );
        w_traits = catacurses::newwin( std::max( 1, TERMY - 10 ), 30, point( 28, 9 ) );
        w_bionics = catacurses::newwin( std::max( 1, TERMY - 10 ), 30, point( 59, 9 ) );
        w_misc = catacurses::newwin( std::max( 1, TERMY - 10 ), 30, point( 90, 9 ) );
        w_gear = catacurses::newwin( std::max( 1, TERMY - 10 ), std::max( 1, TERMX - 122 ), point( 121,
                                     9 ) );

        // Very bottom Row
        w_guide = catacurses::newwin( 6, std::max( 1, TERMX - 3 ), point( 2, TERMY - 7 ) );

        if( use_character_preview ) {
            nc_prepare_preview( character_preview );
        }

        ui.position_from_window( w );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );


    input_context ctxt( "NEW_CHAR_DESCRIPTION" );
    ctxt.register_cardinal();
    ctxt.register_action( "SAVE_TEMPLATE" );
    ctxt.register_action( "RANDOMIZE_CHAR_DESCRIPTION" );
    ctxt.register_action( "CHANGE_GENDER" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "CHOOSE_LOCATION" );
    ctxt.register_action( "REROLL_CHARACTER" );
    ctxt.register_action( "REROLL_CHARACTER_WITH_SCENARIO" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    // Required for the navigator/card clicks to reach this loop at all.
    // MOUSE_LEFT binds to action id SELECT (keybindings.json:1175). An UNREGISTERED
    // mouse action resolves to CATA_ERROR, and input.cpp:894-897 `continue`s on it
    // BEFORE the registered_any_input check at :912 — so ANY_INPUT cannot rescue a
    // mouse event, only registering the action it maps to can. Without this the
    // click callback fires but handle_input() never returns, leaving the loop parked
    // until an unrelated keypress, which then got hijacked into a step change.
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "zoom_in" );
    ctxt.register_action( "zoom_out" );
    ctxt.register_action( "TOGGLE_CHARACTER_PREVIEW_CLOTHES" );

    uilist select_location;
    select_location.text = _( "Select a starting location." );
    int offset = 1;
    const std::string random_start_location_text = string_format( RANDOM_START_LOC_TEXT_TEMPLATE,
        g->scen->start_location_targets_count() );
    uilist_entry entry_random_start_location( RANDOM_START_LOC_ENTRY, true, -1,
            random_start_location_text );
    select_location.entries.emplace_back( entry_random_start_location );
    for( const auto &loc : start_locations::get_all() ) {
        if( g->scen->allowed_start( loc.id ) ) {
            uilist_entry entry( loc.id.id().to_i(), true, -1,
                                string_format( START_LOC_TEXT_TEMPLATE, loc.name(), loc.targets_count() ) );

            select_location.entries.emplace_back( entry );

            if( !you.random_start_location &&
                loc.id.id() == you.start_location.id() ) {
                select_location.selected = offset;
            }
            offset++;
        }
    }
    if( you.random_start_location ) {
        select_location.selected = 0;
    }
    select_location.setup();
    if( MAP_SHARING::isSharing() ) {
        you.name = MAP_SHARING::getUsername();  // set the current username as default character name
    } else if( !get_option<std::string>( "DEF_CHAR_NAME" ).empty() ) {
        you.name = get_option<std::string>( "DEF_CHAR_NAME" );
    }

    char_creation::description_selector current_selector = char_creation::NAME;

    bool no_name_entered = false;

    // RmlUi render path (render-only; keyboard still owns nav/edit/confirm below).
    auto data = std::make_unique<nc_desc_session>();
    rml_doc rml;
    // Set by the arrow click callbacks, consumed by the input loop below. Not a
    // tab_direction: it is translated into an action string so the existing
    // keyboard handling stays the single place navigation is decided.
    int nc_nav = 0;
    const auto sync_rml = [&]() {
        if( !data->handle ) {
            return;
        }
        data->tabs = build_nc_char_tabs<nc_desc_tab>( 7 );  // OVERVIEW tab active
        data->shell = fill_nc_shell( 7, ctxt );
        data->points_rml = cata_text_to_rml( nc_points_line( points ) );

        // Name (selector-highlighted). value mirrors the curses three-way state.
        {
            const bool sel = current_selector == char_creation::NAME;
            std::string val;
            nc_color val_col = c_white;
            if( no_name_entered ) {
                val = _( "--- NO NAME ENTERED ---" );
            } else if( you.name.empty() ) {
                val = _( "--- RANDOM NAME ---" );
            } else {
                val = you.name;
            }
            data->name_rml = cata_text_to_rml( std::string( sel ? "> " : "  " ) +
                                               nc_label( _( "Name:" ), sel ? c_white : c_light_gray ) +
                                               colorize( val, val_col ) );
        }

        data->gender_rml = cata_text_to_rml(
                               nc_label( _( "Gender:" ), c_light_gray ) +
                               colorize( _( "Male" ), you.male ? c_light_cyan : c_light_gray ) +
                               // Separator in its own colour run so it survives the trim, and
                               // so the two values cannot read as one word ("MaleFemale").
                               colorize( " / ", c_dark_gray ) +
                               colorize( _( "Female" ), you.male ? c_light_gray : c_pink ) );

        {
            const bool sel = current_selector == char_creation::HEIGHT;
            data->height_rml = cata_text_to_rml( std::string( sel ? "> " : "  " ) +
                                                 nc_label( _( "Height:" ), sel ? c_white : c_light_gray ) +
                                                 colorize( string_format( "%d cm", you.base_height() ), c_white ) );
        }
        {
            const bool sel = current_selector == char_creation::AGE;
            data->age_rml = cata_text_to_rml( std::string( sel ? "> " : "  " ) +
                                              nc_label( _( "Age:" ), sel ? c_white : c_light_gray ) +
                                              colorize( string_format( "%d", you.base_age() ), c_white ) );
        }

        {
            const std::string locval = you.random_start_location
                                       ? remove_color_tags( random_start_location_text )
                                       : string_format( remove_color_tags( START_LOC_TEXT_TEMPLATE ),
                                           you.start_location.obj().name(),
                                           you.start_location.obj().targets_count() );
            data->location_rml = cata_text_to_rml(
                                     nc_label( _( "Starting location:" ), c_light_gray ) +
                                     colorize( locval, you.random_start_location ? c_red : c_white ) );
        }

        data->scenario_rml = cata_text_to_rml(
                                 colorize( _( "Scenario: " ), COL_HEADER ) +
                                 colorize( g->scen->gender_appropriate_name( you.male ), c_light_gray ) );
        data->profession_rml = cata_text_to_rml(
                                   colorize( _( "Profession: " ), COL_HEADER ) +
                                   colorize( you.prof->gender_appropriate_name( you.male ), c_light_gray ) );

        // ── The six OVERVIEW columns ──────────────────────────────────────────
        // Built as explicit label/value rows rather than one "\n"-joined string per
        // column: the header then has its own element (so it can carry real
        // typographic weight) and no label can weld to its value.
        data->panes.clear();

        // Stats.
        {
            pane_builder b( _( "Stats:" ), COL_HEADER, 0x0101 );
            b.field( _( "Strength:" ), c_light_gray, string_format( "%d", you.str_max ), c_white );
            b.field( _( "Dexterity:" ), c_light_gray, string_format( "%d", you.dex_max ), c_white );
            b.field( _( "Intelligence:" ), c_light_gray, string_format( "%d", you.int_max ), c_white );
            b.field( _( "Perception:" ), c_light_gray, string_format( "%d", you.per_max ), c_white );
            data->panes.push_back( b.take() );
        }

        // Skills (category-grouped, only levels > 0).
        {
            pane_builder b( _( "Skills:" ), COL_HEADER, 0x0102 );
            auto skillslist = Skill::get_skills_sorted_by( [&]( const Skill & a, const Skill & b2 ) {
                return localized_compare( std::make_pair( a.display_category(), a.name() ),
                                          std::make_pair( b2.display_category(), b2.name() ) );
            } );
            bool has_skills = false;
            skill_displayType_id last_category = skill_displayType_id::NULL_ID();
            for( const Skill *elem : skillslist ) {
                int level = you.get_skill_level( elem->ident() );
                if( points.limit != points_left::TRANSFER ) {
                    for( const auto &prof_skill : you.prof->skills() ) {
                        if( prof_skill.first == elem->ident() ) {
                            level += static_cast<int>( prof_skill.second );
                            break;
                        }
                    }
                }
                if( level > 0 ) {
                    if( last_category != elem->display_category() ) {
                        last_category = elem->display_category();
                        b.heading( elem->display_category()->display_string(), c_yellow );
                    }
                    b.field( string_format( "%s:", elem->name() ), c_light_gray,
                             string_format( "%d", level ), c_white );
                    has_skills = true;
                }
            }
            if( !has_skills ) {
                b.none();
            }
            data->panes.push_back( b.take() );
        }

        // Traits.
        {
            pane_builder b( _( "Traits:" ), COL_HEADER, 0x0103 );
            std::vector<trait_id> current_traits = points.limit == points_left::TRANSFER ?
                                                   you.get_mutations() : you.get_base_traits();
            std::sort( current_traits.begin(), current_traits.end(), trait_display_sort );
            if( current_traits.empty() ) {
                b.none();
            } else {
                for( const trait_id &tr : current_traits ) {
                    b.value( tr->name(), tr->get_display_color() );
                }
            }
            data->panes.push_back( b.take() );
        }

        // Bionics + Spells (one column; curses drew it as two).
        {
            pane_builder b( _( "Bionics:" ), COL_HEADER, 0x0104 );
            std::vector<bionic_id> current_bionics;
            for( const bionic_id &id : you.prof->CBMs() ) {
                current_bionics.push_back( id );
            }
            for( const bionic &bio : you.get_bionic_collection() ) {
                current_bionics.push_back( bio.id );
            }
            std::sort( current_bionics.begin(), current_bionics.end(),
            []( const bionic_id & a, const bionic_id & b2 ) {
                return localized_compare( a->name.translated(), b2->name.translated() );
            } );
            if( current_bionics.empty() ) {
                b.none();
            } else {
                for( const bionic_id &bio : current_bionics ) {
                    b.value( bio->name.translated(), c_white );
                }
            }
            b.heading( _( "Spells:" ), COL_HEADER );
            if( you.prof->spells().empty() ) {
                b.none();
            } else {
                for( const std::pair<spell_id, int> &sp : you.prof->spells() ) {
                    b.value( string_format( _( "%s level %d" ), sp.first->name, sp.second ), c_white );
                }
            }
            data->panes.push_back( b.take() );
        }

        // Vehicle / Companions / Cash / Pets / Addictions — everything the scenario
        // and profession grant that is not a stat, skill, trait, bionic or item.
        {
            // Titled: with an icon and a rule under every other column head, a
            // titleless one reads as a rendering fault rather than as deliberate.
            pane_builder b( _( "Other:" ), COL_HEADER, 0x0105 );
            const vproto_id scen_veh = g->scen->vehicle();
            const vproto_id prof_veh = you.prof->vehicle();
            b.heading( _( "Vehicle:" ), c_white,
                       ( !scen_veh && !prof_veh ) ? _( "None!" ) : std::string(), c_light_red );
            if( scen_veh ) {
                b.value( scen_veh->name, c_white );
            }
            if( prof_veh ) {
                b.value( prof_veh->name, c_white );
            }

            const std::vector<npc_class_id> npcs = you.prof->npcs();
            b.heading( _( "Companions:" ), c_white,
                       npcs.empty() ? _( "None!" ) : std::string(), c_light_red );
            for( const npc_class_id &id : npcs ) {
                if( id.is_valid() ) {
                    b.value( id.obj().get_name(), c_white );
                }
            }

            b.heading( _( "Cash:" ), c_white,
                       !you.prof->starting_cash()
                       ? _( "Random!" )
                       : format_money( you.prof->starting_cash().value() ), c_white );

            b.heading( _( "Pets:" ), c_white,
                       you.prof->pets().empty() ? _( "None!" ) : std::string(), c_light_red );
            for( const mtype_id &id : you.prof->pets() ) {
                if( id.is_valid() ) {
                    monster pet( id );
                    b.value( pet.get_name(), c_white );
                }
            }

            b.heading( _( "Addictions:" ), c_white,
                       you.prof->addictions().empty() ? _( "None!" ) : std::string(), c_light_red );
            for( addiction &addict : you.prof->addictions() ) {
                b.value( addiction_name( addict ), c_white );
            }
            data->panes.push_back( b.take() );
        }

        // Items, split into wielded / worn / inventory.
        {
            pane_builder b( _( "Items:" ), c_white, 0x0106 );
            const auto prof_items = you.prof->items( you.male, you.get_mutations() );
            if( prof_items.empty() ) {
                b.none();
            } else {
                std::vector<std::string> wielded;
                std::vector<std::string> worn;
                std::vector<std::string> inventory;
                for( const auto &it : prof_items ) {
                    if( it->has_flag( json_flag_no_auto_equip ) ) {
                        inventory.push_back( it->display_name() );
                    } else if( it->has_flag( json_flag_auto_wield ) ) {
                        wielded.push_back( it->display_name() );
                    } else if( it->is_armor() ) {
                        worn.push_back( it->display_name() );
                    } else {
                        inventory.push_back( it->display_name() );
                    }
                }
                const auto add_group = [&]( const std::string & head,
                const std::vector<std::string> &names ) {
                    b.heading( head, c_yellow, names.empty() ? _( "None!" ) : std::string(),
                               c_light_red );
                    for( const std::string &name : names ) {
                        b.value( name, c_white );
                    }
                };
                add_group( _( "Wielded:" ), wielded );
                add_group( _( "Worn:" ), worn );
                add_group( _( "Inventory:" ), inventory );
            }
            data->panes.push_back( b.take() );
        }

        // Keybinding guide footer (green keys).
        {
            std::string s = string_format(
                                _( "Press <color_light_green>%s</color> or <color_light_green>%s</color> to cycle through name, height, and age." ),
                                ctxt.get_desc( "LEFT" ), ctxt.get_desc( "RIGHT" ) );
            s += "\n" + string_format(
                     _( "Press <color_light_green>%s</color> and <color_light_green>%s</color> to change height and age." ),
                     ctxt.get_desc( "UP" ), ctxt.get_desc( "DOWN" ) );
            s += "\n" + string_format( _( "Press <color_light_green>%s</color> to edit the selected field." ),
                                       ctxt.get_desc( "CONFIRM" ) );
            s += "\n" + string_format( _( "Press <color_light_green>%s</color> to switch gender." ),
                                       ctxt.get_desc( "CHANGE_GENDER" ) );
            s += "\n" + string_format( _( "Press <color_light_green>%s</color> to select location." ),
                                       ctxt.get_desc( "CHOOSE_LOCATION" ) );
            if( allow_reroll ) {
                s += "\n" + string_format(
                         _( "Press <color_light_green>%s</color> to save template, <color_light_green>%s</color> to re-roll or <color_light_green>%s</color> for random scenario." ),
                         ctxt.get_desc( "SAVE_TEMPLATE" ), ctxt.get_desc( "REROLL_CHARACTER" ),
                         ctxt.get_desc( "REROLL_CHARACTER_WITH_SCENARIO" ) );
            } else {
                s += "\n" + string_format(
                         _( "Press <color_light_green>%s</color> to save a template of this character." ),
                         ctxt.get_desc( "SAVE_TEMPLATE" ) );
            }
            s += "\n" + string_format(
                     _( "Press <color_light_green>%s</color> to finish or <color_light_green>%s</color> to go back." ),
                     ctxt.get_desc( "NEXT_TAB" ), ctxt.get_desc( "PREV_TAB" ) );
            data->guide_rml = cata_text_to_rml( s );
        }

        data->handle.DirtyAllVariables();
    };

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            sync_rml();
            if( use_character_preview ) {
                character_preview.display();
            }
            return;
        }
    } );

    rml.open( newcharacter_rmlui_enabled(), "newchardescription", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        register_nc_desc_rml_types( c );
        c.Bind( "tabs", &data->tabs );
        bind_nc_shell( c, data->shell );
        // Arrow clicks are translated into the SAME action strings the keyboard
        // produces, so each step's existing PREV_TAB/NEXT_TAB handling — including
        // the "Return to main menu?" confirm on step 0 — is reused unchanged.
        c.BindEventCallback( "on_prev",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = -1; } );
        c.BindEventCallback( "on_next",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = 1; } );
        c.Bind( "points_rml", &data->points_rml );
        c.Bind( "name_rml", &data->name_rml );
        c.Bind( "gender_rml", &data->gender_rml );
        c.Bind( "height_rml", &data->height_rml );
        c.Bind( "age_rml", &data->age_rml );
        c.Bind( "location_rml", &data->location_rml );
        c.Bind( "scenario_rml", &data->scenario_rml );
        c.Bind( "profession_rml", &data->profession_rml );
        c.Bind( "panes", &data->panes );
        c.Bind( "guide_rml", &data->guide_rml );
        data->handle = c.GetModelHandle();
    } );
    if( rml_doc_unavailable( rml, _( "Character creation (OVERVIEW tab)" ) ) ) {
        return tab_direction::QUIT;
    }

    // do not switch IME mode now, but restore previous mode on return
    ime_sentry sentry( ime_sentry::keep );

    int min_allowed_age = profession::min_age;
    int max_allowed_age = profession::max_age;
    // in centimeters. 2 std. deviations below average female height
    int min_allowed_height = 145;
    int max_allowed_height = 200;

    do {
        const auto [new_min_age, new_max_age] = profession_age_bounds( *you.prof );
        min_allowed_age = new_min_age;
        max_allowed_age = new_max_age;
        you.set_base_age( clamp( you.base_age(), min_allowed_age, max_allowed_age ) );
        ui_manager::redraw();
        nc_nav = 0;
        std::string action = ctxt.handle_input();
        if( nc_nav != 0 ) {
            action = nc_nav < 0 ? "PREV_TAB" : "NEXT_TAB";
        }
        if( action == "zoom_in" && use_character_preview ) {
            character_preview.zoom_in();
        }
        if( action == "zoom_out" && use_character_preview ) {
            character_preview.zoom_out();
        }
        if( action == "TOGGLE_CHARACTER_PREVIEW_CLOTHES" && use_character_preview ) {
            character_preview.toggle_clothes();
        }
        if( action == "NEXT_TAB" ) {
            if( !points.is_valid() ) {
                if( points.skill_points_left() < 0 ) {
                    popup( _( "Too many points allocated, change some features and try again." ) );
                } else if( points.trait_points_left() < 0 ) {
                    popup( _( "Too many trait points allocated, change some traits or lower some stats and try again." ) );
                } else if( points.stat_points_left() < 0 ) {
                    popup( _( "Too many stat points allocated, lower some stats and try again." ) );
                } else {
                    popup( _( "Too many points allocated, change some features and try again." ) );
                }
                continue;
            } else if( points.has_spare() &&
                       !query_yn( _( "Remaining points will be discarded, are you sure you want to proceed?" ) ) ) {
                continue;
            } else if( you.name.empty() ) {
                no_name_entered = true;
                ui_manager::redraw();
                if( !query_yn( _( "Are you SURE you're finished?  Your name will be randomly generated." ) ) ) {
                    continue;
                } else {
                    you.pick_name();
                    character_preview.clear();
                    return tab_direction::FORWARD;
                }
            } else if( query_yn( _( "Are you SURE you're finished?" ) ) ) {
                character_preview.clear();
                return tab_direction::FORWARD;
            } else {
                continue;
            }
        } else if( action == "PREV_TAB" ) {
            character_preview.clear();
            return tab_direction::BACKWARD;
        } else if( action == "RIGHT" ) {
            switch( current_selector ) {
                case char_creation::NAME:
                    current_selector = char_creation::HEIGHT;
                    break;
                case char_creation::HEIGHT:
                    current_selector = char_creation::AGE;
                    break;
                case char_creation::AGE:
                    current_selector = char_creation::NAME;
                    break;
            }
        } else if( action == "LEFT" ) {
            switch( current_selector ) {
                case char_creation::NAME:
                    current_selector = char_creation::AGE;
                    break;
                case char_creation::HEIGHT:
                    current_selector = char_creation::NAME;
                    break;
                case char_creation::AGE:
                    current_selector = char_creation::HEIGHT;
                    break;
            }
        } else if( action == "UP" ) {
            switch( current_selector ) {
                case char_creation::HEIGHT:
                    if( you.base_height() < max_allowed_height ) {
                        you.mod_base_height( 1 );
                    }
                    break;
                case char_creation::AGE:
                    if( you.base_age() < max_allowed_age ) {
                        you.mod_base_age( 1 );
                    }
                    break;
                default:
                    break;
            }
        } else if( action == "DOWN" ) {
            switch( current_selector ) {
                case char_creation::HEIGHT:
                    if( you.base_height() > min_allowed_height ) {
                        you.mod_base_height( -1 );
                    }
                    break;
                case char_creation::AGE:
                    if( you.base_age() > min_allowed_age ) {
                        you.mod_base_age( -1 );
                    }
                    break;
                default:
                    break;
            }
        } else if( action == "REROLL_CHARACTER" && allow_reroll ) {
            points.init_from_options();
            you.randomize( false, points );
            // Return tab_direction::NONE so we re-enter this tab again, but it forces a complete redrawing of it.
            return tab_direction::NONE;
        } else if( action == "REROLL_CHARACTER_WITH_SCENARIO" && allow_reroll ) {
            points.init_from_options();
            you.randomize( true, points );
            // Return tab_direction::NONE so we re-enter this tab again, but it forces a complete redrawing of it.
            return tab_direction::NONE;
        } else if( action == "SAVE_TEMPLATE" ) {
            if( const auto name = query_for_template_name() ) {
                you.save_template( *name, points );
            }
        } else if( action == "RANDOMIZE_CHAR_DESCRIPTION" ) {
            you.male = one_in( 2 );
            if( !MAP_SHARING::isSharing() ) { // Don't allow random names when sharing maps. We don't need to check at the top as you won't be able to edit the name
                you.pick_name();
                no_name_entered = you.name.empty();
            }
            you.set_base_age( rng( 16, 55 ) );
            you.set_base_height( rng( 145, 200 ) );
        } else if( action == "CHANGE_GENDER" ) {
            you.male = !you.male;
        } else if( action == "CHOOSE_LOCATION" ) {
            select_location.query();
            if( select_location.ret == RANDOM_START_LOC_ENTRY ) {
                you.random_start_location = true;
            } else if( select_location.ret >= 0 ) {
                for( const auto &loc : start_locations::get_all() ) {
                    if( loc.id.id().to_i() == select_location.ret ) {
                        you.random_start_location = false;
                        you.start_location = loc.id;
                        break;
                    }
                }
            }
        } else if( action == "CONFIRM" &&
                   // Don't edit names when sharing maps
                   !MAP_SHARING::isSharing() ) {

            string_input_popup popup;
            switch( current_selector ) {
                case char_creation::NAME: {
                    popup.title( _( "Enter name.  Cancel to delete all." ) )
                         .text( you.name )
                         .only_digits( false );
                    you.name = popup.query_string();
                    no_name_entered = you.name.empty();
                    break;
                }
                case char_creation::AGE: {
                    const std::string title = string_format( _( "Enter age in years.  Minimum %d, maximum %d" ),
                                              min_allowed_age, max_allowed_age );
                    popup.title( title )
                         .text( string_format( "%d", you.base_age() ) )
                         .only_digits( true );
                    const int result = popup.query_int();
                    if( result != 0 ) {
                        you.set_base_age( clamp( result, min_allowed_age, max_allowed_age ) );
                    }
                    break;
                }
                case char_creation::HEIGHT: {
                    popup.title( _( "Enter height in centimeters.  Minimum 145, maximum 200" ) )
                         .text( string_format( "%d", you.base_height() ) )
                         .only_digits( true );
                    const int result = popup.query_int();
                    if( result != 0 ) {
                        you.set_base_height( clamp( result, 145, 200 ) );
                    }
                    break;
                }
            }

        } else if( action == "QUIT" && query_yn( _( "Return to main menu?" ) ) ) {
            character_preview.clear();
            return tab_direction::QUIT;
        }
    } while( true );
}

std::optional<std::string> query_for_template_name()
{
    static const std::set<int> fname_char_blacklist = {
#if defined(_WIN32)
        '\"', '*', '/', ':', '<', '>', '?', '\\', '|',
        '\x01', '\x02', '\x03', '\x04', '\x05', '\x06', '\x07',         '\x09',
        '\x0B', '\x0C',         '\x0E', '\x0F', '\x10', '\x11', '\x12',
        '\x13', '\x14',         '\x16', '\x17', '\x18', '\x19', '\x1A',
        '\x1C', '\x1D', '\x1E', '\x1F'
#else
        '/'
#endif
    };
    std::string title = _( "Name of template:" );
    std::string desc = _( "Keep in mind you may not use special characters like / in filenames" );

    string_input_popup spop;
    spop.title( title );
    spop.description( desc );
    spop.width( FULL_SCREEN_WIDTH - utf8_width( title ) - 8 );
    for( int character : fname_char_blacklist ) {
        spop.callbacks[ character ] = []() {
            return true;
        };
    }

    spop.query_string( true );
    if( spop.canceled() ) {
        return std::nullopt;
    } else {
        return spop.text();
    }
}

void reset_scenario( avatar &u, const scenario *scen )
{
    auto psorter = profession_sorter;
    psorter.sort_by_points = true;
    const std::vector<profession_id> permitted = scen->permitted_professions();
    const profession_id &default_prof = *std::min_element( permitted.begin(), permitted.end(),
                                        psorter );

    u.random_start_location = true;
    u.str_max = 8;
    u.dex_max = 8;
    u.int_max = 8;
    u.per_max = 8;
    g->scen = scen;
    u.prof = default_prof;
    u.set_base_age( random_age_for_profession( *u.prof ) );
    for( auto &t : u.get_mutations() ) {
        if( t.obj().hp_modifier != 0 ) {
            u.toggle_trait( t );
        }
    }
    u.clear_mutations();
    u.recalc_hp();
    u.clear_skills();
    u.clear_bionics();
    newcharacter::add_traits( u );
}

namespace newcharacter
{
bool has_conflicting_trait( const Character &ch, const trait_id &t )
{
    return ch.has_opposite_trait( t ) ||
           has_lower_trait( ch, t ) ||
           has_higher_trait( ch, t ) ||
           has_same_type_trait( ch, t ) ;
}

bool bionic_has_conflict( const Character &ch, const bionic_id &b )
{
    bool has_conflict_mut = false;
    for( const trait_id &mid : b->canceled_mutations ) {
        if( ch.has_trait( mid ) ) {
            has_conflict_mut = true;
        }
    }
    bool lacks_needed_bio = false;
    if( !b->required_bionics.empty() ) {
        for( const bionic_id &req_bid : b->required_bionics ) {
            if( !ch.has_bionic( req_bid ) ) {
                lacks_needed_bio = true;
                break;
            }
        }
    }

    bool upgrade_issues = false;
    if( !b->available_upgrades.empty() ) {
        for( const bionic_id &up_bid : b->available_upgrades ) {
            if( ch.has_bionic( up_bid ) ) {
                upgrade_issues = true;
            }
        }
    }

    if( b->upgraded_bionic != bionic_id::NULL_ID() ) {
        if( ch.has_bionic( b->upgraded_bionic ) ) {
            upgrade_issues = true;
        }
    }

    return !ch.bionic_installation_issues( b ).empty() ||
           has_conflict_mut ||
           lacks_needed_bio ||
           upgrade_issues;
}

bool has_lower_trait( const Character &ch, const trait_id &t )
{
    for( const trait_id &it : t->prereqs ) {
        if( ch.has_trait( it ) || has_lower_trait( ch, it ) ) {
            return true;
        }
    }
    return false;
}

bool has_higher_trait( const Character &ch, const trait_id &t )
{
    for( const trait_id &it : t->replacements ) {
        if( ch.has_trait( it ) || has_higher_trait( ch, it ) ) {
            return true;
        }
    }
    return false;
}

bool has_same_type_trait( const Character &ch, const trait_id &t )
{
    for( const trait_id &it : get_mutations_in_types( t->types ) ) {
        if( ch.has_trait( it ) && t != it ) {
            return true;
        }
    }
    return false;
}

} // namespace newcharacter
