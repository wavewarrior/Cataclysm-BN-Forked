#pragma once

#include "coordinates.h"
#include "enums.h"
#include "type_id.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

class Character;
class inventory;
class item;
class mtype;
class player;
class player_activity;
// TODO (https://github.com/cataclysmbn/Cataclysm-BN/issues/1612):
// Remove that forward declaration after repair_activity_actor.
class vehicle;

template <typename T> class detached_ptr;

std::vector<tripoint_bub_ms> get_sorted_tiles_by_distance(
    const tripoint_bub_ms& coord, const std::unordered_set<tripoint_bub_ms>& tiles);
std::vector<tripoint_abs_ms> get_sorted_tiles_by_distance(
    const tripoint_abs_ms& coord, const std::unordered_set<tripoint_abs_ms>& tiles);
std::vector<tripoint_bub_ms> route_adjacent(const player& p, const tripoint_bub_ms& dest);

enum requirement_check_result : int {
    SKIP_LOCATION = 0,
    CAN_DO_LOCATION,
    RETURN_EARLY // another activity like a fetch activity has been started.
};

enum butcher_type : int {
    BUTCHER,      // quick butchery
    BUTCHER_FULL, // full workshop butchery
    F_DRESS,      // field dressing a corpse
    SKIN,         // skinning a corpse
    QUARTER,      // quarter a corpse
    BLEED,        // bleed a corpse
    DISMEMBER,    // destroy a corpse
    DISSECT       // dissect a corpse for CBMs
};

enum class do_activity_reason : int {
    CAN_DO_CONSTRUCTION, // Can do construction.
    CAN_DO_FETCH,        // Can do fetch - this is usually the default result for fetch task
    CAN_DO_PREREQ,       // for constructions - can't build the main construction, but can build the
                         // pre-req
    CAN_DO_PREREQ_2,     // Can do the second pre-req deep below the desired one.
    NO_COMPONENTS,       // can't do the activity there due to lack of components /tools
    NO_COMPONENTS_PREREQ,   // need components to build the pre-requisite for the actual desired
                            // construction
    NO_COMPONENTS_PREREQ_2, // need components to the second pre-req deep.
    DONT_HAVE_SKILL,        // don't have the required skill
    NO_ZONE,                // There is no required zone anymore
    ALREADY_DONE,           // the activity is done already ( maybe by someone else )
    UNKNOWN_ACTIVITY,   // This is probably an error - got to the end of function with no previous
                        // reason
    NEEDS_HARVESTING,   // For farming - tile is harvestable now.
    NEEDS_PLANTING,     // For farming - tile can be planted
    NEEDS_TILLING,      // For farming - tile can be tilled
    NEEDS_WARM_WEATHER, // For farming - need warm weather to plant
    NEEDS_ABOVE_GROUND, // For farming - can't plant seeds lacking `CAN_PLANT_UNDERGROUND` flag
                        // below z-level 0
    BLOCKING_TILE,      // Something has made it's way onto the tile, so the activity cannot proceed
    NEEDS_CHOPPING,     // There is wood there to be chopped
    NEEDS_TREE_CHOPPING,  // There is a tree there that needs to be chopped
    NEEDS_BIG_BUTCHERING, // There is at least one corpse there to butcher, and it's a big one
    NEEDS_BUTCHERING,     // THere is at least one corpse there to butcher, and there's no need for
                          // additional tools
    ALREADY_WORKING,      // somebody is already working there
    NEEDS_VEH_DECONST,    // There is a vehicle part there that we can deconstruct, given the right
                          // tools.
    NEEDS_VEH_REPAIR, // There is a vehicle part there that can be repaired, given the right tools.
    NEEDS_MINING,     // This spot can be mined, if the right tool is present.
    NEEDS_FISHING     // This spot can be fished, if the right tool is present.
};

struct activity_reason_info {
    // reason for success or fail
    do_activity_reason reason;
    // is it possible to do this
    bool can_do;
    // construction index
    std::optional<construction_id> con_idx;

    activity_reason_info(
        do_activity_reason reason_, bool can_do_,
        const std::optional<construction_id>& con_idx_ = std::nullopt)
        : reason(reason_),
          can_do(can_do_),
          con_idx(con_idx_) {}

    static activity_reason_info ok(const do_activity_reason& reason_) {
        return activity_reason_info(reason_, true);
    }

    static activity_reason_info build(
        const do_activity_reason& reason_, bool can_do_, const construction_id& con_idx_) {
        return activity_reason_info(reason_, can_do_, con_idx_);
    }

    static activity_reason_info fail(const do_activity_reason& reason_) {
        return activity_reason_info(reason_, false);
    }
};

enum class butchery_possibility : int { yes = 0, need_confirmation, never, not_this };

struct butchery_setup {
    std::vector<std::string> problems;
    std::vector<std::string> info;
    butchery_possibility can_do;
    int move_cost;
    butcher_type type;
};

butchery_setup consider_butchery(const item& corpse_item, player& u, butcher_type action);
int butcher_time_to_cut(const item& corpse_item, butcher_type action);
void butchery_drops_harvest(
    item* corpse_item, const mtype& mt, player& p, const std::function<int()>& roll_butchery,
    butcher_type action, const std::function<double()>& roll_drops);
