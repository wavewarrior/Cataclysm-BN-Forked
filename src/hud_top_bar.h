#pragma once
/// @file
/// Top bar HUD panel - compact status information.
///
/// Displays: time, weather, location, light level, date
/// Positioned at the top of the screen, full width.

#include "hud_floating_panel.h"

#include <string>

namespace Rml {
    class DataModelConstructor;
}

class avatar;

/// Data model for the top bar panel.
struct top_bar_model {
    std::string time_rml;
    std::string weather_rml;
    std::string location_rml;
    std::string light_rml;
    std::string date_rml;
};

/// Top bar panel with compact status info.
class hud_top_bar : public floating_panel {
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
        top_bar_model model;
};
