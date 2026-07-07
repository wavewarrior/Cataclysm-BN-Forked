#pragma once
/// @file
/// Reusable floating HUD panel base class with fade animations.
///
/// Each HUD panel (top_bar, health_stamina, message_log, minimap) inherits from
/// this base and implements its own data model + sync logic. The base class
/// handles fade animations, positioning, and RML document lifecycle.
///
/// Fade behavior: panels transition between active (full opacity) and idle
/// (reduced opacity) based on activity. When activity occurs, the panel
/// immediately becomes fully opaque. After N frames of inactivity, it fades
/// to the idle opacity over M frames.

#include <RmlUi/Core.h>
#include <memory>
#include <optional>
#include <string>


class avatar;

/// Configuration for a floating panel's fade behavior.
struct floating_panel_fade_config {
    /// Opacity when panel is active (default: fully opaque).
    double active_opacity = 1.0;
    /// Opacity when panel is idle (default: semi-transparent).
    double idle_opacity = 0.3;
    /// Frames of inactivity before starting fade (default: 60 = ~1 second).
    int fade_delay_frames = 60;
    /// Frames to complete the fade animation (default: 30 = ~0.5 seconds).
    int fade_duration_frames = 30;
};

/// Base class for floating HUD panels with fade animations.
///
/// Derived classes implement:
/// - `create_data_model()`: Register and bind the panel's data model
/// - `sync_data()`: Update the data model with current game state
/// - `activity_occurred()`: Call when the panel has new content/activity
///
/// The base class handles:
/// - RML document lifecycle (open/close)
/// - Fade animation (opacity transitions)
/// - Position updates (called each frame)
class floating_panel {
    public:
        virtual ~floating_panel();

        /// Open the panel's RML document and data model.
        /// Returns true if successful.
        bool open( const std::string &rml_path, const std::string &model_name );

        /// Close the panel's RML document and clean up.
        virtual void close();

        /// Update the panel: sync data, update fade, update position.
        /// Call this each frame while the panel is visible.
        void update( avatar &u );

        /// Mark that activity has occurred on this panel.
        /// Resets the fade timer and sets opacity to active.
        void activity_occurred();

        /// Get the current fade configuration.
        auto fade_config() const -> const floating_panel_fade_config &;

        /// Set the fade configuration.
        void set_fade_config( const floating_panel_fade_config &config );

        /// Get the RML document element (for position updates).
        auto document() const -> Rml::ElementDocument *;

        /// Check if the panel is open and active.
        auto is_open() const -> bool;

    protected:
        /// Derived classes implement this to register their data model.
        /// Called during open(). Must bind all data model members.
        virtual void create_data_model( Rml::DataModelConstructor &c ) = 0;

        /// Derived classes implement this to sync game state to the data model.
        /// Called each frame during update().
        virtual void sync_data( avatar &u ) = 0;

        /// Update the panel's position/size. Called each frame.
        /// Override to customize positioning.
        virtual void update_position();

        /// Get the ID of the root element to position (default: "panel").
        virtual auto root_element_id() const -> std::string;

        /// Update fade animation state. Called each frame.
        void update_fade();

        /// Apply current opacity to the RML document.
        void apply_opacity();

        /// Current fade state.
        enum class fade_state {
            active,      /// Fully opaque, no fade in progress
            fading,      /// Currently fading to idle
            idle,        /// At idle opacity
        };

        /// RML document handle.
        Rml::ElementDocument *doc = nullptr;

        /// Current fade state.
        fade_state state = fade_state::active;

        /// Frames since last activity.
        int frames_since_activity = 0;

        /// Current opacity value (interpolated during fade).
        double current_opacity = 1.0;

        /// Fade configuration.
        floating_panel_fade_config config;
};
