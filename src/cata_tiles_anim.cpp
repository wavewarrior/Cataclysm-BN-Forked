#include "cata_tiles.h"
#include "cata_tiles_internal.h"

#include "cata_utility.h"
#include "avatar.h"
#include "catacharset.h"
#include "clzones.h"
#include "cursesdef.h"
#include "field.h"
#include "game.h"
#include "json.h"
#include "line.h"
#include "map.h"
#include "options.h"
#include "sdl_wrappers.h"
#include "sounds.h"
#include "string_utils.h"
#include "translations.h"
#include "weather.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

using namespace cata_tiles_internal;

// Tuning knobs for the sprite-animation system, refreshed once per frame from options
// (avoids re-reading 14 options per creature). File-scope keeps creature.h's
// animation_tuning type out of cata_tiles.h.
static animation_tuning s_anim_tuning;
// When set by the F4 "Animation" tab, the panel owns s_anim_tuning and we stop
// clobbering it with the option values each frame (and force animations on).
static bool s_anim_override = false;

animation_tuning& debug_anim_tuning() { return s_anim_tuning; }

bool& debug_anim_override() { return s_anim_override; }

static int msgtype_to_tilecolor(const game_message_type type, const bool bOldMsg) {
    const int iBold = bOldMsg ? 0 : 8;

    switch (type) {
        case m_good:
            return iBold + catacurses::green;
        case m_bad:
            return iBold + catacurses::red;
        case m_mixed:
        case m_headshot:
            return iBold + catacurses::magenta;
        case m_neutral:
            return iBold + catacurses::white;
        case m_warning:
        case m_critical:
            return iBold + catacurses::yellow;
        case m_info:
        case m_grazing:
            return iBold + catacurses::blue;
        default:
            break;
    }

    return -1;
}

namespace {

struct draw_zone_overlay_options {
    const SDL_Renderer_Ptr& renderer;
    SDL_Rect rect;
    SDL_Color color;
    std::multimap<point, formatted_text>& overlay_strings;
    std::string name = empty_string;
    int alpha = 64;
    bool draw_label = true;
};

void draw_zone_overlay(const draw_zone_overlay_options& opt) {
    SDL_Color color = opt.color;
    color.a = static_cast<Uint8>(opt.alpha);

    constexpr auto flags = sdl_render_state_flags::draw_color | sdl_render_state_flags::blend_mode;
    const auto state = sdl_save_render_state<flags>(opt.renderer.get());

    SetRenderDrawBlendMode(opt.renderer, SDL_BLENDMODE_BLEND);
    SetRenderDrawColor(opt.renderer, color.r, color.g, color.b, color.a);
    {
        const SDL_FRect
            frect{float(opt.rect.x), float(opt.rect.y), float(opt.rect.w), float(opt.rect.h)};
        RenderFillRect(opt.renderer, &frect);
    }

    sdl_restore_render_state(opt.renderer.get(), state);

    if (opt.draw_label && !opt.name.empty()) {
        const point center(opt.rect.x + opt.rect.w / 2, opt.rect.y + opt.rect.h / 2);
        opt.overlay_strings
            .emplace(center, formatted_text(opt.name, catacurses::white, text_alignment::center));
    }
}

} // namespace

