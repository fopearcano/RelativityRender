#pragma once

// CUDA-side variant of the AOV / render-pass data model
// (Stage 14A.1; master order #19).
//
// Currently a thin re-export of the host header so kernels can
// `#include "cuda/CudaAOV.cuh"` to signal intent. Stage 14A.1 is
// data model only - there is no per-pass framebuffer descriptor,
// no `RR_HD inline` write helper, no kernel hook. The eventual
// renderer-integration sub-stage adds:
//
//   - a device-side `DeviceAOVView` POD (per-pass pointer + dims +
//     component count) that the kernel writes into, mirroring
//     the `DeviceTextureView` / `CudaSceneView` pattern;
//   - `RR_HD inline` write helpers for each `AOVType`'s component
//     layout (1-channel scalar passes vs 3-channel vector passes);
//   - the launch-argument plumbing in `CudaSceneView` so a single
//     kernel can fill multiple AOVs in one pass.
//
// The host enum values + descriptors are POD-friendly enough that
// the device-side descriptor will be a small additional struct
// when it lands. Until then, kernels that want to reference the
// upstream types include this header, mirroring the pattern set
// by `CudaMaterial.cuh` / `CudaLight.cuh` / Stage 13A's
// `CudaTexture.cuh`.

#include "renderer/AOV.h"
