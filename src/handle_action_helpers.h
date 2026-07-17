#pragma once
#ifndef CATA_SRC_HANDLE_ACTION_HELPERS_H
#define CATA_SRC_HANDLE_ACTION_HELPERS_H

#include <optional>
#include <string>

#include "coordinates.h"
#include "type_id.h"
#include "units.h"

class avatar;
class player;
class spell;
struct weather_printable;

namespace action_handlers
{

bool init_weather_anim( const weather_type_id& wtype, weather_printable& wPrint );
void generate_weather_anim_frame( const weather_type_id& wtype, weather_printable& wPrint );
void rcdrive( point_rel_ms d );
void pldrive( const tripoint_rel_veh& p );
void pldrive( point_rel_veh d );
void open();
void close();
void grab();
void haul();
void smash();
int try_set_alarm();
auto parse_custom_wait_duration( const std::string& value ) -> std::optional<time_duration>;
void wait();
void sleep();
void loot();
void wear();
void takeoff();
void read();
void reach_attack( avatar& you );
void fire();
void open_movement_mode_menu();
auto start_spellcasting_activity( player& u, spell& sp ) -> void;
auto try_cast_spell( player& u, spell& sp ) -> bool;
auto cast_spell() -> void;
auto cast_last_spell() -> void;

} // namespace action_handlers

#endif // CATA_SRC_HANDLE_ACTION_HELPERS_H
