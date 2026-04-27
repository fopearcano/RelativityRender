#pragma once

#include "math/MathUtils.h"  // RR_HD
#include "math/Vec3.h"

namespace rr::material {

// Plain-data PBR-style material parameter pack.
//
// Host- and device-readable. Used as both the host authoring form
// and the launch-argument form for the eventual GPU BSDF
// (`eval` / `sample` / `pdf`); a single shared POD is enough for
// the M11 foundation. When the BSDF lands it consumes this struct
// on the device directly.
//
// Field names follow the common DCC / PBR convention
// (`baseColor`, `emissionColor`, `roughness`, `metallic`, ...) so
// artists and scene-file authors recognise them. The rest of the
// project uses snake_case for field names; the material module is
// the one exception, mirroring the relativity module's
// physics-literature naming.
//
// Defaults describe a neutral 80% grey diffuse surface with no
// emission and no transmission - a sensible "placeholder" material
// the renderer can default to.
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
    // absorption - join with the path tracer (M14) and the texture /
    // shading systems (M16). Keeping the field here means the
    // upload buffer layout is forward-compatible without ABI churn.
    float          transmission     = 0.0f;
};

}
