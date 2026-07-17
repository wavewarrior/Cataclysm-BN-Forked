#pragma once
#ifndef CATA_SRC_RANGED_INTERNAL_H
#define CATA_SRC_RANGED_INTERNAL_H

#include <string>
#include <vector>

#include "bodypart.h"
#include "coordinates.h"
#include "creature.h"
#include "item.h"
#include "projectile.h"
#include "dispersion.h"
#include "units_angle.h"

class avatar;
class Character;
class input_context;
class player;

// Helpers shared between ranged.cpp and ranged_target_ui.cpp.
// Defined in ranged.cpp.
auto occupied_tile_fraction( creature_size target_size ) -> double;
auto do_aim( avatar& you, const item& relevant, const double min_recoil ) -> void;
auto outside_visible_z_range( const tripoint_bub_ms& from, const tripoint_bub_ms& to ) -> bool;
auto pl_sees( const Creature& cr ) -> bool;
auto calculate_aim_cap( const Character& p, const tripoint_bub_ms& target ) -> double;
auto aim_lines( const Character& p, int bar_width, input_context& ctxt, item& weapon,
                const double target_size, const tripoint_bub_ms& pos, double predicted_recoil,
                item* load_loc ) -> std::vector<std::string>;
auto throw_aim_lines( const player& p, int bar_width, input_context& ctxt, const item& weapon,
                      const tripoint_bub_ms& target_pos,
                      bool is_blind_throw ) -> std::vector<std::string>;
auto make_gun_projectile( const item& gun ) -> projectile;
auto calculate_dispersion( const map& m, const Character& who, const item& gun,
                           int at_recoil, bool burst ) -> dispersion_sources;

#endif // CATA_SRC_RANGED_INTERNAL_H
