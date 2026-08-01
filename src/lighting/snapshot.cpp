#include "lighting/snapshot.h"

#include "avatar.h"
#include "character.h"
#include "creature.h"
#include "debug.h"
#include "effect.h"
#include "field.h"
#include "game.h"
#include "game_constants.h"
#include "item.h"
#include "lighting/dev_test_lights.h"
#include "map.h"
#include "map_iterator.h"
#include "monster.h"
#include "npc.h"
#include "submap.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_position.h"
#include "worldfactory.h"

#include <algorithm>
#include <cmath>

// Runtime-tunable menu-emitter knobs, owned by sdltiles.cpp. F-key handlers
// there mutate these; the snapshot reads them when pushing the decorative
// emitter on the main menu. Forward-declared rather than pulling a sdltiles
// header into the lighting module (no such header exists; this file would
// otherwise have no reason to depend on the SDL frontend).
namespace menu_emitter_tuning {
extern float radius_input;
extern float pos_x;
extern float pos_y;
} // namespace menu_emitter_tuning

// Dev tool (F4 panel): a movable omni light pinned to the mouse cursor, used to
// sweep light across terrain while inspecting normals / shadows / GI. Owned by
// sdltiles.cpp, which computes the world-tile position from the cursor + camera
// each frame; the snapshot just reads these and appends the emitter.
namespace cursor_light_emitter {
extern bool enabled;
extern float radius;     // emission radius in tiles (used directly, not sqrt'd)
extern float intensity;  // emission brightness
extern float color[3];   // RGB tint (default white); saturate a channel for colored GI
extern float wx, wy, wz; // world-tile position under the cursor
} // namespace cursor_light_emitter

#define dbg(x) DebugLogFL((x), DC::SDL)

// effect_onfire is a static in lightmap.cpp; define a local copy.
static const efftype_id snapshot_effect_onfire("onfire");

static constexpr float M_PIf = 3.14159265358979323846f;
static constexpr float FALLOFF_DEFAULT = 2.0f;

