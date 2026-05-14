#include "core/CommandLine.h"

#include "core/Version.h"
#include "manifold/CoordinateChart.h"

#include <charconv>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace rr::core {

namespace {

// MANI-I.1 — Parse a kebab-case `--manifold-chart` value into the
// matching `rr::manifold::CoordinateChartType` enumerator. Returns
// `false` on unknown input; the parser arm produces an error
// message listing the legal names. Case-sensitive matching mirrors
// the rest of the CLI parser's strict-token contract.
bool parse_chart_type(std::string_view s,
                      rr::manifold::CoordinateChartType& out) {
    using rr::manifold::CoordinateChartType;
    if (s == "euclidean") {
        out = CoordinateChartType::Euclidean;               return true;
    }
    if (s == "schwarzschild-like") {
        out = CoordinateChartType::SchwarzschildLike;       return true;
    }
    if (s == "kruskal-like") {
        out = CoordinateChartType::KruskalLikePlaceholder;  return true;
    }
    if (s == "penrose-like") {
        out = CoordinateChartType::PenroseLike;             return true;
    }
    if (s == "kerr-like") {
        out = CoordinateChartType::KerrLikePlaceholder;     return true;
    }
    return false;
}

bool parse_int(std::string_view s, int& out) {
    int value = 0;
    const auto* end  = s.data() + s.size();
    const auto  res  = std::from_chars(s.data(), end, value);
    if (res.ec != std::errc{} || res.ptr != end) return false;
    out = value;
    return true;
}

// Take the next argv slot as the value for `flag`. Treats anything
// starting with "--" as another flag and refuses to swallow it,
// surfacing the missing-value as a parse error instead. Returns
// false on failure and populates `error`.
bool take_value(int argc, char** argv, int& i, std::string_view flag,
                std::string_view& value, std::string& error) {
    if (i + 1 >= argc) {
        error = "missing value after " + std::string(flag);
        return false;
    }
    std::string_view next = argv[i + 1];
    if (next.size() >= 2 && next[0] == '-' && next[1] == '-') {
        error = "missing value after " + std::string(flag)
              + " (got " + std::string(next) + ")";
        return false;
    }
    value = next;
    ++i;
    return true;
}

bool set_action(CommandLine::Action& current, CommandLine::Action target,
                std::string& error) {
    if (current != CommandLine::Action::Default) {
        error = "cannot combine action flags (--help / --version / "
                "--device-info / --render / --scene-info / "
                "--scene-summary / --render-from-scene / "
                "--render-full-scene / --render-rng-test / "
                "--render-accumulation-test / --render-pathtrace / "
                "--render-gradient / --render-rays / --render-sphere / "
                "--render-relativistic / --render-scene / "
                "--render-triangle / --render-mesh-scene / "
                "--render-material-scene / --render-direct-lighting / "
                "--render-texture-sample-test / "
                "--render-textured-material / --render-aovs / "
                "--server / --render-optix-test / "
                "--render-optix-triangle / "
                "--render-optix-relativity / "
                "--render-optix-raygen / "
                "--render-optix-mesh-scene / "
                "--render-optix-material-scene / "
                "--render-optix-pathtrace / "
                "--render-optix-direct-lighting / "
                "--render-optix-shadow-test / "
                "--render-optix-textured-material / "
                "--render-optix-aovs / "
                "--render-optix-denoise / "
                "--render-denoise / "
                "--render-demo)";
        return false;
    }
    current = target;
    return true;
}

}  // namespace

