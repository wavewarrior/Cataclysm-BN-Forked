# Particle System for Visual Overlays

## Context

Projectile rendering currently uses a blocking sub-frame redraw loop: each tile transition does 4 full `invalidate → redraw → refresh_display` cycles with nanosleep between them. A shotgun burst with 8 pellets × 15 tiles × 4 sub-frames = 480 full-scene redraws. The creature animation system already solved this — per-entity offsets interpolated via wall-clock, rendered as part of the normal draw pass. The particle system extends that pattern to transient visual effects (projectiles now, weather/trails/impacts later).

## Pre-work: Revert sub-frame commit

`git revert fd55d9d492` — removes the sub-frame loop and offset vectors. Vehicle smooth-rendering commit `50b36d644b` is independent and survives.

## Phase 1: Particle System Core

**New files**: `src/particle_system.h`, `src/particle_system.cpp`

```cpp
struct particle {
    std::string sprite;
    int rotation = 0;

    // Current render state (written by update)
    tripoint_bub_ms tile;
    float off_x = 0.f;   // sub-tile offset, tile units
    float off_y = 0.f;

    // Trajectory: ordered tile waypoints
    std::vector<tripoint_bub_ms> path;
    double start_wall = 0.0;   // wall-clock seconds at emission
    float duration = 0.f;      // total flight time, seconds

    bool alive = true;
};

class particle_system {
public:
    void emit( particle p );
    void update( double wall_now );
    auto idle() const -> bool;
    void clear();
    auto active() const -> const std::vector<particle>&;
private:
    std::vector<particle> particles_;
};
```

**`update(wall_now)`**: For each particle, compute `progress = clamp((wall_now - start_wall) / duration, 0, 1)`. Map to trajectory: `tile_progress = progress * (path.size() - 1)`, `idx = floor(tile_progress)`, `frac = tile_progress - idx`. Set `tile = path[idx+1]`, `off_x = -(dx) * (1 - frac)`, `off_y = -(dy) * (1 - frac)`. Mark expired when `progress >= 1`. Erase dead particles via `std::erase_if`.

**No SDL/render types** — pure data + wall-clock math. Renderer reads state.

## Phase 2: Render Integration

**`src/cata_tiles.h`**: Add `particle_system particles_` member alongside existing `bul_pos`/`bul_id` vectors. Public accessor `auto particles() -> particle_system&`.

**`src/cata_tiles.cpp:1410-1419`** (animation overlay block):
1. Update `in_animation` to include `!particles_.idle()`.
2. After `draw_bullet_frame()` (line 1419), add particle draw pass:
```cpp
if( !particles_.idle() ) {
    particles_.update( anim_wall_now_ );
    for( const auto& p : particles_.active() ) {
        if( !tile_iso ) {
            active_anim_xform_ = sprite_xform{
                .off_x = p.off_x * static_cast<float>( tile_width ),
                .off_y = p.off_y * static_cast<float>( tile_height ) };
        }
        const tile_search_params tile{ p.sprite, C_BULLET, empty_string, 0, p.rotation };
        draw_from_id_string( tile, p.tile, std::nullopt, std::nullopt,
                             lit_level::LIT, false, 0, false );
        active_anim_xform_ = {};
    }
}
```

Uses `anim_wall_now_` (already maintained by `cata_tiles`, set each frame). Same `active_anim_xform_` pattern proven by creature animation and the vehicle offset system.

## Phase 3: Migrate `game::draw_bullet`

**`src/animation.cpp`**: Replace the (reverted) original `bullet_animation().progress()` call with particle emission + render loop.

```cpp
void game::draw_bullet( const tripoint_bub_ms &t, const int i,
                        const std::vector<tripoint_bub_ms> &trajectory, ... )
{
    if( !is_point_visible( t ) ) { return; }

    const auto sprite = get_bullet_sprite( bullet, custom_sprite );
    const auto rotation = get_bullet_rotation( ... );
    const tripoint_bub_ms &prev = ( i > 0 ) ? trajectory[i - 1] : t;

    const auto delay_ms = get_option<int>( "ANIMATION_DELAY" );
    tilecontext->particles().emit( particle{
        .sprite = sprite,
        .rotation = rotation,
        .path = { prev, t },
        .start_wall = SDL_GetTicks() / 1000.0,
        .duration = delay_ms / 1000.f
    } );

    static_popup popup;
    popup.wait_message( "%s", _( "Hang on a bit…" ) ).on_top( true );

    // Render loop: particle interpolates naturally via wall-clock
    long int remain = static_cast<long int>( delay_ms ) * 1'000'000L;
    while( remain > 0 ) {
        invalidate_main_ui_adaptor();
        ui_manager::redraw_invalidated();
        refresh_display();
        long int do_sleep = std::min( remain, 16'000'000L ); // ~60fps cap
        timespec ts{ 0, do_sleep };
        nanosleep( &ts, nullptr );
        inp_mngr.pump_events();
        remain -= do_sleep;
    }
}
```

Key differences from the sub-frame approach:
- **1 redraw per frame** — particle renders as a single overlay sprite during the normal draw pass
- **Frame-rate-independent** — wall-clock interpolation produces smooth motion at any FPS
- **No manual offset math at call site** — particle_system::update() owns the interpolation

## Phase 4: Migrate `draw_bullet_trajectories` lockstep path

Emit one particle per trajectory (full path), all with the same start time. Enter one render loop for `longest_trajectory_size * tile_duration`. All particles fly simultaneously. Shorter trajectories expire naturally; longer ones keep going. Replaces the per-step sequential loop.

The line-rendering path (`draw_as_line`) stays unchanged — it renders the full path at once via `init_draw_bullets`, no per-tile pacing.

## Phase 5: Clean up + verify

- Revert already removed dead offset code
- Old `init_draw_bullet` / `draw_bullet_frame` / `void_bullet` stay for the line-rendering path
- `cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles`
- `cata_test-tiles "[ranged]"` — exercises projectile_attack trajectory + hit detection
- `cata_test-tiles "[vehicle]~[.]"` — confirm no regression (pre-existing failures only)
- Format via `cmake --build --target format`
- Atomic commit: `feat(render): particle system for projectile visual overlay`

## Files touched

| File | Change |
|---|---|
| New: `src/particle_system.h` | particle struct + particle_system class |
| New: `src/particle_system.cpp` | emit, update, idle, clear, active |
| `src/cata_tiles.h` | Add `particle_system particles_` member |
| `src/cata_tiles.cpp` | Particle update + draw in animation overlay block |
| `src/animation.cpp` | Rewrite draw_bullet + draw_bullet_trajectories to emit particles |

## Future extensions (not this plan)

- **Decouple single-shot from per-tile blocking**: record full trajectory in ballistics.cpp, replay post-hoc (like multishot already does)
- **Trail particles**: fade behind projectile during flight
- **Weather particles**: rain/snow as lightweight overlay
- **Impact/muzzle flash**: emit on hit/fire events
- **Passenger sprite offset**: propagate vehicle render_offset into draw_critter_at for mounted creatures
