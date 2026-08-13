#include "avatar.h" // IWYU pragma: associated
#include "newcharacter.h" // IWYU pragma: associated

#include <algorithm>
#include <array>
#include <chrono>
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
#include "bodypart.h"
#include "cata_tiles.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "character.h"
#include "character_martial_arts.h"
#include "color.h"
#include "cursesdef.h"
#include "damage.h"
#include "detached_ptr.h"
#include "filesystem.h"
#include "fstream_utils.h"
#include "game.h"
#include "game_constants.h"
#include "ime.h"
#include "input.h"
#include "int_id.h"
#include "inventory.h"
#include "item.h"
#include "item_category.h"
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
#include "newchar_aptitude.h"
#include "newchar_bio_scan.h"
#include "newchar_bionic_gate.h"
#include "newchar_dna.h"
#include "newchar_balance.h"
#include "newchar_stat_meter.h"
#include "newchar_trait_gate.h"
#include "newchar_finish_gate.h"
#include "newchar_seal.h"
#include "rml_length.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "rng.h"
#include "scenario.h"
#include "lighting/render_state.h"
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
/// The editable identity fields on the OVERVIEW step, in the order its column lists them. UP/DOWN
/// walk this, LEFT/RIGHT change the current one's value, CONFIRM opens its editor. GENDER and
/// LOCATION joined the list with the rework: they were changeable only through their own dedicated
/// keys, and the cursor could not reach them at all.
enum description_selector {
    NAME = 0,
    GENDER,
    HEIGHT,
    AGE,
    LOCATION,
    NUM_FIELDS,
};
} // namespace char_creation


// Colours used in this file; most else defaults to c_light_gray.
//
// This block used to appear TWICE, verbatim, forty lines apart, which is where most of this file's
// -Wunused-macros noise came from: every use resolved against the second copy, leaving the first
// warning as never expanded. COL_TR_NEUT and COL_HEADER went with the OVERVIEW rework — the last
// consumer of both was that step's six panes, and its columns take their colours from the shared
// row vocabulary instead. The per-row toggled-on/off variants went earlier, with the last curses
// list in this file.
#define COL_STAT_ACT        c_white   // Selected stat
#define COL_STAT_BONUS      c_light_green // Bonus
#define COL_STAT_NEUTRAL    c_white   // Neutral Property
#define COL_STAT_PENALTY    c_light_red   // Penalty
#define COL_TR_GOOD         c_green   // Good trait descriptive text
#define COL_TR_BAD          c_red     // Bad trait descriptive text
#define COL_SKILL_USED      c_green   // A skill with at least one point

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

/// Same generator, but the glyph takes an arbitrary game colour.
///
/// The scenario flag strip encodes VALENCE in colour — danger red, bodily state green,
/// circumstance grey/blue, season yellow — so the strip is readable before any individual
/// glyph has been learned. Two golds cannot express that.
auto nc_icon_dec_col( unsigned seed, int size, const nc_color &col ) -> std::string
{
    // nc_color_to_hex is cached and honours theme.json's "game_colors" overrides, so a
    // retheme moves these glyphs with everything else. It yields "#rrggbbaa"; the
    // generator wants a bare rrggbb triple, matching the literals in nc_icon_dec.
    const std::string hex = nc_color_to_hex( col );
    const std::string rgb = hex.size() >= 7 ? hex.substr( 1, 6 ) : std::string( "a1885f" );
    return string_format( "image( ?proc:runic-icon:%d:%u:%s none contain ) border-box",
                          size, seed, rgb );
}

/// RCSS `decorator` cropping ONE sprite out of the loaded tileset, or an empty string when
/// the tileset has nothing for `id`.
///
/// Resolved here, on the game side, because the render interface deliberately knows nothing
/// about tilesets — it is handed a file path and a pixel rect and does the crop
/// (`?sprite:<x>:<y>:<w>:<h>:<path>`, rmlui_render_interface.cpp).
///
/// `find_tile_looks_like` rather than a bare `find_tile_type`: it already does the seasonal
/// lookup AND walks the category's `looks_like` chain, which is what the world render does,
/// so a modded item with no art of its own shows its base item's sprite instead of a hole.
auto nc_tile_sprite_dec( const std::string &id, TILE_CATEGORY cat ) -> std::string
{
    // Every miss returns "none", not "". `data-style-decorator` evaluates its binding even for an
    // element `data-if` has hidden, and an empty value becomes `decorator: ;`, which RmlUi logs as
    // a parse error EVERY FRAME — 9,583 of them in one session from the doll's empty slots alone.
    static const std::string dec_none = "none";
    if( tilecontext == nullptr || id.empty() ) {
        return dec_none;
    }
    const tileset *ts = tilecontext->current_tileset();
    if( ts == nullptr ) {
        return dec_none;
    }
    // tile_lookup_res::tile() is non-const, so the optional has to be held by value.
    auto found = tilecontext->find_tile_looks_like( id, cat );
    if( !found ) {
        return dec_none;
    }
    const tile_type &tt = found->tile();
    if( tt.sprite.fg.empty() ) {
        return dec_none;
    }
    // Each entry is a weighted_object wrapping the variant list; take the first variant of
    // the first entry so the art is stable rather than rerolled per frame.
    const std::vector<int> &variants = tt.sprite.fg.begin()->obj;
    if( variants.empty() ) {
        return dec_none;
    }
    const auto src = ts->sprite_file_source( variants.front() );
    if( !src ) {
        return dec_none;
    }
    return string_format( "image( ?sprite:%d:%d:%d:%d:%s none contain ) border-box",
                          src->rect.x, src->rect.y, src->rect.w, src->rect.h, src->path );
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

/// Prepares the character portrait for every creator step that shows one.
///
/// Only zoom and tile sizing remain here. Everything that used to make this function
/// interesting is gone, because the portrait no longer competes with the panel for
/// screen space:
///
/// The avatar used to be a GPU sprite drawn into the shared UI composite, which the
/// frame blits BEFORE RmlUi — so any opaque panel over its rect hid it. Keeping it
/// visible meant pinning the box hard right and holding every panel narrow enough to
/// leave an uncovered strip (72%, with a hide threshold derived from the panel's right
/// edge so a larger tileset could not silently slide the box back under the panel).
///
/// It is now drawn into its own render target and reaches the document as a decorator
/// (`?avatar:<gen>` — see render_state::set_avatar_route), so the DOCUMENT places it and
/// the panels are free to fill the screen. `hide_below_ncols` is 0: there is no longer
/// an occlusion to dodge, and the stylesheet decides whether the portrait has room.
void nc_prepare_preview( character_preview_window &pv )
{
    const int ncols = std::max( 10, TERMX * 13 / 100 );
    const int nlines = std::max( 7, TERMY * 22 / 100 );
    constexpr auto orient = character_preview_window::Orientation{
        character_preview_window::TOP_RIGHT,
        character_preview_window::Margin{ 0, 1, 4, 0 }
    };
    pv.prepare( nlines, ncols, &orient, 0 );
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
    // Character portrait, on the four steps that show one. The avatar is drawn into
    // render_state's own target and reaches the document as a decorator, so it is
    // ordinary content the layout places — see set_nc_portrait.
    bool has_portrait = false;
    Rml::String portrait_dec;
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

/// Points the portrait element at render_state's avatar texture.
///
/// The generation is embedded in the source purely to bust RmlUi's source-keyed texture
/// cache: if the target were ever reallocated, a stable string would leave the document
/// sampling a destroyed texture. `contain` keeps the sprite's aspect inside whatever box
/// the stylesheet gives it.
void set_nc_portrait( nc_shell &s, bool shown )
{
    s.has_portrait = shown;
    if( !shown ) {
        s.portrait_dec.clear();
        return;
    }
    // `scale-none`, not `contain`: the target is AVATAR_TARGET_PX square but the sprite
    // only occupies a tile's worth in the middle, so `contain` scaled the whole texture
    // (mostly empty margin) down to the box and rendered the avatar about a quarter size.
    // scale-none draws at native resolution and centres, cropping the empty margin — and
    // it keeps the sprite pixel-exact, which is the point of sampling the target directly
    // instead of resampling it.
    s.portrait_dec = string_format( "image( ?avatar:%u none scale-none center center )",
                                    lighting::get_render_state().avatar_texture_generation() );
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
    c.Bind( "has_portrait", &s.has_portrait );
    c.Bind( "portrait_dec", &s.portrait_dec );
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
    h.DirtyVariable( "has_portrait" );
    h.DirtyVariable( "portrait_dec" );
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

/// One pip of a card's meter. Three booleans rather than a tier string because `data-class-*`
/// binds to booleans, and the tier is only ever consumed as a colour.
struct nc_stat_pip {
    bool on = false;      //< the stat has reached this value
    bool base = false;    //< inside the granted floor, so it can never be sold back
    bool steep = false;   //< costs two points
};

/// A label / value / sub-line triple in the info panel's facts column. Labels differ per stat
/// and are translated, so they are bound from here rather than written into the markup — the
/// SCENARIO and PROFESSION panels hardcode their English labels in RML, which this does not
/// copy.
struct nc_stat_fact {
    Rml::String label_rml;
    Rml::String value_rml;
    Rml::String sub_rml;
};

/// One stat card: identity, the number, the meter, and its two adjust affordances.
struct nc_stat_card {
    Rml::String icon_dec;
    Rml::String name_rml;
    Rml::String val_rml;
    Rml::Vector<nc_stat_pip> pips;
    bool selected = false;   //< cursor is on this card
    bool can_dec = false;    //< not at the granted floor
    bool can_inc = false;    //< not at the cap
};

struct nc_stats_session {
    Rml::Vector<nc_stats_tab> tabs;
    nc_shell shell;
    Rml::String points_rml;
    Rml::String hint_rml;    //< one status line in place of the old seven-line hints block
    Rml::Vector<nc_stat_card> cards;
    /// The selected stat's own sigil, at panel size. Never empty: `data-style-decorator` is
    /// evaluated on the document's FIRST layout, before sync_rml has run, and an empty string
    /// became `decorator: ;` and an RmlUi parse warning per frame.
    Rml::String art_dec = "none";
    Rml::Vector<nc_stat_fact> facts;
    Rml::String desc_rml;    //< the stat's own voice, prose column
    Rml::String leg_base_rml;
    Rml::String leg_cheap_rml;
    Rml::String leg_steep_rml;
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
    // Pips before cards: a card member cannot be registered before its element type is.
    Rml::StructHandle<nc_stat_pip> ph = c.RegisterStruct<nc_stat_pip>();
    ph.RegisterMember( "on", &nc_stat_pip::on );
    ph.RegisterMember( "base", &nc_stat_pip::base );
    ph.RegisterMember( "steep", &nc_stat_pip::steep );
    c.RegisterArray<Rml::Vector<nc_stat_pip>>();
    Rml::StructHandle<nc_stat_fact> fh = c.RegisterStruct<nc_stat_fact>();
    fh.RegisterMember( "label_rml", &nc_stat_fact::label_rml );
    fh.RegisterMember( "value_rml", &nc_stat_fact::value_rml );
    fh.RegisterMember( "sub_rml", &nc_stat_fact::sub_rml );
    c.RegisterArray<Rml::Vector<nc_stat_fact>>();
    Rml::StructHandle<nc_stat_card> ch = c.RegisterStruct<nc_stat_card>();
    ch.RegisterMember( "icon_dec", &nc_stat_card::icon_dec );
    ch.RegisterMember( "name_rml", &nc_stat_card::name_rml );
    ch.RegisterMember( "val_rml", &nc_stat_card::val_rml );
    ch.RegisterMember( "pips", &nc_stat_card::pips );
    ch.RegisterMember( "selected", &nc_stat_card::selected );
    ch.RegisterMember( "can_dec", &nc_stat_card::can_dec );
    ch.RegisterMember( "can_inc", &nc_stat_card::can_inc );
    c.RegisterArray<Rml::Vector<nc_stat_card>>();
    g_nc_stats_types_registered = true;
}

/// The four creator stats behind one cursor index, so every rule that touches them is stated
/// once instead of once per stat. Replaces eight near-identical arms in the input loop.
auto nc_stat_ref( avatar &u, int sel ) -> int & // *NOPAD*
{
    switch( sel ) {
        case 1:
            return u.str_max;
        case 2:
            return u.dex_max;
        case 3:
            return u.int_max;
        default:
            return u.per_max;
    }
}

auto nc_stat_name( int sel ) -> std::string
{
    switch( sel ) {
    case 1:
        return _( "Strength" );
        case 2:
            return _( "Dexterity" );
        case 3:
            return _( "Intelligence" );
        default:
            return _( "Perception" );
    }
}

/// Stable per-stat sigil seeds, so each stat keeps one recognisable glyph across runs. The
/// values are arbitrary but must all DIFFER — the generator keys the shape on the seed.
constexpr std::array<unsigned, 4> nc_stat_seeds = { 0x5354, 0x4458, 0x494E, 0x5052 };

/// The stat's own voice. The prose column, kept separate from the numbers above it.
auto nc_stat_blurb( int sel ) -> std::string
{
    switch( sel ) {
    case 1:
        return
            _( "Strength also makes you more resistant to many diseases and poisons, and makes actions which require brute force more effective." );
        case 2:
            return _( "Dexterity also enhances many actions which require finesse." );
        case 3:
            return
                _( "Intelligence is also used when crafting, installing bionics, and interacting with NPCs." );
        default:
            return _( "Perception is also used for detecting traps and other things of interest." );
    }
}

/// The selected stat's derived effects as label/value fields. Previously one pre-wrapped
/// colour-tagged string joined with newlines, which no stylesheet could give hierarchy and
/// which cannot fit a fixed-height panel.
///
/// `u` is mutated exactly as the curses path did: Strength's HP readout needs recalc_hp().
auto nc_stat_facts( avatar &u, int sel, int max_stat_points ) -> Rml::Vector<nc_stat_fact>
{
    Rml::Vector<nc_stat_fact> out;
    const auto add = [&out]( const std::string & label, const std::string & value,
    const nc_color & col, const std::string & sub = std::string() ) {
        out.push_back( {
            .label_rml = cata_text_to_rml( label ),
            .value_rml = cata_text_to_rml( colorize( value, col ) ),
            .sub_rml = cata_text_to_rml( sub ) } );
    };

    // What the next point costs comes first: it is the decision in front of the player. Stating
    // it at every value replaces a red warning that only appeared once the threshold was
    // already behind them.
    const int val = nc_stat_ref( u, sel );
    const int cost = nc_stat_meter::next_cost( val, HIGH_STAT, max_stat_points );
    if( cost == 0 ) {
        add( _( "Next point" ), _( "Maxed" ), c_dark_gray );
    } else if( cost > 1 ) {
        add( _( "Next point" ), _( "2 points" ), COL_STAT_PENALTY );
    } else {
        add( _( "Next point" ), _( "1 point" ), COL_STAT_NEUTRAL,
             string_format( _( "2 points above %d" ), HIGH_STAT ) );
    }

    switch( sel ) {
        case 1:
            u.recalc_hp();
            add( _( "Base HP" ),
                 string_format( "%d", u.get_part_hp_max( bodypart_id( "head" ) ) ),
                 COL_STAT_NEUTRAL );
            add( _( "Carry weight" ),
                 string_format( "%.1f %s", convert_weight( u.weight_capacity() ), weight_units() ),
                 COL_STAT_NEUTRAL );
            add( _( "Melee damage" ), string_format( "+%.1f", u.bonus_damage( false ) ),
                 COL_STAT_BONUS );
            break;
        case 2:
            add( _( "Melee to-hit" ), string_format( "+%.2f", u.get_hit_base() ), COL_STAT_BONUS );
            add( _( "Throw penalty" ),
                 string_format( _( "+%d per dodge" ),
                                ranged::throw_dispersion_per_dodge( u, false ) ),
                 COL_STAT_BONUS );
            if( u.ranged_dex_mod() != 0 ) {
                add( _( "Ranged penalty" ), string_format( "-%d", std::abs( u.ranged_dex_mod() ) ),
                     COL_STAT_PENALTY );
            }
            break;
        case 3: {
            const int read_spd = u.read_speed( false );
            add( _( "Read times" ), string_format( "%d%%", read_spd ),
                 read_spd == 100 ? COL_STAT_NEUTRAL
                 : ( read_spd < 100 ? COL_STAT_BONUS : COL_STAT_PENALTY ) );
            add( _( "Skill rust" ), string_format( "%d%%", u.rust_rate() ), COL_STAT_PENALTY );
            add( _( "Crafting bonus" ), string_format( "+%d%%", u.get_int() ), COL_STAT_BONUS );
            break;
        }
        default:
            if( u.ranged_per_mod() > 0 ) {
                add( _( "Aiming penalty" ), string_format( "-%d", u.ranged_per_mod() ),
                     COL_STAT_PENALTY );
            }
            add( _( "Night vision" ),
                 string_format( "+%.1f", vision::nv_range_from_per( u.per_max ) ), COL_STAT_BONUS );
            break;
    }
    return out;
}
} // namespace

tab_direction set_stats( avatar &u, points_left &points )
{
    const int max_stat_points = points.is_freeform() ? 20 : MAX_STAT;

    int sel = 1;
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
    const auto init_windows = [&]( ui_adaptor & ui ) {
        w = catacurses::newwin( TERMY, TERMX, point_zero );
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

    auto data = std::make_unique<nc_stats_session>();
    rml_doc rml;
    // Set by the arrow click callbacks, consumed by the input loop below. Not a
    // tab_direction: it is translated into an action string so the existing
    // keyboard handling stays the single place navigation is decided.
    int nc_nav = 0;
    // Card and +/- click intent, applied ONCE per input cycle. `data-event-*` installs a
    // listener per generated element and a `data-for` regeneration adds another without
    // removing the old, so a callback that mutated directly would run an unbounded number of
    // times per click — measured at 15 on the SCENARIO tab, where a toggle silently cancelled
    // itself out. Recording intent is the fix; see plans/charcreation-scenario-tree.md.
    int pending_card = -1;
    int pending_step_stat = -1;
    int pending_step_dir = 0;

    // One place per direction rather than one per stat per direction. The point arithmetic
    // lives in newchar_stat_meter.h, where its HIGH_STAT boundaries are tested.
    const auto dec_stat = [&]( int s ) {
        int &v = nc_stat_ref( u, s );
        const int back = nc_stat_meter::refund( v, HIGH_STAT );
        if( back == 0 ) {
            return;
        }
        v--;
        points.stat_points += back;
    };
    const auto inc_stat = [&]( int s ) {
        int &v = nc_stat_ref( u, s );
        const int cost = nc_stat_meter::next_cost( v, HIGH_STAT, max_stat_points );
        if( cost == 0 ) {
            return;
        }
        v++;
        points.stat_points -= cost;
    };

    const auto sync_rml = [&]() {
        if( !data->handle ) {
            return;
        }
        data->tabs = build_nc_char_tabs<nc_stats_tab>( 3 );  // STATS tab active
        data->shell = fill_nc_shell( 3, ctxt );
        data->points_rml = cata_text_to_rml( nc_points_line( points ) );
        // The cards are a horizontal row, so LEFT/RIGHT walk them and UP/DOWN adjust the
        // value — the creator's rule everywhere else (the list's own axis navigates, the other
        // adjusts), and the direction the meter itself grows.
        //
        // ONE key each, and a NAMED one where there is one. The cardinals carry five bindings
        // apiece ('h', LEFT, '4', NUMPAD_4, JOY_LEFT), so an unfiltered get_desc ran
        // "h, LEFT, 4 or NUMPAD_4 / l, RIGHT, 6 or NUMPAD_6 select · …" across the whole top
        // bar. disallow_lower_case alone was not enough: UP's second binding is the digit '8',
        // so the pair read "DOWN / 8". Keycodes above 0x7F are the named keys (arrows are
        // 0x102-0x105), and the fallback matters — someone who has bound a cardinal to nothing
        // but a letter must still see it, and get_desc answers "Disabled" for a filter that
        // rejects every binding.
        const auto key = [&ctxt]( const std::string & act ) -> std::string {
            const std::string named = ctxt.get_desc( act, 1, []( const input_event & ev )
            {
                return ev.type == input_event_t::keyboard && ev.get_first_input() > 0x7F;
            } );
            return named == pgettext( "keybinding", "Disabled" ) ? ctxt.get_desc( act, 1 ) : named;
        };
        data->hint_rml = cata_text_to_rml( string_format(
                                               _( "<color_light_green>%s</color> / <color_light_green>%s</color> select · <color_light_green>%s</color> / <color_light_green>%s</color> adjust · <color_light_green>%s</color> randomize" ),
                                               key( "LEFT" ), key( "RIGHT" ), key( "DOWN" ), key( "UP" ),
                                               key( "RANDOMIZE" ) ) );

        data->cards.clear();
        for( int s = 1; s <= 4; s++ ) {
            const int val = nc_stat_ref( u, s );
            const bool active = ( sel == s );
            nc_stat_card c;
            c.icon_dec = nc_icon_dec_col( nc_stat_seeds[s - 1], 14,
                                          active ? c_yellow : c_light_gray );
            c.name_rml = cata_text_to_rml( colorize( nc_stat_name( s ), c_light_gray ) );
            // The value is what the four cards are compared on, so it stays bright on every
            // one of them; the cursor is carried by the card's border and by the notch.
            c.val_rml = cata_text_to_rml( colorize( string_format( "%d", val ), COL_STAT_ACT ) );
            for( int p = 1; p <= max_stat_points; p++ ) {
                const nc_stat_meter::tier t = nc_stat_meter::pip_tier( p, HIGH_STAT );
                c.pips.push_back( {
                    .on = ( val >= p ),
                    .base = ( t == nc_stat_meter::tier::base ),
                    .steep = ( t == nc_stat_meter::tier::steep ) } );
            }
            c.selected = active;
            c.can_dec = nc_stat_meter::refund( val, HIGH_STAT ) != 0;
            c.can_inc = nc_stat_meter::next_cost( val, HIGH_STAT, max_stat_points ) != 0;
            data->cards.push_back( c );
        }

        data->art_dec = nc_icon_dec_col( nc_stat_seeds[sel - 1], 112, c_yellow );
        data->facts = nc_stat_facts( u, sel, max_stat_points );
        data->desc_rml = cata_text_to_rml( nc_stat_blurb( sel ) );
        // The legend explains a METER, so its swatches are real pips rather than sigils, and
        // its wording comes from the same constants the pips do.
        data->leg_base_rml = cata_text_to_rml( colorize(
                string_format( _( "Granted (first %d)" ), nc_stat_meter::floor_val ),
                c_light_gray ) );
        data->leg_cheap_rml = cata_text_to_rml( colorize( _( "1 point each" ), c_light_gray ) );
        data->leg_steep_rml = cata_text_to_rml( colorize(
                string_format( _( "2 points each (above %d)" ), HIGH_STAT ), c_light_gray ) );

        data->handle.DirtyVariable( "tabs" );
        dirty_nc_shell( data->handle );
        data->handle.DirtyVariable( "points_rml" );
        data->handle.DirtyVariable( "hint_rml" );
        data->handle.DirtyVariable( "cards" );
        data->handle.DirtyVariable( "art_dec" );
        data->handle.DirtyVariable( "facts" );
        data->handle.DirtyVariable( "desc_rml" );
        data->handle.DirtyVariable( "leg_base_rml" );
        data->handle.DirtyVariable( "leg_cheap_rml" );
        data->handle.DirtyVariable( "leg_steep_rml" );
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
        c.Bind( "hint_rml", &data->hint_rml );
        c.Bind( "cards", &data->cards );
        c.Bind( "art_dec", &data->art_dec );
        c.Bind( "facts", &data->facts );
        c.Bind( "desc_rml", &data->desc_rml );
        c.Bind( "leg_base_rml", &data->leg_base_rml );
        c.Bind( "leg_cheap_rml", &data->leg_cheap_rml );
        c.Bind( "leg_steep_rml", &data->leg_steep_rml );
        // Click callbacks RECORD INTENT and mutate nothing — see the comment on pending_card.
        c.BindEventCallback( "on_card",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int i = -1;
            if( !args.empty() ) {
                args[0].GetInto( i );
            }
            if( i >= 0 && i < 4 ) {
                pending_card = i + 1;
            }
        } );
        // The +/- controls, and the only way a mouse could change a stat at all before this.
        c.BindEventCallback( "on_step",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int i = -1;
            int d = 0;
            if( args.size() >= 2 ) {
                args[0].GetInto( i );
                args[1].GetInto( d );
            }
            if( i >= 0 && i < 4 && d != 0 ) {
                pending_step_stat = i + 1;
                pending_step_dir = d;
            }
        } );
        data->handle = c.GetModelHandle();
    } );
    if( rml_doc_unavailable( rml, _( "Character creation (STATS tab)" ) ) ) {
        return tab_direction::QUIT;
    }

    do {
        ui_manager::redraw();
        nc_nav = 0;
        pending_card = -1;
        pending_step_stat = -1;
        pending_step_dir = 0;
        std::string action = ctxt.handle_input();
        if( nc_nav != 0 ) {
            action = nc_nav < 0 ? "PREV_TAB" : "NEXT_TAB";
        }
        // Applied once, however many times the callback ran. A click on `+` bubbles to its
        // card as well, so both intents land — and moving the cursor to the stat being
        // adjusted is what clicking on it means.
        if( pending_card >= 1 ) {
            sel = pending_card;
        }
        if( pending_step_stat >= 1 ) {
            sel = pending_step_stat;
            if( pending_step_dir < 0 ) {
                dec_stat( sel );
            } else {
                inc_stat( sel );
            }
        }
        if( action == "RIGHT" ) {
            sel = sel < 4 ? sel + 1 : 1;
        } else if( action == "LEFT" ) {
            sel = sel > 1 ? sel - 1 : 4;
        } else if( action == "RANDOMIZE" ) {
            sel = rng( 1, 4 );
        } else if( action == "UP" ) {
            inc_stat( sel );
        } else if( action == "DOWN" ) {
            dec_stat( sel );
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

/// One line of a column. FLAT list with a `header` flag rather than a nested `data-for`, for the
/// same reason the profession equipment tree is flat: every row is then the SAME height, so
/// scrolling the cursor into view is exact arithmetic (scroll_height / row_count) instead of DOM
/// child indexing, which `data-for` makes unreliable.
///
/// The cells are aligned micro-columns — cursor, checkbox, cost, name — which is what lets a
/// 60-row column be scanned down any one of them. See UI_designs/11_charcreation_mutations.png.
struct nc_trait_row {
    Rml::String cursor_rml;   //< ">" on the cursor row, else empty
    Rml::String check_rml;    //< "[x]" held, "[ ]" available, "[-]" cannot be taken
    Rml::String cost_rml;     //< "[ 3]" / "[-2]", empty when free
    Rml::String name_rml;
    bool header = false;      //< a sub-heading inside the column, not a selectable trait
    bool selected = false;
};

/// One column: a heading, a count, and its rows.
struct nc_trait_col {
    Rml::String name_rml;
    Rml::String count_rml;
    Rml::Vector<nc_trait_row> rows;
};

/// A label / value / sub-line triple in the detail panel.
struct nc_trait_fact {
    Rml::String label_rml;
    Rml::String value_rml;
    Rml::String sub_rml;
};

/// One rung of the DNA strand. Positions arrive as LENGTHS for a leading gap and a bond, because
/// the strand is laid out in pure flow — `position: absolute` resolves against .nc-panel in this
/// document rather than against a `position: relative` parent, which is what put the balance
/// scale's beam through the top bar.
struct nc_dna_rung {
    Rml::String gap;          //< dp before the left-hand dot
    Rml::String bond;         //< dp between the two dots
    bool left_front = true;   //< the left dot is nearer the viewer, so it is the bright one
    /// A trait the player has taken is pinned to this rung. Three flags rather than one colour
    /// string because `data-class-*` binds to booleans, and valence is the only thing the colour
    /// carries: advantage, disadvantage, or an appearance pick.
    bool mark_good = false;
    bool mark_bad = false;
    bool mark_cosm = false;
};

struct nc_traits_session {
    Rml::Vector<nc_traits_tab> tabs;
    nc_shell shell;
    Rml::String points_rml;
    Rml::String budget_rml;   //< "Points remaining: N", the reference's meta-bar readout
    nc_balance balance;
    /// Three columns, bound separately rather than as an array: each needs a stable element id so
    /// C++ can scroll its own cursor into view.
    nc_trait_col col0;
    nc_trait_col col1;
    nc_trait_col col2;
    Rml::String sel_name_rml;   //< ":: TRAIT NAME" over the detail panel
    Rml::Vector<nc_trait_fact> facts;
    Rml::String desc_rml;
    Rml::String hint_rml;
    /// The spinning strand. Rebuilt every animation tick; everything else only when the model
    /// actually changed.
    Rml::Vector<nc_dna_rung> dna;
    Rml::String dna_count_rml;
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
    // Rows before the column that holds them: a member cannot be registered before its type is.
    Rml::StructHandle<nc_trait_row> rh = c.RegisterStruct<nc_trait_row>();
    rh.RegisterMember( "cursor_rml", &nc_trait_row::cursor_rml );
    rh.RegisterMember( "check_rml", &nc_trait_row::check_rml );
    rh.RegisterMember( "cost_rml", &nc_trait_row::cost_rml );
    rh.RegisterMember( "name_rml", &nc_trait_row::name_rml );
    rh.RegisterMember( "header", &nc_trait_row::header );
    rh.RegisterMember( "selected", &nc_trait_row::selected );
    c.RegisterArray<Rml::Vector<nc_trait_row>>();
    Rml::StructHandle<nc_trait_col> ch = c.RegisterStruct<nc_trait_col>();
    ch.RegisterMember( "name_rml", &nc_trait_col::name_rml );
    ch.RegisterMember( "count_rml", &nc_trait_col::count_rml );
    ch.RegisterMember( "rows", &nc_trait_col::rows );
    Rml::StructHandle<nc_trait_fact> fh = c.RegisterStruct<nc_trait_fact>();
    fh.RegisterMember( "label_rml", &nc_trait_fact::label_rml );
    fh.RegisterMember( "value_rml", &nc_trait_fact::value_rml );
    fh.RegisterMember( "sub_rml", &nc_trait_fact::sub_rml );
    c.RegisterArray<Rml::Vector<nc_trait_fact>>();
    Rml::StructHandle<nc_dna_rung> dh = c.RegisterStruct<nc_dna_rung>();
    dh.RegisterMember( "gap", &nc_dna_rung::gap );
    dh.RegisterMember( "bond", &nc_dna_rung::bond );
    dh.RegisterMember( "left_front", &nc_dna_rung::left_front );
    dh.RegisterMember( "mark_good", &nc_dna_rung::mark_good );
    dh.RegisterMember( "mark_bad", &nc_dna_rung::mark_bad );
    dh.RegisterMember( "mark_cosm", &nc_dna_rung::mark_cosm );
    c.RegisterArray<Rml::Vector<nc_dna_rung>>();
    g_nc_traits_types_registered = true;
}

/// Which of the three columns a trait belongs in, and — for column 2 only — which sub-heading it
/// sits under. `appearance_type` is empty for the gameplay groups.
///
/// Appearance wins over point sign: a hair style with a stray point cost is still a hair style,
/// and 66 of the 68 point-free starting traits are appearance pickers, which is why the old
/// "neutral" column read as a wardrobe wedged into a gameplay chooser.
struct nc_trait_group {
    int col = 0;
    std::string appearance_type;

    auto operator==( const nc_trait_group & ) const -> bool = default; // *NOPAD*
};

auto nc_classify_trait( const mutation_branch &m ) -> nc_trait_group
{
for( const std::string &t : m.types ) {
    if( mutation_type_is_appearance( t ) ) {
            return { .col = 2, .appearance_type = t };
        }
    }
    if( m.points > 0 ) {
    return { .col = 0 };
}
return m.points < 0 ? nc_trait_group{ .col = 1 } : nc_trait_group{ .col = 2 };
}

/// The label a row shows. Under an appearance sub-heading the group is already named by the
/// heading, so "Eye color: amber" repeats it — strip to the bare option.
///
/// Prefer the AUTHORED short form: `apperance_description` says "afro" where the name says
/// "Hair style: 'fro", and every eye colour, hair colour, hair style and most skin tones declare
/// one. The 28 facial-hair traits declare none, so fall back to splitting at the first ": " —
/// punctuation, NOT a word. Prefix-matching a translated name is the mistake the SCENARIO grouping
/// exists to avoid, and a locale that writes no colon simply keeps the full name here: a worse
/// label, never a wrong one.
auto nc_trait_row_label( const mutation_branch &m, bool in_appearance ) -> std::string
{
    if( !in_appearance ) {
    return m.name();
    }
    const std::string bare = m.apperance_desc();
    if( !bare.empty() ) {
        return bare;
    }
    const std::string full = m.name();
    const size_t sep = full.find( ": " );
    return sep == std::string::npos ? full : full.substr( sep + 2 );
}

auto nc_trait_group_name( const nc_trait_group &g ) -> std::string
{
    if( !g.appearance_type.empty() ) {
    // Already translated, and now covers facial_hair too.
    return mutation_type_display_name( g.appearance_type );
    }
    switch( g.col ) {
    case 0:
        return _( "Advantages" );
        case 1:
            return _( "Disadvantages" );
        default:
            return _( "Neutral" );
    }
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
        nc_trait_group grp;
        bool avatar_has;
        bool conflicts;
        bool forbidden;
    };
    // ONE flat list; the columns are a VIEW over it. That keeps sorting and every id-based lookup
    // independent of how the screen happens to be grouped.
    std::vector<trait_entry> starting_traits;

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
            const nc_trait_group grp = nc_classify_trait( traits_iter );
            // Budget totals key off the POINT SIGN, never off which column the trait landed in:
            // an appearance type with a stray cost sits in the pickers column but still counts
            // against the same budget as any other trait of that sign.
            if( u.has_trait( traits_iter.id ) ) {
                if( traits_iter.points > 0 ) {
                    num_good += traits_iter.points;
                } else if( traits_iter.points < 0 ) {
                    num_bad += traits_iter.points;
                }
            }
            starting_traits.push_back( { .id = traits_iter.id, .grp = grp, .avatar_has = false,
                                         .conflicts = false,
                                         .forbidden = g->scen->is_forbidden_trait( traits_iter.id ) } );
        }
    }

    std::ranges::sort( starting_traits, []( const trait_entry & a, const trait_entry & b ) {
        return trait_display_nocolor_sort( a.id, b.id );
    } );

    // Sub-headings for column 2, in get_all_mutation_type_ids() order (the mutation_types map, so
    // alphabetical by id: arbitrary but deterministic and stable however mods load). Neutral leads,
    // because it is gameplay and the pickers are not. Empty groups are dropped.
    std::vector<nc_trait_group> col2_groups;
    {
        const auto group_has = [&]( const nc_trait_group & g ) {
            return std::ranges::any_of( starting_traits,
            [&g]( const trait_entry & e ) { return e.grp == g; } );
        };
        if( group_has( { .col = 2 } ) ) {
            col2_groups.push_back( { .col = 2 } );
        }
        for( const std::string &t : get_all_mutation_type_ids() ) {
            if( !mutation_type_is_appearance( t ) ) {
                continue;
            }
            const nc_trait_group g{ .col = 2, .appearance_type = t };
            if( group_has( g ) ) {
                col2_groups.push_back( g );
            }
        }
    }

    const auto recalc_display_cache = [&]() {
        for( trait_entry &entry : starting_traits ) {
            entry.conflicts = newcharacter::has_conflicting_trait( u, entry.id );
            entry.avatar_has = u.has_trait( entry.id );
        }
    };
    recalc_display_cache();

    // Each column's rows, as indices into starting_traits. -1 marks a sub-heading, which occupies
    // a row so that every row is the same height — see nc_trait_row.
    std::array<std::vector<int>, 3> col_rows;
    const auto rebuild_col_rows = [&]() {
        for( std::vector<int> &v : col_rows ) {
            v.clear();
        }
        for( int i = 0; i < static_cast<int>( starting_traits.size() ); i++ ) {
            const int c = starting_traits[i].grp.col;
            if( c != 2 ) {
                col_rows[c].push_back( i );
            }
        }
        // Column 2 is grouped, so it is assembled heading by heading rather than in flat order.
        for( const nc_trait_group &g : col2_groups ) {
            col_rows[2].push_back( -1 );
            for( int i = 0; i < static_cast<int>( starting_traits.size() ); i++ ) {
                if( starting_traits[i].grp == g ) {
                    col_rows[2].push_back( i );
                }
            }
        }
    };
    rebuild_col_rows();

    // Which sub-heading each column-2 row sits under, parallel to col_rows[2], so a heading row can
    // name itself without re-deriving the grouping.
    std::vector<int> col2_head_of( col_rows[2].size(), 0 );
    {
        int cur = -1;
        for( size_t r = 0; r < col_rows[2].size(); r++ ) {
            if( col_rows[2][r] < 0 ) {
                cur++;
            }
            col2_head_of[r] = std::max( 0, cur );
        }
    }

    int cur_col = 0;
    std::array<int, 3> cur_row = { 0, 0, 0 };

    const auto col_len = [&]( int c ) {
        return static_cast<int>( col_rows[c].size() );
    };
    /// The trait on a given row, or -1 for a heading or an out-of-range row.
    const auto trait_at = [&]( int c, int r ) {
        return ( r >= 0 && r < col_len( c ) ) ? col_rows[c][r] : -1;
    };
    // Column 2 opens on a heading, so step off it before anything reads the cursor.
    const auto skip_headings = [&]( int c, int dir ) {
        int guard = col_len( c );
        while( guard-- > 0 && trait_at( c, cur_row[c] ) < 0 ) {
            cur_row[c] += dir;
            if( cur_row[c] < 0 ) {
                cur_row[c] = col_len( c ) - 1;
            } else if( cur_row[c] >= col_len( c ) ) {
                cur_row[c] = 0;
            }
        }
    };
    skip_headings( 2, 1 );

    ui_adaptor ui;
    catacurses::window w;

    character_preview_window character_preview;
    character_preview.init( &u );
    const bool use_character_preview = get_option<bool>( "USE_CHARACTER_PREVIEW" );

    const auto init_windows = [&]( ui_adaptor & ui ) {
        w = catacurses::newwin( TERMY, TERMX, point_zero );
        if( use_character_preview ) {
            nc_prepare_preview( character_preview );
        }
        ui.position_from_window( w );
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
    // Mouse MOTION has to wake this loop, or the appearance preview only catches up when the
    // pointer stops. Motion resolves to CATA_ERROR, and input.cpp:894-897 `continue`s on an
    // unrecognised MOUSE event without returning — and because each pass restarts the wait, a
    // moving pointer starves the timeout indefinitely, so handle_input never returns and the loop
    // never reaches redraw(). It reads as a preview that lags a second behind the cursor.
    //
    // BOTH of these are needed. COORDINATE sets handling_coordinate_input, which is what skips that
    // early `continue`; ANY_INPUT is what then makes the fall-through at :912 return rather than
    // loop again. Either alone still parks the loop.
    ctxt.register_action( "COORDINATE" );
    ctxt.register_action( "ANY_INPUT" );

    // RmlUi render path (render-only; keyboard owns nav/confirm/reroll below).
    auto data = std::make_unique<nc_traits_session>();
    rml_doc rml;
    // Set by the arrow click callbacks, consumed by the input loop below. Not a
    // tab_direction: it is translated into an action string so the existing
    // keyboard handling stays the single place navigation is decided.
    int nc_nav = 0;
    // Click intent, applied ONCE per input cycle. `data-event-*` installs a listener per
    // generated element and a `data-for` regeneration adds another without removing the old, so a
    // callback that mutated directly would run an unbounded number of times per click — measured
    // at 15 on the SCENARIO tab. See plans/charcreation-scenario-tree.md.
    int pending_row_col = -1;
    int pending_row = -1;
    int pending_check_col = -1;
    int pending_check_row = -1;
    // Hover intent for the appearance preview. Unlike the click intents this is NOT cleared each
    // cycle: a hover persists until the pointer moves to another row or leaves the list, so the
    // preview must survive the quiet frames in between. -1 is "nothing hovered".
    int pending_hover = -2;   //< -2 = no mouseover this cycle, -1 = mouse left the list
    int hover_flat = -1;      //< index into starting_traits, or -1

    // Why a trait can or cannot be toggled right now. ONE source of truth: the row's checkbox
    // glyph, the Status fact's reason line and CONFIRM's refusal popups all read this, so what a
    // row shows cannot promise something the keypress then refuses. Before this rework the
    // refusals existed only as popups raised AFTER CONFIRM, and conflicted / forbidden /
    // bionic-blocked / over-budget all rendered as the same dark gray row.
    //
    // This lambda only ASKS the game the questions; the precedence between the answers lives in
    // newchar_trait_gate.h, where it is tested without needing an avatar.
    const auto gate_of = [&]( const trait_id & tid ) {
        const mutation_branch &m = tid.obj();
        nc_trait_gate::inputs in;
        in.taken = u.has_trait( tid );
        in.locked = g->scen->is_locked_trait( tid ) || u.prof->is_locked_trait( tid );
        in.mandatory = std::ranges::any_of( m.types, []( const std::string & t ) {
            return mutation_type_is_mandatory( t );
        } );
        in.conflicts = newcharacter::has_conflicting_trait( u, tid );
        in.swaps = std::ranges::any_of( m.types, []( const std::string & t ) {
            return mutation_type_swaps_on_conflict( t );
        } );
        const auto base = u.get_base_traits();
        in.has_swap_holder = std::ranges::any_of( base, [&]( const trait_id & tr ) {
            return tr != tid && std::ranges::any_of( tr.obj().types,
            [&]( const std::string & t ) { return m.types.contains( t ); } );
        } );
        in.scen_forbids = g->scen->is_forbidden_trait( tid );
        in.prof_forbids = u.prof->is_forbidden_trait( tid );
        in.bionic_blocks = !bionics_cancelling_trait( u.prof->CBMs(), tid ).empty() ||
                           !bionics_cancelling_trait( u.get_bionics(), tid ).empty();
        in.points = m.points;
        in.num_good = num_good;
        in.num_bad = num_bad;
        in.max_points = max_trait_points;
        in.freeform = points.is_freeform();
        return nc_trait_gate::evaluate( in );
    };
    /// The bionics in the way, for the popup that names them. Only asked once a refusal is certain,
    /// because building the list on every row of every frame would be wasteful.
    const auto blocking_bionics_of = [&]( const trait_id & tid ) {
        std::vector<bionic_id> out = bionics_cancelling_trait( u.prof->CBMs(), tid );
        for( const bionic_id &b : bionics_cancelling_trait( u.get_bionics(), tid ) ) {
            out.push_back( b );
        }
        return out;
    };
    // The ONE place a trait is taken or dropped. Keyboard CONFIRM and the card's TAKE/DROP control
    // both come here, so the two cannot drift — the STATS steppers set the same precedent.
    // Every popup below fires for exactly the case it fired for before this rework.
    const auto toggle_trait_at = [&]( int flat_idx ) {
        if( flat_idx < 0 || flat_idx >= static_cast<int>( starting_traits.size() ) ) {
            return;
        }
        const trait_id cur_trait = starting_traits[flat_idx].id;
        const mutation_branch &mdata = cur_trait.obj();
        const nc_trait_gate::state gt = gate_of( cur_trait );
        int inc_type = 0;

        if( gt.taken ) {
            if( g->scen->is_locked_trait( cur_trait ) ) {
                popup( _( "Your scenario of %s prevents you from removing this trait." ),
                       g->scen->gender_appropriate_name( u.male ) );
            } else if( u.prof->is_locked_trait( cur_trait ) ) {
                popup( _( "Your profession of %s prevents you from removing this trait." ),
                       u.prof->gender_appropriate_name( u.male ) );
            } else {
                const auto mandatory_type = std::ranges::find_if( mdata.types,
                []( const std::string & t ) { return mutation_type_is_mandatory( t ); } );
                if( mandatory_type != mdata.types.end() ) {
                    popup( _( "You need to select 1 %s." ), mutation_type_display_name( *mandatory_type ) );
                } else {
                    inc_type = -1;
                }
            }
        } else if( gt.conflicts ) {
            if( gt.can_swap ) {
                // Drop whatever of this type is already held, then take the new one.
                const auto base_traits = u.get_base_traits();
                const auto it = std::ranges::find_if( base_traits, [&]( const trait_id & tr ) {
                    return tr != cur_trait && std::ranges::any_of( tr.obj().types,
                    [&]( const std::string & t ) { return mdata.types.contains( t ); } );
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
        } else if( gt.scen_forbids ) {
            popup( _( "The scenario you picked prevents you from taking this trait!" ) );
        } else if( gt.prof_forbids ) {
            popup( _( "Your profession of %s prevents you from taking this trait." ),
                   u.prof->gender_appropriate_name( u.male ) );
        } else if( gt.bionic_blocks ) {
            // Name them, so the player can see what is in the way rather than just that something is.
            const std::vector<bionic_id> blockers = blocking_bionics_of( cur_trait );
            std::vector<std::string> conflict_names;
            conflict_names.reserve( blockers.size() );
            for( const bionic_id &conflict : blockers ) {
                conflict_names.emplace_back( conflict->name.translated() );
            }
            popup( _( "The following bionics prevent you from taking this trait: %s." ),
                   enumerate_as_string( conflict_names ) );
        } else if( gt.over_budget && mdata.points > 0 ) {
            popup( vgettext( "Sorry, but you can only take %d point of advantages.",
                             "Sorry, but you can only take %d points of advantages.", max_trait_points ),
                   max_trait_points );
        } else if( gt.over_budget ) {
            popup( vgettext( "Sorry, but you can only take %d point of disadvantages.",
                             "Sorry, but you can only take %d points of disadvantages.", max_trait_points ),
                   max_trait_points );
        } else {
            inc_type = 1;
        }

        if( inc_type != 0 ) {
            const bool had_trait = gt.taken;
            u.toggle_trait( cur_trait );
            // A dropped trait may have blocked clothing; re-toggle so the preview re-kits.
            if( had_trait && character_preview.clothes_showing() ) {
                character_preview.toggle_clothes();
                character_preview.toggle_clothes();
            }
            points.trait_points -= mdata.points * inc_type;
            if( mdata.points > 0 ) {
                num_good += mdata.points * inc_type;
            } else if( mdata.points < 0 ) {
                num_bad += mdata.points * inc_type;
            }
        }
        recalc_display_cache();
    };

    // ── DNA STRAND ────────────────────────────────────────────────────────────
    //
    // A double helix beside the lists, spinning slowly, with one dot pair lit per trait the player
    // has taken — so the genome visibly fills in as the character is built. Geometry is in
    // newchar_dna.h; this only turns fractions into lengths and decides which rungs are marked.
    //
    // Rebuilt every animation tick, which is why it is separate from the model sync: rebuilding
    // 188 colour-tagged rows at 30fps to move some dots would be absurd.
    const auto anim_start = std::chrono::steady_clock::now();
    const auto sync_dna = [&]() {
        if( !data->handle ) {
            return;
        }
        // Wall clock, not a frame counter: the spin must not speed up because the player is
        // holding a key down, and must not stall while they are not.
        const float secs = std::chrono::duration<float>(
                               std::chrono::steady_clock::now() - anim_start ).count();
        const float phase = nc_dna::phase_at( secs );

        // Which rungs are lit. The i-th taken trait lights rung i, in the flat sorted order, so a
        // given character always lights the same rungs rather than reshuffling as points change.
        std::array<int, nc_dna::rungs> mark = {};
        mark.fill( 0 );
        int taken = 0;
        for( const trait_entry &e : starting_traits ) {
            if( !e.avatar_has ) {
                continue;
            }
            const int slot = taken % nc_dna::rungs;
            const mutation_branch &m = e.id.obj();
            // 1 advantage, 2 disadvantage, 3 appearance. A later trait overwriting an earlier one
            // only happens past 22 picks, and then the strand is full anyway.
            mark[slot] = !e.grp.appearance_type.empty() ? 3 : ( m.points > 0 ? 1 : ( m.points < 0 ? 2 : 3 ) );
            taken++;
        }

        data->dna.clear();
        for( int i = 0; i < nc_dna::rungs; i++ ) {
            const nc_dna::span sp = nc_dna::span_of( nc_dna::at( i, phase ) );
            nc_dna_rung r;
            // Fractions scaled to the strand's travel; the coupling to the stylesheet's column
            // width is documented on nc_dna::travel_dp.
            r.gap = string_format( "%.1fdp", sp.left * nc_dna::travel_dp );
            r.bond = string_format( "%.1fdp", sp.width * nc_dna::travel_dp );
            r.left_front = sp.left_front;
            r.mark_good = mark[i] == 1;
            r.mark_bad = mark[i] == 2;
            r.mark_cosm = mark[i] == 3;
            data->dna.push_back( r );
        }
        data->dna_count_rml = cata_text_to_rml( colorize( string_format( "%d", taken ),
                                                c_dark_gray ) );
        data->handle.DirtyVariable( "dna" );
        data->handle.DirtyVariable( "dna_count_rml" );
    };
    const auto sync_rml = [&]() {
        if( !data->handle ) {
            return;
        }
        data->tabs = build_nc_char_tabs<nc_traits_tab>( 4 );  // TRAITS tab active
        data->shell = fill_nc_shell( 4, ctxt );
        set_nc_portrait( data->shell, use_character_preview );
        data->points_rml = cata_text_to_rml( nc_points_line( points ) );
        data->balance = nc_make_balance( num_good, num_bad, max_trait_points,
                                         points.is_freeform() );
        data->hint_rml = cata_text_to_rml( string_format(
                                               _( "<color_light_green>%s</color> take or drop · <color_light_green>%s</color> column · <color_light_green>%s</color> reroll" ),
                                               ctxt.get_desc( "CONFIRM", 1 ), _( "left/right" ),
                                               ctxt.get_desc( "REROLL_CHARACTER", 1 ) ) );
        // The reference's meta-bar readout: a dim label with the number bright beside it.
        data->budget_rml = cata_text_to_rml( string_format(
                _( "<color_dark_gray>Trait points left:</color> <color_white>%d</color>" ),
                points.trait_points_left() ) );

        // In words, why this trait cannot be toggled — the same conditions CONFIRM's popups use,
        // stated where the decision is made instead of after it. Empty when it can be.
        const auto refusal_of = []( const nc_trait_gate::state & gt ) -> std::string {
            if( gt.taken )
        {
            if( gt.locked ) {
                    return _( "Your profession or scenario will not let you drop this." );
                }
                return gt.mandatory ? _( "You must keep one of these." ) : std::string();
            }
            if( gt.conflicts && !gt.can_swap )
        {
            return _( "Conflicts with a trait you already have." );
            }
            if( gt.scen_forbids )
        {
            return _( "Your scenario forbids this trait." );
            }
            if( gt.prof_forbids )
        {
            return _( "Your profession forbids this trait." );
            }
            if( gt.bionic_blocks )
        {
            return _( "A bionic you start with blocks this trait." );
            }
            return gt.over_budget ? _( "No points left on that side of the budget." ) : std::string();
        };

        // One row. The cells are fixed-width by stylesheet, so cursor, box, cost and name each form
        // a column the eye can run down; that alignment is the whole readability argument.
        const auto build_row = [&]( int flat_idx, bool is_cursor ) {
            const trait_entry &e = starting_traits[flat_idx];
            const mutation_branch &m = e.id.obj();
            const nc_trait_gate::state gt = gate_of( e.id );
            nc_trait_row r;
            r.cursor_rml = cata_text_to_rml( is_cursor ? colorize( ">", c_yellow ) : std::string() );
            // [x] held · [ ] free to take · [-] refused, and the panel says why.
            r.check_rml = gt.taken
                          ? cata_text_to_rml( colorize( "[x]", c_yellow ) )
                          : ( gt.toggleable() ? cata_text_to_rml( colorize( "[ ]", c_light_gray ) )
                              : cata_text_to_rml( colorize( "[-]", c_dark_gray ) ) );
            if( m.points != 0 ) {
                // Green for an advantage, red for a disadvantage: the same valence the balance
                // scale's two pans use, so the column and the scale agree at a glance.
                r.cost_rml = cata_text_to_rml( colorize( string_format( "[%+d]", m.points ),
                                               m.points > 0 ? COL_TR_GOOD : COL_TR_BAD ) );
            }
            const nc_color name_col = gt.taken ? c_white
                                      : ( gt.toggleable() ? c_light_gray : c_dark_gray );
            r.name_rml = cata_text_to_rml( colorize(
                                               nc_trait_row_label( m, !e.grp.appearance_type.empty() ),
                                               name_col ) );
            r.selected = is_cursor;
            return r;
        };

        nc_trait_col *cols[3] = { &data->col0, &data->col1, &data->col2 };
        for( int c = 0; c < 3; c++ ) {
            nc_trait_col &dc = *cols[c];
            dc.rows.clear();
            int items = 0;
            for( int r = 0; r < col_len( c ); r++ ) {
                const int flat = col_rows[c][r];
                if( flat < 0 ) {
                    // Sub-heading. Same row height as an item, which is what keeps the scroll
                    // arithmetic exact; weight and tracking carry the hierarchy instead.
                    nc_trait_row hr;
                    hr.header = true;
                    hr.name_rml = cata_text_to_rml( colorize(
                                                        nc_trait_group_name( col2_groups[col2_head_of[r]] ), c_yellow ) );
                    dc.rows.push_back( hr );
                    continue;
                }
                items++;
                dc.rows.push_back( build_row( flat, c == cur_col && r == cur_row[c] ) );
            }
            dc.name_rml = cata_text_to_rml( colorize(
                                                c == 0 ? _( "Advantages" )
                                                : ( c == 1 ? _( "Disadvantages" ) : _( "Appearance" ) ),
                                                c == cur_col ? c_white : c_light_gray ) );
            dc.count_rml = cata_text_to_rml( colorize( string_format( "%d", items ), c_dark_gray ) );
        }

        // Detail panel for the trait under the cursor.
        data->facts.clear();
        const int sel_flat = trait_at( cur_col, cur_row[cur_col] );
        if( sel_flat >= 0 ) {
            const trait_id tid = starting_traits[sel_flat].id;
            const mutation_branch &wmd = tid.obj();
            const nc_trait_gate::state gt = gate_of( tid );
            // ":: NAME" — the reference's detail header. Uppercasing is left to the stylesheet so
            // no locale gets a hand-rolled case conversion.
            data->sel_name_rml = cata_text_to_rml( colorize( string_format( ":: %s", wmd.name() ),
                                                   c_white ) );
            const auto add_fact = [&]( const std::string & label, const std::string & value,
            const nc_color & col, const std::string & sub = std::string() ) {
                data->facts.push_back( {
                    .label_rml = cata_text_to_rml( label ),
                    .value_rml = cata_text_to_rml( colorize( value, col ) ),
                    .sub_rml = cata_text_to_rml( sub ) } );
            };
            const int pts = std::abs( wmd.points );
            if( wmd.points == 0 ) {
                add_fact( _( "Cost" ), _( "Free" ), c_light_gray );
            } else {
                add_fact( _( "Cost" ),
                          string_format( vgettext( "%d point", "%d points", pts ), pts ),
                          wmd.points > 0 ? COL_TR_GOOD : COL_TR_BAD );
            }
            add_fact( _( "Group" ), nc_trait_group_name( starting_traits[sel_flat].grp ),
                      c_light_gray );
            const std::string refusal = refusal_of( gt );
            add_fact( _( "Status" ),
                      gt.taken ? _( "Taken" ) : ( gt.toggleable() ? _( "Available" ) : _( "Unavailable" ) ),
                      gt.taken ? COL_TR_GOOD : ( gt.toggleable() ? c_white : c_light_red ),
                      refusal );
            data->desc_rml = cata_text_to_rml( wmd.desc() );
        } else {
            data->sel_name_rml.clear();
            data->desc_rml.clear();
        }

        data->handle.DirtyVariable( "tabs" );
        dirty_nc_shell( data->handle );
        data->handle.DirtyVariable( "points_rml" );
        data->handle.DirtyVariable( "budget_rml" );
        data->handle.DirtyVariable( "balance" );
        data->handle.DirtyVariable( "col0" );
        data->handle.DirtyVariable( "col1" );
        data->handle.DirtyVariable( "col2" );
        data->handle.DirtyVariable( "sel_name_rml" );
        data->handle.DirtyVariable( "facts" );
        data->handle.DirtyVariable( "desc_rml" );
        data->handle.DirtyVariable( "hint_rml" );
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
        c.Bind( "budget_rml", &data->budget_rml );
        c.Bind( "balance", &data->balance );
        c.Bind( "col0", &data->col0 );
        c.Bind( "col1", &data->col1 );
        c.Bind( "col2", &data->col2 );
        // The strand. Easy to forget, and it fails SILENTLY: `data-for` over an unbound name
        // renders nothing at all, so the GENOME heading appeared with an empty count and no rungs
        // beneath it while sync_dna was happily filling and dirtying the array every frame.
        c.Bind( "dna", &data->dna );
        c.Bind( "dna_count_rml", &data->dna_count_rml );
        c.Bind( "sel_name_rml", &data->sel_name_rml );
        c.Bind( "facts", &data->facts );
        c.Bind( "desc_rml", &data->desc_rml );
        c.Bind( "hint_rml", &data->hint_rml );
        // Click callbacks RECORD INTENT and mutate nothing — see the comment on pending_row_col.
        c.BindEventCallback( "on_row",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int col = -1;
            int row = -1;
            if( args.size() >= 2 ) {
                args[0].GetInto( col );
                args[1].GetInto( row );
            }
            if( col >= 0 && col < 3 && row >= 0 ) {
                pending_row_col = col;
                pending_row = row;
            }
        } );
        // The checkbox, not the row, is what toggles: it is a deliberate 26dp target for an action
        // that spends or refunds points and can raise a modal. Clicking the row only moves the
        // cursor, so reading a trait is never the same gesture as taking it.
        c.BindEventCallback( "on_check",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int col = -1;
            int row = -1;
            if( args.size() >= 2 ) {
                args[0].GetInto( col );
                args[1].GetInto( row );
            }
            if( col >= 0 && col < 3 && row >= 0 ) {
                pending_check_col = col;
                pending_check_row = row;
            }
        } );
        // Hovering an appearance option previews it on the portrait, so a hair style or eye colour
        // can be judged before spending a click. Records intent only, like every other callback here.
        //
        // NOT filtered on the event target: `mouseover` bubbles, so entering one of the row's child
        // spans arrives here with the row as current element, and that is exactly right — the spans
        // are the whole row's area. It reports the same row index either way, so it is idempotent.
        c.BindEventCallback( "on_hover",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int row = -1;
            if( !args.empty() ) {
                args[0].GetInto( row );
            }
            pending_hover = row;
        } );
        // The pointer leaving the list ends the preview — but ONLY when the list itself is what was
        // left. `mouseout` bubbles too (RmlUi EventSpecification.cpp:16 declares it bubbles=true), so
        // stepping from one row to the next fires mouseout on the row being left and it arrives here
        // as well. Honouring that made hover_flat oscillate row -> -1 -> row, and since -1 falls back
        // to the cursor row the portrait visibly alternated between the option worn and the option
        // under the pointer. Comparing target to current element is what distinguishes the two.
        c.BindEventCallback( "on_hover_out",
        [&]( Rml::DataModelHandle, Rml::Event & ev, const Rml::VariantList & ) {
            if( ev.GetTargetElement() == ev.GetCurrentElement() ) {
                pending_hover = -1;
            }
        } );
        data->handle = c.GetModelHandle();
    } );
    if( rml_doc_unavailable( rml, _( "Character creation (TRAITS tab)" ) ) ) {
        return tab_direction::QUIT;
    }

    // ── APPEARANCE PREVIEW ────────────────────────────────────────────────────
    //
    // The hovered — or cursored — appearance option is worn by the avatar for the duration of ONE
    // portrait draw, so the picture shows what the option looks like before a click is spent.
    //
    // Scoped to the draw, NOT held between frames. An earlier version applied it once per change and
    // left it on, which is cheaper — but frames are presented from paths this loop does not own, and
    // any of them catching the avatar mid-preview made the portrait flicker between the option worn
    // and the option highlighted. Nothing outside this lambda can observe a previewed character now,
    // which also removes three ordering hazards that version needed rules for: a click landing while
    // the preview was applied (gate_of would see `taken` and DROP the option it was asked to take),
    // sync_rml building rows from a previewed character (a row rendering `[x]` for an option nobody
    // owns), and a reroll replacing the character with a preview outstanding.
    //
    // Safe against the POINT BUDGET for a reason that has nothing to do with the trait's cost:
    // `Character::toggle_trait` does not know the creator's budget exists. `points.trait_points`,
    // `num_good` and `num_bad` are written ONLY by the local toggle_trait_at lambda, and this
    // bypasses it. So a preview cannot mis-charge anything even for a modded appearance trait that
    // carries a point cost — which nc_classify_trait deliberately allows into this column.
    //
    // Do NOT "fix" this by routing it through toggle_trait_at because some appearance trait is not
    // free: that lambda is what moves the budget, and charging it per frame is real corruption.
    const auto draw_preview = [&]() {
        // The pointer wins while it is over the list; otherwise the cursor row previews, which is the
        // only way this works at all for a keyboard user.
        const int flat = hover_flat >= 0 ? hover_flat : trait_at( cur_col, cur_row[cur_col] );
        trait_id want = trait_id::NULL_ID();
        trait_id displaced = trait_id::NULL_ID();
        if( flat >= 0 && flat < static_cast<int>( starting_traits.size() ) ) {
            const trait_entry &e = starting_traits[flat];
            // Already worn needs no preview, and previewing it would toggle it OFF.
            if( !e.grp.appearance_type.empty() && !u.has_trait( e.id ) ) {
                want = e.id;
                // Whatever of that type is worn comes off, or a mandatory_one type renders two
                // options at once.
                for( const trait_id &tr : u.get_base_traits() ) {
                    if( tr != want && tr.obj().types.contains( e.grp.appearance_type ) ) {
                        displaced = tr;
                        break;
                    }
                }
            }
        }
        const bool previewing = want != trait_id::NULL_ID();
        if( previewing ) {
            if( displaced != trait_id::NULL_ID() ) {
                u.toggle_trait( displaced );
            }
            u.toggle_trait( want );
        }
        character_preview.display();
        // Reversed in the opposite order, so the character ends up exactly as it started.
        if( previewing ) {
            u.toggle_trait( want );
            if( displaced != trait_id::NULL_ID() ) {
                u.toggle_trait( displaced );
            }
        }
    };

    // The model is rebuilt only when something changed; the strand every frame. Without this split
    // a quiet tick would re-colour 188 rows and regenerate 188 data-for elements to move some dots.
    bool model_dirty = true;
    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            if( model_dirty ) {
                // Built from the character the player actually has, which is automatic now that the
                // preview exists only inside draw_preview: gate_of asks `u.has_trait`, so a preview
                // outstanding here would render its row's box as [x] in gold with STATUS "Taken" for
                // an option nobody owns. The list describes the real character; the portrait shows
                // what the highlighted option would look like.
                sync_rml();
                model_dirty = false;
            }
            sync_dna();
            // display() is RmlUi-aware (cp_rml_open/cp_rml_position + a GPU sprite
            // for the avatar), so it composes over this document rather than
            // fighting it. It was skipped only because this branch returned early.
            if( use_character_preview ) {
                draw_preview();
            }
            return;
        }
    } );

    // Keeps the cursor row on screen. Every row is the SAME height by stylesheet rule — headings
    // included — so row height is scroll_height / row_count exactly, with no DOM child indexing,
    // which `data-for` makes unreliable anyway. Same mechanism as the equipment sheet's tree.
    const auto scroll_col_to_cursor = [&]() {
        if( !rml ) {
            return;
        }
        const int rows = col_len( cur_col );
        if( rows <= 0 ) {
            return;
        }
        Rml::Element *e = rml.document()->GetElementById(
                              string_format( "nc-col%d", cur_col ) );
        if( e == nullptr ) {
            return;
        }
        const float page = e->GetClientHeight();
        const float total = e->GetScrollHeight();
        const float row_h = total / static_cast<float>( rows );
        const float want = row_h * static_cast<float>( cur_row[cur_col] ) - page * 0.5f;
        e->SetScrollTop( std::clamp( want, 0.0f, std::max( 0.0f, total - page ) ) );
    };

    // Open on a column that has something in it, cursor on a real trait rather than a heading.
    for( int c = 0; c < 3; c++ ) {
        if( col_len( c ) > 0 ) {
            cur_col = c;
            break;
        }
    }
    skip_headings( cur_col, 1 );


    do {
        ui_manager::redraw();
        nc_nav = 0;
        pending_row_col = -1;
        pending_row = -1;
        pending_check_col = -1;
        pending_check_row = -1;
        pending_hover = -2;
        std::string action = ctxt.handle_input();
        if( nc_nav != 0 ) {
            action = nc_nav < 0 ? "PREV_TAB" : "NEXT_TAB";
        }
        // Rebuild the model on the next redraw after anything that can have changed it.
        //
        // The click intents MUST be part of this test, not just `action`: cata maps MOUSE_LEFT to
        // SELECT on mouse DOWN while RmlUi fires its `click` — and therefore those callbacks — on
        // mouse UP, so the iteration carrying a click's intent is usually an idle one. Gating on the
        // action alone left every mouse toggle applied but unrendered.
        //
        // TIMEOUT and ANY_INPUT are both idle as far as the MODEL goes — ANY_INPUT is what every
        // mouse motion now returns, and rebuilding 188 rows per pointer move is exactly the cost
        // this split exists to avoid. The strand and the preview still update, because those are
        // outside the gate.
        if( ( action != "TIMEOUT" && action != "ANY_INPUT" ) ||
            pending_row >= 0 || pending_check_row >= 0 ) {
            model_dirty = true;
        }
        // No preview state to unwind before acting on input: draw_preview applies and removes it
        // inside a single draw, so the avatar is always its true self by the time anything here runs
        // — including toggle_trait_at, which would otherwise see the previewed option as `taken` and
        // drop the very thing the click asked it to take.
        // Hover intent. -2 means no mouseover arrived this cycle, so the existing preview stands;
        // -1 is the pointer leaving the list, which ends it. Column 2 is the only one that reports.
        if( pending_hover != -2 ) {
            const int flat = pending_hover >= 0 ? trait_at( 2, pending_hover ) : -1;
            if( flat != hover_flat ) {
                hover_flat = flat;
            }
        }
        // Keyboard navigation takes the preview BACK from the pointer. hover_flat otherwise persists
        // until the pointer leaves the list, and draw_preview gives hover priority — so with the
        // pointer resting anywhere over APPEARANCE, which is where it ends up after hovering, the
        // cursor would move while the portrait stayed on the last hovered row. Applied after the
        // hover intent above, so a key wins even when motion arrived in the same poll.
        if( action == "UP" || action == "DOWN" || action == "LEFT" || action == "RIGHT" ||
            action == "RANDOMIZE" ) {
            hover_flat = -1;
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

        // Apply click intent exactly once, however many times the callback ran.
        if( pending_row_col >= 0 && pending_row >= 0 && pending_row < col_len( pending_row_col ) &&
            trait_at( pending_row_col, pending_row ) >= 0 ) {
            cur_col = pending_row_col;
            cur_row[cur_col] = pending_row;
        }
        // The checkbox last, so the cursor has already moved to the row being acted on.
        if( pending_check_col >= 0 && pending_check_row >= 0 &&
            pending_check_row < col_len( pending_check_col ) ) {
            const int flat = trait_at( pending_check_col, pending_check_row );
            if( flat >= 0 ) {
                cur_col = pending_check_col;
                cur_row[cur_col] = pending_check_row;
                toggle_trait_at( flat );
            }
        }

        // LEFT/RIGHT change column and UP/DOWN move within it — the axis mapping this tab has
        // always had, and now the one the three-column layout implies.
        if( action == "LEFT" || action == "RIGHT" ) {
            const int step = action == "RIGHT" ? 1 : 2;   // +1 / -1 modulo 3
            for( int n = 0; n < 3; n++ ) {
                cur_col = ( cur_col + step ) % 3;
                if( col_len( cur_col ) > 0 ) {
                    break;
                }
            }
            skip_headings( cur_col, 1 );
            scroll_col_to_cursor();
        } else if( action == "DOWN" || action == "UP" ) {
            const int dir = action == "DOWN" ? 1 : -1;
            const int len = col_len( cur_col );
            if( len > 0 ) {
                cur_row[cur_col] = ( cur_row[cur_col] + dir + len ) % len;
                skip_headings( cur_col, dir );
                scroll_col_to_cursor();
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
            // Return tab_direction::NONE so we re-enter this tab again, but it forces a complete redrawing of it.
            return tab_direction::NONE;
        } else if( action == "RANDOMIZE" ) {
            const int len = col_len( cur_col );
            if( len > 0 ) {
                cur_row[cur_col] = rng( 0, len - 1 );
                skip_headings( cur_col, 1 );
                scroll_col_to_cursor();
            }
        } else if( action == "CONFIRM" ) {
            const int flat = trait_at( cur_col, cur_row[cur_col] );
            if( flat >= 0 ) {
                toggle_trait_at( flat );
            }
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

/// One line of a column: cursor | checkbox | cost | name in fixed-width cells, so each cell forms
/// a micro-column the eye can run down. The TRAITS anatomy, for the same reason — 34 starting
/// bionics are a checklist of name, price and do-I-have-it.
struct nc_bio_row {
    Rml::String cursor_rml;   //< ">" on the cursor row, else empty
    Rml::String check_rml;    //< "[x]" held, "[ ]" installable, "[-]" refused
    Rml::String cost_rml;     //< "[+3]" / "[-2]", empty when free
    Rml::String name_rml;
    bool header = false;      //< a region sub-heading, not a selectable implant
    bool selected = false;
};

/// Which region of the chassis an implant occupies. The lists group by this, head to feet, so a
/// column reads top-to-bottom in the same order the diagram beside it does — and so a region
/// heading in the list names something the player can point at on the body.
///
/// `count` is the sentinel the group loop iterates to; `none` deliberately sorts last, since an
/// implant with no site is the exception rather than the opening act.
enum class nc_bio_region : int {
    head = 0,
    torso,
    arms,
    legs,
    none,
    count
};

/// Head-to-feet order, which is also the tie-break when an implant claims two parts equally.
/// These twelve are exactly the parts that declare `bionic_slots`, i.e. every site a bionic can
/// occupy, and they are the same twelve the chassis grid draws.
constexpr std::array<std::pair<body_part, nc_bio_region>, 12> nc_bio_part_regions = {{
        { bp_head, nc_bio_region::head },
        { bp_eyes, nc_bio_region::head },
        { bp_mouth, nc_bio_region::head },
        { bp_torso, nc_bio_region::torso },
        { bp_arm_l, nc_bio_region::arms },
        { bp_arm_r, nc_bio_region::arms },
        { bp_hand_l, nc_bio_region::arms },
        { bp_hand_r, nc_bio_region::arms },
        { bp_leg_l, nc_bio_region::legs },
        { bp_leg_r, nc_bio_region::legs },
        { bp_foot_l, nc_bio_region::legs },
        { bp_foot_r, nc_bio_region::legs }
    }
};

/// The region an implant belongs to: that of the part it claims the MOST space in, which is the
/// same rule the diagram's boxes use to pick the implant they show. Derived from
/// `occupied_bodyparts` — content data — so a modded CBM files itself, and one that occupies a
/// modded body part falls into `none` rather than being mis-filed.
auto nc_bio_classify( const bionic_data &bio ) -> nc_bio_region
{
    nc_bio_region best = nc_bio_region::none;
    int best_space = 0;
    for( const auto &[bp, region] : nc_bio_part_regions ) {
        const auto it = bio.occupied_bodyparts.find( convert_bp( bp ) );
        if( it != bio.occupied_bodyparts.end() && it->second > best_space ) {
            best_space = it->second;
            best = region;
        }
    }
    return best;
}

auto nc_bio_region_name( nc_bio_region r ) -> std::string
{
    switch( r ) {
    case nc_bio_region::head:
        return _( "Head and face" );
        case nc_bio_region::torso:
            return _( "Torso" );
        case nc_bio_region::arms:
            return _( "Arms and hands" );
        case nc_bio_region::legs:
            return _( "Legs and feet" );
        default:
            return _( "No implant site" );
    }
}

/// One column: a heading, a count, and its rows.
struct nc_bio_col {
    Rml::String name_rml;
    Rml::String count_rml;
    Rml::Vector<nc_bio_row> rows;
};

/// A label / value / sub-line triple in the detail panel.
struct nc_bio_fact {
    Rml::String label_rml;
    Rml::String value_rml;
    Rml::String sub_rml;
};

/// One cell of the chassis diagram. EVERY grid position emits a cell — a `junction` draws the
/// connecting lines and a `blank` holds its column open — so the grid aligns from equal flex alone
/// with no pixel arithmetic, exactly as the profession equipment doll does it.
struct nc_bio_cell {
    Rml::String name_rml;     //< body-part caption
    Rml::String impl_rml;     //< the implant occupying the most space here, or a dash
    /// That implant's CBM sprite. "none", NOT empty, for a part with nothing in it:
    /// `data-style-decorator` evaluates its binding for every cell, and an empty value becomes
    /// `decorator: ;`, which RmlUi logs as a syntax error on every frame. Same rule
    /// nc_tile_sprite_dec follows for a sprite it cannot resolve.
    Rml::String dec = "none";
    Rml::String more_rml;     //< "+N" when further implants share the part
    Rml::String cap_rml;      //< "6/18" used slots
    /// Capacity bar width, as a percentage. "0%", NOT empty, for the same reason `dec` is "none":
    /// `data-style-width` evaluates its binding even for an element `data-if` has hidden, and an
    /// empty value becomes `width: ;` — measured at ~6 RmlUi syntax errors per frame before this.
    Rml::String bar_w = "0%";
    bool show_cap = false;    //< only when CBM_SLOTS_ENABLED is on; it is off by default
    bool filled = false;
    bool want = false;        //< the bionic under the cursor claims this part
    bool over = false;        //< ...and there is not enough room for it
    bool junction = false;    //< spine + rung, no box
    bool blank = false;       //< spacer, nothing drawn
};

/// One row of the chassis: left, centre, right, always three cells. `scan_col` is the sweep's
/// colour for this row and is consumed TWICE — by the bus-rail node beside the row and by the
/// scanline inside each of its boxes — so the rail and the body cannot fall out of step.
struct nc_bio_dollrow {
    Rml::Vector<nc_bio_cell> cells;
    Rml::String scan_col;
};

struct nc_bionics_session {
    Rml::Vector<nc_bionics_tab> tabs;
    nc_shell shell;
    Rml::String points_rml;
    Rml::String budget_rml;   //< "Bionic points left: N", the reference's meta-bar readout
    nc_balance balance;
    /// Three columns, bound separately rather than as an array: each needs a stable element id so
    /// C++ can scroll its own cursor into view.
    nc_bio_col col0;
    nc_bio_col col1;
    nc_bio_col col2;
    bool show_col2 = false;
    Rml::String sel_name_rml;   //< ":: BIONIC NAME" over the detail panel
    /// The detail panel's art slot: the CBM's own sprite, not the avatar. Bionics do not change
    /// how the character looks, the chassis beside the lists already IS the body view, and the
    /// sprite ties the panel to the boxes on the diagram — so the portrait earns its place here
    /// least of the four steps that carry one.
    bool has_art = false;
    /// "none", NOT empty, for the same reason nc_bio_cell::dec is: `data-style-decorator` is
    /// applied on the first frame, BEFORE sync_rml has run, and an empty value logs `decorator: ;`.
    Rml::String art_dec = "none";
    Rml::Vector<nc_bio_fact> facts;
    Rml::String desc_rml;
    Rml::String hint_rml;
    /// The chassis, rebuilt every redraw because it carries the scan sweep. That costs nothing
    /// extra: DataViewStyle skips an unchanged property value (RmlUi DataViewDefault.cpp:168), so
    /// the sprite decorators are not re-parsed while only the scan colour moves, and DataViewFor
    /// only creates elements when the array's SIZE changes — this one is always six by three.
    Rml::Vector<nc_bio_dollrow> doll;
    Rml::String chassis_name_rml;  //< the diagram's heading
    Rml::String chassis_rml;       //< implant count
    Rml::String chassis_cap_rml;   //< total used / total slots, when slots are enforced
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
    // Rows before the column that holds them: a member cannot be registered before its type is.
    Rml::StructHandle<nc_bio_row> rh = c.RegisterStruct<nc_bio_row>();
    rh.RegisterMember( "cursor_rml", &nc_bio_row::cursor_rml );
    rh.RegisterMember( "check_rml", &nc_bio_row::check_rml );
    rh.RegisterMember( "cost_rml", &nc_bio_row::cost_rml );
    rh.RegisterMember( "name_rml", &nc_bio_row::name_rml );
    rh.RegisterMember( "header", &nc_bio_row::header );
    rh.RegisterMember( "selected", &nc_bio_row::selected );
    c.RegisterArray<Rml::Vector<nc_bio_row>>();
    Rml::StructHandle<nc_bio_col> ch = c.RegisterStruct<nc_bio_col>();
    ch.RegisterMember( "name_rml", &nc_bio_col::name_rml );
    ch.RegisterMember( "count_rml", &nc_bio_col::count_rml );
    ch.RegisterMember( "rows", &nc_bio_col::rows );
    Rml::StructHandle<nc_bio_fact> fh = c.RegisterStruct<nc_bio_fact>();
    fh.RegisterMember( "label_rml", &nc_bio_fact::label_rml );
    fh.RegisterMember( "value_rml", &nc_bio_fact::value_rml );
    fh.RegisterMember( "sub_rml", &nc_bio_fact::sub_rml );
    c.RegisterArray<Rml::Vector<nc_bio_fact>>();
    Rml::StructHandle<nc_bio_cell> sh = c.RegisterStruct<nc_bio_cell>();
    sh.RegisterMember( "name_rml", &nc_bio_cell::name_rml );
    sh.RegisterMember( "impl_rml", &nc_bio_cell::impl_rml );
    sh.RegisterMember( "dec", &nc_bio_cell::dec );
    sh.RegisterMember( "more_rml", &nc_bio_cell::more_rml );
    sh.RegisterMember( "cap_rml", &nc_bio_cell::cap_rml );
    sh.RegisterMember( "bar_w", &nc_bio_cell::bar_w );
    sh.RegisterMember( "show_cap", &nc_bio_cell::show_cap );
    sh.RegisterMember( "filled", &nc_bio_cell::filled );
    sh.RegisterMember( "want", &nc_bio_cell::want );
    sh.RegisterMember( "over", &nc_bio_cell::over );
    sh.RegisterMember( "junction", &nc_bio_cell::junction );
    sh.RegisterMember( "blank", &nc_bio_cell::blank );
    c.RegisterArray<Rml::Vector<nc_bio_cell>>();
    Rml::StructHandle<nc_bio_dollrow> dh = c.RegisterStruct<nc_bio_dollrow>();
    dh.RegisterMember( "cells", &nc_bio_dollrow::cells );
    dh.RegisterMember( "scan_col", &nc_bio_dollrow::scan_col );
    c.RegisterArray<Rml::Vector<nc_bio_dollrow>>();
    g_nc_bionics_types_registered = true;
}

} // namespace

tab_direction set_bionics( avatar &u, points_left &points )
{
    const int max_trait_points = get_option<int>( "MAX_TRAIT_POINTS" );
    // CBM slots are an EXTERNAL_OPTION defaulting to FALSE (data/json/game_balance.json; the
    // bundled cbm_slots mod turns them on), and bionic_installation_issues returns early when they
    // are off. So the chassis shows capacity only when the mechanic is actually in force — what is
    // always true is which body parts an implant occupies, and that is what the diagram is for.
    const bool slots_enforced = get_option<bool>( "CBM_SLOTS_ENABLED" );

    // Track how many good / bad POINTS we have; cap both at MAX_TRAIT_POINTS
    int num_good = 0;
    int num_bad = 0;

    struct bionic_entry {
        bionic_id id;
        int col = 0;
        /// Which chassis region it lives in — the sub-heading it sits under.
        nc_bio_region region = nc_bio_region::none;
        /// The profession grants it. Profession CBMs are installed by add_profession_items AFTER
        /// the wizard, so `has_bionic` is false for them all the way through creation — the list
        /// has always rendered them as held, and the chassis counts them for the same reason.
        bool granted = false;
    };
    // ONE flat list; the columns are a VIEW over it, so sorting and every id-based lookup stays
    // independent of how the screen happens to be grouped. Same shape as the TRAITS step.
    std::vector<bionic_entry> starting_bionics;

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

    const std::vector<bionic_id> prof_cbms = u.prof->CBMs();
    for( auto &bio_iter : bionic_data::get_all() ) {
        const bool is_profbionic = std::ranges::find( prof_cbms, bio_iter.id ) != prof_cbms.end();
        // We show all starting bionics, even ones we cannot pick, to keep the interface consistent —
        // and profession-locked ones regardless of whether they are otherwise forbidden.
        if( bio_iter.starting_bionic || g->scen->bionicquery( bio_iter.id ) ||
            u.prof->is_allowed_bionic( bio_iter.id ) || is_profbionic ) {
            // Budget totals key off the POINT SIGN, never off which column the bionic landed in.
            if( u.has_bionic( bio_iter.id ) ) {
                if( bio_iter.points > 0 ) {
                    num_good += bio_iter.points;
                } else if( bio_iter.points < 0 ) {
                    num_bad += bio_iter.points;
                }
            }
            const int col = bio_iter.points > 0 ? 0 : ( bio_iter.points < 0 ? 1 : 2 );
            starting_bionics.push_back( { .id = bio_iter.id, .col = col,
                                          .region = nc_bio_classify( bio_iter ),
                                          .granted = is_profbionic } );
        }
    }

    std::ranges::sort( starting_bionics, []( const bionic_entry & a, const bionic_entry & b ) {
        return localized_compare( a.id->name.translated(), b.id->name.translated() );
    } );

    // Each column's rows, as indices into starting_bionics, grouped by chassis region: -1 marks a
    // sub-heading, which OCCUPIES A ROW so every row stays the same height — the invariant the
    // cursor-scroll arithmetic depends on (see .nc-row in newchar_common.rcss).
    //
    // Regions run head to feet, so a column reads top-to-bottom in the same order the diagram
    // beside it does. This is also what the reference art does with its bracketed group headers,
    // and it is why 19 rows in a tall column no longer read as one undifferentiated slab.
    std::array<std::vector<int>, 3> col_rows;
    /// Which region each row belongs to, parallel to col_rows, so a heading row can name itself
    /// without re-deriving the grouping.
    std::array<std::vector<nc_bio_region>, 3> row_region;
    for( int c = 0; c < 3; c++ ) {
        for( int reg = 0; reg < static_cast<int>( nc_bio_region::count ); reg++ ) {
            const nc_bio_region region = static_cast<nc_bio_region>( reg );
            std::vector<int> members;
            for( int i = 0; i < static_cast<int>( starting_bionics.size() ); i++ ) {
                if( starting_bionics[i].col == c && starting_bionics[i].region == region ) {
                    members.push_back( i );
                }
            }
            if( members.empty() ) {
                continue;   // a heading with nothing under it is noise
            }
            col_rows[c].push_back( -1 );
            row_region[c].push_back( region );
            for( const int i : members ) {
                col_rows[c].push_back( i );
                row_region[c].push_back( region );
            }
        }
    }

    int cur_col = 0;
    std::array<int, 3> cur_row = { 0, 0, 0 };

    const auto col_len = [&]( int c ) {
        return static_cast<int>( col_rows[c].size() );
    };
    /// The bionic on a given row, or -1 for a heading or an out-of-range row.
    const auto bionic_at = [&]( int c, int r ) {
        return ( r >= 0 && r < col_len( c ) ) ? col_rows[c][r] : -1;
    };
    /// Every column opens on a heading, so step off it before anything reads the cursor.
    const auto skip_headings = [&]( int c, int dir ) {
        int guard = col_len( c );
        while( guard-- > 0 && bionic_at( c, cur_row[c] ) < 0 ) {
            cur_row[c] += dir;
            if( cur_row[c] < 0 ) {
                cur_row[c] = col_len( c ) - 1;
            } else if( cur_row[c] >= col_len( c ) ) {
                cur_row[c] = 0;
            }
        }
    };
    for( int c = 0; c < 3; c++ ) {
        skip_headings( c, 1 );
    }

    ui_adaptor ui;
    catacurses::window w;

    // No character preview on this step, unlike TRAITS / PROFESSION / SCENARIO / DESCRIPTION: the
    // chassis diagram is this screen's body view, and an implant does not change how the avatar
    // looks — so the detail panel's art slot carries the CBM's own sprite instead. The preview's
    // zoom and clothes actions are not registered below for the same reason: there would be
    // nothing on screen for them to act on.
    const auto init_windows = [&]( ui_adaptor & ui ) {
        w = catacurses::newwin( TERMY, TERMX, point_zero );
        ui.position_from_window( w );
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
    // Required for the navigator/row/checkbox clicks to reach this loop at all.
    // MOUSE_LEFT binds to action id SELECT (keybindings.json:1175). An UNREGISTERED
    // mouse action resolves to CATA_ERROR, and input.cpp:894-897 `continue`s on it
    // BEFORE the registered_any_input check at :912 — so ANY_INPUT cannot rescue a
    // mouse event, only registering the action it maps to can. Without this the
    // click callback fires but handle_input() never returns, leaving the loop parked
    // until an unrelated keypress, which then got hijacked into a step change.
    ctxt.register_action( "SELECT" );
    // ...and BOTH of these, for the same reason TRAITS needs them — dropping them here cost a
    // debugging cycle and looked exactly like a dead checkbox. MOUSE_LEFT arrives as SELECT on
    // mouse DOWN, but RmlUi fires its `click`, and therefore the `data-event-click` callbacks, on
    // mouse UP. That UP event resolves to CATA_ERROR, and input.cpp:894-897 `continue`s on an
    // unrecognised MOUSE event WITHOUT returning — so handle_input() stays parked right through the
    // callbacks, and the intent they recorded is cleared at the top of the next iteration, which
    // only runs when some later input wakes the loop. Observed: a checkbox click did nothing, and a
    // row click appeared to work only because the FOLLOWING click's mouse-down flushed it.
    //
    // COORDINATE sets handling_coordinate_input, which skips that early `continue`; ANY_INPUT makes
    // the fall-through at :912 return. Either alone still parks the loop. ANY_INPUT is then
    // excluded from the model-dirty test below, so a pointer sample does not rebuild the rows.
    ctxt.register_action( "COORDINATE" );
    ctxt.register_action( "ANY_INPUT" );

    // RmlUi render path (render-only; keyboard owns nav/confirm/reroll below).
    auto data = std::make_unique<nc_bionics_session>();
    rml_doc rml;
    // Set by the arrow click callbacks, consumed by the input loop below. Not a
    // tab_direction: it is translated into an action string so the existing
    // keyboard handling stays the single place navigation is decided.
    int nc_nav = 0;
    // Click intent, applied ONCE per input cycle. `data-event-*` installs a listener per generated
    // element and a `data-for` regeneration adds another without removing the old, so a callback
    // that mutated directly would run an unbounded number of times per click — measured at 15 on
    // the SCENARIO tab. See plans/charcreation-scenario-tree.md.
    int pending_row_col = -1;
    int pending_row = -1;
    int pending_check_col = -1;
    int pending_check_row = -1;

    // ── THE ONE GATE ──────────────────────────────────────────────────────────
    //
    // Why a bionic can or cannot be toggled right now. The row's checkbox glyph, the STATUS fact's
    // reason line and CONFIRM's refusal popups all read this, so what a row shows cannot promise
    // something the keypress then refuses. Before this rework the reasons existed ONLY as popups
    // raised after CONFIRM, and every unavailable row rendered as the same dark gray.
    //
    // These lambdas only ASK the game the questions; the precedence between the answers lives in
    // newchar_bionic_gate.h, where it is tested without needing an avatar.
    const auto conflicting_traits_of = [&]( const bionic_id & bid ) {
        std::vector<trait_id> out;
        for( const trait_id &tid : bid->canceled_mutations ) {
            if( u.has_trait( tid ) ) {
                out.push_back( tid );
            }
        }
        return out;
    };
    const auto missing_prereqs_of = [&]( const bionic_id & bid ) {
        std::vector<bionic_id> out;
        for( const bionic_id &req : bid->required_bionics ) {
            if( !u.has_bionic( req ) ) {
                out.push_back( req );
            }
        }
        return out;
    };
    /// Installed bionics that name this one in their `required_bionics`, so removing it would leave
    /// them dangling.
    const auto dependents_of = [&]( const bionic_id & bid ) {
        std::vector<std::string> out;
        for( const bionic &i : u.get_bionic_collection() ) {
            for( const bionic_id &req : i.id->required_bionics ) {
                if( req == bid ) {
                    out.emplace_back( i.id->name.translated() );
                }
            }
        }
        return out;
    };
    /// The version below this one that the avatar already holds, walking the `upgraded_bionic`
    /// chain. BOUNDED: the loop this replaces reassigned its cursor from the original bionic every
    /// iteration, so a chain two links deep with nothing held never advanced — an infinite loop
    /// inside the input handler. The cap also turns a cyclic chain in modded JSON into a wrong
    /// answer rather than a hang.
    const auto held_downgrade_of = [&]( const bionic_id & bid ) -> bionic_id {
        bionic_id step = bid->upgraded_bionic;
        for( int guard = 0; guard < 64 && step != bionic_id::NULL_ID(); guard++ )
        {
            if( u.has_bionic( step ) ) {
                return step;
            }
            step = step->upgraded_bionic;
        }
        return bionic_id::NULL_ID();
    };
    const auto held_upgrade_of = [&]( const bionic_id & bid ) -> bionic_id {
for( const bionic_id &up : bid->available_upgrades )
    {
        if( u.has_bionic( up ) ) {
                return up;
            }
        }
        return bionic_id::NULL_ID();
    };
    const auto gate_of = [&]( int flat_idx ) {
        const bionic_entry &e = starting_bionics[flat_idx];
        const bionic_id &bid = e.id;
        nc_bionic_gate::inputs in;
        in.taken = u.has_bionic( bid );
        in.granted = e.granted;
        // Only meaningful for a bionic that is installed, and the walk is not free.
        in.has_dependents = in.taken && !dependents_of( bid ).empty();
        in.scen_forbids_all = g->scen->forbids_bionics();
        in.prof_forbids_all = u.prof->forbids_bionics();
        in.scen_forbids = g->scen->is_forbidden_bionic( bid );
        in.prof_forbids = u.prof->is_forbidden_bionic( bid );
        in.scen_locked = g->scen->is_locked_bionic( bid );
        in.prof_locked = u.prof->is_locked_bionic( bid );
        in.trait_conflicts = !conflicting_traits_of( bid ).empty();
        in.no_space = !u.bionic_installation_issues( bid ).empty();
        in.missing_prereq = !missing_prereqs_of( bid ).empty();
        in.has_downgrade = held_downgrade_of( bid ) != bionic_id::NULL_ID();
        in.has_upgrade = held_upgrade_of( bid ) != bionic_id::NULL_ID();
        in.points = bid->points;
        in.num_good = num_good;
        in.num_bad = num_bad;
        in.max_points = max_trait_points;
        in.freeform = points.is_freeform();
        return nc_bionic_gate::evaluate( in );
    };

    // The ONE place a bionic is installed or removed. Keyboard CONFIRM and the row's checkbox both
    // come here, so the two cannot drift. Every popup below fires for exactly the case it fired for
    // before this rework, in the same order and with the same words — only the plumbing changed,
    // from re-asking the game mid-chain to reading the gate.
    //
    // ONE behaviour change, and it is deliberate: a scenario- or profession-LOCKED bionic that is
    // installed can no longer be removed. Those two popups existed but were unreachable, because
    // the taken branch returned before them; TRAITS already refuses the same case with the same
    // wording, and a scenario's forced bionic being droppable defeats the point of forcing it.
    const auto toggle_bionic_at = [&]( int flat_idx ) {
        if( flat_idx < 0 || flat_idx >= static_cast<int>( starting_bionics.size() ) ) {
            return;
        }
        const bionic_id cur_bionic = starting_bionics[flat_idx].id;
        const bionic_data &bio = cur_bionic.obj();
        const nc_bionic_gate::state gt = gate_of( flat_idx );
        int inc_type = 0;

        if( gt.taken ) {
            if( gt.has_dependents ) {
                popup( _( "These bionics are dependent on the bionic you are trying to uninstall %s." ),
                       enumerate_as_string( dependents_of( cur_bionic ) ) );
            } else if( g->scen->is_locked_bionic( cur_bionic ) ) {
                popup( _( "Your scenario of %s prevents you from removing this bionic." ),
                       g->scen->gender_appropriate_name( u.male ) );
            } else if( u.prof->is_locked_bionic( cur_bionic ) ) {
                popup( _( "Your profession of %s prevents you from removing this bionic." ),
                       u.prof->gender_appropriate_name( u.male ) );
            } else {
                inc_type = -1;
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
            popup( _( "Your scenario of %s prevents you from removing this bionic." ),
                   g->scen->gender_appropriate_name( u.male ) );
        } else if( u.prof->is_locked_bionic( cur_bionic ) ) {
            popup( _( "Your profession of %s prevents you from removing this bionic." ),
                   u.prof->gender_appropriate_name( u.male ) );
        } else if( gt.trait_conflicts ) {
            // Name the traits, so the player can see what is in the way rather than just that
            // something is.
            const std::vector<trait_id> conflicts = conflicting_traits_of( cur_bionic );
            std::vector<std::string> conflict_names;
            conflict_names.reserve( conflicts.size() );
            for( const trait_id &conflict : conflicts ) {
                conflict_names.emplace_back( conflict.obj().name() );
            }
            popup( _( "The following traits prevent you from taking this bionic: %s." ),
                   enumerate_as_string( conflict_names ) );
        } else if( gt.over_budget && bio.points > 0 ) {
            popup( vgettext( "Sorry, but you can only take %d point of advantages.",
                             "Sorry, but you can only take %d points of advantages.", max_trait_points ),
                   max_trait_points );
        } else if( gt.over_budget ) {
            popup( vgettext( "Sorry, but you can only take %d point of disadvantages.",
                             "Sorry, but you can only take %d points of disadvantages.", max_trait_points ),
                   max_trait_points );
        } else if( gt.no_space ) {
            const std::map<bodypart_id, int> issues = u.bionic_installation_issues( cur_bionic );
            std::string detailed_info;
            for( const std::pair<const bodypart_id, int> &elem : issues ) {
                //~ <Body part name>: <number of slots> more slot(s) needed.
                detailed_info += string_format( _( "\n%s: %i more slot(s) needed." ),
                                                body_part_name_as_heading( elem.first->token, 1 ),
                                                elem.second );
            }
            popup( _( "Not enough space for bionic installation!%s" ), detailed_info );
        } else if( gt.missing_prereq ) {
            const std::vector<bionic_id> missing = missing_prereqs_of( cur_bionic );
            std::vector<std::string> conflict_names;
            conflict_names.reserve( missing.size() );
            for( const bionic_id &conflict : missing ) {
                conflict_names.emplace_back( conflict->name.translated() );
            }
            popup( _( "The lack of the following bionics are prevent you from taking this bionic: %s." ),
                   enumerate_as_string( conflict_names ) );
        } else if( gt.has_downgrade ) {
            popup( _( "You already have the downgraded version of the bionic: %s" ),
                   held_downgrade_of( cur_bionic )->name );
        } else if( gt.has_upgrade ) {
            popup( _( "You already have the upgraded version of the bionic: %s" ),
                   held_upgrade_of( cur_bionic )->name );
        } else {
            // Unreachable for a profession fixture: `profession::is_locked_bionic` IS "in
            // _starting_CBMs", so a granted bionic always stops at that arm above.
            inc_type = 1;
        }

        //inc_type is either -1 or 1, so we can just multiply by it to invert
        if( inc_type != 0 ) {
            u.toggle_bionic( cur_bionic );
            points.trait_points -= bio.points * inc_type;
            // By POINT SIGN, not by column index: a modded zero-point bionic used to take the
            // num_bad path purely because it was not an advantage.
            if( bio.points > 0 ) {
                num_good += bio.points * inc_type;
            } else if( bio.points < 0 ) {
                num_bad += bio.points * inc_type;
            }
        }
    };

    // ── CHASSIS ───────────────────────────────────────────────────────────────
    //
    // The body diagram, after UI_designs/02_cybernetics_terminal.png: a slot box per body part
    // wired to a bus rail, showing which implants live where. The grid is the profession equipment
    // doll's — six rows of three, every position emitted, a junction drawing spine + rung and a
    // blank holding its column open — so it aligns from equal flex with no pixel arithmetic.
    //
    // Rebuilt every redraw, because it carries the scan sweep. See nc_bionics_session::doll for why
    // that is cheap.
    const auto anim_start = std::chrono::steady_clock::now();
    const auto sync_chassis = [&]() {
        if( !data->handle ) {
            return;
        }
        // Everything the finished character will start with. Profession CBMs are counted even
        // though they are not installed yet (add_profession_items does that after the wizard),
        // because the diagram describes the character being built rather than the half-built
        // object in memory — and it is what the list's own [x] has always meant.
        std::vector<bionic_id> held = u.get_bionics();
        for( const bionic_id &bid : prof_cbms ) {
            if( std::ranges::find( held, bid ) == held.end() ) {
                held.push_back( bid );
            }
        }
        // The bionic under the cursor, whose sites the diagram highlights: selecting a row is how
        // you find out where an implant goes.
        const int sel_flat = bionic_at( cur_col, cur_row[cur_col] );
        const bionic_id want_bio = sel_flat >= 0 ? starting_bionics[sel_flat].id
                                   : bionic_id::NULL_ID();
        const std::map<bodypart_id, int> want_issues = want_bio == bionic_id::NULL_ID()
            ? std::map<bodypart_id, int> {}
            : u.bionic_installation_issues( want_bio );

        const auto site_cell = [&]( body_part bp ) -> nc_bio_cell {
            const bodypart_str_id &bpid = convert_bp( bp );
            nc_bio_cell cell;
            cell.name_rml = cata_text_to_rml( colorize( body_part_name( bpid.id() ), c_dark_gray ) );
            int used = 0;
            int count = 0;
            const bionic_data *best = nullptr;
            int best_space = 0;
            for( const bionic_id &bid : held )
            {
                const auto it = bid->occupied_bodyparts.find( bpid );
                if( it == bid->occupied_bodyparts.end() || it->second <= 0 ) {
                    continue;
                }
                used += it->second;
                count++;
                // The biggest implant represents the part, ties broken by name so the box does not
                // change what it shows between two runs of the same character.
                if( best == nullptr || it->second > best_space ||
                    ( it->second == best_space &&
                      localized_compare( bid->name.translated(), best->name.translated() ) ) ) {
                    best = &bid.obj();
                    best_space = it->second;
                }
            }
            const int total = bpid->bionic_slots();
            if( best == nullptr )
            {
                cell.impl_rml = cata_text_to_rml( colorize( "—", c_dark_gray ) );
            } else
            {
                cell.filled = true;
                cell.impl_rml = cata_text_to_rml( colorize( best->name.translated(), c_light_gray ) );
                cell.dec = nc_tile_sprite_dec( best->id.str(), C_ITEM );
                if( count > 1 ) {
                    cell.more_rml = cata_text_to_rml( colorize( string_format( "+%d", count - 1 ),
                                                      c_yellow ) );
                }
            }
            // Capacity, only where the mechanic is switched on. Presenting used/total while
            // CBM_SLOTS_ENABLED is off would state a rule that is not in force.
            if( slots_enforced && total > 0 )
            {
                cell.show_cap = true;
                cell.cap_rml = cata_text_to_rml( colorize( string_format( "%d/%d", used, total ),
                                                 used > 0 ? c_light_gray : c_dark_gray ) );
                cell.bar_w = string_format( "%d%%", std::clamp( used * 100 / total, 0, 100 ) );
            }
            // What the selection would claim here, and whether it fits.
            if( want_bio != bionic_id::NULL_ID() )
            {
                const auto it = want_bio->occupied_bodyparts.find( bpid );
                if( it != want_bio->occupied_bodyparts.end() && it->second > 0 ) {
                    cell.want = true;
                    cell.over = want_issues.contains( bpid.id() );
                }
            }
            return cell;
        };
        const auto junction = []() -> nc_bio_cell {
            return { .junction = true };
        };
        const auto blank = []() -> nc_bio_cell {
            return { .blank = true };
        };

        // The sweep. Wall clock, not a frame counter: it must not accelerate because a key is held
        // down, nor stall while the player is reading. Geometry — and the reason the glow trails
        // rather than surrounds the head — is in newchar_bio_scan.h.
        const float secs = std::chrono::duration<float>(
                               std::chrono::steady_clock::now() - anim_start ).count();
        const float head = nc_bio_scan::head_at( secs );
        const auto scan_col = [&]( int row ) {
            // The step rail's active gold (see nc_icon_dec), carried as ALPHA so the fade is the
            // compositor's job and no colour arithmetic can drift from the theme.
            return string_format( "#c4a832%02x",
                                  nc_bio_scan::alpha_of( nc_bio_scan::intensity( head, row ) ) );
        };

        data->doll.clear();
        data->doll.push_back( { .cells = { blank(), site_cell( bp_head ), blank() },
                                .scan_col = scan_col( 0 ) } );
        data->doll.push_back( { .cells = { site_cell( bp_eyes ), junction(), site_cell( bp_mouth ) },
                                .scan_col = scan_col( 1 ) } );
        data->doll.push_back( { .cells = { site_cell( bp_arm_l ), site_cell( bp_torso ), site_cell( bp_arm_r ) },
                                .scan_col = scan_col( 2 ) } );
        data->doll.push_back( { .cells = { site_cell( bp_hand_l ), junction(), site_cell( bp_hand_r ) },
                                .scan_col = scan_col( 3 ) } );
        data->doll.push_back( { .cells = { site_cell( bp_leg_l ), junction(), site_cell( bp_leg_r ) },
                                .scan_col = scan_col( 4 ) } );
        data->doll.push_back( { .cells = { site_cell( bp_foot_l ), junction(), site_cell( bp_foot_r ) },
                                .scan_col = scan_col( 5 ) } );

        data->chassis_rml = cata_text_to_rml( colorize(
                string_format( vgettext( "%d implant", "%d implants", held.size() ), held.size() ),
                c_dark_gray ) );
        if( slots_enforced ) {
            int used_total = 0;
            int slot_total = 0;
            for( const body_part bp : all_body_parts ) {
                const bodypart_str_id &bpid = convert_bp( bp );
                slot_total += bpid->bionic_slots();
                for( const bionic_id &bid : held ) {
                    const auto it = bid->occupied_bodyparts.find( bpid );
                    if( it != bid->occupied_bodyparts.end() && it->second > 0 ) {
                        used_total += it->second;
                    }
                }
            }
            data->chassis_cap_rml = cata_text_to_rml( colorize(
                                        string_format( _( "%d/%d slots" ), used_total, slot_total ), c_dark_gray ) );
        } else {
            data->chassis_cap_rml.clear();
        }
        data->handle.DirtyVariable( "doll" );
        data->handle.DirtyVariable( "chassis_rml" );
        data->handle.DirtyVariable( "chassis_cap_rml" );
    };

    const auto sync_rml = [&]() {
        if( !data->handle ) {
            return;
        }
        data->tabs = build_nc_char_tabs<nc_bionics_tab>( 5 );  // BIONICS tab active
        data->shell = fill_nc_shell( 5, ctxt );
        data->points_rml = cata_text_to_rml( nc_points_line( points ) );
        data->balance = nc_make_balance( num_good, num_bad, max_trait_points,
                                         points.is_freeform() );
        data->chassis_name_rml = cata_text_to_rml( colorize( _( "Chassis" ), c_light_gray ) );
        data->hint_rml = cata_text_to_rml( string_format(
                                               _( "<color_light_green>%s</color> install or remove · <color_light_green>%s</color> column · <color_light_green>%s</color> reroll" ),
                                               ctxt.get_desc( "CONFIRM", 1 ), _( "left/right" ),
                                               ctxt.get_desc( "REROLL_CHARACTER", 1 ) ) );
        // The reference's meta-bar readout: a dim label with the number bright beside it. Bionics
        // and traits spend the SAME pool, which is why this says the same thing the TRAITS step
        // says rather than inventing a second budget.
        data->budget_rml = cata_text_to_rml( string_format(
                _( "<color_dark_gray>Trait points left:</color> <color_white>%d</color>" ),
                points.trait_points_left() ) );

        // In words, why this bionic cannot be toggled — the same conditions CONFIRM's popups use,
        // stated where the decision is made instead of after it. Empty when it can be.
        const auto refusal_of = []( const nc_bionic_gate::state & gt ) -> std::string {
            if( gt.taken )
        {
            if( gt.has_dependents ) {
                    return _( "Another implant you have depends on this one." );
                }
                return gt.locked ? _( "Your profession or scenario will not let you remove this." )
                       : std::string();
            }
            if( gt.granted )
        {
            return _( "Your profession installs this one for you." );
            }
            if( gt.locked )
        {
            return _( "Your profession or scenario decides this one." );
            }
            if( gt.forbidden )
        {
            return _( "Your profession or scenario forbids this implant." );
            }
            if( gt.trait_conflicts )
        {
            return _( "Conflicts with a trait you already have." );
            }
            if( gt.no_space )
        {
            return _( "Not enough room left in your body for it." );
            }
            if( gt.missing_prereq )
        {
            return _( "Needs another implant you do not have." );
            }
            if( gt.has_downgrade )
        {
            return _( "You already have a lesser version of it." );
            }
            if( gt.has_upgrade )
        {
            return _( "You already have a better version of it." );
            }
            return gt.over_budget ? _( "No points left on that side of the budget." ) : std::string();
        };

        // One row. The cells are fixed-width by stylesheet, so cursor, box, cost and name each form
        // a column the eye can run down; that alignment is the whole readability argument.
        const auto build_row = [&]( int flat_idx, bool is_cursor ) {
            const bionic_data &bio = starting_bionics[flat_idx].id.obj();
            const nc_bionic_gate::state gt = gate_of( flat_idx );
            nc_bio_row r;
            r.cursor_rml = cata_text_to_rml( is_cursor ? colorize( ">", c_yellow ) : std::string() );
            // [x] held · [ ] installable · [-] refused, and the panel says why.
            r.check_rml = gt.held()
                          ? cata_text_to_rml( colorize( "[x]", c_yellow ) )
                          : ( gt.toggleable() ? cata_text_to_rml( colorize( "[ ]", c_light_gray ) )
                              : cata_text_to_rml( colorize( "[-]", c_dark_gray ) ) );
            if( bio.points != 0 ) {
                // The same valence the balance scale's two pans use, so the column and the scale
                // agree at a glance.
                r.cost_rml = cata_text_to_rml( colorize( string_format( "[%+d]", bio.points ),
                                               bio.points > 0 ? COL_TR_GOOD : COL_TR_BAD ) );
            }
            const nc_color name_col = gt.held() ? c_white
                                      : ( gt.toggleable() ? c_light_gray : c_dark_gray );
            r.name_rml = cata_text_to_rml( colorize( bio.name.translated(), name_col ) );
            r.selected = is_cursor;
            return r;
        };

        nc_bio_col *cols[3] = { &data->col0, &data->col1, &data->col2 };
        for( int c = 0; c < 3; c++ ) {
            nc_bio_col &dc = *cols[c];
            dc.rows.clear();
            int items = 0;
            for( int r = 0; r < col_len( c ); r++ ) {
                const int flat = col_rows[c][r];
                if( flat < 0 ) {
                    // Region sub-heading. Same row height as an implant, which is what keeps the
                    // scroll arithmetic exact; tracking, caps and colour carry the hierarchy.
                    nc_bio_row hr;
                    hr.header = true;
                    hr.name_rml = cata_text_to_rml( colorize( nc_bio_region_name( row_region[c][r] ),
                                                    c_yellow ) );
                    dc.rows.push_back( hr );
                    continue;
                }
                items++;
                dc.rows.push_back( build_row( flat, c == cur_col && r == cur_row[c] ) );
            }
            dc.name_rml = cata_text_to_rml( colorize(
                                                c == 0 ? _( "Advantages" )
                                                : ( c == 1 ? _( "Disadvantages" ) : _( "Neutral" ) ),
                                                c == cur_col ? c_white : c_light_gray ) );
            dc.count_rml = cata_text_to_rml( colorize( string_format( "%d", items ), c_dark_gray ) );
        }
        // Vanilla has no zero-point bionic, so the third column exists only for mods.
        data->show_col2 = col_len( 2 ) > 0;

        // Detail panel for the bionic under the cursor.
        data->facts.clear();
        const int sel_flat = bionic_at( cur_col, cur_row[cur_col] );
        if( sel_flat >= 0 ) {
            const bionic_id sel_id = starting_bionics[sel_flat].id;
            const bionic_data &bio = sel_id.obj();
            const nc_bionic_gate::state gt = gate_of( sel_flat );
            // ":: NAME" — the reference's detail header. Uppercasing is left to the stylesheet so
            // no locale gets a hand-rolled case conversion.
            data->sel_name_rml = cata_text_to_rml( colorize(
                    string_format( ":: %s", bio.name.translated() ), c_white ) );
            // Every starting bionic has a CBM item whose id equals the bionic id (measured: 0
            // misses across the 34), and nc_tile_sprite_dec follows `looks_like`, so a modded CBM
            // with no art of its own still resolves rather than leaving a hole.
            data->art_dec = nc_tile_sprite_dec( sel_id.str(), C_ITEM );
            data->has_art = true;
            const auto add_fact = [&]( const std::string & label, const std::string & value,
            const nc_color & col, const std::string & sub = std::string() ) {
                data->facts.push_back( {
                    .label_rml = cata_text_to_rml( label ),
                    .value_rml = cata_text_to_rml( colorize( value, col ) ),
                    .sub_rml = cata_text_to_rml( sub ) } );
            };
            const int pts = std::abs( bio.points );
            if( bio.points == 0 ) {
                add_fact( _( "Cost" ), _( "Free" ), c_light_gray );
            } else {
                add_fact( _( "Cost" ),
                          string_format( vgettext( "%d point", "%d points", pts ), pts ),
                          bio.points > 0 ? COL_TR_GOOD : COL_TR_BAD );
            }
            // WHERE it goes — the fact the chassis draws, in words for the same bionic. Terse
            // "part n" pairs rather than a sentence, because this column is scanned, not read.
            std::vector<std::string> site_parts;
            for( const std::pair<const bodypart_str_id, int> &occ : bio.occupied_bodyparts ) {
                if( occ.second > 0 ) {
                    site_parts.push_back( string_format( "%s %d", body_part_name( occ.first.id() ),
                                                         occ.second ) );
                }
            }
            std::string site_sub;
            if( slots_enforced ) {
                const std::map<bodypart_id, int> issues = u.bionic_installation_issues( sel_id );
                for( const std::pair<const bodypart_id, int> &elem : issues ) {
                    site_sub += string_format( _( "%s: %d more slot(s) needed.  " ),
                                               body_part_name( elem.first ), elem.second );
                }
            }
            add_fact( _( "Site" ),
                      site_parts.empty() ? _( "No body slots" ) : enumerate_as_string( site_parts ),
                      site_parts.empty() ? c_dark_gray : c_light_gray, site_sub );
            // Whether it costs the player anything to run, which is the other half of choosing an
            // implant. `units::display` is the same formatter the in-game bionics menu uses.
            std::vector<std::string> power_bits;
            if( bio.power_activate > 0_kJ ) {
                power_bits.push_back( string_format( _( "%s to switch on" ),
                                                     units::display( bio.power_activate ) ) );
            }
            if( bio.power_over_time > 0_kJ && bio.charge_time > 0 ) {
                power_bits.push_back( string_format( _( "%s while running" ),
                                                     units::display( bio.power_over_time ) ) );
            }
            if( bio.capacity > 0_kJ ) {
                power_bits.push_back( string_format( _( "%s of storage" ),
                                                     units::display( bio.capacity ) ) );
            }
            add_fact( _( "Power" ), bio.activated ? _( "Activated" ) : _( "Passive" ),
                      c_light_gray, enumerate_as_string( power_bits ) );
            const std::string refusal = refusal_of( gt );
            add_fact( _( "Status" ),
                      gt.held() ? _( "Installed" )
                      : ( gt.toggleable() ? _( "Available" ) : _( "Unavailable" ) ),
                      gt.held() ? COL_TR_GOOD : ( gt.toggleable() ? c_white : c_light_red ),
                      refusal );
            data->desc_rml = cata_text_to_rml( bio.description.translated() );
        } else {
            data->sel_name_rml.clear();
            data->desc_rml.clear();
            data->has_art = false;
        }

        data->handle.DirtyVariable( "tabs" );
        dirty_nc_shell( data->handle );
        data->handle.DirtyVariable( "points_rml" );
        data->handle.DirtyVariable( "budget_rml" );
        data->handle.DirtyVariable( "balance" );
        data->handle.DirtyVariable( "col0" );
        data->handle.DirtyVariable( "col1" );
        data->handle.DirtyVariable( "col2" );
        data->handle.DirtyVariable( "show_col2" );
        data->handle.DirtyVariable( "chassis_name_rml" );
        data->handle.DirtyVariable( "sel_name_rml" );
        data->handle.DirtyVariable( "has_art" );
        data->handle.DirtyVariable( "art_dec" );
        data->handle.DirtyVariable( "facts" );
        data->handle.DirtyVariable( "desc_rml" );
        data->handle.DirtyVariable( "hint_rml" );
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
        c.Bind( "budget_rml", &data->budget_rml );
        c.Bind( "balance", &data->balance );
        c.Bind( "col0", &data->col0 );
        c.Bind( "col1", &data->col1 );
        c.Bind( "col2", &data->col2 );
        c.Bind( "show_col2", &data->show_col2 );
        // The chassis. Easy to forget, and it fails SILENTLY: a `data-for` over an unbound name
        // renders nothing at all, with no warning — on TRAITS that left the GENOME heading sitting
        // over blank space while its producer happily filled and dirtied the array every frame.
        c.Bind( "doll", &data->doll );
        c.Bind( "chassis_name_rml", &data->chassis_name_rml );
        c.Bind( "chassis_rml", &data->chassis_rml );
        c.Bind( "chassis_cap_rml", &data->chassis_cap_rml );
        c.Bind( "sel_name_rml", &data->sel_name_rml );
        c.Bind( "has_art", &data->has_art );
        c.Bind( "art_dec", &data->art_dec );
        c.Bind( "facts", &data->facts );
        c.Bind( "desc_rml", &data->desc_rml );
        c.Bind( "hint_rml", &data->hint_rml );
        // Click callbacks RECORD INTENT and mutate nothing — see the comment on pending_row_col.
        c.BindEventCallback( "on_row",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int col = -1;
            int row = -1;
            if( args.size() >= 2 ) {
                args[0].GetInto( col );
                args[1].GetInto( row );
            }
            if( col >= 0 && col < 3 && row >= 0 ) {
                pending_row_col = col;
                pending_row = row;
            }
        } );
        // The checkbox, not the row, is what toggles: installing spends points and can raise a
        // modal, so reading an implant must not be the same gesture as taking it.
        c.BindEventCallback( "on_check",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int col = -1;
            int row = -1;
            if( args.size() >= 2 ) {
                args[0].GetInto( col );
                args[1].GetInto( row );
            }
            if( col >= 0 && col < 3 && row >= 0 ) {
                pending_check_col = col;
                pending_check_row = row;
            }
        } );
        data->handle = c.GetModelHandle();
    } );
    if( rml_doc_unavailable( rml, _( "Character creation (BIONICS tab)" ) ) ) {
        return tab_direction::QUIT;
    }

    // The model is rebuilt only when something changed; the chassis every frame, because it carries
    // the sweep. Without the split a quiet animation tick would re-gate every row — each of which
    // asks the game about slots, conflicts and upgrade chains — thirty times a second to move some
    // light down a rail.
    bool model_dirty = true;
    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            if( model_dirty ) {
                sync_rml();
                model_dirty = false;
            }
            sync_chassis();
            return;
        }
    } );

    // Keeps the cursor row on screen. Every row is the SAME height by stylesheet rule, so row
    // height is scroll_height / row_count exactly, with no DOM child indexing — which `data-for`
    // makes unreliable anyway. Same mechanism as the TRAITS columns and the equipment tree.
    const auto scroll_col_to_cursor = [&]() {
        if( !rml ) {
            return;
        }
        const int rows = col_len( cur_col );
        if( rows <= 0 ) {
            return;
        }
        Rml::Element *e = rml.document()->GetElementById(
                              string_format( "nc-col%d", cur_col ) );
        if( e == nullptr ) {
            return;
        }
        const float page = e->GetClientHeight();
        const float total = e->GetScrollHeight();
        const float row_h = total / static_cast<float>( rows );
        const float want = row_h * static_cast<float>( cur_row[cur_col] ) - page * 0.5f;
        e->SetScrollTop( std::clamp( want, 0.0f, std::max( 0.0f, total - page ) ) );
    };

    // Open on a column that has something in it, cursor on a real implant rather than a heading.
    for( int c = 0; c < 3; c++ ) {
        if( col_len( c ) > 0 ) {
            cur_col = c;
            break;
        }
    }
    skip_headings( cur_col, 1 );

    do {
        ui_manager::redraw();
        nc_nav = 0;
        pending_row_col = -1;
        pending_row = -1;
        pending_check_col = -1;
        pending_check_row = -1;
        std::string action = ctxt.handle_input();
        if( nc_nav != 0 ) {
            action = nc_nav < 0 ? "PREV_TAB" : "NEXT_TAB";
        }
        // Rebuild the model on the next redraw after anything that can have changed it.
        //
        // The click intents MUST be part of this test, not just `action`: cata maps MOUSE_LEFT to
        // SELECT on mouse DOWN while RmlUi fires its `click` — and therefore those callbacks — on
        // mouse UP, so the iteration carrying a click's intent is usually an idle one. Gating on
        // the action alone leaves every mouse toggle applied but unrendered, which looks exactly
        // like a dead control.
        //
        // TIMEOUT and ANY_INPUT are both idle as far as the MODEL goes — ANY_INPUT is what every
        // mouse motion returns now that it is registered, and re-gating every row per pointer
        // sample is exactly the cost this split exists to avoid. The chassis and its sweep update
        // regardless, because they are outside the gate.
        if( ( action != "TIMEOUT" && action != "ANY_INPUT" ) ||
            pending_row >= 0 || pending_check_row >= 0 ) {
            model_dirty = true;
        }

        // Apply click intent exactly once, however many times the callback ran. A click on a
        // heading row is ignored rather than parking the cursor on a label.
        if( pending_row_col >= 0 && pending_row >= 0 && pending_row < col_len( pending_row_col ) &&
            bionic_at( pending_row_col, pending_row ) >= 0 ) {
            cur_col = pending_row_col;
            cur_row[cur_col] = pending_row;
        }
        // The checkbox last, so the cursor has already moved to the row being acted on.
        if( pending_check_col >= 0 && pending_check_row >= 0 &&
            pending_check_row < col_len( pending_check_col ) ) {
            const int flat = bionic_at( pending_check_col, pending_check_row );
            if( flat >= 0 ) {
                cur_col = pending_check_col;
                cur_row[cur_col] = pending_check_row;
                toggle_bionic_at( flat );
            }
        }

        // LEFT/RIGHT change column and UP/DOWN move within it — the axis mapping this tab has
        // always had, and the one the columns imply.
        if( action == "LEFT" || action == "RIGHT" ) {
            const int step = action == "RIGHT" ? 1 : 2;   // +1 / -1 modulo 3
            for( int n = 0; n < 3; n++ ) {
                cur_col = ( cur_col + step ) % 3;
                if( col_len( cur_col ) > 0 ) {
                    break;
                }
            }
            skip_headings( cur_col, 1 );
            scroll_col_to_cursor();
        } else if( action == "DOWN" || action == "UP" ) {
            const int dir = action == "DOWN" ? 1 : -1;
            const int len = col_len( cur_col );
            if( len > 0 ) {
                cur_row[cur_col] = ( cur_row[cur_col] + dir + len ) % len;
                // Headings are skipped in the direction of travel, so UP and DOWN both step over a
                // region header onto the next implant instead of parking the cursor on a label.
                skip_headings( cur_col, dir );
                scroll_col_to_cursor();
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
            // Return tab_direction::NONE so we re-enter this tab again, but it forces a complete redrawing of it.
            return tab_direction::NONE;
        } else if( action == "RANDOMIZE" ) {
            const int len = col_len( cur_col );
            if( len > 0 ) {
                cur_row[cur_col] = rng( 0, len - 1 );
                skip_headings( cur_col, 1 );
                scroll_col_to_cursor();
            }
        } else if( action == "CONFIRM" ) {
            const int flat = bionic_at( cur_col, cur_row[cur_col] );
            if( flat >= 0 ) {
                toggle_bionic_at( flat );
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
/// Which band a profession belongs to: the display category of its HIGHEST-level starting
/// skill, or `unskilled` when it grants none.
///
/// Derived, never hand-mapped. Every skill declares a `display_category` (skills.json, 28/28)
/// and `skill_display_type` gives that id a translated name, so a mod's new skill carries its
/// own grouping and a mod's new profession lands correctly without touching this file. The
/// alternative — cost bands, as the SCENARIO tab uses — would split 258 professions into three
/// groups of ~85, which is no more navigable than the flat list it replaces.
/// Highest-level skill decides the band. Ties break on the skill's ident so the grouping is
/// stable across runs rather than following whatever order the JSON happened to load in.
auto nc_classify_prof( const profession &p ) -> skill_displayType_id
{
    const profession::StartingSkillList sk = p.skills();
    if( sk.empty() ) {
        return skill_displayType_id::NULL_ID();
    }
    const auto top = std::ranges::max_element( sk,
    []( const std::pair<skill_id, int> &a, const std::pair<skill_id, int> &b ) {
        if( a.second != b.second ) {
            return a.second < b.second;
        }
        return a.first.str() > b.first.str();
    } );
    return top->first->display_category();
}

/// One profession sigil: a thing the profession HANDS YOU, which is what the player compares.
/// Colour carries valence — a liability red, a gift green/blue, a neutral fact grey — so the
/// strip reads before any individual glyph is learned.
///
/// SINGLE SOURCE OF TRUTH, as on the SCENARIO tab: card strip, selected-profession chips and
/// the legend all derive from this table. Splitting them across two sources is how that tab
/// ended up drawing a sigil nothing on screen explained.
struct nc_prof_sigil {
    unsigned seed;
    nc_color col;
    const char *label;                        //< terse, for the legend
    const char *desc;                         //< full, for the chip
    bool ( *present )( const profession & );  //< does this profession carry it
};

const std::vector<nc_prof_sigil> &nc_prof_sigils()
{
    static const std::vector<nc_prof_sigil> sigils = {
        {
            0x5452, c_light_gray, translate_marker( "Locked traits" ),
            translate_marker( "Comes with locked traits" ),
            []( const profession & p ) { return !p.get_locked_traits().empty(); }
        },
        {
            0x4249, c_light_blue, translate_marker( "Bionics" ),
            translate_marker( "Starts with bionics installed" ),
            []( const profession & p ) { return !p.CBMs().empty(); }
        },
        {
            0x5645, c_green, translate_marker( "Vehicle" ),
            translate_marker( "Starts with a vehicle" ),
            []( const profession & p ) { return !p.vehicle().str().empty() && p.vehicle().is_valid(); }
        },
        {
            0x4341, c_green, translate_marker( "Cash" ),
            translate_marker( "Starts with money" ),
            // Declared-but-zero cash is common, and "Starts with money" over $0.00 is a lie.
            []( const profession & p ) { return p.starting_cash().value_or( 0 ) != 0; }
        },
        {
            0x5045, c_green, translate_marker( "Animal" ),
            translate_marker( "Starts with an animal" ),
            []( const profession & p ) { return !p.pets().empty(); }
        },
        {
            0x4E50, c_light_blue, translate_marker( "Companion" ),
            translate_marker( "Starts with a companion" ),
            []( const profession & p ) { return !p.npcs().empty(); }
        },
        {
            0x4144, c_red, translate_marker( "Addiction" ),
            translate_marker( "Starts addicted" ),
            []( const profession & p ) { return !p.addictions().empty(); }
        },
        {
            0x4D49, c_yellow, translate_marker( "Mission" ),
            translate_marker( "Starts on a mission" ),
            []( const profession & p ) { return !p.missions().empty(); }
        },
        {
            0x5350, c_light_blue, translate_marker( "Spells" ),
            translate_marker( "Starts knowing spells" ),
            []( const profession & p ) { return !p.spells().empty(); }
        },
    };
    return sigils;
}

struct nc_prof_tab {
    Rml::String name_rml;
    Rml::String icon_dec;   //< placeholder tab glyph
    bool selected = false;
    bool done = false;   //< step already passed
};
/// A sigil paired with words. Serves both the legend and the selected profession's chips —
/// see nc_scen_glyph on the SCENARIO tab, same reasoning.
struct nc_prof_glyph {
    Rml::String dec;
    Rml::String label_rml;
};
struct nc_prof_row {
    Rml::String text_rml;
    Rml::String cost_rml;                  //< point cost, coloured by direction
    Rml::Vector<nc_prof_glyph> icons;      //< what this profession hands you
    bool selected = false;                 //< cursor is on this card
    bool chosen = false;                   //< this is the profession in force
};
/// One group: a header plus a CAROUSEL page of its cards.
struct nc_prof_band {
    Rml::String name_rml;
    Rml::String count_rml;
    Rml::String marker_rml;
    bool collapsed = false;
    bool focused = false;        //< cursor is on this header
    bool has_info = false;       //< this band owns the floaty info panel
    bool has_prev_page = false;
    bool has_next_page = false;
    Rml::Vector<nc_prof_row> rows;
};

// ── STARTING-EQUIPMENT SHEET ───────────────────────────────────────────────────
//
// A Caves-of-Qud-style view of the kit the selected profession hands over: a paper doll of
// body slots on the left, a collapsible category tree with weight rollups on the right, and
// a carry-capacity readout underneath. See plans/charcreation-profession-equipment.md.
//
// It takes the whole stage rather than a fourth column of the 188dp info panel: a doll plus a
// two-level tree does not fit there, and growing the panel would undo the guarantee that
// picking a wordy profession cannot move the tree.

/// One cell of the paper doll. Every grid position emits a cell — a `junction` draws Qud's
/// connecting lines instead of a box, and a `blank` holds its column open — so the grid needs
/// no pixel arithmetic, for the same reason the info panel's notch row mirrors the carousel's
/// flex geometry rather than measuring it.
struct nc_eqp_slot {
    Rml::String name_rml;    //< slot caption ("head", "left hand", "wielded")
    Rml::String item_rml;    //< what fills it
    Rml::String more_rml;    //< "+2" when further items also cover this part
    Rml::String dec = "none";   //< tileset sprite; "none" (never "") when it has none
    bool filled = false;
    bool junction = false;   //< spine + rung, no box
    bool blank = false;      //< spacer, nothing drawn
};
/// One row of the doll: left, centre, right. Always three cells.
struct nc_eqp_dollrow {
    Rml::Vector<nc_eqp_slot> cells;
};
/// One line of the category tree. FLAT with level flags rather than a nested `data-for`:
/// collapsing is then a filter over one list, and — because every row is the same height —
/// scrolling the cursor into view is exact arithmetic rather than DOM child indexing.
struct nc_eqp_row {
    Rml::String marker_rml;  //< "-"/"+" on headers, empty on items
    Rml::String name_rml;
    Rml::String stat_rml;    //< the one number worth comparing, per item kind
    Rml::String wt_rml;
    Rml::String vol_rml;
    Rml::String val_rml;
    bool group = false;      //< WIELDED / WORN / CARRIED
    bool sub = false;        //< item category
    bool focused = false;
    /// Collapse-map key, empty on item rows. NOT registered with the data model — the input
    /// loop reads it to know what a click or CONFIRM on this row should toggle.
    std::string key;
};

/// The one number worth comparing for an item of this kind, or an empty string when none is.
///
/// Deliberately ONE column. Qud can afford `→4 ♥1d2 [6 lbs.]` on every row because its
/// player is choosing what to wield; here the reader is choosing a profession, so a wall of
/// per-item stats would out-shout the weights and totals that actually differ between two
/// kits.
auto nc_eqp_stat( const item &it ) -> std::string
{
    if( it.is_gun() ) {
    const int dmg = it.gun_damage( false ).total_damage();
        if( dmg > 0 ) {
            return colorize( string_format( _( "DMG %d" ), dmg ), c_light_red );
        }
    }
    // Armor BEFORE melee: nearly every boot and hard hat clears the melee threshold as a
    // kicking or headbutting weapon, and "hard hat DMG 6" tells the reader nothing they want to
    // know about a hard hat. A weapon is not armor, so it still reaches the melee branch.
    if( it.is_armor() ) {
    const int bash = it.bash_resist();
        const int cut = it.cut_resist();
        if( bash > 0 || cut > 0 ) {
            return colorize( string_format( _( "ARM %d/%d" ), bash, cut ), c_light_blue );
        }
    }
    const int melee = std::max( { it.damage_melee( DT_BASH ), it.damage_melee( DT_CUT ),
                                  it.damage_melee( DT_STAB ) } );
    if( melee >= 4 ) {
    return colorize( string_format( _( "DMG %d" ), melee ), c_light_red );
    }
    return {};
}

/// How the profession's own equip pass will place an item. Same predicates the curses buffer
/// used, so the grouping is the real outcome rather than a taxonomy invented for the display.
enum class nc_eqp_mode : int { wielded = 0, worn, carried, num_modes };

auto nc_eqp_mode_of( const item &it ) -> nc_eqp_mode
{
    if( it.has_flag( json_flag_no_auto_equip ) ) {
    return nc_eqp_mode::carried;
}
if( it.has_flag( json_flag_auto_wield ) ) {
    return nc_eqp_mode::wielded;
}
return it.is_armor() ? nc_eqp_mode::worn : nc_eqp_mode::carried;
}

auto nc_eqp_mode_name( nc_eqp_mode m ) -> std::string
{
    switch( m ) {
    case nc_eqp_mode::wielded:
        return _( "Wielded" );
        case nc_eqp_mode::worn:
            return _( "Worn" );
        case nc_eqp_mode::carried:
            return _( "Carried" );
        default:
            return {};
    }
}

/// Identical items merged into one row: professions routinely hand out four of a thing, and
/// four identical lines are four times the noise for no extra information.
struct nc_eqp_stack {
    const item *rep = nullptr;
    int count = 0;
    units::mass wt = 0_gram;
    units::volume vol = 0_ml;
    double val = 0.0;              //< cents, summed as double: price() is a float
};

auto nc_eqp_weight_str( const units::mass &m ) -> std::string
{
    return string_format( "%.1f %s", convert_weight( m ), weight_units() );
}

struct nc_eqp_build {
    const std::vector<detached_ptr<item>> &items;
    const profession &prof;
    const avatar &u;
    /// Collapse state keyed by STRING ("wielded", "worn|clothing"), never by index:
    /// switching profession changes which categories exist, and index-keyed state would
    /// silently reassign to a different category.
    const std::map<std::string, bool> &collapsed;
    int focus = 0;                 //< cursor position in the emitted row list
    const input_context *ctxt = nullptr;
};

struct nc_prof_session {
    Rml::Vector<nc_prof_tab> tabs;
    nc_shell shell;
    Rml::String points_rml;
    Rml::String cost_rml;
    Rml::Vector<nc_prof_band> bands;
    /// The expand/collapse-all control above the tree. The marker mirrors the band headers'
    /// +/- vocabulary so it reads as the same family, and the label carries the real shortcut
    /// from input_context::get_desc so it stays correct after a rebind.
    Rml::String all_marker_rml;
    Rml::String all_label_rml;
    Rml::Vector<nc_prof_glyph> legend;
    /// Facts about the selected profession, one binding per field — the old single
    /// pre-wrapped buffer could not be given hierarchy and could not fit a fixed panel.
    Rml::String skills_rml;
    Rml::String skills_sub_rml;
    Rml::String traits_rml;
    Rml::String gear_rml;
    Rml::String gear_sub_rml;
    Rml::Vector<nc_prof_glyph> chips;
    Rml::String desc_rml;
    Rml::String info_rml;      //< exhaustive detail, prose column
    Rml::String sort_rml;
    Rml::String gender_rml;
    Rml::String filter_rml;
    /// The starting-equipment sheet. While `eqp_open` the sheet takes the stage and the card
    /// tree is hidden, so the two never compete for the same keys or the same space.
    bool eqp_open = false;
    /// Label of the screen-level control that opens the sheet. It sits in the status row because
    /// that is where a screen-level toggle belongs, beside the sort hint — not because the info
    /// panel cannot host it. The panel was the first home and its click never fired, but the
    /// cause was gui/mainmenu.rml still being open underneath the whole creator and swallowing
    /// hit-tests (see new_character_tab), not anything about the facts column.
    Rml::String eqp_btn_rml;
    Rml::String eqp_title_rml;
    /// Header totals as FOUR fields rather than one spaced string — see build_nc_eqp_sheet for
    /// why RmlUi cannot be trusted to keep spaces between two colour runs.
    Rml::String eqp_count_rml;
    Rml::String eqp_wt_rml;
    Rml::String eqp_vol_rml;
    Rml::String eqp_val_rml;
    Rml::Vector<nc_eqp_dollrow> eqp_doll;
    Rml::Vector<nc_eqp_row> eqp_rows;
    Rml::String eqp_used_rml;
    Rml::String eqp_cap_rml;
    /// A dp LENGTH fed to `data-style-width`, never empty. `data-if` sets `display: none`
    /// rather than removing the element, so this binding is still evaluated with the sheet
    /// shut — an empty string became `width: ;` and RmlUi logged a parse error EVERY FRAME
    /// (97,015 of them in one session before this default was added).
    Rml::String eqp_bar_w = "0";
    bool eqp_over = false;        //< kit exceeds this character's carry capacity
    Rml::String eqp_close_rml;
    Rml::DataModelHandle handle;
};

/// Fills the sheet's bindings and returns how many rows it emitted (the cursor's range).
auto build_nc_eqp_sheet( nc_prof_session &d, const nc_eqp_build &o ) -> int
{
    using namespace std::views;
    namespace ranges = std::ranges;

    // ── Partition ──────────────────────────────────────────────────────────────
    // Per mode, per item category, a list of merged stacks. display_name() is the stack key
    // because it already folds in charges and damage: two items that PRINT the same are the
    // same row as far as the reader is concerned.
    struct cat_bucket {
        const item_category *cat = nullptr;
        std::map<std::string, nc_eqp_stack> stacks;
    };
    std::array<std::map<std::string, cat_bucket>, 3> by_mode;
    units::mass total_wt = 0_gram;
    units::volume total_vol = 0_ml;
    double total_val = 0.0;
    int total_count = 0;

    for( const detached_ptr<item> &ptr : o.items ) {
        if( !ptr ) {
            continue;
        }
        const item &it = *ptr;
        const int mode = static_cast<int>( nc_eqp_mode_of( it ) );
        const item_category &cat = it.get_category();
        cat_bucket &bucket = by_mode[mode][cat.get_id().str()];
        bucket.cat = &cat;
        // `tname( 1, false )` rather than `display_name()`: with ITEM_HEALTH_BAR on,
        // display_name() prefixes every armor item with a durability bar ("|| hoodie"),
        // which is noise on a kit that is pristine by construction. with_prefix=false gates
        // only that prefix and the burnt text (item_display.cpp:343,372) — charges, contents
        // and container suffixes all survive, so "lighter (25/25)" still reads as such.
        nc_eqp_stack &st = bucket.stacks[it.tname( 1, false )];
        if( st.rep == nullptr ) {
            st.rep = &it;
        }
        st.count++;
        st.wt += it.weight();
        st.vol += it.volume();
        st.val += it.price( false );
        total_wt += it.weight();
        total_vol += it.volume();
        total_val += it.price( false );
        total_count++;
    }

    // ── Paper doll ─────────────────────────────────────────────────────────────
    // For each body part, the item with the greatest coverage of it wins the cell (ties to
    // the heavier), and the rest become a "+N" badge. Layered clothing is not drawn as
    // layers; neither does Qud.
    struct slot_pick {
        const item *best = nullptr;
        int best_cov = -1;
        int count = 0;
    };
    std::map<bodypart_str_id, slot_pick> picks;
    for( const detached_ptr<item> &ptr : o.items ) {
        // WORN, not merely `is_armor()`: a profession that packs a spare pair of boots gives
        // an armor item flagged `no_auto_equip`, which the equip pass leaves in the pack. Drawing
        // it on the feet slot would claim the character is wearing something they are not.
        if( !ptr || nc_eqp_mode_of( *ptr ) != nc_eqp_mode::worn ) {
            continue;
        }
        const item &it = *ptr;
        const body_part_set covered = it.get_covered_body_parts();
        for( const body_part bp : all_body_parts ) {
            const bodypart_str_id &bpid = convert_bp( bp );
            if( !covered.test( bpid ) ) {
                continue;
            }
            slot_pick &p = picks[bpid];
            p.count++;
            const int cov = it.get_coverage( bpid.id() );
            if( cov > p.best_cov ||
                ( cov == p.best_cov && p.best != nullptr && it.weight() > p.best->weight() ) ) {
                p.best_cov = cov;
                p.best = &it;
            }
        }
    }
    const item *wielded = nullptr;
    for( const detached_ptr<item> &ptr : o.items ) {
        if( ptr && nc_eqp_mode_of( *ptr ) == nc_eqp_mode::wielded ) {
            wielded = &*ptr;
            break;
        }
    }

    const auto body_cell = [&]( body_part bp ) -> nc_eqp_slot {
        const bodypart_str_id &bpid = convert_bp( bp );
        nc_eqp_slot cell;
        cell.name_rml = cata_text_to_rml( colorize( body_part_name( bpid.id() ), c_dark_gray ) );
        const auto it = picks.find( bpid );
        if( it == picks.end() || it->second.best == nullptr )
        {
            cell.item_rml = cata_text_to_rml( colorize( "—", c_dark_gray ) );
            return cell;
        }
        cell.filled = true;
        cell.item_rml = cata_text_to_rml( colorize( it->second.best->tname( 1, false ), c_light_gray ) );
        cell.dec = nc_tile_sprite_dec( it->second.best->typeId().str(), C_ITEM );
        if( it->second.count > 1 )
        {
            cell.more_rml = cata_text_to_rml( colorize(
                                                  string_format( "+%d", it->second.count - 1 ), c_yellow ) );
        }
        return cell;
    };
    const auto junction = []() -> nc_eqp_slot {
        return { .junction = true };
    };
    const auto blank = []() -> nc_eqp_slot {
        return { .blank = true };
    };

    nc_eqp_slot wield_cell;
    wield_cell.name_rml = cata_text_to_rml( colorize( _( "wielded" ), c_dark_gray ) );
    if( wielded != nullptr ) {
        wield_cell.filled = true;
        wield_cell.item_rml = cata_text_to_rml( colorize( wielded->tname( 1, false ), c_light_gray ) );
        wield_cell.dec = nc_tile_sprite_dec( wielded->typeId().str(), C_ITEM );
    } else {
        wield_cell.item_rml = cata_text_to_rml( colorize( _( "empty hands" ), c_dark_gray ) );
    }

    // Six rows of three. WIELDED sits on the spine between the hands: CBN wields with both
    // hands, so there is no left/right weapon slot to mirror Qud's pair.
    d.eqp_doll.clear();
    d.eqp_doll.push_back( { .cells = { blank(), body_cell( bp_head ), blank() } } );
    d.eqp_doll.push_back( { .cells = { body_cell( bp_eyes ), junction(), body_cell( bp_mouth ) } } );
    d.eqp_doll.push_back( { .cells = { body_cell( bp_arm_l ), body_cell( bp_torso ), body_cell( bp_arm_r ) } } );
    d.eqp_doll.push_back( { .cells = { body_cell( bp_hand_l ), wield_cell, body_cell( bp_hand_r ) } } );
    d.eqp_doll.push_back( { .cells = { body_cell( bp_leg_l ), junction(), body_cell( bp_leg_r ) } } );
    d.eqp_doll.push_back( { .cells = { body_cell( bp_foot_l ), junction(), body_cell( bp_foot_r ) } } );

    // ── Tree ───────────────────────────────────────────────────────────────────
    d.eqp_rows.clear();
    for( int m = 0; m < 3; ++m ) {
        if( by_mode[m].empty() ) {
            continue;   // a group with nothing under it is noise
        }
        const std::string mode_key = string_format( "m%d", m );
        const auto mode_it = o.collapsed.find( mode_key );
        const bool mode_shut = mode_it != o.collapsed.end() && mode_it->second;

        // Categories in the order the item factory declares for display, so this tree agrees
        // with every other item list in the game.
        std::vector<const cat_bucket *> cats;
        for( const auto &[key, bucket] : by_mode[m] ) {
            cats.push_back( &bucket );
        }
        ranges::sort( cats, []( const cat_bucket * a, const cat_bucket * b ) {
            return *a->cat < *b->cat;
        } );

        units::mass mode_wt = 0_gram;
        for( const cat_bucket *bucket : cats ) {
            for( const auto &[name, st] : bucket->stacks ) {
                mode_wt += st.wt;
            }
        }
        nc_eqp_row head;
        head.group = true;
        head.marker_rml = cata_text_to_rml( colorize( mode_shut ? "+" : "-", c_yellow ) );
        head.name_rml = cata_text_to_rml( colorize(
                                              to_upper_case( nc_eqp_mode_name( static_cast<nc_eqp_mode>( m ) ) ), c_white ) );
        head.wt_rml = cata_text_to_rml( colorize( nc_eqp_weight_str( mode_wt ), c_light_gray ) );
        head.key = mode_key;
        d.eqp_rows.push_back( head );
        if( mode_shut ) {
            continue;
        }

        for( const cat_bucket *bucket : cats ) {
            const std::string cat_key = string_format( "m%d|%s", m, bucket->cat->get_id().str() );
            const auto cat_it = o.collapsed.find( cat_key );
            const bool cat_shut = cat_it != o.collapsed.end() && cat_it->second;
            units::mass cat_wt = 0_gram;
            for( const auto &[name, st] : bucket->stacks ) {
                cat_wt += st.wt;
            }
            nc_eqp_row sub;
            sub.sub = true;
            sub.marker_rml = cata_text_to_rml( colorize( cat_shut ? "+" : "-", c_yellow ) );
            sub.name_rml = cata_text_to_rml( colorize( bucket->cat->name(), c_yellow ) );
            sub.wt_rml = cata_text_to_rml( colorize( nc_eqp_weight_str( cat_wt ), c_dark_gray ) );
            sub.key = cat_key;
            d.eqp_rows.push_back( sub );
            if( cat_shut ) {
                continue;
            }
            for( const auto &[name, st] : bucket->stacks ) {
                nc_eqp_row row;
                row.name_rml = cata_text_to_rml( colorize(
                                                     st.count > 1 ? string_format( "%s x%d", name, st.count ) : name,
                                                     c_light_gray ) );
                row.stat_rml = cata_text_to_rml( nc_eqp_stat( *st.rep ) );
                row.wt_rml = cata_text_to_rml( colorize( nc_eqp_weight_str( st.wt ), c_light_gray ) );
                row.vol_rml = cata_text_to_rml( colorize( string_format( "%s %s",
                                                format_volume( st.vol ), volume_units_abbr() ), c_dark_gray ) );
                row.val_rml = cata_text_to_rml( colorize(
                                                    format_money( static_cast<int>( st.val ) ), c_dark_gray ) );
                d.eqp_rows.push_back( row );
            }
        }
    }
    const int nrows = static_cast<int>( d.eqp_rows.size() );
    if( nrows > 0 ) {
        d.eqp_rows[std::clamp( o.focus, 0, nrows - 1 )].focused = true;
    }

    // ── Header, capacity ───────────────────────────────────────────────────────
    d.eqp_title_rml = cata_text_to_rml( colorize( string_format(
                                            _( "Starting equipment · %s" ),
                                            o.prof.gender_appropriate_name( o.u.male ) ), c_white ) );
    // FOUR bindings, not one string with spaces between the parts: `cata_text_to_rml` emits one
    // <span> per colour run and RmlUi trims a lone space between two runs at parse time, which
    // welded the first attempt into "16 items9.1 kg9.32 L$2795.26". Separation is the
    // stylesheet's job; margins cannot be trimmed.
    d.eqp_count_rml = cata_text_to_rml( colorize( string_format(
                                            vgettext( "%d item", "%d items", total_count ),
                                            total_count ), c_light_gray ) );
    d.eqp_wt_rml = cata_text_to_rml( colorize( nc_eqp_weight_str( total_wt ), c_light_gray ) );
    d.eqp_vol_rml = cata_text_to_rml( colorize( string_format( "%s %s",
                                      format_volume( total_vol ), volume_units_abbr() ), c_light_gray ) );
    d.eqp_val_rml = cata_text_to_rml( colorize(
                                          format_money( static_cast<int>( total_val ) ), c_green ) );

    // weight_capacity() is STR-derived and the creator avatar's STR is live, so this readout
    // tracks the STATS step: a kit that fits a strong character and not a weak one says so.
    const units::mass cap = o.u.weight_capacity();
    const double frac = cap > 0_gram
                        ? static_cast<double>( to_gram( total_wt ) ) / to_gram( cap )
                        : 0.0;
    d.eqp_over = frac > 1.0;
    // dp, not "%%": the fill is a plain block inside a fixed-width track, and a percentage
    // width plus a transition starting from `auto` rendered nothing at all. NC_EQP_BAR_DP
    // must match .nc-eqp-bar's width in newcharprofession.rcss minus its 1dp borders.
    constexpr int NC_EQP_BAR_DP = 318;
    d.eqp_bar_w = string_format( "%ddp",
                                 static_cast<int>( std::lround( std::clamp( frac, 0.0, 1.0 ) *
                                     NC_EQP_BAR_DP ) ) );
    // Same reason as the totals: the "/" and its spacing live in the markup.
    d.eqp_used_rml = cata_text_to_rml( colorize( string_format( "%.1f",
                                       convert_weight( total_wt ) ),
                                       d.eqp_over ? c_light_red : c_light_gray ) );
    d.eqp_cap_rml = cata_text_to_rml( colorize( nc_eqp_weight_str( cap ), c_dark_gray ) );
    d.eqp_close_rml = cata_text_to_rml( colorize( string_format(
                                            _( "Close [%s]" ),
                                            o.ctxt != nullptr ? o.ctxt->get_desc( "VIEW_EQUIPMENT", 1 ) : std::string( "?" ) ),
                                        c_dark_gray ) );
    return nrows;
}

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
    Rml::StructHandle<nc_prof_glyph> gh = c.RegisterStruct<nc_prof_glyph>();
    gh.RegisterMember( "dec", &nc_prof_glyph::dec );
    gh.RegisterMember( "label_rml", &nc_prof_glyph::label_rml );
    // One array registration covers the strip, the chips and the legend — same element type.
    c.RegisterArray<Rml::Vector<nc_prof_glyph>>();
    Rml::StructHandle<nc_prof_row> rh = c.RegisterStruct<nc_prof_row>();
    rh.RegisterMember( "text_rml", &nc_prof_row::text_rml );
    rh.RegisterMember( "cost_rml", &nc_prof_row::cost_rml );
    rh.RegisterMember( "icons", &nc_prof_row::icons );
    rh.RegisterMember( "selected", &nc_prof_row::selected );
    rh.RegisterMember( "chosen", &nc_prof_row::chosen );
    c.RegisterArray<Rml::Vector<nc_prof_row>>();
    Rml::StructHandle<nc_prof_band> bh = c.RegisterStruct<nc_prof_band>();
    bh.RegisterMember( "name_rml", &nc_prof_band::name_rml );
    bh.RegisterMember( "count_rml", &nc_prof_band::count_rml );
    bh.RegisterMember( "marker_rml", &nc_prof_band::marker_rml );
    bh.RegisterMember( "collapsed", &nc_prof_band::collapsed );
    bh.RegisterMember( "focused", &nc_prof_band::focused );
    bh.RegisterMember( "has_info", &nc_prof_band::has_info );
    bh.RegisterMember( "has_prev_page", &nc_prof_band::has_prev_page );
    bh.RegisterMember( "has_next_page", &nc_prof_band::has_next_page );
    bh.RegisterMember( "rows", &nc_prof_band::rows );
    c.RegisterArray<Rml::Vector<nc_prof_band>>();
    Rml::StructHandle<nc_eqp_slot> sh = c.RegisterStruct<nc_eqp_slot>();
    sh.RegisterMember( "name_rml", &nc_eqp_slot::name_rml );
    sh.RegisterMember( "item_rml", &nc_eqp_slot::item_rml );
    sh.RegisterMember( "more_rml", &nc_eqp_slot::more_rml );
    sh.RegisterMember( "dec", &nc_eqp_slot::dec );
    sh.RegisterMember( "filled", &nc_eqp_slot::filled );
    sh.RegisterMember( "junction", &nc_eqp_slot::junction );
    sh.RegisterMember( "blank", &nc_eqp_slot::blank );
    c.RegisterArray<Rml::Vector<nc_eqp_slot>>();
    Rml::StructHandle<nc_eqp_dollrow> dh = c.RegisterStruct<nc_eqp_dollrow>();
    dh.RegisterMember( "cells", &nc_eqp_dollrow::cells );
    c.RegisterArray<Rml::Vector<nc_eqp_dollrow>>();
    Rml::StructHandle<nc_eqp_row> eh = c.RegisterStruct<nc_eqp_row>();
    eh.RegisterMember( "marker_rml", &nc_eqp_row::marker_rml );
    eh.RegisterMember( "name_rml", &nc_eqp_row::name_rml );
    eh.RegisterMember( "stat_rml", &nc_eqp_row::stat_rml );
    eh.RegisterMember( "wt_rml", &nc_eqp_row::wt_rml );
    eh.RegisterMember( "vol_rml", &nc_eqp_row::vol_rml );
    eh.RegisterMember( "val_rml", &nc_eqp_row::val_rml );
    eh.RegisterMember( "group", &nc_eqp_row::group );
    eh.RegisterMember( "sub", &nc_eqp_row::sub );
    eh.RegisterMember( "focused", &nc_eqp_row::focused );
    c.RegisterArray<Rml::Vector<nc_eqp_row>>();
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

    // ── Tree/carousel view state ──────────────────────────────────────────────
    //
    // Mirrors the SCENARIO tab (plans/charcreation-scenario-tree.md): `cur_id` stays the
    // authoritative index into the flat sorted list, so SORT, FILTER, RANDOMIZE and
    // CHANGE_GENDER keep working untouched — the grouping is a VIEW over that list.
    //
    // Six cards per page, matching SCENARIO: same panel width, and a card has to hold a
    // wrapped two-line profession name.
    constexpr int NC_PROF_PAGE = 6;
    // Bands are DISCOVERED from the data, not fixed: one per skill display category that some
    // profession actually leads with, plus "unskilled" last. A mod adding a skill category
    // therefore adds a band without touching this file.
    std::vector<skill_displayType_id> band_cats;
    std::vector<std::vector<int>> band_items;      //< indices into sorted_profs, per band
    std::vector<bool> band_collapsed;
    std::vector<int> band_page;
    bool first_group_build = true;
    int focus_band = 0;
    bool focus_header = true;
    int focus_card = 0;
    // Click intent, applied ONCE per input cycle. A click callback must never mutate directly:
    // `data-event-*` accumulates one listener per `data-for` regeneration, so a toggle run an
    // even number of times cancels itself out. See the SCENARIO tab for the measurement.
    int pending_band = -1;
    int pending_card_band = -1;
    int pending_card_slot = -1;
    int pending_page_band = -1;
    int pending_page_dir = 0;
    bool pending_all = false;
    // ── Starting-equipment sheet state ────────────────────────────────────────
    //
    // Opened over the tree rather than beside it: a paper doll plus a two-level item tree
    // does not fit the fixed 188dp info panel. See plans/charcreation-profession-equipment.md.
    bool eqp_open = false;
    int eqp_focus = 0;
    int eqp_rows = 0;
    // Keyed by string ("m1", "m1|clothing"), never by index: switching profession changes
    // which categories exist, so an index-keyed map would reassign state to a stranger.
    std::map<std::string, bool> eqp_collapsed;
    bool pending_eqp_open = false;
    int pending_eqp_row = -1;

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
    // Expand/collapse every group at once. The UI control and this key are the same
    // action, so a rebind moves the hint printed beside the control too.
    ctxt.register_action( "TOGGLE_ALL_GROUPS" );
    // Opens the starting-equipment sheet over the tree. The label beside the sheet's close
    // control prints this binding via get_desc, so a rebind moves the hint too.
    ctxt.register_action( "VIEW_EQUIPMENT" );
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
    const auto sync_rml = [&]() {
        if( !data->handle ) {
            return;
        }
        data->tabs = build_nc_char_tabs<nc_prof_tab>( 2 );  // PROFESSION tab active
        data->shell = fill_nc_shell( 2, ctxt );
        set_nc_portrait( data->shell, use_character_preview );
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
            // Traits and skills are NOT repeated here: the fact fields beside this buffer own
            // them now, and stating them twice in one panel is noise. What stays is the
            // itemised detail no summary can carry.
            std::vector<std::pair<skill_id, int>> prof_skills = pid->skills();
            std::stable_sort( prof_skills.begin(), prof_skills.end(),
            []( const std::pair<skill_id, int> &a, const std::pair<skill_id, int> &b ) {
                return localized_compare( std::make_pair( a.first->display_category(), a.first->name() ),
                                          std::make_pair( b.first->display_category(), b.first->name() ) );
            } );
            // Full skill breakdown by category — the fact field shows only the top three.
            // Already sorted by (category, name) just above.
            if( !prof_skills.empty() ) {
                buf += colorize( _( "Skills:" ), c_light_blue ) + "\n";
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
            // Items are built ONCE per sync and shared by the GEAR fact, the paper doll and the
            // equipment tree — this used to be constructed twice per redraw (here and for the
            // gear count), and profession::items() runs item groups to do it.
            const auto prof_items = pid->items( u.male, u.get_mutations() );
            // No flat "Items:" list here any more: the equipment sheet ([g]) itemises the same
            // kit with weights, volumes, values, rollups and a body-slot doll, and two
            // disagreeing item lists on one screen is exactly what the fact fields fixed for
            // skills and traits.
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

            // Facts: the handful that decide the choice, as label/value fields. The prose
            // buffer above keeps the exhaustive detail — it is scrollable and nobody reads it
            // to compare two professions.
            std::vector<std::pair<skill_id, int>> fact_skills = pid->skills();
            std::ranges::sort( fact_skills, []( const auto & a, const auto & b ) {
                return a.second > b.second;
            } );
            if( fact_skills.empty() ) {
                data->skills_rml = cata_text_to_rml( colorize( _( "None" ), c_dark_gray ) );
                data->skills_sub_rml.clear();
            } else {
                // Top three by level: enough to characterise a profession, short enough to
                // stay on one line at card-panel width.
                std::string top;
                for( std::size_t k = 0; k < fact_skills.size() && k < 3; ++k ) {
                    if( k > 0 ) {
                        top += ", ";
                    }
                    top += string_format( "%s %d", fact_skills[k].first->name(),
                                          fact_skills[k].second );
                }
                data->skills_rml = cata_text_to_rml( top );
                data->skills_sub_rml = fact_skills.size() > 3
                                       ? cata_text_to_rml( colorize( string_format(
                                               vgettext( "and %d more skill", "and %d more skills",
                                                   static_cast<int>( fact_skills.size() ) - 3 ),
                                               static_cast<int>( fact_skills.size() ) - 3 ), c_dark_gray ) )
                                       : Rml::String();
            }

            const std::vector<trait_id> fact_traits = pid->get_locked_traits();
            if( fact_traits.empty() ) {
                data->traits_rml = cata_text_to_rml( colorize( _( "None" ), c_dark_gray ) );
            } else {
                std::string tl;
                for( std::size_t k = 0; k < fact_traits.size(); ++k ) {
                    if( k > 0 ) {
                        tl += ", ";
                    }
                    tl += mutation_branch::get_name( fact_traits[k] );
                }
                data->traits_rml = cata_text_to_rml( tl );
            }

            // GEAR keeps summarising with the sheet shut — count, total weight and cash — so
            // the reader never has to open anything to compare two kits at a glance.
            const int gear_count = static_cast<int>( prof_items.size() );
            units::mass gear_wt = 0_gram;
            for( const detached_ptr<item> &gi : prof_items ) {
                if( gi ) {
                    gear_wt += gi->weight();
                }
            }
            data->gear_rml = cata_text_to_rml( string_format(
                                                   vgettext( "%d item", "%d items", gear_count ), gear_count ) );
            const std::optional<int> fact_cash = pid->starting_cash();
            std::string gear_sub = colorize( nc_eqp_weight_str( gear_wt ), c_dark_gray );
            if( fact_cash.value_or( 0 ) != 0 ) {
                gear_sub += "  " + colorize( format_money( *fact_cash ), c_green );
            }
            data->gear_sub_rml = cata_text_to_rml( gear_sub );

            // The equipment sheet. Built only while it is on screen: it walks every item three
            // times (partition, doll, tree) and nothing reads it when shut.
            if( eqp_open ) {
                eqp_rows = build_nc_eqp_sheet( *data, {
                    .items = prof_items,
                    .prof = *pid,
                    .u = u,
                    .collapsed = eqp_collapsed,
                    .focus = eqp_focus,
                    .ctxt = &ctxt
                } );
            }

            // Chips: this profession's own sigils in words, from the SAME table the card strip
            // uses, so a card can never show a glyph the panel fails to explain.
            data->chips.clear();
            for( const nc_prof_sigil &s : nc_prof_sigils() ) {
                if( !s.present( *pid ) ) {
                    continue;
                }
                data->chips.push_back( {
                    .dec = nc_icon_dec_col( s.seed, 14, s.col ),
                    .label_rml = cata_text_to_rml( colorize( _( s.desc ), s.col ) ) } );
            }

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
            data->skills_rml.clear();
            data->skills_sub_rml.clear();
            data->traits_rml.clear();
            data->gear_rml.clear();
            data->gear_sub_rml.clear();
            data->chips.clear();
            data->sort_rml.clear();
            data->gender_rml.clear();
            // Nothing selected means nothing to itemise. Closed through the LOCAL, which the
            // input loop reads: setting only the model flag would hide the sheet while leaving
            // the loop convinced it is up, so the navigation keys would stay captured by
            // something not on screen.
            eqp_open = false;
            eqp_rows = 0;
            eqp_focus = 0;
            data->eqp_doll.clear();
            data->eqp_rows.clear();
            data->eqp_title_rml.clear();
            data->eqp_count_rml.clear();
            data->eqp_wt_rml.clear();
            data->eqp_vol_rml.clear();
            data->eqp_val_rml.clear();
            data->eqp_used_rml.clear();
            data->eqp_cap_rml.clear();
            data->eqp_close_rml.clear();
            // Reset, not cleared: an empty value is not a parseable length.
            data->eqp_bar_w = "0";
        }
        // One assignment, after both branches: the local is the single source of truth for
        // whether the sheet is up, and the model always agrees with it.
        data->eqp_open = eqp_open;

        // Static vocabulary: built once, then left alone.
        if( data->legend.empty() ) {
            for( const nc_prof_sigil &s : nc_prof_sigils() ) {
                data->legend.push_back( {
                    .dec = nc_icon_dec_col( s.seed, 14, s.col ),
                    .label_rml = cata_text_to_rml( colorize( _( s.label ), s.col ) ) } );
            }
            // Filled after the model was constructed, so the data-for must be told.
            data->handle.DirtyVariable( "legend" );
        }

        // Expand/collapse-all control. Its label states what a click WILL do rather than the
        // current state: a control named after its own condition makes the reader work out the
        // consequence for themselves.
        {
            const bool any_open = std::ranges::any_of( band_collapsed, []( bool c ) { return !c; } );
            data->all_marker_rml = cata_text_to_rml( colorize( any_open ? "-" : "+", c_yellow ) );
            data->all_label_rml = cata_text_to_rml( colorize( string_format(
                    any_open ? _( "Collapse all  [%s]" ) : _( "Expand all  [%s]" ),
                    ctxt.get_desc( "TOGGLE_ALL_GROUPS", 1 ) ), c_dark_gray ) );
        }

        // Grouped carousel view. Only the VISIBLE page of each open band is emitted, so the
        // document holds a couple of dozen cards rather than all 258 professions.
        data->bands.clear();
        for( std::size_t b = 0; b < band_cats.size(); ++b ) {
            nc_prof_band band;
            const int total = static_cast<int>( band_items[b].size() );
            band.name_rml = cata_text_to_rml( colorize(
                                                  band_cats[b].is_null()
                                                  ? _( "Unskilled" )
                                                  // Used verbatim. display_string() reads "Melee skills", which is a little
                                                  // long for a header above cards that are obviously professions — but it is
                                                  // TRANSLATED, and stripping a " skills" suffix would be prefix-matching a
                                                  // localised string: the same mistake the scenario grouping avoids by
                                                  // deriving from the CHALLENGE flag instead of the "Challenge - " name.
                                                  : SkillDisplayType::get_skill_type( band_cats[b] ).display_string(),
                                                  c_white ) );
            band.collapsed = band_collapsed[b];
            band.focused = ( static_cast<int>( b ) == focus_band && focus_header );
            band.has_info = !band_collapsed[b] && static_cast<int>( b ) == focus_band &&
                            !focus_header;
            const int page_start = band_collapsed[b] ? 0 : band_page[b];
            const int page_end = std::min( total, page_start + NC_PROF_PAGE );
            band.count_rml = cata_text_to_rml( colorize(
                                                   band_collapsed[b] || total <= NC_PROF_PAGE
                                                   ? string_format( "%d", total )
                                                   : string_format( "%d-%d / %d", page_start + 1, page_end, total ),
                                                   c_dark_gray ) );
            band.marker_rml = cata_text_to_rml( colorize(
                                                    band_collapsed[b] ? "+" : "-", c_yellow ) );
            band.has_prev_page = !band_collapsed[b] && page_start > 0;
            band.has_next_page = !band_collapsed[b] && page_end < total;
            if( !band_collapsed[b] ) {
                for( int k = page_start; k < page_end; ++k ) {
                    const int idx = band_items[b][k];
                    const string_id<profession> &pr = sorted_profs[idx];
                    nc_prof_row r;
                    r.chosen = ( u.prof == pr );
                    // Name stays UNCOLOURED: the card's own border and fill carry cursor and
                    // chosen state, and a baked colour would beat any stylesheet rule.
                    r.text_rml = cata_text_to_rml( colorize(
                                                       pr->gender_appropriate_name( u.male ), c_light_gray ) );
                    const int cost = pr->point_cost();
                    r.cost_rml = cata_text_to_rml( colorize(
                                                       cost == 0 ? std::string( "0" ) : string_format( "%+d", -cost ),
                                                       cost > 0 ? c_red : ( cost < 0 ? c_green : c_dark_gray ) ) );
                    for( const nc_prof_sigil &s : nc_prof_sigils() ) {
                        if( s.present( *pr ) ) {
                            r.icons.push_back( { .dec = nc_icon_dec_col( s.seed, 14, s.col ) } );
                        }
                    }
                    r.selected = ( static_cast<int>( b ) == focus_band && !focus_header &&
                                   k == focus_card );
                    band.rows.push_back( r );
                }
            }
            data->bands.push_back( band );
        }

        // Screen-level control: states the action, and takes its shortcut from get_desc so a
        // rebind moves the printed key too — same contract as the expand/collapse-all control.
        data->eqp_btn_rml = cata_text_to_rml( colorize( string_format(
                _( "Equipment  [%s]" ),
                ctxt.get_desc( "VIEW_EQUIPMENT", 1 ) ), c_light_gray ) );

        data->filter_rml = cata_text_to_rml( string_format( "<%s>",
                                             filterstring.empty() ? _( "no filter" ) : filterstring ) );

        data->handle.DirtyVariable( "tabs" );
        dirty_nc_shell( data->handle );
        data->handle.DirtyVariable( "points_rml" );
        data->handle.DirtyVariable( "cost_rml" );
        data->handle.DirtyVariable( "bands" );
        data->handle.DirtyVariable( "all_marker_rml" );
        data->handle.DirtyVariable( "all_label_rml" );
        data->handle.DirtyVariable( "desc_rml" );
        data->handle.DirtyVariable( "info_rml" );
        data->handle.DirtyVariable( "skills_rml" );
        data->handle.DirtyVariable( "skills_sub_rml" );
        data->handle.DirtyVariable( "traits_rml" );
        data->handle.DirtyVariable( "gear_rml" );
        data->handle.DirtyVariable( "gear_sub_rml" );
        data->handle.DirtyVariable( "chips" );
        data->handle.DirtyVariable( "sort_rml" );
        data->handle.DirtyVariable( "gender_rml" );
        data->handle.DirtyVariable( "filter_rml" );
        data->handle.DirtyVariable( "eqp_open" );
        data->handle.DirtyVariable( "eqp_btn_rml" );
        data->handle.DirtyVariable( "eqp_title_rml" );
        data->handle.DirtyVariable( "eqp_count_rml" );
        data->handle.DirtyVariable( "eqp_wt_rml" );
        data->handle.DirtyVariable( "eqp_vol_rml" );
        data->handle.DirtyVariable( "eqp_val_rml" );
        data->handle.DirtyVariable( "eqp_doll" );
        data->handle.DirtyVariable( "eqp_rows" );
        data->handle.DirtyVariable( "eqp_used_rml" );
        data->handle.DirtyVariable( "eqp_cap_rml" );
        data->handle.DirtyVariable( "eqp_bar_w" );
        data->handle.DirtyVariable( "eqp_over" );
        data->handle.DirtyVariable( "eqp_close_rml" );

        // No ScrollIntoView: the carousel pages the cursor into view (sync_cur_from_focus),
        // so there is never an off-screen card to scroll to. The old block indexed a flat
        // #nc-prof-list by cur_id, which the grouped view no longer has.
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
    // Keeps the sheet's cursor on screen. Every tree row is the SAME height by stylesheet
    // rule, so row height is scroll_height / row_count exactly — no DOM child indexing, which
    // `data-for` makes unreliable anyway.
    const auto scroll_eqp_to_focus = [&]() {
        if( !rml || eqp_rows <= 0 ) {
            return;
        }
        Rml::Element *e = rml.document()->GetElementById( "nc-eqp-tree" );
        if( e == nullptr ) {
            return;
        }
        const float page = e->GetClientHeight();
        const float total = e->GetScrollHeight();
        const float row_h = total / static_cast<float>( eqp_rows );
        const float want = row_h * static_cast<float>( eqp_focus ) - page * 0.5f;
        e->SetScrollTop( std::clamp( want, 0.0f, std::max( 0.0f, total - page ) ) );
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
        c.Bind( "bands", &data->bands );
        c.Bind( "legend", &data->legend );
        c.Bind( "all_marker_rml", &data->all_marker_rml );
        c.Bind( "all_label_rml", &data->all_label_rml );
        c.BindEventCallback( "on_toggle_all",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) {
            pending_all = true;
        } );
        // Click callbacks RECORD INTENT and mutate nothing — `data-event-*` installs a
        // listener per generated element and a `data-for` regeneration adds another without
        // removing the old, so one click invokes these an unbounded number of times. The loop
        // applies the intent once. See plans/charcreation-scenario-tree.md for the measurement.
        c.BindEventCallback( "on_band",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int b = -1;
            if( !args.empty() ) {
                args[0].GetInto( b );
            }
            if( b >= 0 && b < static_cast<int>( band_cats.size() ) ) {
                pending_band = b;
            }
        } );
        c.BindEventCallback( "on_card",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int b = -1;
            int k = -1;
            if( args.size() >= 2 ) {
                args[0].GetInto( b );
                args[1].GetInto( k );
            }
            if( b >= 0 && b < static_cast<int>( band_cats.size() ) && k >= 0 ) {
                pending_card_band = b;
                pending_card_slot = k;
            }
        } );
        c.BindEventCallback( "on_page",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int b = -1;
            int d = 0;
            if( args.size() >= 2 ) {
                args[0].GetInto( b );
                args[1].GetInto( d );
            }
            if( b >= 0 && b < static_cast<int>( band_cats.size() ) && d != 0 ) {
                pending_page_band = b;
                pending_page_dir = d;
            }
        } );
        c.Bind( "desc_rml", &data->desc_rml );
        c.Bind( "info_rml", &data->info_rml );
        c.Bind( "skills_rml", &data->skills_rml );
        c.Bind( "skills_sub_rml", &data->skills_sub_rml );
        c.Bind( "traits_rml", &data->traits_rml );
        c.Bind( "gear_rml", &data->gear_rml );
        c.Bind( "gear_sub_rml", &data->gear_sub_rml );
        c.Bind( "chips", &data->chips );
        c.Bind( "sort_rml", &data->sort_rml );
        c.Bind( "gender_rml", &data->gender_rml );
        c.Bind( "filter_rml", &data->filter_rml );
        c.Bind( "eqp_open", &data->eqp_open );
        c.Bind( "eqp_btn_rml", &data->eqp_btn_rml );
        c.Bind( "eqp_title_rml", &data->eqp_title_rml );
        c.Bind( "eqp_count_rml", &data->eqp_count_rml );
        c.Bind( "eqp_wt_rml", &data->eqp_wt_rml );
        c.Bind( "eqp_vol_rml", &data->eqp_vol_rml );
        c.Bind( "eqp_val_rml", &data->eqp_val_rml );
        c.Bind( "eqp_doll", &data->eqp_doll );
        c.Bind( "eqp_rows", &data->eqp_rows );
        c.Bind( "eqp_used_rml", &data->eqp_used_rml );
        c.Bind( "eqp_cap_rml", &data->eqp_cap_rml );
        c.Bind( "eqp_bar_w", &data->eqp_bar_w );
        c.Bind( "eqp_over", &data->eqp_over );
        c.Bind( "eqp_close_rml", &data->eqp_close_rml );
        // Same rule as the tree's callbacks: RECORD INTENT, mutate nothing. `data-event-*`
        // installs one listener per generated element and a `data-for` regeneration adds
        // another without removing the old, so a handler that toggled here would self-cancel.
        c.BindEventCallback( "on_gear",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) {
            pending_eqp_open = true;
        } );
        c.BindEventCallback( "on_eqp_row",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int r = -1;
            if( !args.empty() ) {
                args[0].GetInto( r );
            }
            if( r >= 0 ) {
                pending_eqp_row = r;
            }
        } );
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

            // Re-derive the grouped view. Order WITHIN a band follows sorted_profs, so SORT
            // still governs card order; only the partition is new.
            //
            // Band order follows the order skill_display_type declares its categories, so it
            // is stable and data-driven rather than dependent on which profession happened to
            // be first. Unskilled goes last: it is a fallback, not a discipline.
            {
                std::vector<skill_displayType_id> discovered;
                for( const SkillDisplayType &dt : SkillDisplayType::skillTypes ) {
                    discovered.push_back( dt.ident() );
                }
                discovered.push_back( skill_displayType_id::NULL_ID() );

                std::vector<std::vector<int>> items( discovered.size() );
                for( int k = 0; k < profs_length; ++k ) {
                    const skill_displayType_id cat = nc_classify_prof( *sorted_profs[k] );
                    const auto it = std::ranges::find( discovered, cat );
                    // A category no display type declares (mod data) falls into Unskilled
                    // rather than vanishing from every band.
                    items[it != discovered.end()
                          ? std::distance( discovered.begin(), it )
                          : discovered.size() - 1].push_back( k );
                }

                // Drop empty bands: a header with nothing under it is noise, and which
                // categories are populated depends on the loaded mods.
                std::vector<skill_displayType_id> kept_cats;
                std::vector<std::vector<int>> kept_items;
                for( std::size_t b = 0; b < discovered.size(); ++b ) {
                    if( !items[b].empty() ) {
                        kept_cats.push_back( discovered[b] );
                        kept_items.push_back( std::move( items[b] ) );
                    }
                }
                // Preserve collapse state across a re-sort by category, not by index: FILTER
                // can remove a whole band and shift every later one.
                std::vector<bool> kept_collapsed( kept_cats.size(), true );
                std::vector<int> kept_page( kept_cats.size(), 0 );
                for( std::size_t b = 0; b < kept_cats.size(); ++b ) {
                    const auto old_it = std::ranges::find( band_cats, kept_cats[b] );
                    if( old_it != band_cats.end() ) {
                        kept_collapsed[b] = band_collapsed[std::distance( band_cats.begin(), old_it )];
                    }
                }
                band_cats = std::move( kept_cats );
                band_items = std::move( kept_items );
                band_collapsed = std::move( kept_collapsed );
                band_page = std::move( kept_page );
            }

            // Park the cursor on the band holding the active profession, and on ENTRY open
            // that band — a profession is already selected, so opening it puts the cursor, the
            // notch and the info panel on something real. Only on the first build: a later
            // SORT or FILTER must not reopen a band the player deliberately closed.
            focus_band = 0;
            focus_header = true;
            focus_card = 0;
            for( std::size_t b = 0; b < band_items.size(); ++b ) {
                const auto it = std::ranges::find( band_items[b], cur_id );
                if( it != band_items[b].end() ) {
                    focus_band = static_cast<int>( b );
                    focus_card = static_cast<int>( std::distance( band_items[b].begin(), it ) );
                    if( first_group_build ) {
                        band_collapsed[b] = false;
                        first_group_build = false;
                    }
                    focus_header = band_collapsed[b];
                    band_page[b] = focus_card - focus_card % NC_PROF_PAGE;
                    break;
                }
            }

            recalc_profs = false;
        }

        ui_manager::redraw();
        nc_nav = 0;
        pending_all = false;
        pending_band = -1;
        pending_card_band = -1;
        pending_card_slot = -1;
        pending_page_band = -1;
        pending_page_dir = 0;
        pending_eqp_open = false;
        pending_eqp_row = -1;
        std::string action = ctxt.handle_input();
        if( nc_nav != 0 ) {
            action = nc_nav < 0 ? "PREV_TAB" : "NEXT_TAB";
        }
        // ── Starting-equipment sheet ──────────────────────────────────────────
        //
        // The sheet takes the stage, so while it is up it OWNS the navigation keys: leaving
        // them wired to the hidden card tree would move an invisible cursor and change the
        // selected profession behind the reader's back. Handled before everything below for
        // that reason, including TOGGLE_ALL_GROUPS, which the sheet reuses for its own groups.
        if( pending_eqp_open ) {
            eqp_open = true;
            eqp_focus = 0;
            continue;
        }
        if( action == "VIEW_EQUIPMENT" ) {
            eqp_open = !eqp_open;
            eqp_focus = 0;
            continue;
        }
        if( eqp_open ) {
            // Collapse key of the focused row, empty on item rows (which toggle nothing).
            const auto focus_key = [&]() -> std::string {
                const int n = static_cast<int>( data->eqp_rows.size() );
                return eqp_focus >= 0 && eqp_focus < n ? data->eqp_rows[eqp_focus].key : std::string();
            };
            if( pending_eqp_row >= 0 &&
                pending_eqp_row < static_cast<int>( data->eqp_rows.size() ) ) {
                eqp_focus = pending_eqp_row;
                const std::string &key = data->eqp_rows[pending_eqp_row].key;
                if( !key.empty() ) {
                    eqp_collapsed[key] = !eqp_collapsed[key];
                }
            } else if( action == "DOWN" ) {
                eqp_focus = std::min( eqp_focus + 1, std::max( 0, eqp_rows - 1 ) );
                scroll_eqp_to_focus();
            } else if( action == "UP" ) {
                eqp_focus = std::max( eqp_focus - 1, 0 );
                scroll_eqp_to_focus();
            } else if( action == "CONFIRM" ) {
                const std::string key = focus_key();
                if( !key.empty() ) {
                    eqp_collapsed[key] = !eqp_collapsed[key];
                }
            } else if( action == "LEFT" ) {
                const std::string key = focus_key();
                if( !key.empty() ) {
                    eqp_collapsed[key] = true;
                }
            } else if( action == "RIGHT" ) {
                const std::string key = focus_key();
                if( !key.empty() ) {
                    eqp_collapsed[key] = false;
                }
            } else if( action == "TOGGLE_ALL_GROUPS" ) {
                // A key absent from the map means EXPANDED, so "expand all" is a clear() and
                // cannot leave a nested category shut behind a reopened group — which a
                // set-every-visible-key loop would, since a collapsed group emits no category
                // rows for the loop to see.
                const bool any_open = std::ranges::any_of( data->eqp_rows,
                [&]( const nc_eqp_row & r ) {
                    if( r.key.empty() ) {
                        return false;
                    }
                    const auto it = eqp_collapsed.find( r.key );
                    return it == eqp_collapsed.end() || !it->second;
                } );
                if( any_open ) {
                    for( const nc_eqp_row &r : data->eqp_rows ) {
                        if( !r.key.empty() ) {
                            eqp_collapsed[r.key] = true;
                        }
                    }
                } else {
                    eqp_collapsed.clear();
                }
                eqp_focus = 0;
            } else if( action == "QUIT" ) {
                // Closes the sheet rather than asking about the main menu: ESC on an open
                // overlay means "back", not "quit the whole thing".
                eqp_open = false;
            } else if( action == "PREV_TAB" ) {
                retval = tab_direction::BACKWARD;
            } else if( action == "NEXT_TAB" ) {
                retval = tab_direction::FORWARD;
            } else if( action == "CHANGE_GENDER" ) {
                // Gender changes the kit, so it stays live with the sheet open.
                u.male = !u.male;
                profession_sorter.male = u.male;
                if( !profession_sorter.sort_by_points ) {
                    std::sort( sorted_profs.begin(), sorted_profs.end(), profession_sorter );
                }
            }
            continue;
        }
        // ── Tree navigation ───────────────────────────────────────────────────
        //
        // Vertical order is header, cards, next header … with a collapsed band's cards
        // skipped, so the cursor can never land on a card that is not on screen. Horizontal
        // movement walks the focused band's cards and advances the carousel page at either
        // end, which is why paging needs no key of its own.
        const auto band_size = [&]( int b ) {
            return static_cast<int>( band_items[b].size() );
        };
        const auto nbands = [&]() {
            return static_cast<int>( band_cats.size() );
        };
        // Keeps cur_id (and therefore the panel, the portrait and CONFIRM) on the focused card.
        const auto sync_cur_from_focus = [&]() {
            if( !focus_header && band_size( focus_band ) > 0 ) {
                focus_card = std::clamp( focus_card, 0, band_size( focus_band ) - 1 );
                cur_id = band_items[focus_band][focus_card];
                desc_offset = 0;
                if( focus_card < band_page[focus_band] ||
                    focus_card >= band_page[focus_band] + NC_PROF_PAGE ) {
                    band_page[focus_band] = focus_card - focus_card % NC_PROF_PAGE;
                }
                // The portrait wears the selected profession's kit, so it has to be redrawn
                // the moment the cursor moves rather than on the next input.
                if( use_character_preview ) {
                    ui_manager::redraw();
                }
            }
        };
        // Commit the focused profession. One definition, called by CONFIRM and by a card
        // click, so "picking" cannot come to mean two different things on one screen — and so
        // this tab behaves like the SCENARIO tab, where a card click also selects.
        const auto pick_prof = [&]() {
            if( !( cur_id >= 0 && static_cast<std::size_t>( cur_id ) < sorted_profs.size() ) ) {
                return;
            }
            if( sorted_profs[cur_id] == u.prof ) {
                return;   // already in force: toggling its locked traits off and on is not a no-op
            }
            // Remove traits from the previous profession
            for( const trait_id &old_trait : u.prof->get_locked_traits() ) {
                u.toggle_trait( old_trait );
            }
            const int netPointCost = sorted_profs[cur_id]->point_cost() - u.prof->point_cost();
            u.prof = sorted_profs[cur_id];
            u.set_base_age( random_age_for_profession( *u.prof ) );
            // Add traits for the new profession (and perhaps scenario, if, for example, both
            // the scenario and old profession require the same trait)
            newcharacter::add_traits( u, points );
            points.skill_points -= netPointCost;
        };
        // Expand every group, or collapse every group when any is open. Collapsing parks the
        // cursor on the focused group's header, because the card it stood on is gone.
        const auto toggle_all_bands = [&]() {
            const bool any_open = std::ranges::any_of( band_collapsed, []( bool c ) { return !c; } );
            for( std::size_t b = 0; b < band_collapsed.size(); ++b ) {
                band_collapsed[b] = any_open;
            }
            if( any_open ) {
                focus_header = true;
            } else if( band_size( focus_band ) > 0 ) {
                focus_header = false;
                sync_cur_from_focus();
            }
        };
        if( pending_all || action == "TOGGLE_ALL_GROUPS" ) {
            toggle_all_bands();
        }
        // Apply click intent recorded during handle_input, exactly once — however many times
        // the callback ran. Cleared above so a stale intent cannot re-apply on a later frame.
        if( pending_band >= 0 ) {
            const int b = pending_band;
            focus_band = b;
            band_collapsed[b] = !band_collapsed[b];
            // Opening steps onto the first visible card so the notch and panel have something
            // to describe; closing returns the cursor to the header.
            focus_header = band_collapsed[b] || band_size( b ) == 0;
            if( !focus_header ) {
                focus_card = band_page[b];
                sync_cur_from_focus();
            }
        } else if( pending_page_band >= 0 ) {
            const int b = pending_page_band;
            const int next = band_page[b] + pending_page_dir * NC_PROF_PAGE;
            if( next >= 0 && next < band_size( b ) ) {
                band_page[b] = next;
                focus_band = b;
                focus_header = false;
                focus_card = next;
                sync_cur_from_focus();
            }
        } else if( pending_card_band >= 0 && pending_card_slot >= 0 ) {
            const int b = pending_card_band;
            const int idx = band_page[b] + pending_card_slot;
            if( idx < band_size( b ) ) {
                focus_band = b;
                focus_header = false;
                focus_card = idx;
                sync_cur_from_focus();
                pick_prof();
            }
        }
        if( action == "DOWN" ) {
            if( focus_header && !band_collapsed[focus_band] && band_size( focus_band ) > 0 ) {
                focus_header = false;          // into this band's cards
            } else {
                focus_band = ( focus_band + 1 ) % nbands();
                focus_header = true;
                focus_card = 0;
            }
            sync_cur_from_focus();
        } else if( action == "UP" ) {
            if( !focus_header ) {
                focus_header = true;           // back onto this band's header
            } else {
                focus_band = ( focus_band + nbands() - 1 ) % nbands();
                // Land on the previous band's CARDS when it is open, so walking up is the
                // exact inverse of walking down.
                focus_header = band_collapsed[focus_band] || band_size( focus_band ) == 0;
                focus_card = focus_header ? 0 : band_size( focus_band ) - 1;
            }
            sync_cur_from_focus();
        } else if( action == "LEFT" ) {
            if( !focus_header && focus_card > 0 ) {
                focus_card--;
                sync_cur_from_focus();
            } else if( rml ) {
                // On a header there is no row to walk, so LEFT/RIGHT keep their old job of
                // scrolling the prose column.
                scroll_info( -1 );
            } else if( desc_offset > 0 ) {
                desc_offset--;
            }
        } else if( action == "RIGHT" ) {
            if( !focus_header && focus_card + 1 < band_size( focus_band ) ) {
                focus_card++;
                sync_cur_from_focus();
            } else if( rml ) {
                scroll_info( +1 );
            } else if( desc_offset < iheight ) {
                desc_offset++;
            }
        } else if( action == "RANDOMIZE" ) {
            cur_id = rng( 0, profs_length - 1 );
            // Follow the roll: it may land in a collapsed band, which then opens, because
            // rolling a profession the player cannot see would look like nothing happened.
            for( int b = 0; b < nbands(); ++b ) {
                const auto it = std::ranges::find( band_items[b], cur_id );
                if( it != band_items[b].end() ) {
                    focus_band = b;
                    focus_card = static_cast<int>( std::distance( band_items[b].begin(), it ) );
                    band_collapsed[b] = false;
                    focus_header = false;
                    band_page[b] = focus_card - focus_card % NC_PROF_PAGE;
                    break;
                }
            }
            sync_cur_from_focus();
        } else if( action == "CONFIRM" ) {
            if( focus_header ) {
                band_collapsed[focus_band] = !band_collapsed[focus_band];
                if( !band_collapsed[focus_band] && band_size( focus_band ) > 0 ) {
                    focus_header = false;
                    focus_card = band_page[focus_band];
                    sync_cur_from_focus();
                }
                continue;
            }
            pick_prof();
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

/// One pip of a row's level meter, the vocabulary the STATS cards established. Bools only: the
/// stylesheet owns what each tier looks like, and a pip is only ever consumed as a colour and a
/// height.
struct nc_skill_pip {
    bool on = false;      //< bought
    bool bonus = false;   //< granted by the profession after creation, so it cannot be sold back
    bool steep = false;   //< reaching this level costs two points or more
};

/// One row of a column. The cells are fixed-width by stylesheet, so cursor, meter, level, price and
/// name each form a column the eye can run down — which is the whole reason 28 skills are rows and
/// not cards: a column of meters reads as a competence profile, and "Marksmanship (2) (+1)" does
/// not. A heading occupies a row of the SAME height, which is what keeps the cursor-scroll
/// arithmetic exact; the markup drops its middle cells with `data-if`.
struct nc_skill_row {
    Rml::String cursor_rml;   //< ">" on the cursor row, else empty
    Rml::String lvl_rml;      //< "4", plus " +1" when the profession adds more
    Rml::String cost_rml;     //< price of the NEXT level, "[max]" at the cap
    Rml::String name_rml;
    Rml::Vector<nc_skill_pip> pips;
    bool can_dec = false;
    bool can_inc = false;
    bool header = false;
    bool selected = false;
};

/// One column: a category sigil, a heading, a count and its rows.
struct nc_skill_col {
    Rml::String name_rml;
    Rml::String count_rml;
    /// "none", NOT empty: `data-style-decorator` is applied on the first frame, before sync_rml has
    /// run, and an empty value becomes `decorator: ;`, which RmlUi logs every frame.
    Rml::String sigil_dec = "none";
    Rml::Vector<nc_skill_row> rows;
};

/// A label / value / sub-line triple in the detail panel.
struct nc_skill_fact {
    Rml::String label_rml;
    Rml::String value_rml;
    Rml::String sub_rml;
};

/// One dot of the aptitude matrix: a colour and nothing else. Geometry lives in
/// newchar_aptitude.h, and the sweep is carried in that colour's ALPHA — so the compositor does the
/// fading and there is no colour arithmetic here to get wrong. Transparent for a dot outside the
/// disc, which is what makes a square grid read as a circle.
struct nc_apt_dot {
    Rml::String col = "#00000000";
};
struct nc_apt_row {
    Rml::Vector<nc_apt_dot> cells;
};
/// A sector's line in the legend under the disc. An unlabelled radar is decoration.
struct nc_apt_sector {
    Rml::String dec = "none";
    Rml::String name_rml;
    Rml::String val_rml;
};

struct nc_skills_session {
    Rml::Vector<nc_skills_tab> tabs;
    nc_shell shell;
    Rml::String points_rml;
    Rml::String budget_rml;   //< "Skill points left: N", the reference's meta-bar readout
    /// Two columns, bound separately rather than as an array: each needs a stable element id so
    /// C++ can scroll its own cursor into view.
    nc_skill_col col0;
    nc_skill_col col1;
    Rml::String sel_name_rml;   //< ":: SKILL NAME" over the detail panel
    /// The art slot carries the CATEGORY's sigil, not the avatar: a skill does not change how the
    /// character looks, and the sigil ties the panel to its wedge on the matrix and to the column
    /// heading it sits under.
    bool has_art = false;
    Rml::String art_dec = "none";
    Rml::Vector<nc_skill_fact> facts;
    Rml::String desc_rml;     //< the skill's own description
    Rml::String detail_rml;   //< the recipes it unlocks; scrolls on its own
    Rml::String hint_rml;
    /// The matrix, rebuilt every redraw because it carries the sweep. DataViewStyle skips an
    /// unchanged property value (RmlUi DataViewDefault.cpp:168) and DataViewFor only creates
    /// elements when an array's SIZE changes — this one is always 13 by 13 — so a quiet tick costs
    /// the dots the beam is actually moving over and nothing else.
    Rml::Vector<nc_apt_row> apt;
    Rml::String apt_name_rml;
    Rml::String apt_count_rml;
    Rml::Vector<nc_apt_sector> apt_legend;
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
    // Pips before rows and rows before the column that holds them: a member cannot be registered
    // before its own type is.
    Rml::StructHandle<nc_skill_pip> ph = c.RegisterStruct<nc_skill_pip>();
    ph.RegisterMember( "on", &nc_skill_pip::on );
    ph.RegisterMember( "bonus", &nc_skill_pip::bonus );
    ph.RegisterMember( "steep", &nc_skill_pip::steep );
    c.RegisterArray<Rml::Vector<nc_skill_pip>>();
    Rml::StructHandle<nc_skill_row> rh = c.RegisterStruct<nc_skill_row>();
    rh.RegisterMember( "cursor_rml", &nc_skill_row::cursor_rml );
    rh.RegisterMember( "lvl_rml", &nc_skill_row::lvl_rml );
    rh.RegisterMember( "cost_rml", &nc_skill_row::cost_rml );
    rh.RegisterMember( "name_rml", &nc_skill_row::name_rml );
    rh.RegisterMember( "pips", &nc_skill_row::pips );
    rh.RegisterMember( "can_dec", &nc_skill_row::can_dec );
    rh.RegisterMember( "can_inc", &nc_skill_row::can_inc );
    rh.RegisterMember( "header", &nc_skill_row::header );
    rh.RegisterMember( "selected", &nc_skill_row::selected );
    c.RegisterArray<Rml::Vector<nc_skill_row>>();
    Rml::StructHandle<nc_skill_col> ch = c.RegisterStruct<nc_skill_col>();
    ch.RegisterMember( "name_rml", &nc_skill_col::name_rml );
    ch.RegisterMember( "count_rml", &nc_skill_col::count_rml );
    ch.RegisterMember( "sigil_dec", &nc_skill_col::sigil_dec );
    ch.RegisterMember( "rows", &nc_skill_col::rows );
    Rml::StructHandle<nc_skill_fact> fh = c.RegisterStruct<nc_skill_fact>();
    fh.RegisterMember( "label_rml", &nc_skill_fact::label_rml );
    fh.RegisterMember( "value_rml", &nc_skill_fact::value_rml );
    fh.RegisterMember( "sub_rml", &nc_skill_fact::sub_rml );
    c.RegisterArray<Rml::Vector<nc_skill_fact>>();
    Rml::StructHandle<nc_apt_dot> dh = c.RegisterStruct<nc_apt_dot>();
    dh.RegisterMember( "col", &nc_apt_dot::col );
    c.RegisterArray<Rml::Vector<nc_apt_dot>>();
    Rml::StructHandle<nc_apt_row> arh = c.RegisterStruct<nc_apt_row>();
    arh.RegisterMember( "cells", &nc_apt_row::cells );
    c.RegisterArray<Rml::Vector<nc_apt_row>>();
    Rml::StructHandle<nc_apt_sector> sh = c.RegisterStruct<nc_apt_sector>();
    sh.RegisterMember( "dec", &nc_apt_sector::dec );
    sh.RegisterMember( "name_rml", &nc_apt_sector::name_rml );
    sh.RegisterMember( "val_rml", &nc_apt_sector::val_rml );
    c.RegisterArray<Rml::Vector<nc_apt_sector>>();
    g_nc_skills_types_registered = true;
}

/// Sigil seed and colour for a skill display category. ONE source, read by the column heading's
/// glyph, the wedge that category owns on the aptitude matrix and the detail panel's art slot, so
/// the three cannot disagree about which category a skill is in.
///
/// The seed is an FNV-1a hash of the category id rather than std::hash, which is not required to
/// agree between platforms or runs — the same category must draw the same glyph everywhere. The
/// COLOUR comes from the category's position in the sorted list instead, because a hash would
/// happily hand two neighbouring wedges the same one, and telling the wedges apart is the entire
/// job of the palette.
struct nc_skill_cat_art {
    unsigned seed = 0;
    nc_color col = c_light_gray;
};

auto nc_skill_cat_seed( const std::string &id ) -> unsigned
{
    unsigned h = 2166136261U;
    for( const char ch : id ) {
        h ^= static_cast<unsigned char>( ch );
        h *= 16777619U;
    }
    return h;
}

auto nc_skill_cat_art_of( const skill_displayType_id &id, int order ) -> nc_skill_cat_art
{
    static const std::array<nc_color, 6> palette = {
        c_yellow, c_light_blue, c_light_green, c_light_cyan, c_pink, c_light_gray
    };
    return { .seed = nc_skill_cat_seed( id.str() ),
             .col = palette[static_cast<size_t>( std::max( 0, order ) ) % palette.size()] };
}

/// A category's heading. `display_string()` is already translated, so it is used verbatim; the
/// `weapon` skill declares `display_category: none`, which has no display string at all, and a
/// heading occupies a row either way — so it gets a name rather than an empty gap.
auto nc_skill_cat_name( const skill_displayType_id &id ) -> std::string
{
    const std::string name = SkillDisplayType::get_skill_type( id ).display_string();
    return name.empty() ? _( "Other skills" ) : name;
}

/// A themed colour at an explicit alpha. nc_color_to_hex yields "#rrggbbaa" and honours theme.json's
/// overrides, so keeping its rgb and replacing only the alpha is what lets the sweep brighten a dot
/// without blending anything.
auto nc_dot_col( const nc_color &col, int alpha ) -> Rml::String
{
    const std::string hex = nc_color_to_hex( col );
    return string_format( "%s%02x",
                          hex.size() >= 7 ? hex.substr( 0, 7 ) : std::string( "#a89984" ),
                          std::clamp( alpha, 0, 255 ) );
}

/// The recipes a skill unlocks, and how many. Mirrors the recipe-gathering block the pre-rework
/// screen ran inline. Brown for the current skill's own recipes, gray for recipes that merely
/// require it.
///
/// The COUNT is returned with the text rather than recomputed, so the RECIPES fact and the prose
/// pane cannot disagree. This walks all of recipe_dict, which is why the caller keeps it behind the
/// model-dirty gate and out of the animation tick.
struct nc_skill_recipes {
    std::string text;
    int count = 0;
};

auto nc_skill_recipe_list( avatar &u, const Skill *currentSkill,
                           const std::map<skill_id, int> &prof_skills ) -> nc_skill_recipes
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
    nc_skill_recipes out;
    for( auto &elem : recipes ) {
        out.count += static_cast<int>( elem.second.size() );
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
            out.text = colorize( rec_temp, c_brown ) + out.text;
        } else {
            out.text += ( out.text.empty() ? "" : "\n\n" ) +
                        colorize( "[" + elem.first + "]\n" + rec_temp, c_light_gray );
        }
    }
    return out;
}
} // namespace

