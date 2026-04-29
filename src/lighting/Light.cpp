#include "lighting/Light.h"

#include "math/MathUtils.h"
#include "math/Vec3.h"

namespace rr::lighting {

namespace {

// Normalize, falling back to a default direction for degenerate input.
// Used by the directional / area factories so the caller doesn't have
// to pre-normalize.
rr::math::Vec3 safe_normalize(rr::math::Vec3 v, rr::math::Vec3 fallback) {
    const float len = rr::math::length(v);
    if (len <= rr::math::kEpsilon) return fallback;
    return v * (1.0f / len);
}

}

Light make_point_light(rr::math::Vec3 position,
                       rr::math::Vec3 color,
                       float          intensity) {
    Light l;
    l.type      = LightType::Point;
    l.position  = position;
    l.color     = color;
    l.intensity = intensity;
    return l;
}

Light make_directional_light(rr::math::Vec3 direction,
                             rr::math::Vec3 color,
                             float          intensity) {
    Light l;
    l.type      = LightType::Directional;
    l.direction = safe_normalize(direction,
                                 rr::math::Vec3{0.0f, -1.0f, 0.0f});
    l.color     = color;
    l.intensity = intensity;
    return l;
}

Light make_area_light(rr::math::Vec3 position,
                      rr::math::Vec3 normal,
                      float          width,
                      float          height,
                      rr::math::Vec3 color,
                      float          intensity) {
    Light l;
    l.type        = LightType::Area;
    l.position    = position;
    l.direction   = safe_normalize(normal,
                                   rr::math::Vec3{0.0f, -1.0f, 0.0f});
    l.area_width  = width  > 0.0f ? width  : 0.0f;
    l.area_height = height > 0.0f ? height : 0.0f;
    l.color       = color;
    l.intensity   = intensity;
    return l;
}

Light make_environment_light(rr::math::Vec3 color, float intensity) {
    Light l;
    l.type      = LightType::Environment;
    l.color     = color;
    l.intensity = intensity;
    return l;
}

}