namespace lighting {

// px/py are FLOAT world-tile positions, not tile indices: Step 4 of the
// grid-decoupled lighting plan lets a carried light follow its sprite's sub-tile
// slide instead of snapping to the tile centre. Callers that genuinely sit at a
// tile centre pass `x + 0.5f` to keep the previous behaviour exactly.
static gpu_emitter make_omni(float px, float py, int lz, float radius, float r, float g,
                             float b) {
    gpu_emitter e{};
    e.pos_x = px;
    e.pos_y = py;
    e.pos_z = static_cast<float>(lz);
    // Convert game luminance units to GPU tile radius via sqrt.
    // light_emitted/active_light use luminance (not tile radius):
    //   lum=30 (fire field)   → 5.5 tiles
    //   lum=120 (indoor lamp) → 11 tiles
    //   lum=400 (bright)      → 20 tiles
    e.radius = 3.0f * std::sqrt(std::max(0.f, radius));
    e.r = r;
    e.g = g;
    e.b = b;
    e.falloff = FALLOFF_DEFAULT;
    e.cone_dir_x = 0.0f;
    e.cone_dir_y = 0.0f;
    e.cone_half_angle = M_PIf;
    e.shape = static_cast<uint32_t>(emitter_shape::OMNI);
    // Flicker phase is keyed to the emitter's LOGICAL tile. For a static emitter
    // px is exactly tile+0.5, so flooring recovers it exactly. A SLIDING emitter
    // (a carried torch, Step 4a) must not use this: px = tile+0.5+slide crosses the
    // x.5 boundary mid-step, so floor() would flip to the neighbour and the flame
    // would visibly jump phase twice per walked tile. Those callers overwrite the
    // seed with with_tile_seed() using the tile the creature logically occupies.
    const int lx = static_cast<int>(std::floor(px));
    const int ly = static_cast<int>(std::floor(py));
    e.flicker_seed = static_cast<uint32_t>(lx * 31337 + ly * 7919);
    return e;
}

static gpu_emitter make_cone(
    float px, float py, int lz, float radius, float r, float g, float b, float dir_x, float dir_y,
    float half_angle_rad) {
    gpu_emitter e = make_omni(px, py, lz, radius, r, g, b);
    e.cone_dir_x = dir_x;
    e.cone_dir_y = dir_y;
    e.cone_half_angle = half_angle_rad;
    e.shape = static_cast<uint32_t>(emitter_shape::CONE);
    return e;
}

/// Re-key an emitter's flicker phase to an explicit logical tile. Used by emitters
/// whose position carries a sub-tile slide offset.
static gpu_emitter with_tile_seed(gpu_emitter e, int lx, int ly) {
    e.flicker_seed = static_cast<uint32_t>(lx * 31337 + ly * 7919);
    return e;
}

// Step 4b: a light INSIDE an opaque tile (wall lamp, lit sign, glowing terrain)
// belongs on the wall FACE, not at the wall centre — at the centre its own tile
// occludes it and trace_shadow's self-shadow escape fires, so the corridor in front
// stays dark. Average the directions of the four orthogonal neighbours that transmit
// light, normalise, and push 0.45 tile that way. No transparent neighbour (fully
// enclosed) → leave at the centre. Uses the same `> LIGHT_TRANSPARENCY_SOLID` test
// the rest of the engine uses for "is this a wall".
struct emitter_pos { float x, y; };
static emitter_pos face_offset(const level_cache& mc, int lx, int ly) {
    const float cx = static_cast<float>(lx) + 0.5f;
    const float cy = static_cast<float>(ly) + 0.5f;
    if (!mc.inbounds(point_bub_ms(lx, ly))) { return {cx, cy}; }
    const int self = mc.idx(lx, ly);
    if (self < 0 || self >= static_cast<int>(mc.transparency_cache.size())
        || mc.transparency_cache[self] > LIGHT_TRANSPARENCY_SOLID) {
        return {cx, cy}; // not inside an occluder — nothing to push off
    }
    float dx = 0.0f;
    float dy = 0.0f;
    constexpr point dirs[4] = {point(1, 0), point(-1, 0), point(0, 1), point(0, -1)};
    for (const point& d : dirs) {
        const int nx = lx + d.x;
        const int ny = ly + d.y;
        if (!mc.inbounds(point_bub_ms(nx, ny))) { continue; }
        const int ni = mc.idx(nx, ny);
        if (ni < 0 || ni >= static_cast<int>(mc.transparency_cache.size())) { continue; }
        if (mc.transparency_cache[ni] > LIGHT_TRANSPARENCY_SOLID) {
            dx += static_cast<float>(d.x);
            dy += static_cast<float>(d.y);
        }
    }
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) { return {cx, cy}; } // fully enclosed
    constexpr float PUSH = 0.45f;
    return {cx + dx / len * PUSH, cy + dy / len * PUSH};
}

