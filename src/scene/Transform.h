#pragma once

// Back-compat shim. The canonical Transform now lives in
// `src/math/Transform.h` so geometry / scene / camera / future
// animation systems can share it without crossing module boundaries.
// `rr::scene::Transform` is preserved as an alias.

#include "math/Transform.h"

namespace rr::scene {

using Transform = rr::math::Transform;

}
