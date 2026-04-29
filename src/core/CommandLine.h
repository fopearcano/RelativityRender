#pragma once

#include "core/Config.h"

#include <string>
#include <string_view>

namespace rr::core {

// Command-line parser. Pure host code, no dependencies beyond
// `core/Config.h`. The CLI surface today:
//
//   --help                  Print usage and exit.
//   --version               Print version and exit.
//   --device-info           Print GPU device info.
//   --render <scene>        Run the renderer on the given scene file
//                           (placeholder; scene loading lands in a
//                           later stage).
//   --render-gradient       Run the GPU UV-gradient diagnostic and
//                           save it to <output>. Requires CUDA.
//   --render-rays           Run the GPU camera-ray-direction
//                           visualisation and save it to <output>.
//                           Requires CUDA.
//   --output <path>         Write the rendered image to <path>.
//                           Default for --render-gradient is
//                           "output/gpu_gradient.ppm";
//                           default for --render-rays     is
//                           "output/gpu_camera_rays.ppm".
//   --width  <int>          Render width in pixels  (default 1280).
//   --height <int>          Render height in pixels (default 720).
//
// Action flags (--help / --version / --device-info / --render /
// --render-gradient / --render-rays) are mutually exclusive;
// combining them is a parse error. The remaining flags configure
// `Config` and are accepted regardless of action.

class CommandLine {
public:
    enum class Action {
        Default,        // no action flag given
        Help,
        Version,
        DeviceInfo,
        Render,
        RenderGradient,
        RenderRays,
        Error,          // parse failure; see `error_message`
    };

    struct ParseResult {
        Action      action = Action::Default;
        Config      config;
        std::string error_message;
    };

    // Parse `argc` / `argv` (typically straight from `main`). The
    // returned `ParseResult` is self-contained; callers dispatch on
    // `action` and read `config` / `error_message` as appropriate.
    [[nodiscard]] static ParseResult parse(int argc, char** argv);

    // Multi-line usage text. Includes a trailing newline.
    [[nodiscard]] static std::string usage(std::string_view argv0);

    // Single-line "ProjectName X.Y.Z" string (no trailing newline).
    [[nodiscard]] static std::string version_string();
};

}
