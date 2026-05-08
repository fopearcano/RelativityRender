#pragma once

#include "scene/Scene.h"

#include <string>

namespace rr::io {

// True iff `path` resolves to a regular file the process can stat.
// Symlinks are followed; directories and special files do not count.
// Any filesystem error is collapsed to `false` (the caller can re-
// attempt the open and surface a more specific OS error then).
[[nodiscard]] bool sceneFileExists(const std::string& path);

// Result of a `.rrscene` load / parse attempt.
//
// Stage 10B.2 surface: only the top-level `version` plus the
// `render_settings` section are mapped onto `scene`; every other
// top-level key (`camera`, `relativity`, `materials`, `spheres`,
// `meshes`, `lights`) is parsed for syntactic validity and then
// dropped. Camera / relativity / etc. mappers join in follow-up
// sub-stages without changing this struct's shape.
//
// On success: `ok == true`, `scene` is populated, `version` carries
// the literal version string from the file, and `error_message` is
// empty.
//
// On failure: `ok == false`, `scene` holds whatever was decoded up
// to the point of failure (typically defaults), `version` may be
// populated if the failure happened after the version field was
// read, and `error_message` is a human-readable description. When
// the failure is a JSON parse error, `error_line` and
// `error_column` are 1-based positions into the source text;
// otherwise they are 0.
struct LoadResult {
    bool                ok           = false;
    rr::scene::Scene    scene{};
    std::string         version;
    std::string         error_message;
    int                 error_line   = 0;
    int                 error_column = 0;
};

// Read `path` from disk and parse its contents as a `.rrscene` v1
// document. Returns a `LoadResult` describing success or failure.
// File-not-found and read errors collapse to `ok = false` with an
// `error_message` describing the OS-level cause.
[[nodiscard]] LoadResult load(const std::string& path);

// Parse an in-memory `.rrscene` v1 document. Identical to `load`
// except the source is the literal `text` argument. Useful for
// fixture-based tests that don't want to round-trip through disk.
[[nodiscard]] LoadResult parse(const std::string& text);

}  // namespace rr::io
