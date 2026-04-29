#pragma once

#include "math/MathUtils.h"  // RR_HD
#include "math/Vec3.h"

#include <cstdint>

namespace rr::lighting {

// Discriminator for the unified `Light` POD.
//
// The renderer uploads a single `GpuBuffer<Light>` array; the kernel
// switches on `Light::type` to interpret the type-specific fields.
// Adding a new light kind = adding a new enumerator + a new branch in
// the eventual sampling / eval / pdf helpers.
enum class LightType : std::int32_t {
    Point       = 0,
    Directional = 1,

    // PLACEHOLDER. The geometry slot for area sampling is reserved
    // (`area_width` / `area_height` on the POD) so the upload buffer
    // layout is forward-compatible. The real area-light sampling
    // routine lands with the path tracer (M14) and the texture /
    // shading systems (M16).
    Area        = 2,

    // PLACEHOLDER. Today this is a single colour + intensity that
    // acts as a flat sky tint. Environment maps (HDR images, IBL
    // sampling) join with the texture system (M16) and the path
    // tracer (M14).
    Environment = 3,
};

// Plain-data light POD. Host- and device-readable. Field meanings
// depend on `type`:
//
//   Point        : `position` is the world-space light location.
//                  `direction` and `area_*` are unused.
//   Directional  : `direction` is the direction photons propagate
//                  (a unit vector). The shader uses `-direction` as
//                  the "to-light" vector. `position` and `area_*`
//                  are unused.
//   Area         : PLACEHOLDER. `position` + `direction` give the
//                  anchor + surface normal; `area_width` /
//                  `area_height` are the rectangle extents in the
//                  light's local frame. Sampling routines arrive at
//                  M14.
//   Environment  : PLACEHOLDER. `color * intensity` is a flat sky
//                  tint. `direction`, `position`, `area_*` are
//                  unused until env-map textures land at M16.
//
// The struct is a flat layout (no union) so `GpuBuffer<Light>` can
// upload it with `std::is_trivially_copyable`. Defaults describe a
// neutral point light at the origin with white colour and unit
// intensity.
struct Light {
    LightType      type        = LightType::Point;

    rr::math::Vec3 color       = rr::math::Vec3{1.0f, 1.0f, 1.0f};
    float          intensity   = 1.0f;

    rr::math::Vec3 position    = rr::math::Vec3{0.0f, 0.0f, 0.0f};
    rr::math::Vec3 direction   = rr::math::Vec3{0.0f, -1.0f, 0.0f};

    float          area_width  = 0.0f;
    float          area_height = 0.0f;
};

// Convenience factories. Each returns a `Light` with `type` set and
// the type-specific fields populated. Direction-bearing factories
// normalize their input; degenerate (zero-length) directions fall
// back to a sensible default.

[[nodiscard]] Light make_point_light(rr::math::Vec3 position,
                                     rr::math::Vec3 color,
                                     float          intensity);

[[nodiscard]] Light make_directional_light(rr::math::Vec3 direction,
                                           rr::math::Vec3 color,
                                           float          intensity);

[[nodiscard]] Light make_area_light(rr::math::Vec3 position,
                                    rr::math::Vec3 normal,
                                    float          width,
                                    float          height,
                                    rr::math::Vec3 color,
                                    float          intensity);

[[nodiscard]] Light make_environment_light(rr::math::Vec3 color,
                                           float          intensity);

}
