#pragma once

// Per-palette shade ramps (Step 7 of the grid-decoupled lighting plan).
//
// Pixel art does not darken by multiplying toward grey — it steps DOWN a hand-authored
// ramp, so a red couch in shadow becomes dark red (usually cooler and more saturated),
// not desaturated brown. Multiplying the texel by a light value, which is what the
// renderer does today, is the single biggest reason HD lighting reads as "bolted onto"
// pixel art.
//
// This builds those ramps procedurally from the tileset's OWN pixels, so no per-tileset
// authoring is needed: histogram every sheet, keep the most frequent colours as palette
// rows, generate `steps` shades per row by the standard convention (shadows shift hue
// cool and lose value faster than saturation; highlights shift warm, gain value and
// lose saturation), and build a 32^3 lookup so the shader can map any texel to its row.
//
// The lookup is nearest-neighbour in OkLab, not RGB: plain RGB distance picks visibly
// wrong rows on saturated art (it will happily match a saturated red to a dark brown).

#include <cstdint>
#include <unordered_map>
#include <vector>

struct SDL_Surface;

namespace lighting
{

/// Procedural ramp generation. Defaults are the standard pixel-art convention.
struct ramp_gen_params {
    int steps = 8;
    float shadow_hue_shift = -0.055f; // fraction of the hue circle at step 0
    float light_hue_shift = 0.030f;   // at the brightest step
    float shadow_value = 0.28f;       // value multiplier at step 0
    float light_value = 1.30f;        // at the brightest step
    float shadow_sat = 1.20f;         // saturation multiplier at step 0
    float light_sat = 0.72f;          // at the brightest step
};
struct palette_ramp_data {
    std::vector<std::uint32_t> ramp;  // palette_size * steps, RGBA8 (0xAABBGGRR)
    std::vector<std::uint32_t> index; // 32*32*32, palette row per quantised RGB
    int palette_size = 0;
    int steps = 0;
    // Coverage diagnostics — is PALETTE_ROWS actually enough for this tileset?
    // A large tail_pixels/total_pixels ratio, or a kept_min_count that is still
    // high, means the histogram is truncating colours the art genuinely uses and
    // PALETTE_ROWS should go up (the buffers are tiny).
    std::uint64_t total_pixels = 0;  // opaque pixels histogrammed
    std::uint64_t tail_pixels = 0;   // pixels whose colour did NOT make the palette
    std::uint32_t unique_colours = 0;
    std::uint32_t kept_min_count = 0; // frequency of the least-frequent KEPT row
};

/// Number of palette rows kept. 256 is enough for MSX++UnDeadPeopleEdition; the
/// buffers are tiny either way (256*8 uints = 8 KB, plus 32^3 = 128 KB).
inline constexpr int PALETTE_ROWS = 256;
/// Index LUT side. 32^3 = 128 KB. Do not raise without measuring — 48^3 is 442 KB
/// and the perceptual gain is small.
inline constexpr int PALETTE_LUT_SIDE = 32;

/// Accumulates colours from every tileset sheet, then bakes ramps + the lookup.
class palette_accumulator
{
    public:
        auto reset() -> void { hist_.clear(); }
        /// Histogram every pixel with alpha >= 128, keyed on RGB888.
        auto add_surface( const SDL_Surface &s ) -> void;
        auto build( const ramp_gen_params &gen ) const -> palette_ramp_data;
        auto empty() const -> bool { return hist_.empty(); }

    private:
        std::unordered_map<std::uint32_t, std::uint32_t> hist_; // RGB888 -> count
};

} // namespace lighting
