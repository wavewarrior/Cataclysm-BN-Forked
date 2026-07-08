#include "armor_layers.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "cata_utility.h"
#include "catacharset.h" // used for utf8_width()
#include "character_display.h"
#include "clothing_utils.h"
#include "debug.h"
#include "enums.h"
#include "flag.h"
#include "game.h"
#include "game_inventory.h"
#include "input.h"
#include "inventory.h"
#include "item.h"
#include "line.h"
#include "output.h"

#include <RmlUi/Core.h>

#include "rml_screen.h"
#include "rml_util.h"
#include "player_activity.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui_manager.h"
#include "units_utility.h"


namespace
{
const activity_id ACT_ARMOR_LAYERS( "ACT_ARMOR_LAYERS" );
const bodypart_str_id body_part_appendix( "num_bp" );
const flag_id json_flag_HIDDEN( "HIDDEN" );

std::string clothing_layer( const item &worn_item );
std::vector<std::string> clothing_properties(
    const item &worn_item, int width, const Character &, const bodypart_id &bp );
std::vector<std::string> clothing_protection( const item &worn_item, int width );
std::vector<std::string> clothing_flags_description( const item &worn_item );

struct item_penalties {
    std::vector<bodypart_id> body_parts_with_stacking_penalty;
    std::vector<bodypart_id> body_parts_with_out_of_order_penalty;
    std::set<std::string> bad_items_within;

    int badness() const {
        return !body_parts_with_stacking_penalty.empty() +
        !body_parts_with_out_of_order_penalty.empty();
    }

