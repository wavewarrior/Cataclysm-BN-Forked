# Condition Dispatch Refactor — Plan

## Context

`condition.cpp` (1168 lines) has a ~50+ `if/else if` chain dispatching string condition names to lambda setters. This pattern appears in `set_condition()` and `get_condition()` — two parallel chains that map string identifiers (e.g. `"has_available_mission"`, `"is_season"`) to the corresponding getter/setter lambdas.

The current code looks like:

```cpp
if( type == "has_available_mission" ) {
    set_has_available_mission();
} else if( type == "has_many_available_missions" ) {
    set_has_many_available_missions();
} else if( type == "mission_complete" ) {
    set_mission_complete();
} // ... 50+ more else-if branches
```

Problems:
- O(n) average dispatch time (branch predictor degrades past ~20 entries)
- Adding a new condition requires inserting in the correct position in the chain
- The two chains (`set_condition` / `get_condition`) can drift — adding to one and forgetting the other is a silent bug
- Reviewers must scan 50+ branches to find the one they care about

## Approach

Single phase — replace both if-else chains with a `static const` lookup table.

### Data structure

```cpp
struct condition_entry {
    std::string_view name;
    std::function<auto( const dialogue &, const std::string & ) -> bool> getter;
    std::function<auto( dialogue &, const std::string & ) -> bool> setter;
};

static const auto condition_table = std::to_array<condition_entry>( {
    { "has_available_mission",        &get_has_available_mission,        &set_has_available_mission },
    { "has_many_available_missions",  &get_has_many_available_missions,  &set_has_many_available_missions },
    { "mission_complete",             &get_mission_complete,             &set_mission_complete },
    // ... all 50+ entries
} );
```

Then look up:

```cpp
auto get_condition( const std::string &type, const dialogue &d, const std::string &arg ) -> bool {
    for( const auto &e : condition_table ) {
        if( e.name == type ) return e.getter( d, arg );
    }
    debugmsg( "unknown condition: %s", type );
    return false;
}
```

Or for speed (the table is static and known at compile time):

```cpp
static const auto condition_map = [] {
    std::unordered_map<std::string_view, const condition_entry *> m;
    for( const auto &e : condition_table ) m.emplace( e.name, &e );
    return m;
}();

auto get_condition( const std::string &type, const dialogue &d, const std::string &arg ) -> bool {
    auto it = condition_map.find( type );
    if( it != condition_map.end() ) return it->second->getter( d, arg );
    debugmsg( "unknown condition: %s", type );
    return false;
}
```

### Co-location guarantee

Each entry specifies both getter and setter in one struct literal, so adding a new condition always requires naming both functions. Reviewers see a single `condition_table` initializer — no scanning 50+ branches.

### Dispatching on the same chain for `talker` variants

`condition.cpp` also handles `talker`-scoped variants (NPC conditions, player conditions) with duplicated if-else chains. The same table approach applies: tag entries with a scope field, or use three separate tables (`global_conditions`, `npc_conditions`, `player_conditions`) selected by an enum.

## Verification

- Build green. No behavioral change — existing dialogue tests pass.
- Tracy `condition_dispatch` zone: flat time, not growing with condition count.
- `rg "}\s*else\s+if\s*\(\s*type\s*==" src/condition.cpp` returns 0.
- Adding one new condition: 1 line in the table, instead of 2 × N/2-position insertion.

## Files

| File | Phase |
|------|-------|
| `src/condition.cpp` | Only file touched |
| `src/condition.h` | No changes needed |

## Effort: 1–2 days
