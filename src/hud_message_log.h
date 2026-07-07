#pragma once
/// @file
/// Message log HUD panel.
///
/// Displays recent game messages in a floating panel on the right side.
/// Fades to transparent when no new messages for a while.

#include "hud_floating_panel.h"

#include <string>
#include <vector>

namespace Rml {
    class DataModelConstructor;
}

class avatar;

/// Single message row for the data model.
struct message_row {
    std::string text_rml;
};

/// Data model for the message log panel.
struct message_log_model {
    std::vector<message_row> rows;
};

/// Message log panel with fade behavior.
class hud_message_log : public floating_panel {
    public:
        bool open();
        void close() override;

        /// Check for new messages and mark activity if found.
        /// Call this each frame to detect new messages.
        void check_for_new_messages();

    protected:
        void create_data_model( Rml::DataModelConstructor &c ) override;
        void sync_data( avatar &u ) override;
        void update_position() override;
        auto root_element_id() const -> std::string override;

    private:
        /// Data model storage.
        message_log_model model;

        /// Last known message count (for detecting new messages).
        int last_message_count = 0;
};