    nc_color color_for_stacking_badness() const {
        switch( badness() ) {
        case 0:
            return c_light_gray;
        case 1:
            return c_yellow;
        case 2:
            return c_light_red;
    }
    debugmsg( "Unexpected badness %d", badness() );
    return c_light_gray;
}
};

// Figure out encumbrance penalties this clothing is involved in
item_penalties get_item_penalties( const location_vector<item>::const_iterator &worn_item_it,
                                   const Character &c, const bodypart_id & )
{
    item *const &worn_item = *worn_item_it;
    layer_level layer = worn_item->get_layer();

    std::vector<bodypart_id> body_parts_with_stacking_penalty;
    std::vector<bodypart_id> body_parts_with_out_of_order_penalty;
    std::vector<std::set<std::string>> lists_of_bad_items_within;

    for( const bodypart_id &bp : c.get_all_body_parts() ) {
        if( !worn_item->covers( bp ) ) {
            continue;
        }
        const int num_items = std::count_if( c.worn.begin(), c.worn.end(),
        [&layer, &bp, &c]( item * const & i ) {
            return i->get_layer() == layer && i->covers( bp )
                   && !( i->has_flag( flag_SEMITANGIBLE ) || is_compact( *i, c ) );
        } );
        if( num_items > 1 ) {
            body_parts_with_stacking_penalty.push_back( bp );
        }

        std::set<std::string> bad_items_within;
        for( auto it = c.worn.begin(); it != worn_item_it; ++it ) {
            if( ( *it )->get_layer() > layer && ( *it )->covers( bp ) ) {
                bad_items_within.insert( ( *it )->type_name() );
            }
        }
        if( !bad_items_within.empty() ) {
            body_parts_with_out_of_order_penalty.push_back( bp );
            lists_of_bad_items_within.push_back( bad_items_within );
        }
    }

    // We intersect all the lists_of_bad_items_within so that if there is one
    // common bad item we're wearing this one over it can be mentioned in the
    // message explaining the penalty.
    while( lists_of_bad_items_within.size() > 1 ) {
        std::set<std::string> intersection_of_first_two;
        std::set_intersection(
            lists_of_bad_items_within[0].begin(), lists_of_bad_items_within[0].end(),
            lists_of_bad_items_within[1].begin(), lists_of_bad_items_within[1].end(),
            std::inserter( intersection_of_first_two, intersection_of_first_two.begin() )
        );
        lists_of_bad_items_within.erase( lists_of_bad_items_within.begin() );
        lists_of_bad_items_within[0] = std::move( intersection_of_first_two );
    }

    if( lists_of_bad_items_within.empty() ) {
        lists_of_bad_items_within.emplace_back();
    }

    return { std::move( body_parts_with_stacking_penalty ),
             std::move( body_parts_with_out_of_order_penalty ),
             std::move( lists_of_bad_items_within[0] ) };
}

std::string body_part_names( const std::vector<bodypart_id> &parts )
{
    if( parts.empty() ) {
        debugmsg( "Asked for names of empty list" );
        return {};
    }

    std::vector<std::string> names;
    names.reserve( parts.size() );
    for( size_t i = 0; i < parts.size(); ++i ) {
        const bodypart_id &part = parts[i];
        if( i + 1 < parts.size() &&
            parts[i + 1] == convert_bp( static_cast<body_part>( bp_aiOther[part->token] ) ).id() ) {
            // Can combine two body parts (e.g. arms)
            names.push_back( body_part_name_accusative( part, 2 ) );
            ++i;
        } else {
            names.push_back( body_part_name_accusative( part ) );
        }
    }

    return enumerate_as_string( names );
}

std::string clothing_layer( const item &worn_item )
{
    std::string layer;

    if( worn_item.has_flag( flag_PERSONAL ) ) {
        layer = _( "This is in your personal aura." );
    } else if( worn_item.has_flag( flag_SKINTIGHT ) ) {
        layer = _( "This is worn next to the skin." );
    } else if( worn_item.has_flag( flag_WAIST ) ) {
        layer = _( "This is worn on or around your waist." );
    } else if( worn_item.has_flag( flag_OUTER ) ) {
        layer = _( "This is worn over your other clothes." );
    } else if( worn_item.has_flag( flag_BELTED ) ) {
        layer = _( "This is strapped onto you." );
    } else if( worn_item.has_flag( flag_AURA ) ) {
        layer = _( "This is an aura around you." );
    }

    return layer;
}

std::vector<std::string> clothing_properties(
    const item &worn_item, const int width, const Character &c, const bodypart_id &bp )
{
    std::vector<std::string> props;
    props.reserve( 5 );

    const std::string space = "  ";
    const int coverage = bp.id().is_null() ? worn_item.get_avg_coverage() :
                         worn_item.get_coverage( bp );
    const int encumbrance = bp.id().is_null() ? worn_item.get_avg_encumber(
                                c ) : worn_item.get_encumber( c, bp );
    props.push_back( string_format( "<color_c_green>[%s]</color>", _( "Properties" ) ) );
    props.push_back( name_and_value( space + _( "Coverage:" ),
                                     string_format( "%3d", coverage ), width ) );
    props.push_back( name_and_value( space + _( "Encumbrance:" ),
                                     string_format( "%3d", encumbrance ),
                                     width ) );
    props.push_back( name_and_value( space + _( "Warmth:" ),
                                     string_format( "%3d", worn_item.get_warmth() ), width ) );
    props.push_back( name_and_value( space + string_format( _( "Storage (%s):" ), volume_units_abbr() ),
                                     format_volume( worn_item.get_storage() ), width ) );
    return props;
}

std::vector<std::string> clothing_protection( const item &worn_item, const int width )
{
    std::vector<std::string> prot;
    prot.reserve( 6 );

    const std::string space = "  ";
    prot.push_back( string_format( "<color_c_green>[%s]</color>", _( "Protection" ) ) );
    prot.push_back( name_and_value( space + _( "Bash:" ),
                                    string_format( "%3d", worn_item.bash_resist() ), width ) );
    prot.push_back( name_and_value( space + _( "Cut:" ),
                                    string_format( "%3d", worn_item.cut_resist() ), width ) );
    prot.push_back( name_and_value( space + _( "Ballistic:" ),
                                    string_format( "%3d", worn_item.bullet_resist() ), width ) );
    prot.push_back( name_and_value( space + _( "Acid:" ),
                                    string_format( "%3d", worn_item.acid_resist() ), width ) );
    prot.push_back( name_and_value( space + _( "Fire:" ),
                                    string_format( "%3d", worn_item.fire_resist() ), width ) );
    prot.push_back( name_and_value( space + _( "Environmental:" ),
                                    string_format( "%3d", worn_item.get_env_resist() ), width ) );
    return prot;
}

std::vector<std::string> clothing_flags_description( const item &worn_item )
{
    std::vector<std::string> description_stack;

    if( worn_item.has_flag( flag_FIT ) ) {
        description_stack.emplace_back( _( "It fits you well." ) );
    } else if( worn_item.has_flag( flag_VARSIZE ) ) {
        description_stack.emplace_back( _( "It could be refitted." ) );
    }

    if( worn_item.has_flag( flag_HOOD ) ) {
        description_stack.emplace_back( _( "It has a hood." ) );
    }
    if( worn_item.has_flag( flag_POCKETS ) ) {
        description_stack.emplace_back( _( "It has pockets." ) );
    }
    if( worn_item.has_flag( flag_WATERPROOF ) ) {
        description_stack.emplace_back( _( "It is waterproof." ) );
    }
    if( worn_item.has_flag( flag_WATER_FRIENDLY ) ) {
        description_stack.emplace_back( _( "It is water friendly." ) );
    }
    if( worn_item.has_flag( flag_FANCY ) ) {
        description_stack.emplace_back( _( "It looks fancy." ) );
    }
    if( worn_item.has_flag( flag_SUPER_FANCY ) ) {
        description_stack.emplace_back( _( "It looks really fancy." ) );
    }
    if( worn_item.has_flag( flag_FLOTATION ) ) {
        description_stack.emplace_back( _( "You will not drown today." ) );
    }
    if( worn_item.has_flag( flag_OVERSIZE ) ) {
        description_stack.emplace_back( _( "It is very bulky." ) );
    }
    if( worn_item.has_flag( flag_SWIM_GOGGLES ) ) {
        description_stack.emplace_back( _( "It helps you to see clearly underwater." ) );
    }
    if( worn_item.has_flag( flag_SEMITANGIBLE ) ) {
        description_stack.emplace_back( _( "It can occupy the same space as other things." ) );
    }
    if( worn_item.has_flag( flag_COMPACT ) ) {
        description_stack.emplace_back( _( "It won't encumber you when worn with other things." ) );
    }

    return description_stack;
}

} //namespace

