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
#include "math/MathUtils.h"   // for RR_HD
#include "math/Vec3.h"

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


// ---------------------------------------------------------------------------
// evaluateMaterial: device-side material-graph evaluator.
// ---------------------------------------------------------------------------
//
// `RR_HD inline` so the same implementation runs in the
// kernel (CUDA / OptiX closest-hit) AND in host-side tests.
// The host suite exercises every per-opcode path the kernel
// will execute; the device path is correct by construction.
//
// Per spec section 9.4 the inner loop is straight-line: every
// op fires every iteration, the slot pool is a stack array
// (no dynamic allocation), and unwired inputs resolve through
// the immediate fallback the lowering already baked in. No
// branches on "is this input wired?".
//
// `MaterialEvalResult` is the colour-summary `evaluateMaterial`
// produces - the subset of `MaterialParams` the v1 kernel
// reads when shading a hit (baseColor for Lambertian
// diffuse, emissionColor + emissionStrength for self-emission).
// Other `MaterialParams` fields (roughness / metallic /
// specular / transmission) are NOT covered by the v1 graph and
// stay in `MaterialParams` for the kernel to read directly.

struct MaterialEvalResult {
    rr::math::Vec3 baseColor        = {0.8f, 0.8f, 0.8f};
    rr::math::Vec3 emissionColor    = {0.0f, 0.0f, 0.0f};
    float          emissionStrength = 0.0f;
};

// Maximum slot pool depth the device-side evaluator allocates
// on the stack. Cap at 32 so the per-thread storage stays
// modest (32 * 12 bytes = 384 bytes per thread). v1 graphs
// never approach this cap; ops past the cap are skipped (the
// caller's lowering already validated the size, but the
// runtime stays defensive).
inline constexpr int kMaterialGraphMaxSlots = 32;

RR_HD inline rr::math::Vec3
_eval_slot_or(const rr::math::Vec3* slots,
              int op_count,
              std::int16_t slot_idx,
              rr::math::Vec3 fallback) {
    if (slot_idx < 0)                        return fallback;
    if (slot_idx >= op_count)                return fallback;
    if (slot_idx >= kMaterialGraphMaxSlots)  return fallback;
    return slots[slot_idx];
}

RR_HD inline float
_eval_slot_scalar_or(const rr::math::Vec3* slots,
                     int op_count,
                     std::int16_t slot_idx,
                     float fallback) {
    if (slot_idx < 0)                        return fallback;
    if (slot_idx >= op_count)                return fallback;
    if (slot_idx >= kMaterialGraphMaxSlots)  return fallback;
    // No vec3 -> float implicit conversion in v1 (per spec
    // 7.3) but the IR's terminal-table can still reference
    // a vec3 slot via `in_strength` if a future scene-format
    // slice somehow connects one. Take the .x component as
    // the documented degenerate case so the value is
    // deterministic. v1 graphs always leave `in_strength`
    // unwired.
    return slots[slot_idx].x;
}

RR_HD inline MaterialEvalResult
evaluateMaterial(const CudaMaterialGraphView& view) {
    rr::math::Vec3 slots[kMaterialGraphMaxSlots];
    for (int i = 0; i < kMaterialGraphMaxSlots; ++i) {
        slots[i] = rr::math::Vec3{0.0f, 0.0f, 0.0f};
    }

    int op_count = view.op_count;
    if (op_count > kMaterialGraphMaxSlots) op_count = kMaterialGraphMaxSlots;

    // Linear opcode loop. No early-out, no skip; every op
    // writes its slot. Constant folding / dead-code drop is
    // the host lowering's job (per spec 9.4); the kernel
    // trusts the IR.
    for (int i = 0; i < op_count; ++i) {
        const rr::material::GpuOp& op = view.ops[i];
        rr::math::Vec3 result{0.0f, 0.0f, 0.0f};
        switch (op.opcode) {
        case rr::material::GpuOpcode::ConstantColor:
            result = op.imm_color;
            break;
        case rr::material::GpuOpcode::TextureSample:
            // v1: TextureSample is a placeholder. The lowered
            // op carries the white "missing texture" fallback
            // the kernel returns directly; a future slice
            // replaces this with sample_texture(view, uv).
            result = op.imm_color;
            break;
        case rr::material::GpuOpcode::Add: {
            const auto a = _eval_slot_or(slots, op_count, op.in_a,
                                          rr::math::Vec3{0, 0, 0});
            const auto b = _eval_slot_or(slots, op_count, op.in_b,
                                          rr::math::Vec3{0, 0, 0});
            result = rr::math::Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
            break;
        }
        case rr::material::GpuOpcode::Multiply: {
            const auto a = _eval_slot_or(slots, op_count, op.in_a,
                                          rr::math::Vec3{1, 1, 1});
            const auto b = _eval_slot_or(slots, op_count, op.in_b,
                                          rr::math::Vec3{1, 1, 1});
            result = rr::math::Vec3{a.x * b.x, a.y * b.y, a.z * b.z};
            break;
        }
        default:
            // Diffuse / Emission opcodes are terminals - they
            // do not appear in `ops[]`. Defensive skip.
            break;
        }
        slots[i] = result;
    }

    // Compose the per-hit MaterialParams subset from the
    // terminals. Each terminal writes exactly one piece of
    // the eval struct; v1 allows at most one of each
    // terminal kind, so we just walk the table and assign.
    MaterialEvalResult out;
    for (int i = 0; i < view.terminal_count; ++i) {
        const rr::material::GpuTerminal& t = view.terminals[i];
        switch (t.kind) {
        case rr::material::GpuOpcode::Diffuse:
            out.baseColor = _eval_slot_or(slots, op_count,
                                          t.in_color, t.imm_color);
            break;
        case rr::material::GpuOpcode::Emission:
            out.emissionColor    = _eval_slot_or(slots, op_count,
                                                  t.in_color, t.imm_color);
            out.emissionStrength = _eval_slot_scalar_or(slots, op_count,
                                                        t.in_strength,
                                                        t.imm_strength);
            if (out.emissionStrength < 0.0f) out.emissionStrength = 0.0f;
            break;
        default:
            // Non-terminal opcodes in the terminal table
            // would be a lowering bug; v1 cannot produce
            // them. Skip defensively.
            break;
        }
    }
    return out;
}

}