tab_direction set_skills( avatar &u, points_left &points )
{
    ui_adaptor ui;
    catacurses::window w;
    const auto init_windows = [&]( ui_adaptor & ui ) {
        w = catacurses::newwin( TERMY, TERMX, point_zero );
        ui.position_from_window( w );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    // ONE flat list in display order; the columns are a VIEW over it, so every index-based lookup
    // stays independent of how the screen happens to be grouped.
    const std::vector<const Skill *> skills =
    Skill::get_skills_sorted_by( []( const Skill & a, const Skill & b ) {
        return localized_compare( std::make_pair( a.display_category(), a.name() ),
                                  std::make_pair( b.display_category(), b.name() ) );
    } );

    /// A display category and the skills in it, in that same order.
    struct cat_group {
        skill_displayType_id id;
        std::string name;
        nc_skill_cat_art art;
        std::vector<int> skills;   //< indices into `skills`
    };
    std::vector<cat_group> groups;
    for( int i = 0; i < static_cast<int>( skills.size() ); i++ ) {
        const skill_displayType_id &dt = skills[i]->display_category();
        if( groups.empty() || groups.back().id != dt ) {
            const int order = static_cast<int>( groups.size() );
            groups.push_back( { .id = dt,
                                .name = nc_skill_cat_name( dt ),
                                .art = nc_skill_cat_art_of( dt, order ),
                                .skills = {} } );
        }
        groups.back().skills.push_back( i );
    }

    const int num_skills = static_cast<int>( skills.size() );
    if( num_skills == 0 ) {
        return tab_direction::FORWARD;
    }

    // TWO columns, not the three TRAITS and BIONICS use: 28 skills plus their headings is 34 rows,
    // which fits two columns without scrolling, and a row here needs width for steppers, meter,
    // level, price and name.
    //
    // They are a WRAP of one ordered list rather than two independent ones, which is what lets
    // LEFT/RIGHT stay on the level — the primary action of this screen — instead of being spent on
    // changing column. UP/DOWN walk the concatenated row sequence, which is the order the eye reads
    // the two columns in anyway.
    static constexpr int ncols = 2;
    std::array<std::vector<int>, ncols> col_rows;    //< skill index, or -1 for a heading row
    std::array<std::vector<int>, ncols> col_head_of; //< the group each row belongs to
    {
        // Linear partition preserving the sorted order: keep filling column 0 while adding the next
        // category leaves it closer to half the rows than stopping would. Derived from the counts,
        // so a mod adding skills or a whole category redistributes with no edit here, and the split
        // is identical on every run.
        const int total_rows = num_skills + static_cast<int>( groups.size() );
        const int want = ( total_rows + ncols - 1 ) / ncols;
        int c = 0;
        int placed = 0;
        for( int gi = 0; gi < static_cast<int>( groups.size() ); gi++ ) {
            const int rows_here = 1 + static_cast<int>( groups[gi].skills.size() );
            if( c + 1 < ncols && placed > 0 &&
                placed + rows_here - want >= want - placed ) {
                c++;
                placed = 0;
            }
            col_rows[c].push_back( -1 );
            col_head_of[c].push_back( gi );
            for( const int si : groups[gi].skills ) {
                col_rows[c].push_back( si );
                col_head_of[c].push_back( gi );
            }
            placed += rows_here;
        }
    }

    int cur_col = 0;
    std::array<int, ncols> cur_row = {};

    const auto col_len = [&]( int c ) {
        return static_cast<int>( col_rows[c].size() );
    };
    /// The skill on a given row, or -1 for a heading or an out-of-range row.
    const auto skill_at = [&]( int c, int r ) {
        return ( r >= 0 && r < col_len( c ) ) ? col_rows[c][r] : -1;
    };
    const auto total_rows = [&]() {
        return col_len( 0 ) + col_len( 1 );
    };
    /// Position in the flat visual order: column 0 top to bottom, then column 1.
    const auto visual_of = [&]( int c, int r ) {
        return c == 0 ? r : col_len( 0 ) + r;
    };
    const auto place_visual = [&]( int v ) {
        if( v < col_len( 0 ) ) {
            cur_col = 0;
            cur_row[0] = v;
        } else {
            cur_col = 1;
            cur_row[1] = v - col_len( 0 );
        }
    };
    /// Step the cursor along that flat order, skipping headings and crossing the column boundary.
    /// Bounded by the row count, so a list of nothing but headings cannot spin here.
    const auto move_cursor = [&]( int dir ) {
        const int n = total_rows();
        if( n <= 0 ) {
            return;
        }
        int v = visual_of( cur_col, cur_row[cur_col] );
        for( int guard = 0; guard < n; guard++ ) {
            v = ( v + dir + n ) % n;
            place_visual( v );
            if( skill_at( cur_col, cur_row[cur_col] ) >= 0 ) {
                return;
            }
        }
    };
    // Column 0 opens on a heading, so step off it before anything reads the cursor.
    move_cursor( 1 );

    input_context ctxt( "NEW_CHAR_SKILLS" );
    ctxt.register_cardinal();
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "SCROLL_DOWN" );
    ctxt.register_action( "SCROLL_UP" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "RANDOMIZE" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "QUIT" );
    // All three are required for a click to reach this loop at all. MOUSE_LEFT binds to SELECT on
    // mouse DOWN while RmlUi fires `click` — and therefore the callbacks — on mouse UP; that UP
    // resolves to CATA_ERROR, and input.cpp:894-897 `continue`s on an unrecognised MOUSE event
    // WITHOUT returning. COORDINATE sets handling_coordinate_input, which skips that early
    // `continue`; ANY_INPUT makes the fall-through at :912 return rather than loop again. Either
    // alone still parks the loop, and a parked loop clears the intent a click recorded. See
    // plans/charcreation-bionics-chassis.md.
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "COORDINATE" );
    ctxt.register_action( "ANY_INPUT" );

    std::map<skill_id, int> prof_skills;
    const auto &pskills = u.prof->skills();
    std::copy( pskills.begin(), pskills.end(),
               std::inserter( prof_skills, prof_skills.begin() ) );
    /// What the profession will add to a skill after creation. Not part of get_skill_level yet:
    /// avatar::add_profession_items installs those AFTER the wizard, so the screen has to say so
    /// itself — the same compensation the BIONICS chassis makes for profession CBMs.
    const auto prof_bonus_of = [&]( const skill_id & id ) {
        const auto it = prof_skills.find( id );
        return it == prof_skills.end() ? 0 : it->second;
    };

    auto data = std::make_unique<nc_skills_session>();
    rml_doc rml;
    // Set by the arrow click callbacks, consumed by the input loop below. Not a tab_direction: it is
    // translated into an action string so the existing keyboard handling stays the single place
    // navigation is decided.
    int nc_nav = 0;
    // Click intent, applied ONCE per input cycle. `data-event-*` installs a listener per generated
    // element and a `data-for` regeneration adds another without removing the old, so a callback
    // that mutated directly would run an unbounded number of times per click — measured at 15 on
    // the SCENARIO tab. See plans/charcreation-scenario-tree.md.
    int pending_row_col = -1;
    int pending_row = -1;
    int pending_step_col = -1;
    int pending_step_row = -1;
    int pending_step_dir = 0;

    /// Raise or lower one skill. The ONE place the level and the point pool move together, so the
    /// keyboard, CONFIRM and the two steppers cannot drift apart. The balance rules are the ones
    /// this step has always had: buying the first level grants two, and selling back from 2 forfeits
    /// the free one.
    const auto adjust_skill = [&]( int flat_idx, int dir ) {
        if( flat_idx < 0 || flat_idx >= num_skills ) {
            return;
        }
        const skill_id id = skills[flat_idx]->ident();
        const int level = u.get_skill_level( id );
        if( dir < 0 ) {
            if( level > 0 ) {
                u.mod_skill_level( id, level == 2 ? -2 : -1 );
                // Done *after* the decrementing to get the original cost for incrementing back.
                points.skill_points += skill_increment_cost( u, id );
            }
        } else if( level < MAX_SKILL ) {
            points.skill_points -= skill_increment_cost( u, id );
            u.mod_skill_level( id, level == 0 ? +2 : +1 );
        }
    };

    // ── APTITUDE MATRIX ───────────────────────────────────────────────────────
    //
    // A 13x13 dot radar beside the lists: one filled SECTOR per display category, its depth that
    // category's share of the strongest, three rings for scale, and a beam sweeping clockwise from
    // up. Geometry
    // is newchar_aptitude.h; this only decides which layer claims each dot and at what alpha.
    //
    // Rebuilt every animation tick, which is why it is separate from the model sync — and why the
    // sector depths are recomputed here rather than cached: 28 get_skill_level calls cost nothing,
    // and a wedge that deepened the instant a level was bought is the point of putting it beside the
    // list.
    const auto anim_start = std::chrono::steady_clock::now();
    const auto sync_apt = [&]() {
        if( !data->handle ) {
            return;
        }
        // Wall clock, not a frame counter: the sweep must not speed up because the player is holding
        // a key down, and must not stall while they are not.
        const float secs = std::chrono::duration<float>(
                               std::chrono::steady_clock::now() - anim_start ).count();
        const float beam = nc_apt::beam_at( secs );

        const int nsectors = static_cast<int>( groups.size() );
        struct sector {
            float reach = 0.0F;
            nc_color col = c_light_gray;
            int levels = 0;
        };
        std::vector<sector> sectors;
        sectors.reserve( groups.size() );
        int invested = 0;
        int strongest = 0;
        for( int gi = 0; gi < nsectors; gi++ ) {
            const cat_group &g = groups[gi];
            int total = 0;
            for( const int si : g.skills ) {
                const skill_id id = skills[si]->ident();
                // The profession's grant counts: this describes the character being built, not the
                // half-built object in memory. Same rule the chassis applies to profession CBMs.
                total += u.get_skill_level( id ) + prof_bonus_of( id );
                invested += u.get_skill_level( id );
            }
            strongest = std::max( strongest, total );
            sectors.push_back( { .col = g.art.col, .levels = total } );
        }
        // Sectors are scaled against the STRONGEST category, not against every skill at MAX_SKILL.
        // Measured: a finished creation buys a handful of levels, so a category averages well under
        // one level of ten — against the theoretical cap every wedge was under a cell deep and the
        // radar was a dot with a legend. What the player wants from it is comparative anyway: where
        // the competence sits. The absolute count stays in the legend beside each name.
        //
        // The floor stops that relative scale from over-claiming: with it, a category needs half of
        // one skill's range invested before its wedge reaches the rim, so a single level fills a
        // fifth of the radius rather than the whole disc.
        const float denom = static_cast<float>( std::max( strongest, MAX_SKILL / 2 ) );
        for( sector &s : sectors ) {
            s.reach = static_cast<float>( s.levels ) / denom * nc_apt::disc_radius();
        }
        const int sel_flat = skill_at( cur_col, cur_row[cur_col] );
        const int sel_group = sel_flat >= 0 ? col_head_of[cur_col][cur_row[cur_col]] : -1;

        data->apt.clear();
        for( int row = 0; row < nc_apt::grid; row++ ) {
            nc_apt_row ar;
            for( int col = 0; col < nc_apt::grid; col++ ) {
                const nc_apt::vec off = nc_apt::offset_of( col, row );
                if( !nc_apt::inside( off ) ) {
                    // Transparent, and the ONLY reason a 13x13 square reads as a circle.
                    ar.cells.emplace_back();
                    continue;
                }
                const float r = nc_apt::radius_of( off );
                const float bearing = nc_apt::angle_of( off );
                // Layers, weakest first: field, ring, then the wedge that owns this bearing. Only
                // ONE sector can, since they partition the turn, so there is no precedence to
                // decide — the cursor's category is merely drawn brighter.
                nc_color col_of = c_dark_gray;
                int base = 0x22;
                if( nc_apt::on_ring( r ) ) {
                    col_of = c_brown;
                    base = 0x4d;
                }
                const int owner = nc_apt::sector_of( bearing, nsectors );
                if( owner < nsectors && r <= sectors[owner].reach ) {
                    col_of = sectors[owner].col;
                    base = owner == sel_group ? 0xff : 0x99;
                }
                if( r < 0.5F ) {
                    col_of = c_yellow;
                    base = 0xcc;
                }
                // The sweep raises a dot's own alpha toward opaque instead of blending a second
                // colour into it. On a dark ground that is the compositor's job, and it means the
                // fade cannot be wrong in a way a retheme would hide.
                const float g = nc_apt::glow( beam, nc_apt::angle_of( off ) );
                const int alpha = base +
                                  static_cast<int>( std::lround( g * static_cast<float>( 255 - base ) ) );
                ar.cells.push_back( { .col = nc_dot_col( col_of, alpha ) } );
            }
            data->apt.push_back( ar );
        }

        data->apt_legend.clear();
        for( int gi = 0; gi < nsectors; gi++ ) {
            data->apt_legend.push_back( {
                .dec = nc_icon_dec_col( groups[gi].art.seed, 12, groups[gi].art.col ),
                .name_rml = cata_text_to_rml( colorize( groups[gi].name,
                                                        gi == sel_group ? c_white : c_light_gray ) ),
                // The count, not a percentage: the wedges are already comparative, so the legend is
                // where the absolute number belongs. A percentage of every skill at MAX_SKILL reads
                // as 3% for a perfectly ordinary character, which looks like a fault.
                .val_rml = cata_text_to_rml( colorize( string_format( "%d", sectors[gi].levels ),
                                                       groups[gi].art.col ) ) } );
        }
        data->apt_name_rml = cata_text_to_rml( colorize( _( "Aptitude" ), c_light_gray ) );
        data->apt_count_rml = cata_text_to_rml( colorize( string_format( "%d", invested ),
                                                c_dark_gray ) );
        data->handle.DirtyVariable( "apt" );
        data->handle.DirtyVariable( "apt_legend" );
        data->handle.DirtyVariable( "apt_name_rml" );
        data->handle.DirtyVariable( "apt_count_rml" );
    };

    const auto sync_rml = [&]() {
        if( !data->handle ) {
            return;
        }
        data->tabs = build_nc_char_tabs<nc_skills_tab>( 6 );  // SKILLS tab active
        data->shell = fill_nc_shell( 6, ctxt );
        data->points_rml = cata_text_to_rml( nc_points_line( points ) );
        data->budget_rml = cata_text_to_rml( string_format(
                _( "<color_dark_gray>Skill points left:</color> <color_white>%d</color>" ),
                points.skill_points_left() ) );
        data->hint_rml = cata_text_to_rml( string_format(
                                               _( "<color_light_green>%s</color> raise or lower · <color_light_green>%s</color> scroll recipes" ),
                                               _( "left/right" ), ctxt.get_desc( "SCROLL_DOWN", 1 ) ) );

        // One row. Cells are fixed-width by stylesheet, so cursor, meter, level, price and name each
        // form a column the eye can run down.
        const auto build_row = [&]( int flat_idx, bool is_cursor ) {
            const Skill *sk = skills[flat_idx];
            const skill_id id = sk->ident();
            const int level = u.get_skill_level( id );
            const int bonus = prof_bonus_of( id );
            const int cost = skill_increment_cost( u, id );
            nc_skill_row r;
            r.cursor_rml = cata_text_to_rml( is_cursor ? colorize( ">", c_yellow ) : std::string() );
            for( int i = 1; i <= MAX_SKILL; i++ ) {
                r.pips.push_back( {
                    .on = i <= level,
                    .bonus = i > level && i <= level + bonus,
                    // Reaching level i costs max(1, i/2), so pip 4 is where a level stops being a
                    // single point. The price curve is drawn rather than described.
                    .steep = i <= level && i / 2 >= 2 } );
            }
            std::string lvl = colorize( string_format( "%d", level ),
                                        level > 0 ? COL_SKILL_USED : c_dark_gray );
            if( bonus > 0 ) {
                // Same blue the bonus pips are painted, so the number and the meter agree about
                // which levels the player did not buy.
                lvl += colorize( string_format( " +%d", bonus ), c_light_blue );
            }
            r.lvl_rml = cata_text_to_rml( lvl );
            r.cost_rml = cata_text_to_rml( level >= MAX_SKILL
                                           ? colorize( string_format( "[%s]", _( "max" ) ), c_dark_gray )
                                           : colorize( string_format( "[%2d]", cost ),
                                               points.skill_points_left() >= cost ? COL_SKILL_USED : c_light_red ) );
            r.name_rml = cata_text_to_rml( colorize( sk->name(),
                                           level > 0 ? COL_SKILL_USED : c_light_gray ) );
            r.can_dec = level > 0;
            r.can_inc = level < MAX_SKILL;
            r.selected = is_cursor;
            return r;
        };

        nc_skill_col *cols[ncols] = { &data->col0, &data->col1 };
        for( int c = 0; c < ncols; c++ ) {
            nc_skill_col &dc = *cols[c];
            dc.rows.clear();
            int items = 0;
            for( int r = 0; r < col_len( c ); r++ ) {
                const int flat = col_rows[c][r];
                if( flat < 0 ) {
                    // A heading is a row of the same height as an item, which is what keeps the
                    // scroll arithmetic exact; weight and tracking carry the hierarchy instead.
                    nc_skill_row hr;
                    hr.header = true;
                    hr.name_rml = cata_text_to_rml( colorize( groups[col_head_of[c][r]].name,
                                                    c_yellow ) );
                    dc.rows.push_back( hr );
                    continue;
                }
                items++;
                dc.rows.push_back( build_row( flat, c == cur_col && r == cur_row[c] ) );
            }
            // The two columns are a WRAP of one list, so neither of them names a grouping — the
            // category headings inside the list do that. They get a newspaper caption instead, and
            // the sigil is a "you are here": the cursor's own category, on the column holding it,
            // matching the sigil the detail panel draws.
            dc.name_rml = cata_text_to_rml( colorize(
                                                c == 0 ? _( "Skills" ) : _( "Continued" ),
                                                c == cur_col ? c_white : c_light_gray ) );
            dc.count_rml = cata_text_to_rml( colorize( string_format( "%d", items ), c_dark_gray ) );
            dc.sigil_dec = "none";
            if( c == cur_col && col_len( c ) > 0 ) {
                const cat_group &g = groups[col_head_of[c][cur_row[c]]];
                dc.sigil_dec = nc_icon_dec_col( g.art.seed, 14, g.art.col );
            }
        }

        // Detail panel for the skill under the cursor.
        data->facts.clear();
        const int sel_flat = skill_at( cur_col, cur_row[cur_col] );
        if( sel_flat >= 0 ) {
            const Skill *sk = skills[sel_flat];
            const skill_id id = sk->ident();
            const int level = u.get_skill_level( id );
            const int bonus = prof_bonus_of( id );
            const int cost = skill_increment_cost( u, id );
            const cat_group &g = groups[col_head_of[cur_col][cur_row[cur_col]]];
            // ":: NAME" — uppercasing is left to the stylesheet, so no locale gets a hand-rolled
            // case conversion.
            data->sel_name_rml = cata_text_to_rml( colorize( string_format( ":: %s", sk->name() ),
                                                   c_white ) );
            data->has_art = true;
            data->art_dec = nc_icon_dec_col( g.art.seed, 128, g.art.col );
            const auto add_fact = [&]( const std::string & label, const std::string & value,
            const nc_color & col, const std::string & sub = std::string() ) {
                data->facts.push_back( {
                    .label_rml = cata_text_to_rml( label ),
                    .value_rml = cata_text_to_rml( colorize( value, col ) ),
                    .sub_rml = cata_text_to_rml( sub ) } );
            };
            add_fact( _( "Level" ), string_format( "%d", level ),
                      level > 0 ? COL_SKILL_USED : c_light_gray,
                      bonus > 0 ? string_format( vgettext( "Your profession adds %d more level.",
                                                 "Your profession adds %d more levels.", bonus ), bonus )
                      : std::string() );
            if( level >= MAX_SKILL ) {
                add_fact( _( "Next level" ), _( "At maximum" ), c_dark_gray );
            } else {
                add_fact( _( "Next level" ),
                          string_format( vgettext( "%d point", "%d points", cost ), cost ),
                          points.skill_points_left() >= cost ? COL_SKILL_USED : c_light_red,
                          // The one balance rule this screen has that nothing on it used to state.
                          level == 0 ? _( "The first point buys two levels." ) : std::string() );
            }
            int gtotal = 0;
            for( const int si : g.skills ) {
                gtotal += u.get_skill_level( skills[si]->ident() ) +
                          prof_bonus_of( skills[si]->ident() );
            }
            add_fact( _( "Category" ), g.name, g.art.col,
                      // The same number the matrix draws this wedge from, and the same one its
                      // legend prints, so the panel and the disc cannot disagree.
                      string_format( vgettext( "%d level across %d skills",
                                               "%d levels across %d skills", gtotal ),
                                     gtotal, static_cast<int>( g.skills.size() ) ) );
            const nc_skill_recipes rec = nc_skill_recipe_list( u, sk, prof_skills );
            add_fact( _( "Unlocks" ),
                      string_format( vgettext( "%d recipe", "%d recipes", rec.count ), rec.count ),
                      rec.count > 0 ? c_light_gray : c_dark_gray );
            data->desc_rml = cata_text_to_rml( sk->description() );
            data->detail_rml = cata_text_to_rml( rec.text );
        } else {
            data->sel_name_rml.clear();
            data->desc_rml.clear();
            data->detail_rml.clear();
            data->has_art = false;
            data->art_dec = "none";
        }

        data->handle.DirtyVariable( "tabs" );
        dirty_nc_shell( data->handle );
        data->handle.DirtyVariable( "points_rml" );
        data->handle.DirtyVariable( "budget_rml" );
        data->handle.DirtyVariable( "col0" );
        data->handle.DirtyVariable( "col1" );
        data->handle.DirtyVariable( "sel_name_rml" );
        data->handle.DirtyVariable( "has_art" );
        data->handle.DirtyVariable( "art_dec" );
        data->handle.DirtyVariable( "facts" );
        data->handle.DirtyVariable( "desc_rml" );
        data->handle.DirtyVariable( "detail_rml" );
        data->handle.DirtyVariable( "hint_rml" );
    };

    rml.open( newcharacter_rmlui_enabled(), "newcharskills", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        register_nc_skills_rml_types( c );
        c.Bind( "tabs", &data->tabs );
        bind_nc_shell( c, data->shell );
        // Arrow clicks are translated into the SAME action strings the keyboard produces, so each
        // step's existing PREV_TAB/NEXT_TAB handling is reused unchanged.
        c.BindEventCallback( "on_prev",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = -1; } );
        c.BindEventCallback( "on_next",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) { nc_nav = 1; } );
        c.Bind( "points_rml", &data->points_rml );
        c.Bind( "budget_rml", &data->budget_rml );
        c.Bind( "col0", &data->col0 );
        c.Bind( "col1", &data->col1 );
        // The matrix. Easy to forget, and it fails SILENTLY: `data-for` over an unbound name renders
        // nothing at all, so the heading would appear over an empty square while sync_apt happily
        // filled and dirtied the array every frame.
        c.Bind( "apt", &data->apt );
        c.Bind( "apt_legend", &data->apt_legend );
        c.Bind( "apt_name_rml", &data->apt_name_rml );
        c.Bind( "apt_count_rml", &data->apt_count_rml );
        c.Bind( "sel_name_rml", &data->sel_name_rml );
        c.Bind( "has_art", &data->has_art );
        c.Bind( "art_dec", &data->art_dec );
        c.Bind( "facts", &data->facts );
        c.Bind( "desc_rml", &data->desc_rml );
        c.Bind( "detail_rml", &data->detail_rml );
        c.Bind( "hint_rml", &data->hint_rml );
        // Click callbacks RECORD INTENT and mutate nothing — see the comment on pending_row_col.
        c.BindEventCallback( "on_row",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int col = -1;
            int row = -1;
            if( args.size() >= 2 ) {
                args[0].GetInto( col );
                args[1].GetInto( row );
            }
            if( col >= 0 && col < ncols && row >= 0 ) {
                pending_row_col = col;
                pending_row = row;
            }
        } );
        // The steppers, not the row, are what change a level: they flank the meter so the gesture is
        // spatially the same as the LEFT/RIGHT keys, and reading a skill is never the same gesture
        // as buying one. Written out twice rather than from a factory lambda: a nested lambda
        // capturing a reference capture of its enclosing closure outlives that closure here, and the
        // document holds these callbacks for its whole life.
        c.BindEventCallback( "on_dec",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int col = -1;
            int row = -1;
            if( args.size() >= 2 ) {
                args[0].GetInto( col );
                args[1].GetInto( row );
            }
            if( col >= 0 && col < ncols && row >= 0 ) {
                pending_step_col = col;
                pending_step_row = row;
                pending_step_dir = -1;
            }
        } );
        c.BindEventCallback( "on_inc",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int col = -1;
            int row = -1;
            if( args.size() >= 2 ) {
                args[0].GetInto( col );
                args[1].GetInto( row );
            }
            if( col >= 0 && col < ncols && row >= 0 ) {
                pending_step_col = col;
                pending_step_row = row;
                pending_step_dir = 1;
            }
        } );
        data->handle = c.GetModelHandle();
    } );
    if( rml_doc_unavailable( rml, _( "Character creation (SKILLS tab)" ) ) ) {
        return tab_direction::QUIT;
    }

    // The model is rebuilt only when something changed; the matrix every frame. Without this split a
    // quiet tick would re-colour 34 rows, re-scan all of recipe_dict and regenerate every pip to
    // move the beam one step.
    bool model_dirty = true;
    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            if( model_dirty ) {
                sync_rml();
                model_dirty = false;
            }
            sync_apt();
            return;
        }
    } );

    // Keeps the cursor row on screen. Every row is the SAME height by stylesheet rule — headings
    // included — so row height is scroll_height / row_count exactly, with no DOM child indexing,
    // which `data-for` makes unreliable anyway.
    const auto scroll_col_to_cursor = [&]() {
        if( !rml ) {
            return;
        }
        const int rows = col_len( cur_col );
        if( rows <= 0 ) {
            return;
        }
        Rml::Element *e = rml.document()->GetElementById( string_format( "nc-col%d", cur_col ) );
        if( e == nullptr ) {
            return;
        }
        const float page = e->GetClientHeight();
        const float total = e->GetScrollHeight();
        const float row_h = total / static_cast<float>( rows );
        const float want = row_h * static_cast<float>( cur_row[cur_col] ) - page * 0.5f;
        e->SetScrollTop( std::clamp( want, 0.0f, std::max( 0.0f, total - page ) ) );
    };
    // SCROLL_UP/DOWN scroll the recipe pane, which is the one thing on this screen that can be
    // taller than the space it has.
    const auto scroll_detail = [&]( int dir ) {
        if( !rml ) {
            return;
        }
        if( Rml::Element *e = rml.document()->GetElementById( "nc-skill-detail" ) ) {
            const float page = e->GetClientHeight();
            const float maxtop = std::max( 0.0f, e->GetScrollHeight() - page );
            e->SetScrollTop( std::clamp( e->GetScrollTop() + dir * page * 0.15f, 0.0f, maxtop ) );
        }
    };

    do {
        ui_manager::redraw();
        nc_nav = 0;
        pending_row_col = -1;
        pending_row = -1;
        pending_step_col = -1;
        pending_step_row = -1;
        pending_step_dir = 0;
        std::string action = ctxt.handle_input();
        if( nc_nav != 0 ) {
            action = nc_nav < 0 ? "PREV_TAB" : "NEXT_TAB";
        }
        // Rebuild the model on the next redraw after anything that can have changed it. The click
        // intents MUST be part of this test, not just `action`: cata maps MOUSE_LEFT to SELECT on
        // mouse DOWN while RmlUi fires `click` on mouse UP, so the iteration carrying a click's
        // intent is usually an idle one. TIMEOUT and ANY_INPUT are idle as far as the MODEL goes —
        // ANY_INPUT is what every pointer motion returns — and the matrix updates either way,
        // because it is outside this gate.
        if( ( action != "TIMEOUT" && action != "ANY_INPUT" ) ||
            pending_row >= 0 || pending_step_row >= 0 ) {
            model_dirty = true;
        }

        // Apply click intent exactly once, however many times the callback ran.
        if( pending_row_col >= 0 && pending_row >= 0 && pending_row < col_len( pending_row_col ) &&
            skill_at( pending_row_col, pending_row ) >= 0 ) {
            cur_col = pending_row_col;
            cur_row[cur_col] = pending_row;
        }
        // The stepper last, so the cursor has already moved to the row being acted on.
        if( pending_step_col >= 0 && pending_step_row >= 0 &&
            pending_step_row < col_len( pending_step_col ) && pending_step_dir != 0 ) {
            const int flat = skill_at( pending_step_col, pending_step_row );
            if( flat >= 0 ) {
                cur_col = pending_step_col;
                cur_row[cur_col] = pending_step_row;
                adjust_skill( flat, pending_step_dir );
            }
        }

        if( action == "DOWN" ) {
            move_cursor( 1 );
            scroll_col_to_cursor();
        } else if( action == "UP" ) {
            move_cursor( -1 );
            scroll_col_to_cursor();
        } else if( action == "RANDOMIZE" ) {
            const int n = total_rows();
            if( n > 0 ) {
                place_visual( rng( 0, n - 1 ) );
                // Land on a skill, never on a heading.
                if( skill_at( cur_col, cur_row[cur_col] ) < 0 ) {
                    move_cursor( 1 );
                }
                scroll_col_to_cursor();
            }
        } else if( action == "LEFT" ) {
            adjust_skill( skill_at( cur_col, cur_row[cur_col] ), -1 );
        } else if( action == "RIGHT" || action == "CONFIRM" ) {
            adjust_skill( skill_at( cur_col, cur_row[cur_col] ), 1 );
        } else if( action == "SCROLL_DOWN" ) {
            scroll_detail( +1 );
        } else if( action == "SCROLL_UP" ) {
            scroll_detail( -1 );
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
/// Which band a scenario belongs to. Derived, so mods group correctly without any JSON.
enum class nc_scen_group : int {
    basic = 0,      //< costs nothing or costs you points: an ordinary start
    advanced = 1,   //< grants points, i.e. it is harder in exchange
    challenge = 2,  //< carries the CHALLENGE flag
    count = 3,
};

/// Classify by the CHALLENGE FLAG and the point cost — never by the `"Challenge - "` name
/// prefix. The prefix is translated, so prefix-matching silently collapses to one group
/// in every non-English locale, and mods that do not copy the naming convention would be
/// misfiled. The flag is real data: 28 definitions carry it.
auto nc_classify_scen( const scenario &s ) -> nc_scen_group
{
    if( s.has_flag( "CHALLENGE" ) ) {
    return nc_scen_group::challenge;
}
// A negative cost GRANTS points, which the game only does to compensate for a harder
// start — so "grants points" is the honest signal for "advanced".
return s.point_cost() < 0 ? nc_scen_group::advanced : nc_scen_group::basic;
}

/// One flag worth showing as a glyph, with the colour carrying its valence so the strip
/// reads before any individual glyph has been learned.
///
/// SINGLE SOURCE OF TRUTH for the flag vocabulary: the card strip, the selected scenario's
/// chips and the legend all derive from this table. They used to disagree — the strip drew
/// from here while the info text came from a separate, narrower if-chain, so a card could
/// show a sigil (CITY_START) that nothing on the screen explained.
struct nc_scen_flag_icon {
const char *flag;
unsigned seed;
nc_color col;
/// Terse form for the legend. Thirteen of these share one row.
const char *label;
/// Full form for the selected scenario's chip, where there is room to be explicit.
const char *desc;
};

/// Danger in red, bodily state in green, circumstance in grey/blue, season in yellow.
///
/// Seeds must all differ — the generator keys the glyph on the seed, so two flags sharing
/// one would draw the same shape and the strip would stop distinguishing them. They are
/// otherwise arbitrary.
const std::vector<nc_scen_flag_icon> &nc_scen_flag_icons()
{
    static const std::vector<nc_scen_flag_icon> icons = {
        { "FIRE_START", 0x4649, c_red, translate_marker( "Starts on fire" ), translate_marker( "Fire nearby" ) },
        { "SUR_START", 0x5352, c_red, translate_marker( "Surrounded" ), translate_marker( "Zombies nearby" ) },
        { "HELI_CRASH", 0x4843, c_red, translate_marker( "Crash injuries" ), translate_marker( "Various limb wounds" ) },
        { "INFECTED", 0x494E, c_green, translate_marker( "Infected wound" ), translate_marker( "Infected player" ) },
        { "BAD_DAY", 0x4244, c_green, translate_marker( "Bad day" ), translate_marker( "Drunk and sick player" ) },
        { "CITY_START", 0x4349, c_light_gray, translate_marker( "Starts in a city" ), translate_marker( "Starts inside a city" ) },
        { "BORDERED", 0x424F, c_light_gray, translate_marker( "Walled in" ), translate_marker( "Bordered by an immense wall" ) },
        { "LONE_START", 0x4C4F, c_light_blue, translate_marker( "No starting NPC" ), translate_marker( "No starting NPC" ) },
        { "SPR_START", 0x5350, c_yellow, translate_marker( "Spring" ), translate_marker( "Spring start" ) },
        { "SUM_START", 0x5355, c_yellow, translate_marker( "Summer" ), translate_marker( "Summer start" ) },
        { "AUT_START", 0x4155, c_yellow, translate_marker( "Autumn" ), translate_marker( "Autumn start" ) },
        { "WIN_START", 0x5749, c_yellow, translate_marker( "Winter" ), translate_marker( "Winter start" ) },
        { "SUM_ADV_START", 0x5341, c_yellow, translate_marker( "Late summer" ), translate_marker( "Next summer start" ) },
    };
    return icons;
}

struct nc_scen_icon {
    Rml::String dec;
};

/// RCSS `decorator` showing the scenario's start-location art, or an empty string when the
/// tileset has no sprite for it.
///
/// Bespoke per-scenario art does not exist yet, so the start location's overmap sprite is
/// the closest honest stand-in for "where this run begins".
auto nc_scen_art_dec( const scenario &s ) -> std::string
{
    const start_location_id loc = s.start_location();
    if( !loc.is_valid() ) {
        return {};
    }
    const auto target = loc->first_target();
    if( !target ) {
        return {};
    }
    // Overmap sprites are keyed by the plain terrain id.
    return nc_tile_sprite_dec( target->first, C_OVERMAP_TERRAIN );
}

/// A sigil paired with words. Used twice: the legend beneath the tree (which states the
/// vocabulary, since the card strips are unreadable without it) and the selected
/// scenario's flag chips (which restate its own sigils in full). Sharing the type is what
/// ties strip, chip and legend to one glyph per flag.
struct nc_scen_glyph {
    Rml::String dec;
    Rml::String label_rml;
};

/// One scenario card.
struct nc_scen_row {
    Rml::String text_rml;
    Rml::String cost_rml;             //< point cost, coloured by direction
    Rml::Vector<nc_scen_icon> icons;  //< flag glyph strip
    bool selected = false;            //< cursor is on this card
    bool chosen = false;              //< this is the scenario in force
    bool unavailable = false;         //< CITY_START while cities are disabled
};

/// One group: a header plus a CAROUSEL page of its cards.
struct nc_scen_band {
    Rml::String name_rml;
    Rml::String count_rml;
    Rml::String marker_rml;          //< disclosure glyph
    bool collapsed = false;
    bool focused = false;            //< cursor is on this header
    /// This band owns the floaty info panel: it is open and holds the cursor. Only one
    /// band can, so the panel reads as belonging to the category being browsed.
    bool has_info = false;
    bool has_prev_page = false;
    bool has_next_page = false;
    Rml::Vector<nc_scen_row> rows;   //< only the visible page
};

struct nc_scen_session {
    Rml::Vector<nc_scen_glyph> legend;
    /// Facts about the selected scenario, one binding per field. Previously a single
    /// pre-wrapped string with embedded headers and blank-line separators, which no
    /// stylesheet could give hierarchy to and which spent a third of its height on
    /// whitespace. Split so each field carries its own size, weight and hue.
    Rml::String loc_rml;
    Rml::String loc_sub_rml;
    Rml::String prof_rml;
    Rml::String prof_sub_rml;
    Rml::String veh_rml;
    Rml::String art_dec;
    Rml::Vector<nc_scen_glyph> chips;
    Rml::Vector<nc_scen_tab> tabs;
    nc_shell shell;
    Rml::String points_rml;
    Rml::String cost_rml;
    Rml::Vector<nc_scen_band> bands;
    /// The expand/collapse-all control above the tree. The marker mirrors the band headers'
    /// +/- vocabulary so it reads as the same family, and the label carries the real shortcut
    /// from input_context::get_desc so it stays correct after a rebind.
    Rml::String all_marker_rml;
    Rml::String all_label_rml;
    Rml::String desc_rml;
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
    Rml::StructHandle<nc_scen_icon> ih = c.RegisterStruct<nc_scen_icon>();
    ih.RegisterMember( "dec", &nc_scen_icon::dec );
    c.RegisterArray<Rml::Vector<nc_scen_icon>>();
    Rml::StructHandle<nc_scen_row> rh = c.RegisterStruct<nc_scen_row>();
    rh.RegisterMember( "text_rml", &nc_scen_row::text_rml );
    rh.RegisterMember( "cost_rml", &nc_scen_row::cost_rml );
    rh.RegisterMember( "icons", &nc_scen_row::icons );
    rh.RegisterMember( "selected", &nc_scen_row::selected );
    rh.RegisterMember( "chosen", &nc_scen_row::chosen );
    rh.RegisterMember( "unavailable", &nc_scen_row::unavailable );
    c.RegisterArray<Rml::Vector<nc_scen_row>>();
    Rml::StructHandle<nc_scen_glyph> lh = c.RegisterStruct<nc_scen_glyph>();
    lh.RegisterMember( "dec", &nc_scen_glyph::dec );
    lh.RegisterMember( "label_rml", &nc_scen_glyph::label_rml );
    // One array registration covers both the legend and the chips — same element type.
    c.RegisterArray<Rml::Vector<nc_scen_glyph>>();
    Rml::StructHandle<nc_scen_band> bh = c.RegisterStruct<nc_scen_band>();
    bh.RegisterMember( "name_rml", &nc_scen_band::name_rml );
    bh.RegisterMember( "count_rml", &nc_scen_band::count_rml );
    bh.RegisterMember( "marker_rml", &nc_scen_band::marker_rml );
    bh.RegisterMember( "collapsed", &nc_scen_band::collapsed );
    bh.RegisterMember( "focused", &nc_scen_band::focused );
    bh.RegisterMember( "has_info", &nc_scen_band::has_info );
    bh.RegisterMember( "has_prev_page", &nc_scen_band::has_prev_page );
    bh.RegisterMember( "has_next_page", &nc_scen_band::has_next_page );
    bh.RegisterMember( "rows", &nc_scen_band::rows );
    c.RegisterArray<Rml::Vector<nc_scen_band>>();
    g_nc_scen_types_registered = true;
}
} // namespace

tab_direction set_scenario( avatar &u, points_left &points,
                            const tab_direction direction )
{
    int cur_id = 0;
    tab_direction retval = tab_direction::NONE;
    int iContentHeight = 0;

    // ── Tree/carousel view state ──────────────────────────────────────────────
    //
    // `cur_id` stays the authoritative index into the flat `sorted_scens`, so SORT,
    // FILTER, RANDOMIZE and reset_scenario keep working untouched — the grouping is a
    // VIEW over that list, rebuilt whenever the list is.
    // Six, not eight: the tree shares the stage with the info pane, so eight cards left
    // roughly 60dp each and every name longer than "Ambush" was clipped. Six gives a card
    // wide enough for a wrapped two-line name — see .nc-scen-card in newcharscenario.rcss.
    constexpr int NC_SCEN_PAGE = 6;             //< cards visible per group at once
    constexpr int NC_BANDS = static_cast<int>( nc_scen_group::count );
    // Indices into sorted_scens, per group, in list order.
    std::array<std::vector<int>, NC_BANDS> band_items;
    // Declared collapsed, but the group holding the already-selected scenario is opened on
    // the first build (see recalc_scens) — the alternative was an entry screen of three
    // headers over an empty stage. Every OTHER group stays shut so the first thing the
    // screen says is still "what KIND of run".
    std::array<bool, NC_BANDS> band_collapsed = { true, true, true };
    // Cleared once the entry group has been opened; see the parking loop in recalc_scens.
    bool first_group_build = true;
    std::array<int, NC_BANDS> band_page = { 0, 0, 0 };
    // Cursor: which group, and whether it sits on that group's HEADER or on a card. The
    // header being a focus stop is what makes this a tree without needing a new
    // keybinding — CONFIRM on a header toggles it, CONFIRM on a card selects.
    int focus_band = 0;
    bool focus_header = true;
    int focus_card = 0;
    // Click intent, written by the RmlUi callbacks and applied ONCE per input cycle. See
    // the BindEventCallback block for why a click callback must not mutate directly.
    int pending_band = -1;
    int pending_card_band = -1;
    int pending_card_slot = -1;
    int pending_page_band = -1;
    int pending_page_dir = 0;
    bool pending_all = false;

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
    // Expand/collapse every group at once. The UI control and this key are the same
    // action, so a rebind moves the hint printed beside the control too.
    ctxt.register_action( "TOGGLE_ALL_GROUPS" );
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

            // Facts, one field at a time. The label/value split lives in the stylesheet,
            // so no header text or blank-line padding is baked into these strings.
            auto psorter = profession_sorter;
            psorter.sort_by_points = true;
            const auto permitted = s->permitted_professions();
            const auto default_prof = *std::min_element( permitted.begin(), permitted.end(), psorter );
            const int prof_points = default_prof->point_cost();
            std::string prof = default_prof->gender_appropriate_name( u.male );
            if( prof_points > 0 ) {
                prof += colorize( string_format( " (-%d)", prof_points ), c_red );
            } else if( prof_points < 0 ) {
                prof += colorize( string_format( " (+%d)", -prof_points ), c_green );
            }
            data->prof_rml = cata_text_to_rml( prof );
            data->prof_sub_rml = cata_text_to_rml( s->prof_count_str() );

            data->loc_rml = cata_text_to_rml( s->start_name() );
            // Interpunct rather than a second row: two counts do not deserve two lines.
            data->loc_sub_rml = cata_text_to_rml( string_format(
                    _( "%d locations \u00b7 %d variants" ),
                    s->start_location_count(), s->start_location_targets_count() ) );

            // A field with nothing in it reads as a rendering fault, so name the empty state.
            data->veh_rml = cata_text_to_rml( s->vehicle()
                                              ? s->vehicle()->name
                                              : std::string( _( "None" ) ) );
            data->art_dec = nc_scen_art_dec( *s );

            // Flag chips: this scenario's own sigils, each restated in words. Derived from
            // the SAME table the card strip draws from, so a chip can never go missing for a
            // glyph the card shows — the previous hand-written if-chain omitted CITY_START
            // and BORDERED, leaving those sigils unexplained.
            data->chips.clear();
            for( const nc_scen_flag_icon &fi : nc_scen_flag_icons() ) {
                if( !s->has_flag( fi.flag ) ) {
                    continue;
                }
                // The starting-NPC flag only means anything when the option defers to the
                // scenario; otherwise the option decides and the chip would be a lie.
                if( std::string_view( fi.flag ) == "LONE_START" &&
                    get_option<std::string>( "STARTING_NPC" ) != "scenario" ) {
                    continue;
                }
                data->chips.push_back( {
                    .dec = nc_icon_dec_col( fi.seed, 14, fi.col ),
                    .label_rml = cata_text_to_rml( colorize( _( fi.desc ), fi.col ) ) } );
            }
        } else {
            data->cost_rml.clear();
            data->desc_rml.clear();
            data->loc_rml.clear();
            data->loc_sub_rml.clear();
            data->prof_rml.clear();
            data->prof_sub_rml.clear();
            data->veh_rml.clear();
            data->art_dec.clear();
            data->chips.clear();
        }

        // Static vocabulary: built on the first sync and then left alone.
        if( data->legend.empty() ) {
            for( const nc_scen_flag_icon &fi : nc_scen_flag_icons() ) {
                data->legend.push_back( {
                    .dec = nc_icon_dec_col( fi.seed, 14, fi.col ),
                    .label_rml = cata_text_to_rml( colorize( _( fi.label ), fi.col ) ) } );
            }
            // Filled after the model was constructed, so the data-for has to be told: with
            // no dirty notification the legend row stays empty for the life of the screen.
            data->handle.DirtyVariable( "legend" );
        }

        // Expand/collapse-all control. Its label states what a click WILL do rather than the
        // current state: a control named after its own condition makes the reader work out the
        // consequence for themselves.
        {
            const bool any_open = std::ranges::any_of( band_collapsed, []( bool c ) { return !c; } );
            data->all_marker_rml = cata_text_to_rml( colorize( any_open ? "-" : "+", c_yellow ) );
            data->all_label_rml = cata_text_to_rml( colorize( string_format(
                    any_open ? _( "Collapse all  [%s]" ) : _( "Expand all  [%s]" ),
                    ctxt.get_desc( "TOGGLE_ALL_GROUPS", 1 ) ), c_dark_gray ) );
        }

        // Grouped carousel view. Only the VISIBLE page of each open group is emitted, so
        // the document holds ~24 cards at most rather than every scenario in the game.
        data->bands.clear();
        {
            const std::array<std::string, 3> band_names = {
                _( "Basic" ), _( "Advanced" ), _( "Challenge" )
            };
            for( int b = 0; b < NC_BANDS; ++b ) {
                nc_scen_band band;
                const int total = static_cast<int>( band_items[b].size() );
                band.name_rml = cata_text_to_rml( colorize( band_names[b], c_white ) );
                band.collapsed = band_collapsed[b];
                band.focused = ( b == focus_band && focus_header );
                // Page position is worth stating: with a group collapsed you cannot see
                // how much you are hiding, and with it open you cannot see how far the
                // carousel has left to go.
                const int page_start = band_collapsed[b] ? 0 : band_page[b];
                const int page_end = std::min( total, page_start + NC_SCEN_PAGE );
                band.count_rml = cata_text_to_rml( colorize(
                                                       band_collapsed[b] || total <= NC_SCEN_PAGE
                                                       ? string_format( "%d", total )
                                                       : string_format( "%d-%d / %d", page_start + 1, page_end, total ),
                                                       c_dark_gray ) );
                band.marker_rml = cata_text_to_rml( colorize(
                                                        band_collapsed[b] ? "+" : "-", c_yellow ) );
                band.has_info = !band_collapsed[b] && b == focus_band && !focus_header;
                band.has_prev_page = !band_collapsed[b] && page_start > 0;
                band.has_next_page = !band_collapsed[b] && page_end < total;
                if( !band_collapsed[b] ) {
                    for( int k = page_start; k < page_end; ++k ) {
                        const int i = band_items[b][k];
                        const scenario *s = sorted_scens[i];
                        nc_scen_row r;
                        r.chosen = ( g->scen == s );
                        r.unavailable = s->has_flag( "CITY_START" ) && !scenario_sorter.cities_enabled;
                        // Name stays UNCOLOURED on the card: the card's own border and fill
                        // carry cursor/chosen state, and a baked colour would beat any rule
                        // the stylesheet tries to apply (see newchar_common.rcss).
                        r.text_rml = cata_text_to_rml( colorize(
                                                           s->gender_appropriate_name( u.male ),
                                                           r.unavailable ? c_dark_gray : c_light_gray ) );
                        const int cost = s->point_cost();
                        r.cost_rml = cata_text_to_rml( colorize(
                                                           cost == 0 ? std::string( "0" ) : string_format( "%+d", -cost ),
                                                           cost > 0 ? c_red : ( cost < 0 ? c_green : c_dark_gray ) ) );
                        for( const nc_scen_flag_icon &fi : nc_scen_flag_icons() ) {
                            if( s->has_flag( fi.flag ) ) {
                                r.icons.push_back( { nc_icon_dec_col( fi.seed, 14, fi.col ) } );
                            }
                        }
                        r.selected = ( b == focus_band && !focus_header && k == focus_card );
                        band.rows.push_back( r );
                    }
                }
                data->bands.push_back( band );
            }
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
        data->handle.DirtyVariable( "bands" );
        data->handle.DirtyVariable( "all_marker_rml" );
        data->handle.DirtyVariable( "all_label_rml" );
        data->handle.DirtyVariable( "desc_rml" );
        data->handle.DirtyVariable( "loc_rml" );
        data->handle.DirtyVariable( "loc_sub_rml" );
        data->handle.DirtyVariable( "prof_rml" );
        data->handle.DirtyVariable( "prof_sub_rml" );
        data->handle.DirtyVariable( "veh_rml" );
        data->handle.DirtyVariable( "art_dec" );
        data->handle.DirtyVariable( "chips" );
        data->handle.DirtyVariable( "sort_rml" );
        data->handle.DirtyVariable( "filter_rml" );

        // No ScrollIntoView any more: the carousel pages the cursor into view in
        // sync_cur_from_focus, so there is never an off-screen card to scroll to. The old
        // block indexed a flat #nc-scen-list by cur_id, which the grouped view no longer
        // has — it would have addressed the wrong child.
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
        c.Bind( "bands", &data->bands );
        // Clicking a card focuses AND selects it; clicking a header toggles the group.
        // Both need SELECT registered above, or the click never reaches this loop.
        // Click callbacks here RECORD INTENT and mutate nothing. `data-event-*` installs a
        // DataControllerEvent listener on each generated element
        // (DataControllerDefault.cpp:95), and a `data-for` regeneration adds another
        // without removing the old one — it is only removed on element destruction. This
        // document dirties "bands" on every sync_rml, i.e. every redraw, so the listener
        // count grows with frame count and ONE click invokes these callbacks an unbounded,
        // unpredictable number of times. Measured: 15 for a single click.
        //
        // That cannot be filtered by phase or by target — both are legitimate for a real
        // click on a child span. A toggle run N times cancels itself out whenever N is
        // even, which is exactly what kept the groups from opening. So the loop applies
        // these once per input cycle; N duplicate dispatches record the same intent.
        // Mirrors the nc_nav idiom the navigators already use.
        c.BindEventCallback( "on_band",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int b = -1;
            if( !args.empty() ) {
                args[0].GetInto( b );
            }
            if( b >= 0 && b < NC_BANDS ) {
                pending_band = b;
            }
        } );
        c.BindEventCallback( "on_card",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            // Two args: the band index and the index WITHIN THE VISIBLE PAGE. The page
            // offset is added back when the loop applies this.
            int b = -1;
            int k = -1;
            if( args.size() >= 2 ) {
                args[0].GetInto( b );
                args[1].GetInto( k );
            }
            if( b >= 0 && b < NC_BANDS && k >= 0 ) {
                pending_card_band = b;
                pending_card_slot = k;
            }
        } );
        // The carousel arrows. Keyboard reaches the next page by walking off the end of a
        // row, but a mouse user has only these, so they must be real controls.
        c.BindEventCallback( "on_page",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int b = -1;
            int d = 0;
            if( args.size() >= 2 ) {
                args[0].GetInto( b );
                args[1].GetInto( d );
            }
            if( b >= 0 && b < NC_BANDS && d != 0 ) {
                pending_page_band = b;
                pending_page_dir = d;
            }
        } );
        c.Bind( "desc_rml", &data->desc_rml );
        c.Bind( "loc_rml", &data->loc_rml );
        c.Bind( "loc_sub_rml", &data->loc_sub_rml );
        c.Bind( "prof_rml", &data->prof_rml );
        c.Bind( "prof_sub_rml", &data->prof_sub_rml );
        c.Bind( "veh_rml", &data->veh_rml );
        c.Bind( "art_dec", &data->art_dec );
        c.Bind( "chips", &data->chips );
        c.Bind( "sort_rml", &data->sort_rml );
        c.Bind( "filter_rml", &data->filter_rml );
        c.Bind( "legend", &data->legend );
        c.Bind( "all_marker_rml", &data->all_marker_rml );
        c.Bind( "all_label_rml", &data->all_label_rml );
        c.BindEventCallback( "on_toggle_all",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) {
            pending_all = true;
        } );
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

            // Re-derive the grouped view. Order within a group follows sorted_scens, so
            // SORT still governs card order; only the partition is new.
            for( std::vector<int> &v : band_items ) {
                v.clear();
            }
            for( int i = 0; i < scens_length; ++i ) {
                band_items[static_cast<int>( nc_classify_scen( *sorted_scens[i] ) )].push_back( i );
            }
            // Park the cursor on the group holding the active scenario, so re-entering the
            // step (or changing the filter) does not silently move the selection.
            for( int b = 0; b < NC_BANDS; ++b ) {
                const auto it = std::ranges::find( band_items[b], cur_id );
                if( it != band_items[b].end() ) {
                    focus_band = b;
                    focus_card = static_cast<int>( std::distance( band_items[b].begin(), it ) );
                    // On ENTRY, open that group. A scenario is already selected — it is a
                    // child of this group — so opening it puts the cursor, the notch and the
                    // info panel on something real instead of greeting the player with three
                    // collapsed headers and an empty stage. Only on the first build: a later
                    // SORT or FILTER must not reopen a group the player deliberately closed.
                    if( first_group_build ) {
                        band_collapsed[b] = false;
                        first_group_build = false;
                    }
                    focus_header = band_collapsed[b];
                    band_page[b] = focus_card - focus_card % NC_SCEN_PAGE;
                    break;
                }
            }

            recalc_scens = false;
        }

        ui_manager::redraw();
        nc_nav = 0;
        pending_all = false;
        pending_band = -1;
        pending_card_band = -1;
        pending_card_slot = -1;
        pending_page_band = -1;
        pending_page_dir = 0;
        std::string action = ctxt.handle_input();
        if( nc_nav != 0 ) {
            action = nc_nav < 0 ? "PREV_TAB" : "NEXT_TAB";
        }
        // ── Tree navigation ───────────────────────────────────────────────────
        //
        // Vertical order is header, cards, next header … with a collapsed group's cards
        // skipped, so the cursor can never land on a card that is not on screen.
        // Horizontal movement walks the focused group's cards and advances the carousel
        // page at either end — which is why paging needs no key of its own.
        const auto band_size = [&]( int b ) {
            return static_cast<int>( band_items[b].size() );
        };
        // Keeps cur_id (and therefore the info pane and CONFIRM) on the focused card.
        const auto sync_cur_from_focus = [&]() {
            if( !focus_header && band_size( focus_band ) > 0 ) {
                focus_card = std::clamp( focus_card, 0, band_size( focus_band ) - 1 );
                cur_id = band_items[focus_band][focus_card];
                // Page the carousel to keep the cursor visible.
                if( focus_card < band_page[focus_band] ) {
                    band_page[focus_band] = focus_card - focus_card % NC_SCEN_PAGE;
                } else if( focus_card >= band_page[focus_band] + NC_SCEN_PAGE ) {
                    band_page[focus_band] = focus_card - focus_card % NC_SCEN_PAGE;
                }
            }
        };
        // Expand every group, or collapse every group when any is open. Collapsing parks the
        // cursor on the focused group's header, because the card it stood on is gone.
        const auto toggle_all_bands = [&]() {
            const bool any_open = std::ranges::any_of( band_collapsed, []( bool c ) { return !c; } );
            for( std::size_t b = 0; b < band_collapsed.size(); ++b ) {
                band_collapsed[b] = any_open;
            }
            if( any_open ) {
                focus_header = true;
            } else if( band_size( focus_band ) > 0 ) {
                focus_header = false;
                sync_cur_from_focus();
            }
        };
        if( pending_all || action == "TOGGLE_ALL_GROUPS" ) {
            toggle_all_bands();
        }
        // Apply click intent recorded during handle_input, exactly once — however many
        // times the callback was invoked. Cleared before handle_input so a stale intent
        // cannot re-apply on a later frame.
        if( pending_band >= 0 ) {
            const int b = pending_band;
            focus_band = b;
            band_collapsed[b] = !band_collapsed[b];
            // Opening lands the cursor on the first visible card so the arrow has something
            // to point at and the info panel has a scenario to describe; closing returns it
            // to the header, which is then the only thing left to stand on.
            focus_header = band_collapsed[b] || band_size( b ) == 0;
            if( !focus_header ) {
                focus_card = band_page[b];
                sync_cur_from_focus();
            }
        } else if( pending_page_band >= 0 ) {
            const int b = pending_page_band;
            const int next = band_page[b] + pending_page_dir * NC_SCEN_PAGE;
            if( next >= 0 && next < band_size( b ) ) {
                band_page[b] = next;
                // Carry the cursor onto the page being shown, so the highlight never sits
                // on a card that is no longer rendered.
                focus_band = b;
                focus_header = false;
                focus_card = next;
                sync_cur_from_focus();
            }
        } else if( pending_card_band >= 0 && pending_card_slot >= 0 ) {
            const int b = pending_card_band;
            const int idx = band_page[b] + pending_card_slot;
            if( idx < band_size( b ) ) {
                focus_band = b;
                focus_header = false;
                focus_card = idx;
                sync_cur_from_focus();
                // Unavailable card: the cursor moves onto it, the selection does not change.
                if( !( sorted_scens[cur_id]->has_flag( "CITY_START" ) &&
                       !scenario_sorter.cities_enabled ) ) {
                    reset_scenario( u, sorted_scens[cur_id] );
                    points.init_from_options();
                    points.skill_points -= sorted_scens[cur_id]->point_cost();
                }
            }
        }
        if( action == "DOWN" ) {
            if( focus_header && !band_collapsed[focus_band] && band_size( focus_band ) > 0 ) {
                focus_header = false;          // into this group's cards
            } else {
                // To the next group's header, wrapping.
                focus_band = ( focus_band + 1 ) % NC_BANDS;
                focus_header = true;
                focus_card = 0;
            }
            sync_cur_from_focus();
        } else if( action == "UP" ) {
            if( !focus_header ) {
                focus_header = true;           // back onto this group's header
            } else {
                focus_band = ( focus_band + NC_BANDS - 1 ) % NC_BANDS;
                // Land on the previous group's CARDS when it is open, so walking up is the
                // mirror of walking down rather than skipping every card row.
                const bool open = !band_collapsed[focus_band] && band_size( focus_band ) > 0;
                focus_header = !open;
                focus_card = open ? band_size( focus_band ) - 1 : 0;
            }
            sync_cur_from_focus();
        } else if( action == "RIGHT" && !focus_header ) {
            if( focus_card + 1 < band_size( focus_band ) ) {
                focus_card++;
            }
            sync_cur_from_focus();
        } else if( action == "LEFT" && !focus_header ) {
            if( focus_card > 0 ) {
                focus_card--;
            }
            sync_cur_from_focus();
        } else if( action == "RANDOMIZE" ) {
            cur_id = rng( 0, scens_length - 1 );
            // Follow the roll: it may land in a collapsed group, which then opens, because
            // a cursor you cannot see is worse than a group you did not open yourself.
            for( int b = 0; b < NC_BANDS; ++b ) {
                const auto it = std::ranges::find( band_items[b], cur_id );
                if( it != band_items[b].end() ) {
                    focus_band = b;
                    focus_card = static_cast<int>( std::distance( band_items[b].begin(), it ) );
                    focus_header = false;
                    band_collapsed[b] = false;
                    break;
                }
            }
            sync_cur_from_focus();
        } else if( action == "CONFIRM" ) {
            if( focus_header ) {
                band_collapsed[focus_band] = !band_collapsed[focus_band];
                // Mirrors the click route: opening steps onto the first visible card.
                if( !band_collapsed[focus_band] && band_size( focus_band ) > 0 ) {
                    focus_header = false;
                    focus_card = band_page[focus_band];
                    sync_cur_from_focus();
                }
                // Collapsing the group the cursor is inside would hide the cursor.
                if( band_collapsed[focus_band] ) {
                    focus_header = true;
                }
            } else {
                if( sorted_scens[cur_id]->has_flag( "CITY_START" ) && !scenario_sorter.cities_enabled ) {
                    continue;
                }
                reset_scenario( u, sorted_scens[cur_id] );
                points.init_from_options();
                points.skill_points -= sorted_scens[cur_id]->point_cost();
            }
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

/// One IDENTITY row: a field the player can still change on this last step. Fixed cells, so the five
/// labels form one column and the five values another — the alignment argument every row list in the
/// creator makes.
struct nc_ident_row {
    Rml::String cursor_rml;   //< ">" on the cursor row, else empty
    Rml::String label_rml;
    Rml::String value_rml;
    bool selected = false;
    /// The value has an ordering LEFT/RIGHT can walk, so the steppers apply. False on NAME, which has
    /// nothing to step along and whose only editor is a modal.
    bool has_step = false;
    bool can_dec = false;
    bool can_inc = false;
};

/// One row of a record column, or of the BACKGROUND group under the identity list.
///
/// `name` and `value` are separate spans so a label can carry its trailing space INSIDE its own
/// colour run — `colorize(label) + " " + colorize(value)` puts U+0020 between two spans, where RmlUi
/// trims it at parse time (this is what used to render `Traits:None!`). Either may be empty: a plain
/// list entry is name-only, and a group heading is a name plus the `header` flag.
struct nc_desc_row {
    Rml::String name_rml;
    Rml::String value_rml;
    bool header = false;
};

/// One read-only record column: a sigil, a heading, a count of its DATA rows, and the rows.
struct nc_desc_col {
    Rml::String name_rml;
    Rml::String count_rml;
    /// "none", NOT empty: `data-style-decorator` is evaluated on the first frame, before sync_rml has
    /// run, and an empty value becomes `decorator: ;` — a parse error RmlUi logs every frame.
    Rml::String sigil_dec = "none";
    Rml::Vector<nc_desc_row> rows;
};

/// A label / value / sub-line triple in the detail panel.
struct nc_desc_fact {
    Rml::String label_rml;
    Rml::String value_rml;
    Rml::String sub_rml;
};

/// One cell of the seal's lattice. BOTH channels always carry a valid value — a fully transparent
/// colour and "none" — for the same reason as `sigil_dec` above: every cell is bound on the first
/// frame, and an empty style value would be a parse error per cell per frame.
struct nc_seal_cell {
    Rml::String col = "#00000000";
    Rml::String dec = "none";
    /// A node cell, i.e. one of the seven steps. Drawn as a full-size disc carrying that step's
    /// glyph, where a thread cell is a small stitch — the stylesheet cannot tell them apart from
    /// the colour alone, and a ring of uniform blobs reads as a wreath rather than as a seal.
    bool node = false;
};
struct nc_seal_row {
    Rml::Vector<nc_seal_cell> cells;
};

/// One line of the legend under the seal: a step's glyph, its name and its one-line summary. An
/// unlabelled emblem is decoration; with this the seal IS the digest of the build.
struct nc_seal_tally {
    Rml::String dec = "none";
    Rml::String name_rml;
    Rml::String val_rml;
};

struct nc_desc_session {
    Rml::Vector<nc_desc_tab> tabs;
    nc_shell shell;
    Rml::String points_rml;
    Rml::String ready_rml;      //< the finish gate's readiness word, in the meta bar
    Rml::String ident_name_rml;
    Rml::String ident_count_rml;
    Rml::String ident_sigil_dec = "none";
    Rml::Vector<nc_ident_row> ident;
    /// Scenario and profession: part of the record, not editable here, and not cursorable — which is
    /// what keeps the cursor list exactly the five fields above.
    Rml::Vector<nc_desc_row> background;
    nc_desc_col col0;
    nc_desc_col col1;
    nc_desc_col col2;
    Rml::String seal_name_rml;
    Rml::Vector<nc_seal_row> seal;
    Rml::Vector<nc_seal_tally> tally;
    Rml::String sel_name_rml;
    Rml::Vector<nc_desc_fact> facts;
    Rml::String desc_rml;
    Rml::String hint_rml;
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

    Rml::StructHandle<nc_ident_row> ih = c.RegisterStruct<nc_ident_row>();
    ih.RegisterMember( "cursor_rml", &nc_ident_row::cursor_rml );
    ih.RegisterMember( "label_rml", &nc_ident_row::label_rml );
    ih.RegisterMember( "value_rml", &nc_ident_row::value_rml );
    ih.RegisterMember( "selected", &nc_ident_row::selected );
    ih.RegisterMember( "has_step", &nc_ident_row::has_step );
    ih.RegisterMember( "can_dec", &nc_ident_row::can_dec );
    ih.RegisterMember( "can_inc", &nc_ident_row::can_inc );
    c.RegisterArray<Rml::Vector<nc_ident_row>>();

    // Rows before the column that holds them: a member cannot be registered before its own type is.
    Rml::StructHandle<nc_desc_row> rh = c.RegisterStruct<nc_desc_row>();
    rh.RegisterMember( "name_rml", &nc_desc_row::name_rml );
    rh.RegisterMember( "value_rml", &nc_desc_row::value_rml );
    rh.RegisterMember( "header", &nc_desc_row::header );
    c.RegisterArray<Rml::Vector<nc_desc_row>>();

    Rml::StructHandle<nc_desc_col> ch = c.RegisterStruct<nc_desc_col>();
    ch.RegisterMember( "name_rml", &nc_desc_col::name_rml );
    ch.RegisterMember( "count_rml", &nc_desc_col::count_rml );
    ch.RegisterMember( "sigil_dec", &nc_desc_col::sigil_dec );
    ch.RegisterMember( "rows", &nc_desc_col::rows );

    Rml::StructHandle<nc_desc_fact> fh = c.RegisterStruct<nc_desc_fact>();
    fh.RegisterMember( "label_rml", &nc_desc_fact::label_rml );
    fh.RegisterMember( "value_rml", &nc_desc_fact::value_rml );
    fh.RegisterMember( "sub_rml", &nc_desc_fact::sub_rml );
    c.RegisterArray<Rml::Vector<nc_desc_fact>>();

    Rml::StructHandle<nc_seal_cell> sch = c.RegisterStruct<nc_seal_cell>();
    sch.RegisterMember( "col", &nc_seal_cell::col );
    sch.RegisterMember( "dec", &nc_seal_cell::dec );
    sch.RegisterMember( "node", &nc_seal_cell::node );
    c.RegisterArray<Rml::Vector<nc_seal_cell>>();
    Rml::StructHandle<nc_seal_row> srh = c.RegisterStruct<nc_seal_row>();
    srh.RegisterMember( "cells", &nc_seal_row::cells );
    c.RegisterArray<Rml::Vector<nc_seal_row>>();

    Rml::StructHandle<nc_seal_tally> tyh = c.RegisterStruct<nc_seal_tally>();
    tyh.RegisterMember( "dec", &nc_seal_tally::dec );
    tyh.RegisterMember( "name_rml", &nc_seal_tally::name_rml );
    tyh.RegisterMember( "val_rml", &nc_seal_tally::val_rml );
    c.RegisterArray<Rml::Vector<nc_seal_tally>>();

    g_nc_desc_types_registered = true;
}

/// Deterministic seeds for the four OVERVIEW column glyphs, in column order (IDENTITY, ATTRIBUTES,
/// BODY, EQUIPMENT). Stable by contract, as the tab seeds are: changing one reshuffles a placeholder
/// the artist is matching. The SEAL draws no seed of its own — its nodes reuse `NC_TAB_ICON_SEEDS`,
/// which is the whole point of it.
constexpr unsigned NC_DESC_COL_SEEDS[4] = { 0x4944, 0x4154, 0x424f, 0x4551 };

/// Accumulates one read-only record column.
class record_column
{
    public:
        record_column( const std::string &name, unsigned seed ) {
            c.name_rml = cata_text_to_rml( colorize( name, c_white ) );
            c.sigil_dec = nc_icon_dec( seed, 14, true );
        }

        /// A group heading, optionally with an inline value — "WIELDED  None!". The stylesheet tracks
        /// and upper-cases a header row, so a translated label's trailing ":" and space are dropped
        /// here rather than in the PO: the msgids stay exactly as the panes used them.
        void head( const std::string &label, const std::string &value = std::string(),
                   const nc_color &value_col = c_light_red ) {
            std::string l = label;
            while( !l.empty() && ( l.back() == ':' || l.back() == ' ' ) ) {
                l.pop_back();
            }
            nc_desc_row r;
            r.header = true;
            r.name_rml = cata_text_to_rml( colorize( l, c_yellow ) );
            if( !value.empty() ) {
                r.value_rml = cata_text_to_rml( colorize( value, value_col ) );
            }
            c.rows.push_back( r );
        }

        /// A data row: a name, and the one number worth comparing where there is one.
        void row( const std::string &name, const nc_color &name_col,
                  const std::string &value = std::string(), const nc_color &value_col = c_white ) {
            nc_desc_row r;
            r.name_rml = cata_text_to_rml( colorize( name, name_col ) );
            if( !value.empty() ) {
                r.value_rml = cata_text_to_rml( colorize( value, value_col ) );
            }
            c.rows.push_back( r );
            items++;
        }

        /// The count promises how much there is to READ, so headings do not count towards it.
        auto take() -> nc_desc_col {
            c.count_rml = cata_text_to_rml( colorize( string_format( "%d", items ), c_dark_gray ) );
            return std::move( c );
        }

    private:
        nc_desc_col c;
        int items = 0;
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

    character_preview_window character_preview;
    character_preview.init( &you );
    const bool use_character_preview = get_option<bool>( "USE_CHARACTER_PREVIEW" );

    const auto init_windows = [&]( ui_adaptor & ui ) {
        // The fifteen curses windows this step used to lay out went with the rework: the document
        // owns every box on screen now, and this one is left only for ui_adaptor's own sizing.
        w = catacurses::newwin( TERMY, TERMX, point_zero );
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
    // All three are required for a click to reach this loop at all. MOUSE_LEFT binds to SELECT on
    // mouse DOWN while RmlUi fires `click` — and therefore the callbacks — on mouse UP; that UP
    // resolves to CATA_ERROR, and input.cpp:894-897 `continue`s on an unrecognised MOUSE event
    // WITHOUT returning. COORDINATE sets handling_coordinate_input, which skips that early
    // `continue`; ANY_INPUT makes the fall-through at :912 return rather than loop again. Either
    // alone still parks the loop, and a parked loop clears the intent a click recorded. See
    // plans/charcreation-bionics-chassis.md.
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "COORDINATE" );
    ctxt.register_action( "ANY_INPUT" );
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
    /// The scenario's allowed locations, in the same order the CHOOSE_LOCATION list shows them.
    /// LEFT/RIGHT step through this ring with "* Random *" at position 0, so the field answers the
    /// same keys as the other four instead of needing a modal to change at all.
    std::vector<start_location_id> allowed_locs;
    for( const auto &loc : start_locations::get_all() ) {
        if( g->scen->allowed_start( loc.id ) ) {
            uilist_entry entry( loc.id.id().to_i(), true, -1,
                                string_format( START_LOC_TEXT_TEMPLATE, loc.name(), loc.targets_count() ) );

            select_location.entries.emplace_back( entry );
            allowed_locs.push_back( loc.id );

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

    int min_allowed_age = profession::min_age;
    int max_allowed_age = profession::max_age;
    // in centimeters. 2 std. deviations below average female height
    const int min_allowed_height = 145;
    const int max_allowed_height = 200;

    /// Position in the LEFT/RIGHT ring: 0 is "* Random *", 1.. are the allowed locations.
    ///
    /// One accumulator and a single `return` at the end, rather than early returns: astyle 3.6.16
    /// mis-indents a trailing-return-type function whose FIRST statement is an if-guard, and it
    /// reformats this file on every build. Same reason the two lambdas below are shaped this way.
    const auto loc_pos = [&]() -> int {
        int p = 0;
        if( !you.random_start_location )
        {
            for( int i = 0; i < static_cast<int>( allowed_locs.size() ); i++ ) {
                if( allowed_locs[i] == you.start_location ) {
                    p = i + 1;
                    break;
                }
            }
        }
        return p;
    };
    /// Targets the current choice can drop you in: the scenario's whole allowance for a roll, or
    /// that one location's own count.
    const auto loc_variants = [&]() -> int {
        return you.random_start_location ? g->scen->start_location_targets_count()
        : you.start_location.obj().targets_count();
    };
    const auto loc_step = [&]( int dir ) {
        const int n = static_cast<int>( allowed_locs.size() ) + 1;
        if( n <= 1 ) {
            return;
        }
        const int p = ( loc_pos() + dir + n ) % n;
        if( p == 0 ) {
            you.random_start_location = true;
        } else {
            you.random_start_location = false;
            you.start_location = allowed_locs[p - 1];
        }
    };

    /// LEFT/RIGHT on the cursored field, and the same thing the row's two steppers do. A name has no
    /// ordering to walk, so it is the one field this does nothing for.
    const auto adjust_field = [&]( char_creation::description_selector f, int dir ) {
        switch( f ) {
            case char_creation::GENDER:
                you.male = !you.male;
                break;
            case char_creation::HEIGHT:
                you.set_base_height( clamp( you.base_height() + dir, min_allowed_height,
                                            max_allowed_height ) );
                break;
            case char_creation::AGE:
                you.set_base_age( clamp( you.base_age() + dir, min_allowed_age, max_allowed_age ) );
                break;
            case char_creation::LOCATION:
                loc_step( dir );
                break;
            default:
                break;
        }
    };

    /// Every field's editor, reached from CONFIRM and from the row's edit cell.
    ///
    /// Called from the step's own loop, never from a click callback: three of the five open a modal,
    /// and running a nested input loop from inside an RmlUi event dispatch is not the same thing as
    /// running it from here. The callbacks record the intent; this applies it.
    const auto edit_field = [&]( char_creation::description_selector f ) {
        string_input_popup popup;
        switch( f ) {
            case char_creation::NAME: {
                // Don't edit names when sharing maps: the name IS the username there.
                if( MAP_SHARING::isSharing() ) {
                    break;
                }
                popup.title( _( "Enter name.  Cancel to delete all." ) )
                     .text( you.name )
                     .only_digits( false );
                you.name = popup.query_string();
                no_name_entered = you.name.empty();
                break;
            }
            case char_creation::GENDER:
                you.male = !you.male;
                break;
            case char_creation::HEIGHT: {
                popup.title( _( "Enter height in centimeters.  Minimum 145, maximum 200" ) )
                     .text( string_format( "%d", you.base_height() ) )
                     .only_digits( true );
                const int result = popup.query_int();
                if( result != 0 ) {
                    you.set_base_height( clamp( result, min_allowed_height, max_allowed_height ) );
                }
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
            case char_creation::LOCATION: {
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
                break;
            }
            default:
                break;
        }
    };

    /// The finish gate's inputs. `spare_ok` is how the NEXT_TAB chain clears a question it has
    /// already had answered, so confirming that spare points may be discarded still asks about a
    /// missing name — which is what the old if/else chain did within one keypress.
    const auto gate_inputs = [&]( bool spare_ok ) -> nc_finish_gate::inputs {
        return {
            .valid = points.is_valid(),
            .stat_left = points.stat_points_left(),
            .trait_left = points.trait_points_left(),
            .skill_left = points.skill_points_left(),
            .spare = points.has_spare() && !spare_ok,
            .name_empty = you.name.empty() };
    };
    /// The four refusal sentences, unchanged from the popups this chain has always raised. An
    /// overspent SKILL pool has always shown the generic one.
    const auto refusal_text = []( nc_finish_gate::verdict v ) -> std::string {
        std::string msg = _( "Too many points allocated, change some features and try again." );
        if( v == nc_finish_gate::verdict::over_trait )
        {
            msg = _( "Too many trait points allocated, change some traits or lower some stats and try again." );
        } else if( v == nc_finish_gate::verdict::over_stat )
        {
            msg = _( "Too many stat points allocated, lower some stats and try again." );
        }
        return msg;
    };

    // RmlUi render path (render-only; keyboard still owns nav/edit/confirm below).
    auto data = std::make_unique<nc_desc_session>();
    rml_doc rml;
    // Set by the arrow click callbacks, consumed by the input loop below. Not a
    // tab_direction: it is translated into an action string so the existing
    // keyboard handling stays the single place navigation is decided.
    int nc_nav = 0;
    // Click intent, applied ONCE per input cycle. `data-event-*` installs a listener per generated
    // element and a `data-for` regeneration adds another without removing the old, so a callback that
    // mutated directly would run an unbounded number of times per click — measured at 15 on the
    // SCENARIO tab. See plans/charcreation-scenario-tree.md.
    int pending_row = -1;
    int pending_step_row = -1;
    int pending_step_dir = 0;
    int pending_edit = -1;

    /// The seven steps the seal welds together, POINTS .. SKILLS. A node's colour is its step's
    /// STATE, so it belongs with the model rather than with the animation tick — only the socket glow
    /// behind the glyph moves, and sync_seal reads these.
    std::array<nc_color, nc_seal::nodes> step_col;
    step_col.fill( c_dark_gray );
    bool finish_ready = false;

    const auto sync_rml = [&]() {
        if( !data->handle ) {
            return;
        }
        data->tabs = build_nc_char_tabs<nc_desc_tab>( 7 );  // OVERVIEW tab active
        data->shell = fill_nc_shell( 7, ctxt );
        set_nc_portrait( data->shell, use_character_preview );
        data->points_rml = cata_text_to_rml( nc_points_line( points ) );

        // ── The finish gate: one verdict, three surfaces ───────────────────────
        const nc_finish_gate::verdict verdict = nc_finish_gate::evaluate( gate_inputs( false ) );
        finish_ready = ( verdict == nc_finish_gate::verdict::ready );
        std::string ready_word;
        std::string ready_why;
        nc_color ready_col = c_light_green;
        switch( verdict ) {
            case nc_finish_gate::verdict::ready:
                ready_word = _( "Ready to begin" );
                ready_why = string_format( _( "%s to begin" ), ctxt.get_desc( "NEXT_TAB" ) );
                break;
            case nc_finish_gate::verdict::over_skill:
                ready_word = _( "Too many points allocated" );
                ready_why = _( "Skill points overspent" );
                ready_col = c_red;
                break;
            case nc_finish_gate::verdict::over_trait:
                ready_word = _( "Too many points allocated" );
                ready_why = _( "Trait points overspent" );
                ready_col = c_red;
                break;
            case nc_finish_gate::verdict::over_stat:
                ready_word = _( "Too many points allocated" );
                ready_why = _( "Stat points overspent" );
                ready_col = c_red;
                break;
            case nc_finish_gate::verdict::over_pool:
                ready_word = _( "Too many points allocated" );
                ready_why = _( "The pool does not balance" );
                ready_col = c_red;
                break;
            case nc_finish_gate::verdict::spare_points:
                ready_word = _( "Points left to spend" );
                ready_why = _( "Starting now discards them" );
                ready_col = c_yellow;
                break;
            case nc_finish_gate::verdict::needs_name:
                ready_word = _( "No name entered" );
                ready_why = _( "One will be generated for you" );
                ready_col = c_yellow;
                break;
        }
        data->ready_rml = cata_text_to_rml( colorize( ready_word, ready_col ) );

        // ── IDENTITY: the five fields that are still editable here ─────────────
        /// ONE place naming the five fields: the row labels, the ":: HEADER" over the detail panel
        /// and the first fact's label all read it, so they cannot drift apart.
        const auto field_name = []( char_creation::description_selector f ) -> std::string {
            const std::array<std::string, char_creation::NUM_FIELDS> names = {
                _( "Name" ), _( "Gender" ), _( "Height" ), _( "Age" ), _( "Starting location" )
            };
            const int i = static_cast<int>( f );
            return i >= 0 && i < static_cast<int>( names.size() ) ? names[i] : std::string();
        };
        data->ident_name_rml = cata_text_to_rml( colorize( _( "Identity" ), c_white ) );
        data->ident_count_rml = cata_text_to_rml( colorize(
                                    string_format( "%d", static_cast<int>( char_creation::NUM_FIELDS ) ), c_dark_gray ) );
        data->ident_sigil_dec = nc_icon_dec( NC_DESC_COL_SEEDS[0], 14, true );
        data->ident.clear();
        /// One field's row. `value` arrives already COLOURED: gender paints its two halves
        /// separately, and the location's colour carries whether it is a roll or a choice.
        struct ident_spec {
            char_creation::description_selector field;
            std::string label;
            std::string value;
            /// The value has an ordering LEFT/RIGHT can walk, so the steppers are drawn.
            bool has_step = false;
            bool can_dec = false;
            bool can_inc = false;
        };
        const auto add_ident = [&]( const ident_spec & s ) {
            nc_ident_row r;
            r.selected = ( current_selector == s.field );
            if( r.selected ) {
                r.cursor_rml = cata_text_to_rml( colorize( ">", c_yellow ) );
            }
            r.label_rml = cata_text_to_rml( colorize( s.label, r.selected ? c_white : c_light_gray ) );
            r.value_rml = cata_text_to_rml( s.value );
            r.has_step = s.has_step;
            r.can_dec = s.can_dec;
            r.can_inc = s.can_inc;
            data->ident.push_back( r );
        };
        {
            std::string val;
            nc_color val_col = c_white;
            if( no_name_entered ) {
                val = _( "--- NO NAME ENTERED ---" );
                val_col = c_light_red;
            } else if( you.name.empty() ) {
                val = _( "--- RANDOM NAME ---" );
                val_col = c_light_gray;
            } else {
                val = you.name;
            }
            add_ident( { .field = char_creation::NAME,
                         .label = field_name( char_creation::NAME ),
                         .value = colorize( val, val_col ) } );
        }
        add_ident( { .field = char_creation::GENDER,
                     .label = field_name( char_creation::GENDER ),
                     // Separator in its own colour run so it survives the trim, and so the two
                     // values cannot read as one word ("MaleFemale").
                     .value = colorize( _( "Male" ), you.male ? c_light_cyan : c_light_gray ) +
                              colorize( " / ", c_dark_gray ) +
                              colorize( _( "Female" ), you.male ? c_light_gray : c_pink ),
                     .has_step = true, .can_dec = true, .can_inc = true } );
        add_ident( { .field = char_creation::HEIGHT,
                     .label = field_name( char_creation::HEIGHT ),
                     .value = colorize( string_format( "%d cm", you.base_height() ), c_white ),
                     .has_step = true,
                     .can_dec = you.base_height() > min_allowed_height,
                     .can_inc = you.base_height() < max_allowed_height } );
        add_ident( { .field = char_creation::AGE,
                     .label = field_name( char_creation::AGE ),
                     .value = colorize( string_format( _( "%d years" ), you.base_age() ), c_white ),
                     .has_step = true,
                     .can_dec = you.base_age() > min_allowed_age,
                     .can_inc = you.base_age() < max_allowed_age } );
        // Name only, and a short marker for a roll: the row clips rather than wraps, and the "(N
        // variants)" the full template carries is on the LOCATION fact's sub-line where it has room.
        const std::string locval = you.random_start_location
                                   ? _( "* Random *" )
                                   : you.start_location.obj().name();
        // A shorter label than the panel's: the row's label cell is a fixed column, and "Starting
        // location" does not fit in it at any sensible width.
        add_ident( { .field = char_creation::LOCATION,
                     .label = _( "Location" ),
                     .value = colorize( locval, you.random_start_location ? c_red : c_white ),
                     .has_step = true,
                     .can_dec = !allowed_locs.empty(),
                     .can_inc = !allowed_locs.empty() } );

        data->background.clear();
        {
            const auto add_bg = [&]( const std::string & label, const std::string & value ) {
                std::string l = label;
                while( !l.empty() && ( l.back() == ':' || l.back() == ' ' ) ) {
                    l.pop_back();
                }
                nc_desc_row h;
                h.header = true;
                h.name_rml = cata_text_to_rml( colorize( l, c_yellow ) );
                data->background.push_back( h );
                nc_desc_row v;
                v.name_rml = cata_text_to_rml( colorize( value, c_light_gray ) );
                data->background.push_back( v );
            };
            // Each on its own row under its own heading rather than as "label: value": a scenario
            // name needs the whole row width, and the row clips rather than wraps.
            add_bg( _( "Scenario: " ), g->scen->gender_appropriate_name( you.male ) );
            add_bg( _( "Profession: " ), you.prof->gender_appropriate_name( you.male ) );
        }

        // ── The record: everything the earlier steps committed ─────────────────
        // Gathered ONCE, then read by both a column and the seal's legend, so the digest and the
        // list cannot disagree about what was bought.
        std::vector<trait_id> current_traits = points.limit == points_left::TRANSFER ?
                                               you.get_mutations() : you.get_base_traits();
        std::sort( current_traits.begin(), current_traits.end(), trait_display_sort );

        std::vector<bionic_id> current_bionics;
        for( const bionic_id &id : you.prof->CBMs() ) {
            current_bionics.push_back( id );
        }
        for( const bionic &bio : you.get_bionic_collection() ) {
            current_bionics.push_back( bio.id );
        }
        std::sort( current_bionics.begin(), current_bionics.end(),
        []( const bionic_id & a, const bionic_id & b ) {
            return localized_compare( a->name.translated(), b->name.translated() );
        } );

        record_column attrs( _( "Attributes" ), NC_DESC_COL_SEEDS[1] );
        attrs.head( _( "Stats:" ) );
        attrs.row( _( "Strength:" ), c_light_gray, string_format( "%d", you.str_max ) );
        attrs.row( _( "Dexterity:" ), c_light_gray, string_format( "%d", you.dex_max ) );
        attrs.row( _( "Intelligence:" ), c_light_gray, string_format( "%d", you.int_max ) );
        attrs.row( _( "Perception:" ), c_light_gray, string_format( "%d", you.per_max ) );
        attrs.head( _( "Skills:" ) );
        int skill_levels = 0;
        {
            auto skillslist = Skill::get_skills_sorted_by( [&]( const Skill & a, const Skill & b ) {
                return localized_compare( std::make_pair( a.display_category(), a.name() ),
                                          std::make_pair( b.display_category(), b.name() ) );
            } );
            bool has_skills = false;
            skill_displayType_id last_category = skill_displayType_id::NULL_ID();
            for( const Skill *elem : skillslist ) {
                int level = you.get_skill_level( elem->ident() );
                if( points.limit != points_left::TRANSFER ) {
                    // The profession's grant is installed AFTER the wizard, so the screen has to add
                    // it itself — the same compensation the SKILLS meters make.
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
                        attrs.head( elem->display_category()->display_string() );
                    }
                    attrs.row( elem->name(), c_light_gray, string_format( "%d", level ) );
                    skill_levels += level;
                    has_skills = true;
                }
            }
            if( !has_skills ) {
                attrs.head( _( "Skills:" ), _( "None!" ) );
            }
        }
        data->col0 = attrs.take();

        record_column body( _( "Body" ), NC_DESC_COL_SEEDS[2] );
        body.head( _( "Traits:" ), current_traits.empty() ? _( "None!" ) : std::string() );
        for( const trait_id &tr : current_traits ) {
            body.row( tr->name(), tr->get_display_color() );
        }
        body.head( _( "Bionics:" ), current_bionics.empty() ? _( "None!" ) : std::string() );
        for( const bionic_id &bio : current_bionics ) {
            body.row( bio->name.translated(), c_white );
        }
        body.head( _( "Spells:" ), you.prof->spells().empty() ? _( "None!" ) : std::string() );
        for( const std::pair<spell_id, int> &sp : you.prof->spells() ) {
            body.row( string_format( _( "%s level %d" ), sp.first->name, sp.second ), c_white );
        }
        body.head( _( "Addictions:" ),
                   you.prof->addictions().empty() ? _( "None!" ) : std::string() );
        for( addiction &addict : you.prof->addictions() ) {
            body.row( addiction_name( addict ), c_white );
        }
        data->col1 = body.take();

        record_column gear( _( "Equipment" ), NC_DESC_COL_SEEDS[3] );
        {
            const auto prof_items = you.prof->items( you.male, you.get_mutations() );
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
                gear.head( head, names.empty() ? _( "None!" ) : std::string() );
                for( const std::string &name : names ) {
                    gear.row( name, c_white );
                }
            };
            add_group( _( "Wielded:" ), wielded );
            add_group( _( "Worn:" ), worn );
            add_group( _( "Inventory:" ), inventory );
        }
        {
            const vproto_id scen_veh = g->scen->vehicle();
            const vproto_id prof_veh = you.prof->vehicle();
            gear.head( _( "Vehicle:" ),
                       ( !scen_veh && !prof_veh ) ? _( "None!" ) : std::string() );
            if( scen_veh ) {
                gear.row( scen_veh->name, c_white );
            }
            if( prof_veh ) {
                gear.row( prof_veh->name, c_white );
            }
            const std::vector<npc_class_id> npcs = you.prof->npcs();
            gear.head( _( "Companions:" ), npcs.empty() ? _( "None!" ) : std::string() );
            for( const npc_class_id &id : npcs ) {
                if( id.is_valid() ) {
                    gear.row( id.obj().get_name(), c_white );
                }
            }
            gear.head( _( "Pets:" ), you.prof->pets().empty() ? _( "None!" ) : std::string() );
            for( const mtype_id &id : you.prof->pets() ) {
                if( id.is_valid() ) {
                    monster pet( id );
                    gear.row( pet.get_name(), c_white );
                }
            }
            gear.head( _( "Cash:" ), !you.prof->starting_cash()
                       ? _( "Random!" )
                       : format_money( you.prof->starting_cash().value() ), c_white );
        }
        data->col2 = gear.take();

        // ── The seal's seven steps ─────────────────────────────────────────────
        // One glyph colour and one number per step, read by the ring's nodes and by the legend
        // beneath it. WHITE when the step has content, dim when it is empty, RED when its budget is
        // overspent — so the seal says what the meta bar's readiness word says.
        //
        // White rather than gold, even though gold is this creator's accent: the glyph is drawn ON
        // the socket the weld animates, and gold on gold at three quarters alpha is not a glyph, it
        // is a smudge.
        //
        // Every value is a NUMBER — the step's effect on the point pool, or the count it committed.
        // A name does not fit here: at this column width "One Last Assassination" overflowed its
        // 16dp row and drew over the two rows beneath it, and both names are already printed in full
        // in the IDENTITY column's BACKGROUND group.
        const auto tally_col = []( bool overspent, bool empty ) -> nc_color {
            return overspent ? c_red : ( empty ? c_dark_gray : c_white );
        };
        const int trait_count = static_cast<int>( current_traits.size() );
        const int bionic_count = static_cast<int>( current_bionics.size() );
        const std::array<std::string, nc_seal::nodes> tally_val = {
            string_format( "%d", points.stat_points + points.trait_points + points.skill_points ),
            string_format( "%+d", -g->scen->point_cost() ),
            string_format( "%+d", -you.prof->point_cost() ),
            string_format( "%d", you.str_max + you.dex_max + you.int_max + you.per_max ),
            string_format( "%d", trait_count ),
            string_format( "%d", bionic_count ),
            string_format( "%d", skill_levels ),
        };
        const std::array<nc_color, nc_seal::nodes> tally_colour = {
            points.is_valid() ? c_white : c_red,
            c_white,
            c_white,
            tally_col( points.stat_points_left() < 0, false ),
            tally_col( points.trait_points_left() < 0, trait_count == 0 ),
            tally_col( false, bionic_count == 0 ),
            tally_col( points.skill_points_left() < 0, skill_levels == 0 ),
        };
        const std::vector<std::string> &caps = nc_step_captions();
        data->seal_name_rml = cata_text_to_rml( colorize( _( "Record" ), c_light_gray ) );
        data->tally.clear();
        for( int i = 0; i < nc_seal::nodes; i++ ) {
            step_col[i] = tally_colour[i];
            nc_seal_tally t;
            t.dec = nc_icon_dec_col( NC_TAB_ICON_SEEDS[i], 12, tally_colour[i] );
            t.name_rml = cata_text_to_rml( colorize( caps[i], c_light_gray ) );
            t.val_rml = cata_text_to_rml( colorize( tally_val[i], tally_colour[i] ) );
            data->tally.push_back( t );
        }

        // ── Detail panel ──────────────────────────────────────────────────────
        data->sel_name_rml = cata_text_to_rml( colorize(
                string_format( ":: %s", field_name( current_selector ) ), c_white ) );
        data->facts.clear();
        const auto add_fact = [&]( const std::string & label, const std::string & value,
        const nc_color & col, const std::string & sub = std::string() ) {
            nc_desc_fact f;
            f.label_rml = cata_text_to_rml( label );
            f.value_rml = cata_text_to_rml( colorize( value, col ) );
            f.sub_rml = cata_text_to_rml( sub );
            data->facts.push_back( f );
        };
        switch( current_selector ) {
            case char_creation::NAME:
                add_fact( field_name( char_creation::NAME ),
                          you.name.empty() ? _( "--- RANDOM NAME ---" ) : you.name,
                          you.name.empty() ? c_light_gray : c_white,
                          string_format( _( "%s to edit" ), ctxt.get_desc( "CONFIRM", 1 ) ) );
                data->desc_rml = cata_text_to_rml( colorize(
                                                       _( "The name your character is remembered by.  Leave it empty and one is generated for you when creation finishes." ),
                                                       c_light_gray ) );
                break;
            case char_creation::GENDER:
                add_fact( field_name( char_creation::GENDER ),
                          you.male ? _( "Male" ) : _( "Female" ),
                          you.male ? c_light_cyan : c_pink,
                          string_format( _( "%s to switch" ), ctxt.get_desc( "CHANGE_GENDER", 1 ) ) );
                data->desc_rml = cata_text_to_rml( colorize(
                                                       _( "Which gender the world addresses you as.  Professions and scenarios have gendered names, and the starting outfit follows it." ),
                                                       c_light_gray ) );
                break;
            case char_creation::HEIGHT:
                add_fact( field_name( char_creation::HEIGHT ),
                          string_format( "%d cm", you.base_height() ), c_white,
                          string_format( _( "%d to %d" ), min_allowed_height, max_allowed_height ) );
                data->desc_rml = cata_text_to_rml( colorize(
                                                       _( "Your height in centimetres.  Together with your build it sets your body weight, and mutations that change your size shift it further." ),
                                                       c_light_gray ) );
                break;
            case char_creation::AGE:
                add_fact( field_name( char_creation::AGE ),
                          string_format( _( "%d years" ), you.base_age() ), c_white,
                          string_format( _( "%d to %d" ), min_allowed_age, max_allowed_age ) );
                data->desc_rml = cata_text_to_rml( colorize(
                                                       _( "How old your character is when the Cataclysm finds them.  It is recorded on your character sheet and advances with the calendar." ),
                                                       c_light_gray ) );
                break;
            case char_creation::LOCATION:
                add_fact( field_name( char_creation::LOCATION ),
                          you.random_start_location
                          ? remove_color_tags( random_start_location_text )
                          : you.start_location.obj().name(),
                          you.random_start_location ? c_red : c_white,
                          // The count the row's short value gives up: how many kinds of place this
                          // choice can drop you in.
                          string_format( vgettext( "%d variant", "%d variants", loc_variants() ),
                                         loc_variants() ) );
                data->desc_rml = cata_text_to_rml( colorize(
                                                       _( "Where you wake up.  A random location is rolled from everything this scenario allows; a named one always starts you somewhere of that kind." ),
                                                       c_light_gray ) );
                break;
            default:
                break;
        }
        // What HEIGHT feeds, which nothing else in creation shows. Both readouts honour the
        // imperial/metric option, and `height_string` includes what mutations do to your size.
        add_fact( _( "Body" ),
                  string_format( "%.0f %s", convert_weight( you.bodyweight() ), weight_units() ),
                  c_white, you.height_string() );
        add_fact( _( "Status" ), ready_word, ready_col, ready_why );

        // Everything the seven-line guide named that the cursor does not now make obvious. `up/down`
        // and `left/right` are the axis names the other steps' hints use.
        if( allow_reroll ) {
            data->hint_rml = cata_text_to_rml( string_format(
                                                   _( "<color_light_green>%s</color> field  ·  <color_light_green>%s</color> change  ·  <color_light_green>%s</color> edit  ·  <color_light_green>%s</color> save template  ·  <color_light_green>%s</color> re-roll  ·  <color_light_green>%s</color> random scenario  ·  <color_light_green>%s</color> finish" ),
                                                   _( "up/down" ), _( "left/right" ), ctxt.get_desc( "CONFIRM", 1 ),
                                                   ctxt.get_desc( "SAVE_TEMPLATE", 1 ), ctxt.get_desc( "REROLL_CHARACTER", 1 ),
                                                   ctxt.get_desc( "REROLL_CHARACTER_WITH_SCENARIO", 1 ), ctxt.get_desc( "NEXT_TAB", 1 ) ) );
        } else {
            data->hint_rml = cata_text_to_rml( string_format(
                                                   _( "<color_light_green>%s</color> field  ·  <color_light_green>%s</color> change  ·  <color_light_green>%s</color> edit  ·  <color_light_green>%s</color> save template  ·  <color_light_green>%s</color> finish" ),
                                                   _( "up/down" ), _( "left/right" ), ctxt.get_desc( "CONFIRM", 1 ),
                                                   ctxt.get_desc( "SAVE_TEMPLATE", 1 ), ctxt.get_desc( "NEXT_TAB", 1 ) ) );
        }

        data->handle.DirtyAllVariables();
    };

    // Wall clock, not a frame counter: the weld must not speed up while a key is held, and must not
    // stall while the player is reading. Same source the DNA strand and the aptitude beam use.
    const auto anim_start = std::chrono::steady_clock::now();
    /// The seal's lattice, rebuilt every animation tick — which is why it is separate from the model
    /// sync: a quiet tick must not re-derive 45 record rows, and the ring must keep moving when
    /// nothing about the character has changed.
    const auto sync_seal = [&]() {
        if( !data->handle ) {
            return;
        }
        const float secs = std::chrono::duration<float>(
                               std::chrono::steady_clock::now() - anim_start ).count();
        const nc_seal::phase ph = nc_seal::at( secs );
        data->seal.clear();
        for( int row = 0; row < nc_apt::grid; row++ ) {
            nc_seal_row sr;
            for( int col = 0; col < nc_apt::grid; col++ ) {
                nc_seal_cell sc;
                switch( nc_seal::classify( col, row ) ) {
                    case nc_seal::layer::empty:
                        // Drawn as nothing, which is the only reason a square lattice reads as a ring.
                        break;
                    case nc_seal::layer::thread:
                        sc.col = nc_dot_col( c_yellow, nc_seal::alpha_of( nc_seal::glow_at( ph,
                                             nc_apt::angle_of( nc_apt::offset_of( col, row ) ) ) ) );
                        break;
                    case nc_seal::layer::node: {
                        const int i = nc_seal::node_at( col, row );
                        // The socket animates; the GLYPH's colour is its step's state and does not.
                        sc.node = true;
                        sc.col = nc_dot_col( c_yellow, nc_seal::node_alpha_of( nc_seal::node_glow( ph, i ) ) );
                        sc.dec = nc_icon_dec_col( NC_TAB_ICON_SEEDS[i], 14, step_col[i] );
                        break;
                    }
                    case nc_seal::layer::hub:
                        // The record itself: the OVERVIEW glyph the other seven feed, on a socket
                        // that flares when the strike closes the ring.
                        sc.node = true;
                        sc.col = nc_dot_col( c_yellow, nc_seal::alpha_of( ph.hub ) );
                        sc.dec = nc_icon_dec_col( NC_TAB_ICON_SEEDS[7], 16,
                                                  finish_ready ? c_white : c_light_gray );
                        break;
                }
                sr.cells.push_back( sc );
            }
            data->seal.push_back( sr );
        }
        data->handle.DirtyVariable( "seal" );
    };

    // The model is rebuilt only when something changed; the seal advances every frame.
    bool model_dirty = true;
    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            if( model_dirty ) {
                sync_rml();
                model_dirty = false;
            }
            sync_seal();
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
        c.Bind( "ready_rml", &data->ready_rml );
        c.Bind( "ident_name_rml", &data->ident_name_rml );
        c.Bind( "ident_count_rml", &data->ident_count_rml );
        c.Bind( "ident_sigil_dec", &data->ident_sigil_dec );
        c.Bind( "ident", &data->ident );
        c.Bind( "background", &data->background );
        c.Bind( "col0", &data->col0 );
        c.Bind( "col1", &data->col1 );
        c.Bind( "col2", &data->col2 );
        c.Bind( "seal_name_rml", &data->seal_name_rml );
        // Easy to forget, and it fails SILENTLY: `data-for` over an unbound name renders nothing at
        // all, so the ring would be an empty square while sync_seal happily filled the array.
        c.Bind( "seal", &data->seal );
        c.Bind( "tally", &data->tally );
        c.Bind( "sel_name_rml", &data->sel_name_rml );
        c.Bind( "facts", &data->facts );
        c.Bind( "desc_rml", &data->desc_rml );
        c.Bind( "hint_rml", &data->hint_rml );
        // Click callbacks RECORD INTENT and mutate nothing — see the comment on pending_row.
        c.BindEventCallback( "on_row",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int row = -1;
            if( !args.empty() ) {
                args[0].GetInto( row );
            }
            pending_row = row;
        } );
        // The steppers and the edit cell, not the row, are what change the character: reading a
        // field is never the same gesture as editing one.
        c.BindEventCallback( "on_dec",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int row = -1;
            if( !args.empty() ) {
                args[0].GetInto( row );
            }
            pending_step_row = row;
            pending_step_dir = -1;
        } );
        c.BindEventCallback( "on_inc",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int row = -1;
            if( !args.empty() ) {
                args[0].GetInto( row );
            }
            pending_step_row = row;
            pending_step_dir = 1;
        } );
        c.BindEventCallback( "on_edit",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int row = -1;
            if( !args.empty() ) {
                args[0].GetInto( row );
            }
            pending_edit = row;
        } );
        data->handle = c.GetModelHandle();
    } );
    if( rml_doc_unavailable( rml, _( "Character creation (OVERVIEW tab)" ) ) ) {
        return tab_direction::QUIT;
    }

    // do not switch IME mode now, but restore previous mode on return
    ime_sentry sentry( ime_sentry::keep );

    const auto in_range = []( int row ) {
        return row >= 0 && row < static_cast<int>( char_creation::NUM_FIELDS );
    };

    do {
        const auto [new_min_age, new_max_age] = profession_age_bounds( *you.prof );
        min_allowed_age = new_min_age;
        max_allowed_age = new_max_age;
        you.set_base_age( clamp( you.base_age(), min_allowed_age, max_allowed_age ) );
        ui_manager::redraw();
        nc_nav = 0;
        pending_row = -1;
        pending_step_row = -1;
        pending_step_dir = 0;
        pending_edit = -1;
        std::string action = ctxt.handle_input();
        if( nc_nav != 0 ) {
            action = nc_nav < 0 ? "PREV_TAB" : "NEXT_TAB";
        }
        // Rebuild the model on the next redraw after anything that can have changed it. The click
        // intents MUST be part of this test, not just `action`: MOUSE_LEFT maps to SELECT on mouse
        // DOWN while RmlUi fires `click` on mouse UP, so the iteration carrying a click's intent is
        // usually an idle one. TIMEOUT and ANY_INPUT are idle as far as the MODEL goes — ANY_INPUT is
        // what every pointer motion returns — and the seal advances either way, because it is
        // outside this gate.
        if( ( action != "TIMEOUT" && action != "ANY_INPUT" ) ||
            pending_row >= 0 || pending_step_row >= 0 || pending_edit >= 0 ) {
            model_dirty = true;
        }
        // Apply click intent exactly once, however many times the callback ran.
        if( in_range( pending_row ) ) {
            current_selector = static_cast<char_creation::description_selector>( pending_row );
        }
        if( in_range( pending_step_row ) && pending_step_dir != 0 ) {
            // The stepper moves the cursor as well as acting, so the panel describes the field the
            // player just changed rather than one they left behind.
            current_selector = static_cast<char_creation::description_selector>( pending_step_row );
            adjust_field( current_selector, pending_step_dir );
        }
        if( in_range( pending_edit ) ) {
            current_selector = static_cast<char_creation::description_selector>( pending_edit );
            edit_field( current_selector );
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
            // The gate names ONE reason at a time, and the chain this replaces applied every
            // applicable one within a single keypress: confirming that spare points may be discarded
            // then still asks about a missing name. So it is asked until it is ready, refuses, or
            // somebody says no.
            bool spare_confirmed = false;
            bool advance = false;
            while( true ) {
                const nc_finish_gate::verdict v = nc_finish_gate::evaluate( gate_inputs( spare_confirmed ) );
                if( nc_finish_gate::is_refusal( v ) ) {
                    popup( refusal_text( v ) );
                    break;
                }
                if( v == nc_finish_gate::verdict::spare_points ) {
                    if( !query_yn( _( "Remaining points will be discarded, are you sure you want to proceed?" ) ) ) {
                        break;
                    }
                    spare_confirmed = true;
                    continue;
                }
                if( v == nc_finish_gate::verdict::needs_name ) {
                    no_name_entered = true;
                    model_dirty = true;
                    ui_manager::redraw();
                    if( !query_yn( _( "Are you SURE you're finished?  Your name will be randomly generated." ) ) ) {
                        break;
                    }
                    you.pick_name();
                    advance = true;
                    break;
                }
                if( query_yn( _( "Are you SURE you're finished?" ) ) ) {
                    advance = true;
                }
                break;
            }
            if( advance ) {
                character_preview.clear();
                return tab_direction::FORWARD;
            }
            continue;
        } else if( action == "PREV_TAB" ) {
            character_preview.clear();
            return tab_direction::BACKWARD;
        } else if( action == "DOWN" ) {
            current_selector = static_cast<char_creation::description_selector>(
                                   ( current_selector + 1 ) % char_creation::NUM_FIELDS );
        } else if( action == "UP" ) {
            current_selector = static_cast<char_creation::description_selector>(
                                   ( current_selector + char_creation::NUM_FIELDS - 1 ) % char_creation::NUM_FIELDS );
        } else if( action == "RIGHT" ) {
            adjust_field( current_selector, 1 );
        } else if( action == "LEFT" ) {
            adjust_field( current_selector, -1 );
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
            edit_field( char_creation::LOCATION );
        } else if( action == "CONFIRM" ) {
            edit_field( current_selector );
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
