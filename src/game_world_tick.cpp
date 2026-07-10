// Extracted from game.cpp — B5 decomposition (world-tick cluster).
// All game:: methods remain declared in game.h (unchanged).
#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor.h"
#include "avatar.h"
#include "avatar_functions.h"
#include "batch_turns.h"
#include "calendar.h"
#include "cached_options.h"
#include "character_functions.h"
#include "character_turn.h"
#include "coordinates.h"
#include "creature_tracker.h"
#include "debug.h"
#include "distribution_grid.h"
#include "event.h"
#include "event_bus.h"
#include "faction.h"
#include "field_type.h"
#include "fire_spread_loader.h"
#include "fluid_grid.h"
#include "game_constants.h"
#include "map.h"
#include "mapbuffer.h"
#include "mapbuffer_registry.h"
#include "mapdata.h"
#include "messages.h"
#include "mission.h"
#include "monster.h"
#include "monster_action.h"
#include "monster_plan.h"
#include "mtype.h"
#include "npc.h"
#include "npc_class.h"
#include "omdata.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "player_activity.h"
#include "rng.h"
#include "rot.h"
#include "sounds.h"
#include "string_formatter.h"
#include "submap.h"
#include "thread_pool.h"
#include "timed_event.h"
#include "translations.h"
#include "type_id.h"
#include "catalua_hooks.h"
#include "catalua_sol.h"
#include "activity_monmove_cache.h"
#include "cata_cartesian_product.h"
#include "submap_fields.h"
#include "monfaction.h"
#include "monattack.h"

#include "profile.h"

#define dbg(x) DebugLogFL((x),DC::Game)

// File-local string-IDs — mirror the declarations in game.cpp
static const activity_id ACT_OPERATION( "ACT_OPERATION" );

static const efftype_id effect_ai_controlled( "ai_controlled" );
static const efftype_id effect_ai_waiting( "ai_waiting" );
static const efftype_id effect_docile( "docile" );
static const efftype_id effect_npc_suspend( "npc_suspend" );
static const efftype_id effect_ridden( "ridden" );
static const efftype_id effect_downed( "downed" );
static const efftype_id effect_lying_down( "lying_down" );

static const trait_id trait_LEG_TENT_BRACE( "LEG_TENT_BRACE" );

// ——— void game::cleanup_dead() ———
void game::cleanup_dead()
{
    // Dead monsters need to stay in the tracker until everything else that needs to die does so
    // This is because dying monsters can still interact with other dying monsters (@ref Creature::killer)
    bool monster_is_dead = critter_tracker->kill_marked_for_death();

    bool npc_is_dead = false;
    // can't use all_npcs as that does not include dead ones
    for( const auto &n : active_npc ) {
        if( n->is_dead() ) {
            n->die( nullptr ); // make sure this has been called to create corpses etc.
            npc_is_dead = true;
        }
    }

    if( monster_is_dead ) {
        // From here on, pointers to creatures get invalidated as dead creatures get removed.
        critter_tracker->remove_dead();
    }

    if( npc_is_dead ) {
        for( auto it = active_npc.begin(); it != active_npc.end(); ) {
            if( ( *it )->is_dead() ) {
                if( !( *it )->is_manually_erased() ) {
                    // Normal death path — npc::erase() was not called, so do cleanup here.
                    remove_npc_follower( ( *it )->getID() );
                    get_overmapbuffer( ( *it )->get_dimension() ).remove_npc( ( *it )->getID() );
                }
                it = active_npc.erase( it );
            } else {
                it++;
            }
        }
    }

    critter_died = false;
}

// ——— static int lod_dist() ———
static int lod_dist( const tripoint_bub_ms &a, const tripoint_bub_ms &b )
{
    const int dz = std::abs( a.z() - b.z() );
    if( dz < 2 ) {
        return rl_dist( a.xy(), b.xy() );
    }
    return rl_dist( a.xy(), b.xy() ) + lod_z_penalty * ( dz - 1 );
}

// ——— int game::tier_assign_all() ———
int game::tier_assign_all()
{
    if( !monster_lod_enabled ) {
        const std::string &player_dim_lod = m.get_bound_dimension();
        int count = 0;
        int cross_dim = 0;
        for( monster &mon : all_monsters() ) {
            if( mon.get_dimension() != player_dim_lod ) {
                // Monsters in other dimensions are always Tier 2, even with LOD disabled.
                mon.lod_tier     = 2;
                mon.lod_cooldown = 0;
                ++cross_dim;
            } else {
                mon.lod_tier     = 0;
                mon.lod_cooldown = 0;
                ++count;
            }
        }
        TracyPlot( "LOD Tier 0 (Full AI)",  static_cast<int64_t>( count ) );
        TracyPlot( "LOD Tier 1 (Coarse)",   static_cast<int64_t>( 0 ) );
        TracyPlot( "LOD Tier 2 (Macro)",    static_cast<int64_t>( cross_dim ) );
        static_cast<void>( cross_dim );
        return count;
    }

    const auto player_pos = u.bub_pos();
    int tier_counts[3] = { 0, 0, 0 };

    const int tier01_dist  = lod_tier_full_dist;
    // Clamp so Tier 1/2 boundary is always strictly greater than Tier 0/1.
    const int tier12_dist  = std::max( lod_tier_coarse_dist, tier01_dist + 1 );
    const int demote_cd    = lod_demotion_cooldown;

    const std::string &player_dim = m.get_bound_dimension();

    for( monster &mon : all_monsters() ) {
        int8_t new_tier;

        // Monsters in a different dimension are always Tier 2 regardless of distance.
        if( mon.get_dimension() != player_dim ) {
            new_tier = 2;
        } else {
            const int dist = lod_dist( mon.bub_pos(), player_pos );
            const int abs_dz = std::abs( mon.bub_pos().z() - player_pos.z() );
            if( dist <= tier01_dist || ( !mon.is_wandering() && abs_dz < 2 ) ) {
                new_tier = 0;
            } else if( dist <= tier12_dist ) {
                new_tier = 1;
            } else {
                new_tier = 2;
            }
        }

        if( new_tier < mon.lod_tier ) {
            // Promotion is always immediate.
            mon.lod_tier     = new_tier;
            mon.lod_cooldown = 0;
        } else if( new_tier > mon.lod_tier && mon.lod_cooldown <= 0 ) {
            // Demotion only when cooldown has expired.
            mon.lod_tier     = new_tier;
            mon.lod_cooldown = static_cast<int8_t>( demote_cd );
        }

        if( mon.lod_cooldown > 0 ) {
            mon.lod_cooldown--;
        }

        tier_counts[mon.lod_tier]++;
    }

    // Emit per-tier monster counts as continuous Tracy plots.
    // "LOD Eligible" (after budget cap) is emitted from monmove().
    TracyPlot( "LOD Tier 0 (Full AI)",  static_cast<int64_t>( tier_counts[0] ) );
    TracyPlot( "LOD Tier 1 (Coarse)",   static_cast<int64_t>( tier_counts[1] ) );
    TracyPlot( "LOD Tier 2 (Macro)",    static_cast<int64_t>( tier_counts[2] ) );
    return tier_counts[0];
}

