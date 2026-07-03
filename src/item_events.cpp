// Item lifecycle event handlers: on_wear, on_takeoff, on_wield, on_unwield,
// on_pickup, on_damage, on_map_placement, etc.
// — split out of item.cpp. .cpp-only, no API changes.

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

// File-scope id constants (moved with event methods; internal linkage).
static const activity_id ACT_PICKUP("ACT_PICKUP");
static const std::string flag_LIQUIDCONT("LIQUIDCONT");

void item::on_wear(Character& who) {
    if (is_sided() && get_side() == side::BOTH) {
        if (has_flag(flag_SPLINT)) {
            set_side(side::LEFT);
            if ((covers(bodypart_id("leg_l")) && who.is_limb_broken(bodypart_id("leg_r"))
                 && !who.worn_with_flag(flag_SPLINT, bodypart_id("leg_r")))
                || (covers(bodypart_id("arm_l")) && who.is_limb_broken(bodypart_id("arm_r"))
                    && !who.worn_with_flag(flag_SPLINT, bodypart_id("arm_r")))) {
                set_side(side::RIGHT);
            }
        } else if (has_flag(flag_POWERARMOR_MOD)) {
            // for power armor mods, wear on side with least mods
            std::vector<std::pair<bodypart_str_id, int>> mod_parts;
            int lhs = 0;
            int rhs = 0;
            const auto& all_bps = who.get_all_body_parts();
            for (const bodypart_id& bp : all_bps) {
                if (get_covered_body_parts().test(bp.id())) { mod_parts.emplace_back(bp, 0); }
            }
            for (auto& elem : who.worn) {
                for (std::pair<bodypart_str_id, int>& mod_part : mod_parts) {
                    const bodypart_str_id& bp = mod_part.first;
                    if (elem->get_covered_body_parts().test(bp)
                        && elem->has_flag(flag_POWERARMOR_MOD)) {
                        if (elem->is_sided() && elem->get_side() == bp->part_side) {
                            mod_part.second++;
                            continue;
                        }
                        mod_part.second++;
                    }
                }
            }
            for (std::pair<bodypart_str_id, int>& mod_part : mod_parts) {
                const bodypart_str_id& bp = mod_part.first;
                if (bp->part_side == side::LEFT && mod_part.second > lhs) {
                    lhs = mod_part.second;
                } else if (bp->part_side == side::RIGHT && mod_part.second > rhs) {
                    rhs = mod_part.second;
                }
            }
            set_side((lhs > rhs) ? side::RIGHT : side::LEFT);
        } else {
            // for sided items wear the item on the side which results in least encumbrance
            const auto& all_bps = who.get_all_body_parts();
            int lhs = 0;
            int rhs = 0;
            set_side(side::LEFT);
            const char_encumbrance_data left_enc = who.get_encumbrance();
            for (const bodypart_id& bp : all_bps) {
                if (get_covered_body_parts().test(bp.id())) {
                    lhs += left_enc.elems.at(bp.id()).encumbrance;
                }
            }

            set_side(side::RIGHT);
            const char_encumbrance_data right_enc = who.get_encumbrance();
            for (const bodypart_id& bp : all_bps) {
                if (get_covered_body_parts().test(bp.id())) {
                    rhs += right_enc.elems.at(bp.id()).encumbrance;
                }
            }

            set_side(lhs <= rhs ? side::LEFT : side::RIGHT);
        }
    }

    if (type->can_use("set_transformed")) {
        bool transform = false;
        const set_transformed_iuse* actor = dynamic_cast<const set_transformed_iuse*>(
            this->get_use("set_transformed")->get_actor_ptr());
        if (actor == nullptr) {
            debugmsg("iuse_actor type descriptor and actual type mismatch");
            return;
        }
        flag_id transform_flag(actor->dependencies);
        for (const auto& elem : who.worn) {
            if (elem->has_flag(transform_flag) && elem->is_active() != is_active()) {
                transform = true;
            }
        }
        if (transform && actor->restricted) {
            actor->bypass(*who.as_player(), *this, false, who.bub_pos());
        }
    }

    // TODO: artifacts currently only work with the player character
    if (&who == &get_avatar() && type->artifact) {
        g->add_artifact_messages(type->artifact->effects_worn);
    }
    // if game is loaded - don't want ownership assigned during char creation
    if (get_avatar().getID().is_valid()) { handle_pickup_ownership(who); }
    who.on_item_wear(*this);

    if (type->iwearable_callbacks) { type->iwearable_callbacks->call_on_wear(who, *this); }
}

void item::on_takeoff(Character& who) {
    who.on_item_takeoff(*this);

    if (is_sided()) { set_side(side::BOTH); }

    // if power armor, no power_draw and active, shut down.
    if (type->can_use("set_transformed") && is_active()) {
        const set_transformed_iuse* actor = dynamic_cast<const set_transformed_iuse*>(
            this->get_use("set_transformed")->get_actor_ptr());
        if (actor == nullptr) {
            debugmsg("iuse_actor type descriptor and actual type mismatch");
            return;
        }
        actor->bypass(*who.as_player(), *this, false, who.bub_pos());
    }

    if (type->iwearable_callbacks) { type->iwearable_callbacks->call_on_takeoff(who, *this); }
}

