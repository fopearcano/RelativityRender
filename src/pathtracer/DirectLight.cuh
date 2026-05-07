#pragma once

// NEE.2 skeleton — host/device-shared direct-light sampler.
//
// Picks one light uniformly from the scene's `Light` array
// and evaluates its unattenuated contribution at a hit
// vertex. Returns a `DirectLightSample` POD (defined in
// `pathtracer/DirectLight.h`) that the kernel multiplies
// by a shadow-ray visibility, the BRDF, the cosine to the
// surface normal, and the running throughput.
//
// This file mirrors the `pathtracer/RNG.{h,cuh}` and
// `pathtracer/Sampling.{h,cuh}` split: the `.h` carries
// the data type (`DirectLightSample`); the `.cuh`
// re-exports the host-friendly helper for CUDA TUs to
// include alongside `<cuda_runtime.h>`. Despite the
// `.cuh` extension the helper is `RR_HD inline` so host
// tests exercise the same code the device runs — the
// extension is convention, not necessity.
//
// NEE.2 (the current slice) integrates this helper into
// `k_pathtrace_sample` behind a default-off `enable_nee`
// guard. Until a future slice adds a CLI flag flipping
// the gate, no caller passes `enable_nee == true`; the
// helper compiles and links but is not executed at
// runtime, so the per-pixel output of every existing
// fixture / golden image is byte-identical with the
// pre-NEE build.
//
// NEE.3 (next CUDA slice in this sub-arc): adds a CLI
// flag (`--enable-nee`) and the host-side wiring that
// flows it into `PathTraceConfig::enable_nee`. NEE.4
// adds the OptiX-side mirror; until then mixing
// `enable_nee == true` with the OptiX pathtrace
// dispatcher silently produces an emission-only render
// from OptiX, while the CUDA backend produces a NEE
// render. The two backends DO NOT converge to the same
// image until the OptiX slice lands.

#include "lighting/Light.h"
#include "math/MathUtils.h"     // RR_HD
#include "math/Vec3.h"
#include "pathtracer/DirectLight.h"

