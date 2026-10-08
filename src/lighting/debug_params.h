#pragma once
#ifndef CATA_SRC_LIGHTING_DEBUG_PARAMS_H
#    define CATA_SRC_LIGHTING_DEBUG_PARAMS_H

// The DebugParams cbuffer, moved verbatim out of `lighting/sprite_batcher.h` so the
// SDL-free `lighting/lighting_settings.h` can own it by value (sprite_batcher.h
// includes gpu_device.h, which pulls in SDL). Nothing here changes: same field
// order, same defaults, same 272-byte wire layout; the static assertion is kept in
// `sprite_batcher.cpp` as before and repeated here so the guarantee travels with
// the struct.

#    include <cstdint>

namespace lighting {
// Debug visualisation + runtime tuning knobs (DebugParams cbuffer at
// register(b2, space3); 272 bytes; wire-stable). debug_mode dispatches per-
// component visualisations in the fragment shader; emitter/sun/sky_scale
// multiply the corresponding contributions; shadow_k and shadow_steps tune
// the shared sphere-trace (emitter + sun); dither_amt/dither_bands tune the
// world-locked ordered (Bayer) dither. Defaults are the shipping look
// (scale=1, k=8, steps=16, dither on at 32 bands).
struct debug_params {
    uint32_t debug_mode = 0u;
    float debug_opacity = 0.6f;
    float emitter_scale = 1.0f;
    float sun_scale = 1.0f;
    float sky_scale = 1.0f;
    float shadow_k = 8.0f;
    uint32_t shadow_steps = 16u;
    float dither_amt = 1.0f;
    // The ordered dither quantises the dynamic light into this many bands. At 12
    // bands each step is 1/12 of the light range, which over a scene whose total
    // dynamic light sat near 0.5 left only ~6 usable levels — coarse enough to
    // read as blotching rather than as dither. 32 keeps the mean-preserving
    // stipple while dropping the step below visual threshold.
    float dither_bands = 32.0f;
    // 1-bounce indirect multiplier (0=off); Alt+F8/F9 to tune. Was 0.35, tuned down
    // from 0.60 against the OLD tile-resolution `gi_bounce.comp.hlsl` pass, whose
    // error was tile-scale soft blobs that 0.60 made the dominant large-scale image
    // structure. Stage 7 (gpu-daylight-black-scene-bisect-plan) replaced that pass
    // with Radiance Cascades — exactly the "higher-resolution... GI pass" this
    // comment used to say was the real fix — verified smooth/continuous with no
    // blocky steps or cascade seams (see the plan doc's visual-inspection rounds).
    // With the blob problem gone, 0.35 was just leaving window-portal-lit interiors
    // and colour bleed reading too dim relative to the un-scaled direct sun term.
    // Raised to 0.7, the value RC's cleaner output can actually carry without
    // reintroducing a dominant low-frequency artifact.
    float gi_strength = 0.7f;
    // Vision rework knobs (Stoneshard-style). All default ON so the effect ships;
    // set any to its off-value to bisect live. Wire-stable with DebugParams cbuffer.
    float vis_curve = 1.0f;    // vision-edge falloff exponent (0=off → no falloff)
    float mem_dim = 0.35f;     // memorized-tile brightness floor (effect 3)
    float portal_reach = 8.0f; // Step 3: sky-portal scan march reach, tiles. Was dbg_pad_a.
    float night_floor = 0.02f; // ambient floor at night   (effect 4)
    float day_floor = 0.05f;   // ambient floor at noon     (effect 4)
    float portal_dirs = 16.0f; // Step 3: sky-portal scan direction count. Was dbg_pad_b.
    float dbg_pad_c = 0.0f;    // reserved (was grade_cool)
    float dbg_pad_d = 0.0f;    // reserved (was grade_bright)
    // Radial player-distance falloff radius (tiles; 0 = off). Read by the Step 5b
    // sub-tile vision carve. Ships at 0: the carve's LOS term is the point of the
    // effect, and a 16-tile radial dim would silently darken daylight scenes.
    float vis_radius = 0.0f;
    float player_x = 0.0f;    // DATA (not a knob): player map-tile centre x
    float player_y = 0.0f;    // DATA: player map-tile centre y
    float mem_radius = 30.0f; // memory distance-fade scale in tiles (effect 3)
    // Bucket A / A1 surface-normal knobs (live; sprite.frag surface_normal + Lambert).
    float nrm_amount = 0.9f;   // normal Lambert blend: 0=flat(off) .. 1=full
    float nrm_relief = -2.0f;  // tilt magnitude; SIGNED — negative flips global relief dir
    float nrm_elev = 0.3f;     // implied light height; LOWER=more grazing=stronger relief
    float dbg_pad_e = 0.0f;    // reserved (was sdf_sharp — SDF sampling is hardwired bilinear)
    float ao_strength = 0.35f; // A4 ambient occlusion: 0=off .. 1=full SDF-cavity darkening (ships
                               // ON)
    float shadow_mask_str = 1.0f; // silhouette sun-shadow mask on ground (Phase 2.3: shipped ON;
                                  // 0=off). The mask is the SOLE sun-shadow source for tall
                                  // sprites (trees/creatures) — see tile_occlusion.h.
    // Foliage sway (vertex stage; sprite.vert reads these via DebugParams b2/space1).
    float sway_amp = 3.0f;  // wind displacement amplitude in pixels (0=off)
    float sway_freq = 1.2f; // wind oscillation frequency (Hz-ish)
    float anim_time = 0.0f; // DATA (not a knob): wrapped render seconds, injected per-frame
    // Wet specular glint strength. DATA (not a slider): the frame code folds the
    // user knob g_spec_strength with rain intensity per-frame so the sheen only
    // shows while raining. 0 = off.
    float spec_strength = 0.0f;
    // P1: contribution epsilon for shadow march gating. 0 means use shader default.
    float light_eps = 0.0f;
    // P2: max emitters per pixel that get full shadow trace (cast as uint in HLSL).
    float max_shadow_k = 16.0f;
    // P5b: sky/sun quality knobs (sky_sun.comp cbuffer). Float cast to uint in HLSL.
    float sky_dirs = 8.0f;     // sky hemisphere directions (1=flat sky, 16=high quality)
    float sky_reach = 10.0f;   // sky march max distance (tiles)
    float sun_steps = 24.0f;   // celestial march steps
    float sun_penumbra = 4.0f; // penumbra angular samples (1=hard edge, 6=very soft)
    // Wave 2: vegetation life knobs (vertex stage; sprite.vert reads via DebugParams b2/space1).
    float ripple_k = 1.5f;          // intra-sprite column UV desync (0=rigid, 2=heavy shear)
    float gust_amp = 0.4f;          // multi-octave wind gust envelope amplitude (0=steady)
    float gust_freq = 0.3f;         // gust envelope frequency (Hz-ish, slow)
    float part_radius = 2.5f;       // player foliage parting radius in tiles (0=off)
    float part_strength = 0.5f;     // player foliage parting push strength (0=off)
    float nrm_entity_amount = 0.3f; // entity (tall sprite) normal relief: 0=flat .. 1=full bevel
    // Pixel-art light quantisation (Step 1).
    float texels_per_tile = 32.0f; // DATA: tileset native tile width in art texels
    float light_quant = 1.0f;      // 1 = snap light sample to art texels, 0 = per-screen-pixel
    // Sub-tile occluders (Step 3/4).
    float occ_soft_gain = 1.0f;  // partial-occluder block gain (0 = hard occluders only)
    float self_eps_tall = 0.55f; // trace_shadow self-shadow escape radius for TALL sprites
    // Palette shade ramps (Step 7).
    float ramp_enable = 1.0f;  // 0 = plain multiply, 1 = full ramp resolve
    float ramp_steps = 8.0f;   // shade steps per palette row (must match built LUT)
    float ramp_chroma = 0.35f; // how much coloured light tints the ramped surface
    // Step 6: SDF-guided bilateral GI upsample. 1 = reject GI taps across an SDF
    // discontinuity (bounce stops at walls), 0 = plain bilinear (the pre-Step-6
    // behaviour). Occupies what was dbg_pad2; size unchanged.
    float gi_bilat = 1.0f;
    // Step 8: sub-tile vision FRONTIER. The outward edge of the seen region is drawn
    // as full-tile `lighting_*` overlay sprites, so it can only ever be a tile
    // staircase — the one grid artefact the rest of this work leaves behind, and it
    // reads badly next to the now-smooth lighting. 1 = feather the overlay across
    // the tile from its neighbours' visibility, 0 = the old hard tile edge.
    float vis_edge = 1.0f;
    // Normalised V offset from a colour texel to its NORMAL texel in the same atlas
    // page (double-height page => 0.5). 0.0 disables the procedural normal atlas
    // entirely and sprite.frag falls back to surface_normal() unchanged, which is why
    // it ships at 0.0: the value is supplied per-frame from the atlas once a page
    // actually carries normals.
    float nrm_atlas_v = 0.0f;
    // Signed strength of the per-sprite vertical-FACE arc in sprite.frag: how far the
    // surface normal tilts toward the viewer at a sprite's base and away at its top,
    // scaled by sprite_instance::face_amt. Signed so the F4 panel can flip the sense
    // (which end reads as "lit from the south") without a rebuild. face_amt is 0 for
    // everything that is not a wall/window/tall furniture, so this is inert there
    // regardless of its value.
    float face_arc = 1.5f;
    // Radial macro-normal blend: 0=flat(off) .. 1=full cylinder. Treats each TALL
    // sprite (creatures/trees/tall furniture/items — frag_is_tall_n) as a gently
    // rounded volume so Lambert shading responds to light direction even for opaque
    // sprites where alpha-gradient normals are (0,0,1). Blends with base_n (atlas
    // micro-relief) before the per-edge face_arc block. EXCLUDED for flat ground/
    // floor tiles: they tile seamlessly edge-to-edge, so a per-tile radial bump
    // there repeats identically across every tile and reads as a checkerboard of
    // diamonds instead of one flat plane (see sprite.frag.hlsl apply_radial_n).
    float nrm_radial_amount = 0.4f;
    // Passing cloud shadows (procedural, moving noise pattern of cloud coverage
    // drifting over the terrain). Multiplies the SUN contribution only — clouds
    // block direct sunlight, ambient sky-light is unaffected — so this is an
    // exact no-op at night (sun_intensity=0) and stacks correctly UNDER
    // weather_cloud_mult() (src/sdl_render_frame.cpp): that is a flat whole-sky
    // multiplier for the current weather TYPE; this is the moving SHAPE of
    // individual clouds passing overhead on top of it.
    float cloud_strength = 0.8f;   // 0=off .. 1=full darkening under a cloud (ships ON)
    float cloud_scale = 0.03f;     // noise frequency, tiles^-1 (~33-tile cloud period)
    float cloud_wind_x = 0.4f;     // cloud drift velocity, world tiles/second, x
    float cloud_wind_y = 0.15f;    // cloud drift velocity, world tiles/second, y
    float cloud_threshold = 0.46f; // noise cutoff where a cloud begins casting shadow
    float cloud_softness = 0.06f;  // feather width of the cloud edge around cloud_threshold
    // Canopy cut-out (sprite.frag): soft circular hole around the player in
    // overhanging terrain canopies so the character stays visible under the
    // leaves. Keys off sprite_instance::cutout, so only flagged canopies are
    // affected. Radius/feather in tiles; radius 0 = effect off.
    float cutout_radius = 0.55f;  // hole radius around the player tile centre (tiles)
    float cutout_feather = 0.22f; // hole edge softness (tiles)
    // SDF sun-shadow penumbra (sky_sun.comp): feather width in tiles of the
    // shadow edge around occluders. 0 = hard edge; larger = softer, longer
    // penumbra. The SDF distance IS the miss distance, so this is a direct
    // clearance-to-lightness mapping. Occupies a former pad slot — the struct
    // stays 16-byte aligned.
    float sun_soft = 0.35f;
    float guard_amount = 0.5f; // Step 2: soft-knee gpu_total/raw_light overshoot guard, [0,1],
                               // 0=off. Occupies a former pad slot.
    // Validity sentinel for the directional lighting layer (Stage 1, gpu-daylight
    // black-scene plan): 1.0 when sky_sun_pass is ready AND has dispatched at
    // least once this run, 0.0 otherwise ("no data yet" vs a genuinely dark
    // buffer — see sprite.frag.hlsl's magenta debug tint and
    // assemble_light_inputs in sdl_render_frame.cpp). Occupies a former pad
    // slot; the struct stays the same size.
    float sky_valid = 0.0f;
    float flicker_gain = 1.0f; // Step 5: fire/torch flicker master gain; 0=frozen. Was cloud_pad1.
};
static_assert(sizeof(debug_params) == 272, "debug_params wire-stable with DebugParams cbuffer");

} // namespace lighting

#endif // CATA_SRC_LIGHTING_DEBUG_PARAMS_H
