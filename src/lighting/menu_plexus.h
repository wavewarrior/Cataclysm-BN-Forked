#pragma once
#ifndef CATA_SRC_LIGHTING_MENU_PLEXUS_H
#define CATA_SRC_LIGHTING_MENU_PLEXUS_H
#include <cstdint>
#include <vector>

namespace lighting
{

struct plexus_particle {
    float x, y;
    float vx, vy;
    float life;
    std::uint8_t glyph;
};

struct plexus_config {
    int particle_count = 200;
    int connection_dist = 180;
    float speed_scale = 0.15f;
    int node_radius = 1;
    std::uint8_t bg_r = 10, bg_g = 10, bg_b = 15, bg_a = 255;
    std::uint8_t line_r = 161, line_g = 136, line_b = 95, line_a = 40;
    std::uint8_t node_r = 196, node_g = 168, node_b = 50, node_a = 80;
    std::uint8_t glyph_r = 90, glyph_g = 176, glyph_b = 160, glyph_a = 100;
    bool enabled = true;
};

void plexus_init();
void plexus_finish();
void plexus_resize( int width, int height );
void plexus_step();
auto plexus_pixels() -> const std::vector<std::uint8_t> &;
auto plexus_width() -> int;
auto plexus_height() -> int;
auto plexus_generation() -> unsigned;
auto plexus_get_config() -> plexus_config &;

extern bool g_plexus_visible;

} // namespace lighting
#endif // CATA_SRC_LIGHTING_MENU_PLEXUS_H
