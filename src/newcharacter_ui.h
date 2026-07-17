#pragma once
#ifndef CATA_SRC_NEWCHARACTER_UI_H
#define CATA_SRC_NEWCHARACTER_UI_H

#include <optional>
#include <string>
#include <vector>

#include "type_id.h"

class avatar;
class Character;
class scenario;
struct points_left;

enum struct tab_direction {
    NONE,
    FORWARD,
    BACKWARD,
    QUIT
};

tab_direction set_points( avatar &u, points_left &points );
tab_direction set_stats( avatar &u, points_left &points );
tab_direction set_traits( avatar &u, points_left &points );
tab_direction set_bionics( avatar &u, points_left &points );
tab_direction set_scenario( avatar &u, points_left &points, tab_direction direction );
tab_direction set_profession( avatar &u, points_left &points, tab_direction direction );
tab_direction set_skills( avatar &u, points_left &points );
tab_direction set_description( avatar &you, bool allow_reroll, points_left &points );

auto query_for_template_name() -> std::optional<std::string>;
void reset_scenario( avatar &u, const scenario *scen );

auto has_conflicting_trait( const avatar &u, const trait_id &tid ) -> bool;
auto bionic_has_conflict( const avatar &u, const bionic_id &bio ) -> bool;
auto has_lower_trait( const avatar &u, const trait_id &tid ) -> bool;
auto has_higher_trait( const avatar &u, const trait_id &tid ) -> bool;
auto has_same_type_trait( const avatar &u, const trait_id &tid ) -> bool;

bool &newcharacter_rmlui_enabled();

#endif // CATA_SRC_NEWCHARACTER_UI_H
