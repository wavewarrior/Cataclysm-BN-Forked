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

namespace overmap_ui
{
// Colored-text overmap minichunk (RmlUi HUD "map" panel): `height` rows of
// colorize()-tagged single-glyph cells, `width` wide, centred on `global_omt`.
// Curses-window-free successor to the removed draw_overmap_chunk.
auto overmap_chunk_rows( const avatar &you, const tripoint_abs_omt &global_omt, int width,
                         int height ) -> std::vector<std::string>;
} // namespace overmap_ui

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

// ── Sidebar HUD → RmlUi (Tier 7, render-only, continuous) ────────────────────
// The persistent HUD document that renders sidebar panels through RmlUi instead of
// the curses cell renderer. Lives in panels.cpp so it can reuse the TU-static stat
// colour helpers (str_string/etc.). Lifecycle is driven from game::draw_panels (NOT
// the modal rml_doc harness — the HUD has no blocking input loop):
//   open()  — lazily create the "sidebar_hud" data model + open the doc (no-op when
//             disabled / RmlUi not ready / already open). Idempotent.
//   sync()  — repopulate the bound model from the avatar each turn. No-op if closed.
//   close() — close the doc + remove the model. Idempotent; call on toggle-off and
//             on leaving gameplay (game::cleanup_at_end) so the HUD never lingers
//             over the main menu.
//   owns_panel(name) — true while the HUD is live AND has taken over the panel named
//             `name`; draw_panels skips that panel's curses draw to avoid double-draw.
//   position(name,left%,top%,width%) — place the named owned panel's HUD fragment at
// Tier 7 sidebar HUD (slice 3 structural pivot): a persistent, render-only RmlUi
// document = ONE flex column owning the WHOLE sidebar region. sidebar_hud_open() opens it
// lazily; sidebar_hud_sync() rebuilds the row list (one per present panel — migrated
// producer RML or a "[name]" placeholder) + repositions the container at the sidebar rect
// every turn; sidebar_hud_close() tears it down. sidebar_hud_active() is true while the
// doc is open → game::draw_panels suppresses the entire curses sidebar.
void sidebar_hud_open();
void sidebar_hud_sync( avatar &u );
void sidebar_hud_close();
bool sidebar_hud_active();
// True iff panel `name` has an RmlUi producer (else the HUD shows a [name] placeholder).
bool sidebar_hud_has_producer( const std::string &name );
// One-line audit: "sidebar HUD coverage: C/T panels [— uncovered: …]" over the active layout.
// The mechanical Tier-10 rip-out gate ("every panel in my UI built?").
std::string sidebar_hud_coverage_report();
// Rows of standard-font cells reserved above/below the viewport for the Qud HUD
// chrome strips. 0 whenever the RmlUi HUD can't render (curses fallback keeps the
// full viewport).
int sidebar_hud_top_rows();    // = 2 when the HUD can render, else 0
int sidebar_hud_bottom_rows(); // = 1 when the HUD can render, else 0

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

