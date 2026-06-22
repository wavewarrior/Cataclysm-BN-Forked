# NPC AI Modernization — Plan

## Context

`npcmove.cpp` (4888 lines) is the largest game-AI file in the project. It uses a 25-value `npc_action` C enum and a ~400-line `execute_action()` switch to dispatch NPC behavior. `method_of_attack()` mixes targeting, ammo, range, and behavior selection in one monolithic function. Zero `std::ranges`, zero `std::views`, zero options structs.

The `SIM_PERFORMANCE_PLAN Part 2` (NPC LOD/budget sketch) addresses perf gating but not code quality. This plan covers structural modernization only.

| Metric | npcmove.cpp | npctalk.cpp | npc.cpp |
|--------|-------------|-------------|---------|
| Lines | 4888 | 3919 | 3790 |
| `ranges::` | 0 | 0 | 2 |
| `std::views` | 0 | 0 | 0 |
| Trailing returns | few | few | ~10 |
| Raw `Creature*` params | ~20 sites | ~5 | ~10 |

## Phases

### Phase 1 — Strategy pattern for NPC actions

Current dispatch flow:
```cpp
enum npc_action : int {
    npc_undecided = 0,
    npc_pause, npc_reload, npc_sleep, // 25+ values
};

void npc::execute_action( npc_action action ) {
    switch( action ) {
        case npc_pause: move_pause(); break;
        case npc_reload: do_reload( primary_weapon() ); break;
        // 400+ lines, 25+ cases
    }
}
```

Replace with:

```cpp
struct npc_goal {
    virtual ~npc_goal() = default;
    virtual auto utility( const npc &who, const Character &target ) const -> float = 0;
    virtual auto execute( npc &who, const Character &target ) -> bool = 0;
    virtual auto id() const -> npc_goal_id = 0;
};
```

Goals are registered in a `std::vector<std::unique_ptr<npc_goal>>` at startup. `execute_action()` becomes `goals_by_utility[0].execute()`. The `switch` is replaced by the planner scanning candidates by `utility()` descending.

### Phase 2 — Decompose `method_of_attack()`

Current: `method_of_attack()` (~200 lines in `npcmove.cpp`) evaluates all attack options inline. Split into:

| Function | Returns | Concern |
|----------|---------|---------|
| `select_target()` | `Creature *` or `std::optional<weak_ptr<Creature>>` | Which enemy to attack |
| `evaluate_range()` | `struct range_assessment { int dist; int accuracy; bool in_range; }` | Distance + accuracy check |
| `check_ammo()` | `struct ammo_assessment { bool has_ammo; bool has_magazine; int reload_turns; }` | Ammo + reload status |
| `choose_attack_type()` | `npc_goal_id` or `attack_type` enum | Final action selection |

Each returns a named struct (no out-params via references).

### Phase 3 — Extract perception from AI loop

`regen_ai_cache()` (in `npcmove.cpp`) handles perception (listening, sight checks, sound tracking) mixed with AI evaluation. Extract all sensory functions into a `npc_perception` module:

- `npc_perception::hear_sounds()`
- `npc_perception::see_targets()`
- `npc_perception::track_target()`

The `npc` retains a `npc_perception` member. `regen_ai_cache()` calls perception first, then AI evaluation. This decoupling is what `SIM_PERFORMANCE_PLAN Part 2` needs to gate perception and AI independently.

### Phase 4 — NPC talk modernization

`npctalk.cpp` (3919 lines) has significant legacy:
- Dialogue `switch` on `talk_function` enums (should be function table)
- Mission-grant logic mixing with NPC response generation
- Raw `std::string` for dialogue options instead of typed IDs

Scope: extract mission-giving dialogue into `npctalk_missions.cpp`, replace talk-function switch with map dispatch, use `trial_id`/`dialogue_id` typed strings.

### Phase 5 — C++23 modernization pass

- Trailing return types in all touched files.
- `ranges::*` for NPC filtering in pathfinding and target selection.
- Options structs for functions with 4+ bare params.
- `Creature*` → `Creature&` where guaranteed non-null.
- `std::expected` for operations that may fail (pathfinding, target acquisition).

## Verification (per phase)

- Build green. Tracy `npcmove` zone: no regression.
- NPC behavior A/B: same-save companion actions identical (same attack choices, same movement).
- Phase 1: `rg "npc_action" src/` usage count drops.
- Phase 3: `rg "regen_ai_cache" src/` shows separation of perception/AI calls.
- Phase 5: `rg "for\s*\(.*int\s+\w+\s*=\s*0" src/npcmove.cpp` drops below 3.

## Files

| File | Phase |
|------|-------|
| `src/npcmove.cpp` | 1 (strategy), 2 (decomp), 5 (modernize) |
| `src/npc.h` | 1 (goal types + member), 3 (perception member) |
| `src/npc_perception.{h,cpp}` (new) | 3 |
| `src/npctalk.cpp` | 4 |
| `src/npctalk_missions.cpp` (new) | 4 |
| `src/CMakeLists.txt` | 1, 3, 4 (new files) |

## Effort: 2–3 weeks
- Phase 1: 3–4 days (strategy pattern, highest risk)
- Phase 2: 1–2 days (decompose one function)
- Phase 3: 2–3 days (perception extraction)
- Phase 4: 2–3 days (NPC talk)
- Phase 5: 1–2 days (C++23)