void cata_tiles::refresh_anim_frame() {
    creatures_anim_active_ = false; // re-evaluated as creatures/tiles draw this frame
    anim_wall_now_ = static_cast<double>(SDL_GetTicks()) / 1000.0;
    if (s_anim_override) { // F4 live-tuning owns the struct; don't re-read options
        anim_enabled_ = true;
        return;
    }
    anim_enabled_ = get_option<bool>("SPRITE_ANIMATIONS");
    if (!anim_enabled_) { return; }
    animation_tuning& t = s_anim_tuning;
    t.move_bob = get_option<bool>("SPRITE_MOVE_BOB");
    t.breathing = get_option<bool>("SPRITE_BREATHING");
    t.hit_reaction = get_option<bool>("SPRITE_HIT_REACTION");
    t.attack_lunge = get_option<bool>("SPRITE_ATTACK_LUNGE");
    t.bob_amplitude = static_cast<float>(get_option<float>("SPRITE_BOB_AMPLITUDE"));
    t.bob_duration = static_cast<float>(get_option<float>("SPRITE_BOB_DURATION"));
    t.idle_sway = static_cast<float>(get_option<float>("SPRITE_IDLE_SWAY"));
    t.hit_push = static_cast<float>(get_option<float>("SPRITE_HIT_PUSH"));
    t.hit_duration = static_cast<float>(get_option<float>("SPRITE_HIT_DURATION"));
    t.hit_flash_intensity = static_cast<float>(get_option<float>("SPRITE_HIT_FLASH_INTENSITY"));
    t.attack_amplitude = static_cast<float>(get_option<float>("SPRITE_ATTACK_AMPLITUDE"));
    t.attack_duration = static_cast<float>(get_option<float>("SPRITE_ATTACK_DURATION"));
}

sprite_xform cata_tiles::compute_anim_xform(const Creature& c) const {
    sprite_xform x;
    if (!anim_enabled_) { return x; }
    // Per-creature idle phase (from tile position) so a horde doesn't sway in unison.
    const tripoint_bub_ms cp = c.bub_pos();
    const float idle_phase = static_cast<float>((cp.x() * 7 + cp.y() * 13) & 15) * 0.3927f;
    update_animation_state(c.anim_state, s_anim_tuning, anim_wall_now_, idle_phase);
    const animation_state& a = c.anim_state;
    x.off_x = a.hit_offset_x + a.attack_offset_x + a.idle_offset_x
            + a.slide_offset_x * static_cast<float>(tile_width);
    x.off_y = a.bob_offset_y + a.hit_offset_y + a.attack_offset_y + a.idle_offset_y
            + a.slide_offset_y * static_cast<float>(tile_height);
    x.tilt_deg = a.tilt_degrees + a.hit_tilt + a.attack_tilt + a.idle_tilt;
    if (a.hit_flash > 0.f) {
        if (c.is_avatar()) {
            x.flash_r = x.flash_g = x.flash_b = a.hit_flash * 0.5f; // white
        } else {
            x.flash_r = a.hit_flash * 0.6f; // red
            x.flash_g = x.flash_b = -a.hit_flash * 0.5f;
        }
    }
    if (x.active()) {
        creatures_anim_active_ = true; // keep the redraw pump alive
    }
    return x;
}

bool cata_tiles::creatures_require_animation() const { return creatures_anim_active_; }

void cata_tiles::register_tile_hit(const tripoint_bub_ms& p, float dir_x, float dir_y) {
    if (!get_option<bool>("SPRITE_ANIMATIONS") || !get_option<bool>("SPRITE_TILE_HIT")) { return; }
    tile_hits_[p] = tile_hit_state{static_cast<double>(SDL_GetTicks()) / 1000.0, dir_x, dir_y};
}

sprite_xform cata_tiles::tile_hit_xform(const tripoint_bub_ms& p) {
    sprite_xform x;
    if (tile_hits_.empty()) {
        return x; // fast path: no bashes pending
    }
    const auto it = tile_hits_.find(p);
    if (it == tile_hits_.end()) { return x; }
    constexpr double dur = 0.25;
    const double t = anim_wall_now_ - it->second.wall;
    if (t < 0.0 || t >= dur) {
        tile_hits_.erase(it); // self-cleaning when the recoil decays
        return x;
    }
    // Directional recoil away from the basher + decaying wobble (like a creature hit reaction,
    // a bit more extreme). dir is normalized; falls back to a vertical jolt if unknown.
    const float decay = 1.0f - static_cast<float>(t / dur);
    const float kick = std::cos(static_cast<float>(t) * 28.0f) * 9.0f * decay; // px
    float dx = it->second.dir_x;
    float dy = it->second.dir_y;
    if (dx == 0.f && dy == 0.f) {
        dy = 1.f; // unknown source: jolt downward
    }
    x.off_x = kick * dx;
    x.off_y = kick * dy;
    x.fg_only = true;              // recoil the smashed feature, not the ground beneath it
    creatures_anim_active_ = true; // keep the redraw pump alive while shaking
    return x;
}

