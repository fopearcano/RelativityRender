#pragma once

#include "scene/Scene.h"

#include <filesystem>
#include <string>

// `.rrscene` v1 writer.
//
// Inverse of `rr::io::load_rrscene`. Serialises a host
// `rr::scene::Scene` to a v1 JSON file. Round-trips with the
// loader for the v1 sections both currently understand
// (render_settings, camera, relativity, materials, spheres,
// lights, meshes); fields that v1 doesn't expose
// (`metallic`, `specular`, `transmission` on materials; per-vertex
// normals / UVs on meshes; `area` / `environment` lights) are
// silently dropped during serialisation - they live on the host
// PODs but are not part of the v1 schema.
//
// The writer is intentionally simple: human-readable indentation,
// no canonicalisation, no schema-aware diffing. Authoring tools
// that need stable round-tripping should always treat a write
// followed by a read as the canonical form.
//
// Host-only; no GPU dependencies.

namespace rr::io {

struct WriteResult {
    bool        ok = false;
    std::string message;  // human-readable; empty on success
};

// Write `scene` to `path` as a v1 `.rrscene` JSON file. Creates
// the parent directory if it does not exist. Returns
// `ok = false` with a descriptive `message` on IO failure.
WriteResult save_rrscene(const rr::scene::Scene& scene,
                         const std::filesystem::path& path);

}
