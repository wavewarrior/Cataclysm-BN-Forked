#pragma once
#include <cstdint>
namespace lighting {
/// Fragment storage-buffer slots for sprite.frag, in bind order. The shader
/// declares them contiguously at t2..t8 (space2); DXC strips any buffer the
/// shader does not read, which shifts the t-range and makes D3D12 reject the
/// root signature. Keep this enum, the bind array in sprite_batcher.cpp and
/// the register declarations in sprite.frag.hlsl in lockstep.
enum class frag_sbuf : std::uint32_t {
    emitters = 0, sdf = 1, sky_vis = 2, gi = 3, sky = 4, ramp = 5, pal_index = 6,
};
inline constexpr std::uint32_t FRAG_SBUF_COUNT = 7u;
inline constexpr std::uint32_t FRAG_STORAGE_TEX_COUNT = 1u;
} // namespace lighting
