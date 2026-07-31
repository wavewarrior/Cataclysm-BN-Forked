// JFA shared constants — included by jfa_seed.comp.hlsl, jfa_flood.comp.hlsl,
// jfa_resolve.comp.hlsl. Keep in sync with gpu_sdf_pass.cpp SDF_SS constant.

static const int   SDF_SS    = 8;     // subcells per tile side (SS-grid = map_w*SDF_SS × map_h*SDF_SS)
static const float SDF_FLOOD = 16.0;  // max flood step size (covers ~4 tiles in SS units)
