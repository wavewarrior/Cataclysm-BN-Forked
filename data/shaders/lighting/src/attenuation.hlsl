// Shared point-light attenuation — smooth falloff with C1 continuity at radius boundary.
// Uses parabolic window: value AND derivative reach zero at dist=radius.
float point_light_atten(float dist, float radius, float falloff) {
    float t = saturate(dist / radius);
    float window = 1.0 - t * t;           // parabolic window, C1 at t=1
    float core   = 1.0 - pow(t, falloff); // artist-tunable inner shape
    return core * window;
}