struct layering_item_info {
    item_penalties penalties;
    int encumber;
    std::string name;

    // Operator overload required to leverage vector equality operator.
    bool operator ==( const layering_item_info &o ) const {
        // This is used to merge e.g. both arms into one entry when their items
        // are equivalent.  For that purpose we don't care about the exact
        // penalities because they will list different body parts; we just
        // check that the badness is the same (which is all that matters for
        // rendering the right-hand list).
        return this->penalties.badness() == o.penalties.badness() &&
               this->encumber == o.encumber &&
               this->name == o.name;
    }
};

static std::vector<layering_item_info> items_cover_bp( const Character &c, const bodypart_id &bp )
{
    std::vector<layering_item_info> s;
    for( auto elem_it = c.worn.begin(); elem_it != c.worn.end(); ++elem_it ) {
        item *const &elem = *elem_it;
        if( elem->covers( bp ) ) {
            s.push_back( { get_item_penalties( elem_it, c, bp ),
                           elem->get_encumber( c, bp ),
                           elem->tname()
                         } );
        }
    }
    return s;
}

// ── RmlUi render path (full UI→RmlUi migration, Tier 2: armor layers) ─────────
// 10th rml_doc consumer; biggest Tier-2 yet — a 4-pane render-only doc (cat
// header / left worn-list+protection / mid item-detail+encumbrance / right
// layering). Two contained reconstructions reuse in-TU/shared helpers without
// touching the curses paths (kept pristine for the A/B): mid_pane_lines() (this
// file, parallel to draw_mid_pane) and character_display::encumbrance_lines()
// (parallel to print_encumbrance). All actions stay on input_context + popups;
// mouse selects the left list (only when NOT in move-mode) + the bodypart arrows.
namespace
{
std::vector<std::string> mid_pane_lines(
    location_vector<item>::const_iterator const &worn_item_it,
    const Character &c, const bodypart_id &bp )
{
    item *const &worn_item = *worn_item_it;
    const int width = 40;
    std::vector<std::string> out;
    out.push_back( worn_item->type_name( 1 ) );
    for( const std::string &s : clothing_properties( *worn_item, width, c, bp ) ) {
        out.push_back( s );
    }
    for( const std::string &s : clothing_protection( *worn_item, width ) ) {
        out.push_back( s );
    }
    const std::string layer = clothing_layer( *worn_item );
    if( !layer.empty() ) {
        out.push_back( layer );
    }
    for( const std::string &s : clothing_flags_description( *worn_item ) ) {
        out.push_back( s );
    }
    const item_penalties penalties = get_item_penalties( worn_item_it, c, bp );
    if( !penalties.body_parts_with_stacking_penalty.empty() ) {
        const std::string layer_description = [&]() {
            switch( worn_item->get_layer() ) {
                case PERSONAL_LAYER:
                    return _( "in your <color_light_blue>personal aura</color>" );
                case UNDERWEAR_LAYER:
                    return _( "<color_light_blue>close to your skin</color>" );
                case REGULAR_LAYER:
                    return _( "of <color_light_blue>normal</color> clothing" );
                case WAIST_LAYER:
                    return _( "on your <color_light_blue>waist</color>" );
                case OUTER_LAYER:
                    return _( "of <color_light_blue>outer</color> clothing" );
                case BELTED_LAYER:
                    return _( "<color_light_blue>strapped</color> to you" );
                case AURA_LAYER:
                    return _( "an <color_light_blue>aura</color> around you" );
                default:
                    return _( "Unexpected layer" );
            }
        }
        ();
        const std::string body_parts = body_part_names( penalties.body_parts_with_stacking_penalty );
        out.push_back( string_format(
                           vgettext( "Wearing multiple items %s on your "
                                     "<color_light_red>%s</color> is adding encumbrance there.",
                                     "Wearing multiple items %s on your "
                                     "<color_light_red>%s</color> is adding encumbrance there.",
                                     penalties.body_parts_with_stacking_penalty.size() ),
                           layer_description, body_parts ) );
    }
    if( !penalties.body_parts_with_out_of_order_penalty.empty() ) {
        const std::string body_parts = body_part_names( penalties.body_parts_with_out_of_order_penalty );
        if( penalties.bad_items_within.empty() ) {
            out.push_back( string_format(
                               vgettext( "Wearing this outside items it would normally be beneath "
                                         "is adding encumbrance to your <color_light_red>%s</color>.",
                                         "Wearing this outside items it would normally be beneath "
                                         "is adding encumbrance to your <color_light_red>%s</color>.",
                                         penalties.body_parts_with_out_of_order_penalty.size() ),
                               body_parts ) );
        } else {
            const std::string bad_item_name = *penalties.bad_items_within.begin();
            out.push_back( string_format(
                               vgettext( "Wearing this outside your <color_light_blue>%s</color> "
                                         "is adding encumbrance to your <color_light_red>%s</color>.",
                                         "Wearing this outside your <color_light_blue>%s</color> "
                                         "is adding encumbrance to your <color_light_red>%s</color>.",
                                         penalties.body_parts_with_out_of_order_penalty.size() ),
                               bad_item_name, body_parts ) );
        }
    }
    return out;
}

struct al_left_row {
    Rml::String name_rml;
    Rml::String storage_rml;
    Rml::String hidden_rml;
    bool selected = false;   // cursor row
    bool moving = false;     // picked up for reordering (move mode)
};
struct al_line {
    Rml::String text_rml;
};
struct al_session {
    Rml::String cat_rml;        // "<< Torso >>" bodypart name
    Rml::String hint_rml;       // footer key hints
    Rml::String protect_rml;    // Total Protection block (specific bp only)
    Rml::Vector<al_left_row> left;
    Rml::Vector<al_line> mid;
    Rml::Vector<al_line> encumb;
    Rml::Vector<al_line> right;
    bool left_empty = false;
    Rml::DataModelHandle handle;
};

bool g_al_types_registered = false;

void register_al_rml_types( Rml::DataModelConstructor &c )
{
    if( g_al_types_registered ) {
        return;
    }
    Rml::StructHandle<al_left_row> rh = c.RegisterStruct<al_left_row>();
    rh.RegisterMember( "name_rml", &al_left_row::name_rml );
    rh.RegisterMember( "storage_rml", &al_left_row::storage_rml );
    rh.RegisterMember( "hidden_rml", &al_left_row::hidden_rml );
    rh.RegisterMember( "selected", &al_left_row::selected );
    rh.RegisterMember( "moving", &al_left_row::moving );
    c.RegisterArray<Rml::Vector<al_left_row>>();
    Rml::StructHandle<al_line> lh = c.RegisterStruct<al_line>();
    lh.RegisterMember( "text_rml", &al_line::text_rml );
    c.RegisterArray<Rml::Vector<al_line>>();
    g_al_types_registered = true;
}
} // namespace

