#include "hud_message_log.h"

#include <string>
#include <vector>

#include "avatar.h"
#include "lighting/rmlui_layer.h"
#include "messages.h"
#include "rml_util.h"
#include "path_info.h"
bool hud_message_log::open()
{
    // Configure fade: fade after 2 seconds, take 1 second to fade
    config.fade_delay_frames = 120; // 2 seconds at 60fps
    config.fade_duration_frames = 60; // 1 second fade
    config.idle_opacity = 0.2; // Very transparent when idle

    return floating_panel::open( PATH_INFO::datadir() + "gui/hud/message_log.rml", "message_log" );
}

void hud_message_log::close()
{
    floating_panel::close();
}

void hud_message_log::create_data_model( Rml::DataModelConstructor &c )
{
    Rml::StructHandle<message_row> row_h = c.RegisterStruct<message_row>();
    row_h.RegisterMember( "text_rml", &message_row::text_rml );

    Rml::StructHandle<message_log_model> h = c.RegisterStruct<message_log_model>();
    h.RegisterMember( "rows", &message_log_model::rows );

    c.RegisterArray<Rml::Vector<message_row>>();
    c.Bind( "data", &model );
}

void hud_message_log::sync_data( avatar & )
{
    // Get recent messages
    auto recent = Messages::recent_messages( 20 );

    // Update model
    model.rows.clear();
    model.rows.reserve( recent.size() );

    for( const auto &[time, text] : recent ) {
        message_row row;
        row.text_rml = cata_text_to_rml( text );
        model.rows.push_back( std::move( row ) );
    }

    // Check if we have new messages
    int current_count = static_cast<int>( Messages::recent_messages( 1 ).size() );
    if( current_count > last_message_count ) {
        activity_occurred();
        last_message_count = current_count;
    }
}

void hud_message_log::update_position()
{
    if( doc == nullptr ) {
        return;
    }

    Rml::Element *root = doc->GetElementById( root_element_id() );
    if( root == nullptr ) {
        return;
    }

    // Right side panel, below top bar and health bars
    root->SetProperty( "right", "2%" );
    root->SetProperty( "top", "60dp" ); // Below top bar + health bars
    root->SetProperty( "width", "30%" );
    root->SetProperty( "height", "40%" );
}

auto hud_message_log::root_element_id() const -> std::string
{
    return "message-log-panel";
}

void hud_message_log::check_for_new_messages()
{
    // This can be called externally to force activity check
    int current_count = static_cast<int>( Messages::recent_messages( 1 ).size() );
    if( current_count > last_message_count ) {
        activity_occurred();
        last_message_count = current_count;
    }
}
