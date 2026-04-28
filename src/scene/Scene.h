#pragma once

#include "camera/Camera.h"
#include "geometry/Mesh.h"
#include "geometry/Sphere.h"
#include "lighting/Light.h"
#include "material/MaterialTypes.h"
#include "math/Vec3.h"
#include "relativity/RelativityParams.h"
#include "scene/SceneObject.h"
#include "texture/ImageTexture.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rr::scene {

// Per-render settings that are not part of the camera or the
// relativity model. Resolution lives here rather than on the camera
// because it depends on the output surface, not the optical
// configuration. Sample counts and depth are reserved for the path
// tracer (M14); for now they are stored faithfully but not consumed.
struct RenderSettings {
    int width        = 1280;
    int height       = 720;
    int samples_per_pixel = 1;
    int max_depth        = 1;     // 1 = primary rays only (current state).
};

// --- Scene-side wrappers around the host primitives ---------------
//
// Each entity carries a `SceneObject` for transform / name /
// visibility plus the type-specific payload. Materials are referenced
// by integer index into `Scene::materials` rather than by pointer so
// the data is trivially serialisable when the scene file format
// arrives (M13) and uploadable as-is to the device (M11).

struct SceneSphere {
    SceneObject          object;
    rr::geometry::Sphere geometry;          // center + radius (current convention: world space)
    int                  material_index = -1;
};

// Authoring-side mesh entry.
//
// Embeds the host `rr::geometry::Mesh` (vertices, triangles,
// `material_id` lookup key, local-to-world `transform`) for real
// scene data. `object.name` / `object.visible` carry the
// authoring metadata; the geometry inside `data` is what the GPU
// upload path consumes. `source_path` is reserved for future
// external-asset references; it is unused in v1.
struct SceneMesh {
    SceneObject        object;
    rr::geometry::Mesh data;
    std::string        source_path;
};

// Authoring-side material entry.
//
// The placeholder shape (just name + albedo) was rewritten in
// M13 parser slice 2 once a real consumer (the .rrscene loader)
// arrived. `id` is the lookup key used by `material_id`
// references on `SceneSphere` / `SceneMesh` and matches the
// spec's stable handle. `params` carries the full host
// `MaterialParams` POD so the GPU upload path can publish it
// without reshaping.
//
// `id == -1` means "the renderer's neutral default" - the same
// fallback that an unmatched lookup uses.
struct SceneMaterial {
    int                          id   = -1;
    std::string                  name;
    rr::material::MaterialParams params;
};

// Authoring-side light entry.
//
// Embeds the host `rr::lighting::Light` POD (type-discriminated
// fields - position / direction / color / intensity / area
// extents) so the GPU upload path can publish it without
// reshaping. `object.name` / `object.visible` carry authoring
// metadata; toggling visibility on a `SceneLight` is the host-
// side equivalent of dropping it from the array at upload time.
struct SceneLight {
    SceneObject         object;
    rr::lighting::Light data;
};

// Authoring-side scene container. Owns the camera, render settings,
// observer state, relativity knobs, and lists of every entity type
// in the scene. Indices into the lists are stable for the lifetime
// of the scene; clients that hold an index across edits are
// responsible for invalidating their references.
//
// This is plain data with one helper. Renderer integration (kernel
// upload paths, AOV bookkeeping, scene-file IO) lives in its own
// modules and consumes `Scene` as input - `Scene` itself does not
// know about the GPU or the file format.
struct Scene {
    rr::camera::Camera                 camera;
    RenderSettings                     render_settings;
    rr::relativity::Observer           observer;
    rr::relativity::RelativityParams   relativity;

    std::vector<SceneSphere>           spheres;
    std::vector<SceneMesh>             meshes;
    std::vector<SceneMaterial>         materials;
    std::vector<SceneLight>            lights;

    // M16: image textures the materials reference by index. A
    // material with `params.base_color_texture_id == i` samples
    // `textures[i]` for its diffuse albedo. Empty by default; the
    // .rrscene format does not yet carry textures (a future schema
    // version will), so today the renderer / main builds them
    // programmatically.
    std::vector<rr::texture::ImageTexture> textures;

    // Reset the scene to its default-constructed state. Cheaper than
    // building a fresh `Scene` object when the same instance is
    // reused across renders.
    void clear();
};

}
