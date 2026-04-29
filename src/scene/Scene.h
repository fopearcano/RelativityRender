#pragma once

#include "camera/Camera.h"
#include "geometry/Sphere.h"
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
// Stage 6A scaffolding: only the authoring metadata is present.
// `source_path` is reserved for external-asset references (e.g. an
// .obj path) once the loader lands. The geometry payload
// (vertices / triangles / per-mesh transform / material id) joins
// at master module 12, which introduces `rr::geometry::Mesh`.
struct SceneMesh {
    SceneObject object;
    std::string source_path;
};

// Material entry.
//
// Stage 6A scaffolding: only the lookup id and a human-readable
// name are present. The shading payload (`MaterialParams` -
// albedo / emission / roughness / ...) joins at master module 13,
// which introduces `rr::material::MaterialParams`.
//
// `id == -1` is reserved for "the renderer's neutral default" -
// the same fallback an unmatched lookup uses.
struct SceneMaterial {
    int         id = -1;
    std::string name;
};

// Light entry.
//
// Stage 6A scaffolding: only the authoring metadata is present.
// The light type + parameters (point / directional / area /
// environment, intensity, colour, extents) join at master module 14,
// which introduces `rr::lighting::Light`.
struct SceneLight {
    SceneObject object;
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
