#pragma once
/// @file
/// HUD manager for floating panels.
///
/// Manages the lifecycle of all floating HUD panels (top bar, health/stamina,
/// message log, minimap). Opens panels when gameplay starts, closes them when
/// gameplay ends, and updates them each frame.

#include <memory>
#include <vector>

#include "hud_floating_panel.h"

class avatar;

// Forward declarations for panel types
class hud_top_bar;
class hud_health_stamina;
class hud_message_log;
class hud_minimap;

/// Manager for all floating HUD panels.
class hud_manager {
    public:
        /// Singleton access.
        static auto instance() -> hud_manager &;

        /// Delete copy/move.
        hud_manager( const hud_manager & ) = delete;
        auto operator=( const hud_manager & ) -> hud_manager & = delete;

        /// Open all HUD panels. Call when gameplay starts.
        void open_all();

        /// Close all HUD panels. Call when gameplay ends.
        void close_all();

        /// Update all HUD panels. Call each frame during gameplay.
        void update( avatar &u );

        /// Check if any panels are open.
        auto is_active() const -> bool;

        /// Get a specific panel (for external access if needed).
        auto get_top_bar() -> hud_top_bar *;
        auto get_health_stamina() -> hud_health_stamina *;
        auto get_message_log() -> hud_message_log *;
        auto get_minimap() -> hud_minimap *;

    private:
        hud_manager();
        ~hud_manager();

        /// Panel pointers.
        std::unique_ptr<hud_top_bar> top_bar;
        std::unique_ptr<hud_health_stamina> health_stamina;
        std::unique_ptr<hud_message_log> message_log;
        std::unique_ptr<hud_minimap> minimap;
};
