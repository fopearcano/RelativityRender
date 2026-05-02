#include "core/CommandLine.h"

#include "core/Version.h"

#include <charconv>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace rr::core {

namespace {

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
                "--render-optix-relativity)";
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
        } else if (a == "--output") {
            if (!take_value(argc, argv, i, a, value, r.error_message)) {
                r.action = Action::Error;
                return r;
            }
            r.config.output_path.assign(value);
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
     || r.action == Action::RenderOptixRelativity) {
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
       << "                        -DRELATIVITYRENDER_ENABLE_OPTIX=ON "
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
