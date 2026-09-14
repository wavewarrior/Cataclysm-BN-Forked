# Prewarm Research Report

## 1. main.cpp flow
- `src/main.cpp:628`: `g->load_static_data()` — init input/panel/autoupick/safemode/distraction
- `src/game.cpp:435`: `void game::load_static_data()` — no world data
- `src/game.cpp:489`: `void game_ui::init_ui()` — RmlUi init
- After both → main menu loop → `opening_screen()`

## 2. init.cpp — the heavy path

### load_world_modfiles (THE TARGET)
- `src/init.cpp:1354`: `void init::load_world_modfiles(loading_ui& ui, const world* world, const std::string& artifacts_file)`
- Calls `clear_loaded_data()` → `unload_data()`, normalizes mods, calls `load_and_finalize_packs()` per mod
- `load_and_finalize_packs` (line 1175): creates Lua state, runs preload, loads JSON via `load_data_from_path()`, calls `finalize_loaded_data()`

### game::setup
- `src/game.cpp:572`: `void game::setup(bool load_world_modfiles = true)`
- Clears overmapbuffers; if flag true → `init::load_world_modfiles()`

### finalize_loaded_data entries (43 callbacks)
- `src/init.cpp:1009`: `void DynamicDataLoader::finalize_loaded_data(loading_ui& ui)`
- Entries table: lines 1024-1069. Run sequentially lines 1074-1087.
- Key entries: Flags, Body parts, Bionics, Items, Terrain, Furniture, Monster types, Crafting recipes, Mutations, Tileset...

### unload_data (reset all singletons)
- `src/init.cpp:900-1007`: `void DynamicDataLoader::unload_data()`
- Resets every game data singleton + Lua state

### clear_loaded_data
- `src/init.cpp:1318`: `static void clear_loaded_data()` → delegates to `unload_data()`

### last_world_name
- `src/worldfactory.h:58`: `std::string last_world_name;`
- `src/worldfactory.cpp:722-738`: `load_last_world_info()` reads from `PATH_INFO::lastworld()` JSON
- `src/worldfactory.cpp:400`: called from `worldfactory::generate()`
- `src/main_menu.cpp:1069`: set when loading character

## 3. main_menu.cpp

### opening_screen
- `src/main_menu.cpp:377`: `bool main_menu::opening_screen()` — RmlUi title screen
- CONFIRM handler (line 830): dispatches LOADCHAR→`load_character_tab()`, WORLD→`world_tab()`, NEWCHAR→`new_character_tab()`

### load_character_tab
- `src/main_menu.cpp:949`: `bool main_menu::load_character_tab(const std::string &worldname)`
- Shows char selection → `g->setup(worldname)` → `g->load(save_name)`
- `g->setup()` is where `load_world_modfiles()` is called — pre-warm target

## 4. Thread pool

### get_thread_pool
- `src/thread_pool.h:115`: `cata_thread_pool &get_thread_pool()` — lazy-init process-lifetime pool

### submit_returning
- `src/thread_pool.h:85`: `template<typename F, typename... Args> auto submit_returning(F&& f, Args&&...) -> std::future<...>`
- Uses `std::packaged_task` wrapped in `shared_ptr`
- Single-core fallback: runs synchronously if only 1 worker

### cata_thread_pool class
- `src/thread_pool.h:53`: `class cata_thread_pool`
- `num_workers()`, `queue_size()`, `submit()`, `submit_returning()`

## 5. is_pool_worker_thread
- `src/thread_pool.cpp:13`: `bool is_pool_worker_thread()` returns `thread_local bool tl_is_worker_thread`
- Used to detect if current thread is a pool worker (for UI thread guards)

## Key Insight for Pre-warm
After `load_static_data()` but before `opening_screen()`, we know `last_world_name` from `load_last_world_info()`. Submit background task via `get_thread_pool().submit_returning()` to call `load_world_modfiles()` for that world. When user selects same world in `load_character_tab()`, `g->setup()` detects pre-loaded data and skips reload.
