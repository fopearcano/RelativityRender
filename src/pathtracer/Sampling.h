#pragma once

#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "pathtracer/RNG.h"

#include <cmath>

// Hemisphere sampling primitives for the path tracer. Header-only
// and `RR_HD inline`, so the same code runs in host tests and
// CUDA kernels. No path-tracing integration yet; this is the
// foundation M14's BSDF sampling and direct-lighting routines
// will compose with.
//
// Convention: every "_local" helper returns a unit vector in a
// tangent-space frame where the surface normal is `+Z`. The
// world-space wrappers transform the local sample into the
// shading frame defined by the supplied normal.

namespace rr::pathtracer {

// Branchless orthonormal basis from a unit normal.
// Frisvad / Duff "Building an orthonormal basis, revisited".
// Stable for the n.z = -1 antipode that the older Frisvad form
// breaks at.
RR_HD inline void build_orthonormal_basis(rr::math::Vec3 n,
                                          rr::math::Vec3& t,
                                          rr::math::Vec3& b) {
    const float sign = n.z >= 0.0f ? 1.0f : -1.0f;
    const float a    = -1.0f / (sign + n.z);
    const float c    = n.x * n.y * a;
    t = rr::math::Vec3{1.0f + sign * n.x * n.x * a,  sign * c,                 -sign * n.x};
    b = rr::math::Vec3{c,                            sign + n.y * n.y * a,     -n.y};
}

// --- Local-frame samples (returned in the +Z hemisphere) -------

// Uniform hemisphere sample. PDF = 1 / (2 * pi).
RR_HD inline rr::math::Vec3 sample_hemisphere_uniform_local(float u1, float u2) {
    const float z   = u1;                          // cos(theta) uniform in [0, 1)
    const float r   = sqrtf(1.0f - z * z);
    const float phi = 2.0f * rr::math::kPi * u2;
    return rr::math::Vec3{r * cosf(phi), r * sinf(phi), z};
}

// Cosine-weighted hemisphere sample (Lambertian importance).
// PDF = cos(theta) / pi.
RR_HD inline rr::math::Vec3 sample_hemisphere_cosine_local(float u1, float u2) {
    const float r2  = u1;                          // r^2 uniform in [0, 1)
    const float r   = sqrtf(r2);
    const float phi = 2.0f * rr::math::kPi * u2;
    const float x   = r * cosf(phi);
    const float y   = r * sinf(phi);
    const float z   = sqrtf(1.0f - r2);            // = sqrt(max(0, 1 - x^2 - y^2))
    return rr::math::Vec3{x, y, z};
}

// --- World-frame wrappers --------------------------------------

// `normal` must be unit length.
RR_HD inline rr::math::Vec3 sample_hemisphere_uniform(rr::math::Vec3 normal,
                                                     float u1, float u2) {
    rr::math::Vec3 t, b;
    build_orthonormal_basis(normal, t, b);
    const auto local = sample_hemisphere_uniform_local(u1, u2);
    return t * local.x + b * local.y + normal * local.z;
}

RR_HD inline rr::math::Vec3 sample_hemisphere_cosine(rr::math::Vec3 normal,
                                                    float u1, float u2) {
    rr::math::Vec3 t, b;
    build_orthonormal_basis(normal, t, b);
    const auto local = sample_hemisphere_cosine_local(u1, u2);
    return t * local.x + b * local.y + normal * local.z;
}

// --- PDFs -------------------------------------------------------

RR_HD inline float pdf_hemisphere_uniform() {
    return 1.0f / (2.0f * rr::math::kPi);
}

RR_HD inline float pdf_hemisphere_cosine(float cos_theta) {
    if (cos_theta < 0.0f) cos_theta = 0.0f;
    return cos_theta / rr::math::kPi;
}

// --- RNG-driven convenience wrappers ---------------------------
//
// Path-tracer kernels typically have an `RNG` per thread; these
// wrappers pull two floats from it and produce the world-space
// sample directly.

RR_HD inline rr::math::Vec3 sample_hemisphere_uniform(rr::math::Vec3 normal,
                                                     RNG& rng) {
    const float u1 = next_float(rng);
    const float u2 = next_float(rng);
    return sample_hemisphere_uniform(normal, u1, u2);
}

RR_HD inline rr::math::Vec3 sample_hemisphere_cosine(rr::math::Vec3 normal,
                                                    RNG& rng) {
    const float u1 = next_float(rng);
    const float u2 = next_float(rng);
    return sample_hemisphere_cosine(normal, u1, u2);
}

}
