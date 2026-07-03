// Item ownership, enchantments, book/chapter, gunmod locations, and
// brewing/revive helpers — split out of item.cpp. .cpp-only, no API changes.

#include "active_tile_data_def.h"
#include "ammo.h"
#include "ascii_art.h"
#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "cached_item_options.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "catalua_icallback_actor.h"
#include "character.h"
#include "character_encumbrance.h"
#include "character_functions.h"
#include "character_id.h"
#include "character_martial_arts.h"
#include "character_stat.h"
#include "cloning_utils.h"
#include "clothing_mod.h"
#include "clzones.h"
#include "color.h"
#include "craft_command.h"
#include "damage.h"
#include "debug.h"
#include "dispersion.h"
#include "drop_token.h"
#include "effect.h" // for weed_msg
#include "enums.h"
#include "explosion.h"
#include "faction.h"
#include "fault.h"
#include "field_type.h"
#include "fire.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "gun_mode.h"
#include "iexamine.h"
#include "int_id.h"
#include "inventory.h"
#include "item.h"
#include "item_category.h"
#include "item_factory.h"
#include "item_group.h"
#include "iteminfo_format_utils.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "line.h"
#include "locations.h"
#include "magic.h"
#include "magic_enchantment.h"
#include "map.h"
#include "martialarts.h"
#include "material.h"
#include "melee.h"
#include "messages.h"
#include "mod_manager.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "pimpl.h"
#include "player.h"
#include "player_activity.h"
#include "pldata.h"
#include "point.h"
#include "profile.h"
#include "projectile.h"
#include "ranged.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "relic.h"
#include "requirements.h"
#include "ret_val.h"
#include "rng.h"
#include "rot.h"
#include "scores_ui.h"
#include "skill.h"
#include "stomach.h"
#include "string_formatter.h"
#include "string_id_utils.h"
#include "string_utils.h"
#include "text_snippets.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "units_energy.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vitamin.h"
#include "vpart_position.h"
#include "weather.h"
#include "weather_gen.h"
#include "wheel_dimensions.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_set>

// File-scope id constants (internal linkage).
static const activity_id ACT_PICKUP("ACT_PICKUP");
static const std::string has_thievery_witness("has_thievery_witness");

bool item::is_owned_by(const Character& c, bool available_to_take) const {
    // owner.is_null() implies faction_id( "no_faction" ) which shouldn't happen, or no owner at
    // all. either way, certain situations this means the thing is available to take. in other
    // scenarios we actually really want to check for id == id, even for no_faction
    if (get_owner().is_null()) { return available_to_take; }
    if (!c.get_faction()) {
        debugmsg("Character %s has no faction", c.disp_name());
        return false;
    }
    return c.get_faction()->id == get_owner();
}

bool item::is_old_owner(const Character& c, bool available_to_take) const {
    if (get_old_owner().is_null()) { return available_to_take; }
    if (!c.get_faction()) {
        debugmsg("Character %s has no faction.", c.disp_name());
        return false;
    }
    return c.get_faction()->id == get_old_owner();
}

std::string item::get_owner_name() const {
    if (!g->faction_manager_ptr->get(get_owner())) {
        debugmsg("item::get_owner_name() item %s has no valid nor null faction id ", tname());
        return "no owner";
    }
    return g->faction_manager_ptr->get(get_owner())->name;
}

void item::set_owner(const Character& c) {
    if (!c.get_faction()) {
        debugmsg("item::set_owner() Character %s has no valid faction", c.disp_name());
        return;
    }
    owner = c.get_faction()->id;
}

faction_id item::get_owner() const {
    validate_ownership();
    return owner;
}

faction_id item::get_old_owner() const {
    validate_ownership();
    return old_owner;
}

void item::validate_ownership() const {
    if (!old_owner.is_null() && !g->faction_manager_ptr->get(old_owner, false)) {
        remove_old_owner();
    }
    if (!owner.is_null() && !g->faction_manager_ptr->get(owner, false)) { remove_owner(); }
}


bool item::is_money() const { return ammo_types().contains(ammotype("money")); }

bool item::count_by_charges() const { return type->count_by_charges(); }

int item::count() const { return count_by_charges() ? charges : 1; }

bool item::craft_has_charges() {
    if (count_by_charges()) {
        return true;
    } else if (ammo_types().empty()) {
        return true;
    }

    return false;
}


double item::bonus_from_enchantments(
    const Character& owner, double base, enchant_vals::mod value, bool round) const {
    double add = 0.0;
    double mul = 0.0;
    for (const enchantment& ench : get_enchantments()) {
        if (ench.is_active(owner, *this)) {
            add += ench.get_value_add(value);
            mul += ench.get_value_multiply(value);
        }
    }
    // TODO: this part duplicates enchantment::calc_bonus()
    double ret = add + base * mul;
    if (round) { ret = trunc(ret); }
    return ret;
}

double item::bonus_from_enchantments_wielded(
    double base, enchant_vals::mod value, bool round) const {
    double add = 0.0;
    double mul = 0.0;
    for (const enchantment& ench : get_enchantments()) {
        if (ench.is_active_when_wielded()) {
            add += ench.get_value_add(value);
            mul += ench.get_value_multiply(value);
        }
    }
    // TODO: this part duplicates enchantment::calc_bonus()
    double ret = add + base * mul;
    if (round) { ret = trunc(ret); }
    return ret;
}

const std::vector<relic_recharge>& item::get_relic_recharge_scheme() const {
    return relic_data->get_recharge_scheme();
}

bool item::can_contain(const item& it) const {
    // TODO: Volume check
    return can_contain(*it.type);
}

bool item::can_contain(const itype& tp) const {
    if (!type->container) {
        // TODO: Tools etc.
        return false;
    }

    if (tp.phase == LIQUID && !type->container->watertight) { return false; }

    // TODO: Acid in waterskins
    return true;
}

const item& item::get_contained() const {
    if (contents.empty()) { return null_item_reference(); }
    return contents.front();
}


void item::handle_pickup_ownership(Character& c) {
    if (is_owned_by(c)) { return; }
    // Add ownership to item if unowned
    if (owner.is_null()) {
        set_owner(c);
    } else {
        Character& you = get_player_character();
        if (!is_owned_by(c) && &c == &you) {
            std::vector<npc*> witnesses;
            for (npc& elem : g->all_npcs()) {
                // If they already want to murder you, no point in confronting you about theft
                if (rl_dist(elem.bub_pos(), you.bub_pos()) < g_max_view_distance
                    && elem.get_faction() && is_owned_by(elem) && elem.sees(you.bub_pos())
                    && !elem.guaranteed_hostile()) {
                    elem.say("<witnessed_thievery>", 7);
                    npc* npc_to_add = &elem;
                    witnesses.push_back(npc_to_add);
                }
            }
            if (!witnesses.empty()) {
                set_old_owner(get_owner());
                // Make sure there is only one witness
                for (npc& guy : g->all_npcs()) {
                    if (guy.get_attitude() == NPCATT_RECOVER_GOODS) {
                        guy.set_attitude(NPCATT_NULL);
                    }
                }
                random_entry(witnesses)->set_attitude(NPCATT_RECOVER_GOODS);
                // Notify the activity that we got a witness
                if (c.activity && !c.activity->is_null() && c.activity->id() == ACT_PICKUP) {
                    c.activity->str_values.clear();
                    c.activity->str_values.emplace_back(has_thievery_witness);
                }
            }
            set_owner(c);
        }
    }
}
