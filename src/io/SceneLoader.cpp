#include "io/SceneLoader.h"

#include <filesystem>
#include <system_error>

namespace rr::io {

bool sceneFileExists(const std::string& path) {
    std::error_code ec;
    const std::filesystem::path p(path);

    // `exists` follows symlinks. We require a regular file underneath,
    // not a directory or special file. Any filesystem error reported
    // through `ec` collapses to "does not exist as a usable scene
    // file" - the caller can re-attempt the open and surface a more
    // specific error from the OS at that point if needed.
    if (!std::filesystem::exists(p, ec) || ec) return false;
    if (!std::filesystem::is_regular_file(p, ec) || ec) return false;
    return true;
}

}  // namespace rr::io
