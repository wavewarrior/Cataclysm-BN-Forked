#pragma once
/// @file
/// Health and stamina bars HUD panel.
///
/// Displays full-width health and stamina bars beneath the top bar.
/// Shows current/max values with visual bar representation.

#include "hud_floating_panel.h"

#include <string>

namespace Rml {
    class DataModelConstructor;
}

class avatar;

/// Data model for the health/stamina bars panel.
struct health_stamina_model {
    /// Health bar RML (color-coded bar + text).
    std::string health_bar_rml;
    /// Health text (e.g., "150/200").
    std::string health_text_rml;
    /// Stamina bar RML (color-coded bar + text).
    std::string stamina_bar_rml;
    /// Stamina text (e.g., "80/100").
    std::string stamina_text_rml;
};

/// Health and stamina bars panel.
class hud_health_stamina : public floating_panel {
    public:
        bool open();
        void close() override;

    protected:
        void create_data_model( Rml::DataModelConstructor &c ) override;
        void sync_data( avatar &u ) override;
        void update_position() override;
        auto root_element_id() const -> std::string override;

    private:
        /// Data model storage.
        health_stamina_model model;
};
