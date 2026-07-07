#include "hud_minimap.h"

#include <string>

#include "avatar.h"
#include "color.h"
#include "lighting/rmlui_layer.h"
#include "rml_util.h"
#include "path_info.h"
namespace {

/// Create a placeholder minimap display.
/// This will be replaced with actual minimap rendering later.
std::string create_minimap_placeholder( const avatar & )
{
    // For now, just show a placeholder
    return colorize( "[Minimap - TODO: implement RTT rendering]", c_dark_gray );
}

} // namespace

bool hud_minimap::open()
{
    return floating_panel::open( PATH_INFO::datadir() + "gui/hud/minimap.rml", "minimap" );
}

void hud_minimap::close()
{
    floating_panel::close();
}

void hud_minimap::create_data_model( Rml::DataModelConstructor &c )
{
    Rml::StructHandle<minimap_model> h = c.RegisterStruct<minimap_model>();
    h.RegisterMember( "minimap_rml", &minimap_model::minimap_rml );
    c.Bind( "data", &model );
}

void hud_minimap::sync_data( avatar &u )
{
    model.minimap_rml = cata_text_to_rml( create_minimap_placeholder( u ) );
}

void hud_minimap::update_position()
{
    if( doc == nullptr ) {
        return;
    }

    Rml::Element *root = doc->GetElementById( root_element_id() );
    if( root == nullptr ) {
        return;
    }

    // Right side panel, below message log
    root->SetProperty( "right", "2%" );
    root->SetProperty( "top", "calc(60dp + 40% + 10dp)" ); // Below message log
    root->SetProperty( "width", "30%" );
    root->SetProperty( "height", "30%" );
}

auto hud_minimap::root_element_id() const -> std::string
{
    return "minimap-panel";
}
