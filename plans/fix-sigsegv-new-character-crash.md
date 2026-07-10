# Fix SIGSEGV on New Character Creation

## Status: In Progress — Guard flag added, needs wiring and verification

## Crash

```
SIGSEGV in Rml::DataTypeRegister::GetDefinitionDetail<std::string>
  → loading_ui::init()::$_1 (on_redraw callback)
  → ui_adaptor::redraw_invalidated()
  → load_and_finalize_packs()
  → init::load_world_modfiles()
  → game::setup(true)
  → main_menu::new_character_tab()
```

Repro: Launch game → "New Character" → fresh world → crash during loading screen.

## Root Cause

`loading_doc_open()` calls `RegisterStruct<loading_row_model>()` which internally calls `GetDefinitionDetail<std::string>` to register `Rml::String` members. This crashes because:

1. **Missing guard flag:** Every other `RegisterStruct` in the codebase uses a `g_*_types_registered` guard to prevent double registration. `loading_doc_open()` is the only one missing it. (39+ instances verified via grep.)
2. **Double registration after Rml::Shutdown/Initialise cycle:** When `game::setup()` runs, if RmlUI was previously shut down (e.g. from a prior session or render state reset), the type registry is cleared. A second call to `loading_doc_open()` tries to re-register types into a partially-initialized registry where `std::string` may not yet be resolved.

## Call Flow

```
main_menu::new_character_tab()
  → game::setup(true)
    → loading_ui ui(true)                    # line 575
    → init::load_world_modfiles(ui, ...)     # line 587
      → load_and_finalize_packs(ui, ...)
        → ui.show()                          # ~line 1332
          → loading_ui::init()               # line 213 — sets on_redraw callback
          → ui_manager::redraw()             # line 216 — triggers redraw_invalidated()
            → on_redraw callback fires       # loading_doc_open() called here
              → RegisterStruct<...>()        # line 74 — CRASH
          → refresh_display()                # line 217 — rmlui_layer::init() happens here (too late)
```

## Files Involved

| File | Lines | Role |
|------|-------|------|
| `src/loading_ui.cpp` | 36-39, 55-92, 133-144, 174-191 | `loading_row_model`, `loading_doc_open/close`, `init()` |
| `src/lighting/rmlui_layer.cpp` | 45-47, 265-350, 355-375 | `rmlui_layer::init/shutdown`, `g_ready`/`g_attempted` |
| `src/sdl_render_frame.cpp` | 112, 957 | `begin_frame()` calls `rmlui_layer::init()` |
| `src/ui_manager.cpp` | 260-310 | `redraw()` / `redraw_invalidated()` |
| `src/game.cpp` | 573-647 | `game::setup()` |

## Pattern (from 39+ other files)

```cpp
bool g_something_types_registered = false;

void register_something_rml_types( Rml::DataModelConstructor &c )
{
    if( g_something_types_registered ) {
        return;
    }
    // ... RegisterStruct, RegisterMember, RegisterArray ...
    g_something_types_registered = true;
}
```

## What's Done

- [x] Read crash log, confirmed SIGSEGV at `GetDefinitionDetail<std::string>`
- [x] Traced full call stack from `main_menu` to `loading_doc_open()`
- [x] Confirmed `rmlui_layer::init()` timing (called in `begin_frame()` which runs in `refresh_display()`)
- [x] Verified `Rml::Shutdown()` exists and clears type registry
- [x] Grepped 39+ `g_*_types_registered` patterns — confirmed `loading_ui.cpp` is the outlier
- [x] Added `g_loading_types_registered` flag at line 55

## What Remains

### 1. Wire the guard in `loading_doc_open()` (lines 74-77)

The guard flag exists but is not yet used. The `RegisterStruct`/`RegisterMember`/`RegisterArray` block at lines 74-77 needs the guard check:

```cpp
// BEFORE (crashes):
Rml::StructHandle<loading_row_model> rh = c.RegisterStruct<loading_row_model>();
rh.RegisterMember( "rml", &loading_row_model::rml );
rh.RegisterMember( "state", &loading_row_model::state );
c.RegisterArray<Rml::Vector<loading_row_model>>();

// AFTER (safe):
if( !g_loading_types_registered ) {
    Rml::StructHandle<loading_row_model> rh = c.RegisterStruct<loading_row_model>();
    rh.RegisterMember( "rml", &loading_row_model::rml );
    rh.RegisterMember( "state", &loading_row_model::state );
    c.RegisterArray<Rml::Vector<loading_row_model>>();
    g_loading_types_registered = true;
}
```

### 2. Reset the flag in `loading_doc_close()` or `rmlui_layer::shutdown()`

When RmlUI shuts down, the type registry is cleared. The guard needs to reset so types can be re-registered after the next `Rml::Initialise()`. Two options:

**Option A:** Reset in `loading_doc_close()` (line 133) — simple but only resets when loading doc closes.
**Option B:** Reset in `rmlui_layer::shutdown()` — more robust, covers all RmlUI shutdown scenarios.

Recommendation: Option B, add `g_loading_types_registered = false;` in `rmlui_layer::shutdown()` after `g_ready = false;` at line 367. This requires declaring `g_loading_types_registered` in a header or forwarding it.

**Simpler alternative:** Since `g_loading_types_registered` is file-local to `loading_ui.cpp`, add a reset function and call it from a shutdown callback, or just reset it at the top of `loading_doc_open()` when `!rmlui_layer::ready()`:

```cpp
void loading_doc_open() {
    if ( !rmlui_layer::ready() ) {
        g_loading_types_registered = false;  // reset on RmlUI shutdown
        return;
    }
    // ...
}
```

### 3. Build and test

```sh
cmake --build --preset linux-full --target cataclysm-bn-tiles 2>&1 | rtk
# Then test: launch game → New Character → verify no crash
```

## Key Lines to Edit

| File | Line | Change |
|------|------|--------|
| `src/loading_ui.cpp:74-77` | Wrap RegisterStruct/RegisterMember/RegisterArray in `if (!g_loading_types_registered) { ... g_loading_types_registered = true; }` |
| `src/loading_ui.cpp:63` | Add `g_loading_types_registered = false;` reset when `!rmlui_layer::ready()` |
