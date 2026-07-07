#include "hud_floating_panel.h"

#include <algorithm>
#include <string>

#include "avatar.h"
#include "lighting/rmlui_layer.h"
floating_panel::~floating_panel()
{
    close();
}

bool floating_panel::open( const std::string &rml_path, const std::string &model_name )
{
    if( doc != nullptr ) {
        return true;  // already open (idempotent)
    }
    if( !rmlui_layer::ready() ) {
        return false;
    }

    Rml::Context *ctx = rmlui_layer::context();
    if( ctx == nullptr ) {
        return false;
    }

    // Create and register the data model
    Rml::DataModelConstructor c = ctx->CreateDataModel( model_name );
    if( !c ) {
        return false;
    }

    // Let derived class register its data model
    create_data_model( c );

    // Open the RML document (passive = render-only, doesn't capture input)
    doc = rmlui_layer::open_document( rml_path, true );
    if( doc == nullptr ) {
        ctx->RemoveDataModel( model_name );
        return false;
    }

    state = fade_state::active;
    current_opacity = config.active_opacity;
    frames_since_activity = 0;

    return true;
}

void floating_panel::close()
{
    if( doc != nullptr ) {
        rmlui_layer::close_document( doc );
        doc = nullptr;
    }
    // Data model is cleaned up by the context when the document closes
}

void floating_panel::update( avatar &u )
{
    if( doc == nullptr ) {
        return;
    }

    // Sync game state to data model
    sync_data( u );

    // Update fade animation
    update_fade();

    // Update position
    update_position();
}

void floating_panel::activity_occurred()
{
    frames_since_activity = 0;
    if( state != fade_state::active ) {
        state = fade_state::active;
        current_opacity = config.active_opacity;
        apply_opacity();
    }
}

auto floating_panel::fade_config() const -> const floating_panel_fade_config &
{
    return config;
}

void floating_panel::set_fade_config( const floating_panel_fade_config &cfg )
{
    config = cfg;
}

auto floating_panel::document() const -> Rml::ElementDocument *
{
    return doc;
}

auto floating_panel::is_open() const -> bool
{
    return doc != nullptr;
}

void floating_panel::update_fade()
{
    if( state == fade_state::active ) {
        // Check if we should start fading
        frames_since_activity++;
        if( frames_since_activity >= config.fade_delay_frames ) {
            state = fade_state::fading;
            frames_since_activity = 0; // Reset for fade progress tracking
        }
    } else if( state == fade_state::fading ) {
        // Continue fading
        frames_since_activity++;
        double progress = std::min( 1.0, static_cast<double>( frames_since_activity ) / config.fade_duration_frames );

        // Interpolate opacity from active to idle
        current_opacity = config.active_opacity + ( config.idle_opacity - config.active_opacity ) * progress;
        apply_opacity();

        if( progress >= 1.0 ) {
            state = fade_state::idle;
            current_opacity = config.idle_opacity;
        }
    }
    // If idle, stay idle until activity_occurred() is called
}

void floating_panel::apply_opacity()
{
    if( doc == nullptr ) {
        return;
    }

    Rml::Element *root = doc->GetElementById( root_element_id() );
    if( root != nullptr ) {
    root->SetProperty( "opacity", std::to_string( current_opacity ) );
    }
}

void floating_panel::update_position()
{
    // Default: no-op. Derived classes override to set position/size.
}

auto floating_panel::root_element_id() const -> std::string
{
    return "panel";
}
