#pragma once

#include "camera/Camera.h"
#include "field/ScalarField.h"
#include "geometry/Mesh.h"
#include "geometry/Sphere.h"
#include "lighting/Light.h"
#include "manifold/ManifoldMode.h"
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

    // SCHW.9 — per-scene Manifold Core mode authored in the
    // scene file's optional `manifold` block. Default
    // `ManifoldMode{}` is the disabled / Euclidean / strength-0
    // no-op anchor (matches the CLI default
    // `disabled_manifold_mode()` from MANI-I.1). When the scene
    // file authors any of the supported fields (`enabled`,
    // `chart`, `strength`, `debug_visualization`), the value is
    // recorded here; renderer dispatchers may merge with the
    // CLI-side `cfg.manifold` per main.cpp's policy (CLI wins
    // on explicit `--manifold-enable`; scene fills in when the
    // CLI default is in force).
    rr::manifold::ManifoldMode        manifold;

    // FIELD-I.13 — per-scene Scalar Field config authored in the
    // scene file's optional `scalar_field` block. Default
    // `ScalarFieldConfig{}` is the disabled / Constant /
    // strength-0 no-op anchor (matches the FIELD-I.2 default
    // `disabled_scalar_field_config()`). When the scene file
    // authors any of the supported fields (`enabled`,
    // `strength`, `kind`, `center`, `min_radius`,
    // `max_radius`, `falloff`, `min_value`, `max_value`,
    // `constant_value`), the value is recorded here. The
    // FIELD-I.* arc's renderer dispatchers (after the future
    // CLI bridge slice lands) will merge `scene.scalar_field_config`
    // with the CLI-side `cfg.scalar_field_config` per the same
    // policy `scene.manifold` ↔ `cfg.manifold` uses today (CLI
    // wins on explicit override; scene fills in otherwise).
    // Until that CLI bridge slice lands the field is parsed +
    // carried but no renderer dispatcher reads it — the
    // FIELD-I.6 task brief's "AOV only when requested" anchor
    // is preserved structurally because no consumer wires the
    // payload into any `AOVTargets::scalar_field_config` /
    // `OptixRenderer::render_aovs` trailing parameter this
    // slice.
    rr::field::ScalarFieldConfig      scalar_field_config;

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

// TEX-P.2 + TEX-P.5: validate every material's
// `useBaseColorTexture` / `baseColorTextureId` pair against the
// supplied texture-array count. Three cases — one for each
// possible state of the flag/id pair — are handled explicitly
// (mirrors `docs/TEXTURE_SYSTEM.md` §2 and the kernel-side
// gate in `src/cuda/CudaTestKernel.cu` +
// `src/optix/OptixPrograms.cu`):
//
//   Case 1 — flag OFF (TEX-P.5 audit):
//     `useBaseColorTexture == false`. The kernel-side gate
//     short-circuits on the flag and uses flat
//     `params.baseColor`; `baseColorTextureId` is therefore
//     ignored. If the id is nonetheless set (`>= 0`) emit a
//     `Logger::info` line so the operator can find the
//     dangling assignment. State is NOT modified — the artist
//     may intend to toggle the flag back later.
//
//   Case 2 — flag ON, id in range (happy path):
//     `useBaseColorTexture == true` AND
//     `baseColorTextureId in [0, texture_count)`. No log, no
//     state change; the kernel will sample the texture.
//
//   Case 3 — flag ON, id out of range (TEX-P.2 fixup):
//     `useBaseColorTexture == true` AND `baseColorTextureId`
//     is OUTSIDE `[0, texture_count)`. Emit a
//     `Logger::warning` naming the offending material
//     (id + name) and the bad index, then set
//     `useBaseColorTexture = false` so the kernel-side gate
//     falls back to flat `params.baseColor` on every
//     subsequent frame. The bad `baseColorTextureId` is
//     preserved on the POD; it is just no longer consulted.
//     This is what counts as a "fixup".
//
// Returns the number of Case 3 fixups applied. Case 1 info
// notes are NOT counted (they describe an artist decision, not
// a state correction). Defence-in-depth on top of the
// kernel's existing range check (`CudaTestKernel.cu` +
// `OptixPrograms.cu` both validate the id at hit time and
// silently fall back); the host-side validator gives the
// operator a warning / info log so they can find the
// authoring issue.
//
// Caller protocol: invoke after constructing / loading the
// scene + textures BUT before any GPU upload, so the kernel
// never sees an out-of-range id. Safe to call repeatedly
// (idempotent — the second call sees the post-fixup state and
// reports zero new fixups; Case 1 info notes re-emit on
// every call, which is the intended behaviour for a non-
// state-mutating audit).
//
// `texture_count == 0` is a valid input: every material with
// `useBaseColorTexture == true` becomes a Case 3 fixup since no
// id is in range.
[[nodiscard]] int validate_material_texture_ids(
    std::vector<SceneMaterial>& materials,
    std::size_t                  texture_count);

}
