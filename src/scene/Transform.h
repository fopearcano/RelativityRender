#pragma once

// `rr::scene::Transform` is a thin alias for `rr::math::Transform`.
//
// The canonical Transform lives in `src/math/Transform.h` so geometry,
// scene, camera, and (later) animation systems can share it without
// crossing module boundaries. The prior reuse audit
// (`PROTOTYPE_REUSE_AUDIT.md`) flagged this scene-side alias as
// DELETE_LATER, but it stays in the rewrite as long as the user-facing
// scene API names a `Transform` from inside the scene namespace.
//
// New code under `src/scene/` is welcome to use `rr::math::Transform`
// directly; this alias exists to keep `SceneObject` legible as
// `name + Transform + visible`.

#include "math/Transform.h"

namespace rr::scene {

using Transform = rr::math::Transform;

}
