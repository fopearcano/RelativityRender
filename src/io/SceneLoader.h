#pragma once

#include "scene/Scene.h"

#include <filesystem>
#include <string>

// `.rrscene` v1 loader.
//
// Populates an `rr::scene::Scene` from disk. All v1 sections in
// the format spec are now parsed:
//
//   - `render_settings` (width, height)
//   - `camera` (position, forward, up, fov)
//   - `relativity` (beta_velocity, velocity_direction,
//                   aberration_strength, doppler_strength,
//                   searchlight_strength)
//   - `materials` (id, name, base_color, emission_color,
//                  emission_strength, roughness)
//   - `spheres`   (position, radius, material_id)
//   - `lights`    (type = "point" | "directional",
//                  position | direction, color, intensity)
//   - `meshes`    (vertices, triangles, material_id, transform)
//
// `material_id` references on spheres and meshes are stored
// verbatim as the spec lookup key (the entry's `id`); the
// renderer's `GpuScene::upload_from` translates lookup keys
// into device-side array indices at upload time.
//
// Unknown top-level keys are not errors - per the spec the
// parser warns-and-ignores them so future versions can extend
// the schema without breaking v1 files.
//
// The header is host-only; no GPU dependencies. The CUDA
// renderer consumes the populated `rr::scene::Scene` through
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
