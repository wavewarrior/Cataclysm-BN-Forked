#include "hud_top_bar.h"

#include <string>

#include "avatar.h"
#include "calendar.h"
#include "color.h"
#include "game.h"
#include "character_functions.h"
#include "lighting/rmlui_layer.h"
#include "options.h"
#include "rml_util.h"
#include "output.h"
#include "string_formatter.h"
#include "weather.h"
#include "path_info.h"
#include "omdata.h"
#include "overmapbuffer.h"
#include "overmapbuffer_registry.h"
namespace {

/// Format time for display.
std::string time_approx()
{
    const int iHour = hour_of_day<int>( calendar::turn );
    if( iHour >= 23 || iHour <= 1 ) {
        return _( "Around midnight" );
    } else if( iHour <= 4 ) {
        return _( "Dead of night" );
    } else if( iHour <= 6 ) {
        return _( "Around dawn" );
    } else if( iHour <= 8 ) {
        return _( "Early morning" );
    } else if( iHour <= 10 ) {
        return _( "Morning" );
    } else if( iHour <= 13 ) {
        return _( "Around noon" );
    } else if( iHour <= 16 ) {
        return _( "Afternoon" );
    } else if( iHour <= 18 ) {
        return _( "Early evening" );
    } else if( iHour <= 20 ) {
        return _( "Around dusk" );
    }
    return _( "Night" );
}

std::string format_time( const avatar &u )
{
    if( u.has_watch() ) {
        return to_string_time_of_day( calendar::turn );
    } else if( g->get_levz() >= 0 ) {
        return time_approx();
    }
    return "???";
}

/// Format weather for display.
std::string format_weather( const avatar & )
{
    if( g->get_levz() < 0 ) {
        return colorize( _( "Underground" ), c_light_gray );
    }
    return colorize( get_weather().weather_id->name.translated(), get_weather().weather_id->color );
}

/// Format location for display.
std::string format_location( const avatar &u )
{
    const oter_id &cur_ter = ACTIVE_OVERMAP_BUFFER.ter( u.abs_omt_pos() );
    return colorize( cur_ter->get_name(), c_white );
}

/// Format light level for display.
std::string format_light( const avatar &u )
{
    const auto &[text, color] = get_light_level(
        character_funcs::fine_detail_vision_mod( u ) );
    return colorize( text, color );
}

/// Format date for display.
std::string format_date( const avatar & )
{
    return string_format( "%s, %s",
                          calendar::name_season( season_of_year( calendar::turn ) ),
                          string_format( "day %d", day_of_season<int>( calendar::turn ) + 1 ) );
}

} // namespace

bool hud_top_bar::open()
{
    return floating_panel::open( PATH_INFO::datadir() + "gui/hud/top_bar.rml", "top_bar" );
}

void hud_top_bar::close()
{
    floating_panel::close();
}

void hud_top_bar::create_data_model( Rml::DataModelConstructor &c )
{
    Rml::StructHandle<top_bar_model> h = c.RegisterStruct<top_bar_model>();
    h.RegisterMember( "time_rml", &top_bar_model::time_rml );
    h.RegisterMember( "weather_rml", &top_bar_model::weather_rml );
    h.RegisterMember( "location_rml", &top_bar_model::location_rml );
    h.RegisterMember( "light_rml", &top_bar_model::light_rml );
    h.RegisterMember( "date_rml", &top_bar_model::date_rml );
    c.Bind( "data", &model );
}

void hud_top_bar::sync_data( avatar &u )
{
    model.time_rml = cata_text_to_rml( format_time( u ) );
    model.weather_rml = cata_text_to_rml( format_weather( u ) );
    model.location_rml = cata_text_to_rml( format_location( u ) );
    model.light_rml = cata_text_to_rml( format_light( u ) );
    model.date_rml = cata_text_to_rml( format_date( u ) );
}

void hud_top_bar::update_position()
{
    if( doc == nullptr ) {
        return;
    }

    Rml::Element *root = doc->GetElementById( root_element_id() );
    if( root == nullptr ) {
        return;
    }

    // Full width at top of screen
    root->SetProperty( "left", "0%" );
    root->SetProperty( "top", "0%" );
    root->SetProperty( "width", "100%" );
    root->SetProperty( "height", "auto" );
}

auto hud_top_bar::root_element_id() const -> std::string
{
    return "top-bar-panel";
}