/* Animation Functions */
/* -- Inits */
void cata_tiles::init_explosion(const tripoint_bub_ms& p, int radius, const std::string& name) {
    do_draw_explosion = true;
    exp_pos = p;
    exp_rad = radius;
    exp_name = name;
}
void cata_tiles::init_custom_explosion_layer(
    const std::map<tripoint_bub_ms, explosion_tile>& layer, const std::string& name) {
    do_draw_custom_explosion = true;
    custom_explosion_layer = layer;
    exp_name = name;
}
void cata_tiles::init_draw_cone_aoe(const tripoint_bub_ms& origin, const one_bucket& layer) {
    do_draw_cone_aoe = true;
    cone_aoe_origin = origin;
    cone_aoe_layer = layer;
}
void cata_tiles::init_draw_bullet(const tripoint_bub_ms& p, std::string name, int rotation) {
    do_draw_bullet = true;
    bul_pos.push_back(p);
    bul_id.push_back(std::move(name));
    bul_rotation.push_back(rotation);
}
void cata_tiles::init_draw_bullets(
    const std::vector<tripoint_bub_ms>& ps, const std::vector<std::string>& names,
    const std::vector<int>& rotations) {
    do_draw_bullet = true;
    bul_pos.insert(bul_pos.end(), ps.begin(), ps.end());
    bul_id.insert(bul_id.end(), names.begin(), names.end());
    bul_rotation.insert(bul_rotation.end(), rotations.begin(), rotations.end());
}
void cata_tiles::init_draw_hit(const tripoint_bub_ms& p, std::string name) {
    do_draw_hit = true;
    hit_pos = p;
    hit_entity_id = std::move(name);
}
void cata_tiles::init_draw_line(
    const tripoint_bub_ms& p, std::vector<tripoint_bub_ms> trajectory, std::string name,
    bool target_line) {
    do_draw_line = true;
    is_target_line = target_line;
    line_pos = p;
    line_endpoint_id = std::move(name);
    line_trajectory = std::move(trajectory);
}
void cata_tiles::init_draw_cursor(const tripoint_bub_ms& p) {
    do_draw_cursor = true;
    cursors.emplace_back(p);
}
void cata_tiles::init_draw_highlight(const tripoint_bub_ms& p) {
    do_draw_highlight = true;
    highlights.emplace_back(p);
}
void cata_tiles::init_draw_weather(weather_printable weather, std::string name) {
    do_draw_weather = true;
    weather_name = std::move(name);
    anim_weather = std::move(weather);
}
void cata_tiles::init_draw_sct() { do_draw_sct = true; }
void cata_tiles::init_draw_zones(const zone_draw_options& options) {
    do_draw_zones = true;
    zone_start = options.start;
    zone_end = options.end;
    zone_offset = options.offset;
    zone_points = options.points;
    zone_point_lookup.clear();
    if (!zone_points.empty()) {
        std::ranges::for_each(zone_points, [&](const tripoint_bub_ms& point) {
            zone_point_lookup.insert(point);
        });
    }
}

/* -- Void Animators */
void cata_tiles::void_explosion() {
    do_draw_explosion = false;
    exp_pos = {-1, -1, -1};
    exp_rad = -1;
}
void cata_tiles::void_custom_explosion() {
    do_draw_custom_explosion = false;
    custom_explosion_layer.clear();
}
void cata_tiles::void_bullet() {
    do_draw_bullet = false;
    bul_pos.clear();
    bul_id.clear();
    bul_rotation.clear();
}
void cata_tiles::void_hit() {
    do_draw_hit = false;
    hit_pos = {-1, -1, -1};
    hit_entity_id.clear();
}
void cata_tiles::void_line() {
    do_draw_line = false;
    is_target_line = false;
    line_pos = {-1, -1, -1};
    line_endpoint_id.clear();
    line_trajectory.clear();
}
void cata_tiles::void_cursor() {
    do_draw_cursor = false;
    cursors.clear();
}
void cata_tiles::void_highlight() {
    do_draw_highlight = false;
    highlights.clear();
}
void cata_tiles::void_weather() {
    do_draw_weather = false;
    weather_name.clear();
    anim_weather.vdrops.clear();
}
void cata_tiles::void_sct() { do_draw_sct = false; }
void cata_tiles::void_zones() {
    do_draw_zones = false;
    zone_points.clear();
    zone_point_lookup.clear();
}
void cata_tiles::void_cone_aoe() {
    do_draw_cone_aoe = true;
    cone_aoe_origin = {-1, -1, -1};
    cone_aoe_layer.clear();
}