bool &armor_layers_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

void show_armor_layers_ui( Character &who )
{
    /* Define required height of the right pane:
    * + 3 - horizontal lines;
    * + 1 - caption line;
    * + 2 - innermost/outermost string lines;
    * + num_of_parts - sub-categories (torso, head, eyes, etc.);
    * + 1 - gap;
    * number of lines required for displaying all items is calculated dynamically,
    * because some items can have multiple entries (i.e. cover a few parts of body).
    */

    const auto all_parts = who.get_all_body_parts();
    const int num_of_parts = all_parts.size();

    // FIXME: get_all_body_parts() doesn't return a sorted list
    //        and bodypart_id is not compatible with std::sort()
    //        so let's use a dirty hack
    cata::flat_set<bodypart_id> armor_cat;
    for( const bodypart_id &it : all_parts ) {
        armor_cat.insert( it );
    }
    armor_cat.insert( bodypart_str_id::NULL_ID().id() );

    int req_right_h = 3 + 1 + 2 + num_of_parts + 1;
    for( const bodypart_id &cover : armor_cat ) {
        for( const item * const &elem : who.worn ) {
            if( elem->covers( cover ) ) {
                req_right_h++;
            }
        }
    }

    /* Define required height of the mid pane:
    * + 3 - horizontal lines;
    * + 1 - caption line;
    * + 8 - general properties
    * + 13 - ASSUMPTION: max possible number of flags @ item
    * + num_of_parts+1 - warmth & enc block
    */
    const int req_mid_h = 3 + 1 + 8 + 13 + num_of_parts + 1;

    int win_h = 0;
    int win_w = 0;
    point win;

    int cont_h   = 0;
    int left_w   = 0;
    int right_w  = 0;
    int middle_w = 0;

    int tabindex = 0;
    const int tabcount = num_of_parts + 1;

    int leftListIndex  = 0;
    int leftListOffset = 0;
    int selected       = -1;

    int rightListOffset = 0;

    int leftListLines = 0;
    int rightListLines = 0;

    std::vector<int> tmp_worn;

    auto access_tmp_worn = [&]( int index ) {
        int worn_index = tmp_worn[index];
        location_vector<item>::iterator it = who.worn.begin();
        std::advance( it, worn_index );
        return it;
    };

    // Layout window
    catacurses::window w_sort_armor;
    // Subwindows (between lines)
    catacurses::window w_sort_cat;
    catacurses::window w_sort_left;
    catacurses::window w_sort_middle;
    catacurses::window w_sort_right;
    catacurses::window w_encumb;

    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        win_h = std::min( TERMY, std::max( { FULL_SCREEN_HEIGHT, req_right_h, req_mid_h } ) );
        win_w = FULL_SCREEN_WIDTH + ( TERMX - FULL_SCREEN_WIDTH ) * 3 / 4;
        win.x = TERMX / 2 - win_w / 2;
        win.y = TERMY / 2 - win_h / 2;
        cont_h = win_h - 4;
        left_w = ( win_w - 4 ) / 3;
        right_w = left_w;
        middle_w = ( win_w - 4 ) - left_w - right_w;
        leftListLines = rightListLines = cont_h - 2;
        w_sort_armor = catacurses::newwin( win_h, win_w, win );
        w_sort_cat = catacurses::newwin( 1, win_w - 4, win + point( 2, 1 ) );
        w_sort_left = catacurses::newwin( cont_h, left_w, win + point( 1, 3 ) );
        w_sort_middle = catacurses::newwin( cont_h - num_of_parts - 1, middle_w,
                                            win + point( 2 + left_w, 3 ) );
        w_sort_right = catacurses::newwin( cont_h, right_w,
                                           win + point( 3 + left_w + middle_w, 3 ) );
        w_encumb = catacurses::newwin( num_of_parts + 1, middle_w,
                                       win + point( 2 + left_w, -1 + 3 + cont_h - num_of_parts ) );
        ui.position_from_window( w_sort_armor );
    } );
    ui.mark_resize();

    input_context ctxt( "SORT_ARMOR" );
    ctxt.register_cardinal();
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "MOVE_ARMOR" );
    ctxt.register_action( "CHANGE_SIDE" );
    ctxt.register_action( "TOGGLE_CLOTH" );
    ctxt.register_action( "ASSIGN_INVLETS" );
    ctxt.register_action( "SORT_ARMOR" );
    ctxt.register_action( "EQUIP_ARMOR" );
    ctxt.register_action( "EQUIP_ARMOR_HERE" );
    ctxt.register_action( "REMOVE_ARMOR" );
    ctxt.register_action( "USAGE_HELP" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    auto do_return_entry = []() {
        avatar &you = get_avatar();
        you.assign_activity( std::make_unique<player_activity>(
                                 std::make_unique<armor_layers_activity_actor>() ) );
        you.activity->auto_resume = true;
        you.activity->moves_left = INT_MAX;
    };

    int leftListSize = 0;
    int rightListSize = 0;

    // RmlUi render path (Tier 2: armor layers). data/doc declared before on_redraw
    // so the redraw can sync; opened after ctxt is built (harness 16ms tick). The
    // multiple early returns below all exit the function → the rml_doc destructor
    // tears down (no explicit close).
    std::unique_ptr<al_session> data;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !data ) {
            return;
        }
        const bodypart_id &bp = armor_cat[ tabindex ];

        // cat header ("Sort Armor  << name >>") + right-aligned hint keys
        const std::string name = bp.id() ? body_part_name_as_heading( bp, 1 ) : _( "All" );
        data->cat_rml = cata_text_to_rml( colorize( _( "Sort Armor" ), c_white ) +
                                          colorize( string_format( "  << %s >>", name ), c_yellow ) );
        data->hint_rml = cata_text_to_rml( string_format(
                                               _( "[<color_yellow>%s</color>] Hide sprite.  "
                                                   "[<color_yellow>%s</color>] Change side.  "
                                                   "Press [<color_yellow>%s</color>] for help.  "
                                                   "Press [<color_yellow>%s</color>] to change keybindings." ),
                                               ctxt.get_desc( "TOGGLE_CLOTH" ), ctxt.get_desc( "CHANGE_SIDE" ),
                                               ctxt.get_desc( "USAGE_HELP" ), ctxt.get_desc( "HELP_KEYBINDINGS" ) ) );

        // LEFT worn list (RmlUi renders all rows; no curses windowing)
        data->left.clear();
        for( int i = 0; i < leftListSize; i++ ) {
            al_left_row r;
            item *const worn = *access_tmp_worn( i );
            const item_penalties penalties = get_item_penalties( access_tmp_worn( i ), who, bp );
            r.name_rml = cata_text_to_rml( colorize( worn->display_name(),
                                           penalties.color_for_stacking_badness() ) );
            r.storage_rml = cata_text_to_rml( format_volume( worn->get_storage() ) );
            r.hidden_rml = worn->has_flag( json_flag_HIDDEN )
                           ? cata_text_to_rml( colorize( _( "H" ), c_cyan ) ) : Rml::String();
            r.selected = ( i == leftListIndex );
            r.moving = ( i == selected );
            data->left.push_back( r );
        }
        data->left_empty = ( leftListSize == 0 );

        // Total Protection block (specific bodypart only)
        data->protect_rml.clear();
        if( bp.id() ) {
            const int stab = static_cast<int>( std::round( who.get_armor_cut( bp ) * 0.8f ) );
            std::string p = _( "Total Protection:" );
            p += "\n  " + string_format( _( "Bash: %d" ), who.get_armor_bash( bp ) );
            p += "\n  " + string_format( _( "Cut: %d" ), who.get_armor_cut( bp ) );
            p += "\n  " + string_format( _( "Stab: %d" ), stab );
            p += "\n  " + string_format( _( "Ballistic: %d" ), who.get_armor_bullet( bp ) );
            data->protect_rml = cata_text_to_rml( p );
        }

        // Append one colour-tagged line to a pane vector.
        const auto push_line = [&]( Rml::Vector<al_line> &v, const std::string & s ) {
            al_line ln;
            ln.text_rml = cata_text_to_rml( s );
            v.push_back( ln );
        };

        // MIDDLE item detail (mid_pane_lines) or the empty hint
        data->mid.clear();
        if( leftListSize > 0 ) {
            for( const std::string &l : mid_pane_lines( access_tmp_worn( leftListIndex ), who, bp ) ) {
                push_line( data->mid, l );
            }
        } else {
            push_line( data->mid, _( "Nothing to see here!" ) );
        }

        // Encumbrance + warmth table (shared builder)
        data->encumb.clear();
        const item *sel_clothing = leftListSize > 0 ? *access_tmp_worn( leftListIndex ) : nullptr;
        for( const std::string &l : character_display::encumbrance_lines( who, sel_clothing ) ) {
            push_line( data->encumb, l );
        }

        // RIGHT per-bodypart layering (combine logic mirrors the curses path)
        data->right.clear();
        const auto combine_bp = [&who]( const bodypart_id & cover ) -> bool {
            const bodypart_id opposite = cover.obj().opposite_part;
            return cover != opposite &&
            items_cover_bp( who, cover ) == items_cover_bp( who, opposite );
        };
        cata::flat_set<bodypart_id> rl;
        for( const bodypart_id cover : armor_cat ) {
            if( !combine_bp( cover ) || rl.count( cover.obj().opposite_part ) == 0 ) {
                rl.insert( cover );
            }
        }
        for( const bodypart_id cover : rl ) {
            if( cover.id().is_null() ) {
                continue;
            }
            const bool is_highlighted = cover == bp || ( combine_bp( cover ) &&
                                        static_cast<bodypart_id>( cover.obj().opposite_part ) == bp );
            push_line( data->right, colorize( string_format( "%s:",
                                              body_part_name_as_heading( cover, combine_bp( cover ) ? 2 : 1 ) ),
                                              is_highlighted ? c_yellow : c_white ) );
            for( layering_item_info &elem : items_cover_bp( who, cover ) ) {
                const char plus = elem.penalties.badness() > 0 ? '+' : ' ';
                push_line( data->right, "  " +
                           colorize( elem.name, elem.penalties.color_for_stacking_badness() ) +
                           "  " + colorize( string_format( "%3d%c", elem.encumber, plus ), c_light_gray ) );
            }
        }

        data->handle.DirtyVariable( "cat_rml" );
        data->handle.DirtyVariable( "hint_rml" );
        data->handle.DirtyVariable( "protect_rml" );
        data->handle.DirtyVariable( "left" );
        data->handle.DirtyVariable( "mid" );
        data->handle.DirtyVariable( "encumb" );
        data->handle.DirtyVariable( "right" );
        data->handle.DirtyVariable( "left_empty" );
    };

    rml.open( armor_layers_rmlui_enabled(), "sortarmor", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        data = std::make_unique<al_session>();
        register_al_rml_types( c );
        c.Bind( "cat_rml", &data->cat_rml );
        c.Bind( "hint_rml", &data->hint_rml );
        c.Bind( "protect_rml", &data->protect_rml );
        c.Bind( "left", &data->left );
        c.Bind( "mid", &data->mid );
        c.Bind( "encumb", &data->encumb );
        c.Bind( "right", &data->right );
        c.Bind( "left_empty", &data->left_empty );
        // Mouse: click/hover a worn row → select it, but ONLY outside move-mode
        // (in move-mode UP/DOWN swap the picked item; a mouse jump would desync
        // the cursor and the picked-item marker). << / >> cycle the bodypart.
        c.BindEventCallback( "on_left",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            if( selected >= 0 ) {
                return;
            }
            int idx = -1;
            if( !args.empty() ) {
                args[0].GetInto( idx );
            }
            if( idx >= 0 && idx < leftListSize ) {
                leftListIndex = idx;
            }
        } );
        c.BindEventCallback( "on_prev_bp",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) {
            if( tabindex-- == 0 ) {
                tabindex = tabcount - 1;
            }
            leftListIndex = leftListOffset = 0;
            selected = -1;
        } );
        c.BindEventCallback( "on_next_bp",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) {
            tabindex = ( tabindex + 1 ) % tabcount;
            leftListIndex = leftListOffset = 0;
            selected = -1;
        } );
        data->handle = c.GetModelHandle();
    } );

    ui.on_redraw( [&]( ui_adaptor & ui ) {
        // RmlUi path owns the screen — sync the model and skip curses drawing.
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    avatar &you = get_avatar();
    bool exit = false;
    while( !exit ) {
        if( who.is_avatar() ) {
            // Totally hoisted this from advanced_inv
            if( you.moves < 0 ) {
                do_return_entry();
                return;
            }
        } else {
            // Player is sorting NPC's armor here
            if( rl_dist( you.bub_pos(), who.bub_pos() ) > 1 ) {
                you.add_msg_if_npc( m_bad, _( "%s is too far to sort armor." ), who.name );
                return;
            }
            if( you.attitude_to( you ) != Attitude::A_FRIENDLY ) {
                you.add_msg_if_npc( m_bad, _( "%s is not friendly!" ), who.name );
                return;
            }
        }

        // Create ptr list of items to display
        tmp_worn.clear();
        const bodypart_id &bp = armor_cat[ tabindex ];
        if( bp.id().is_null() ) {
            // All
            int i = 0;
            for( auto it = who.worn.begin(); it != who.worn.end(); ++it ) {
                tmp_worn.push_back( i++ );
            }
        } else {
            // bp_*
            int i = 0;
            for( auto it = who.worn.begin(); it != who.worn.end(); ++it ) {
                if( ( *it )->covers( bp ) ) {
                    tmp_worn.push_back( i );
                }
                i++;
            }
        }
        leftListSize = tmp_worn.size();

        // Ensure leftListIndex is in bounds
        int new_index_upper_bound = std::max( 0, leftListSize - 1 );
        leftListIndex = std::min( leftListIndex, new_index_upper_bound );

        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( who.is_npc() && action == "ASSIGN_INVLETS" ) {
            // It doesn't make sense to assign invlets to NPC items
            continue;
        }

        // Helper function for moving items in the list
        auto shift_selected_item = [&]() {
            if( selected >= 0 ) {
                auto selected_it = access_tmp_worn( selected );
                auto left_it = access_tmp_worn( leftListIndex );

                std::swap( *selected_it, *left_it );

                int temp = tmp_worn[selected];
                tmp_worn[selected] = tmp_worn[leftListIndex];
                tmp_worn[leftListIndex] = temp;
                selected = leftListIndex;
                who.reset_encumbrance();
            }
        };

        if( action == "UP" && leftListSize > 0 ) {
            if( leftListIndex > 0 ) {
                leftListIndex--;
                if( leftListIndex < leftListOffset ) {
                    leftListOffset = leftListIndex;
                }
            } else {
                leftListIndex = leftListSize - 1;
                if( leftListLines >= leftListSize ) {
                    leftListOffset = 0;
                } else {
                    leftListOffset = leftListSize - leftListLines;
                }
            }

            shift_selected_item();
        } else if( action == "DOWN" && leftListSize > 0 ) {
            if( leftListIndex + 1 < leftListSize ) {
                leftListIndex++;
                if( leftListIndex >= leftListOffset + leftListLines ) {
                    leftListOffset = leftListIndex + 1 - leftListLines;
                }
            } else {
                leftListIndex = 0;
                leftListOffset = 0;
            }

            shift_selected_item();
        } else if( action == "LEFT" ) {
            tabindex--;
            if( tabindex < 0 ) {
                tabindex = tabcount - 1;
            }
            leftListIndex = leftListOffset = 0;
            selected = -1;
        } else if( action == "RIGHT" ) {
            tabindex = ( tabindex + 1 ) % tabcount;
            leftListIndex = leftListOffset = 0;
            selected = -1;
        } else if( action == "NEXT_TAB" ) {
            if( rightListOffset + rightListLines < rightListSize ) {
                rightListOffset++;
            }
        } else if( action == "PREV_TAB" ) {
            if( rightListOffset > 0 ) {
                rightListOffset--;
            }
        } else if( action == "MOVE_ARMOR" ) {
            if( selected >= 0 ) {
                selected = -1;
            } else {
                selected = leftListIndex;
            }
        } else if( action == "CHANGE_SIDE" ) {
            if( leftListIndex < leftListSize && ( *access_tmp_worn( leftListIndex ) )->is_sided() ) {
                if( you.query_yn( _( "Swap side for %s?" ),
                                  colorize( ( *access_tmp_worn( leftListIndex ) )->tname(),
                                            ( *access_tmp_worn( leftListIndex ) )->color_in_inventory() ) ) ) {
                    who.change_side( *access_tmp_worn( leftListIndex ) );
                }
            }
        } else if( action == "TOGGLE_CLOTH" ) {
            if( !( *access_tmp_worn( leftListIndex ) )->has_flag( json_flag_HIDDEN ) ) {
                ( *access_tmp_worn( leftListIndex ) )->set_flag( json_flag_HIDDEN );
            } else {
                ( *access_tmp_worn( leftListIndex ) )->unset_flag( json_flag_HIDDEN );
            }
        } else if( action == "SORT_ARMOR" ) {
            std::stable_sort( who.worn.begin(),
                              who.worn.end(),
            []( item * const & l, item * const & r ) {
                return l->get_layer() < r->get_layer();
            }
                            );
            who.reset_encumbrance();
        } else if( action == "EQUIP_ARMOR" ) {
            // filter inventory for all items that are armor/clothing
            item *loc = game_menus::inv::wear( *who.as_player() );

            // only equip if something valid selected!
            if( loc ) {
                // wear the item
                loc->obtain( who );
                bool equipped = who.as_player()->wear_possessed( *loc );
                if( equipped ) {
                    const bodypart_id &bp = armor_cat[tabindex];
                    if( tabindex == num_of_parts || loc->covers( bp ) ) {
                        // Set ourselves up to be pointing at the new item
                        // TODO: This doesn't work yet because we don't save our
                        // state through other activities, but that's a thing
                        // that would be nice to do.
                        bool found = false;
                        leftListIndex =
                            std::count_if( who.worn.begin(), who.worn.end(),
                        [&]( item * const & i ) {
                            if( i == loc ) {
                                found = true;
                            }
                            return !found && ( tabindex == num_of_parts || i->covers( bp ) );
                        } );
                    }
                } else if( who.is_npc() ) {
                    // TODO: Pass the reason here
                    popup( _( "Can't put this on!" ) );
                }
            }
        } else if( action == "EQUIP_ARMOR_HERE" ) {
            // filter inventory for all items that are armor/clothing
            item *loc = game_menus::inv::wear( *who.as_player() );

            // only equip if something valid selected!
            if( loc ) {
                // wear the item
                loc->obtain( who );
                const std::optional<location_vector<item>::iterator> position = ( leftListSize > 0 ) ?
                    access_tmp_worn( leftListIndex ) : std::optional<location_vector<item>::iterator>( std::nullopt );
                if( !who.as_player()->wear_possessed( *loc, true, position ) &&
                    who.is_npc() ) {
                    // TODO: Pass the reason here
                    popup( _( "Can't put this on!" ) );
                }
            }
        } else if( action == "REMOVE_ARMOR" ) {
            // query (for now)
            if( leftListIndex < leftListSize ) {
                if( you.query_yn( _( "Remove selected armor?" ) ) ) {
                    do_return_entry();
                    // remove the item, asking to drop it if necessary
                    item &to_takeoff = **access_tmp_worn( leftListIndex );
                    who.as_player()->takeoff( to_takeoff );
                    if( !you.has_activity( ACT_ARMOR_LAYERS ) ) {
                        // An activity has been created to take off the item;
                        // we must surrender control until it is done.
                        return;
                    }
                    you.cancel_activity();
                    selected = -1;
                    leftListIndex = std::max( 0, leftListIndex - 1 );
                }
            }
        } else if( action == "ASSIGN_INVLETS" ) {
            assert( who.is_avatar() );
            // prompt first before doing this (yes, yes, more popups...)
            if( query_yn( _( "Reassign invlets for armor?" ) ) ) {
                // Start with last armor (the most unimportant one?)
                auto iiter = inv_chars.rbegin();
                auto witer = who.worn.rbegin();
                while( witer != who.worn.rend() && iiter != inv_chars.rend() ) {
                    const char invlet = *iiter;
                    item &w = **witer;
                    if( invlet == w.invlet ) {
                        ++witer;
                    } else if( who.invlet_to_item( invlet ) != nullptr ) {
                        ++iiter;
                    } else {
                        who.inv_reassign_item( w, invlet );
                        ++witer;
                        ++iiter;
                    }
                }
            }
        } else if( action == "USAGE_HELP" ) {
            popup_getkey(
                _( "Use the [<color_yellow>arrow- or keypad keys</color>] to navigate the left list.\n"
                   "[<color_yellow>%s</color>] to select highlighted armor for reordering.\n"
                   "[<color_yellow>%s</color>] / [<color_yellow>%s</color>] to scroll the right list.\n"
                   "[<color_yellow>%s</color>] to assign special inventory letters to clothing.\n"
                   "[<color_yellow>%s</color>] to change the side on which item is worn.\n"
                   "[<color_yellow>%s</color>] to sort armor into natural layer order.\n"
                   "[<color_yellow>%s</color>] to equip a new item.\n"
                   "[<color_yellow>%s</color>] to equip a new item at the currently selected position.\n"
                   "[<color_yellow>%s</color>] to remove selected armor from oneself.\n"
                   "\n"
                   "\n"
                   "Encumbrance explanation:\n"
                   "\n"
                   "<color_light_gray>The first number is the summed encumbrance from all clothing "
                   "on that bodypart.  The second number is an additional encumbrance penalty "
                   "caused by wearing either multiple items on one of the bodypart's layers or "
                   "wearing items the wrong way (e.g. a shirt over a backpack).  "
                   "The sum of these values is the effective encumbrance value "
                   "your character has for that bodypart.</color>" ),
                ctxt.get_desc( "MOVE_ARMOR" ),
                ctxt.get_desc( "PREV_TAB" ),
                ctxt.get_desc( "NEXT_TAB" ),
                ctxt.get_desc( "ASSIGN_INVLETS" ),
                ctxt.get_desc( "CHANGE_SIDE" ),
                ctxt.get_desc( "TOGGLE_CLOTH" ),
                ctxt.get_desc( "SORT_ARMOR" ),
                ctxt.get_desc( "EQUIP_ARMOR" ),
                ctxt.get_desc( "EQUIP_ARMOR_HERE" ),
                ctxt.get_desc( "REMOVE_ARMOR" )
            );
        } else if( action == "QUIT" ) {
            exit = true;
        }
    }
}
