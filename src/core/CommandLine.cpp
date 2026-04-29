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
                "--device-info / --render / --render-gradient / "
                "--render-rays / --render-sphere / "
                "--render-relativistic / --render-scene / "
                "--render-triangle / --render-mesh-scene / "
                "--render-material-scene / --render-direct-lighting)";
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
     || r.action == Action::RenderGradient
     || r.action == Action::RenderRays
     || r.action == Action::RenderSphere
     || r.action == Action::RenderRelativistic
     || r.action == Action::RenderScene
     || r.action == Action::RenderTriangle
     || r.action == Action::RenderMeshScene
     || r.action == Action::RenderMaterialScene
     || r.action == Action::RenderDirectLighting) {
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