// ——— void game::world_tick() ———
void game::world_tick()
{
    ZoneScoped;
    TracyPlot( "Active Dimensions", static_cast<int64_t>( loaded_dimensions_.size() ) );

    const auto fire_spread = reality_bubble_fire_spread;
    const auto do_emits = calendar::once_every( 10_seconds );

    if( !fire_spread ) {
        fire_loader.clear( submap_loader );
    }

    auto total_loaded_submaps = int64_t{ 0 };
    auto total_simulated_submaps = int64_t{ 0 };
    auto total_no_field_submaps = int64_t{ 0 };
    auto total_field_submaps = int64_t{ 0 };
    auto total_emitter_dirty_submaps = int64_t{ 0 };
    auto total_emitter_active_submaps = int64_t{ 0 };
    auto total_fire_request_submaps = int64_t{ 0 };
    auto total_field_count = int64_t{ 0 };
    MAPBUFFER_REGISTRY.for_each( [&]( const std::string & dim, mapbuffer & mb ) {
        ZoneScopedN( "world_tick_dimension" );
        ZoneText( dim.c_str(), dim.size() );

        // When pocket simulation is disabled, skip all non-primary dimensions.
        // The primary dimension always uses dim == "" (empty string).
        // none/minimal/moderate distinctions are deferred to a future PR —
        // for now any setting other than "off" runs the full simulation path.
        if( pocket_simulation_level == pocket_sim_level::off && !dim.empty() ) {
            return;
        }

        {
            ZoneScopedN( "world_tick_submap_scan" );
            mb.for_each_submap( [&]( std::pair<const tripoint_abs_sm, std::unique_ptr<submap>> &entry ) {
                auto &[pos_sm, sm_ptr] = entry;
                if( !sm_ptr ) {
                    return;
                }
                ++total_loaded_submaps;

                // Only simulate submaps that are actively requested (reality bubble,
                // fire spread, player base, script).  Skip lazy-border and streamer
                // pre-loaded submaps that are merely resident in memory.
                // Use the precomputed O(1) set rather than is_simulated() which does
                // an O(log N) mapbuffer lookup + O(R) request scan per submap.
                if( !submap_loader.is_in_simulated_set( dim, pos_sm ) ) {
                    return;
                }
                ++total_simulated_submaps;

                ZoneScopedN( "wtd_submap_body" );

                if( sm_ptr->field_count == 0 ) {
                    ++total_no_field_submaps;
                } else {
                    ++total_field_submaps;
                }
                total_field_count += sm_ptr->field_count;

                auto has_fire = false;
                if( sm_ptr->field_count > 0 ) {
                    ZoneScopedN( "wtd_process_fields" );
                    has_fire = process_fields_in_submap( *sm_ptr, pos_sm, mb );
                }
                sm_ptr->last_touched = calendar::turn;

                // Furniture field emitters — covers all loaded submaps, not just the bubble.
                // Primary dimension only: m.emit_field() operates in primary-map coordinates.
                // emitter_cache holds the positions of EMITTER furniture, lazily rebuilt on first
                // use after furniture changes and iterated directly on subsequent ticks.
                if( do_emits && dim.empty() ) {
                    if( !sm_ptr->emitter_cache.has_value() ) {
                        ++total_emitter_dirty_submaps;
                        ZoneScopedN( "field_emits_rebuild" );
                        auto &positions = sm_ptr->emitter_cache.emplace();
                        std::ranges::for_each(
                            cata::views::cartesian_product( std::views::iota( 0, SEEX ),
                                                            std::views::iota( 0, SEEY ) ),
                        [&]( const auto & xy ) {
                            const point_sm_ms p( std::get<0>( xy ), std::get<1>( xy ) );
                            if( sm_ptr->get_furn( p ).obj().has_flag( "EMITTER" ) ) {
                                positions.emplace_back( p );
                            }
                        } );
                    }
                    if( !sm_ptr->emitter_cache->empty() ) {
                        ++total_emitter_active_submaps;
                        ZoneScopedN( "field_emits" );
                        const tripoint_bub_ms bub_sm_origin = m.abs_to_bub( project_to<coords::ms>( pos_sm ) );
                        std::ranges::for_each( *sm_ptr->emitter_cache, [&]( const point_sm_ms & lp ) {
                            const tripoint_bub_ms local_pos = bub_sm_origin + tripoint_rel_ms( lp.x(), lp.y(), 0 );
                            std::ranges::for_each(
                                sm_ptr->get_furn( lp ).obj().emissions,
                            [&]( const emit_id & e ) {
                                m.emit_field( local_pos, e );
                            } );
                        } );
                    }
                }

                if( fire_spread && has_fire ) {
                    ++total_fire_request_submaps;
                    ZoneScopedN( "wtd_fire_requests" );
                    // Always register the fire submap itself — including while it is
                    // still inside the bubble — so the fire_spread request already
                    // exists when the bubble shifts away on a future turn.
                    fire_loader.request_for_fire( dim, pos_sm );

                    // Look up dimension bounds once per submap so we can
                    // prevent fire from escaping a bounded pocket dimension.
                    const auto dim_it = loaded_dimensions_.find( dim );
                    std::optional<dimension_bounds> dim_bounds;
                    if( dim_it != loaded_dimensions_.end() && dim_it->second.pocket_info.has_value() ) {
                        dim_bounds = dim_it->second.pocket_info->bounds;
                    }

                    // Cardinal neighbours used for fire-spread boundary requests.
                    static constexpr auto card = std::array{
                        tripoint{ 1, 0, 0 }, tripoint{ -1, 0, 0 },
                        tripoint{ 0, 1, 0 }, tripoint{ 0, -1, 0 }
                    };
                    std::ranges::for_each( card, [&]( const tripoint & delta ) {
                        const tripoint_abs_sm nbr{ pos_sm.raw() + delta };
                        // Do not request a fire-spread load outside the dimension's
                        // spatial bounds.  Fire cannot spread through boundary tiles
                        // (they are impassable non-terrain markers, not real submaps).
                        if( dim_bounds && !dim_bounds->contains( nbr ) ) {
                            return;
                        }
                        if( !submap_loader.is_requested( dim, nbr ) ) {
                            fire_loader.request_for_fire( dim, nbr );
                        }
                    } );
                }
            } );
        }
    } );

    TracyPlot( "World Tick Loaded Submaps", total_loaded_submaps );
    TracyPlot( "World Tick Simulated Submaps", total_simulated_submaps );
    TracyPlot( "World Tick No-Field Submaps", total_no_field_submaps );
    TracyPlot( "World Tick Field Submaps", total_field_submaps );
    TracyPlot( "World Tick Dirty Emitter Submaps", total_emitter_dirty_submaps );
    TracyPlot( "World Tick Active Emitter Submaps", total_emitter_active_submaps );
    TracyPlot( "World Tick Fire Request Submaps", total_fire_request_submaps );
    TracyPlot( "Active Fields", total_field_count );

    // Prune fire-spread load requests that are no longer connected or lack fire.
    {
        ZoneScopedN( "fire_spread" );
        fire_loader.prune_disconnected( submap_loader );
    }
}

