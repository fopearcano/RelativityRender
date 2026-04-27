#pragma once

#include <optional>
#include <string>

namespace rr::core {

// Runtime configuration for the RelativityRender executable.
//
// Defaults match a sensible HD-ish render. Fields populated by
// `CommandLine::parse(...)`. Anything optional that is left unset means
// "not requested on the command line"; downstream code decides what to do
// with that information (or, at this milestone, just logs it).
struct Config {
    // Action flags.
    bool show_device_info = false;

    // Render request (M2 scope: parsed only, no rendering yet).
    std::optional<std::string> render_scene_path;
    std::optional<std::string> output_image_path;

    // Image resolution.
    int width  = 1280;
    int height = 720;

    // True iff `--render <path>` was specified on the command line.
    [[nodiscard]] bool wants_render() const { return render_scene_path.has_value(); }
};

}
