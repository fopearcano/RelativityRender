#pragma once

// CUDA-side variant of the AOV / render-pass data model
// (Stage 14A.1; master order #19).
//
// Stage 14A.1 shipped a thin re-export of the host header so
// kernels could `#include "cuda/CudaAOV.cuh"` to signal intent.
// Stage 14A.3 extends this header with `DeviceAOVView`, a small
// POD the kernel reads at launch time to know which AOVs to
// write into. Each pointer is the device target for one pass;
// `nullptr` means "this pass is not requested" and the kernel
// skips the corresponding write. The pointer layout matches the
// six prompt-required pass types (Beauty / Normal / Depth /
// Albedo / DopplerFactor / SearchlightFactor).
//
// Per-pass component count (mirrors `aov_component_count` in the
// host header):
//
//   beauty             - 3 floats / pixel (RGB; encoded the same
//                        way as the framebuffer, no remap)
//   normal             - 3 floats / pixel (XYZ encoded as
//                        `0.5 * n + 0.5` for hits so the pass is
//                        directly viewable; (0, 0, 0) on miss)
//   depth              - 1 float  / pixel (`1.0 / (1.0 + t)` for
//                        hits, so closer surfaces are brighter
//                        and the value is bounded in [0, 1] for
//                        PPM viewing; 0 on miss)
//   albedo             - 3 floats / pixel (pre-lighting RGB,
//                        sampled from the material; (0, 0, 0)
//                        on miss)
//   doppler_factor     - 1 float  / pixel (raw D = omega_obs /
//                        omega_emit; ~1 for a stationary
//                        observer, varies for relativistic motion)
//   searchlight_factor - 1 float  / pixel (raw D^4; ~1 for a
//                        stationary observer)
//
// The AOV writes are independent of the existing relativity
// `enable_*` toggles: the searchlight_factor / doppler_factor
// passes record the raw physical values regardless of whether
// the beauty pass applies them. This keeps each pass as its own
// data product.
//
// No texture object, no mipmap, no per-pass UV transform.
// Stage 14A.1 did not add a device-side descriptor; Stage 14A.3
// does so now that the kernel actually consumes one.

#include "renderer/AOV.h"

namespace rr::cuda {

// Per-launch device-pointer set the kernel walks to write each
// requested pass. Default-constructed (all `nullptr`) means "no
// AOVs requested" and every existing render action is byte-
// identical to its pre-14A.3 behaviour.
//
// Pointers are device-resident raw float arrays; lifetime is the
// caller's responsibility (the eventual renderer-integration
// path holds a `GpuAOVBuffer` per active pass and snapshots its
// `device_ptr()` here).
struct DeviceAOVView {
    float* beauty               = nullptr;  // 3 floats / pixel
    float* normal               = nullptr;  // 3 floats / pixel
    float* depth                = nullptr;  // 1 float  / pixel
    float* albedo               = nullptr;  // 3 floats / pixel
    float* doppler_factor       = nullptr;  // 1 float  / pixel
    float* searchlight_factor   = nullptr;  // 1 float  / pixel

    // MANI-I.8 — manifold debug coordinate-visualisation AOV.
    // Writes the per-pixel chart-space hit position
    // `(world_hit.x, world_hit.y, world_hit.z)` on hit and
    // `(0, 0, 0)` on miss (matches the Normal AOV's miss
    // convention). The active manifold mode is read from
    // launch params; for MANI-I.8 the kernel writes the
    // world-space hit position regardless of chart selection
    // — the documented identity / neutral diagnostic — because
    // no curved-chart `world_to_chart` math has landed yet.
    // Future MANI-I.9+ slices will branch on
    // `manifold_mode.chart` inside this write arm.
    float* manifold_coordinates = nullptr;  // 3 floats / pixel

    // OBSERVER.13 — observer-frame debug-visualisation AOV.
    // Writes the per-pixel `view.observer_frame.beta` value
    // on hit and `(0, 0, 0)` on miss (matches the Normal /
    // ManifoldCoordinates AOVs' miss convention). The
    // `view.observer_frame` field is populated by the
    // dispatcher via the OBSERVER.6 camera-to-observer
    // adapter and threaded through OBSERVER.8's
    // `CudaSceneView::observer_frame` carry-only slot. The
    // AOV write is read-only on the observer payload — no
    // perception transform (no aberration / Doppler /
    // searchlight re-keying) is applied this slice per the
    // OBSERVER.12 task brief's "no perception transform yet"
    // contract. Opt-in: the pointer is null unless the
    // operator passes `--render-aovs --observer-debug`
    // (see `docs/OBSERVER_DEBUG_AOV_TASK.md`).
    float* observer_beta        = nullptr;  // 3 floats / pixel
};

}  // namespace rr::cuda

