#pragma once

// CUDA-side view of `rr::gpu::GpuScene`. Only safe to include from
// `.cu` files because it pulls in `<cuda_runtime.h>`.
//
// `CudaSceneView` is the launch-argument POD the renderer kernel
// receives by value. It carries the camera / observer / relativity
// parameters directly (small PODs that fit in the launch-argument
// buffer) and a device pointer + count for the sphere array.
//
// Future entity types (texture views) join this struct so the
// kernel signature does not change with every new module. Stage 9B
// extends the view with the lights array; multi-mesh + texture
// support is a future slice.

#include "camera/CameraRay.h"
#include "cuda/CudaAOV.cuh"       // DeviceAOVView for the AOV write slot
#include "cuda/CudaMesh.cuh"
#include "cuda/CudaTexture.cuh"   // DeviceTextureView for the texture array slot
#include "field/FieldMapping.h"   // FIELD-BEAUTY.3: per-launch field-mapping config payload
#include "field/ScalarField.h"    // FIELD-I.9: per-launch scalar-field config payload
#include "geometry/Sphere.h"
#include "lighting/Light.h"
#include "manifold/CoordinateChart.h"  // SCHW.5: per-launch chart payload
#include "manifold/ManifoldMode.h"     // SCHW.5: per-launch manifold mode
#include "manifold/ObserverFrame.h"    // OBSERVER.8: per-launch observer-frame payload
#include "material/MaterialTypes.h"
#include "relativity/RelativityParams.h"

#include <cuda_runtime.h>

namespace rr::cuda {

struct CudaSceneView {
    rr::camera::GpuCamera             camera;
    rr::relativity::Observer          observer;
    rr::relativity::RelativityParams  params;
    const rr::geometry::Sphere*       spheres      = nullptr;  // device pointer
    int                               sphere_count = 0;

    // Single-mesh slot. `mesh.triangle_count == 0` means "no mesh
    // contributes triangles to this scene". Multi-mesh support
    // promotes this to an array (or device-resident handle list)
    // in a later slice.
    CudaMeshView                      mesh;

    // Material array. `Sphere::material_index` and `Mesh::material_id`
    // are integer indices into `materials[0 .. material_count - 1]`;
    // values outside that range fall back to a neutral default in
    // the kernel. `nullptr` + `material_count == 0` is allowed and
    // means "no materials uploaded - everything uses the default".
    const rr::material::MaterialParams* materials      = nullptr;
    int                                 material_count = 0;

    // Light array. Iterated per hit (Stage 9B reads it without
    // shadow-ray visibility tests; shadows land later). `nullptr`
    // + `light_count == 0` is allowed and means "no lights
    // uploaded - the kernel falls through to the facing-ratio
    // shade for backwards compatibility with the unlit
    // diagnostics".
    const rr::lighting::Light*        lights        = nullptr;
    int                               light_count   = 0;

    // Texture array. Stage 13B.3 wiring (master order #18). The
    // kernel reads `textures[mat.baseColorTextureId]` whenever
    // `mat.useBaseColorTexture` is true and the id is in
    // `[0, texture_count)`; otherwise it falls back to
    // `mat.baseColor`. `nullptr` + `texture_count == 0` is
    // allowed and means "no textures uploaded - every material
    // uses its flat baseColor", preserving backward compatibility
    // with every existing CLI action.
    const rr::cuda::DeviceTextureView* textures      = nullptr;
    int                                texture_count = 0;

    // AOV write slot. Stage 14A.3 wiring (master order #19). Each
    // member of `aovs` is a device pointer (or `nullptr` when the
    // pass is not requested) into a per-pass `GpuAOVBuffer`. The
    // kernel writes whichever AOVs have non-null pointers; the
    // default-constructed view skips every pass, which keeps every
    // existing render action's behaviour byte-identical.
    rr::cuda::DeviceAOVView           aovs;

    // ---- SCHW.5 per-launch manifold payload ----
    //
    // Mirrors the OptiX side's
    // `OptixLaunchParams::manifold_mode` (MANI-I.5) +
    // `OptixLaunchParams::coordinate_chart` (SCHW.7)
    // fields. The CUDA kernel
    // (`CudaTestKernel.cu::k_render_scene`'s
    // `ManifoldCoordinates` AOV write arm) reads these
    // to gate the SchwarzschildLike warp on the
    // `is_active(manifold_mode) && chart ==
    // SchwarzschildLike && strength > 0` triple-gate,
    // and to invoke the shared SCHW.1 math leaf with
    // the chart's per-pixel artist parameters extracted
    // from `coordinate_chart.params` per the
    // `SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §3
    // reinterpretation table.
    //
    // Defaults are the pre-pivot
    // disabled / Euclidean / strength-0 / no-chart no-op
    // anchor. With the defaults, the kernel arm
    // short-circuits and the AOV write is byte-identical
    // to the pre-SCHW.5 MANI-I.8 raw `best.position`
    // output. The host
    // (`CudaRenderer::render_scene_with_aovs`) populates
    // these when the operator engages the manifold via
    // CLI or scene file (SCHW.9).
    rr::manifold::ManifoldMode        manifold_mode{};
    rr::manifold::CoordinateChart     coordinate_chart{};

