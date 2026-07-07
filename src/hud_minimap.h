#pragma once
/// @file
/// Minimap HUD panel.
///
/// Displays a minimap in a floating panel on the right side, below the message log.
/// This is a placeholder - the actual minimap rendering will be added later.

#include "hud_floating_panel.h"

#include <string>

namespace Rml {
    class DataModelConstructor;
}

class avatar;

/// Data model for the minimap panel.
struct minimap_model {
    /// Minimap content RML (placeholder for now).
    std::string minimap_rml;
};

/// Minimap panel.
class hud_minimap : public floating_panel {
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
        minimap_model model;
};