// ——— auto game::monmove() ———
auto game::monmove( const monster_activity_ai_mode mode, activity_monmove_cache *cache ) -> void
{
    ZoneScopedN( "game::monmove" );
    const auto activity_skip_ai = mode == monster_activity_ai_mode::activity_skip &&
                                  monster_lod_enabled;
    if( !activity_skip_ai ) {
    cache = nullptr;
}
{
    ZoneScopedN( "monmove_cleanup_initial" );
        cleanup_dead();
    }

    // P-8: clear the per-turn sight cache at the top of every monmove() call
    // so results from the previous turn are not reused.
    {
        ZoneScopedN( "monmove_clear_sight_cache" );
        turn_sight_cache_.clear();
    }

    auto use_activity_cache = cache != nullptr && cache->valid &&
                              cache->monster_count == static_cast<int>( critter_tracker->size() );
    if( cache != nullptr && cache->valid && !use_activity_cache ) {
    cache->valid = false;
}

// LOD-A: assign tier 0/1/2 to every monster based on distance from player.
// Must run before the plannable collection so Tier-2 monsters are excluded
// from the parallel planning pass (they use the macro step instead).
int tier0_count = 0;
if( use_activity_cache ) {
    tier0_count = cache->tier0_count;
} else {
    ZoneScopedN( "monmove_assign_lod_tiers" );
        tier0_count = tier_assign_all();
    }

    // -----------------------------------------------------------------------
    // P-7: Parallel planning pass.
    //
    // Collect Tier-0 and Tier-1 monsters that are alive and eligible for AI
    // planning this turn, compute their plans in parallel, then apply and
    // execute serially.  Tier-2 (Macro) monsters are excluded here; they take
    // a single Manhattan step in the move execution loop below.
    //
    // compute_plan() is const w.r.t. *this (monster) and only reads shared
    // game state (map caches, faction data, creature positions).  The only
    // shared writes are:
    //   - skew_vision_cache (protected by skew_vision_cache_mutex, P-6)
    //   - turn_sight_cache_ (protected by turn_sight_cache_mutex_, P-8)
    //   - per-thread RNG (thread-local engine, P-5)
    // This makes the parallel phase data-race-free.
    //
    // Over-collection is intentional: monsters that die during the serial
    // setup phase (process_turn, creature_in_field) simply have their
    // pre-computed plan discarded.
    // -----------------------------------------------------------------------

    // Configurable via Debug → Performance → "Monster LOD" settings.
    const int action_budget  = lod_action_budget;
    const int macro_interval = lod_macro_interval;

    // Dynamic budget: at least the floor, but expanded to cover all Tier-0
    // monsters so the cap never defers a full-AI monster.
    // Activity skip cache reuse keeps this budget fixed from the first pass in
    // the window; normal monmove computes it from the current LOD tiers.
    const int effective_budget = use_activity_cache ? cache->effective_budget :
                                 std::max( action_budget, tier0_count );
    TracyPlot( "LOD Effective Budget", static_cast<int64_t>( effective_budget ) );

    // Build actor snapshots for planning setup. Holding shared references keeps
    // pointer snapshots valid if live lists change later in the turn. Activity
    // skip mode can reuse the first snapshot across the fixed-window batch.
    auto mon_snap_local = std::vector<monster *> {};
    const std::vector<monster *> *mon_snap = &mon_snap_local;
    auto npc_snap_local = std::vector<npc *> {};
    const std::vector<npc *> *npc_snap = &npc_snap_local;
    if( use_activity_cache ) {
    mon_snap = &cache->mon_snap;
    npc_snap = &cache->npc_snap;
} else {
    ZoneScopedN( "monmove_build_actor_snapshots" );
        auto monster_refs = critter_tracker->get_monsters_list();
        mon_snap_local.reserve( monster_refs.size() );
        for( const shared_ptr_fast<monster> &mon_ptr : monster_refs ) {
            if( mon_ptr && !mon_ptr->is_dead() ) {
                mon_snap_local.push_back( mon_ptr.get() );
            }
        }

        auto npc_refs = std::vector<shared_ptr_fast<npc>> {};
        npc_refs.reserve( active_npc.size() );
        std::ranges::copy( active_npc, std::back_inserter( npc_refs ) );
        npc_snap_local.reserve( npc_refs.size() );
        for( const shared_ptr_fast<npc> &guy : npc_refs ) {
            if( guy && !guy->is_dead() ) {
                npc_snap_local.push_back( guy.get() );
            }
        }

        if( cache != nullptr ) {
            cache->monster_refs = std::move( monster_refs );
            cache->mon_snap = std::move( mon_snap_local );
            cache->npc_refs = std::move( npc_refs );
            cache->npc_snap = std::move( npc_snap_local );
            cache->monster_count = static_cast<int>( critter_tracker->size() );
            cache->tier0_count = tier0_count;
            cache->effective_budget = effective_budget;
            mon_snap = &cache->mon_snap;
            npc_snap = &cache->npc_snap;
        }
    }

    auto activity_lod_restore_local = std::vector<std::pair<monster *, int8_t>> {};
    const std::vector<std::pair<monster *, int8_t>> *activity_lod_restore =
        &activity_lod_restore_local;
    auto activity_ai_paused_local = std::unordered_set<monster *> {};
    const std::unordered_set<monster *> *activity_ai_paused = &activity_ai_paused_local;
    if( activity_skip_ai ) {
    ZoneScopedN( "monmove_activity_demote_lod" );
        if( use_activity_cache ) {
            activity_lod_restore = &cache->real_lod;
            activity_ai_paused = &cache->ai_paused;
        } else {
            activity_lod_restore_local.reserve( mon_snap->size() );
            activity_ai_paused_local.reserve( mon_snap->size() );
            for( monster *critter : *mon_snap ) {
                const auto real_lod_tier = critter->lod_tier;
                activity_lod_restore_local.emplace_back( critter, real_lod_tier );
                if( real_lod_tier > activity_skip_monster_lod_gate ) {
                    activity_ai_paused_local.insert( critter );
                }
            }
            if( cache != nullptr ) {
                cache->real_lod = std::move( activity_lod_restore_local );
                cache->ai_paused = std::move( activity_ai_paused_local );
                activity_lod_restore = &cache->real_lod;
                activity_ai_paused = &cache->ai_paused;
            }
        }
        for( const auto &[critter, real_lod_tier] : *activity_lod_restore ) {
            if( activity_ai_paused->contains( critter ) ) {
                continue;
            }
            critter->lod_tier = static_cast<int8_t>( std::min<int>( 2, real_lod_tier + 1 ) );
        }
        TracyPlot( "Activity Skip Monster AI Paused",
                   static_cast<int64_t>( activity_ai_paused->size() ) );
    }

    // OPP-7: Unified disposition map: a single hash lookup in the execution
    // loop suffices:
    //   value >= 0  → index into precomputed[] (monster has a parallel plan)
    //   not present → not collected this turn (plan serially)
    std::unordered_map<monster *, int> plan_index;

    std::vector<monster *> plannable;
    {
        ZoneScopedN( "monmove_build_plannable" );
        auto plannable_candidates_local = std::vector<monster *> {};
        const std::vector<monster *> *plannable_candidates = mon_snap;
        if( activity_skip_ai ) {
            if( use_activity_cache ) {
                plannable_candidates = &cache->plannable_candidates;
            } else {
                plannable_candidates_local.reserve( mon_snap->size() );
                for( monster *critter : *mon_snap ) {
                    if( !critter->is_dead() &&
                        !activity_ai_paused->contains( critter ) &&
                        critter->lod_tier < 2 &&
                        critter->is_simulated() ) {
                        plannable_candidates_local.push_back( critter );
                    }
                }
                if( cache != nullptr ) {
                    cache->plannable_candidates = std::move( plannable_candidates_local );
                    plannable_candidates = &cache->plannable_candidates;
                } else {
                    plannable_candidates = &plannable_candidates_local;
                }
            }
        }
        plannable.reserve( plannable_candidates->size() );
        for( monster *critter : *plannable_candidates ) {
            if( !critter->is_dead() &&
                !activity_ai_paused->contains( critter ) &&
                !critter->has_effect( effect_ai_controlled ) &&
                critter->moves > 0 &&
                !critter->has_effect( effect_ridden ) &&
                critter->lod_tier < 2 &&
                critter->is_simulated() ) {
                // Tier-2 monsters skip full planning; they use the macro step.
                plannable.push_back( critter );
            }
        }
    }

    // Pre-warm directed Creature::sees() jobs before the parallel planning phase.
    // Worker threads compute raw perception results only; the shared
    // turn_sight_cache_ is filled serially afterward, avoiding cache write-lock
    // contention in compute_plan().  map::sees() still supplies symmetric LOS reuse
    // for the ray traces below Creature::sees().
    auto sight_jobs = std::vector<std::pair<const Creature *, const Creature *>> {};
    const auto initial_sight_job_capacity =
        plannable.size() * ( npc_snap->size() + std::min( mon_snap->size(), size_t{ 16 } ) + 1 );
    sight_jobs.reserve( initial_sight_job_capacity );
    const auto add_sight_job = [&]( const Creature & seer, const Creature & target ) {
        sight_jobs.emplace_back( &seer, &target );
    };
    {
        ZoneScopedN( "monmove_build_sight_jobs" );
        for( auto *mon : plannable ) {
            const auto mon_max_sight = std::max( mon->type->vision_day, mon->type->vision_night );
            const auto mon_pos = mon->bub_pos();
            const auto waiting = mon->has_effect( effect_ai_waiting );
            const auto docile = mon->friendly != 0 && mon->has_effect( effect_docile );
            if( !waiting && mon->friendly <= 0 &&
                rl_dist( mon_pos, u.bub_pos() ) <= mon_max_sight ) {
                add_sight_job( *mon, u );
            }
            for( auto *n : *npc_snap ) {
                const auto faction_att = mon->faction.obj().attitude( n->get_monster_faction() );
                if( faction_att == MFA_NEUTRAL || faction_att == MFA_FRIENDLY ) {
                    continue;
                }
                if( rl_dist( mon_pos, n->bub_pos() ) <= mon_max_sight ) {
                    add_sight_job( *mon, *n );
                }
            }

            const auto needs_group_sight =
                mon->lod_tier <= lod_group_morale_max_tier &&
                ( ( mon->has_flag( MF_GROUP_MORALE ) && mon->morale < mon->type->morale ) ||
                  mon->has_flag( MF_SWARMS ) );
            for( auto *target_mon : *mon_snap ) {
                if( target_mon == mon ) {
                    continue;
                }
                if( rl_dist( mon_pos, target_mon->bub_pos() ) > mon_max_sight ) {
                    continue;
                }
                if( mon->friendly != 0 && !docile && !waiting && target_mon->friendly == 0 ) {
                    add_sight_job( *mon, *target_mon );
                    continue;
                }
                if( mon->friendly == 0 ) {
                    const auto faction_att = mon->faction.obj().attitude( target_mon->faction );
                    if( faction_att != MFA_NEUTRAL && faction_att != MFA_FRIENDLY ) {
                        add_sight_job( *mon, *target_mon );
                        continue;
                    }
                    if( needs_group_sight && mon->faction == target_mon->faction ) {
                        add_sight_job( *mon, *target_mon );
                    }
                }
            }
        }
    }
    TracyPlot( "Monmove Sight Jobs", static_cast<int64_t>( sight_jobs.size() ) );
    if( !sight_jobs.empty() ) {
    auto sight_results = std::vector<char>( sight_jobs.size(), 0 );
        {
            ZoneScopedN( "monmove_parallel_sight_prewarm" );
            if( parallel_enabled && parallel_monster_planning && sight_jobs.size() > 1 ) {
                parallel_for_chunked( 0, static_cast<int>( sight_jobs.size() ),
                monster_plan_chunk_size, [&]( int i ) {
                    const auto index = static_cast<size_t>( i );
                    const auto &[seer, target] = sight_jobs[index];
                    sight_results[index] = seer->sees( *target ) ? 1 : 0;
                } );
            } else {
                for( const auto index : std::views::iota( size_t{ 0 }, sight_jobs.size() ) ) {
                    const auto &[seer, target] = sight_jobs[index];
                    sight_results[index] = seer->sees( *target ) ? 1 : 0;
                }
            }
        }
        {
            ZoneScopedN( "monmove_insert_sight_prewarm" );
            auto lock = std::unique_lock<std::shared_mutex>( turn_sight_cache_mutex_ );
            turn_sight_cache_.reserve( sight_jobs.size() );
            for( const auto index : std::views::iota( size_t{ 0 }, sight_jobs.size() ) ) {
                turn_sight_cache_.emplace( sight_jobs[index], sight_results[index] != 0 );
            }
        }
    }

    // Use the actor snapshots for thread-safe compute_plan() access.
    // compute_plan() calls g->all_monsters() / g->all_npcs() to find targets.
    // Those functions iterate weak_ptr_fast<T> objects whose refcounting uses
    // _S_single (non-atomic).  Concurrent lock() calls from worker threads are
    // a data race.  Building plain pointer snapshots serially avoids
    // touching any weak_ptr_fast from worker threads.
    // Build faction snapshot: group monster pointers by faction so compute_plan()
    // can do group-morale/swarm checks on worker threads without calling
    // weak_ptr_fast::lock() (non-atomic _S_single refcount — data race on Linux).
    monster::faction_snap_t faction_snap;
    const monster::faction_snap_t *faction_snap_for_plan = nullptr;
    if( use_activity_cache ) {
    faction_snap_for_plan = &cache->faction_snap;
} else {
    ZoneScopedN( "monmove_build_faction_snap" );
        std::ranges::for_each( *mon_snap, [&]( monster * mon_ptr ) {
            faction_snap[mon_ptr->faction].push_back( mon_ptr );
        } );
        if( cache != nullptr ) {
            cache->faction_snap = faction_snap;
            faction_snap_for_plan = &cache->faction_snap;
        } else {
            faction_snap_for_plan = &faction_snap;
        }
    }
    // Pre-compute per-faction hostile-faction lists once per tick.  compute_plan()
    // iterates only the hostile entries rather than all factions on every call.
    monster::hostile_fac_map_t hostile_fac_map;
    const monster::hostile_fac_map_t *hostile_fac_map_for_plan = nullptr;
    if( use_activity_cache ) {
    hostile_fac_map_for_plan = &cache->hostile_fac_map;
} else {
    ZoneScopedN( "monmove_build_hostile_fac_map" );
        for( const auto &[fac_id, _m] : *faction_snap_for_plan ) {
            for( const auto &[other_id, _o] : *faction_snap_for_plan ) {
                if( fac_id == other_id ) {
                    continue;
                }
                const auto att = fac_id.obj().attitude( other_id );
                if( att != MFA_NEUTRAL && att != MFA_FRIENDLY ) {
                    hostile_fac_map[fac_id].push_back( other_id );
                }
            }
        }
        if( cache != nullptr ) {
            cache->hostile_fac_map = hostile_fac_map;
            hostile_fac_map_for_plan = &cache->hostile_fac_map;
        } else {
            hostile_fac_map_for_plan = &hostile_fac_map;
        }
    }
    // Build spatial grid for O(k) target acquisition in compute_plan().
    // Built fresh every pass from live monster positions — the grid is keyed on
    // position, which changes every turn, so it must NOT be cached/reused across
    // an activity fixed-window (doing so acquired targets against stale geometry).
    // Iterating *mon_snap here is exactly as safe as compute_plan's own use of it.
    monster::spatial_grid_t spatial_grid;
    {
        ZoneScopedN( "monmove_build_spatial_grid" );
        for( monster *mon_ptr : *mon_snap ) {
            if( mon_ptr->is_dead() || !mon_ptr->is_simulated() ) {
                continue;
            }
            const auto pos = mon_ptr->bub_pos();
            const auto key = monster::spatial_grid_t::key_t{
                pos.x() / monster::spatial_grid_t::bucket_size,
                pos.y() / monster::spatial_grid_t::bucket_size
            };
            spatial_grid.buckets[key].push_back( mon_ptr );
        }
    }
    const monster::compute_plan_context plan_ctx{ mon_snap, npc_snap, faction_snap_for_plan,
            hostile_fac_map_for_plan, &spatial_grid };

    // parallel_for_chunked with a small chunk size gives the
    // pool a queue of fine-grained tasks.  Workers that finish a cheap monster
    // (no ray traces) immediately pull the next chunk rather than sitting idle
    // while a thread blocked on a costly monster finishes its oversized slice.
    std::vector<monster_plan_t> precomputed( plannable.size() );
    {
        ZoneScopedN( "monmove_compute_plans" );
        if( parallel_enabled && parallel_monster_planning ) {
            ZoneScopedN( "monmove_compute_plans_parallel" );
            parallel_for_chunked( 0, static_cast<int>( plannable.size() ),
            monster_plan_chunk_size, [&]( int i ) {
                precomputed[i] = plannable[i]->compute_plan( plan_ctx );
            } );
        } else {
            ZoneScopedN( "monmove_compute_plans_serial" );
            for( const auto index : std::views::iota( size_t{ 0 }, plannable.size() ) ) {
                precomputed[index] = plannable[index]->compute_plan( plan_ctx );
            }
        }
    }

    // Insert plannable entries into plan_index now that precomputed[] is built.
    {
        ZoneScopedN( "monmove_build_plan_index" );
        plan_index.reserve( plannable.size() );
        for( const auto index : std::views::iota( size_t{ 0 }, plannable.size() ) ) {
            plan_index[plannable[index]] = static_cast<int>( index );
        }
    }
    // -----------------------------------------------------------------------

    const auto player_pos = u.bub_pos();
    const int current_turn = to_turn<int>( calendar::turn );

    // -----------------------------------------------------------------------
    // LOD-B: Lifecycle loop — runs for EVERY monster regardless of tier or
    // budget.  Effect durations, hunger, and field damage tick normally for
    // all monsters.  The budget/tier system gates only the move loop below.
    // -----------------------------------------------------------------------
    {
        ZoneScopedN( "monmove_lifecycle" );
        for( monster *critter_ptr : *mon_snap ) {
            monster &critter = *critter_ptr;
            // Skip monsters in lazy-border or otherwise non-simulated submaps — their
            // submap has no active caches (transparency, lightmap, fields) and
            // processing them would read stale or missing data.  They will be
            // despawned into the overmap monster_map when their submap evicts.
            if( !critter.is_simulated() ) {
                continue;
            }
            // Critters in impassable tiles get pushed away, unless it's not impassable for them
            if( !critter.is_dead() && m.impassable( critter.bub_pos() ) &&
                !critter.can_move_to( critter.bub_pos() ) ) {
                std::string msg = string_format( "%s can't move to its location!  %s  %s", critter.name(),
                                                 critter.bub_pos().to_string(), m.tername( critter.bub_pos() ) );
                dbg( DL::Error ) << msg;
                add_msg( m_debug, msg );
                bool okay = false;
                for( const tripoint_bub_ms &dest : m.points_in_radius( critter.bub_pos(), 3 ) ) {
                    if( critter.can_move_to( dest ) && is_empty( dest ) ) {
                        critter.setpos( dest );
                        okay = true;
                        break;
                    }
                }
                if( !okay ) {
                    // die of "natural" cause (overpopulation is natural)
                    critter.die( nullptr );
                }
            }

            // ── Lifecycle stride gate ──────────────────────────────────────
            // Tier-2 monsters far off-z (|dz| >= 2) skip the EXPENSIVE lifecycle
            // (item + turn processing) every N turns.  Field damage is applied
            // unconditionally below — it is cheap (early-outs on field-free tiles)
            // and skipping it would let off-z monsters standing in fire/acid
            // escape damage.  Striding the rest is safe because:
            //   (a) effect durations become N-granular on off-screen monsters
            //   (b) process_turn side effects (cooldown decrement, emit fields,
            //       grab persistence, electrical field) are deferred, off-screen.
            const auto monitor_pos = critter.bub_pos();
            const bool striding = critter.lod_tier == 2 &&
                                  std::abs( monitor_pos.z() - u.bub_pos().z() ) >= 2;

            if( !striding || calendar::stride_due( lod_lifecycle_stride ) ) {
                if( !critter.is_dead() ) {
                    critter.process_items();
                }
                if( !critter.is_dead() ) {
                    critter.process_turn();
                }
            }
            // Field damage always applies, even on strided turns.
            m.creature_in_field( critter );
            // Daily events stay unconditional (already time-throttled):
            if( calendar::once_every( 1_days ) ) {
                if( critter.has_flag( MF_MILKABLE ) ) {
                    critter.refill_udders();
                }
                critter.try_reproduce();
            }
            if( activity_ai_paused->contains( critter_ptr ) ) {
                critter.moves = 0;
                critter.next_turn = current_turn + 1;
            }
        }
    }

    // -----------------------------------------------------------------------
    // LOD-C: Build eligible list.
    //
    // Include only monsters whose next_turn <= current_turn (i.e., not
    // deliberately deferred from a previous turn — currently all monsters
    // advance next_turn by 1 so this simply re-includes every monster that
    // is alive and has moves this turn).
    //
    // Sort ascending by Chebyshev distance to player so the budget cap
    // removes the farthest monsters rather than arbitrary ones. Activity skip
    // mode sorts once and reuses that ordering through the fixed-window batch.
    // Monsters skipped by the budget retain their current next_turn value so
    // they are guaranteed to run on the following turn.
    // -----------------------------------------------------------------------
    // Build eligible list paired with pre-computed distances so each monster's
    // distance is calculated exactly once.  The pair is (dist, monster*) so
    // the default comparator orders by distance first.
    std::vector<std::pair<int, monster *>> eligible;
    if( activity_skip_ai && cache != nullptr ) {
    ZoneScopedN( "monmove_build_eligible" );
        if( !use_activity_cache ) {
            auto eligible_order = std::vector<std::pair<int, monster *>> {};
            eligible_order.reserve( mon_snap->size() );
            for( monster *critter : *mon_snap ) {
                if( !critter->is_dead() &&
                    !activity_ai_paused->contains( critter ) &&
                    critter->is_simulated() ) {
                    eligible_order.emplace_back( lod_dist( critter->bub_pos(), player_pos ), critter );
                }
            }
            std::ranges::sort( eligible_order );
            cache->eligible_order = std::move( eligible_order );
            cache->valid = true;
            use_activity_cache = true;
        }
        eligible.reserve( cache->eligible_order.size() );
        auto accepted = 0;
        for( const auto &[distance, critter] : cache->eligible_order ) {
            if( critter == nullptr || critter->is_dead() ||
                activity_ai_paused->contains( critter ) ||
                critter->has_effect( effect_ridden ) ||
                critter->moves <= 0 ||
                critter->next_turn > current_turn ||
                !critter->is_simulated() ) {
                continue;
            }
            if( effective_budget <= 0 || accepted < effective_budget ) {
                eligible.emplace_back( distance, critter );
                ++accepted;
            } else {
                critter->moves = 0;
            }
        }
    } else {
        {
            ZoneScopedN( "monmove_build_eligible" );
            auto monsters = all_monsters();
            eligible.reserve( monsters.items ? monsters.items->size() : 0 );
            for( monster &critter : monsters ) {
                if( !critter.is_dead() &&
                    !activity_ai_paused->contains( &critter ) &&
                    !critter.has_effect( effect_ridden ) &&
                    critter.moves > 0 &&
                    critter.next_turn <= current_turn &&
                    critter.is_simulated() ) {
                    eligible.emplace_back( lod_dist( critter.bub_pos(), player_pos ), &critter );
                }
            }
        }

        // Apply the budget cap.  Excess monsters (farthest) are not processed
        // this turn; next_turn is NOT advanced for them so they are highest-
        // priority next turn (no starvation).
        //
        // nth_element is O(M) average — it partitions the N closest to the front
        // without fully ordering them, which is all we need for the budget cut.
        // Only pay the ordering cost when the budget actually fires.
        if( effective_budget > 0 &&
            static_cast<int>( eligible.size() ) > effective_budget ) {
            ZoneScopedN( "monmove_apply_budget" );
            std::nth_element( eligible.begin(),
                              eligible.begin() + effective_budget,
                              eligible.end() );
            // Drain moves for budget-cut monsters to prevent accumulation.
            // Without this, a monster deferred for N turns accumulates N turns
            // of moves from process_turn(), then bursts through N actions when
            // it finally gets a slot — no net savings at the budget boundary.
            for( auto &entry : eligible |
                 std::views::drop( static_cast<size_t>( effective_budget ) ) ) {
                entry.second->moves = 0;
            }
            eligible.resize( effective_budget );
        }
    }

    // How many monsters will actually enter the move loop this turn (after cap).
    // Compare against "LOD Tier 0 (Full AI)" to verify the budget floor is safe.
    TracyPlot( "LOD Eligible (post-cap)", static_cast<int64_t>( eligible.size() ) );

    // LOD-D: execute each eligible monster's turn.
    // Bio-alarm helper — called after each monster finishes its move loop.
    // static const: string_id hash lookup happens once, not every turn.
    static const bionic_id bio_alarm( "bio_alarm" );
    const auto check_bio_alarm = [&]( const monster & critter ) {
        if( !critter.is_dead() &&
            u.has_active_bionic( bio_alarm ) &&
            u.get_power_level() >= bio_alarm->power_trigger &&
            rl_dist( u.bub_pos(), critter.bub_pos() ) <= 5 &&
            !critter.is_hallucination() ) {
            u.mod_power_level( -bio_alarm->power_trigger );
            add_msg( m_warning, _( "Your motion alarm goes off!" ) );
            cancel_activity_or_ignore_query( distraction_type::alert,
                                             _( "Your motion alarm goes off!" ) );
            if( u.has_effect( efftype_id( "sleep" ) ) ) {
                u.wake_up();
            }
        }
    };

    // Tier-2 macro-step: once per macro_interval turns, if the monster has an
    // active wander destination (heard a sound), nudge it one step toward that
    // destination without running full AI.  Truly wandering monsters (wandf==0)
    // have no goal and remain stationary — they should NOT drift toward the player.
    const auto do_tier2_macro = [&]( monster & critter ) {
        if( current_turn % macro_interval == 0 &&
            critter.wandf > 0 && critter.wander_pos != critter.bub_pos() ) {
            const auto cpos      = critter.bub_pos();
            const auto macro_goal = critter.wander_pos;
            const std::array<tripoint_bub_ms, 4> dirs = { {
                    { cpos.x() + 1, cpos.y(),     cpos.z() },
                    { cpos.x() - 1, cpos.y(),     cpos.z() },
                    { cpos.x(),     cpos.y() + 1, cpos.z() },
                    { cpos.x(),     cpos.y() - 1, cpos.z() }
                }
            };
            int best_dist = rl_dist( cpos, macro_goal );
            auto best = cpos;
            for( const auto &t : dirs ) {
                const int d = rl_dist( t, macro_goal );
                if( d < best_dist && critter.can_move_to( t ) && is_empty( t ) ) {
                    best_dist = d;
                    best      = t;
                }
            }
            if( best != cpos ) {
                // NOTE: setpos() updates the critter-tracker position map but
                // does NOT call creature_in_field() for the new tile.  Field
                // damage (fire, acid, etc.) at the macro-step destination is
                // deferred to the next turn's LOD-B pass.
                critter.setpos( best );
            }
        }
        // else: no active sound cue — remain stationary this macro tick.
        critter.moves = 0;
        critter.next_turn = current_turn + 1;
    };

    const bool has_creature_do_turn_hooks = cata::has_hooks( "on_creature_do_turn" );
    const bool has_monster_do_turn_hooks = cata::has_hooks( "on_monster_do_turn" );
    auto monmove_executed_eligible = int64_t{ 0 };
    auto monmove_tier2_macros = int64_t{ 0 };
    auto monmove_move_iterations = int64_t{ 0 };
    auto monmove_preplans_used = int64_t{ 0 };
    auto monmove_fallback_plans = int64_t{ 0 };
    auto monmove_serial_replans = int64_t{ 0 };
    auto monmove_controlled_moves = int64_t{ 0 };
    {
        ZoneScopedN( "monmove_execute_eligible" );
        for( const auto &entry : eligible ) {
            monster &critter = *entry.second;
            if( critter.is_dead() ) {
                continue;
            }
            ++monmove_executed_eligible;
            if( has_creature_do_turn_hooks || has_monster_do_turn_hooks ) {
                ZoneScopedN( "monmove_turn_hooks" );
                if( has_creature_do_turn_hooks ) {
                    cata::run_hooks( "on_creature_do_turn", [&critter]( sol::table & params ) {
                        params["creature"] = static_cast<Creature *>( &critter );
                    } );
                }
                if( has_monster_do_turn_hooks ) {
                    cata::run_hooks( "on_monster_do_turn", [&critter]( sol::table & params ) {
                        params["monster"] = &critter;
                    } );
                }
            }
            if( critter.lod_tier == 2 ) {
                ++monmove_tier2_macros;
                {
                    ZoneScopedN( "monmove_tier2_macro" );
                    do_tier2_macro( critter );
                }
                {
                    ZoneScopedN( "monmove_bio_alarm" );
                    check_bio_alarm( critter );
                }
                continue;
            }
            bool used_preplan = false;
            while( critter.moves > 0 && !critter.is_dead() &&
                   !critter.has_effect( effect_ridden ) ) {
                ++monmove_move_iterations;
                critter.made_footstep = false;
                if( !critter.has_effect( effect_ai_controlled ) ) {
                    if( !used_preplan ) {
                        used_preplan = true;
                        const auto it = plan_index.find( &critter );
                        if( it == plan_index.end() ) {
                            ++monmove_fallback_plans;
                            {
                                ZoneScopedN( "monmove_fallback_plan" );
                                critter.plan();
                            }
                        } else {
                            ++monmove_preplans_used;
                            {
                                ZoneScopedN( "monmove_apply_precomputed_plan" );
                                critter.apply_plan( precomputed[it->second] );
                            }
                        }
                    } else {
                        ++monmove_serial_replans;
                        {
                            ZoneScopedN( "monmove_serial_replan" );
                            critter.plan();
                        }
                    }
                    const monster_action_t action = [&critter]() {
                        ZoneScopedN( "monmove_decide_action" );
                        return critter.decide_action();
                    }
                    ();
                    {
                        ZoneScopedN( "monmove_execute_action" );
                        critter.execute_action( action );
                    }
                } else {
                    ++monmove_controlled_moves;
                    {
                        ZoneScopedN( "monmove_controlled_move" );
                        critter.move();
                    }
                }
                {
                    ZoneScopedN( "monmove_process_triggers" );
                    critter.process_triggers();
                }
                {
                    ZoneScopedN( "monmove_execute_field" );
                    m.creature_in_field( critter );
                }
            }
            critter.next_turn = current_turn + 1;
            {
                ZoneScopedN( "monmove_bio_alarm" );
                check_bio_alarm( critter );
            }
        }
    }
    TracyPlot( "Monmove Executed Eligible", monmove_executed_eligible );
    TracyPlot( "Monmove Tier2 Macros", monmove_tier2_macros );
    TracyPlot( "Monmove Move Iterations", monmove_move_iterations );
    TracyPlot( "Monmove Preplans Used", monmove_preplans_used );
    TracyPlot( "Monmove Fallback Plans", monmove_fallback_plans );
    TracyPlot( "Monmove Serial Replans", monmove_serial_replans );
    TracyPlot( "Monmove Controlled Moves", monmove_controlled_moves );

    if( activity_skip_ai ) {
    ZoneScopedN( "monmove_activity_restore_lod" );
        for( const auto &[critter, real_lod_tier] : *activity_lod_restore ) {
            if( critter != nullptr ) {
                critter->lod_tier = real_lod_tier;
            }
        }
    }

    {
        ZoneScopedN( "monmove_cleanup_post_execute" );
        cleanup_dead();
    }

    // The remaining monsters are all alive, but may be outside of the reality bubble.
    // If so, despawn them. This is not the same as dying, they will be stored for later and the
    // monster::die function is not called.
    {
        ZoneScopedN( "monmove_despawn_oob" );
        for( monster &critter : all_monsters() ) {
            if( critter.bub_pos().x() < 0 - ( g_mapsize_x ) / 6 ||
                critter.bub_pos().y() < 0 - ( g_mapsize_y ) / 6 ||
                critter.bub_pos().x() > ( g_mapsize_x * 7 ) / 6 ||
                critter.bub_pos().y() > ( g_mapsize_y * 7 ) / 6 ) {
                despawn_monster( critter );
            }
        }
    }

    {
        ZoneScopedN( "monmove_cleanup_final" );
        cleanup_dead();
    }
}

