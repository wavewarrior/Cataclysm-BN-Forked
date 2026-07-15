#pragma once

#include <string>
#include <utility>
#include <vector>

#include "character_stat.h"

class Character;
class item;
class avatar;
class ui_adaptor;

namespace catacurses
{
class window;
} // namespace catacurses

namespace character_display
{

/**
 * Builds the per-bodypart encumbrance + warmth rows as colour-tagged strings
 * (one per row), for the RmlUi armor-layers pane and the '@' character sheet
 * encumbrance pane. RmlUi handles wrapping + scroll, so there is no curses
 * column positioning or scrollbar.
 */
std::vector<std::string> encumbrance_lines( const Character &ch,
        const item *selected_clothing = nullptr );

/**
 * Builds the (name, description) pairs for every effect/condition currently
 * affecting `ch` that the '@' screen's Effects tab shows: active effects,
 * perceived pain, starvation/BMI, TROGLO sunlight irritation, and active
 * addictions. Shared by disp_info() and the Qud HUD bottom strip (hud_botbar).
 */
std::vector<std::pair<std::string, std::string>> effect_name_and_text( const Character &ch );

/**
 * @brief Handles and displays detailed character info for the '@' screen.
 *
 * @param ch Character to display info for. Has to be non-const reference
 * to allow toggling skills and upgrading stats for stats-through-x mods.
 */
void disp_info( Character &ch );

/**
 * Handles upgrade of avatar stats.
 */
void upgrade_stat_prompt( avatar &you, const character_stat &stat );

/** Gets the minimum combined bare-handed damage from skill, bionics, and mutations for display functions */
int display_empty_handed_base_damage( const Character &you );

} // namespace character_display