/* -- Animation Renders */
void cata_tiles::draw_explosion_frame() {
    for (int i = 1; i < exp_rad; ++i) {
        draw_from_id_string(
            {exp_name, C_NONE, empty_string, corner, 0}, exp_pos + point(-i, -i), std::nullopt,
            std::nullopt, lit_level::LIT, true, 0, false);
        draw_from_id_string(
            {exp_name, C_NONE, empty_string, corner, 1}, exp_pos + point(-i, i), std::nullopt,
            std::nullopt, lit_level::LIT, true, 0, false);
        draw_from_id_string(
            {exp_name, C_NONE, empty_string, corner, 2}, exp_pos + point(i, i), std::nullopt,
            std::nullopt, lit_level::LIT, true, 0, false);
        draw_from_id_string(
            {exp_name, C_NONE, empty_string, corner, 3}, exp_pos + point(i, -i), std::nullopt,
            std::nullopt, lit_level::LIT, true, 0, false);

        for (int j = 1 - i; j < 0 + i; j++) {
            draw_from_id_string(
                {exp_name, C_NONE, empty_string, edge, 0}, exp_pos + point(j, -i), std::nullopt,
                std::nullopt, lit_level::LIT, true, 0, false);
            draw_from_id_string(
                {exp_name, C_NONE, empty_string, edge, 0}, exp_pos + point(j, i), std::nullopt,
                std::nullopt, lit_level::LIT, true, 0, false);

            draw_from_id_string(
                {exp_name, C_NONE, empty_string, edge, 1}, exp_pos + point(-i, j), std::nullopt,
                std::nullopt, lit_level::LIT, true, 0, false);
            draw_from_id_string(
                {exp_name, C_NONE, empty_string, edge, 1}, exp_pos + point(i, j), std::nullopt,
                std::nullopt, lit_level::LIT, true, 0, false);
        }
    }
}

void cata_tiles::draw_custom_explosion_frame() {
    // TODO: Make the drawing code handle all the missing tiles: <^>v and *
    // TODO: Add more explosion tiles, like "strong explosion", so that it displays more info

    // explosion_weak/explosion_medium/explosion removed from tiles in favor of allowing custom
    // explosion sprites.

    int subtile = 0;
    int rotation = 0;

    for (const auto& pr : custom_explosion_layer) {
        const explosion_neighbors ngh = pr.second.neighborhood;

        switch (ngh) {
            case N_NORTH:
            case N_SOUTH:
                subtile = edge;
                rotation = 1;
                break;
            case N_WEST:
            case N_EAST:
                subtile = edge;
                rotation = 0;
                break;
            case N_NORTH | N_SOUTH:
            case N_NORTH | N_SOUTH | N_WEST:
            case N_NORTH | N_SOUTH | N_EAST:
                subtile = edge;
                rotation = 1;
                break;
            case N_WEST | N_EAST:
            case N_WEST | N_EAST | N_NORTH:
            case N_WEST | N_EAST | N_SOUTH:
                subtile = edge;
                rotation = 0;
                break;
            case N_SOUTH | N_EAST:
                subtile = corner;
                rotation = 0;
                break;
            case N_NORTH | N_EAST:
                subtile = corner;
                rotation = 1;
                break;
            case N_NORTH | N_WEST:
                subtile = corner;
                rotation = 2;
                break;
            case N_SOUTH | N_WEST:
                subtile = corner;
                rotation = 3;
                break;
            case N_NO_NEIGHBORS:
                subtile = edge;
                break;
            case N_WEST | N_EAST | N_NORTH | N_SOUTH:
                // Needs some special tile
                subtile = edge;
                break;
        }

        const tripoint_bub_ms& p = pr.first;
        const tile_search_params tile{exp_name, C_NONE, empty_string, subtile, rotation};
        draw_from_id_string(tile, p, std::nullopt, std::nullopt, lit_level::LIT, true, 0, false);
        // Used to be divided into explosion_weak/explosion_medium/explosion.
    }
}