// ——— static bool npc_is_always_full() ———
static bool npc_is_always_full( const npc &guy )
{
    return guy.is_player_ally() || guy.is_following() ||
           guy.mission == NPC_MISSION_ACTIVITY ||
           guy.mission == NPC_MISSION_GUARD_ALLY
#ifdef COOP_ENABLED
           || guy.is_coop_remote
#endif
           ;
}

// ——— static int npc_tier_assign_all() ———
static int npc_tier_assign_all()
{
    if( !npc_lod_enabled ) {
        for( npc &guy : g->all_npcs() ) {
            guy.npc_lod_tier     = 0;
            guy.npc_lod_cooldown = 0;
        }
        return 0;
    }

    const auto player_pos = g->u.bub_pos();
    const int tier01_dist = npc_tier0_dist;
    const int tier12_dist = std::max( npc_tier1_dist, tier01_dist + 1 );
    const int demote_cd   = npc_demotion_cooldown;
    // Player's approximate sight range — NPCs within this band are "potentially visible"
    // and must keep full AI.  Use g_max_view_distance (cached) as a cheap approximation
    // instead of calling sees() on every NPC.
    const int sight_radius = g_max_view_distance;
    int tier0_count = 0;

    for( npc &guy : g->all_npcs() ) {
        int8_t new_tier;

        // Followers/companions (and co-op proxy NPC) are always Tier 0.
        if( npc_is_always_full( guy ) ) {
            new_tier = 0;
        } else {
            const int dist = lod_dist( guy.bub_pos(), player_pos );
            const int abs_dz = std::abs( guy.bub_pos().z() - player_pos.z() );

            // NPCs within player sight radius are potentially visible — keep full AI.
            if( dist <= tier01_dist || ( abs_dz < 2 && dist <= sight_radius ) ) {
                new_tier = 0;
            } else if( dist <= tier12_dist ) {
                new_tier = 1;
            } else {
                new_tier = 2;
            }
        }

        // Promotion immediate; demotion respects cooldown.
        if( new_tier < guy.npc_lod_tier ) {
            guy.npc_lod_tier     = new_tier;
            guy.npc_lod_cooldown = 0;
        } else if( new_tier > guy.npc_lod_tier && guy.npc_lod_cooldown <= 0 ) {
            guy.npc_lod_tier     = new_tier;
            guy.npc_lod_cooldown = static_cast<int8_t>( demote_cd );
        }

        if( guy.npc_lod_cooldown > 0 ) {
            guy.npc_lod_cooldown--;
        }

        if( guy.npc_lod_tier == 0 ) {
            ++tier0_count;
        }
    }

    TracyPlot( "NPC LOD Tier 0 (Full)",  static_cast<int64_t>( tier0_count ) );
    return tier0_count;
}

