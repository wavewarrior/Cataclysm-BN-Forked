#include "widget.h"

#include <string>
#include <vector>

#include <map>

#include "avatar.h"
#include "character.h"
#include "debug.h"
#include "flag.h"
#include "generic_factory.h"
#include "json.h"
#include "magic.h"

namespace
{
generic_factory<widget> widget_factory( "widget" );

widget_var string_to_widget_var( const std::string &s )
{
    static const std::map<std::string, widget_var> m = {
        { "stat_str", widget_var::stat_str },
        { "stat_dex", widget_var::stat_dex },
        { "stat_int", widget_var::stat_int },
        { "stat_per", widget_var::stat_per },
        { "pain", widget_var::pain },
        { "stamina", widget_var::stamina },
        { "mana", widget_var::mana },
        { "max_mana", widget_var::max_mana },
        { "morale", widget_var::morale },
        { "thirst", widget_var::thirst },
        { "fatigue", widget_var::fatigue },
        { "speed", widget_var::speed },
        { "body_graph", widget_var::body_graph },
        { "body_graph_temp", widget_var::body_graph_temp },
        { "body_graph_encumb", widget_var::body_graph_encumb },
        { "body_graph_status", widget_var::body_graph_status },
        { "body_graph_wet", widget_var::body_graph_wet },
    };
    const auto it = m.find( s );
    return it != m.end() ? it->second : widget_var::last;
}
} // namespace

/** @relates string_id */
template<>
const widget &string_id<widget>::obj() const
{
    return widget_factory.obj( *this );
}

/** @relates string_id */
template<>
bool string_id<widget>::is_valid() const
{
    return widget_factory.is_valid( *this );
}

void widget::load_widget( const JsonObject &jo, const std::string &src )
{
    widget_factory.load( jo, src );
}

void widget::reset()
{
    widget_factory.reset();
}

void widget::finalize_all()
{
    widget_factory.finalize();
}

void widget::check_consistency()
{
    widget_factory.check();
}

const std::vector<widget> &widget::get_all()
{
    return widget_factory.get_all();
}

void widget::check() const
{
    // A "sidebar"/layout widget references child widgets by id; catch typos at
    // data-load time rather than as a silent blank panel when dereferenced.
for( const widget_id &child : _widgets ) {
    if( !child.is_valid() ) {
            debugmsg( "widget '%s' references unknown child widget '%s'",
                      id.c_str(), child.c_str() );
        }
    }
}

void widget::load( const JsonObject &jo, const std::string & )
{
    optional( jo, was_loaded, "style", _style, "number" );
    optional( jo, was_loaded, "label", _label );
    optional( jo, was_loaded, "width", _width, 0 );
    optional( jo, was_loaded, "height", _height, 1 );
    optional( jo, was_loaded, "native", _native, std::string() );
    optional( jo, was_loaded, "show_if", _show_if, std::string() );
    optional( jo, was_loaded, "icon", _icon, std::string() );
    if( jo.has_string( "var" ) ) {
        _var = string_to_widget_var( jo.get_string( "var" ) );
    }
    optional( jo, was_loaded, "flags", _flags );
    optional( jo, was_loaded, "widgets", _widgets, string_id_reader<widget> {} );
}

int widget::get_var_value( const avatar &ava ) const
{
    switch( _var ) {
    case widget_var::stat_str:
        return ava.get_str();
        case widget_var::stat_dex:
            return ava.get_dex();
        case widget_var::stat_int:
            return ava.get_int();
        case widget_var::stat_per:
            return ava.get_per();
        case widget_var::pain:
            return ava.get_perceived_pain();
        case widget_var::stamina:
            return ava.get_stamina();
        case widget_var::mana:
            return ava.magic->available_mana();
        case widget_var::max_mana:
            return ava.magic->max_mana( ava );
        case widget_var::morale:
            return ava.get_morale_level();
        case widget_var::thirst:
            return ava.get_thirst();
        case widget_var::fatigue:
            return ava.get_fatigue();
        case widget_var::speed:
            return ava.get_speed();
        // Body-graph dimensions are not scalar values — the body_graph renderer
        // colors a limb grid directly and never calls get_var_value.
        case widget_var::body_graph:
        case widget_var::body_graph_temp:
        case widget_var::body_graph_encumb:
        case widget_var::body_graph_status:
        case widget_var::body_graph_wet:
        case widget_var::last:
            break;
    }
    return 0;
}

bool widget::has_flag( const flag_id &f ) const
{
    return _flags.contains( f );
}

bool widget::has_flag( const std::string &f ) const
{
    return has_flag( flag_id( f ) );
}
