#pragma once

namespace rr::io {

// Stage 10B.1 scaffold. The writer's API surface
// (`save(path, scene)`, `serialize(scene)`, `WriteResult`)
// arrives alongside the parser implementation in a follow-up
// sub-stage, against the contract defined in
// `docs/RRSCENE_FORMAT.md` v1.0.
//
// This header exists so the `rr_io` library has a stable
// public include path for the writer; the matching `.cpp`
// is currently empty but reserved.

}  // namespace rr::io
