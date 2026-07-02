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
        bool open(
            bool enabled, const std::string& model_name, input_context& ctx,
            const std::function<void( Rml::DataModelConstructor & )> &bind );
        // Close the document, remove the data model, and release the guard.
        // Idempotent and a no-op when the curses path ran (nothing opened). Safe
        // to call on any exit path (including an early return); the destructor
        // also calls it as a safety net.
        void close();
        bool active() const { return doc_ != nullptr; }
        explicit operator bool() const { return doc_ != nullptr; }
        Rml::ElementDocument *document() const { return doc_; }

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

// §8.1 gate step-4 font straggler: the overmap city / note / center-info labels
// (cata_tiles::draw_om) routed off the curses SDL Font glyph path onto the §7
// RmlUi world-text layer. Same timing as the SCT feed (redraw cycle, pre-prepare).
// Default OFF.
bool &overmap_text_rmlui_enabled();

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

// Minigames (Tier 9): the 5 grid games (lightson/snake/sokoban/minesweeper/
// kitten) rendered through ONE shared char-grid RmlUi doc (minigame.rml). One
// toggle lights all five; each game's draw syncs the shared title/grid/footer
// model (see minigame_rml.h). Render-only; default OFF.
bool &minigames_rmlui_enabled();

// character_display::disp_info() RmlUi render path (the '@' character sheet:
// §8.1 gate-blocker backlog, biggest first). 3-column grid of 6 navigable tabs
// (stats/encumbrance/skills/traits/bionics/effects) + read-only speed panel +
// focus-tracking info pane + tip bar; diary multi-pane focus model, faction
// parallel-text producers (curses pristine). Render-only; default OFF.
bool &character_display_rmlui_enabled();

// Messages::display_messages() RmlUi render path (the full message-LOG screen,
// the ESC log: §8.1 gate-blocker backlog). A scrolling text pane — time column
// + msgtype-coloured text, one row per folded line; native scroll replaces the
// curses offset windowing. The transient FILTER overlay stays the Tier-0
// string_input_popup compositing on top. Render-only; default OFF.
bool &messages_rmlui_enabled();

// player_morale::display() RmlUi render path (the morale screen: §8.1
// gate-blocker backlog). Fixed title + Source/Value header, a scrolling list of
// morale sources (name + percent, coloured by sign, with positive/negative total
// caption rows), and a fixed bottom block (Total / Pain / Fatigue cap / Focus).
// Static for the view (model built once); native scroll. Render-only; default OFF.
bool &morale_rmlui_enabled();

// ma_style_callback::key() SHOW_DESCRIPTION RmlUi render path (the martial-arts
// style description popup: §8.1 gate-blocker backlog). A single scrolling text
// pane over the Tier-0 uilist style picker; colour-tagged writeup, native scroll.
// Render-only; default OFF.
bool &martialarts_rmlui_enabled();

// pickup.cpp pick_up_from_items() RmlUi render path (the item pickup menu: §8.1
// gate-blocker backlog). A multi-select list (hotkey + parent/pick mark + name,
// selected row highlighted) over a scrolling item-info pane, with a weight/volume
// header + footer hints. Render-only (keyboard owns marking/counts/filter; synced
// each frame); native scroll. Default OFF.
bool &pickup_rmlui_enabled();

// game::list_monsters RmlUi render path (the nearby-monster `m` list: §8.1
// gate-blocker backlog, track-A creature-info). A right-docked panel: header +
// counter, a scrolling list (attitude-category headers + creature rows with a
// name + HP-bar/attitude/distance cluster), the selected creature's info pane
// (Creature::print_info_text() — the shared monster/npc producer), and a footer.
// Render-only (keyboard owns nav / safemode / look / fire; synced each frame);
// native scroll. Default OFF.
bool &list_monsters_rmlui_enabled();

// game::list_items RmlUi render path (the nearby-items list, the `V` screen). A
// right-docked panel mirroring list_monsters: header (Items + active/total
// counter), a scrolling list (magenta category headers + item rows with a
// coloured name, an optional NEW! badge, and a distance/direction cluster), the
// selected item's info-title + scrolling item-info pane (rml_util::item_info_rml_lines),
// and a footer hint line. Render-only (keyboard owns nav / filter / priority /
// examine / compare / travel; synced each frame); native scroll replaces the
// curses calcStartPos windowing. Default OFF.
bool &list_items_rmlui_enabled();

// game::list_vehicles RmlUi render path (the nearby-vehicle list, V-screen tab 2).
// A right-docked panel mirroring list_monsters: header (Vehicles + active/total
// counter), a scrolling vehicle list (name left + distance/direction right), and
// the selected vehicle's detail info pane (speed/engine/wheels/status/cargo/leak
// as colour-tagged lines). Render-only; synced each frame. Default ON.
bool &list_vehicles_rmlui_enabled();

// game::zones_manager RmlUi render path (the `Y` zones screen). A right-docked
// panel mirroring list_items: header ("Zones manager"), a scrolling zone list
// (name / type / distance-direction / vehicle marker, the active row recoloured),
// the active zone's options block (key→value descriptions), and a multi-line
// shortcut footer (the <O> overlay / <G> submap-grid toggles live-coloured by
// state). Render-only (keyboard owns add/remove/enable/move/edit/overlay; the map
// cursor + zone overlay stay on the map path); synced each frame, hidden during
// the nested query_position look_around (mirrors the curses `show` gate).
// Default OFF.
bool &zones_manager_rmlui_enabled();

