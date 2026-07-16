# Build Repair Notes — `feature/improvements` broken merge

Diagnosis of why the tiles target fails to compile, and the per-file fix recipe.
Generated during the responsive-sidebar work (Stage 1 shipped separately).

## Root cause

`feature/improvements` carries a **mis-resolved merge of the colored-lighting feature
against a coordinate-type migration**. The collision predates the lighting cutover merge —
`6ef3bf6f3e` ("Merge branch 'main' into feature/improvements") is already internally
inconsistent. Key facts established from git:

- Merge-base `330f076b` (upstream main): colored lighting present, raw `tripoint`, internally
  consistent (compiles).
- `6ef3bf6f3e`: coordinate migration `tripoint → tripoint_bub_ms` applied, but it **dropped the
  colored-lighting overloads** from `map.h` and **duplicated/garbled blocks** in `lightmap.cpp`
  (kept both the colored `{sx,sy}` side and the monochrome `sm_ms` side of conflicts).
- `d31ddd2039` (lighting cutover) piled more on top.

Net: `map.h` declares only **monochrome** lighting (`add_light_source(tripoint_bub_ms,float)`),
while sources still call the **colored** 3-arg form, plus duplicated conflict blocks reference
out-of-scope `sx/sy`.

## Already fixed (committed: c0acc17665 + working tree)

- **Stage 1 sidebar** (`game.cpp`, `panels.h/cpp`) — committed.
- **Drift** (`cata_tiles.cpp`, `sdltiles.cpp`, `explosion.cpp`, `weather.cpp`) — committed:
  coord accessors `.x()/.y()/.z()`, `u.pos()→bub_pos()`, `getglobal()→bub_to_abs()`, dead
  `display_buffer` helpers removed.
- **world.cpp** — restored main's parallel-DB version (lighting branch had no unique content).
  Compiles.
- **map.cpp** — restored main's version (lighting's only delta was an incidental `#ifdef TILES`
  guard removal, dropped intentionally). Compiles.

## Remaining — colored-lighting reconciliation

The fix everywhere: **re-add the colored overloads in coord-migrated (`tripoint_bub_ms`) form**,
and resolve the duplicated conflict blocks by keeping the `sm_ms` (coord-correct) side and
**grafting the color argument** from the deleted orphan.

### map.h (add 2 decls)
Beside `add_light_source(const tripoint_bub_ms&, float)` and the existing
`apply_light_source`/`apply_light_arc`, add:
```cpp
void add_light_source( const tripoint_bub_ms &p, float luminance, const light_color_rgb &color );
void apply_light_source( const tripoint_bub_ms &p, float luminance, const light_color_rgb &color );
```
(`apply_light_arc(tripoint,…,color)` colored overload already survived.)

### lightmap.cpp
1. **Delete duplicated orphan blocks** (lines ~758–801 in the current restored file): the
   `cur_submap->get_lum({ sx, sy })` items block and the `get_ter({sx,sy})`/`get_furn({sx,sy})`
   terrain/furniture blocks. These cause `sx/sy undeclared` + `redefinition of terrain/furniture`.
2. **Keep the `sm_ms` siblings** (the duplicates immediately below) but **graft color** onto the
   terrain/furniture emit calls:
   ```cpp
   const ter_id terrain = cur_submap->get_ter( sm_ms );
   if( terrain->light_emitted > 0 ) {
       add_light_source( p, terrain->light_emitted, terrain->light_color );   // was 2-arg
   }
   const furn_id furniture = cur_submap->get_furn( sm_ms );
   if( furniture->light_emitted > 0 ) {
       add_light_source( p, furniture->light_emitted, furniture->light_color ); // was 2-arg
   }
   ```
   (`arc_light_def`/`dir_light_def` already have a `color` field, so the `sm_ms` items block's
   4-field aggregate push is fine — color defaults; items have no color.)
3. **⚠ MAJOR FINDING — colored lighting is a cohesive feature main STRIPPED, not a few overloads.**
   On `330f076b` (base), colored lighting spans:
   - `add_light_source(p,float,color)` — buffers hue into `light_source_buffer[idx].color`.
   - `apply_light_source(p,float,color)` — immediate; accumulates into `light_color_cache`.
   - `apply_light_arc(p,angle,float,angle,color)` — calls colored `apply_light_source` +
     colored `apply_light_ray`.
   - `apply_light_ray(lit, s, e, luminance, light_color_rgb *color_cache)` — **extra color_cache
     param** that main dropped.
   - a `g_current_source_color` global and a per-cache `light_color_cache`.
   - `buffered_light_source` is a **struct `{luminance, color}`** (base/lighting), but main's
     lightmap.cpp treats `light_source_buffer[idx]` as a bare `float` (breaks at lightmap.cpp
     ~920/921/934/1898/1899: `apply_light_source(p, lsb)` should be
     `apply_light_source(p, lsb.luminance, lsb.color)`, and `max(.., lsb)` → `lsb.luminance`).

   **Consequence:** patching main's stripped lightmap.cpp = re-implementing the whole colored
   propagation path by hand (apply_light_ray color_cache, g_current_source_color, light_color_cache
   plumbing). High silent-bug risk.

   **Recommended approach instead:** take base `330f076b`'s lightmap.cpp (complete + internally
   consistent colored feature) and **coord-migrate it** to `tripoint_bub_ms`
   (`p.z→p.z()`, `idx(p.x,p.y)→idx(p.x(),p.y())`, signature types), reconciling against HEAD's
   map.h. Large but systematic/mechanical, vs error-prone feature re-implementation. Then re-apply
   main's lightmap perf delta (`git diff 330f076b 6ef3bf6f3e -- src/lightmap.cpp`) if desired.
   Equivalently: a clean git re-merge of the colored-lighting branch vs the coord-migration, which
   git's 3-way machinery handles far more reliably than hand-porting.

   Progress so far (working tree): map.h colored decls added; lightmap.cpp orphan-conflict blocks
   resolved + terrain/furniture color grafted. Remaining = the cohesive-feature reconstruction above.

### snapshot.cpp (lighting-only feature file, 405-line addition)
Mechanical API migration: `avatar/Character/monster.pos()→bub_pos()`, `map::getlocal()→abs_to_bub()`,
vehicle `global_pos3()/global_part_pos3()→bub_part_location()` (creature.h/vehicle.h:893), `i_at`
signature, `inbounds(typed)`.

### pixel_minimap.cpp (both-sided: lighting Δ 60/199, main Δ 121/114)
Merge-mangled (orphaned braces, `continue` not in loop) + removed `main_tex` /
`set_displaybuffer_rendertarget` / `ms_to_sm_remain`. Best resolved by starting from main's
version and re-applying lighting's delta-from-base (`git diff 330f076b d47b144d69 -- src/pixel_minimap.cpp`),
migrated. The `display_buffer`/`main_tex` paths are dead post-cutover — drop them.

### sdl_font.cpp (both-sided: lighting Δ 106/7 GPU-glyph, main Δ 73/30)
Keep lighting's `create_gpu_glyph`/`gpu_texture_unique_ptr` additions; adopt main's `ascii_surf`
removal. Reconcile from main's version + lighting's delta-from-base, migrated.

## Verification
Per-TU compile with the real flags:
```
ninja -C build src/CMakeFiles/cataclysm-bn-tiles-common.dir/<file>.o
```
Then full `ninja -C build`. Lighting *behavior* (colored emission, GI) is not compile-checkable —
run the game and eyeball against the intended Stoneshard-style look.