// ——— void game::npcmove() ———
void game::npcmove()
{
    ZoneScoped;
    // Active NPC processing.  Extracted from monmove() so it can be
    // individually controlled by SLEEP_SKIP_NPC without affecting monsters.
    ++g_npcmove_attitude_epoch;
    processing_npcs_ = true;
    const bool has_creature_do_turn_hooks = cata::has_hooks( "on_creature_do_turn" );
    const bool has_npc_do_turn_hooks = cata::has_hooks( "on_npc_do_turn" );

    // Assign LOD tiers and build processing lists for tier + budget.
    const int tier0_count = npc_tier_assign_all();
    const auto player_pos = u.bub_pos();
    const int effective_budget = npc_action_budget > 0
                                 ? std::max( npc_action_budget, tier0_count )
                                 : std::numeric_limits<int>::max();

    // Separate always-process (followers + Tier 0) from budgeted (Tier 1+2).
    std::vector<npc *> always_process;
    std::vector<std::pair<int, npc *>> budgeted;
    for( npc &guy : g->all_npcs() ) {
        if( !guy.is_simulated() ) {
            continue;
        }
        if( npc_is_always_full( guy ) || guy.npc_lod_tier == 0 ) {
            always_process.push_back( &guy );
        } else {
            budgeted.emplace_back( lod_dist( guy.bub_pos(), player_pos ), &guy );
        }
    }

    // Cap budgeted NPCs by distance — farthest are deferred.
    if( static_cast<int>( budgeted.size() ) > effective_budget ) {
        std::nth_element( budgeted.begin(),
                          budgeted.begin() + effective_budget,
                          budgeted.end() );
        for( auto &entry : budgeted |
             std::views::drop( static_cast<size_t>( effective_budget ) ) ) {
            entry.second->moves = 0;
        }
        budgeted.resize( effective_budget );
    }

    // Lambda to process a single NPC with tier-dependent AI.
    const auto process_npc = [&]( npc * guy_ptr ) -> void {
        npc &guy = *guy_ptr;
        const bool is_tier_2 = guy.npc_lod_tier == 2;

        if( has_creature_do_turn_hooks || has_npc_do_turn_hooks )
        {
            ZoneScopedN( "npc_turn_hooks" );
            if( has_creature_do_turn_hooks ) {
                cata::run_hooks( "on_creature_do_turn", [&guy]( sol::table & params ) {
                    params["creature"] = static_cast<Creature *>( &guy );
                } );
            }
            if( has_npc_do_turn_hooks ) {
                cata::run_hooks( "on_npc_do_turn", [&guy]( sol::table & params ) {
                    params["npc"] = &guy;
                } );
            }
        }

        if( guy.is_mounted() )
        {
            guy.check_mount_is_spooked();
        }
        m.creature_in_field( guy );
        {
            ZoneScopedN( "npc_process_turn" );
            if( !guy.has_effect( effect_npc_suspend ) )
            {
                guy.process_turn();
            }
        }

        if( is_tier_2 )
        {
            // Tier 2: process_turn + occasional macro step (via move() early return).
            if( !guy.is_dead() && guy.moves > 0 ) {
                guy.move();
            }
        } else
        {
            // Tier 0/1: full move loop.
            int turns = 0;
            while( !guy.is_dead() && guy.moves > 0 && turns < 10 &&
                   ( !guy.in_sleep_state() || guy.activity->id() == ACT_OPERATION )
                 ) {
                ZoneScopedN( "npc_move_iter" );
                int moves = guy.moves;
                guy.move();
                if( moves == guy.moves ) {
                    turns++;
                }
                if( turns == 9 ) {
                    debugmsg( "NPC %s entered infinite loop.  Turning on debug mode",
                              guy.name );
                    debug_mode = true;
                }
            }
            if( turns == 10 ) {
                add_msg( _( "%s faints!" ), guy.name );
                guy.reboot();
            }
        }

        if( !guy.is_dead() )
        {
            guy.npc_update_body();
        }
    };

    for( npc *guy : always_process ) {
        process_npc( guy );
    }
    for( auto &entry : budgeted ) {
        process_npc( entry.second );
    }
    processing_npcs_ = false;
    cleanup_dead();
}

