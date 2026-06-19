#pragma once
#ifndef CATA_SRC_RML_SCREEN_H
#define CATA_SRC_RML_SCREEN_H

// F.3 — per-screen RmlUi migration harness (full UI→RmlUi migration plan).
//
// `rml_doc` owns the boilerplate lifecycle every migrated modal screen repeats
// identically (proven byte-for-byte across missions/scores/help/distraction):
// the single-instance guard, data-model creation, document load, the 16ms input
// tick, and teardown ordering. Centralizing those correctness-critical bits is
// the point — screens 5–45 can't each re-introduce a subtle guard/teardown bug.
//
// What stays in the screen (irreducibly screen-specific, NOT shared): the
// data-model struct, its type registration, the variable Binds + event
// callbacks (all done in the `bind` lambda passed to open()), and the
// sync-on-redraw that DirtyVariables the model. There is intentionally no
// rml_sync() helper — sync has no shared content (cf. the plan's
// rml_open/rml_sync/rml_close; only open/close are shared).
//
// The 3 already-committed screens are deliberately NOT retrofitted onto this
// (the master plan forbids regressing eyeballed screens for no gain); they keep
// their inline boilerplate. New screens use rml_doc.
//
// Each migrated screen gets an OFF-by-default toggle so it can be A/B'd in
// isolation from the F4 dev panel; the whole toggle layer is deleted at the
// final curses rip-out.

#include <functional>
#include <string>

namespace Rml
{
class DataModelConstructor;
class ElementDocument;
} // namespace Rml
class input_context;

// Owns one per-screen RmlUi document + its data model. Move-free, single-owner;
// declare it as a local in the screen's draw function alongside the screen's own
// data-model `unique_ptr`. IMPORTANT: declare the data-model storage BEFORE this
// (so it outlives the document) and call close() while that data is still alive
// — RmlUi holds raw pointers into the bound members until RemoveDataModel.
class rml_doc
{
    public:
        // Open data/gui/<model_name>.rml bound to a fresh data model named
        // <model_name>. No-op returning false when `enabled` is false, RmlUi is
        // not ready, or a document with the same model_name is already open
        // (single-instance guard). `bind` runs after the model is created and
        // before the document loads: register structs, Bind members,
        // BindEventCallback, and capture c.GetModelHandle() into the screen's
        // session for later DirtyVariable calls. On success the 16ms tick is set
        // on `ctx` (so RmlUi hover/mouse-wheel stay live between keystrokes) and
        // the single-instance guard is taken — and it is taken ONLY on full
        // success, so every failure path leaves the guard clean (a stuck guard
        // would stop the screen from ever reopening).
        bool open( bool enabled, const std::string &model_name, input_context &ctx,
                   const std::function<void( Rml::DataModelConstructor & )> &bind );
        // Close the document, remove the data model, and release the guard.
        // Idempotent and a no-op when the curses path ran (nothing opened). Safe
        // to call on any exit path (including an early return); the destructor
        // also calls it as a safety net.
        void close();
        bool active() const {
            return doc_ != nullptr;
        }
        explicit operator bool() const {
            return doc_ != nullptr;
        }
        Rml::ElementDocument *document() const {
            return doc_;
        }

        rml_doc() = default;
        ~rml_doc();
        rml_doc( const rml_doc & ) = delete;
        rml_doc &operator=( const rml_doc & ) = delete;

    private:
        std::string model_name_;
        Rml::ElementDocument *doc_ = nullptr;
};

// ── Per-screen enable toggles ────────────────────────────────────────────────
// (definitions live next to each screen; defaults OFF — opt in via the F4 panel)

// game::list_missions() RmlUi render path.
bool &missions_rmlui_enabled();

// show_scores_ui() RmlUi render path (achievements/scores/kills tabs).
bool &scores_rmlui_enabled();

// help::display_help() RmlUi render path (topic menu + scrolling article).
bool &help_rmlui_enabled();

// distraction_manager_gui::show() RmlUi render path (toggle-list + description).
bool &distraction_rmlui_enabled();

// auto_note_manager_gui::show() RmlUi render path (toggle-list; first rml_doc user).
bool &auto_note_rmlui_enabled();

// diary::show_diary_ui() RmlUi render path (4-pane: pages/changes/text/info).
bool &diary_rmlui_enabled();

// show_mutations_ui() RmlUi render path (Tier 2 #1: 2-column active/passive grid).
bool &mutations_rmlui_enabled();

// show_bionics_ui() RmlUi render path (Tier 2 #2: tabs + list + examine pane).
bool &bionics_rmlui_enabled();

// safemode::show() RmlUi render path (Tier 2 #3: tabs + 5-column rules table).
bool &safemode_rmlui_enabled();

