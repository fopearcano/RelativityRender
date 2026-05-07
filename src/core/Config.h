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

    // Stage 19E.2: artist-supplied observer-velocity magnitude
    // along the camera's forward axis (-Z) for the
    // `--render-demo` action. `--beta` is a *modifier* flag
    // (like `--denoise`); it is stored regardless of action so
    // the parser stays simple, but only `--render-demo` reads
    // it. Sentinel `-1.0f` means "the user did not pass
    // --beta"; the action substitutes its own default. The
    // value is the magnitude `|beta|` in c-units; the sign is
    // chosen by the action (the demo points the observer at
    // -Z, the camera's default forward direction). Range is
    // not clamped at parse time — the action passes the value
    // through `rr::relativity::clampBeta` when it is consumed,
    // which silently caps |beta| at 0.999999 per the existing
    // design (see tests/relativity_tests.cpp #6).
    float       beta             = -1.0f;

    // Modifier flag. Per-channel firefly clamp on the path
    // tracer's per-sample radiance. Read by
    // `--render-pathtrace` (CUDA dispatcher) and
    // `--render-optix-pathtrace` (OptiX dispatcher); other
    // actions ignore it. Default 0.0f matches
    // `PathTraceConfig::firefly_clamp`'s PT-P.21 default
    // exactly so a caller that does NOT pass
    // `--firefly-clamp` sees byte-identical behaviour with
    // the pre-CLI build. See `docs/FIREFLY_CLAMP_CLI_TASK.md`
    // §1 for the canonical contract; the `--firefly-clamp`
    // parser at `CommandLine.cpp` rejects negative values at
    // parse time so this field is always >= 0.
    float       firefly_clamp    = 0.0f;

    // Returns an empty string when the configuration is internally
    // consistent. Otherwise returns a human-readable description of
    // the first problem.
    [[nodiscard]] std::string validate() const;
};

}
