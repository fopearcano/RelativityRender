#pragma once

#include "math/Vec3.h"

namespace rr::relativity {

// Observer state in the scene's rest frame.
//
// Velocity is in natural units (c = 1), so each component is a beta
// value in (-1, +1). The rest of the relativity math takes
// `velocity` directly; callers are responsible for ensuring
// |velocity| < 1 (use `clampBeta(...)` from RelativityMath.h).
//
// Position is intentionally not stored here - the camera owns
// position; the observer carries the kinematic state.
struct Observer {
    rr::math::Vec3 velocity = {0.0f, 0.0f, 0.0f};  // beta = v / c
};

// Knobs that control how the relativistic effects participate in
// rendering. Kept separate from `Observer` so artist-facing toggles
// don't pollute the physical state.
//
// `*_strength` values are dimensionless multipliers in [0, 1] (or
// beyond, at the artist's risk):
//   - 1.0 = the underlying formula is applied as-is.
//   - 0.0 = the effect is disabled entirely (identity).
//   - intermediate values lerp between identity and the full effect,
//     letting an artist dial subtle relativistic cues without lying
//     to the physics that generates them.
//
// `max_beta` caps |beta| just below 1 so gamma stays finite.
struct RelativityParams {
    bool  enable_aberration      = true;
    bool  enable_doppler         = true;
    bool  enable_searchlight     = true;

    float doppler_color_strength = 1.0f;
    float searchlight_strength   = 1.0f;
    float max_beta               = 0.999999f;
};

}