void butchery_quarter(item* corpse_item, const player& p);
void extract_or_wreck_cbms(std::vector<detached_ptr<item>>& cbms, int roll, player& p);
int size_factor_in_time_to_cut(creature_size size);

// activity_item_handling.cpp
void activity_on_turn_drop();
void activity_on_turn_move_loot(player_activity& act, player& p);
// return true if there is an activity that can be done potentially, return false if no work can be
// found.
bool generic_multi_activity_handler(player_activity& act, player& p, bool check_only = false);
void activity_on_turn_fetch(player_activity&, player* p);
void activity_on_turn_wear(player_activity& act, player& p);

enum class consume_type : bool { FOOD, DRINK };

/**
 * @brief Find an item to consume automatically
 *
 * @param consume_type type of item to consume
 * @return true player ate food or was nauseous
 * @return false player did not find anything suitable or is a npc
 */
bool find_auto_consume(player& p, const consume_type type);
void try_fuel_fire(player_activity& act, player& p, bool starting_fire = false);

enum class item_drop_reason { deliberate, too_large, too_heavy, tumbling };

void put_into_vehicle_or_drop(Character& c, item_drop_reason, detached_ptr<item>&& it);
void put_into_vehicle_or_drop(
    Character& c, item_drop_reason, std::vector<detached_ptr<item>>& items);
void put_into_vehicle_or_drop(
    Character& c, item_drop_reason, std::vector<detached_ptr<item>>& items,
    const tripoint_bub_ms& where, bool force_ground = false);
void put_into_vehicle_or_drop(
    Character& c, item_drop_reason, detached_ptr<item>&& it, const tripoint_bub_ms& where,
    bool force_ground = false);
void drop_on_map(
    Character& c, item_drop_reason reason, std::vector<detached_ptr<item>>& items,
    const tripoint_bub_ms& where);
void drop_on_map(
    Character& c, item_drop_reason reason, detached_ptr<item>&& it, const tripoint_bub_ms& where);

namespace activity_handlers {

bool resume_for_multi_activities(player& p);
void perform_zone_activity_turn(
    player* p, const zone_type_id& ztype,
    const std::function<bool(const tripoint_bub_ms&)>& tile_filter,
    const std::function<void(player& p, const tripoint_bub_ms&)>& tile_action,
    const std::string& finished_msg);
/** activity_do_turn functions: */
void craft_do_turn(player_activity* act, player* p);
void drop_do_turn(player_activity* act, player* p);
void stash_do_turn(player_activity* act, player* p);
void multiple_chop_planks_do_turn(player_activity* act, player* p);
void wear_do_turn(player_activity* act, player* p);
void move_items_do_turn(player_activity* act, player* p);
void multiple_farm_do_turn(player_activity* act, player* p);
void multiple_fish_do_turn(player_activity* act, player* p);
void multiple_construction_do_turn(player_activity* act, player* p);
void multiple_mine_do_turn(player_activity* act, player* p);
void multiple_butcher_do_turn(player_activity* act, player* p);
void vehicle_deconstruction_do_turn(player_activity* act, player* p);
void vehicle_repair_do_turn(player_activity* act, player* p);
void chop_trees_do_turn(player_activity* act, player* p);
void fetch_do_turn(player_activity* act, player* p);
void move_loot_do_turn(player_activity* act, player* p);
void armor_layers_do_turn(player_activity* act, player* p);
void repair_item_do_turn(player_activity* act, player* p);
void tidy_up_do_turn(player_activity* act, player* p);
void operation_do_turn(player_activity* act, player* p);

// defined in activity_handlers.cpp
extern const std::map<activity_id, std::function<void(player_activity*, player*)>>
    do_turn_functions;

/** activity_finish functions: */
void hotwire_finish(player_activity* act, player* p);
void longsalvage_finish(player_activity* act, player* p);
void reload_finish(player_activity* act, player* p);
void vehicle_finish(player_activity* act, player* p);
void start_engines_finish(player_activity* act, player* p);
void repair_item_finish(player_activity* act, player* p);
void mend_item_finish(player_activity* act, player* p);
void gunmod_add_finish(player_activity* act, player* p);
void toolmod_add_finish(player_activity* act, player* p);
void operation_finish(player_activity* act, player* p);
void jackhammer_finish(player_activity* act, player* p);
void fill_pit_finish(player_activity* act, player* p);
void unload_mag_finish(player_activity* act, player* p);

void try_sleep_query(player_activity* act, player* p);

// defined in activity_handlers.cpp
extern const std::map<activity_id, std::function<void(player_activity*, player*)>> finish_functions;

// HACK: This is a hack to provide fake items
// from vehicles or furniture until
// `repair_activity_actor` would be implemented.
//
// TODO (https://github.com/cataclysmbn/Cataclysm-BN/issues/1612):
// Remove that repair code after repair_activity_actor.
namespace repair_activity_hack {

void patch_activity_for_vehicle(
    player_activity& activity, const tripoint_bub_ms& veh_part_position, const vehicle& veh,
    int interact_part_idx, const itype_id& it);
void patch_activity_for_furniture(
    player_activity& activity, const tripoint_bub_ms& furniture_position, const itype_id& itt);

} // namespace repair_activity_hack

} // namespace activity_handlers
