#include "hud_health_stamina.h"

#include <string>

#include "avatar.h"
#include "color.h"
#include "lighting/rmlui_layer.h"
#include "rml_util.h"
#include "string_formatter.h"
#include "path_info.h"
namespace {

/// Create a color-coded bar string for RML.
/// @param current Current value.
/// @param max Maximum value.
/// @param bar_width Number of characters for the bar.
/// @return Color-coded bar string.
std::string create_bar( int current, int max, int bar_width = 50 )
{
    if( max <= 0 ) {
        return colorize( std::string( bar_width, '=' ), c_red );
    }

    int filled = ( current * bar_width ) / max;
    filled = std::max( 0, std::min( bar_width, filled ) );

    // Choose color based on percentage
    double perc = static_cast<double>( current ) / max;
    nc_color color;
    if( perc > 0.6 ) {
        color = c_green;
    } else if( perc > 0.3 ) {
        color = c_yellow;
    } else {
        color = c_red;
    }

    std::string bar;
    bar.reserve( bar_width );
    for( int i = 0; i < bar_width; i++ ) {
        if( i < filled ) {
            bar += colorize( "█", color );
        } else {
            bar += colorize( "░", c_dark_gray );
        }
    }
    return bar;
}

/// Format health text.
std::string format_health_text( const avatar &u )
{
    int hp_cur = u.get_hp();
    int hp_max = u.get_hp_max();
    return colorize( string_format( "%d/%d", hp_cur, hp_max ), c_light_gray );
}

/// Format stamina text.
std::string format_stamina_text( const avatar &u )
{
    int stamina = u.get_stamina();
    int stamina_max = u.get_stamina_max();
    return colorize( string_format( "%d/%d", stamina, stamina_max ), c_light_gray );
}

} // namespace

bool hud_health_stamina::open()
{
    return floating_panel::open( PATH_INFO::datadir() + "gui/hud/health_stamina.rml", "health_stamina" );
}

void hud_health_stamina::close()
{
    floating_panel::close();
}

void hud_health_stamina::create_data_model( Rml::DataModelConstructor &c )
{
    Rml::StructHandle<health_stamina_model> h = c.RegisterStruct<health_stamina_model>();
    h.RegisterMember( "health_bar_rml", &health_stamina_model::health_bar_rml );
    h.RegisterMember( "health_text_rml", &health_stamina_model::health_text_rml );
    h.RegisterMember( "stamina_bar_rml", &health_stamina_model::stamina_bar_rml );
    h.RegisterMember( "stamina_text_rml", &health_stamina_model::stamina_text_rml );
    c.Bind( "data", &model );
}

void hud_health_stamina::sync_data( avatar &u )
{
    int hp_cur = u.get_hp();
    int hp_max = u.get_hp_max();
    int stamina = u.get_stamina();
    int stamina_max = u.get_stamina_max();

    model.health_bar_rml = cata_text_to_rml( create_bar( hp_cur, hp_max ) );
    model.health_text_rml = cata_text_to_rml( format_health_text( u ) );
    model.stamina_bar_rml = cata_text_to_rml( create_bar( stamina, stamina_max ) );
    model.stamina_text_rml = cata_text_to_rml( format_stamina_text( u ) );

    // Mark activity if values changed (could track this more efficiently)
    // For now, just mark activity every sync
    activity_occurred();
}

void hud_health_stamina::update_position()
{
    if( doc == nullptr ) {
        return;
    }

    Rml::Element *root = doc->GetElementById( root_element_id() );
    if( root == nullptr ) {
        return;
    }

    // Positioned just below the top bar
    root->SetProperty( "left", "0%" );
    root->SetProperty( "top", "24dp" ); // Below top bar
    root->SetProperty( "width", "100%" );
    root->SetProperty( "height", "auto" );
}

auto hud_health_stamina::root_element_id() const -> std::string
{
    return "health-stamina-panel";
}