    // ---- OBSERVER.8 per-launch observer-frame payload ----
    //
    // Built by the OBSERVER.6 camera-to-observer adapter
    // (`build_observer_frame_from_camera(...)`) at the
    // dispatcher's call site
    // (`main.cpp::run_render_aovs` for the AOV path) and
    // threaded into the view by
    // `CudaRenderer::render_scene_with_aovs`.
    //
    // The default `rr::manifold::ObserverFrame{}` is the
    // byte-identity no-op anchor: `perception_mode =
    // Identity`, `beta = 0`, world-basis tetrad, both
    // time placeholders = 0. With the default, the kernel
    // arms (which currently do NOT read this field per
    // OBSERVER.8's "no CUDA kernel behavior change beyond
    // carrying data" contract) preserve the existing
    // ray-generation + shading behaviour byte-for-byte.
    //
    // The field is reserved-but-carried this slice: a
    // subsequent slice (no earlier than the kernel-side
    // reads land) will gate the SR-helper call sites on
    // `observer_frame.perception_mode == ConstantVelocityMinkowski`
    // and key the existing aberration / Doppler /
    // searchlight helpers on `observer_frame.beta` (instead
    // of the legacy `observer.velocity`). Until that slice
    // lands the field travels through the launch boundary
    // but is not read.
    rr::manifold::ObserverFrame       observer_frame{};

    // ---- FIELD-I.9 per-launch scalar-field config payload ----
    //
    // The FIELD-I.4 + FIELD-I.2 tagged-form
    // `rr::field::ScalarFieldConfig` POD; default
    // `disabled_scalar_field_config()` (= `enabled = false,
    // strength = 0.0f, kind = Constant`, all other defaults
    // = 0). With the default, the kernel arm reading this
    // field
    // (`CudaTestKernel.cu::k_render_scene`'s `FieldScalar`
    // AOV-write arm) computes
    // `rr::field::evaluate(scalar_field_config, hit_pos)`
    // which short-circuits to `0.0f` at every position (the
    // FIELD-I.3 audit's check #2 three-layer no-op anchor).
    //
    // The CUDA kernel's FIELD-I.9 AOV-write arm reads this
    // field **only** when the corresponding
    // `aovs.field_scalar` device pointer is non-null (i.e.
    // when the operator has requested the diagnostic AOV).
    // No other kernel arm consumes this payload field this
    // slice — the FIELD-I.6 task brief §6 "no
    // field-to-beauty mapping yet" non-goal is satisfied
    // structurally because the field is not read by the
    // beauty / Normal / Depth / Albedo / DopplerFactor /
    // SearchlightFactor / ManifoldCoordinates / ObserverBeta
    // arms.
    //
    // Populated by `CudaRenderer::render_scene_with_aovs`
    // from `AOVTargets::scalar_field_config`. Until a future
    // CLI / scene-loader slice lands the authoring surface
    // (the renumbered FIELD-I.11 CLI + Config bridge), every
    // dispatcher call site passes the default; the kernel
    // arm therefore writes `0.0f` per pixel into the AOV
    // (the documented "field-disabled = neutral/zero
    // diagnostic" anchor from the FIELD-I.6 task brief's
    // §3.2).
    rr::field::ScalarFieldConfig      scalar_field_config{};

    // ---- FIELD-BEAUTY.3 per-launch field-mapping config payload ----
    //
    // The FIELD-I.4 single-target tagged-form
    // `rr::field::FieldMappingConfig` POD; default
    // `disabled_field_mapping_config()` (= `target = None,
    // strength = 0.0f, bias = 0.0f, min_value = 0.0f,
    // max_value = 1.0f, clamp_output = false`). Companion to
    // `scalar_field_config` above; together they describe
    // **what** the scalar field is + **how** it maps into a
    // beauty channel.
    //
    // The CUDA kernel's FIELD-BEAUTY.3 beauty-mapping arm
    // (`CudaTestKernel.cu::k_render_scene`) gates on the
    // double-condition `scalar_field_config.enabled == true`
    // AND `field_mapping_config.target` ∈ {`ColorMultiplier`,
    // `Emission`}. Default `disabled_field_mapping_config()`
    // (target = None) preserves byte-identical output by
    // construction even when `scalar_field_config.enabled
    // = true` — the target-None short-circuit in
    // `evaluate_mapping(...)` returns 0 AND the kernel arm's
    // target-specific guard skips both ColorMultiplier and
    // Emission branches.
    //
    // The FieldScalar diagnostic AOV's write arm (FIELD-I.9
    // at `CudaTestKernel.cu`'s post-framebuffer-write block)
    // does NOT consume this field — the diagnostic AOV
    // writes the raw `evaluate(scalar_field_config,
    // hit_pos)` output regardless of mapping target. This
    // preserves the FIELD-I.6 task brief §3.2 "neutral/zero
    // diagnostic" anchor + the FIELD-I.4 audit's
    // mapping-vs-diagnostic separation.
    //
    // Populated by `CudaRenderer::render_scene_with_aovs`
    // from `AOVTargets::field_mapping_config`. Until a
    // future CLI / scene-loader slice lands the authoring
    // surface, every dispatcher caller passes the default
    // `disabled_field_mapping_config()`; the beauty-mapping
    // arms are structurally unreachable.
    rr::field::FieldMappingConfig     field_mapping_config{};
};

}