// ——— void game::sleep_skip_npc_process() ———
void game::sleep_skip_npc_process()
{
    // SLEEP_SKIP_NPC is active: NPC movement is suppressed while the player
    // sleeps.  Instead, NPCs are forced to sleep alongside the player so they
    // recover fatigue, heal wounds, etc. via the normal sleep path.
    //
    // NPCs whose current activity is not suspendable (e.g. ACT_OPERATION) are
    // left frozen for the turn rather than interrupted mid-activity.

    processing_npcs_ = true;
    // Every ~30 in-game minutes, re-examine sleeping NPCs and wake any whose
    // sleep need is satisfied or whose player has woken up.  Otherwise, renew
    // their lying-down effect for another 30-minute window.
    if( calendar::once_every( 30_minutes ) ) {
        for( npc &guy : g->all_npcs() ) {
            if( guy.is_dead() || !guy.in_sleep_state() ) {
                continue;
            }
            // Wake threshold: fatigue <= 25 mirrors the natural wake-up check in
            // player_hardcoded_effects.cpp (effect_sleep handler).
            const bool player_awake = !u.in_sleep_state();
            const bool npc_rested   = guy.get_fatigue() <= 25;
            if( player_awake || npc_rested ) {
                guy.wake_up();
            } else {
                // Renew for another 30 minutes so the effect does not expire mid-sleep.
                guy.add_effect( effect_lying_down, 30_minutes, bodypart_str_id::NULL_ID(), 1 );
            }
        }
    }

    for( npc &guy : g->all_npcs() ) {
        if( guy.is_dead() ) {
            continue;
        }

        if( !guy.in_sleep_state() ) {
            // If the NPC has an active non-suspendable activity, leave them alone.
            if( *guy.activity && !guy.activity->is_suspendable() ) {
                continue;
            }
            // Cancel any suspendable activity (it goes to the backlog for later
            // resumption) and begin sleep via effect_lying_down.
            if( *guy.activity ) {
                guy.cancel_activity();
            }
            // Only put the NPC to sleep if they still need rest.  This avoids
            // re-sleeping NPCs that were just woken because their fatigue was satisfied.
            if( !guy.has_effect( effect_lying_down ) && guy.get_fatigue() > 25 ) {
                guy.add_effect( effect_lying_down, 30_minutes, bodypart_str_id::NULL_ID(), 1 );
            }
        }

        // Run the same per-turn housekeeping that normal npcmove() would:
        // field damage, effect ticking via process_turn(), and sleep-recovery
        // body updates (fatigue, healing, etc.) via npc_update_body().
        // The move loop is intentionally skipped — sleeping NPCs don't move.
        if( guy.is_mounted() ) {
            guy.check_mount_is_spooked();
        }
        m.creature_in_field( guy );
        if( !guy.has_effect( effect_npc_suspend ) ) {
            guy.process_turn();
        }
        guy.npc_update_body();
    }
    processing_npcs_ = false;
    cleanup_dead();
}

