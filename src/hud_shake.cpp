#include "hud_shake.h"

#include <cmath>
#include <random>

namespace hud_shake
{

namespace
{

constexpr float MAX_PX = 6.0f;
constexpr float DECAY_RATE = 25.0f;

float g_intensity = 0.0f;

// Per-frame RNG for shake direction (file-local, doesn't perturb game RNG).
std::mt19937 rng{ 0xCAFEu };
std::uniform_real_distribution<float> dist( -1.0f, 1.0f );

} // namespace

auto trigger( float intensity ) -> void
{
    g_intensity = std::max( g_intensity, std::clamp( intensity, 0.0f, 1.0f ) );
}

auto sample() -> shake_offset
{
    if( g_intensity <= 0.01f ) {
        return { 0.0f, 0.0f };
    }
    const float scale = g_intensity * MAX_PX;
    return { dist( rng ) * scale, dist( rng ) * scale };
}

auto tick( float dt_seconds ) -> void
{
    if( g_intensity <= 0.0f ) {
        return;
    }
    g_intensity *= std::exp( -DECAY_RATE * dt_seconds );
    if( g_intensity < 0.01f ) {
        g_intensity = 0.0f;
    }
}

auto intensity() -> float
{
    return g_intensity;
}

} // namespace hud_shake