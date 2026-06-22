# Faction System Modernization — Plan

## Context

`faction.h` (146 lines) and `faction.cpp` (1078 lines) are small files with disproportionately large legacy baggage for their size. The system has been partially reworked (JSON loading exists) but retains several C++98-era patterns.

Current issues:

| Issue | Location | Severity |
|-------|----------|----------|
| Public data members | `faction.h:77-91` (name, likes_u, respects_u, size, power, etc.) | Medium |
| `std::set<std::tuple<int,int,snippet_id>>` for epilogue data | `faction.h:90` | High — tuple hides intent |
| Raw `enum relationship` with hand-maintained `relation_strs` map | `npc_factions` namespace | Medium |
| "TODO: Redefine?" for `MAX_FAC_NAME_SIZE = 40` | `faction.h:19` | Low — ancient open question |
| Bare `std::map` for faction storage | `faction_manager::factions` | Low |

## Approach

Single phase — small, mechanical, reviewable in one pass.

### Replace epilogue tuple with struct

```cpp
// Before:  std::set<std::tuple<int, int, snippet_id>> epilogue_data;
// After:
struct epilogue_entry {
    int field_1;
    int field_2;
    snippet_id id;

    auto operator<=>( const epilogue_entry & ) const = default; // *NOPAD*
};
```

Update all readers of `epilogue_data` (grep `std::get<0>` / `std::get<1>` / `std::get<2>` on faction epilogue tuples → named field access).

### Encapsulate public members

Move from:
```cpp
// faction.h
struct faction {
    std::string name;
    int likes_u;
    int respects_u;
    int size;
    int power;
    // ...
};
```

To:
```cpp
class faction {
    std::string name_;
    int likes_u_ = 0;
    int respects_u_ = 0;
    int size_ = 0;
    int power_ = 0;
    // ...
public:
    auto name() const -> const std::string & { return name_; }
    auto likes_u() const -> int { return likes_u_; }
    auto set_likes_u( int v ) -> void { likes_u_ = v; }
    // ...
};
```

Audit all `faction.foo` accesses in callers and update to `faction.foo()` / `faction.set_foo()`. Callers using designated initializers for `faction` (if any) need to migrate to setter calls.

### Replace raw enum with `enum class`

```cpp
// Before:
namespace npc_factions {
    enum relationship : int {
        relationship_fear = 0,
        relationship_mate = 1,
        relationship_kill = 2,
        relationship_max = 3,
    };
    const std::map<relationship, std::string> relation_strs = { ... };
}

// After:
enum class faction_relationship : int {
    fear = 0,
    mate = 1,
    kill = 2,
};

// Use enum_traits pattern (as used elsewhere in codebase):
template<>
struct enum_traits<faction_relationship> {
    static constexpr auto last() -> faction_relationship { return faction_relationship::kill; };
    static constexpr auto count() -> int { return static_cast<int>( last() ) + 1; };
};
```

### Remove `MAX_FAC_NAME_SIZE`

Replace the `name` storage pattern (char array bounded by `MAX_FAC_NAME_SIZE` or `std::string` with length check) — use `std::string` directly. The JSON loader already truncates at reasonable lengths.

## Verification

- Build green.
- Faction behavior identical — load an existing save, check faction reputation/relation values match.
- `rg "std::get<.*epilogue" src/` returns 0.
- `rg "faction\.(name|likes_u|respects_u|size|power)" src/` returns only accessor-style calls.
- `rg "MAX_FAC_NAME_SIZE" src/` returns 0.
- `rg "enum relationship" src/` returns 0.

## Files

| File | Changes |
|------|---------|
| `src/faction.h` | Encapsulate members, replace epilogue tuple, convert enum |
| `src/faction.cpp` | Update implementation to match new interface |

## Effort: 1–2 days
