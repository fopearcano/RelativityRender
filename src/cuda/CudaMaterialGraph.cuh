#pragma once

// CUDA-side view of a compiled `rr::material::GpuMaterial`.
//
// `CudaMaterialGraphView` is the launch-argument POD a future
// kernel will receive when the per-hit graph evaluator lands.
// The host owns the device storage (a `GpuBuffer<GpuOp>` plus
// a `GpuBuffer<GpuTerminal>`); the view exposes them as raw
// device pointers + counts the kernel can iterate without a
// device-side allocation.
//
// This header is host-includable: it pulls in
// `material/GpuMaterial.h` for the `GpuOp` / `GpuTerminal`
// PODs but does NOT use any CUDA-runtime types beyond what
// those PODs already need. That matches the rest of the
// `cuda/*View*` headers (`CudaTexture.cuh`,
// `CudaMaterial.cuh`) which the host-side `GpuScene` upload
// path also includes.
//
// v1 ships the view but no execution. Future slices add:
//   - the kernel-side interpreter that walks `ops[]` /
//     `terminals[]`,
//   - the `GpuScene::upload_material_graphs` path that fills
//     `GpuBuffer<GpuOp>` / `GpuBuffer<GpuTerminal>` and
//     returns a populated view per material,
//   - the binding from a hit's material id to its view.

#include "material/GpuMaterial.h"

#include <cstdint>

namespace rr::cuda {

struct CudaMaterialGraphView {
    // Operation list. `op_count` is the number of records
    // and equals the slot pool size: every op produces
    // exactly one slot value, indexed by its position in
    // this array.
    const rr::material::GpuOp*       ops        = nullptr;
    std::int32_t                     op_count   = 0;

    // Terminal-table list. Each entry contributes one piece
    // of the per-hit `MaterialParams` snapshot the renderer
    // composes (Diffuse -> baseColor, Emission -> emission
    // colour + strength). v1 has at most one of each
    // terminal kind per material.
    const rr::material::GpuTerminal* terminals       = nullptr;
    std::int32_t                     terminal_count  = 0;
};

// `material_id` -> view lookup table. A future
// `GpuScene::upload_material_graphs` populates one of these
// per scene; the kernel reads `views[material_id]` to fetch
// the per-hit material's compiled IR. v1 leaves the
// definition here so the shape is committed across
// host / device without any code yet referencing it.
struct CudaMaterialGraphArrayView {
    const CudaMaterialGraphView* views          = nullptr;
    std::int32_t                 material_count = 0;
};

}