// panel_manager::show_adm RmlUi render path (the `}` SIDEBAR OPTIONS menu). A
// centered modal: a title, then three columns — the renderable-panel list (toggle
// on/off, reorder via swap-drag, the source row highlighted yellow during a
// move), a help/keys column, and the layout list (current layout in light_blue).
// 2D cursor (column + row): the active row in the active column gets the shared
// accent highlight. Render-only (keyboard owns nav / toggle / move / layout-switch).
// Default OFF.
bool &panel_adm_rmlui_enabled();

// live_view RmlUi render path (the SDL mouse-hover tile tooltip). A NON-modal,
// passive overlay box (no input loop): opened lazily when the hover box appears,
// fed each redraw by game::print_all_tile_info_text() (the same producer behind
// the migrated look_around info pane), positioned at the sidebar edge, and closed
// when the box hides. Uses the rmlui_layer doc lifecycle directly (not the modal
// rml_doc harness). Default OFF.
bool &live_view_rmlui_enabled();

// character_preview RmlUi render path (the new-character / preview portrait frame).
// The character itself is a GPU sprite; only the surrounding box + "CHARACTER
// PREVIEW" title were curses (draw_border). NON-modal passive backdrop with a
// TRANSPARENT centre (so the sprite shows through), positioned at the preview rect
// each redraw. rmlui_layer doc lifecycle (like live_view), not the modal rml_doc.
// Default OFF.
bool &character_preview_rmlui_enabled();

// game::look_around RmlUi render path (the examine/look-around info pane: §8.1
// track-A creature-info). Render-only doc fed by print_all_tile_info_text() (the
// parallel tile-readout producer; creature section reuses Creature::print_info_text()):
// title + cursor coords + the full tile info (terrain/fields/trap/creature/vehicle/
// items/graffiti) + footer hints. Map cursor + zone overlay stay on the map path.
// Default OFF.
bool &look_around_rmlui_enabled();

// editmap (game::look_debug, dev map editor) RmlUi render path: the w_info side
// panel renders as a passive backdrop doc (gui/editmap_info.rml). The map cursor /
// selection stay on the GPU map path; the editing menus are uilists. Default ON.
bool &editmap_rmlui_enabled();

// veh_interact RmlUi render path (the vehicle interaction screen: §8.1
// gate-blocker backlog, THE GIANT — migrated in slices). Slice 1 = lifecycle
// harness + the vehicle name + the action mode bar; later slices add stats /
// overview / part list / msg / the 2D diagram / install-repair sub-modes.
// Render-only; default OFF.
bool &veh_interact_rmlui_enabled();

// gamemode_defense RmlUi render path (the Defense game-mode screens: §8.1
// gate-blocker backlog). Slice 1 = the setup settings form (defense_game::setup /
// refresh_setup); slice 2 = the between-wave caravan shop. Render-only (keyboard
// owns every field/selection); synced each frame. Default OFF.
bool &gamemode_defense_rmlui_enabled();

// loading_ui progress list RmlUi render path (the data-load step list shown while
// the game loads). A bespoke loading document (gui/loading.rml): context title +
// progress bar + the step list with per-row done/current/pending state. Like the
// sidebar HUD it is NON-modal (no input_context / loop) — lazy open + sync each
// loading_ui::show() + close on destruction; does NOT use rml_doc (which bundles a
// modal input tick). Render-only: the uilist `menu` stays the state holder (entries
// / selected / green-done) and the doc reads it. When OFF or RmlUi is not yet ready
// (early data load, before first refresh_display) it falls back to menu->show().
// Default OFF.
bool &loading_rmlui_enabled();

// input_context::display_menu() RmlUi path (the keybindings editor — the legend +
// the windowed/filtered action list; the filter string_input stays Tier-0). The
// curses w_help draw is the toggle-OFF fallback. Default OFF.
bool &keybindings_rmlui_enabled();

// color_manager::show_gui() RmlUi path (the Colors editor — header + the
// name/Normal/Invert table; the color-pick uilists stay Tier-0). Curses fallback
// behind the toggle. Default OFF.
bool &color_manager_rmlui_enabled();

// scrollable_text() RmlUi path (the shared scroll-text popup primitive — title +
// the windowed folded body; keyboard keys still drive beg_line). Curses fallback
// behind the toggle. Default OFF.
bool &scrollable_text_rmlui_enabled();

// Character::conduct_blood_analysis() RmlUi path (the Blood Test Results popup —
// red-bordered title + the per-effect coloured line list). Curses fallback behind
// the toggle. Default OFF.
bool &blood_test_rmlui_enabled();

// game::cleanup_at_end() death/RIP screen RmlUi path (the post-death gravestone
// art + survival stats + player name + last-words string_input_popup on top).
// Default ON.
bool &death_rip_rmlui_enabled();

// string_editor_window::query_string() RmlUi render path (text editor for diary pages
// and Lua console EDIT action). Default ON.
bool &string_editor_rmlui_enabled();

// Default ON.
bool &lua_console_rmlui_enabled();

#endif // CATA_SRC_RML_SCREEN_H
