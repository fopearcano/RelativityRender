#pragma once

// Orthonormal observer frame at a chart event (see
// `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.3). Carries the
// observer's spatial tetrad (right / up / forward) plus the
// three-velocity `beta = v / c` along the observer's worldline.
// The four-velocity is recovered as
// `u^mu = gamma * (1, beta_x, beta_y, beta_z)`; the renderer
// computes `gamma` on demand via the existing
// `rr::relativity::gamma` helper.
//
// The Identity chart's specialisation of this frame is exactly the
// existing special-relativistic observer in `src/relativity/`:
// `velocity` plays the role of `Observer::velocity`, and the spatial
// tetrad columns play the role of the camera's `right` / `up` /
// `forward` basis. The architecture doc §7.2 records that the
// existing `aberrateDirection` / `dopplerFactor` /
// `searchlightFactor` leaf becomes the Minkowski +
// constant-velocity-frame specialisation of the observer-frame
// contract. This slice does NOT touch `src/relativity/`; the
// subsumption is purely structural until the chart-aware-seam slice
// wires it in.

#include "math/MathUtils.h"
#include "math/Vec3.h"

namespace rr::manifold {

struct ObserverFrame {
    // Three-velocity in natural units (c = 1). Each component is a
    // beta in (-1, +1); callers cap |beta| with the existing
    // `rr::relativity::clampBeta`. Default is the scene-rest
    // observer.
    rr::math::Vec3 velocity = {0.0f, 0.0f, 0.0f};

    // Spatial tetrad columns in chart coordinates. Defaults form the
    // right-handed world basis the existing pinhole camera produces.
    rr::math::Vec3 right    = {1.0f, 0.0f, 0.0f};
    rr::math::Vec3 up       = {0.0f, 1.0f, 0.0f};
    rr::math::Vec3 forward  = {0.0f, 0.0f, 1.0f};
};

// Returns the scene-rest observer frame on the Identity chart.
// `velocity = 0` and the tetrad is the world basis; this is the
// only frame the Manifold Core implements concretely today.
RR_HD inline ObserverFrame rest_frame() {
    return ObserverFrame{};
}

}
