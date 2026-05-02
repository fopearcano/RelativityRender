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

    // Stage 19B.4: when true, render-* actions that support
    // post-process denoising additionally invoke the OptiX
    // denoiser on their AOV outputs and write
    // `output/denoised.ppm` alongside the standard outputs.
    // Set by the `--denoise` CLI flag. Per DENOISER_PLAN
    // §9.2, this is the manual-trigger mode; no action is
    // forced into automatic mode by it. Actions that do not
    // expose Beauty / Albedo / Normal AOVs (e.g. the bare
    // `--render-scene` / `--render-pathtrace`) silently
    // ignore the flag - the user gets the un-denoised
    // output exactly as today.
    bool        denoise_enabled = false;

    // Returns an empty string when the configuration is internally
    // consistent. Otherwise returns a human-readable description of
    // the first problem.
    [[nodiscard]] std::string validate() const;
};

}