// ——— void game::overmap_npc_move() ———
void game::overmap_npc_move()
{
    ZoneScoped;
    std::vector<npc *> travelling_npcs;
    static constexpr int move_search_radius = 600;
    for( auto &elem : get_overmapbuffer( current_dimension_id_ ).get_npcs_near_player(
             move_search_radius ) ) {
        if( !elem ) {
            continue;
        }
        npc *npc_to_add = elem.get();
        if( ( !npc_to_add->is_active() || rl_dist( u.bub_pos(), npc_to_add->bub_pos() ) > SEEX * 2 ) &&
            npc_to_add->mission == NPC_MISSION_TRAVELLING ) {
            travelling_npcs.push_back( npc_to_add );
        }
    }
    for( auto &elem : travelling_npcs ) {
        if( elem->has_omt_destination() ) {
            if( !elem->omt_path.empty() && rl_dist( elem->omt_path.back(), elem->abs_omt_pos() ) > 2 ) {
                //recalculate path, we got distracted doing something else probably
                elem->omt_path.clear();
            }
            if( elem->omt_path.empty() ) {
                const tripoint_abs_omt &from = elem->abs_omt_pos();
                const tripoint_abs_omt &to = elem->goal;
                elem->omt_path = get_overmapbuffer( elem->get_dimension() ).get_travel_path(
                                     elem->abs_omt_pos(), elem->goal,
                                     overmap_path_params::for_npc() );
                if( elem->omt_path.empty() ) {
                    add_msg( m_debug, "%s couldn't find overmap path from %s to %s",
                             elem->get_name(), from.to_string(), to.to_string() );
                    elem->goal = npc::no_goal_point;
                    elem->mission = NPC_MISSION_NULL;
                }
            } else {
                if( elem->omt_path.back() == elem->abs_omt_pos() ) {
                    elem->omt_path.pop_back();
                }
                // TODO: fix point types
                elem->travel_overmap(
                    project_to<coords::sm>( elem->omt_path.back() ) );
            }
            reload_npcs();
        }
    }
    return;
}

// ——— void game::replace_stair_monsters() ———
void game::replace_stair_monsters()
{
    for( auto &elem : coming_to_stairs ) {
        elem->staircount = 0;
        const tripoint_bub_ms pnt( elem->bub_pos().xy(), get_levz() );
        place_critter_around( elem, pnt, 10 );
    }

    coming_to_stairs.clear();
}

// ——— void game::update_stair_monsters() ———
void game::update_stair_monsters()
{
    ZoneScoped;

    // Search for the stairs closest to the player.
    std::vector<int> stairx;
    std::vector<int> stairy;
    std::vector<int> stairdist;

    const bool from_below = monstairz < get_levz();

    if( coming_to_stairs.empty() ) {
        return;
    }

    if( m.has_zlevels() ) {
        debugmsg( "%d monsters coming to stairs on a map with z-levels",
                  coming_to_stairs.size() );
        coming_to_stairs.clear();
    }

    for( const tripoint_bub_ms &dest : m.points_on_zlevel( u.bub_pos().z() ) ) {
        if( ( from_below && m.has_flag( "GOES_DOWN", dest ) ) ||
            ( !from_below && m.has_flag( "GOES_UP", dest ) ) ) {
            stairx.push_back( dest.x() );
            stairy.push_back( dest.y() );
            stairdist.push_back( rl_dist( dest, u.bub_pos() ) );
        }
    }
    if( stairdist.empty() ) {
        return;         // Found no stairs?
    }

    // Find closest stairs.
    size_t si = 0;
    for( size_t i = 0; i < stairdist.size(); i++ ) {
        if( stairdist[i] < stairdist[si] ) {
            si = i;
        }
    }

    // Find up to 4 stairs for distance stairdist[si] +1
    std::vector<int> nearest;
    nearest.push_back( si );
    for( size_t i = 0; i < stairdist.size() && nearest.size() < 4; i++ ) {
        if( ( i != si ) && ( stairdist[i] <= stairdist[si] + 1 ) ) {
            nearest.push_back( i );
        }
    }
    // Randomize the stair choice
    si = random_entry_ref( nearest );

    // Attempt to spawn zombies.
    for( size_t i = 0; i < coming_to_stairs.size(); i++ ) {
        point_bub_ms mpos( stairx[si], stairy[si] );
        monster &critter = *coming_to_stairs[i];
        const tripoint_bub_ms dest {
            mpos, g->get_levz()
        };

        // We might be not be visible.
        if( ( critter.bub_pos().x() < 0 - ( g_mapsize_x ) / 6 ||
              critter.bub_pos().y() < 0 - ( g_mapsize_y ) / 6 ||
              critter.bub_pos().x() > ( g_mapsize_x * 7 ) / 6 ||
              critter.bub_pos().y() > ( g_mapsize_y * 7 ) / 6 ) ) {
            continue;
        }

        critter.staircount -= 4;
        // Let the player know zombies are trying to come.
        if( u.sees( dest ) ) {
            std::string dump;
            if( critter.staircount > 4 ) {
                dump += string_format( _( "You see a %s on the stairs" ), critter.name() );
            } else {
                if( critter.staircount > 0 ) {
                    dump += ( from_below ?
                              //~ The <monster> is almost at the <bottom/top> of the <terrain type>!
                              string_format( _( "The %1$s is almost at the top of the %2$s!" ),
                                             critter.name(),
                                             m.tername( dest ) ) :
                              string_format( _( "The %1$s is almost at the bottom of the %2$s!" ),
                                             critter.name(),
                                             m.tername( dest ) ) );
                }
            }

            add_msg( m_warning, dump );
        } else {
            sounds::sound( dest, 5, sounds::sound_t::movement,
                           _( "a sound nearby from the stairs!" ), true, "misc", "stairs_movement" );
        }

        if( critter.staircount > 0 ) {
            continue;
        }

        if( is_empty( dest ) ) {
            critter.spawn( dest );
            critter.staircount = 0;
            place_critter_at( make_shared_fast<monster>( critter ), dest );
            if( u.sees( dest ) ) {
                if( !from_below ) {
                    add_msg( m_warning, _( "The %1$s comes down the %2$s!" ),
                             critter.name(),
                             m.tername( dest ) );
                } else {
                    add_msg( m_warning, _( "The %1$s comes up the %2$s!" ),
                             critter.name(),
                             m.tername( dest ) );
                }
            }
            coming_to_stairs.erase( coming_to_stairs.begin() + i );
            continue;
        } else if( u.bub_pos() == dest ) {
            // Monster attempts to push player of stairs
            point_rel_ms push( point_north_west );
            int tries = 0;

            // the critter is now right on top of you and will attack unless
            // it can find a square to push you into with one of his tries.
            const int creature_push_attempts = 9;
            const int player_throw_resist_chance = 3;

            critter.spawn( dest );
            while( tries < creature_push_attempts ) {
                tries++;
                push.x() = rng( -1, 1 );
                push.y() = rng( -1, 1 );
                point_bub_ms ipos( mpos + push );
                tripoint_bub_ms pos( ipos, get_levz() );
                if( ( push.x() != 0 || push.y() != 0 ) && !critter_at( pos ) &&
                    critter.can_move_to( pos ) ) {
                    bool resiststhrow = ( u.is_throw_immune() ) ||
                                        ( u.has_trait( trait_LEG_TENT_BRACE ) );
                    if( resiststhrow && one_in( player_throw_resist_chance ) ) {
                        u.moves -= 25; // small charge for avoiding the push altogether
                        add_msg( _( "The %s fails to push you back!" ),
                                 critter.name() );
                        return; //judo or leg brace prevent you from getting pushed at all
                    }
                    // Not accounting for tentacles latching on, so..
                    // Something is about to happen, lets charge half a move
                    u.moves -= 50;
                    if( resiststhrow && ( u.is_throw_immune() ) ) {
                        //we have a judoka who isn't getting pushed but counterattacking now.
                        mattack::thrown_by_judo( &critter );
                        return;
                    }
                    std::string msg;
                    ///\EFFECT_DODGE reduces chance of being downed when pushed off the stairs
                    if( !( resiststhrow ) && ( u.get_dodge() + rng( 0, 3 ) < 12 ) ) {
                        // dodge 12 - never get downed
                        // 11.. avoid 75%; 10.. avoid 50%; 9.. avoid 25%
                        u.add_effect( effect_downed, 2_turns );
                        msg = _( "The %s pushed you back hard!" );
                    } else {
                        msg = _( "The %s pushed you back!" );
                    }
                    add_msg( m_warning, msg.c_str(), critter.name() );
                    u.setpos( u.bub_pos() + push );
                    return;
                }
            }
            add_msg( m_warning,
                     _( "The %s tried to push you back but failed!  It attacks you!" ),
                     critter.name() );
            critter.melee_attack( u );
            u.moves -= 50;
            return;
        } else if( monster *const mon_ptr = critter_at<monster>( dest ) ) {
            // Monster attempts to displace a monster from the stairs
            monster &other = *mon_ptr;
            critter.spawn( dest );

            // the critter is now right on top of another and will push it
            // if it can find a square to push it into inside of his tries.
            const int creature_push_attempts = 9;
            const int creature_throw_resist = 4;

            int tries = 0;
            point push2;
            while( tries < creature_push_attempts ) {
                tries++;
                push2.x = rng( -1, 1 );
                push2.y = rng( -1, 1 );
                point_bub_ms ipos2( mpos + push2 );
                tripoint_bub_ms pos( ipos2, get_levz() );
                if( ( push2.x == 0 && push2.y == 0 ) || ( ( ipos2.x() == u.bub_pos().x() ) &&
                        ( ipos2.y() == u.bub_pos().y() ) ) ) {
                    continue;
                }
                if( !critter_at( pos ) && other.can_move_to( pos ) ) {
                    other.setpos( tripoint_bub_ms( ipos2, get_levz() ) );
                    other.moves -= 50;
                    std::string msg;
                    if( one_in( creature_throw_resist ) ) {
                        other.add_effect( effect_downed, 2_turns );
                        msg = _( "The %1$s pushed the %2$s hard." );
                    } else {
                        msg = _( "The %1$s pushed the %2$s." );
                    }
                    add_msg( m_neutral, msg, critter.name(), other.name() );
                    return;
                }
            }
            return;
        }
    }
}