// auto_pickup user_interface::show() RmlUi render path (Tier 2 #4: tabs + rules).
bool &autopickup_rmlui_enabled();

// computer_session::use() RmlUi render path (Tier 2 #5: terminal text pane).
bool &computer_rmlui_enabled();

// construction_menu() RmlUi render path (Tier 2 #6: tabs + list + detail buffer).
bool &construction_rmlui_enabled();

// select_crafting_recipe() RmlUi render path (Tier 2: 2 tab rows + list + 2 info panes).
bool &crafting_rmlui_enabled();

// show_armor_layers_ui() RmlUi render path (Tier 2: 4-pane sort-armor).
bool &armor_layers_rmlui_enabled();

// examine_item_menu::run() RmlUi render path (Tier 3 entry: item-info component +
// action list). First consumer of rml_util::item_info_rml_lines.
bool &examine_item_rmlui_enabled();

// inventory_selector framework RmlUi path (Tier 3, sliced). Gated per selector
// subclass via inventory_selector::uses_rml(); slice 1 lights inventory_pick_selector.
bool &inventory_rmlui_enabled();

// game_menus::inv::compare() RmlUi path (Tier 3 follower: the two-pane item-info
// comparison display; first compare-delta consumer of item_info_rml_lines).
bool &compare_items_rmlui_enabled();

// advanced_inventory::display() RmlUi path (Tier 3 sub-project: dual-pane AIM).
// One toggle lights the whole doc; the work is sliced (slice 1 = dual item lists).
bool &advanced_inv_rmlui_enabled();

// options_manager::show() RmlUi path (Tier 4 screen #1: tabbed two-column form +
// tooltip). Slice 1 lights standalone mode only (world_options_only stays curses).
bool &options_rmlui_enabled();

// worldfactory RmlUi path (Tier 4 screen #2, sliced). One toggle lights all
// worldfactory docs, gated per-screen (slice 1 = the Finalize wizard step).
bool &worldfactory_rmlui_enabled();

// main_menu::opening_screen() RmlUi path (Tier 4 screen #3: the title screen).
bool &main_menu_rmlui_enabled();

// main_menu::load_character_tab() RmlUi path: the load/character-select list, where
// each saved character is a slot decorated with its own seeded bindrune sigil.
bool &loadchar_rmlui_enabled();

// avatar::create() new-character creator RmlUi path (Tier 4 screen #4, sliced).
// One toggle lights all 8 character-creation tabs, gated per-tab (slice 1 = the
// POINTS tab, set_points).
bool &newcharacter_rmlui_enabled();

// trading_window::perform_trade() RmlUi path (Tier 5: the NPC trade screen —
// dual item panes + credit/debt head + per-pane stats + item-info pane).
bool &trade_rmlui_enabled();

// iexamine::vending() RmlUi path (Tier 5: the vending-machine screen — item list
// + item-info pane).
bool &vending_rmlui_enabled();

// dialogue::opt() RmlUi path (Tier 5: the NPC dialogue window — history pane +
// lettered response list + keybind hints).
bool &dialogue_rmlui_enabled();

// overmap_ui::display() RmlUi path (Tier 6 slice 1: the overmap legend sidebar —
// tile description + keybind hints + coordinates. The map tile grid stays GPU/ASCII).
bool &overmap_rmlui_enabled();

// §7 world-space text layer (Tier 6 slice 4): on-map scrolling combat text rendered
// through RmlUi's own font engine instead of the curses overlay_strings path. The
// rip-out-surviving glyph path; foundation for future floating damage numbers.
bool &world_text_rmlui_enabled();

// Examine-tile description view (game::extended_description): the creature /
// furniture / terrain extended_description() text pane. Render-only RmlUi doc.
bool &description_view_rmlui_enabled();

// Faction manager (faction_manager::display): 4-tab list+detail screen
// (followers / factions / lore / creatures). Render-only RmlUi doc.
bool &faction_rmlui_enabled();

// Ranged targeting panel (target_ui::run → the w_target side panel). Slice 2a:
// shallow sections; aim/hit-chance readout WIP. Render-only RmlUi doc.
bool &ranged_rmlui_enabled();

// Sidebar HUD (Tier 7): the continuous every-turn sidebar panels (game::draw_panels).
// UNLIKE every screen above this is NOT a modal open/loop/close — it is a persistent
// HUD document opened once during gameplay and synced each turn (see sidebar_hud_*
// in panels.h/.cpp; it does NOT use rml_doc, which bundles a modal input tick). Slice 1
// owns only the Stats panel. Render-only; default OFF.
bool &sidebar_hud_rmlui_enabled();

#endif // CATA_SRC_RML_SCREEN_H
