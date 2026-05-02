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
// SDK-gated: this header includes `<optix.h>` only when the
// SDK was located at configure time (Stage 12B.4 detection).
// On the audit host the file compiles down to "no record
// types" and consumers gate on the same macro.

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND

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

}  // namespace rr::optix

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND
