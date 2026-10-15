#include "map_spawn.h"

#include "avatar.h"
#include "catalua_hooks.h"
#include "catalua_sol.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "enums.h"
#include "game.h"
#include "game_constants.h"
#include "line.h"
#include "map.h"
#include "map_iterator.h"
#include "mapbuffer.h"
#include "memory_fast.h"
#include "messages.h"
#include "mission.h"
#include "mongroup.h"
#include "monster.h"
#include "overmapbuffer.h"
#include "player.h"
#include "point.h"
#include "profile.h"
#include "rng.h"
#include "submap.h"
#include "vehicle.h"
#include "vehicle_ownership.h"
#include "vpart_position.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <optional>
#include <vector>

#define dbg(x) DebugLog((x), DC::Map)

namespace {

auto horde_should_avoid_vehicle_tile(
    const map& here, const tripoint_bub_ms& p, const mongroup& group) -> bool {
    if (!group.horde) { return false; }

    const auto vp = here.veh_at(p);
    if (!vp) { return false; }

    const auto& veh = vp->vehicle();
    return vehicle_ownership::is_owned_by(veh, get_avatar());
}

auto bubble_submaps(const map& here) -> tripoint_range<tripoint_bub_sm> {
    const auto size = here.getmapsize();
    return {tripoint_bub_sm(0, 0, -OVERMAP_DEPTH),
            tripoint_bub_sm(size - 1, size - 1, OVERMAP_HEIGHT)};
}

auto reality_bubble_2D_bounds(const map& here) -> inclusive_rectangle<point_bub_sm> {
    const auto size = here.getmapsize();
    return {point_bub_sm::zero(), point_bub_sm(size - 1, size - 1)};
}

struct spawn_group_options {
    tripoint_bub_sm gp;
    mongroup& group;
    bool ignore_sight;
};

/// Spawns monsters on one submap from one group on this submap.
auto spawn_submap_group(map& here, spawn_group_options opts) -> void {
    const auto gp = opts.gp;
    auto& group = opts.group;
    auto ignore_sight = opts.ignore_sight;
    const auto map_size = here.getmapsize();
    const int s_range =
        std::min(g_half_mapsize_x, g->u.sight_range(g->light_level(g->u.bub_pos().z())));
    int pop = group.population;
    std::vector<tripoint_bub_ms> locations;
    if (!ignore_sight) {
        // If the submap is one of the outermost submaps, assume that monsters are
        // invisible there.
        if (gp.x() == 0 || gp.y() == 0 || gp.x() + 1 == map_size || gp.y() + 1 == map_size) {
            ignore_sight = true;
        }
    }

    if (gp.z() != g->u.bub_pos().z()) {
        // Note: this is only OK because 3D vision isn't a thing yet
        ignore_sight = true;
    }

    const auto allow_on_terrain = [&](const tripoint_bub_ms& p) {
        // TODO: flying creatures should be allowed to spawn without a floor,
        // but the new creature is created *after* determining the terrain, so
        // we can't check for it here.
        return here.passable(p) && here.has_floor(p);
    };

    // If the submap is uniform, we can skip many checks
    const auto current_submap = here.active_submap_view(map_local_to_abs(here, gp));
    if (!current_submap) { return; }
    bool ignore_terrain_checks = false;
    bool ignore_inside_checks = false;
    if (current_submap->get_submap().is_uniform) {
        const tripoint_bub_ms upper_left{SEEX * gp.x(), SEEY * gp.y(), gp.z()};
        if (!allow_on_terrain(upper_left)
            || (!ignore_inside_checks && !here.is_outside(upper_left))) {
            const auto glp = map_local_to_abs(here, gp);
            dbg(DL::Warn) << "Empty locations for group " << group.type.str()
                          << " at uniform submap " << gp << " global " << glp;
            return;
        }

        ignore_terrain_checks = true;
        ignore_inside_checks = true;
    }

    for (const auto sm_ms : submap_tiles()) {
        const auto fp = project_combine(gp, sm_ms);
        // If there is already a creature at this location, skip it
        if ((g->critter_at(fp) == nullptr) &&
            // Skip impassable terrain
            (ignore_terrain_checks || allow_on_terrain(fp)) &&
            // monster must spawn outside the viewing range of the player
            (ignore_sight || !here.sees(g->u.bub_pos(), fp, s_range)) &&
            // monster must spawn outside.
            (ignore_inside_checks || here.is_outside(fp)) &&
            // hordes must not appear inside player-owned vehicles.
            (!horde_should_avoid_vehicle_tile(here, fp, group))) {
            locations.push_back(fp);
        }
    }

    if (locations.empty()) {
        // TODO: what now? there is no possible place to spawn monsters, most
        // likely because the player can see all the places.
        const auto glp = map_local_to_abs(here, gp);
        dbg(DL::Warn) << "Empty locations for group " << group.type.str() << " at " << gp
                      << " global " << glp;
        // Just kill the group. It's not like we're removing existing monsters
        // Unless it's a horde - then don't kill it and let it spawn behind a tree or smoke cloud
        if (!group.horde) { group.clear(); }

        return;
    }

    if (pop) {
        // Populate the group from its population variable.
        for (int m = 0; m < pop; m++) {
            MonsterGroupResult spawn_details =
                MonsterGroupManager::GetResultFromGroup(group.type, &pop);
            if (!spawn_details.name) { continue; }
            monster tmp(spawn_details.name);

            // If a monster came from a horde population, configure them to always be willing to
            // rejoin a horde.
            if (group.horde) { tmp.set_horde_attraction(MHA_ALWAYS); }
            for (int i = 0; i < spawn_details.pack_size; i++) { group.monsters.push_back(tmp); }
        }
    }

    // Find horde's target submap
    // TODO: fix point types
    auto horde_target = project_to<coords::ms>(group.target);
    for (auto& tmp : group.monsters) {
        for (int tries = 0; tries < 10 && !locations.empty(); tries++) {
            const auto p = random_entry_removed(locations);
            if (!tmp.can_move_to(p)) {
                continue; // target can not contain the monster
            }
            if (group.horde) {
                // Give monster a random point near horde's expected destination
                const auto rand_dest =
                    abs_to_map_local(here, horde_target) + point_rel_ms(rng(0, SEEX), rng(0, SEEY));
                const int turns = rl_dist(p, rand_dest) + group.interest;
                tmp.wander_to(rand_dest, turns);
                add_msg(m_debug, "%s targeting %d,%d,%d", tmp.disp_name(), tmp.wander_pos.x(),
                        tmp.wander_pos.y(), tmp.wander_pos.z());
            }

            monster* const placed = g->place_critter_at(make_shared_fast<monster>(tmp), p);
            if (placed) { placed->on_load(); }
            break;
        }
    }
    // indicates the group is empty, and can be removed later
    group.clear();
}

/// Spawns monsters on one submap.
auto spawn_submap(map& here, const tripoint_bub_sm& gp, bool ignore_sight) -> void {
    const auto& dimension = here.get_bound_dimension();
    // Load unloaded monsters
    // TODO: fix point types
    get_overmapbuffer(dimension).spawn_monster(map_local_to_abs(here, gp));

    // Only spawn new monsters after existing monsters are loaded.
    // TODO: fix point types
    auto groups = get_overmapbuffer(dimension).groups_at(map_local_to_abs(here, gp));
    for (auto& mgp : groups) {
        spawn_submap_group(here, {.gp = gp, .group = *mgp, .ignore_sight = ignore_sight});
    }

    submap* const current_submap = here.get_mapbuffer().lookup_submap_in_memory(
        map_local_to_abs(here, gp));
    if (current_submap == nullptr) { return; }
    const auto gp_ms = project_to<coords::ms>(gp);

    for (auto& i : current_submap->spawns) {
        const auto center = gp_ms + i.pos.raw();
        const tripoint_range<tripoint_bub_ms> points = here.points_in_radius(center, 3);

        for (int j = 0; j < i.count; j++) {
            monster tmp(i.type);
            tmp.mission_id = i.mission_id;
            if (i.mission_id != -1) {
                mission* found_mission = mission::find(i.mission_id);
                if (found_mission != nullptr
                    && found_mission->get_type().goal == MGOAL_KILL_MONSTERS) {
                    found_mission->register_kill_needed();
                }
            }
            if (i.name != "NONE") { tmp.unique_name = i.name; }
            if (i.is_friendly()) { tmp.friendly = -1; }

            const auto valid_location = [&](const tripoint_bub_ms& p) {
                // Checking for creatures via g is only meaningful if this is the main game map.
                // If it's some local map instance, the coordinates will most likely not even match.
                return (!g || &get_map() != &here || !g->critter_at(p)) && tmp.can_move_to(p);
            };

            const auto place_it = [&](const tripoint_bub_ms& p) {
                monster* const placed = g->place_critter_at(make_shared_fast<monster>(tmp), p);
                if (placed) {
                    placed->on_load();
                    cata::run_hooks("on_creature_spawn", [&](sol::table& params) {
                        params["creature"] = placed;
                    });
                    cata::run_hooks("on_monster_spawn", [&](sol::table& params) {
                        params["monster"] = placed;
                    });
                    if (i.disposition == spawn_disposition::SpawnDisp_Pet) { placed->make_pet(); }
                }
            };

            // First check out defined spawn location for a valid placement, and if that doesn't
            // work then fall back to picking a random point that is a valid location.
            if (valid_location(center)) {
                place_it(center);
            } else if (
                const std::optional<tripoint_bub_ms> pos = random_point(points, valid_location)) {
                place_it(*pos);
            }
        }
    }
    current_submap->spawns.clear();
}

} // namespace

namespace map_spawn {

auto spawn_monsters(map& here, bool ignore_sight) -> void {
    ZoneScoped;
    for (const auto gp : bubble_submaps(here)) { spawn_submap(here, gp, ignore_sight); }
}

auto spawn_monsters_new_submaps(map& here, const point_rel_sm& shift_amount) -> void {
    ZoneScoped;

    // If the shift covers the full map in any dimension every submap is new -
    // fall back to the full spawn to avoid an empty or degenerate strip.
    const auto map_size = here.getmapsize();
    if (std::abs(shift_amount.x()) >= map_size || std::abs(shift_amount.y()) >= map_size) {
        spawn_monsters(here, false);
        return;
    }

    const auto bounds = reality_bubble_2D_bounds(here);
    for (const auto gp : bubble_submaps(here)) {
        if (!bounds.contains(gp.xy() + shift_amount)) { spawn_submap(here, gp, false); }
    }
}

} // namespace map_spawn