void item::on_wield(player& p, int mv) {
    // TODO: artifacts currently only work with the player character
    if (&p == &get_avatar() && type->artifact) {
        g->add_artifact_messages(type->artifact->effects_wielded);
    }

    // weapons with bayonet/bipod or other generic "unhandiness"
    if (has_flag(flag_SLOW_WIELD) && !is_gunmod()) {
        float d = 32.0; // arbitrary linear scaling factor
        if (is_gun()) {
            d /= std::max(p.get_skill_level(gun_skill()), 1);
        } else if (is_melee()) {
            d /= std::max(p.get_skill_level(melee_skill()), 1);
        }

        int penalty = get_var("volume", volume() / units::legacy_volume_factor) * d;
        p.moves -= penalty;
        mv += penalty;
    }

    // firearms with a folding stock or tool/melee without collapse/retract iuse
    if (has_flag(flag_NEEDS_UNFOLD) && !is_gunmod()) {
        int penalty = 50; // 200-300 for guns, 50-150 for melee, 50 as fallback
        if (is_gun()) {
            penalty = std::max(0, 300 - p.get_skill_level(gun_skill()) * 10);
        } else if (is_melee()) {
            penalty = std::max(0, 150 - p.get_skill_level(melee_skill()) * 10);
        }

        p.moves -= penalty;
        mv += penalty;
    }

    std::string msg;

    if (mv > 500) {
        msg = _("It takes you a long time to wield your %s.");
    } else if (mv > 250) {
        msg = _("It takes you several seconds to wield your %s.");
    } else if (mv > 100) {
        msg = _("It takes you a couple seconds to wield your %s.");
    } else if (mv > 50) {
        msg = _("It takes you a moment to wield your %s.");
    } else {
        msg = _("You wield your %s.");
    }
    // if game is loaded - don't want ownership assigned during char creation
    if (p.getID().is_valid()) { handle_pickup_ownership(p); }
    p.add_msg_if_player(m_neutral, msg, tname());

    if (!p.martial_arts_data->selected_is_none()) {
        p.martial_arts_data->martialart_use_message(p);
    }

    // Update encumbrance in case we were wearing it
    p.flag_encumbrance();

    if (type->iwieldable_callbacks) { type->iwieldable_callbacks->call_on_wield(p, *this, mv); }
}

void item::on_unwield(Character& who) {
    if (type->iwieldable_callbacks) { type->iwieldable_callbacks->call_on_unwield(who, *this); }
}


void item::on_pickup(Character& who) {
    // Fake characters are used to determine pickup weight and volume
    if (who.is_fake()) { return; }
    avatar& you = get_avatar();
    // TODO: artifacts currently only work with the player character
    if (&who == &you && type->artifact) {
        g->add_artifact_messages(type->artifact->effects_carried);
    }
    // if game is loaded - don't want ownership assigned during char creation
    if (you.getID().is_valid()) { handle_pickup_ownership(who); }
    if (is_bucket_nonempty()) { contents.spill_contents(who.bub_pos()); }

    who.flag_encumbrance();

    if (type->istate_callbacks) { type->istate_callbacks->call_on_pickup(who, *this); }
}

void item::on_contents_changed() {
    if (is_non_resealable_container()) { convert(type->container->unseals_into); }

    encumbrance_update_ = true;
}

void item::on_damage(int qty, damage_type) {
    if (is_corpse() && qty + damage_ >= max_damage()) { set_flag(flag_PULPED); }

    if (type->iequippable_callbacks) {
        type->iequippable_callbacks
            ->call_on_durability_change(get_avatar(), *this, damage_, damage_ + qty);
    }
}

void item::on_map_placement(const map& m, const tripoint_bub_ms& p) {

    // TODO: Move to reveal_map_actor
    if (is_map() && !has_var("reveal_map_center_omt")) {
        set_var("reveal_map_center_omt", project_to<coords::omt>(m.bub_to_abs(p)));
    }

    for (const auto& func : type->use_methods | std::views::values) {
        const auto actor = func.get_actor_ptr();
        if (actor != nullptr) { actor->on_placed(*this, m, p); }
    }
}

bool item::on_drop(const tripoint_bub_ms& pos) { return on_drop(pos, get_map()); }

bool item::on_drop(const tripoint_bub_ms& pos, map& m) {
    avatar& you = get_avatar();

    if (type->istate_callbacks) {
        bool prevented = type->istate_callbacks->call_on_drop(you, *this, pos);
        if (prevented) { return true; }
    }

    // dropping liquids, even currently frozen ones, on the ground makes them
    // dirty
    if (made_of(LIQUID) && !m.has_flag(flag_LIQUIDCONT, pos) && !has_own_flag(flag_DIRTY)) {
        set_flag(flag_DIRTY);
    }
    you.flag_encumbrance();

    return type->drop_action && type->drop_action.call(you, *this, false, pos);
}