void cata_tiles::draw_cone_aoe_frame() {
    // Should probably jsonize for flamethrower, dragon breath etc.
    static const std::array<std::string, 3> sprite_ids =
        {"shot_cone_weak", "shot_cone_medium", "shot_cone_strong"};

    for (const point_with_value& pv : cone_aoe_layer) {
        const tripoint diff = pv.pt - cone_aoe_origin.raw();
        int rotation = (sgn(diff.x) == sgn(diff.y) ? 1 : 0);

        size_t intensity = (pv.val >= 1.0) + (pv.val >= 0.5);
        const tile_search_params tile{sprite_ids[intensity], C_NONE, empty_string, 0, rotation};
        draw_from_id_string(
            tile, tripoint_bub_ms(pv.pt), std::nullopt, std::nullopt, lit_level::LIT, false, 0,
            false);
    }
}

void cata_tiles::draw_bullet_frame() {
    for (size_t i = 0; i < bul_pos.size(); ++i) {
        const auto tile = tile_search_params{
            .id = bul_id[i],
            .category = C_BULLET,
            .subcategory = empty_string,
            .subtile = 0,
            .rota = bul_rotation[i]};
        draw_from_id_string(
            tile, bul_pos[i], std::nullopt, std::nullopt, lit_level::LIT, false, 0, false);
    }
}
void cata_tiles::draw_hit_frame() {
    std::string hit_overlay = "animation_hit";

    draw_from_id_string(
        {hit_entity_id, C_HIT_ENTITY, empty_string, 0, 0}, hit_pos, std::nullopt, std::nullopt,
        lit_level::LIT, false, 0, false);
    draw_from_id_string(
        {hit_overlay, C_NONE, empty_string, 0, 0}, hit_pos, std::nullopt, std::nullopt,
        lit_level::LIT, false, 0, false);
}
void cata_tiles::draw_line() {
    if (line_trajectory.empty()) { return; }
    static std::string line_overlay = "animation_line";
    if (!is_target_line || g->u.sees(tripoint_bub_ms(line_pos))) {
        for (auto it = line_trajectory.begin(); it != line_trajectory.end() - 1; ++it) {
            draw_from_id_string(
                {line_overlay, C_NONE, empty_string, 0, 0}, *it, std::nullopt, std::nullopt,
                lit_level::LIT, false, 0, false);
        }
    }

    draw_from_id_string(
        {line_endpoint_id, C_NONE, empty_string, 0, 0}, line_trajectory.back(), std::nullopt,
        std::nullopt, lit_level::LIT, false, 0, false);
}
void cata_tiles::draw_cursor() {
    for (const tripoint_bub_ms& p : cursors) {
        draw_from_id_string(
            {"cursor", C_NONE, empty_string, 0, 0}, p, std::nullopt, std::nullopt, lit_level::LIT,
            false, 0, false);
    }
}
void cata_tiles::draw_highlight() {
    for (const tripoint_bub_ms& p : highlights) {
        draw_from_id_string(
            {"highlight", C_NONE, empty_string, 0, 0}, p, std::nullopt, std::nullopt,
            lit_level::LIT, false, 0, false);
    }
}
void cata_tiles::draw_weather_frame() {

    for (auto& vdrop : anim_weather.vdrops) {
        // TODO: Z-level awareness if weather ever happens on anything but z-level 0.
        tripoint_bub_ms p(vdrop.first, vdrop.second, 0);
        if (!tile_iso) {
            // currently in ASCII screen coordinates
            p = p + o.raw();
        }
        draw_from_id_string(
            {weather_name, C_WEATHER, empty_string, 0, 0}, p, std::nullopt, std::nullopt,
            lit_level::LIT, true, 0, false);
    }
}

