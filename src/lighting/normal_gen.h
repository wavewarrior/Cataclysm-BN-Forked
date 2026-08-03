#pragma once

// Procedural normal maps generated from sprite ART, at tileset load.
//
// WHY THIS EXISTS. `surface_normal()` in sprite.frag.hlsl derives relief from a 4-tap
// gradient of ALPHA, and its own comment concedes the limit: "Interior pixels (a~=1)
// flatten". Every BN terrain sprite is a fully-opaque tile, so dx == dy == 0 and the
// normal is exactly (0,0,1). Measured in debug view 9 (flat reads 128,128,255): grass,
// asphalt, building wall and building interior ALL read 121.6, 121.6, 243.5 --
// identical. `nrm_amount`, `nrm_relief` and `nrm_elev` therefore all multiply ZERO, so
// no knob can produce shading and sun direction can only ever appear as cast shadows.
// This module replaces the SOURCE of the normal so those knobs have something to scale.
//
// TECHNIQUE: "beveling" (dual-mask EDT), arXiv:2212.09692 sec II-D -- the only
// fully-automatic method that survey found to produce coherent geometry for pixel art:
//
//     dual binary masks -> euclidean distance transform -> weighted merge
//     -> gaussian smooth -> Sobel -> normal
//
// Mask 1 (external): the alpha silhouette.
// Mask 2 (internal): colour contours INSIDE the silhouette. This is the mask that
// carries essentially everything for BN terrain, because a wall tile is fully opaque so
// mask 1 degenerates to "distance to tile border" (see the full-tile gate below).
//
// REJECTED ALTERNATIVES, each with the measurement that rejected it. These are the
// reason the code looks the way it does; do not re-litigate them without new numbers,
// and do not "simplify" this module back into any of them:
//
//  * Sobel from the COLOUR map (i.e. luminance as height). The survey: "uses
//    information exclusively from edges, resulting in incoherent geometry" -- inverted
//    volumes and grooves, because it reads pre-baked shading as height. BN art is
//    heavily pre-shaded. This module uses colour DISTANCE as a segment boundary and
//    never luminance as height, which is what keeps pre-baked shading from inverting
//    volumes (the documented failure mode, arXiv:2212.09692 sec IV-B).
//  * 3x3 MEDIAN prefilter before contour detection. Implemented and measured: it erased
//    the exact signal it was meant to clean up -- `t_rock_wall` brick mortar and plank
//    seams went from ny std 0.211 to 0.116, visibly flat.
//  * ADAPTIVE percentile threshold (a percentile of each sprite's own gradient).
//    Implemented and measured: it fires on a fixed fraction of pixels BY CONSTRUCTION,
//    even on a uniform sprite with no contours at all, so the EDT invented large smooth
//    domes out of quantisation noise -- confident nonsense, worse than mush.
//  * Hand-painted normals / hand height maps / Sprite Lamp's four illumination angles:
//    best quality, but ~30k sprites. Not a candidate here.
//  * Deep generative models: "weak internal geometry on pixel art", and trained on
//    non-pixel-art data.
//
// Dither is therefore rejected AFTER masking, by a SHAPE statistic
// (`coherence_gap_run`) rather than by a colour constant -- which is why
// `edge_threshold` needs no per-tileset retuning and survives a user swapping tilesets.
//
// SCOPE. This delivers LOCAL relief only: brick reads as rounded blocks, herringbone
// planks as individual planks. It does NOT deliver macro facing -- measured S-lit
// bottom-minus-top delta was ~0.3/255 before the coherence gate and 0 after, matching
// the survey's finding that bevel geometry sits near contours "without a good
// distribution of information along the visible surfaces". A wall face brightening with
// light direction needs the orthogonal per-sprite facing term (`sprite_instance::
// face_amt` plus the `quad_v` varying). The two compose; neither is redundant.

#include <cstdint>
#include <span>
#include <vector>

#include <SDL3/SDL_rect.h>

struct SDL_Surface;

namespace lighting
{

/// Generator tuning. Every default was measured against MSX++UnDeadPeopleEdition; see
/// the per-field notes and the rejected-alternatives block above before changing one.
struct normal_gen_params {
    /// Colour distance (in 0..1 RGB units) counting as an internal contour. A GLOBAL
    /// constant is correct here only because the coherence gate cleans up afterwards:
    /// on its own, th=0.22 keeps brick (0.53 edge density) but kills plank seams (0.00).
    float edge_threshold = 0.14f;
    /// Gaussian sigma on the merged height map.
    float blur_sigma     = 1.1f;
    /// Height-to-normal gain. This is the "how blue is the atlas" control.
    float slope          = 2.6f;
    /// Gap run at/below which a sprite reads as noise and gets no relief at all.
    float coh_lo         = 2.9f;
    /// Gap run at/above which a sprite reads as fully structured.
    float coh_hi         = 4.0f;
    /// Edge density below which the mask is too sparse to be structure. Closes the one
    /// hole in the gap-run statistic -- see `coherence_gap_run`.
    float min_density    = 0.05f;
    /// Max colour gradient inside the silhouette below which the sprite is genuinely
    /// flat and anything extracted would be quantisation noise promoted to geometry.
    float flat_eps       = 0.04f;
    /// Blend weight of the external (silhouette) EDT against the internal one. Also
    /// the floor on `slope` for a non-full tile: a cut-out sprite keeps its bevel even
    /// when the coherence gate zeroes its internal relief.
    float ext_weight     = 0.5f;
};

/// Diagnostics for one generated sprite. Not consumed by the renderer; this is what
/// makes the gates inspectable while tuning, and what the unit tests assert on.
struct normal_gen_stats {
    /// Mean non-edge run length; 0 when short-circuited by `flat` or by sparsity.
    float coherence = 0.0f;
    /// The coherence gate result in 0..1, written to the B channel of every opaque
    /// texel. 0 means "deliberately left flat", which is a correct outcome.
    float amplitude = 0.0f;
    /// Fraction of the rect classified as an internal contour.
    float density   = 0.0f;
    /// Opaque region touches all four borders AND mean alpha > 0.97.
    bool  full_tile = false;
    /// Whole silhouette varies by less than `flat_eps`.
    bool  flat      = false;
};

/// One sprite rect to generate. All five fields are required; there are no defaults
/// because a partially initialised request has no useful meaning.
struct normal_gen_request {
    /// Source sheet. Any pixel format; read through SDL_GetPixelFormatDetails.
    const SDL_Surface *src;
    /// The sprite's rect within `src`. Must lie entirely inside it.
    SDL_Rect rect;
    /// Destination. MUST be SDL_PIXELFORMAT_RGBA32 (byte order R,G,B,A).
    SDL_Surface *dst;
    /// Top-left of the `rect.w` x `rect.h` region written in `dst`.
    SDL_Point dst_at;
    normal_gen_params params;
};

/// Generate a normal map for ONE sprite rect of `req.src` into `req.dst` at
/// `req.dst_at`. `req.dst` must be RGBA32. Returns stats for diagnostics and tests.
///
/// Texel encoding, per opaque pixel:
///   R = nx * 0.5 + 0.5      G = ny * 0.5 + 0.5
///   B = amplitude * 255     A = 255
/// nz is NOT stored: the normal is unit length with nz > 0 by construction, so the
/// shader reconstructs it as sqrt(max(0, 1 - nx*nx - ny*ny)) and B is free to carry the
/// gate scalar. Rounding is to nearest, so a flat normal encodes as exactly 128,128 --
/// the offline prototype truncated (giving 127), which is fine for a preview PNG but
/// would put every "flat" texel one code value off the neutral the atlas is prefilled
/// with. Decode side: `n.xy = t.rg * 2 - 1`, blend weight = `t.b`.
///
/// TRANSPARENT pixels get nx = ny = 0 (as the prototype does) AND B = 0, which is a
/// deliberate deviation from "B = amplitude everywhere": outside the silhouette is the
/// one place the existing `surface_normal()` alpha bevel produces a non-flat normal, so
/// blending a hard (0,0,1) over it at full weight would delete the only relief that
/// works today. Zero weight there leaves `surface_normal()` untouched.
///
/// A no-op (default stats, nothing written) if `src`/`dst` are null, the rect is empty
/// or out of bounds, or `dst` is not RGBA32.
auto generate_sprite_normal( const normal_gen_request &req ) -> normal_gen_stats;

/// Exact euclidean distance transform: for each SET pixel of `mask`, the distance in
/// pixels to the nearest UNSET pixel; UNSET pixels get 0. `mask` is row-major with
/// `w * h` entries, nonzero meaning set. Returns `w * h` distances, empty on bad input.
///
/// EXACT (Felzenszwalb & Huttenlocher, run per column then per row), deliberately NOT a
/// chamfer or Manhattan approximation. The merged height map is differentiated by a
/// Sobel immediately afterwards, so the anisotropy of an approximate transform lands
/// straight in nx/ny as a per-axis bias -- i.e. as a fabricated lighting direction
/// baked into the atlas. A single seed one pixel diagonally away must read 1.414, not
/// the 2.0 a Manhattan pass would give.
///
/// A mask with no unset pixel has no boundary and comes back all zeros. Callers
/// normalise by the maximum, which turns the mathematically-correct "infinity
/// everywhere" into a constant field whose Sobel is zero, so zeros is behaviour
/// identical and cannot leak a sentinel into anyone's arithmetic.
auto exact_edt( std::span<const std::uint8_t> mask, int w, int h ) -> std::vector<float>;

/// Mean NON-edge ("gap") run length of the edge mask `mask` (nonzero = edge), pooled
/// over a row scan and a column scan and averaged. Separates structure from dither.
///
/// Measured over 12 UnDeadPeople tiles at `edge_threshold` 0.14 this separates cleanly,
/// with nothing in the gap: noise 1.60-2.89 (dithered concrete, grass, door) vs
/// structured 4.04-23.84 (brick, planks, sidewalk, pavement, metal floor). That is
/// where `coh_lo` = 2.9 and `coh_hi` = 4.0 come from.
///
/// Two alternatives were implemented and MEASURED TO FAIL on this data:
///
///  * EDGE run length (runs of edge pixels rather than gaps): herringbone plank seams
///    are DIAGONAL, so they give short runs along both axes and scored 2.39 -- BELOW
///    dithered concrete at 3.26. Axis-aligned edge runs cannot see diagonal structure.
///  * SCALE RATIO (edge density at native resolution vs a 2x box downsample), which is
///    orientation-free and so looked more principled: structured 0.00-1.35 vs noise
///    0.02-0.63, i.e. heavily overlapping and unusable. Planks scored 0.06, identical
///    to grass noise. Box downsampling AVERAGES, which erases 1px plank and pavement
///    seams along with the dither, so it discriminates thick-vs-thin features rather
///    than structure-vs-noise.
///
/// Gap length has one hole: it RISES as edges get sparser, so a sprite carrying a few
/// isolated speckles scores high and would pass the gate -- the sparse-seed to
/// invented-blob failure the gate exists to stop. That is closed separately by
/// `normal_gen_params::min_density`, not by this statistic.
auto coherence_gap_run( std::span<const std::uint8_t> mask, int w, int h ) -> float;

} // namespace lighting
