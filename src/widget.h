#pragma once

#include <set>
#include <string>
#include <vector>

#include "translations.h"
#include "type_id.h"

class JsonObject;

// Data-driven sidebar widget — CDDA widget-engine port, Stage 3 trim.
//
// Stage 3 scope is JSON loading + the "native" wrapper style only. A native
// widget delegates drawing to an existing draw_* sidebar function (the parity
// bridge, resolved in panels.cpp::make_native_widget_panel). This class
// therefore deliberately OMITS CDDA's value/expression machinery
// (widget_var/get_var_value, widget_custom_var, widget_clause) and the display::
// text helpers: BN has no dbl_or_var and no display.cpp. Those land in Stage 4+,
// at which point the widget_var enum + get_var_value are added here.
class widget
{
    public:
        widget_id id;
        bool was_loaded = false;

        // ── loaded fields (Stage 3 subset) ──────────────────────────────────
        // Display style. Stage 3 renders two: "native" (delegate to the draw_*
        // function named by _native) and "sidebar" (a layout container listing
        // child widget ids in _widgets). Other CDDA styles load but do not
        // render until the engine lands (Stage 5).
        std::string _style = "number";
        translation _label;
        int _width  = 0;
        int _height = 1;
        // For _style == "native": name of the existing draw_* sidebar function
        // to delegate to. Bound to a draw callback in panels.cpp.
        std::string _native;
        std::set<flag_id>      _flags;
        std::vector<widget_id> _widgets;

        // ── generic_factory API ─────────────────────────────────────────────
        static void load_widget( const JsonObject &jo, const std::string &src );
        void load( const JsonObject &jo, const std::string &src );
        void check() const;
        static void finalize_all();
        static void check_consistency();
        static void reset();
        static const std::vector<widget> &get_all();

        const widget_id &getId() const {
            return id;
        }
        bool has_flag( const flag_id &f ) const;
        bool has_flag( const std::string &f ) const;

        // ── accessors for the panels.cpp native bridge ──────────────────────
        const std::string &style()  const {
            return _style;
        }
        const std::string &native() const {
            return _native;
        }
        // Raw height — may be negative. window_panel treats -1/-2 as flex
        // sentinels (minimap height / fill remaining sidebar space), so the
        // bridge must pass it through UNCLAMPED for a native Log/Map to flex.
        int height() const {
            return _height;
        }
        int width() const {
            return _width;
        }
        // Display label (translated). NOT used as the window_panel name — that
        // key must stay an untranslated, stable string for save/load matching,
        // so the bridge keys panels on the widget id and applies _label when
        // rendering lands (Stage 6).
        const translation &label() const {
            return _label;
        }
};