// §7 world-text layer (Tier 6 slice 4): route SCT through RmlUi's own font engine
// instead of the curses overlay_strings path. OFF by default; A/B via the F4
// "world text (SCT)" checkbox.
bool& world_text_rmlui_enabled() {
    static bool enabled = true;
    return enabled;
}

void cata_tiles::draw_sct_frame(std::multimap<point, formatted_text>& overlay_strings) {
    // Phase 2: Font-only rendering path for SCT.
    // Drop ASCII fallback — tiles-focused scope; accept edge cases with non-Latin scripts or custom
    // tilesets. Apply damage-type color mapping, size scaling hints, and crit effects.

    const bool show_damage = get_option<bool>("ANIMATION_SCT_DAMAGE");
    const bool type_colors = get_option<bool>("ANIMATION_SCT_TYPE_COLORS");
    const bool show_criticals = get_option<bool>("ANIMATION_SCT_CRITICALS");

    for (auto iter = SCT.vSCT.begin(); iter != SCT.vSCT.end(); ++iter) {
        // Gate damage numbers by option, but always show outcome indicators
        // (MISS/DODGE/PARRY/BLOCK/GRAZE).
        if (!show_damage && iter->getFeedbackType() == sct_feedback_type::damage) { continue; }

        const point_bub_ms iD(iter->getPosX(), iter->getPosY());
        const int full_text_length = utf8_width(iter->getText());

        // Apply jitter offset (screen-space pixel offsets from radial distribution).
        const auto jitter = iter->getJitterOffset();

        for (int j = 0; j < 2; ++j) {
            std::string sText = iter->getText((j == 0) ? "first" : "second");
            if (sText.empty()) { continue; }

            // Determine color: use damage-type-specific mapping when enabled.
            int FG;
            if (type_colors && !iter->getText().empty()
                && iter->getDamageType() != sct_damage_type::none) {
                // Map SCT damage type to a game_message_type, then to tile color.
                static const std::map<sct_damage_type, game_message_type> dt_color_map{
                    {sct_damage_type::bash, m_neutral},     // white/gray - blunt impact
                    {sct_damage_type::cut, m_info},         // cyan/light blue - sharp slicing
                    {sct_damage_type::stab, m_bad},         // red-orange - piercing
                    {sct_damage_type::acid, m_good},        // green-yellow - corrosive
                    {sct_damage_type::heat, m_warning},     // orange/red - fire
                    {sct_damage_type::cold, m_info},        // light blue/cyan - ice
                    {sct_damage_type::dark, m_mixed},       // purple/magenta - eldritch
                    {sct_damage_type::light, m_critical},   // yellow/gold - holy radiant
                    {sct_damage_type::psi, m_info},         // blue/violet - psychic
                    {sct_damage_type::electric, m_warning}, // bright yellow - lightning
                    {sct_damage_type::bullet, m_neutral},   // white - fast projectile
                };
                const auto color_it = dt_color_map.find(iter->getDamageType());
                game_message_type gmt = (color_it != dt_color_map.end()) ? color_it->second : m_bad;
                FG = msgtype_to_tilecolor(
                    gmt, iter->getStep() >= scrollingcombattext::iMaxSteps / 2);
            } else {
                // Fall back to original behavior: use the stored message type.
                FG = msgtype_to_tilecolor(
                    iter->getMsgType((j == 0) ? "first" : "second"),
                    iter->getStep() >= scrollingcombattext::iMaxSteps / 2);
            }

            // Critical hit visual effects: gold/yellow overlay for crits.
            // Gate behind ANIMATION_SCT_CRITICALS option.
            if (show_criticals) {
                if (iter->isCrit() && !iter->isTripleCrit()) {
                    FG = msgtype_to_tilecolor(m_critical, false); // bright yellow/gold
                } else if (iter->isTripleCrit()) {
                    // Triple crit: extra-bright magenta/red for maximum impact.
                    FG = msgtype_to_tilecolor(m_headshot, false); // bright pink/magenta
                }
            }

            const auto direction = iter->getDirecton();
            // Compensate for string length offset added at SCT creation
            // (it will be readded using font size and proper encoding later).
            const int direction_offset = (-displace_XY(direction).x + 1) * full_text_length / 2;

            // Apply jitter offset to the screen position.
            const point screen_pos = player_to_screen(iD + point(direction_offset, 0)) + jitter;

            overlay_strings.emplace(screen_pos, formatted_text(sText, FG, direction));

            // Triple crit flash effect: render text twice with slight offset on frame 0.
            if (iter->isTripleCrit() && iter->getStep() == 0) {
                const point flash_offset = jitter + point(1, -1);
                overlay_strings.emplace(
                    player_to_screen(iD + point(direction_offset, 0)) + flash_offset,
                    formatted_text(sText, FG, direction));
            }
        }
    }
}

