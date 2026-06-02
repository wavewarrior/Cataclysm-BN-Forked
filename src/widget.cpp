#include "widget.h"

#include <string>
#include <vector>

#include "debug.h"
#include "flag.h"
#include "generic_factory.h"
#include "json.h"

namespace
{
generic_factory<widget> widget_factory( "widget" );
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
    optional( jo, was_loaded, "flags", _flags );
    optional( jo, was_loaded, "widgets", _widgets, string_id_reader<widget> {} );
}

bool widget::has_flag( const flag_id &f ) const
{
    return _flags.contains( f );
}

bool widget::has_flag( const std::string &f ) const
{
    return has_flag( flag_id( f ) );
}