void game::despawn_monster( monster &critter )
{
    if( !critter.is_hallucination() ) {
        // hallucinations aren't stored, they come and go as they like,
        get_overmapbuffer( critter.get_dimension() ).despawn_monster( critter );
    }

    critter.on_unload();
    remove_zombie( critter );
    // simulate it being dead so further processing of it (e.g. in monmove) will yield
    critter.set_hp( 0 );
}

// ——— void game::shift_monsters() ———
void game::shift_monsters( const tripoint_rel_sm &shift )
{
    // If either shift argument is non-zero, we're shifting.
    if( shift == tripoint_rel_sm::zero() ) {
        return;
    }
    for( monster &critter : all_monsters() ) {
        if( shift.xy() != point_rel_sm::zero() ) {
            critter.shift( shift.xy() );
        }

        if( ( shift.z() == 0 || m.has_zlevels() )
            && m.get_submap_at( tripoint_bub_ms( critter.bub_pos() ) ) != nullptr ) {
            // The critter is on a loaded submap — keep it regardless of whether
            // it's inside the render-area grid (inbounds).  Creatures can validly
            // reside in loaded-but-OOB submaps (e.g. knocked into a lazy-border
            // zone) and should not be despawned just because they are outside
            // the render area.
            continue;
        }
        // Either a vertical shift, the critter is outside the reality bubble, or it
        // landed on a null corner slot — save and remove it.
        despawn_monster( critter );
    }
    // The order in which zombies are shifted may cause zombies to briefly exist on
    // the same square. This messes up the mon_at cache, so we need to rebuild it.
    critter_tracker->rebuild_cache();
}

double npc_overmap::spawn_chance_in_hour( int current_npc_count, double density )
{
    static constexpr int days_in_year = 14 * 4;
    const double expected_npc_count = days_in_year * density;
    const double overcrowding_ratio = current_npc_count / expected_npc_count;
    if( overcrowding_ratio < 1.0 ) {
        return std::min( 1.0, density / 24.0 );
    }
    return ( 1.0 / 24.0 ) / overcrowding_ratio;
}

// ——— void game::perhaps_add_random_npc() ———
void game::perhaps_add_random_npc()
{
    ZoneScoped;
    static constexpr time_duration spawn_interval = 1_hours;
    if( !calendar::once_every( spawn_interval ) ) {
        return;
    }
    // Create a new NPC?
    // Only allow NPCs on 0 z-level, otherwise they can bug out due to lack of spots
    if( !get_option<bool>( "RANDOM_NPC" ) || ( !m.has_zlevels() && get_levz() != 0 ) ) {
        return;
    }

    // We want the "NPC_DENSITY" to denote number of NPCs per week, per overmap, or so
    // But soft-cap it at about a standard year (4*14 days) worth
    const int npc_num = get_overmapbuffer( current_dimension_id_ ).get_npcs_near_player(
                            npc_overmap::density_search_radius ).size();
    const double chance = npc_overmap::spawn_chance_in_hour( npc_num,
                          get_option<float>( "NPC_DENSITY" ) );
    add_msg( m_debug, "Random NPC spawn chance %0.3f%%", chance * 100 );
    if( !x_in_y( chance, 1.0f ) ) {
        return;
    }

    bool spawn_allowed = false;
    tripoint_abs_omt spawn_point;
    int counter = 0;
    while( !spawn_allowed ) {
        if( counter >= 100 ) {
            return;
        }
        // Shouldn't be larger than search radius or it might get swarmy at the edges
        static constexpr int radius_spawn_range = npc_overmap::density_search_radius;
        const tripoint_abs_omt u_omt = u.abs_omt_pos();
        spawn_point = u_omt + point( rng( -radius_spawn_range, radius_spawn_range ),
                                     rng( -radius_spawn_range, radius_spawn_range ) );
        spawn_point.z() = 0;
        const oter_id oter = get_overmapbuffer( current_dimension_id_ ).ter( spawn_point );
        // Shouldn't spawn on lakes or rivers.
        // TODO: Prefer greater distance
        if( !is_river_or_lake( oter ) || rl_dist( u_omt.xy(), spawn_point.xy() ) < 30 ) {
            spawn_allowed = true;
        }
        counter += 1;
    }
    shared_ptr_fast<npc> tmp = make_shared_fast<npc>();
    tmp->randomize();
    std::string new_fac_id = "solo_";
    new_fac_id += tmp->name;
    // create a new "lone wolf" faction for this one NPC
    faction *new_solo_fac = faction_manager_ptr->add_new_faction( tmp->name, faction_id( new_fac_id ),
                            faction_id( "no_faction" ) );
    tmp->set_fac( new_solo_fac ? new_solo_fac->id() : faction_id( "no_faction" ) );
    // adds the npc to the correct overmap.
    // Only spawn random NPCs on z-level 0
    auto submap_spawn = project_to<coords::sm>( spawn_point );
    tmp->spawn_at_sm( tripoint_abs_sm( submap_spawn.xy(), 0 ) );
    get_overmapbuffer( current_dimension_id_ ).insert_npc( tmp );
    tmp->form_opinion( u );
    tmp->mission = NPC_MISSION_NULL;
    tmp->long_term_goal_action();
    tmp->add_new_mission( mission::reserve_random( ORIGIN_ANY_NPC, tmp->abs_omt_pos(),
                          tmp->getID() ) );
    dbg( DL::Debug ) << "Spawning a random NPC at " << spawn_point;
    cata::run_hooks( "on_creature_spawn", [&]( sol::table & params ) {
        params["creature"] = tmp.get();
    } );
    cata::run_hooks( "on_npc_spawn", [&]( sol::table & params ) {
        params["npc"] = tmp.get();
    } );
    // This will make the new NPC active- if its nearby to the player
    load_npcs();
}

