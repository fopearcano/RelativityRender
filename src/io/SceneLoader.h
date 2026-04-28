#pragma once

#include "scene/Scene.h"

#include <filesystem>
#include <string>

// `.rrscene` v1 loader.
//
// Populates an `rr::scene::Scene` from disk. Only the v1 sections
// the M13 spec slice has stabilised so far are parsed:
//
//   - `render_settings` (width, height)
//   - `camera` (position, forward, up, fov)
//   - `relativity` (beta_velocity, velocity_direction,
//                   aberration_strength, doppler_strength,
//                   searchlight_strength)
//
// `materials`, `spheres`, `lights`, `meshes` are documented in the
// spec but **deliberately ignored** by this slice; they will be
// added in subsequent M13 parser sub-prompts. Unknown top-level
// keys (including the deferred sections) are not errors - per the
// spec the parser warns-and-ignores them.
//
// The header is host-only; no GPU dependencies. The CUDA renderer
// continues to consume the populated `rr::scene::Scene` through
// the existing upload path.

namespace rr::io {

struct LoadResult {
    bool             ok = false;
    rr::scene::Scene scene;        // populated only when ok == true
    std::string      message;      // human-readable description; empty on success
};

// Read the file at `path`, parse it as v1 `.rrscene` JSON, and
// return the populated scene. Returns `ok = false` with a
// descriptive `message` on any IO / parse / validation failure.
LoadResult load_rrscene(const std::filesystem::path& path);

}