CommandLine::ParseResult CommandLine::parse(int argc, char** argv) {
    ParseResult r;

    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        std::string_view value;

        if (a == "--help" || a == "-h") {
            if (!set_action(r.action, Action::Help, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--version") {
            if (!set_action(r.action, Action::Version, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--device-info") {
            if (!set_action(r.action, Action::DeviceInfo, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render") {
            if (!set_action(r.action, Action::Render, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            r.config.scene_path.assign(value);
        } else if (a == "--scene-info") {
            if (!set_action(r.action, Action::SceneInfo, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            r.config.scene_path.assign(value);
        } else if (a == "--scene-summary") {
            if (!set_action(r.action, Action::SceneSummary, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            r.config.scene_path.assign(value);
        } else if (a == "--render-from-scene") {
            if (!set_action(r.action, Action::RenderFromScene,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            r.config.scene_path.assign(value);
        } else if (a == "--render-full-scene") {
            if (!set_action(r.action, Action::RenderFullScene,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            r.config.scene_path.assign(value);
        } else if (a == "--render-rng-test") {
            if (!set_action(r.action, Action::RenderRngTest,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-accumulation-test") {
            if (!set_action(r.action, Action::RenderAccumulationTest,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-pathtrace") {
            if (!set_action(r.action, Action::RenderPathtrace,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            r.config.scene_path.assign(value);
        } else if (a == "--render-gradient") {
            if (!set_action(r.action, Action::RenderGradient,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-rays") {
            if (!set_action(r.action, Action::RenderRays,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-sphere") {
            if (!set_action(r.action, Action::RenderSphere,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-relativistic") {
            if (!set_action(r.action, Action::RenderRelativistic,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-demo") {
            // Stage 19E.2: smallest-meaningful-relativistic-render
            // demo. Mutually exclusive with the other render-*
            // actions; reads the optional `--beta` modifier.
            if (!set_action(r.action, Action::RenderDemo,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-scene") {
            if (!set_action(r.action, Action::RenderScene,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-triangle") {
            if (!set_action(r.action, Action::RenderTriangle,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-mesh-scene") {
            if (!set_action(r.action, Action::RenderMeshScene,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-material-scene") {
            if (!set_action(r.action, Action::RenderMaterialScene,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-direct-lighting") {
            if (!set_action(r.action, Action::RenderDirectLighting,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-texture-sample-test") {
            if (!set_action(r.action, Action::RenderTextureSampleTest,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-textured-material") {
            if (!set_action(r.action, Action::RenderTexturedMaterial,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-aovs") {
            if (!set_action(r.action, Action::RenderAOVs,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--server") {
            if (!set_action(r.action, Action::Server,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-optix-test") {
            if (!set_action(r.action, Action::RenderOptixTest,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-optix-triangle") {
            if (!set_action(r.action, Action::RenderOptixTriangle,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-optix-relativity") {
            if (!set_action(r.action, Action::RenderOptixRelativity,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-optix-raygen") {
            if (!set_action(r.action, Action::RenderOptixRaygen,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-optix-mesh-scene") {
            // Stage 20F: OptiX mesh-scene render. Like
            // --render-pathtrace / --render-from-scene, takes a
            // .rrscene path as its argument; the loaded
            // Scene's first non-empty mesh is uploaded to a GAS
            // and rendered through the existing OptiX pipeline.
            if (!set_action(r.action, Action::RenderOptixMeshScene,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            r.config.scene_path.assign(value);
        } else if (a == "--render-optix-material-scene") {
            // Stage 20G: OptiX material-scene render. Same shape
            // as --render-optix-mesh-scene; the picked mesh's
            // material data is uploaded into the hit-group SBT
            // record and the closest-hit emits baseColor +
            // emission instead of normal-as-color.
            if (!set_action(r.action, Action::RenderOptixMaterialScene,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            r.config.scene_path.assign(value);
        } else if (a == "--render-optix-pathtrace") {
            // Stage 20I: OptiX minimum-viable path tracer. Same
            // <file> argument shape as --render-optix-mesh-scene;
            // the dispatcher runs the launch twice (spp=1 then
            // spp=16) and writes two PPMs.
            if (!set_action(r.action, Action::RenderOptixPathtrace,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            r.config.scene_path.assign(value);
        } else if (a == "--render-optix-direct-lighting") {
            // Stage 20K: OptiX direct-lighting render. Same
            // <file> argument shape; the closest-hit evaluates
            // direct lighting (point + directional + emission +
            // environment ambient) at the primary hit. Single
            // launch, no path tracing.
            if (!set_action(r.action, Action::RenderOptixDirectLighting,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            r.config.scene_path.assign(value);
        } else if (a == "--render-optix-shadow-test") {
            // Stage 20L: OptiX direct-lighting render WITH
            // shadow rays. Same <file> argument as
            // --render-optix-direct-lighting; the closest-hit
            // additionally traces a shadow ray per light to
            // gate direct contributions on visibility.
            if (!set_action(r.action, Action::RenderOptixShadowTest,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            r.config.scene_path.assign(value);
        } else if (a == "--render-optix-textured-material") {
            // Stage 20M: OptiX textured-material render.
            // Mirrors the CUDA --render-textured-material
            // shape: takes NO scene argument (the dispatcher
            // builds the procedural textured-quad scene + 2x2
            // reference texture inline) and writes
            // output/optix_textured_material.ppm.
            if (!set_action(r.action, Action::RenderOptixTexturedMaterial,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-optix-aovs") {
            // Stage 20N: OptiX AOV render. Mirrors the CUDA
            // --render-aovs shape: takes NO scene argument
            // (the dispatcher builds a procedural multi-light
            // scene inline) and writes the six AOVs to
            // output/optix_aov_*.ppm.
            if (!set_action(r.action, Action::RenderOptixAovs,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-optix-denoise") {
            // Stage 21D.6: end-to-end run of the new
            // OptixDenoiser::denoise() API. Mirrors the
            // --render-denoise shape: takes NO scene
            // argument (the dispatcher builds the same
            // 4-sphere demo scene inline) and writes
            // output/denoised.ppm.
            if (!set_action(r.action, Action::RenderOptixDenoise,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--render-denoise") {
            if (!set_action(r.action, Action::RenderDenoise,
                            r.error_message)) {
                r.action = Action::Error;
                return r;
            }
        } else if (a == "--denoise") {
            // Stage 19B.4 modifier flag. NOT an action - it
            // does not call set_action; combining it with any
            // action flag is allowed (and required for it to
            // do anything). Sets the denoise_enabled bit on
            // Config; the per-action handler decides whether
            // to honour it.
            r.config.denoise_enabled = true;
        } else if (a == "--enable-nee") {
            // NEE.5 modifier flag. NOT an action - it does
            // not call set_action; combining it with any
            // action flag is allowed (and required for it to
            // do anything). Sets the enable_nee bit on
            // Config; only `--render-pathtrace` (CUDA) and
            // `--render-optix-pathtrace` (OptiX) read it.
            // Other actions ignore it. Presence-only switch
            // (no value-take), mirroring `--denoise` above.
            // Per `docs/PATH_TRACER_NEE_AUDIT.md` §3.2,
            // NEE.4's OptiX-side mirror (commit b29daae)
            // unblocks the field-flip contract: at default
            // `false` both backends are byte-identical with
            // the pre-NEE build; at `true` both backends
            // produce convergence-equivalent NEE renders
            // (once the OptiX dispatcher consumes the field;
            // deferred to a follow-up slice).
            r.config.enable_nee = true;
        } else if (a == "--output") {
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            r.config.output_path.assign(value);
        } else if (a == "--beta") {
            // Stage 19E.2 modifier flag. Stores the artist-supplied
            // observer-velocity magnitude on Config; only the
            // `--render-demo` action reads it. Negative values are
            // accepted at parse time (the action interprets the sign
            // relative to its forward axis and clamps the magnitude
            // via `rr::relativity::clampBeta`).
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            float beta_value = 0.0f;
            const auto* end = value.data() + value.size();
            const auto  res = std::from_chars(value.data(), end, beta_value);
            if (res.ec != std::errc{} || res.ptr != end) {
                r.action        = Action::Error;
                r.error_message = "invalid float for --beta: "
                                + std::string(value);
                return r;
            }
            r.config.beta = beta_value;
        } else if (a == "--firefly-clamp") {
            // Modifier flag. Stores the per-channel firefly
            // clamp value on Config; only `--render-pathtrace`
            // (CUDA dispatcher) and `--render-optix-pathtrace`
            // (OptiX dispatcher) read it. Negative values are
            // rejected at parse time per
            // FIREFLY_CLAMP_CLI_TASK.md §1.2 option A — the
            // renderer's existing lower-bound rejections at
            // PathTracer.cpp:84, CudaPathTracer.cu:282,
            // OptixRenderer.cpp:1243+1502 are defence in depth,
            // but rejecting at parse time produces a clearer
            // operator error message + faster exit (no GPU
            // resource allocated).
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            float clamp_value = 0.0f;
            const auto* end = value.data() + value.size();
            const auto  res = std::from_chars(value.data(), end, clamp_value);
            if (res.ec != std::errc{} || res.ptr != end) {
                r.action        = Action::Error;
                r.error_message = "invalid float for --firefly-clamp: "
                                + std::string(value);
                return r;
            }
            if (clamp_value < 0.0f) {
                r.action        = Action::Error;
                r.error_message = "--firefly-clamp must be >= 0 (got "
                                + std::string(value) + ")";
                return r;
            }
            r.config.firefly_clamp = clamp_value;
        } else if (a == "--manifold-enable") {
            // MANI-I.1 modifier flag (NOT an action). Sets
            // `r.config.manifold.enabled = true`. Mirrors the
            // `--denoise` / `--enable-nee` presence-only shape.
            // Default off matches the pre-pivot renderer byte-
            // for-byte; flipping the field alone is a no-op
            // until MANI-I.3 wires the GPU consumer.
            r.config.manifold.enabled = true;
        } else if (a == "--manifold-chart") {
            // MANI-I.1 modifier flag. Takes one value naming the
            // chart family. Legal names (case-sensitive,
            // kebab-case): euclidean, schwarzschild-like,
            // kruskal-like, penrose-like, kerr-like. Maps to the
            // `CoordinateChartType` enumerator on the active
            // `ManifoldMode`. Unknown values are a parse error
            // with a message listing the legal names.
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            if (!parse_chart_type(value, r.config.manifold.chart)) {
                r.action        = Action::Error;
                r.error_message = "invalid --manifold-chart value: "
                                + std::string(value)
                                + " (expected one of: euclidean, "
                                  "schwarzschild-like, kruskal-like, "
                                  "penrose-like, kerr-like)";
                return r;
            }
        } else if (a == "--manifold-strength") {
            // MANI-I.1 modifier flag. Takes one float. Sets
            // `r.config.manifold.strength`. Per `ManifoldMode`'s
            // contract the field's nominal range is `[0, 1]` but
            // values outside that range are not clamped at parse
            // time — the renderer may extrapolate (e.g. for
            // stylised over-deformation) at the artist's risk.
            // Only a non-finite / non-parseable string is
            // rejected here.
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            float strength = 0.0f;
            const auto* end = value.data() + value.size();
            const auto  res = std::from_chars(value.data(), end, strength);
            if (res.ec != std::errc{} || res.ptr != end) {
                r.action        = Action::Error;
                r.error_message = "invalid float for --manifold-strength: "
                                + std::string(value);
                return r;
            }
            r.config.manifold.strength = strength;
        } else if (a == "--manifold-debug") {
            // MANI-I.1 modifier flag (NOT an action). Sets
            // `r.config.manifold.debug_visualization = true`.
            // Reserved for the MANI-I.4 debug coordinate-warp
            // AOV; no observable behaviour change this slice.
            r.config.manifold.debug_visualization = true;
        } else if (a == "--width") {
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            if (!parse_int(value, r.config.width)) {
                r.action = Action::Error;
                r.error_message = "invalid integer for --width: "
                                + std::string(value);
                return r;
            }
        } else if (a == "--height") {
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            if (!parse_int(value, r.config.height)) {
                r.action = Action::Error;
                r.error_message = "invalid integer for --height: "
                                + std::string(value);
                return r;
            }
        } else {
            r.action        = Action::Error;
            r.error_message = "unknown argument: " + std::string(a);
            return r;
        }
    }

    // Validate config for actions that would actually use it. --help /
    // --version / --device-info are pure information requests and must
    // not fail on, e.g., a defaulted dimension being misconfigured.
    if (r.action == Action::Default
     || r.action == Action::Render
     || r.action == Action::SceneInfo
     || r.action == Action::SceneSummary
     || r.action == Action::RenderFromScene
     || r.action == Action::RenderFullScene
     || r.action == Action::RenderRngTest
     || r.action == Action::RenderAccumulationTest
     || r.action == Action::RenderPathtrace
     || r.action == Action::RenderGradient
     || r.action == Action::RenderRays
     || r.action == Action::RenderSphere
     || r.action == Action::RenderRelativistic
     || r.action == Action::RenderScene
     || r.action == Action::RenderTriangle
     || r.action == Action::RenderMeshScene
     || r.action == Action::RenderMaterialScene
     || r.action == Action::RenderDirectLighting
     || r.action == Action::RenderTextureSampleTest
     || r.action == Action::RenderTexturedMaterial
     || r.action == Action::RenderAOVs
     || r.action == Action::RenderOptixTest
     || r.action == Action::RenderOptixTriangle
     || r.action == Action::RenderOptixRelativity
     || r.action == Action::RenderOptixRaygen
     || r.action == Action::RenderOptixMeshScene
     || r.action == Action::RenderOptixMaterialScene
     || r.action == Action::RenderOptixPathtrace
     || r.action == Action::RenderOptixDirectLighting
     || r.action == Action::RenderOptixShadowTest
     || r.action == Action::RenderOptixTexturedMaterial
     || r.action == Action::RenderOptixAovs
     || r.action == Action::RenderOptixDenoise
     || r.action == Action::RenderDenoise) {
        if (auto err = r.config.validate(); !err.empty()) {
            r.action        = Action::Error;
            r.error_message = std::move(err);
        }
    }

    return r;
}

std::string CommandLine::usage(std::string_view argv0) {
    std::ostringstream os;
    os << "Usage: " << argv0 << " [options]\n"
       << "\n"
       << "Options:\n"
       << "  --help                Print this message and exit.\n"
       << "  --version             Print version and exit.\n"
       << "  --device-info         Print GPU device info.\n"
       << "  --render <scene>      Run the renderer on the given "
                                  "scene file (placeholder).\n"
       << "  --scene-info <file>   Load a .rrscene file, print the "
                                  "parsed render settings, and exit.\n"
       << "                        No render. Works without CUDA.\n"
       << "  --scene-summary <file>\n"
       << "                        Load a .rrscene file and print a "
                                  "compact summary (resolution,\n"
       << "                        material/sphere/mesh/light counts, "
                                  "|beta|). No render. Works\n"
       << "                        without CUDA.\n"
       << "  --render-from-scene <file>\n"
       << "                        Load a .rrscene file and render its "
                                  "sphere scene on the GPU.\n"
       << "                        CPU parses + uploads; the kernel "
                                  "produces every pixel. Meshes\n"
       << "                        are skipped in this slice. Resolution "
                                  "comes from the scene's\n"
       << "                        render_settings; --width / --height "
                                  "are ignored. Requires CUDA.\n"
       << "  --render-full-scene <file>\n"
       << "                        Like --render-from-scene, but also "
                                  "uploads the first visible\n"
       << "                        non-empty mesh (single-mesh GpuScene "
                                  "slot today; multi-mesh\n"
       << "                        support is a future slice). Default "
                                  "output\n"
       << "                        \"output/from_scene_full.ppm\". "
                                  "Requires CUDA.\n"
       << "  --render-rng-test     Run the Stage 11A GPU RNG / sampling "
                                  "validation kernel. Splits\n"
       << "                        the framebuffer into four quadrants, "
                                  "each exercising one of\n"
       << "                        next_float / next_vec2 / "
                                  "sample_uniform_hemisphere /\n"
       << "                        sample_cosine_hemisphere. Default "
                                  "output\n"
       << "                        \"output/gpu_rng_test.ppm\". "
                                  "Requires CUDA.\n"
       << "  --render-accumulation-test\n"
       << "                        Stage 11B progressive-accumulation "
                                  "validation. Loops 64 sample\n"
       << "                        frames of per-pixel random RGB "
                                  "through an AccumulationBuffer\n"
       << "                        and resolves to a display image. "
                                  "Result converges to mid-gray.\n"
       << "                        Default output "
                                  "\"output/gpu_accumulation_test.ppm\". "
                                  "Requires CUDA.\n"
       << "  --render-pathtrace <file>\n"
       << "                        Stage 11C minimal diffuse GPU path "
                                  "tracer. Loads the .rrscene\n"
       << "                        file and runs the path tracer at "
                                  "spp = 1 and spp = 16, writing\n"
       << "                        \"output/pathtrace_spp_1.ppm\" "
                                  "and \"output/pathtrace_spp_16.ppm\".\n"
       << "                        Resolution from render_settings; "
                                  "--width / --height ignored.\n"
       << "                        Requires CUDA.\n"
       << "  --render-gradient     Run the GPU UV-gradient diagnostic "
                                  "and save it (requires CUDA).\n"
       << "  --render-rays         Run the GPU camera-ray "
                                  "visualisation and save it "
                                  "(requires CUDA).\n"
       << "  --render-sphere       Run the GPU single-sphere "
                                  "intersection diagnostic "
                                  "(requires CUDA).\n"
       << "  --render-relativistic Run the relativistic single-sphere "
                                  "pipeline at four observer\n"
       << "                        speeds (beta = 0.00, 0.25, 0.75, "
                                  "0.95) and write the four PPMs\n"
       << "                        into output/. Requires CUDA.\n"
       << "  --render-demo         Stage 19E.2 smallest-meaningful "
                                  "relativistic-render demo: one\n"
       << "                        sphere + one diffuse material + "
                                  "one environment light + camera\n"
       << "                        with --beta-configurable observer. "
                                  "Outputs output/demo_beauty.ppm\n"
       << "                        and output/demo_doppler.ppm. "
                                  "Defaults to beta = 0.7 if --beta\n"
       << "                        is not given. Requires CUDA.\n"
       << "  --render-scene        Render a built-in multi-sphere scene "
                                  "via the GpuScene upload\n"
       << "                        path (requires CUDA).\n"
       << "  --render-triangle     Render a single uploaded triangle "
                                  "on the GPU (requires CUDA).\n"
       << "  --render-mesh-scene   Render the multi-sphere scene plus "
                                  "a triangle-mesh quad with\n"
       << "                        sphere / triangle closest-hit "
                                  "competition (requires CUDA).\n"
       << "  --render-material-scene\n"
       << "                        Render the multi-sphere + quad "
                                  "scene with per-object materials\n"
       << "                        uploaded to the GPU "
                                  "(requires CUDA).\n"
       << "  --render-direct-lighting\n"
       << "                        Render the multi-sphere + quad "
                                  "scene with materials AND lights\n"
       << "                        uploaded; the kernel evaluates "
                                  "direct lighting (point +\n"
       << "                        directional, no shadows) plus an "
                                  "environment ambient,\n"
       << "                        with emission and the relativistic "
                                  "Doppler / searchlight\n"
       << "                        pipeline applied on top "
                                  "(requires CUDA).\n"
       << "  --render-texture-sample-test\n"
       << "                        Stage 13B.2 GPU texture-sampling "
                                  "validation: synthesise a 2x2\n"
       << "                        RGBA8 four-colour pattern, upload "
                                  "it via GpuTexture, and launch the\n"
       << "                        kernel that samples it per-pixel "
                                  "via sampleTextureNearest. Default\n"
       << "                        output output/gpu_texture_sample_test.ppm "
                                  "(requires CUDA).\n"
       << "  --render-textured-material\n"
       << "                        Stage 13B.3 material-texture "
                                  "integration: build the multi-sphere\n"
       << "                        + quad scene; the quad's neutral "
                                  "material is replaced with one whose\n"
       << "                        useBaseColorTexture flag is set "
                                  "and baseColorTextureId points at\n"
       << "                        an uploaded 2x2 four-colour "
                                  "reference texture. Default output\n"
       << "                        output/gpu_textured_material.ppm "
                                  "(requires CUDA).\n"
       << "  --render-aovs         Stage 14A.3 AOV / render-pass "
                                  "validation: build a lit multi-sphere\n"
       << "                        + quad scene with non-zero observer "
                                  "velocity, allocate one\n"
       << "                        GpuAOVBuffer per declared AOVType, "
                                  "and run the render kernel writing\n"
       << "                        Beauty / Normal / Depth / Albedo / "
                                  "DopplerFactor / SearchlightFactor.\n"
       << "                        Outputs output/aov_beauty.ppm, "
                                  "output/aov_normal.ppm,\n"
       << "                        output/aov_depth.ppm, "
                                  "output/aov_albedo.ppm,\n"
       << "                        output/aov_doppler.ppm, "
                                  "output/aov_searchlight.ppm. --output\n"
       << "                        is ignored (requires CUDA).\n"
       << "  --server              Stage 15A.2 renderer-server mode: "
                                  "bind a TCP listen socket\n"
       << "                        to localhost:7777 and accept clients "
                                  "one at a time. Supported\n"
       << "                        commands: ping -> pong. Press Ctrl-C "
                                  "(SIGINT) or send SIGTERM\n"
       << "                        to stop. Pure host code; runs without CUDA.\n"
       << "  --render-optix-test   Stage 17A.3 OptiX pipeline-skeleton "
                                  "validation: build raygen + miss\n"
       << "                        pipeline + minimal SBT, run "
                                  "optixLaunch which writes a flat\n"
       << "                        colour to the framebuffer, save "
                                  "PPM. Default output\n"
       << "                        output/optix_test.ppm. Requires\n"
       << "                        -DRR_ENABLE_OPTIX=ON "
                                  "+ CUDA Toolkit + OptiX SDK.\n"
       << "  --render-optix-triangle\n"
       << "                        Stage 17A.4 OptiX triangle render: "
                                  "single triangle GAS + closest-hit\n"
       << "                        normal-as-colour shading + miss "
                                  "sky gradient. Visually matches the\n"
       << "                        CUDA --render-triangle output. "
                                  "Default output\n"
       << "                        output/optix_triangle.ppm. Same "
                                  "OptiX requirements as above.\n"
       << "  --render-optix-relativity\n"
       << "                        Stage 17A.5 OptiX relativistic "
                                  "render: same single-triangle GAS as\n"
       << "                        --render-optix-triangle, but with "
                                  "a non-zero observer velocity\n"
       << "                        (beta = 0.5 along -Z). The OptiX "
                                  "raygen Lorentz-aberrates the\n"
       << "                        primary ray; closest-hit / miss "
                                  "apply the Doppler colour shift +\n"
       << "                        the bolometric searchlight scale. "
                                  "Default output\n"
       << "                        output/optix_relativity.ppm. Same "
                                  "OptiX requirements as above.\n"
       << "  --render-optix-raygen Stage 20C OptiX raygen / miss baseline. "
                                  "Builds a tiny triangle GAS\n"
       << "                        placed BEHIND the camera (z = +5), so "
                                  "every primary ray misses\n"
       << "                        the geometry and the miss program runs "
                                  "per pixel - producing the\n"
       << "                        sky-gradient environment colour. No "
                                  "geometry visible, no\n"
       << "                        closest-hit firing. Default output "
                                  "output/optix_raygen.ppm.\n"
       << "                        Same OptiX requirements as above.\n"
       << "  --render-optix-mesh-scene <file>\n"
       << "                        Stage 20F OptiX mesh-scene render. "
                                  "Loads <file> via SceneLoader,\n"
       << "                        builds an OptiX GAS from the first "
                                  "non-empty mesh in scene.meshes\n"
       << "                        (positions extracted to a tightly-"
                                  "packed float3 buffer the GAS\n"
       << "                        builder requires), uses the scene's "
                                  "camera, runs the existing\n"
       << "                        raygen + miss + closest-hit pipeline "
                                  "(normal-as-color shading +\n"
       << "                        gradient sky). No materials beyond "
                                  "basic color, no path tracing.\n"
       << "                        Default output "
                                  "output/optix_mesh_scene.ppm. Same\n"
       << "                        OptiX requirements as above.\n"
       << "  --render-optix-material-scene <file>\n"
       << "                        Stage 20G OptiX material-scene render. "
                                  "Same mesh-scene plumbing as\n"
       << "                        --render-optix-mesh-scene, but the "
                                  "picked mesh's material data\n"
       << "                        (MaterialParams baseColor + "
                                  "emissionColor + emissionStrength)\n"
       << "                        is copied into the hit-group SBT "
                                  "record and the closest-hit\n"
       << "                        emits baseColor + emission instead of "
                                  "normal-as-color. No textures,\n"
       << "                        no path tracing. Default output "
                                  "output/optix_material_scene.ppm.\n"
       << "                        Same OptiX requirements as above.\n"
       << "  --render-optix-pathtrace <file>\n"
       << "                        Stage 20I minimum-viable OptiX path "
                                  "tracer. Loads <file>, builds\n"
       << "                        a path-tracer pipeline (raygen / miss "
                                  "/ closest-hit entries that\n"
       << "                        iterate spp samples + max_bounces "
                                  "bounces in raygen), runs the\n"
       << "                        launch at spp=1 then spp=16, writes "
                                  "two PPMs:\n"
       << "                        output/optix_pathtrace_spp1.ppm + "
                                  "output/optix_pathtrace_spp16.ppm.\n"
       << "                        Diffuse Lambert BSDF only (no NEE / "
                                  "MIS / shadows / textures).\n"
       << "                        Same OptiX requirements as above.\n"
       << "  --render-optix-direct-lighting <file>\n"
       << "                        Stage 20K OptiX direct-lighting "
                                  "render. Loads <file>, builds an OptiX\n"
       << "                        GAS from the first non-empty mesh, "
                                  "uploads scene.lights, and runs\n"
       << "                        the closest-hit's direct-lighting "
                                  "branch (point + directional +\n"
       << "                        emission + environment ambient). No "
                                  "path tracing, no shadow rays\n"
       << "                        (matches CUDA --render-direct-"
                                  "lighting precedent: shadows\n"
       << "                        deferred). Default output "
                                  "output/optix_direct_lighting.ppm.\n"
       << "                        Same OptiX requirements as above.\n"
       << "  --render-optix-shadow-test <file>\n"
       << "                        Stage 20L OptiX direct-lighting "
                                  "render WITH shadow rays. Same\n"
       << "                        scene-load + GAS-build path as "
                                  "--render-optix-direct-lighting,\n"
       << "                        but the closest-hit additionally "
                                  "traces a shadow ray per light\n"
       << "                        before accumulating its "
                                  "contribution. Single ray type;\n"
       << "                        OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT "
                                  "| TERMINATE_ON_FIRST_HIT +\n"
       << "                        missSbtIndex = 1 routes the shadow "
                                  "trace through the dedicated\n"
       << "                        __miss__shadow program. Default "
                                  "output output/optix_shadow_test.ppm.\n"
       << "                        Same OptiX requirements as above.\n"
       << "  --render-optix-textured-material\n"
       << "                        Stage 20M OptiX textured-material "
                                  "render. Builds a procedural\n"
       << "                        textured-quad scene + 2x2 reference "
                                  "texture inline (mirrors\n"
       << "                        CUDA --render-textured-material), "
                                  "uploads UVs + indices +\n"
       << "                        per-texture pixel buffers + a "
                                  "DeviceTextureView array via\n"
       << "                        OptixLaunchParams, runs the "
                                  "closest-hit material-flat branch\n"
       << "                        with nearest-neighbour texture "
                                  "sampling. No advanced filtering.\n"
       << "                        Default output "
                                  "output/optix_textured_material.ppm.\n"
       << "                        Same OptiX requirements as above.\n"
       << "  --render-optix-aovs   Stage 20N OptiX AOV render. Builds a "
                                  "small procedural multi-light\n"
       << "                        mesh-scene inline (mirrors the CUDA "
                                  "--render-aovs surface), allocates\n"
       << "                        six per-pixel device buffers, "
                                  "threads them through OptixLaunchParams,\n"
       << "                        and runs the existing direct-lighting "
                                  "closest-hit; the raygen / miss /\n"
       << "                        closest-hit programs write Beauty / "
                                  "Normal / Depth / Albedo /\n"
       << "                        DopplerFactor / SearchlightFactor. "
                                  "Outputs:\n"
       << "                        output/optix_aov_beauty.ppm, "
                                  "output/optix_aov_normal.ppm,\n"
       << "                        output/optix_aov_depth.ppm, "
                                  "output/optix_aov_albedo.ppm,\n"
       << "                        output/optix_aov_doppler.ppm, "
                                  "output/optix_aov_searchlight.ppm.\n"
       << "                        Same OptiX requirements as above.\n"
       << "  --render-optix-denoise\n"
       << "                        Stage 21D.6 first end-to-end run of "
                                  "the new OptixDenoiser::denoise()\n"
       << "                        API. Builds the same 4-sphere demo "
                                  "scene as --render-denoise, runs the\n"
       << "                        AOV pipeline (Beauty / Albedo / "
                                  "Normal), then drives the new\n"
       << "                        denoise_and_save_ppm helper "
                                  "(denoise -> download -> save). On\n"
       << "                        denoiser failure the noisy Beauty "
                                  "AOV is saved as a fallback per the\n"
       << "                        Stage 21A.7 contract. Default output "
                                  "output/denoised.ppm.\n"
       << "                        Requires both -DRR_ENABLE_CUDA=ON "
                                  "and -DRR_ENABLE_OPTIX=ON plus a host\n"
       << "                        with the CUDA Toolkit + OptiX SDK "
                                  "installed at runtime.\n"
       << "  --render-denoise      Stage 19B.3 OptiX denoiser end-to-end "
                                  "fixture. Builds a small\n"
       << "                        4-sphere demo scene + renders it via "
                                  "render_scene_with_aovs to\n"
       << "                        populate Beauty / Albedo / Normal AOV "
                                  "device buffers, then runs the\n"
       << "                        OptiX denoiser over them "
                                  "(optixDenoiserComputeMemoryResources ->\n"
       << "                        optixDenoiserSetup -> "
                                  "optixDenoiserInvoke). Default output\n"
       << "                        output/denoised.ppm. Requires both "
                                  "CUDA and OptiX SDK.\n"
       << "  --denoise             Stage 19B.4 modifier flag (not an "
                                  "action). When combined with an\n"
       << "                        AOV-aware action (today: --render-aovs), "
                                  "run the OptiX denoiser on\n"
       << "                        the action's Beauty / Albedo / Normal "
                                  "AOV buffers and write the\n"
       << "                        result to output/denoised.ppm "
                                  "alongside the standard outputs.\n"
       << "                        Silently ignored by actions that do "
                                  "not expose those AOVs.\n"
       << "                        Same CUDA + OptiX SDK requirements as "
                                  "--render-denoise.\n"
       << "  --output <path>       Write the rendered image to <path>.\n"
       << "                        Default for --render-gradient is "
                                  "output/gpu_gradient.ppm;\n"
       << "                        default for --render-rays     is "
                                  "output/gpu_camera_rays.ppm;\n"
       << "                        default for --render-sphere   is "
                                  "output/gpu_sphere.ppm;\n"
       << "                        default for --render-scene    is "
                                  "output/gpu_scene_spheres.ppm;\n"
       << "                        default for --render-triangle is "
                                  "output/gpu_triangle.ppm;\n"
       << "                        default for --render-mesh-scene is "
                                  "output/gpu_mesh_scene.ppm;\n"
       << "                        default for --render-material-scene is "
                                  "output/gpu_material_scene.ppm;\n"
       << "                        default for --render-direct-lighting is "
                                  "output/gpu_direct_lighting.ppm.\n"
       << "                        Ignored for --render-relativistic.\n"
       << "  --beta   <float>      Modifier flag (not an action). Sets "
                                  "the observer's velocity\n"
       << "                        magnitude in c-units. Consumers:\n"
       << "                          - --render-demo (Stage 19E.2): "
                                  "default 0.7 if --beta unset.\n"
       << "                          - --render-optix-relativity "
                                  "(Stage 20H): default 0.5 if --beta\n"
       << "                            unset; default output path "
                                  "becomes\n"
       << "                            output/optix_relativity_beta{NNN}.ppm "
                                  "when --beta is set\n"
       << "                            (e.g. --beta 0.75 -> "
                                  "..._beta075.ppm).\n"
       << "                        Silently ignored by every other "
                                  "action. Magnitude is clamped\n"
       << "                        to <= 0.999999 by "
                                  "rr::relativity::clampBeta. The\n"
       << "                        sign and direction are chosen by "
                                  "the consumer (both consumers\n"
       << "                        point the observer along the "
                                  "camera's forward axis -Z).\n"
       << "  --firefly-clamp <float>\n"
       << "                        Modifier flag (not an action). Sets "
                                  "the per-channel firefly\n"
       << "                        clamp on the path tracer's "
                                  "per-sample radiance. Default 0.0\n"
       << "                        disables the clamp (the integrator "
                                  "stays unbiased); values > 0\n"
       << "                        enable a `fminf(radiance.x|y|z, "
                                  "firefly_clamp)` per channel\n"
       << "                        symmetrically on both CUDA and "
                                  "OptiX backends. Read by\n"
       << "                        --render-pathtrace and "
                                  "--render-optix-pathtrace; ignored\n"
       << "                        by every other action. Negative "
                                  "values are rejected at parse\n"
       << "                        time (\"--firefly-clamp must be "
                                  ">= 0\").\n"
       << "  --enable-nee          NEE.5 modifier flag (not an action). "
                                  "Enables\n"
       << "                        explicit direct-light sampling (Next "
                                  "Event\n"
       << "                        Estimation) at every bounce vertex of "
                                  "the path\n"
       << "                        tracer. Default off matches the pre-"
                                  "NEE.5\n"
       << "                        emission + environment-only behaviour "
                                  "byte-for-byte.\n"
       << "                        Read by --render-pathtrace and\n"
       << "                        --render-optix-pathtrace; ignored "
                                  "by every other\n"
       << "                        action.\n"
       << "                        Light-type scope: Point + Directional "
                                  "contribute;\n"
       << "                        Area / Environment are placeholder and "
                                  "contribute\n"
       << "                        zero through the NEE branch (no MIS "
                                  "yet).\n"
       << "  --manifold-enable     MANI-I.1 modifier flag (not an action). "
                                  "Engages the\n"
       << "                        Manifold Core's manifold-rendering "
                                  "surface. Default off\n"
       << "                        matches the pre-pivot renderer "
                                  "byte-for-byte. The other\n"
       << "                        --manifold-* flags configure the mode; "
                                  "the renderer is the\n"
       << "                        consumer that reads them (deferred to "
                                  "MANI-I.3 and later).\n"
       << "  --manifold-chart <name>\n"
       << "                        MANI-I.1 modifier flag. Selects the "
                                  "chart family. Accepted\n"
       << "                        values (case-sensitive):\n"
       << "                          euclidean           (default; "
                                  "identity case, no warp)\n"
       << "                          schwarzschild-like  (reserved; no "
                                  "kernel yet)\n"
       << "                          kruskal-like        (reserved; no "
                                  "kernel yet)\n"
       << "                          penrose-like        (reserved; no "
                                  "kernel yet)\n"
       << "                          kerr-like           (reserved; no "
                                  "kernel yet)\n"
       << "                        The *-like names match the "
                                  "CoordinateChartType enum's\n"
       << "                        \"*Like / *LikePlaceholder\" "
                                  "convention per master rule #3\n"
       << "                        (no fake stubs). Unknown values "
                                  "are a parse error.\n"
       << "  --manifold-strength <float>\n"
       << "                        MANI-I.1 modifier flag. Sets the "
                                  "strength scalar on the\n"
       << "                        active ManifoldMode in nominal [0, "
                                  "1]; out-of-range values\n"
       << "                        pass through (the renderer may "
                                  "extrapolate per the\n"
       << "                        ManifoldMode::strength contract). "
                                  "Default 0 means \"no\n"
       << "                        chart effect\" even when "
                                  "--manifold-enable is set.\n"
       << "  --manifold-debug      MANI-I.1 modifier flag (not an action). "
                                  "Sets the\n"
       << "                        ManifoldMode::debug_visualization "
                                  "toggle. Reserved for the\n"
       << "                        MANI-I.4 debug coordinate-warp AOV; "
                                  "no observable\n"
       << "                        behaviour yet.\n"
       << "  --width  <int>        Render width in pixels "
                                  "(default 1280).\n"
       << "  --height <int>        Render height in pixels "
                                  "(default 720).\n";
    return os.str();
}

std::string CommandLine::version_string() {
    return std::string(kProjectName) + " " + kVersionString;
}

}  // namespace rr::core
