#pragma once

// Stage 17A.3 SBT record types per
// `docs/OPTIX_BACKEND_PLAN.md` §21.
//
// Each record begins with a `OPTIX_SBT_RECORD_HEADER_SIZE`
// header that `optixSbtRecordPackHeader` populates from a
// `OptixProgramGroup`; the rest of the record is per-program
// payload. Stage 17A.3 ships header-only records (no payload)
// for the raygen + miss program groups - the minimum the
// runtime requires.
//
// Stage 20G adds per-record material data to the hit-group
// record: `HitGroupData { MaterialParams params; int
// shading_mode; }`. The closest-hit reads the SBT data
// pointer, casts to `HitGroupData`, and branches on
// `shading_mode`:
//   - 0 (default; existing behaviour): emit normal-as-color
//     (`0.5*n + 0.5`). `params` is ignored. Stage 17A.4
//     visual output is preserved byte-for-byte.
//   - 1 (Stage 20G material shading): emit `params.baseColor
//     + params.emissionColor * params.emissionStrength`.
//     `params` is the picked mesh's material data, copied
//     into the SBT record by `OptixPipeline::set_hit_material(...)`.
// The Stage 17A.5 Doppler / searchlight stack composes on
// top of either base shade.
//
// SDK-gated: this header includes `<optix.h>` only when the
// SDK was located at configure time (Stage 12B.4 detection).
// On the audit host the file compiles down to "no record
// types" and consumers gate on the same macro.

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND

#include "material/MaterialTypes.h"

#include <optix.h>

#include <cstring>

namespace rr::optix {

// Empty raygen record. Per the OptiX 7+ SBT layout, every
// record begins with a header populated by
// `optixSbtRecordPackHeader(programGroup, &record)`. Stage
// 17A.3's raygen needs no extra data, so the record is just
// the header padded out to OPTIX_SBT_RECORD_ALIGNMENT.
struct __align__(OPTIX_SBT_RECORD_ALIGNMENT) RaygenSbtRecord {
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
};

// Empty miss record. Same shape as `RaygenSbtRecord`; future
// sub-stages add a payload field (e.g. sky-tint colour).
struct __align__(OPTIX_SBT_RECORD_ALIGNMENT) MissSbtRecord {
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
};

// Stage 20G hit-group payload. Travels in the SBT record
// after `OPTIX_SBT_RECORD_HEADER_SIZE` bytes; the closest-hit
// program reads it via `optixGetSbtDataPointer()`.
//
// `params` is the picked mesh's material data (copied from
// the host-side `rr::scene::Scene::materials[material_id]`).
// `shading_mode` controls which base shade the closest-hit
// emits:
//   0 = normal-as-color (Stage 17A.4 default; `params` is
//       not consulted)
//   1 = material flat (Stage 20G;
//       baseColor + emissionColor * emissionStrength)
struct HitGroupData {
    rr::material::MaterialParams params{};
    int                          shading_mode = 0;
};

// Stage 17A.4 + 20G hit-group record for the triangle
// closest-hit program. Header packed by
// `optixSbtRecordPackHeader(...)`; payload populated by
// `OptixPipeline::create()` (default `HitGroupData{}` -> mode
// 0) and optionally by `OptixPipeline::set_hit_material(...)`
// (Stage 20G -> mode 1 + the picked material's params).
struct __align__(OPTIX_SBT_RECORD_ALIGNMENT) HitGroupSbtRecord {
    char         header[OPTIX_SBT_RECORD_HEADER_SIZE];
    HitGroupData data;
};

}  // namespace rr::optix

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND
