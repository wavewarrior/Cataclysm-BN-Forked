// JFA shared constants — included by jfa_seed.comp.hlsl, jfa_flood.comp.hlsl,
// jfa_resolve.comp.hlsl. Keep in sync with gpu_sdf_pass.cpp SDF_SS constant.

static const int   SDF_SS    = 8;     // subcells per tile side (SS-grid = map_w*SDF_SS × map_h*SDF_SS)
static const float SDF_FLOOD = 16.0;  // max flood step size (covers ~4 tiles in SS units)

// 4x4 ordered (Bayer) matrix, values 0..15. Used by jfa_seed to turn the fractional
// coverage field occ_raster produces into per-subcell occupancy. Shift-stable:
// map::shift moves the bubble by whole submaps (12 tiles = 96 subcells) and
// 96 % 4 == 0, so the pattern's phase survives a shift and never crawls.
static const float k_jfa_bayer4[16] = {
     0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
     3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0
};
