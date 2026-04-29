#pragma once

#include <string>

namespace rr::core {

// Application-wide configuration. Stage 1 covers only the four knobs
// the CLI lets the user set today: image dimensions, scene file, and
// output path. The struct is intentionally a plain aggregate; later
// stages add fields (samples per pixel, max bounces, device index, ...)
// alongside the modules that consume them.
struct Config {
    int         width       = 1280;
    int         height      = 720;
    std::string scene_path;     // empty => no scene
    std::string output_path;    // empty => no output

    // Returns an empty string when the configuration is internally
    // consistent. Otherwise returns a human-readable description of
    // the first problem.
    [[nodiscard]] std::string validate() const;
};

}
