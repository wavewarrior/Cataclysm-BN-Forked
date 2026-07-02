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


std::map<gunmod_location, int> item::get_mod_locations() const {
    std::map<gunmod_location, int> mod_locations = type->gun->valid_mod_locations;

    for (const item* mod : gunmods()) {
        if (!mod->type->gunmod->add_mod.empty()) {
            std::map<gunmod_location, int> add_locations = mod->type->gunmod->add_mod;

            for (const std::pair<const gunmod_location, int>& add_location : add_locations) {
                mod_locations[add_location.first] += add_location.second;
            }
        }
    }

    return mod_locations;
}

int item::get_free_mod_locations(const gunmod_location& location) const {
    if (!is_gun()) { return 0; }

    std::map<gunmod_location, int> mod_locations = get_mod_locations();

    const auto loc = mod_locations.find(location);
    if (loc == mod_locations.end()) { return 0; }
    int result = loc->second;
    for (const item* elem : contents.all_items_top()) {
        const cata::value_ptr<islot_gunmod>& mod = elem->type->gunmod;
        if (mod && mod->location == location) { result--; }
    }
    return result;
}


time_duration item::brewing_time() const { return is_brewable() ? type->brewable->time : 0_turns; }

const std::vector<itype_id>& item::brewing_results() const {
    static const std::vector<itype_id> nulresult{};
    return is_brewable() ? type->brewable->results : nulresult;
}

bool item::can_revive() const {
    return is_corpse() && corpse->has_flag(MF_REVIVES) && damage() < max_damage()
        && !(has_flag(flag_FIELD_DRESS) || has_flag(flag_FIELD_DRESS_FAILED)
             || has_flag(flag_QUARTERED) || has_flag(flag_SKINNED) || has_flag(flag_PULPED));
}

bool item::ready_to_revive(const tripoint_bub_ms& pos) const {
    if (!can_revive()) { return false; }
    if (get_map().veh_at(pos)) { return false; }
    if (!calendar::once_every(1_seconds)) { return false; }
    int age_in_hours = to_hours<int>(age());
    age_in_hours -= static_cast<int>(static_cast<float>(burnt) / (volume() / 250_ml));
    if (damage_level(4) > 0) { age_in_hours /= (damage_level(4) + 1); }
    int rez_factor = 48 - age_in_hours;
    if (age_in_hours > 6 && (rez_factor <= 0 || one_in(rez_factor))) {
        // If we're a special revival zombie, wait to get up until the player is nearby.
        const bool isReviveSpecial = has_flag(flag_REVIVE_SPECIAL);
        if (isReviveSpecial) {
            const int distance = rl_dist(pos, get_player_character().bub_pos());
            if (distance > 3) { return false; }
            if (!one_in(distance + 1)) { return false; }
        }

        return true;
    }
    return false;
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

bool item::spill_contents(Character& c) {
    if (!is_container() || is_container_empty()) { return true; }

    if (c.is_npc()) { return spill_contents(c.bub_pos()); }

    contents.handle_liquid_or_spill(c);
    on_contents_changed();

    return true;
}

bool item::spill_contents(const tripoint_bub_ms& pos) {
    if (!is_container() || is_container_empty()) { return true; }

    for (detached_ptr<item>& it : contents.clear_items()) {
        get_map().add_item_or_charges(pos, std::move(it));
    }

    return true;
}

int item::get_chapters() const {
    if (!type->book) { return 0; }
    return type->book->chapters;
}

int item::get_remaining_chapters(const Character& ch) const {
    const std::string var = string_format("remaining-chapters-%d", ch.getID().get_value());
    return get_var(var, get_chapters());
}

void item::mark_chapter_as_read(const Character& ch) {
    const std::string var = string_format("remaining-chapters-%d", ch.getID().get_value());
    if (type->book && type->book->chapters == 0) {
        // books without chapters will always have remaining chapters == 0, so we don't need to
        // store them
        erase_var(var);
        return;
    }
    const int remain = std::max(0, get_remaining_chapters(ch) - 1);
    set_var(var, remain);
}

std::vector<std::pair<const recipe*, int>> item::get_available_recipes(const Character& u) const {
    std::vector<std::pair<const recipe*, int>> recipe_entries;
    if (is_book()) {
        for (const book_recipe& elem : type->book->recipes) {
            if (u.get_skill_level(elem.recipe->skill_used) >= elem.skill_level) {
                recipe_entries.emplace_back(elem.recipe, elem.skill_level);
            }
        }
    } else if (has_var("EIPC_RECIPES")) {
        // See einkpc_download_memory_card() in iuse.cpp where this is set.
        const std::string recipes = get_var("EIPC_RECIPES");
        // Capture the index one past the delimiter, i.e. start of target string.
        size_t first_string_index = recipes.find_first_of(',') + 1;
        while (first_string_index != std::string::npos) {
            size_t next_string_index = recipes.find_first_of(',', first_string_index);
            if (next_string_index == std::string::npos) { break; }
            std::string new_recipe =
                recipes.substr(first_string_index, next_string_index - first_string_index);
            const recipe* r = &recipe_id(new_recipe).obj();
            if (u.get_skill_level(r->skill_used) >= r->difficulty) {
                recipe_entries.emplace_back(r, r->difficulty);
            }
            first_string_index = next_string_index + 1;
        }
    }
    return recipe_entries;
}

const material_type& item::get_random_material() const {
    return random_entry(made_of(), material_id::NULL_ID()).obj();
}

const material_type& item::get_base_material() const {
    const std::vector<material_id>& mats = made_of();
    return mats.empty() ? material_id::NULL_ID().obj() : mats.front().obj();
}

bool item::operator<(const item& other) const {
    const item_category& cat_a = get_category();
    const item_category& cat_b = other.get_category();
    if (cat_a != cat_b) {
        return cat_a < cat_b;
    } else {
        const item* me = is_container() && !contents.empty() ? &contents.front() : this;
        const item* rhs =
            other.is_container() && !other.contents.empty() ? &other.contents.front() : &other;

        const itype* me_type = me->type;
        const itype* rhs_type = rhs->type;
        if (!me_type || !rhs_type) { return !!me_type; }

        if (me_type->get_id() == rhs_type->get_id()) {
            if (me->is_money()) { return me->charges > rhs->charges; }
            return me->charges < rhs->charges;
        } else {
            std::string n1 = me_type->nname(1);
            std::string n2 = rhs_type->nname(1);
            return localized_compare(n1, n2);
        }
    }
}
