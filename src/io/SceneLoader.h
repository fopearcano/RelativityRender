#pragma once

#include <string>

namespace rr::io {

// Stage 10B.1 surface — file-existence check only. Returns `true`
// iff `path` names an existing regular file (symlinks are followed).
// Both filesystem errors and "exists but is a directory" return
// `false`. Pure host code; never throws.
//
// The `.rrscene` parsing API (`load(path)`, `parse(text)`,
// `LoadResult`, etc.) lands in a follow-up sub-stage against the
// contract defined in `docs/RRSCENE_FORMAT.md` v1.0. This file
// is kept narrow on purpose: Stage 10B.1's job is only to scaffold
// the `rr_io` module + commit to a JSON strategy.
[[nodiscard]] bool sceneFileExists(const std::string& path);

}  // namespace rr::io
