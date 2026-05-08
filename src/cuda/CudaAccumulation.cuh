#pragma once

// Host-callable launcher declarations for the Stage 11B
// progressive accumulation primitives. The kernels (clear, add a
// sample frame, resolve sums to a normalised display buffer) and
// the launcher definitions live in `CudaAccumulation.cu`.
//
// The signatures use only host-friendly types (raw pointers,
// integers, floats), so this header can be included directly by
// the host-only `renderer/AccumulationBuffer.cpp` without
// pulling `<cuda_runtime.h>` into that translation unit. CUDA
// TUs (e.g. `CudaRenderer.cu`) include this header too when they
// need to drive the same primitives, matching the pattern used
// by `CudaBuffer.h`.
//
// All four launchers are no-ops when called with `count == 0` or
// a null pointer; the per-element grid is sized by the host.

#include <cstddef>

namespace rr::cuda {

// Zero `float_count` floats starting at `device_acc`. Forwards
// to `cudaMemset` on the device (no kernel launch needed).
// Returns false on failure. `float_count == 0` is success.
[[nodiscard]] bool launch_accum_clear(float* device_acc,
                                      std::size_t float_count);

// Stage 18A.4: first-sample fast path. Equivalent to
// `launch_accum_clear` followed by `launch_accum_add`, but skips
// the `cudaMemset` + the read-of-zeros the add kernel would do
// on the freshly cleared accumulator. Forwards to
// `cudaMemcpy(device_acc, device_sample, ..., D2D)` which uses
// the memory controller's bulk-copy fast path - no SM
// occupancy. Output is bit-identical to the scalar add path
// when the accumulator is in its zero state (acc + sample ==
// sample). The host-side `AccumulationBuffer::accumulate_sample`
// routes `samples_count() == 0` through this entry point.
[[nodiscard]] bool launch_accum_first_sample(float* device_acc,
                                             const float* device_sample,
                                             std::size_t float_count);

// Element-wise device-side add: `device_acc[i] += device_sample[i]`
// for `i` in `[0, float_count)`. Both pointers must reference
// device memory. The kernel is a single 1D grid sized by the
// caller; the host never iterates pixels here.
//
// Stage 18A.4: when `float_count` is a multiple of 4 (the
// Rgba32F invariant every documented caller honours) the
// launcher dispatches to a `float4`-vectorised kernel that does
// 1/4 the memory transactions and 1/4 the threads. Falls back
// to the scalar kernel otherwise. Behaviour is bit-identical
// across both paths (single-precision add is deterministic).
[[nodiscard]] bool launch_accum_add(float* device_acc,
                                    const float* device_sample,
                                    std::size_t float_count);

// Element-wise device-side scale into a separate display buffer:
// `device_display[i] = device_acc[i] * inv_samples`. The
// accumulator is read-only; the resolve produces a fresh
// normalised buffer the host then downloads. `inv_samples` is
// `1.0f / samples_count` (computed once on the host so the
// kernel does no division).
//
// Stage 18A.4: same `float4` fast path / scalar fallback split
// as `launch_accum_add`.
[[nodiscard]] bool launch_accum_resolve(const float* device_acc,
                                        float* device_display,
                                        std::size_t float_count,
                                        float inv_samples);

// Test sample-source kernel for the Stage 11B validation path.
// Writes one frame of per-pixel `(r, g, b, 1.0)` where each of
// `r`, `g`, `b` is a fresh `pathtracer::next_float`. `seed` and
// `sample_index` mix into `pathtracer::make_pixel_rng` so each
// of the N accumulated frames produces decorrelated noise; the
// accumulated mean per channel converges to 0.5, which is the
// visual signal that accumulation + resolve are both correct.
//
// Note: this is *not* a path-tracer integration point - it is
// the test-only sample source for `--render-accumulation-test`.
// The real path tracer (master order #16) supplies its own
// per-frame sample buffer to `accumulate_sample`.
[[nodiscard]] bool launch_random_rgba_sample(float* device_pixels,
                                             int width, int height,
                                             unsigned int seed,
                                             unsigned int sample_index);

}  // namespace rr::cuda
