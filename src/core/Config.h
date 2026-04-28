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

    // Renderer-server mode. When set, `main` constructs a
    // `rr::server::RenderServer` and blocks in its accept loop
    // instead of running the one-shot render path. The server
    // binds to `127.0.0.1:7777` by v1 convention; multi-port /
    // multi-host support is a follow-up.
    bool serve = false;

    // Render request (M2 scope: parsed only, no rendering yet).
    std::optional<std::string> render_scene_path;
    std::optional<std::string> output_image_path;

    // Image resolution.
    int width  = 1280;
    int height = 720;

    // True iff `--render <path>` was specified on the command line.
    [[nodiscard]] bool wants_render() const { return render_scene_path.has_value(); }

    // True iff `--serve` was specified on the command line.
    [[nodiscard]] bool wants_serve()  const { return serve; }
};

}
