// Shared soft-shadow sphere trace for every SDF-lit path: point emitters, GI
// probes and volumetrics.
//
// ONE definition on purpose. This file exists because three near-identical
// copies of the trace drifted apart, and the divergence was invisible until
// someone compared them: sprite.frag and gi_field kept one penumbra reference
// while vol.frag was corrected to another. Deduplicating is the actual fix for
// that class of bug.
//
// The including file MUST already define
//     float sdf_bilinear(float2 p);   // signed distance in TILE units
// and declare `sdf_map_w`, so this include belongs AFTER those, not at the top.
//
// Technique: Inigo Quilez's cone-ratio soft shadow
// (https://iquilezles.org/articles/rmshadows/) plus Sebastian Aaltonen's
// between-samples correction (Claybook, GDC 2018; reference implementation at
// https://www.shadertoy.com/view/lsKcDD).
//
// ---------------------------------------------------------------------------
// THE CORRECTION, and why it is what fixes smeared corners.
//
// The raw sample `sd` is the distance at the sample POINT, but the ray's
// closest approach to an occluder almost always falls BETWEEN two samples.
// Feeding `sd` straight into the ratio overestimates the miss on one side of
// that closest approach and underestimates it on the other. That shows up as
// banding along a penumbra, and around a sharp corner as a smeared wedge of
// half shadow - a corner is exactly where sample spacing is coarsest relative
// to the feature, so it is where the raw estimate is worst. Aaltonen's fix
// triangulates the previous and current samples into the real perpendicular
// distance `d` and the point `t - y` along the ray where it occurs, for one
// sqrt and one divide. It is a strict refinement of the estimate: d <= sd, and
// it is applied only where the triangulation is geometrically valid.
// ---------------------------------------------------------------------------
// THE PENUMBRA REFERENCE, and a known deviation kept on purpose.
//
// `ref_receiver` selects which distance the cone ratio is keyed to:
//     true  - `t - y`, distance from the RECEIVER. The textbook IQ form.
//     false - the remaining distance to the light. The legacy reference that
//             sprite.frag and gi_field historically used for point emitters.
//
// Those are NOT equivalent, and the textbook form is the defensible one: the
// penumbra ratio is (miss distance) / (radius of the light cone at the miss),
// and that cone is the one the light subtends AS SEEN FROM THE RECEIVER, so its
// radius grows with t. Keying to the remaining distance inverts which end of
// the ray is sharp.
//
// It is deliberately NOT switched here, because switching it was measured in
// game and is not a drop-in: this engine's shadow_k, emitter radii and GI were
// all tuned around the current reference. Straight substitution cost a flat
// ~25% of luma on ALL lit ground (new/old ratio 0.73-0.80 in every luma bin - a
// smooth global dimming, not a shadow), because a constant k makes the cone
// half-width t/k grow without bound, so at 30 tiles anything passing within
// ~3.7 tiles of a wall is dimmed and in a town that is nearly every long ray.
// Compensating with a distance-scaled k (a fixed physical light radius, the
// physically correct point-light model) removed the dimming but flattened scene
// contrast instead - darks 1.6 -> 14.8, brights 85.5 -> 31.0, verified against a
// cross-launch null whose bin ratios were all 1.000, so that collapse is real
// and not regression to the mean. Making the switch properly means re-tuning
// shadow_k and the emitter radii together, which is an art-direction pass, not
// a shader edit.
// ---------------------------------------------------------------------------
//
// `dist_to_light` is the march length: a true distance for point emitters, a
// fixed reach for directional/volumetric use.
//
// `self_eps` > 0 enables the self-shadow escape. Pass 0 to disable it; that is
// an exact no-op, because the guard cannot fire when self_eps is 0.
float soft_shadow_march(float2 origin, float2 dir, float dist_to_light,
                        float k, int steps, float self_eps, bool ref_receiver) {
    if(sdf_map_w == 0u || steps <= 0) {
        return 1.0;
    }
    float shadow = 1.0;
    float t = min(0.3, dist_to_light * 0.5);
    // Self-shadow guard. A tall sprite is lit from its BASE tile, and that tile
    // is itself the occluder (the wall or tree the shadow is cast from), so a
    // naive march hits it at t~0 and reports the lit TOP as fully shadowed.
    // Step out of the occluder body first, and stop the instant we reach open
    // air so the NEXT occluder still shadows normally - a tree inside a
    // building marches out of the tree, hits the wall, and stays dark.
    if(self_eps > 0.0 && sdf_bilinear(origin) < self_eps) {
        [loop] for(int ss = 0; ss < steps; ++ss) {
            if(t >= dist_to_light - 0.4) { return 1.0; }  // never left it -> lit top
            if(sdf_bilinear(origin + dir * t) >= self_eps) { break; }
            t += 0.15;
        }
    }
    // Previous sample distance, and the step actually taken to reach the
    // current one. Seeded huge so the first iteration produces y = 0 and
    // reduces exactly to the uncorrected form - IQ's documented way of avoiding
    // a first-step artefact.
    float prev_sd   = 1e10;
    float prev_step = 1e10;
    [loop] for(int ss = 0; ss < steps; ++ss) {
        if(t >= dist_to_light - 0.4) { break; }
        const float sd = sdf_bilinear(origin + dir * t);
        if(sd < 0.05) { shadow = 0.0; break; }
        // How far BACK from the current sample the closest approach sits.
        // General form, because the 0.15 floor below means the march does NOT
        // always advance by exactly prev_sd:
        //     y = (s^2 + sd^2 - prev_sd^2) / 2s
        // With s == prev_sd this collapses to Aaltonen's published
        // sd*sd/(2*prev_sd).
        float y = (prev_step * prev_step + sd * sd - prev_sd * prev_sd)
                  / (2.0 * prev_step);
        float d;
        // A true SDF is 1-Lipschitz, which keeps y inside [0, sd] and the
        // triangle valid. This field is bilinearly filtered and the march
        // enforces a minimum step, so both can be violated slightly; outside
        // that range the triangulation is meaningless and collapsing d to ~0
        // would paint a spurious black pixel. Fall back to the plain
        // single-sample estimate there, which is exactly the old behaviour.
        if(y > 0.0 && y < sd) {
            d = sqrt(sd * sd - y * y);
        } else {
            y = 0.0;
            d = sd;
        }
        // Evaluate the ratio AT the closest approach (t - y), not at the sample.
        const float t_hit = t - y;
        const float denom = ref_receiver ? max(t_hit, 0.01)
                                         : max(dist_to_light - t_hit, 0.01);
        shadow = min(shadow, k * d / denom);
        prev_sd   = sd;
        prev_step = max(sd, 0.15);
        t += prev_step;
    }
    return saturate(shadow);
}
