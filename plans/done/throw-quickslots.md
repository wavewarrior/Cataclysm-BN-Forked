# Throw Quick-Slots

## Design

6 quick-slots on `avatar` storing `itype_id` (item type, not instance — items
are consumed on throw). Active slot selects which type to throw. Inventory
count displayed per slot.

### Data (avatar.h)
- `std::array<itype_id, 6> throw_slots_`
- `int active_throw_slot_ = -1`
- Helpers: `mark_for_throwing`, `unmark_for_throwing`, `get_active_throwable`,
  `count_throwable`, `cycle_throw_slot`

### Serialization (avatar.cpp)
- Serialize in `avatar::serialize()` / `avatar::deserialize()` as a JSON array
  of type ID strings + active index.

### Mark/Unmark (examine_item_menu.cpp)
- Add "Mark for throwing" / "Unmark from throwing" to item context menu.
- Mark = find first empty slot, set to item's typeId. Unmark = clear matching slot.

### HUD (panels.cpp)
- Add throw-slot segment to `hud_topbar()` when any slot is filled.
- Format: `THROW [1]Rock×5 [2]Grenade×2 [3]--- ...` with active slot highlighted.

### Quick-Select (handle_action.cpp or game.cpp)
- New action `THROW_QUICKSLOT_SELECT` bound to a key.
- Option `THROW_RADIAL_HOLD` (bool) — hold vs toggle behavior.
- Opens a uilist with the 6 slots; number keys 1-6 select directly.

### Throw Shortcut (handle_action.cpp)
- ACTION_THROW: if active_throw_slot_ >= 0 and matching item exists, call
  plthrow(g->u, item) directly. Else fall through to inventory browse.

## Files
1. `src/avatar.h` — data + helpers
2. `src/avatar.cpp` — serialize + implement helpers
3. `src/handle_action.cpp` — wire ACTION_THROW + new THROW_QUICKSLOT_SELECT
4. `src/examine_item_menu.cpp` — mark/unmark context action
5. `src/panels.cpp` — HUD display
6. `data/raw/keybindings/keybindings.json` — new keybinds
7. `src/options_registration.cpp` — hold/toggle option