void cata_tiles::draw_zones_frame(std::multimap<point, formatted_text>& overlay_strings) {
    const bool has_custom_points = !zone_points.empty();
    const auto min_local =
        has_custom_points
            ? point_bub_ms(
                  std::ranges::min(zone_points, {}, [](const tripoint_bub_ms& p) { return p.x(); })
                      .x(),
                  std::ranges::min(zone_points, {}, [](const tripoint_bub_ms& p) { return p.y(); })
                      .y())
            : zone_offset.xy() + zone_start.xy();
    const auto max_local =
        has_custom_points
            ? point_bub_ms(
                  std::ranges::max(zone_points, {}, [](const tripoint_bub_ms& p) { return p.x(); })
                      .x(),
                  std::ranges::max(zone_points, {}, [](const tripoint_bub_ms& p) { return p.y(); })
                      .y())
            : zone_offset.xy() + zone_end.xy();
    const tripoint_bub_ms center_local(
        (min_local.x() + max_local.x()) / 2, (min_local.y() + max_local.y()) / 2,
        get_avatar().bub_pos().z());
    const auto lookup_local =
        has_custom_points
            ? tripoint_bub_ms(zone_points.front().xy(), zone_points.front().z())
            : center_local;

    // get_zone_at expects absolute coordinates
    const zone_data* zone = zone_manager::get_manager().get_zone_at(
        get_map().bub_to_abs(lookup_local));

    if (has_custom_points) {
        if (zone) {
            overlay_strings.emplace(
                player_to_screen(center_local.xy()),
                formatted_text(zone->get_name(), catacurses::white, direction::NORTH));
        }
        return;
    }

    const point screen_tl = player_to_screen(min_local);
    const point screen_br = player_to_screen(max_local) + point(tile_width, tile_height);

    draw_zone_overlay({
        .renderer = renderer,
        .rect = {screen_tl.x, screen_tl.y, screen_br.x - screen_tl.x, screen_br.y - screen_tl.y},
        .color = zone ? curses_color_to_SDL(zone->get_type().obj().color())
                      : curses_color_to_SDL(c_light_green),
        .overlay_strings = overlay_strings,
        .name = zone ? zone->get_name() : "",
    });
}

void cata_tiles::draw_footsteps_frame(const tripoint_bub_ms& center) {
    static const std::string id_footstep = "footstep";
    static const std::string id_footstep_above = "footstep_above";
    static const std::string id_footstep_below = "footstep_below";

    for (const tripoint_bub_ms& pos : sounds::get_footstep_markers()) {
        if (pos.z() == center.z()) {
            draw_from_id_string(
                {id_footstep, C_NONE, empty_string, 0, 0}, pos, std::nullopt, std::nullopt,
                lit_level::LIT, false, 0, false);
        } else if (pos.z() > center.z()) {
            draw_from_id_string(
                {id_footstep_above, C_NONE, empty_string, 0, 0}, pos, std::nullopt, std::nullopt,
                lit_level::LIT, false, 0, false);
        } else {
            draw_from_id_string(
                {id_footstep_below, C_NONE, empty_string, 0, 0}, pos, std::nullopt, std::nullopt,
                lit_level::LIT, false, 0, false);
        }
    }
}
/* END OF ANIMATION FUNCTIONS */