namespace rr::pathtracer {

// Shadow-ray epsilon along the sample direction. The receiver
// end is offset by this much along the surface normal (the
// kernel does that, not this helper); the light end (Point-
// light case) is reached by subtracting this from the
// `distance` returned by the helper. The same epsilon is
// reused on both ends to keep the shadow-ray contract
// symmetric — a future cross-backend convergence check
// (NEE.4 slice) compares against the existing OptiX
// `__closesthit__ shading_mode == 2` direct-lighting
// branch, which uses an analogous epsilon.
inline constexpr float kShadowEps = 1.0e-3f;

// Shadow-ray `t_max` for Directional lights. Directional
// sources are at infinity in physics; in IEEE-754 single
// precision we pick a finite sentinel that is large
// enough to reach beyond any reasonable scene bound but
// small enough to avoid `inf` propagation in downstream
// arithmetic. The value matches `closest_hit`'s `t_max`
// sentinel in `CudaPathTracer.cu` so a Directional shadow
// ray walks every scene primitive without prematurely
// rejecting hits past a smaller cap.
inline constexpr float kDirectionalShadowTMax = 1.0e30f;

// Pick one light uniformly from `lights[0..count)` using
// the [0, 1) random `u_select`, then evaluate its
// unattenuated contribution at `hit_position` with surface
// `normal`. Returns a default-constructed (zero-
// contribution) sample when:
//
//   * `lights == nullptr` or `count <= 0` (no lights to
//     sample),
//   * the picked light is a PLACEHOLDER type
//     (`LightType::Area`, `LightType::Environment` —
//     `Light.h:20-31` notes them as deferred to a future
//     slice that introduces real area / IBL sampling),
//   * the light is degenerate (zero `direction` for a
//     Directional light, coincident position for a
//     Point light),
//   * the cosine to the sample direction is non-positive
//     (the light is behind the receiver — sampling it
//     contributes zero radiance through a cosine-weighted
//     BRDF).
//
// When the sample is valid, `pdf_inv == count` (the
// inverse uniform-by-count selection PDF); when zero, the
// caller naturally multiplies by 0 and the sample
// contributes nothing.
//
// The helper is scene-AGNOSTIC by construction — it does
// not consult the scene geometry. Visibility / shadow-ray
// tracing is the caller's responsibility (the kernel that
// owns the `CudaSceneView` runs the any-hit walk, then
// multiplies the helper's contribution by the binary
// visibility). This separation keeps the helper RR_HD
// inline-able and host-testable without pulling in
// `<cuda_runtime.h>`.
//
// BRDF / throughput / cosine modulation also lives in the
// caller. The expected per-vertex update at a Lambert
// surface is:
//
//     const float cos_th = max(0, dot(normal, sample.wi));
//     const Vec3  brdf   = baseColor * (1.0f / kPi);  // Lambert
//     const float vis    = trace_shadow_ray(...);
//     radiance += throughput * brdf * sample.li_unattenuated
//                 * (cos_th * vis * sample.pdf_inv);
//
// (componentwise on the Vec3 multiplications).
RR_HD inline DirectLightSample sample_direct_light_uniform(
        const rr::lighting::Light* lights,
        int                        count,
        rr::math::Vec3             hit_position,
        rr::math::Vec3             normal,
        float                      u_select) {
    DirectLightSample s;
    if (lights == nullptr || count <= 0) {
        return s;
    }

    // Map u_select in [0, 1) to integer index in [0, count).
    // Clamp the rare `u_select == 1.0f` case (RNG-floor
    // pathology in client code) back into range to avoid a
    // one-past-the-end read; `next_float` in `RNG.h` is
    // strictly < 1, but defence-in-depth is cheap.
    int li = static_cast<int>(u_select * static_cast<float>(count));
    if (li < 0)      li = 0;
    if (li >= count) li = count - 1;

    const rr::lighting::Light L = lights[li];

    using rr::math::Vec3;
    using rr::math::dot;

    if (L.type == rr::lighting::LightType::Point) {
        const Vec3  to_light = L.position - hit_position;
        const float r2       = dot(to_light, to_light);
        if (r2 <= 0.0f) {
            // Coincident light & receiver — zero
            // contribution, not a divide-by-zero.
            return s;
        }
        const float r       = sqrtf(r2);
        const Vec3  wi      = to_light * (1.0f / r);
        const float cos_th  = dot(normal, wi);
        if (cos_th <= 0.0f) {
            // Light is behind the receiver. Zero
            // contribution; pdf_inv stays at 0 so the
            // caller multiplies by 0.
            return s;
        }
        s.wi              = wi;
        s.distance        = r;
        const float inv_r2 = 1.0f / r2;
        s.li_unattenuated = Vec3{L.color.x * (L.intensity * inv_r2),
                                 L.color.y * (L.intensity * inv_r2),
                                 L.color.z * (L.intensity * inv_r2)};
        s.pdf_inv         = static_cast<float>(count);
        return s;
    }

    if (L.type == rr::lighting::LightType::Directional) {
        // The "to-light" direction is the negation of the
        // light's photon-propagation direction (matches the
        // existing OptiX `__closesthit__ shading_mode == 2`
        // direct-lighting convention). `make_directional_light`
        // normalizes `direction` at factory time, but
        // renormalizing here makes the helper robust against
        // host-side construction shortcuts that bypass the
        // factory.
        const Vec3  d_in = L.direction;
        const float dl   = dot(d_in, d_in);
        if (dl <= 0.0f) {
            return s;
        }
        const Vec3  wi     = d_in * (-1.0f / sqrtf(dl));
        const float cos_th = dot(normal, wi);
        if (cos_th <= 0.0f) {
            return s;
        }
        s.wi              = wi;
        s.distance        = kDirectionalShadowTMax;
        s.li_unattenuated = Vec3{L.color.x * L.intensity,
                                 L.color.y * L.intensity,
                                 L.color.z * L.intensity};
        s.pdf_inv         = static_cast<float>(count);
        return s;
    }

    // Area + Environment are PLACEHOLDER light types per
    // `Light.h:20-31`. NEE.2 silently returns the default
    // zero-contribution sample for them. The slice that
    // unblocks Area + Environment also introduces MIS to
    // handle the double-count window between the existing
    // emission-add and the new NEE term (Area lights have
    // a mesh, so the same surface can be reached via both
    // the random bounce and the explicit NEE sample —
    // unlike Point + Directional which have no mesh).
    return s;
}

}  // namespace rr::pathtracer
