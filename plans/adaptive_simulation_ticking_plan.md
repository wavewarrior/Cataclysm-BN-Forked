# Adaptive Simulation Ticking with Spatial Interest Management

## Problem

C:BN's simulation is its strongest feature — every NPC, vehicle, weather cell, and terrain tile updates each turn. As the world grows in scope, this becomes an O(n) tax on every player action. Deeper NPC AI, larger maps, more monsters, and persistent world changes are all bottlenecked by the flat simulation budget.

## Vision

A **two-tier simulation loop** that allocates simulation budget proportional to the player's proximity and interest, while maintaining world consistency through catch-up simulation when zones are revisited.

```
Distance from player
├── 0-10 tiles  → Full tick (every detail, every turn)
├── 11-50 tiles → Reduced tick (1/4 rate, core state only)
└── 50+ tiles   → Dormant (event-driven: triggers, arrivals, weather fronts)
```

## Why This Is the Lever

1. **Unlocks everything downstream** — deeper NPC AI, larger maps, persistent world changes, more concurrent entities. All become feasible because the per-turn budget is no longer flat.

2. **Invisible to the player** — within line of sight, nothing changes. Beyond perception, the world still simulates at reduced fidelity. When the player arrives, the state catches up to a consistent point.

3. **Built on existing architecture** — C:BN's `mapblock`-based world loading is already a spatial partition. Each mapblock has its own update path; the change is gating update frequency per block's distance tier, not adding a new system.

4. **Pure infrastructure, no new content** — every existing mechanic benefits immediately. No new gameplay systems, just better allocation of existing ones.

## Architecture

### Tier Classification

Each loaded mapblock is classified into a tier based on its distance from the player's current position:

| Tier | Distance | Tick Rate | Updates |
|------|----------|-----------|---------|
| **Hot** | 0-10 tiles | Every turn | Full: combat, crafting, movement, terrain, vehicles, monsters |
| **Warm** | 11-50 tiles | Every 4 turns | Core: movement, basic state changes, vehicle progression |
| **Cold** | 50+ tiles | Event-driven | Triggers only: quest events, vehicle arrivals, weather fronts, timer-expiring hazards |

### Mapblock Tier Assignment

Leverage the existing mapblock loading system. Each mapblock tracks:
- Current tier (hot/warm/cold)
- Accumulated elapsed turns since last full simulation
- Last simulated turn number (for catch-up consistency)

On each player turn:
1. Recalculate tier for each loaded mapblock based on player position.
2. Promote/demote mapblocks as needed.
3. Simulate the appropriate number of turns for each tier.

### Catch-Up Simulation

When a cold/warm mapblock is promoted to hot (player enters the area):

1. **Batch simulate** the elapsed turns in a single pass — fast because it's not interleaved with player input or UI updates.
2. **Apply state changes** — NPCs have moved, fires have burned, vehicles have arrived, terrain has changed.
3. **Present consistently** — the player sees the result, not the process. The world state is as if it simulated every turn.

Catch-up simulation skips intermediate frames that only matter for animation or visual effects. It advances state: positions, health, inventory, terrain damage, vehicle fuel, etc.

### Event-Driven Cold Simulation

Cold mapblocks don't tick every turn, but they register **events** that can fire asynchronously:

- **Timer events** — a bomb set to detonate in 100 turns fires when the global turn counter reaches the threshold, even if the mapblock is cold.
- **Arrival events** — a vehicle scheduled to arrive at a location processes when the turn counter matches.
- **Weather events** — a storm front moves across cold mapblocks at reduced resolution (advance the weather state, not every raindrop).
- **Quest events** — NPC dialogue triggers, faction state changes, mission deadlines.

Events are stored in a global priority queue keyed on turn number. When the global turn advances past an event's trigger, the event fires and simulates its effect on the target mapblock.

## Implementation Phases

### Phase 1: Mapblock Distance Tracking

- [ ] Add distance tracking to each loaded mapblock (Manhattan or Euclidean from player).
- [ ] Classify mapblocks into hot/warm/cold tiers each turn.
- [ ] Log tier transitions for debugging.

### Phase 2: Tiered Tick Rates

- [ ] Implement reduced tick rate for warm mapblocks (simulate every N turns).
- [ ] Implement cold mapblock dormancy (skip simulation, track elapsed turns).
- [ ] Ensure hot mapblocks behave identically to current behavior (regression guard).

### Phase 3: Catch-Up Simulation

- [ ] Implement batch simulation pass for mapblocks transitioning from warm/cold to hot.
- [ ] Elide intermediate visual-only frames; advance state directly.
- [ ] Test world consistency: player leaves, returns, state matches expected progression.

### Phase 4: Event-Driven Cold Simulation

- [ ] Create global event queue for cold mapblock triggers.
- [ ] Port timer-based mechanics (bombs, traps, vehicle schedules) to event queue.
- [ ] Implement weather front propagation across cold mapblocks.

### Phase 5: Tuning and Polish

- [ ] Tune tier distances and tick rates for target framerate.
- [ ] Add debug overlay showing mapblock tiers.
- [ ] Profile and optimize catch-up simulation path.
- [ ] Document behavior for mod authors.

## Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| **Inconsistent world state** — player exploits tier transitions to freeze enemies or preserve favorable conditions. | Catch-up simulation always advances to current turn. No "frozen in time" exploits. |
| **Missed events** — a cold mapblock's event fires but the mapblock isn't loaded. | Event queue defers until mapblock is loaded; if never loaded, event is discarded (it was off-screen anyway). |
| **Performance regression on catch-up** — large batch simulation spikes the frame time. | Cap catch-up to a maximum number of turns per frame; spread across multiple frames if needed. |
| **Mod compatibility** — mods assume every entity ticks every turn. | Provide hooks for mods to declare their simulation tier preference; default to current behavior. |

## Metrics for Success

- **Per-turn simulation time** reduced by ≥50% with a fully loaded world at max distance.
- **Framerate** maintained at target with 2x the concurrent entities.
- **World consistency** — no observable difference in hot-zone behavior vs. current behavior.
- **Catch-up time** — under 100ms for a single mapblock catching up from cold to hot.

## References

- Existing mapblock architecture: `mapblock.h`, `mapdata.cpp`
- Current simulation loop: `monster.cpp` (monster turns), `vehicle.cpp` (vehicle simulation), `terrain.cpp` (terrain updates)
- Related plans: `SIM_PERFORMANCE_PLAN.md`, `MAP_PERFORMANCE_ROADMAP.md`
