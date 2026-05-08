#pragma once

#include "math/MathUtils.h"  // RR_HD
#include "math/Vec3.h"

namespace rr::material {

// Plain-data PBR-style material parameter pack.
//
// Host- and device-readable. Used as both the host authoring form
// and the launch-argument form for the eventual GPU BSDF
// (`eval` / `sample` / `pdf`); a single shared POD is enough for
// the Stage 8 foundation. When the BSDF lands it will consume this
// struct on the device directly.
//
// Field names follow the common DCC / PBR convention
// (`baseColor`, `emissionColor`, `roughness`, `metallic`, ...) so
// artists and scene-file authors recognise them. The rest of the
// project uses snake_case; the material module is one of the two
// documented exceptions (the other being `rr::relativity`'s
// physics-literature naming).
//
// Defaults describe a neutral 80% grey diffuse surface with no
// emission and no transmission - a sensible "renderer default"
// the kernel falls back to when a `material_index` is out of range
// or `-1`.
struct MaterialParams {
    rr::math::Vec3 baseColor        = rr::math::Vec3{0.8f, 0.8f, 0.8f};
    rr::math::Vec3 emissionColor    = rr::math::Vec3{0.0f, 0.0f, 0.0f};

    float          emissionStrength = 0.0f;   // multiplier on emissionColor
    float          roughness        = 0.5f;   // 0 = mirror, 1 = fully rough
    float          metallic         = 0.0f;   // 0 = dielectric, 1 = conductor
    float          specular         = 0.5f;   // F0 scale for dielectrics, in [0, 1]

    // PLACEHOLDER. Reserved for the eventual transmission BSDF
    // (glass / refraction). Today only its slot is allocated; the
    // shader layers that consume it - dielectric Fresnel, IOR,
    // absorption - join with the path tracer and the texture /
    // shading systems. Keeping the field here means the upload
    // buffer layout is forward-compatible without ABI churn.
    float          transmission     = 0.0f;

    // Stage 13B.3 (master order #18) base-colour texture binding.
    // When `useBaseColorTexture` is true and `baseColorTextureId`
    // indexes into the scene-side texture table, the kernel samples
    // the texture at the hit point's UV and substitutes the result
    // for `baseColor`. Otherwise the flat `baseColor` value is
    // used. No mipmap, no wrap-mode metadata, no normal / metallic
    // / roughness texture slots - those join in subsequent
    // sub-stages.
    int            baseColorTextureId  = -1;     // index into scene.textures, -1 = none
    bool           useBaseColorTexture = false;  // gate; false uses flat baseColor
};

// PT-P.15: predicate that returns true iff a material's
// emission contribution is meaningfully non-zero. Used by the
// CUDA path tracer (`src/cuda/CudaPathTracer.cu`) to short-
// circuit the per-bounce emission add on the common (non-
// emissive) path; the branch is uniform per-warp because
// `MaterialParams` is shared by every pixel hitting the same
// surface, so no warp divergence is introduced. Returns false
// for the default-constructed `MaterialParams{}` (the
// renderer's fallback for unmaterialed primitives).
//
// The convention authored across `MaterialParams` is:
//   emitted_radiance = emissionColor * emissionStrength
// where `emissionColor` is the unit-spectrum colour and
// `emissionStrength` is its scalar multiplier. Both must be
// non-zero for the material to contribute light.
[[nodiscard]] RR_HD inline bool is_emissive(const MaterialParams& m) {
    return m.emissionStrength > 0.0f
        && (m.emissionColor.x + m.emissionColor.y + m.emissionColor.z) > 0.0f;
}

}
