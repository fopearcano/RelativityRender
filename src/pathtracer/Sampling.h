#pragma once

#include "math/MathUtils.h"  // RR_HD + kPi + kTwoPi + max
#include "math/Vec2.h"
#include "math/Vec3.h"

#include <cmath>

namespace rr::pathtracer {

// Sampling primitives for the GPU path tracer. Stage 11A surface:
// uniform-hemisphere and cosine-weighted-hemisphere sampling +
// their PDFs. Every entry point is RR_HD inline so the same code
// runs on host (tests) and device (kernels).
//
// Local-frame convention: returned directions live in the
// "tangent" frame where +Z is the surface normal. The path tracer
// (master module 16) is responsible for rotating these into world
// space against the hit's normal/tangent basis. Keeping the math
// in tangent space here means the sampling code is reusable for
// any orientation without re-deriving the formula.

namespace detail {
inline constexpr float kInv2Pi  = 1.0f / rr::math::kTwoPi;
inline constexpr float kQuarter = rr::math::kPi * 0.25f;
inline constexpr float kHalfPiC = rr::math::kPi * 0.5f;
}  // namespace detail

// Uniform hemisphere sample (PHYSICAL).
//   u  : two independent uniforms in [0, 1)^2 (e.g. from
//        `next_vec2(rng)`).
//   ret: unit-length direction in the +Z hemisphere of the local
//        frame. Distribution is uniform in solid angle.
//
// Math: cos(theta) = u.x, sin(theta) = sqrt(1 - cos^2),
//       phi = 2*pi*u.y. The PDF is constant at 1/(2*pi).
RR_HD inline rr::math::Vec3 sample_uniform_hemisphere(rr::math::Vec2 u) {
    const float cos_theta = u.x;
    const float sin_theta = std::sqrt(rr::math::max(0.0f,
                                                    1.0f - cos_theta * cos_theta));
    const float phi       = rr::math::kTwoPi * u.y;
    return rr::math::Vec3{sin_theta * std::cos(phi),
                          sin_theta * std::sin(phi),
                          cos_theta};
}

// PDF of `sample_uniform_hemisphere`. Constant; the parameter is
// reserved to mirror `pdf_cosine_hemisphere`'s signature so call
// sites can write the two PDF helpers symmetrically.
RR_HD inline float pdf_uniform_hemisphere() {
    return detail::kInv2Pi;
}

// Cosine-weighted hemisphere sample (PHYSICAL).
//   u  : two independent uniforms in [0, 1)^2.
//   ret: unit-length direction in the +Z hemisphere, distributed
//        proportional to cos(theta).
//
// Implemented via Malley's method: sample a point uniformly in
// the unit disk and project up onto the hemisphere. The disk
// sample uses concentric (Shirley) mapping, which preserves
// stratification better than the straight polar form and avoids
// the polar form's distortion near the disk centre.
RR_HD inline rr::math::Vec3 sample_cosine_hemisphere(rr::math::Vec2 u) {
    // Map [0, 1)^2 -> [-1, +1)^2.
    const float sx = 2.0f * u.x - 1.0f;
    const float sy = 2.0f * u.y - 1.0f;

    float r;
    float phi;
    if (sx == 0.0f && sy == 0.0f) {
        r   = 0.0f;
        phi = 0.0f;
    } else if (std::fabs(sx) > std::fabs(sy)) {
        r   = sx;
        phi = detail::kQuarter * (sy / sx);
    } else {
        r   = sy;
        phi = detail::kHalfPiC - detail::kQuarter * (sx / sy);
    }

    const float disk_x = r * std::cos(phi);
    const float disk_y = r * std::sin(phi);
    // Project to the hemisphere. cos(theta) = sqrt(1 - r^2) for
    // the projected disk point at radius r.
    const float z = std::sqrt(rr::math::max(0.0f,
                                            1.0f - disk_x * disk_x
                                                 - disk_y * disk_y));
    return rr::math::Vec3{disk_x, disk_y, z};
}

// PDF of `sample_cosine_hemisphere`. Equals cos(theta)/pi for
// directions in the +Z hemisphere; zero otherwise. The caller
// passes `cos_theta = direction.z` (the dot product against the
// local normal in tangent space).
RR_HD inline float pdf_cosine_hemisphere(float cos_theta) {
    if (cos_theta <= 0.0f) return 0.0f;
    return cos_theta * rr::math::kInvPi;
}

}  // namespace rr::pathtracer