// Non-const map& required for i_at() and get_vehicles().
static void collect_zlev(map& m, int zlev, std::vector<gpu_emitter>& out) {
    const int mapsize = m.getmapsize();
    // Step 4b: terrain/furniture/field/item emitters sitting inside their own opaque
    // tile are pushed onto the wall face. Creatures are excluded — they never stand
    // inside a wall, and their positions already carry the sprite slide offset.
    const level_cache& mc = m.access_cache(zlev);

    for (int smx = 0; smx < mapsize; ++smx) {
        for (int smy = 0; smy < mapsize; ++smy) {
            const submap* cur = m.get_submap_at_grid(tripoint_bub_sm{smx, smy, zlev});
            if (!cur) { continue; }
            for (int sx = 0; sx < SEEX; ++sx) {
                for (int sy = 0; sy < SEEY; ++sy) {
                    const int lx = smx * SEEX + sx;
                    const int ly = smy * SEEY + sy;
                    const tripoint_bub_ms p{lx, ly, zlev};
                    const emitter_pos ep = face_offset(mc, lx, ly);

                    // ter_id / furn_id have operator-> returning const ter_t*/furn_t*
                    const ter_id t_id = cur->get_ter({sx, sy});
                    if (t_id->light_emitted > 0) {
                        out.push_back(make_omni(
                            ep.x, ep.y, zlev, static_cast<float>(t_id->light_emitted),
                            t_id->light_color.r, t_id->light_color.g, t_id->light_color.b));
                    }

                    const furn_id f_id = cur->get_furn({sx, sy});
                    if (f_id->light_emitted > 0) {
                        out.push_back(make_omni(
                            ep.x, ep.y, zlev, static_cast<float>(f_id->light_emitted),
                            f_id->light_color.r, f_id->light_color.g, f_id->light_color.b));
                    }

                    // Fields — plain range-based for; std::ranges::for_each not
                    // usable here due to MSVC's stricter range-concept checks.
                    for (const auto& [ftype, fentry] : cur->get_field({sx, sy})) {
                        (void)ftype;
                        const int fe_lum = fentry.light_emitted();
                        if (fe_lum > 0) {
                            const light_color_rgb fc = fentry.light_color();
                            out.push_back(make_omni(
                                ep.x, ep.y, zlev, static_cast<float>(fe_lum), fc.r, fc.g, fc.b));
                        }
                    }

                    if (cur->get_lum({sx, sy})) {
                        for (const item* const itm : m.i_at(p)) {
                            float ilum = 0.0f;
                            units::angle iwidth = 0_degrees;
                            units::angle idir = 0_degrees;
                            if (itm->getlight(ilum, iwidth, idir)) {
                                if (iwidth > 0_degrees) {
                                    const float dir_rad = units::to_radians(idir);
                                    out.push_back(make_cone(
                                        ep.x, ep.y, zlev, ilum, 0, 0, 0, std::cos(dir_rad),
                                        std::sin(dir_rad), units::to_radians(iwidth) * 0.5f));
                                } else {
                                    out.push_back(make_omni(ep.x, ep.y, zlev, ilum, 0, 0, 0));
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Vehicles — non-const for v->lights()
    for (wrapped_vehicle& wv : m.get_vehicles()) {
        vehicle* v = wv.v;
        if (!v) { continue; }
        if (v->bub_ms_location().z() != zlev) { continue; }

        for (const vehicle_part* const pt : v->lights(true)) {
            const vpart_info& vp = pt->info();
            const tripoint part_pos = v->bub_part_location(*pt).raw();
            const int lx = part_pos.x;
            const int ly = part_pos.y;

            const float r = vp.light_color.r;
            const float g = vp.light_color.g;
            const float b = vp.light_color.b;

            if (vp.has_flag(VPFLAG_CONE_LIGHT) || vp.has_flag(VPFLAG_WIDE_CONE_LIGHT)) {
                const float half_rad =
                    vp.has_flag(VPFLAG_WIDE_CONE_LIGHT)
                        ? units::to_radians(45_degrees)
                        : units::to_radians(22.5_degrees);
                // v->face.dir() returns units::angle; convert via to_radians.
                const float face_rad = units::to_radians(v->face.dir());
                out.push_back(make_cone(
                    static_cast<float>(lx) + 0.5f, static_cast<float>(ly) + 0.5f, zlev,
                    static_cast<float>(vp.bonus), r, g, b, std::cos(face_rad),
                    std::sin(face_rad), half_rad));
            } else if (vp.has_flag(VPFLAG_CIRCLE_LIGHT) || vp.has_flag(VPFLAG_DOME_LIGHT)
                       || vp.has_flag(VPFLAG_AISLE_LIGHT) || vp.has_flag(VPFLAG_ATOMIC_LIGHT)) {
                out.push_back(make_omni(
                    static_cast<float>(lx) + 0.5f, static_cast<float>(ly) + 0.5f, zlev,
                    static_cast<float>(vp.bonus), r, g, b));
            }
        }
    }
}

std::vector<gpu_emitter> build_emitter_snapshot(event_queue& eq, float frame_ms) {
    std::vector<gpu_emitter> out;
    out.reserve(1024);

    // Lambda: "no active game" path. g is created in main.cpp BEFORE the
    // main menu is shown, and inbounds((0,0,0)) returns true at that point
    // (my_MAPSIZE > 0), so player-pos checks alone miss the main menu.
    // The reliable discriminator is world_generator->active_world: null
    // when no world is loaded (main menu, between worlds), set after a
    // world is picked. Decorative warm-amber emitter sized to cover a
    // 1920×1080 menu (radius ~45 tiles).
    const auto push_menu_decoration = [&]() {
        // Position is float-valued — make_omni takes ints, but we want
        // sub-tile resolution for the cycle-presets. Build the emitter
        // inline rather than rounding through make_omni.
        gpu_emitter e{};
        e.pos_x = menu_emitter_tuning::pos_x;
        e.pos_y = menu_emitter_tuning::pos_y;
        e.pos_z = 0.f;
        e.radius = 3.0f * std::sqrt(std::max(0.f, menu_emitter_tuning::radius_input));
        e.r = 1.0f;
        e.g = 0.55f;
        e.b = 0.15f;
        e.falloff = 2.0f;
        e.cone_half_angle = static_cast<float>(M_PI);
        e.shape = static_cast<uint32_t>(emitter_shape::OMNI);
        out.push_back(e);
    };

    if (!g || !world_generator || !world_generator->active_world) {
        push_menu_decoration();
        return out;
    }

    map& m = get_map(); // non-const required for i_at, get_vehicles

    // Game is quitting (uquit != QUIT_NO). Map data may be partially freed
    // during teardown even while g is non-null. No decoration here — quit
    // is a transient state, drawing a stray emitter would flicker.
    if (g->uquit != QUIT_NO) { return out; }
    // Defense-in-depth: player outside loaded map (shouldn't happen with
    // active_world non-null, but cheap to guard).
    if (!m.inbounds(g->u.bub_pos())) { return out; }

    const int zlev = g->u.bub_pos().z();

    // Step 4a: a carried light follows the sliding SPRITE, not the logical tile.
    // anim_state.slide_offset_* is in tile units and is the exact value the renderer
    // used this frame (cata_tiles::draw runs before the lighting build, so it is
    // already current); on a no-input frame neither advances, so light and sprite
    // stay locked by construction. Without this the pool of light snaps tile-to-tile
    // while the body glides.
    const auto slid = [](const Creature& c, const tripoint_bub_ms& pos) -> emitter_pos {
        const animation_state& a = c.anim_state;
        return {static_cast<float>(pos.x()) + 0.5f + a.slide_offset_x,
                static_cast<float>(pos.y()) + 0.5f + a.slide_offset_y};
    };

    auto collect_character = [&](const Character& c) {
        const float lum = c.active_light();
        if (lum <= 0.0f) { return; }
        const tripoint_bub_ms pos = c.bub_pos();
        if (!m.inbounds(pos)) { return; }
        const emitter_pos ep = slid(c, pos);
        out.push_back(
            with_tile_seed(make_omni(ep.x, ep.y, pos.z(), lum, 0, 0, 0), pos.x(), pos.y()));
        if (c.has_effect(snapshot_effect_onfire)) {
            out.push_back(with_tile_seed(
                make_omni(ep.x, ep.y, pos.z(), 6.0f, 1.0f, 0.5f, 0.0f), pos.x(), pos.y()));
        }
    };

    // Player's active light is added separately below with a warm tint. Skip
    // collect_character() here — using it as well produced a duplicate uncolored-white
    // emitter stacked on the warm one at the same tile, doubling the player's
    // brightness AND burning two of the 256 GPU emitter slots on one light source.
    // The standalone block below handles both active_light and the on-fire glow.
    for (const npc& guy : g->all_npcs()) { collect_character(guy); }

    for (const monster& critter : g->all_monsters()) {
        if (critter.is_hallucination()) { continue; }
        const tripoint_bub_ms mp = critter.bub_pos();
        if (!m.inbounds(mp)) { continue; }
        const emitter_pos ep = slid(critter, mp);
        if (critter.has_effect(snapshot_effect_onfire)) {
            out.push_back(with_tile_seed(
                make_omni(ep.x, ep.y, mp.z(), 6.0f, 1.0f, 0.5f, 0.0f), mp.x(), mp.y()));
        }
        if (critter.type->luminance > 0) {
            out.push_back(with_tile_seed(
                make_omni(ep.x, ep.y, mp.z(), critter.type->luminance, 0, 0, 0), mp.x(), mp.y()));
        }
    }

    collect_zlev(m, zlev, out);

    // Player personal light (torch, flashlight, worn items, mutations) +
    // on-fire glow. Folded in here after dropping collect_character() for
    // the player so we don't get two stacked emitters at the same tile.
    {
        const tripoint_bub_ms pp = g->u.bub_pos();
        const Character& pc = get_player_character();
        const emitter_pos ep = slid(pc, pp);
        const float lum = pc.active_light();
        if (lum > 0.5f) {
            out.push_back(with_tile_seed(
                make_omni(ep.x, ep.y, pp.z(), lum, 1.0f, 0.9f, 0.7f), pp.x(), pp.y()));
        }
        if (pc.has_effect(snapshot_effect_onfire)) {
            out.push_back(with_tile_seed(
                make_omni(ep.x, ep.y, pp.z(), 6.0f, 1.0f, 0.5f, 0.0f), pp.x(), pp.y()));
        }
    }

#ifdef DEBUG_SYNTHETIC_EMITTER
    // Replace all emitters with one huge test emitter centred on the player.
    // Build with -DDEBUG_SYNTHETIC_EMITTER to isolate whether the emitter
    // pipeline works at all when given known-good input.
    {
        out.clear();
        const tripoint pp = g->u.bub_pos().raw();
        gpu_emitter t{};
        t.pos_x = static_cast<float>(pp.x) + 0.5f;
        t.pos_y = static_cast<float>(pp.y) + 0.5f;
        t.pos_z = static_cast<float>(pp.z);
        t.radius = 30.0f;
        t.r = 1.0f;
        t.g = 1.0f;
        t.b = 1.0f;
        t.falloff = 2.0f;
        t.cone_half_angle = M_PIf;
        t.shape = static_cast<uint32_t>(emitter_shape::OMNI);
        out.push_back(t);
    }
#endif

    {
        std::vector<flash_event> flashes;
        eq.drain(frame_ms, flashes);
        for (const flash_event& f : flashes) {
            const float frac = 1.0f - (f.elapsed_ms / f.duration_ms);
            const float radius = f.intensity * std::max(0.0f, frac);
            const tripoint_bub_ms local = m.abs_to_bub(f.pos);
            if (!m.inbounds(local)) { continue; }
            out.push_back(make_omni(
                static_cast<float>( local.x() ) + 0.5f, static_cast<float>( local.y() ) + 0.5f,
                local.z(), radius, f.r, f.g, f.b));
        }
    }

    // Fix D — CPU-side cull: drop emitters that cannot contribute to any
    // visible fragment. Visible region is ~25 tiles around the player
    // (camera half-extent for a typical viewport at 32 px/tile). An emitter
    // contributes only if its reach (`radius`) plus the half-view margin
    // is greater than its distance to the player. Wrong-z emitters are
    // already culled in the fragment shader, but doing it here saves
    // upload bandwidth and frees emitter slots.
    if (g) {
        const tripoint pp = g->u.bub_pos().raw();
        constexpr float view_margin = 25.0f;
        out.erase(
            std::remove_if(
                out.begin(), out.end(),
                [&pp](const gpu_emitter& e) {
                    if (static_cast<int>(std::floor(e.pos_z + 0.5f)) != pp.z) { return true; }
                    const float dx = e.pos_x - (static_cast<float>(pp.x) + 0.5f);
                    const float dy = e.pos_y - (static_cast<float>(pp.y) + 0.5f);
                    const float d = std::sqrt(dx * dx + dy * dy);
                    return d > e.radius + view_margin;
                }),
            out.end());

        // Fix C — sort by contribution at the player. `dist - radius` is
        // negative when the emitter reaches the player (more negative =
        // deeper inside its lit region); positive when the emitter only
        // contributes to off-player visible tiles. Ascending sort keeps
        // strongest contributors first, so the GPU's 256-row truncation
        // always keeps the player's own light and nearest sources.
        std::sort(out.begin(), out.end(), [&pp](const gpu_emitter& a, const gpu_emitter& b) {
            const float ax = a.pos_x - (static_cast<float>(pp.x) + 0.5f);
            const float ay = a.pos_y - (static_cast<float>(pp.y) + 0.5f);
            const float bx = b.pos_x - (static_cast<float>(pp.x) + 0.5f);
            const float by = b.pos_y - (static_cast<float>(pp.y) + 0.5f);
            return std::sqrt(ax * ax + ay * ay) - a.radius
                 < std::sqrt(bx * bx + by * by) - b.radius;
        });
    }

    // Dev cursor light — appended AFTER the view-cull so it is never dropped
    // while swept around, and given radius/intensity straight from the F4
    // sliders (radius is already in tiles, so it bypasses make_omni's sqrt).
    if (cursor_light_emitter::enabled) {
        gpu_emitter e{};
        e.pos_x = cursor_light_emitter::wx;
        e.pos_y = cursor_light_emitter::wy;
        e.pos_z = cursor_light_emitter::wz;
        e.radius = std::max(0.0f, cursor_light_emitter::radius);
        const float lum = cursor_light_emitter::intensity;
        e.r = lum * cursor_light_emitter::color[0];
        e.g = lum * cursor_light_emitter::color[1];
        e.b = lum * cursor_light_emitter::color[2];
        e.falloff = FALLOFF_DEFAULT;
        e.cone_dir_x = 0.0f;
        e.cone_dir_y = 0.0f;
        e.cone_half_angle = M_PIf;
        e.shape = static_cast<uint32_t>(emitter_shape::OMNI);
        out.push_back(e);
    }

    // Dev test lights (F4 click-to-place, cleared when the panel closes). Static
    // omni lights so occlusion can be studied against fixed walls; colour-tinted
    // intensity so the GI bounce is visibly coloured.
    for (const dev_test_lights::light& L : dev_test_lights::lights) {
        gpu_emitter e{};
        e.pos_x = L.wx;
        e.pos_y = L.wy;
        e.pos_z = L.wz;
        e.radius = std::max(0.0f, L.radius);
        e.r = L.intensity * L.r;
        e.g = L.intensity * L.g;
        e.b = L.intensity * L.b;
        e.falloff = FALLOFF_DEFAULT;
        e.cone_dir_x = 0.0f;
        e.cone_dir_y = 0.0f;
        e.cone_half_angle = M_PIf;
        e.shape = static_cast<uint32_t>(emitter_shape::OMNI);
        out.push_back(e);
    }

    if (static_cast<int>(out.size()) > MAX_EMITTERS * 3 / 4) {
        dbg(DL::Warn) << "build_emitter_snapshot: " << out.size() << " emitters (budget "
                      << MAX_EMITTERS << ")";
    }
    if (static_cast<int>(out.size()) > MAX_EMITTERS) { out.resize(MAX_EMITTERS); }

    return out;
}

} // namespace lighting
