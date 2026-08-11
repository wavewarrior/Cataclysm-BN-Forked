#include "rml_toggle_registry.h"

#include "rml_screen.h"
#include "ui.h" // uilist / query_popup / string_input toggles live here

// Single source of truth. Order mirrors the F4 dev panel's old hand-written bind
// list (sdl_lighting_devui.cpp devui_rml_open). default_on matches each toggle's
// static initializer — only sidebar_hud defaults ON (Tier 7 Phase-1 MVP flip).
const std::vector<rml_toggle> &rml_toggle_registry()
{
    static const std::vector<rml_toggle> reg = {
        {"uilist", &uilist_rmlui_enabled, true},
        {"query_popup", &query_popup_rmlui_enabled, false},
        {"string_input", &string_input_rmlui_enabled, false},
        {"missions", &missions_rmlui_enabled, false},
        {"scores", &scores_rmlui_enabled, false},
        {"help", &help_rmlui_enabled, false},
        {"distraction", &distraction_rmlui_enabled, false},
        {"auto_note", &auto_note_rmlui_enabled, false},
        {"diary", &diary_rmlui_enabled, false},
        {"mutations", &mutations_rmlui_enabled, false},
        {"bionics", &bionics_rmlui_enabled, false},
        {"character_display", &character_display_rmlui_enabled, false},
        {"messages", &messages_rmlui_enabled, false},
        {"morale", &morale_rmlui_enabled, false},
        {"martialarts", &martialarts_rmlui_enabled, false},
        {"pickup", &pickup_rmlui_enabled, false},
        {"veh_interact", &veh_interact_rmlui_enabled, false},
        {"gamemode_defense", &gamemode_defense_rmlui_enabled, false},
        {"list_monsters", &list_monsters_rmlui_enabled, false},
        {"list_items", &list_items_rmlui_enabled, false},
        {"zones_manager", &zones_manager_rmlui_enabled, false},
        {"panel_adm", &panel_adm_rmlui_enabled, false},
        {"live_view", &live_view_rmlui_enabled, false},
        {"look_around", &look_around_rmlui_enabled, false},
        {"loading", &loading_rmlui_enabled, false},
        {"inventory", &inventory_rmlui_enabled, false},
        {"advanced_inv", &advanced_inv_rmlui_enabled, false},
        {"compare_items", &compare_items_rmlui_enabled, false},
        {"examine_item", &examine_item_rmlui_enabled, false},
        {"armor_layers", &armor_layers_rmlui_enabled, false},
        {"autopickup", &autopickup_rmlui_enabled, false},
        {"computer", &computer_rmlui_enabled, false},
        {"construction", &construction_rmlui_enabled, false},
        {"crafting", &crafting_rmlui_enabled, false},
        {"safemode", &safemode_rmlui_enabled, false},
        {"trade", &trade_rmlui_enabled, false},
        {"vending", &vending_rmlui_enabled, false},
        {"dialogue", &dialogue_rmlui_enabled, false},
        {"description_view", &description_view_rmlui_enabled, false},
        {"faction", &faction_rmlui_enabled, false},
        {"ranged", &ranged_rmlui_enabled, false},
        {"options", &options_rmlui_enabled, false},
        {"worldfactory", &worldfactory_rmlui_enabled, false},
        {"main_menu", &main_menu_rmlui_enabled, false},
        {"loadchar", &loadchar_rmlui_enabled, false},
        // Must stay ON: the newcharacter tabs' curses drawing was deleted during the
        // migration, so "off" selects a renderer that no longer exists and the tabs
        // paint nothing while still consuming input.
        {"newcharacter", &newcharacter_rmlui_enabled, true},
        {"overmap", &overmap_rmlui_enabled, false},
        {"world_text", &world_text_rmlui_enabled, false},
        {"overmap_text", &overmap_text_rmlui_enabled, false},
        {"sidebar_hud", &sidebar_hud_rmlui_enabled, true},
        {"minigames", &minigames_rmlui_enabled, false},
        {"editmap", &editmap_rmlui_enabled, true},
        {"death_rip", &death_rip_rmlui_enabled, true},
        {"string_editor", &string_editor_rmlui_enabled, true},
        {"lua_console", &lua_console_rmlui_enabled, true},
        {"error_prompt", &error_prompt_rmlui_enabled, true},
    };
    return reg;
}

void rml_toggles_set_all( bool on )
{
    for( const rml_toggle& t : rml_toggle_registry() ) { t.accessor() = on; }
}

void rml_toggles_reset_defaults()
{
    for( const rml_toggle& t : rml_toggle_registry() ) { t.accessor() = t.default_on; }
}
