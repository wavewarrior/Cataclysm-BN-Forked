#pragma once

#include <set>
#include <string>
#include <vector>

#include "translations.h"
#include "type_id.h"

class JsonObject;
class avatar;

// Value source for a "number"/"value" style widget. A trimmed BN-relevant subset
// of CDDA's widget_var — each maps to an existing avatar/Character getter in
// widget::get_var_value. `last` = unset / unsupported (renders 0). Expression
// (custom_var) and display:: text vars are intentionally excluded (BN lacks the
// deps); they remain native-wrapped.
enum class widget_var : int {
    last = 0,
    stat_str, stat_dex, stat_int, stat_per,
    pain, stamina, mana, max_mana, morale,
    thirst, fatigue, speed,
    // Body-graph color dimensions — not scalar; the body_graph renderer reads
    // these directly to pick which per-bp value colors the limb grid.
    body_graph, body_graph_temp, body_graph_encumb, body_graph_status, body_graph_wet,
};

// Data-driven sidebar widget — CDDA widget-engine port.
//
// Two render paths exist: "native" delegates drawing to an existing draw_*
// sidebar function (the parity bridge, panels.cpp::make_native_widget_panel);
// "number"/"value" is the data-driven renderer (panels.cpp::make_value_widget_panel)
// that draws "[icon] label: value" itself from _var via get_var_value, with an
// optional two-tone SVG icon. CDDA's expression machinery (widget_custom_var,
// widget_clause) and display:: text vars are deliberately omitted — BN lacks the
// deps; those widgets stay native-wrapped.
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
        // Optional show/hide predicate name (e.g. "spell_panel", "veh_panel"),
        // resolved to a window_panel render condition in panels.cpp. Empty =
        // always shown.
        std::string _show_if;
        // For value styles: the value source, and an optional two-tone SVG icon
        // name (gfx/widgets/<icon>.svg) drawn in the leading column.
        widget_var  _var = widget_var::last;
        std::string _icon;
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
        const std::string &show_if() const {
            return _show_if;
        }
        widget_var var() const {
            return _var;
        }
        const std::string &icon() const {
            return _icon;
        }
        // Resolve _var to its current integer value for `ava`. Returns 0 for
        // widget_var::last / unsupported.
        int get_var_value( const avatar &ava ) const;
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
