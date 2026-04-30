#pragma once

#include "camera/Camera.h"
#include "geometry/Mesh.h"
#include "geometry/Sphere.h"
#include "lighting/Light.h"
#include "material/MaterialTypes.h"
#include "relativity/RelativityParams.h"
#include "scene/RenderSettings.h"
#include "scene/SceneObject.h"
#include "scene/Transform.h"

#include <string>
#include <vector>

namespace rr::scene {

// Authoring-side scene container. Owns the camera, render settings,
// observer state, relativity knobs, and lists of every entity type
// in the scene. Indices into the lists are stable for the lifetime
// of the scene; clients that hold an index across edits are
// responsible for invalidating their references.
//
// `Scene` is plain data with one helper. Renderer integration (GPU
// upload paths, scene-file IO) lives in its own modules and consumes
// `Scene` as input - `Scene` itself does not know about the GPU or
// the file format.
//
// Stage 6A surface: spheres are real (the kernel already consumes
// `rr::geometry::Sphere`); meshes / materials / lights ship as
// authoring-metadata-only placeholders so the container shape is
// stable across the next few stages without claiming functionality
// that does not exist. Each placeholder type documents which master-
// order module fills in its type-specific payload.

// --- Real entry: sphere primitive ---------------------------------

// Authoring-side wrapper around a single sphere primitive. The
// authoring metadata (name / Transform / visibility) lives on
// `object`; the renderer-facing geometry (centre + radius +
// material index) lives on `geometry`. Sphere's `material_index`
// is `-1` until the materials stage (master module 13) assigns it.
struct SceneSphere {
    SceneObject          object;
    rr::geometry::Sphere geometry;
};

// --- Placeholder entries (filled in at later master-order modules) -

// Mesh entry.
//
// Stage 6A scaffolding shipped this as a placeholder shell
// (`{object, source_path}`). Stage 10B.8 promotes it to a real
// authoring entry that carries the `rr::geometry::Mesh` payload
// alongside the SceneObject metadata, mirroring the
// `SceneMaterial` / `SceneLight` pattern. The kernel-side mesh
// upload (`GpuScene::upload_mesh`) still consumes
// `rr::geometry::Mesh` directly; the parser writes into
// `geometry` and a future stage threads it through the upload
// path. `source_path` remains for the eventual external-asset
// loader; v1 parsers treat it as informational metadata.
struct SceneMesh {
    SceneObject         object;
    std::string         source_path;
    rr::geometry::Mesh  geometry;
};

// Material entry.
//
// Stage 8A promotes this from a placeholder shell (`{id, name}`) to
// a real authoring entry that carries the device-friendly
// `MaterialParams` POD. Sphere `material_index` and Mesh
// `material_id` reference into the scene's `materials` array; the
// kernel reads `materials[Hit::material_index]` once a material
// upload lands (Stage 8B).
//
// `id == -1` is reserved for "the renderer's neutral default" -
// the same fallback an unmatched lookup uses.
struct SceneMaterial {
    int                          id = -1;
    std::string                  name;
    rr::material::MaterialParams params;
};

// Light entry.
//
// Stage 9A promotes this from a placeholder shell to a real
// authoring entry that carries the device-friendly
// `rr::lighting::Light` POD. The kernel reads light arrays
// via `GpuScene::upload_lights` once that lands (Stage 9B).
struct SceneLight {
    SceneObject         object;
    rr::lighting::Light data;
};

// --- The scene container ------------------------------------------

struct Scene {
    rr::camera::Camera                camera;
    RenderSettings                    render_settings;
    rr::relativity::Observer          observer;     // 3-velocity in c-units
    rr::relativity::RelativityParams  relativity;   // artist-facing toggles + max_beta

    std::vector<SceneSphere>          spheres;

    // Placeholder lists (see comments on the corresponding types
    // above). Empty by default. Populated as later master-order
    // modules ship the matching primitive / shading / lighting
    // pieces.
    std::vector<SceneMesh>            meshes;
    std::vector<SceneMaterial>        materials;
    std::vector<SceneLight>           lights;

    // Reset the scene to its default-constructed state. Cheaper than
    // building a fresh `Scene` instance when the same object is
    // reused across renders.
    void clear();
};

}
