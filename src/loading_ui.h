#pragma once

#include <chrono>
#include <memory>
#include <string>

class ui_adaptor;
class uilist;

class loading_ui
{
    private:
        std::unique_ptr<uilist> menu;
        std::unique_ptr<ui_adaptor> ui;
        /// When the last frame was presented; drives show()'s rate limit.
        std::chrono::steady_clock::time_point last_draw_{};

        void init();
        /// Update the menu on screen. `force` bypasses the rate limit.
        void draw( bool force );
    public:
        loading_ui( bool display );
        ~loading_ui();

        /**
         * Sets the description for the menu and clears existing entries.
         */
        void new_context( const std::string &desc );
        /**
         * Adds a named entry in the current loading context.
         */
        void add_entry( const std::string &description );
        /**
         * Place the UI onto UI stack, mark current entry as processed, scroll down,
         * and redraw. (if display is enabled)
         */
        void proceed();
        /**
         * Place the UI onto UI stack and redraw it on the screen (if display is
         * enabled).
         *
         * Rate-limited: presenting a frame costs 20-35ms (the full lighting +
         * composite pipeline plus an RmlUi relayout of the loading document) and the
         * loaders call this thousands of times per world load, so an unthrottled
         * loading screen spends the whole load presenting instead of loading.
         */
        void show();
};
