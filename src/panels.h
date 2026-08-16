#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "coordinates.h"

class JsonIn;
class JsonOut;
class avatar;
struct point;
struct tripoint;

namespace catacurses
{
class window;
} // namespace catacurses
enum face_type : int {
    face_human = 0,
    face_bird,
    face_bear,
    face_cat,
    num_face_types
};

enum moon_phase : int;

// (name, icon-id) per lunar phase — untranslated; callers translate `name` (mirrors
// hud_moon in panels.cpp, and how g_hud_producers keeps `title` untranslated until
// sync time).
struct moon_phase_info {
    const char *name;
    const char *icon;
};
auto moon_phase_display( moon_phase phase ) -> moon_phase_info;
// 8-way sector icon bucket for a wind direction angle (RMLUI_HUD_PANEL_REFERENCE.md §3.12).
auto wind_arrow_icon( int dirangle ) -> const char *;

bool default_render();

class widget;

class window_panel
{
    public:
        window_panel( std::function<void( avatar &, const catacurses::window & )> draw_func,
                      const std::string &nm, int ht, int wd, bool default_toggle_,
                      std::function<bool()> render_func = default_render, bool force_draw = false );

        std::function<void( avatar &, const catacurses::window & )> draw;
        std::function<bool()> render;
        // Optional content-driven height. When set, get_height() returns this instead of the
        // static height, letting a panel shrink/grow to its actual rendered content.
        std::function<int()> dynamic_height;
        // RmlUi HUD content producer bound to this panel instance (value/bodygraph
        // widgets know their own widget id; the name-keyed g_hud_producers table can't).
        // Checked FIRST by sidebar_hud_sync. Output is colorize()-tagged text unless
        // hud_raw, then it is ready RML.
        std::function<std::string( avatar & )> hud_produce;
        bool hud_raw = false;

        int get_height() const;
        int get_width() const;
        std::string get_name() const;
        bool toggle;
        bool always_draw;

    private:
        int height;
        int width;
        bool default_toggle;
        std::string name;
};

// Build a window_panel for a "native"-style widget by delegating its draw to the
// existing draw_* sidebar function named by widget::native(). This is the parity
// bridge for the widget-engine port (Stage 3): a JSON widget can reference an
// existing panel by id and render identically, without value/var wiring. An
// unknown target yields a panel that just erases its window (logged once) — never
// crashes.
window_panel make_native_widget_panel( const widget &w, int width );
// Build a window_panel for a "number"/"value" style widget: hud_produce renders
// "label  value" (bar+percent when bounded, else the raw number) from the
// widget's _var. Icons are a Phase-4 SVG concern, not this text row.
window_panel make_value_widget_panel( const widget &w, int width );
// Build a window_panel for a "body_graph" style widget: hud_produce renders one
// row per main body part, coloring each by the widget's body_graph* dimension
// (hp/temp/encumb/status/wet — see bodygraph_bp_color in panels.cpp).
window_panel make_bodygraph_widget_panel( const widget &w, int width );

// ── Sidebar HUD → RmlUi: the chassis ─────────────────────────────────────────
// One persistent, render-only RmlUi document drawing the whole HUD in the
// character creator's register (plans/hud-creator-register.md). panels.cpp owns
// only the chassis — the seven-string data model, the document lifecycle, the
// per-turn sync and the dp geometry; every producer lives in
// hud_runic_panels.cpp (soma, dock) and hud_runic_strips.cpp (status, log, keys,
// vehicle), over the primitives in hud_runic.h.
//
// Lifecycle is driven from game::draw_panels — NOT the modal rml_doc harness,
// because the HUD has no blocking input loop:
//   open()   — lazily create the "sidebar_hud" data model + open the doc (no-op
//              when disabled / RmlUi not ready / already open). Idempotent.
//   sync()   — rebuild the seven bound strings from the avatar and re-place every
//              region. No-op if closed.
//   close()  — close the doc + remove the model. Idempotent; call on toggle-off
//              and on leaving gameplay (game::cleanup_at_end) so the HUD never
//              lingers over the main menu.
//   active() — true while the doc is open → game::draw_panels suppresses the
//              entire curses sidebar.
void sidebar_hud_open();
void sidebar_hud_sync( avatar &u );
void sidebar_hud_close();
bool sidebar_hud_active();
// Expand/collapse the SOMA panel's limb card, persisting the choice in
// `uistate.hud_soma_expanded`. Bound to ACTION_TOGGLE_SOMA_DETAIL; safe to call
// with the HUD closed, in which case it only records the preference.
auto sidebar_hud_toggle_soma_detail() -> void;
// Rows of standard-font (`fontheight`) cells reserved above and below the terrain
// viewport for the HUD's two OPAQUE strips — the status strip and the function-key
// strip. The translucent regions (soma, dock, log, vehicle) float over the terrain
// and are deliberately NOT carved. 0 whenever the HUD can't render.
//
// The HUD measures in dp, the terrain in `fontheight` pixels, so these convert:
// `ceil( strip_h_dp * dp_ratio / fontheight )`, rounded up so the carve always
// covers the strip.
int sidebar_hud_top_rows();
int sidebar_hud_bottom_rows();

// Tick HUD animations (advance tweens, apply CSS properties). Called each render frame.
auto sidebar_hud_anim_tick() -> void;
class panel_manager
{
    public:
        panel_manager();
        ~panel_manager() = default;
        panel_manager( panel_manager && ) = default;
        panel_manager( const panel_manager & ) = default;
        panel_manager &operator=( panel_manager && ) = default;
        panel_manager &operator=( const panel_manager & ) = default;

        static panel_manager &get_manager() {
            static panel_manager single_instance;
            return single_instance;
        }

        std::vector<window_panel> &get_current_layout();
        std::string get_current_layout_id() const;
        // True if a layout with this id (built-in or widget-built) is registered.
        bool has_layout( const std::string &id ) const;
        int get_width_right();
        int get_width_left();

        void show_adm();

        void init();
        // (Re)build selectable layouts from data-driven "sidebar" widgets. Must be
        // called AFTER world modfiles load (widget JSON), since init() runs in
        // load_static_data before any mod data exists.
        void reload_widget_layouts();
        auto sync_lua_panels() -> void;

    private:
        bool save();
        bool load();
        void serialize( JsonOut &json );
        void deserialize( JsonIn &jsin );
        // update the screen offsets so the game knows how to adjust the main window
        void update_offsets( int x );

        // The amount of screen space from each edge the sidebar takes up
        int width_right = 0;
        int width_left = 0;
        std::string current_layout_id;
        std::map<std::string, std::vector<window_panel>> layouts;
        std::set<std::string> lua_panel_names;

};

