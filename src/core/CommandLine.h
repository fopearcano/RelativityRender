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
//   --scene-info <file>     Load a `.rrscene` file, print the parsed
//                           render settings (version + width/height
//                           + samples_per_pixel + max_depth +
//                           output_path), and exit. No render. Pure
//                           host code; works without CUDA. Stage
//                           10B.2 surface.
//   --render-gradient       Run the GPU UV-gradient diagnostic and
//                           save it to <output>. Requires CUDA.
//   --render-rays           Run the GPU camera-ray-direction
//                           visualisation and save it to <output>.
//                           Requires CUDA.
//   --render-sphere         Run the GPU single-sphere intersection
//                           diagnostic and save it to <output>.
//                           Requires CUDA.
//   --render-relativistic   Run the relativistic single-sphere
//                           pipeline at four observer speeds
//                           (beta = 0.00, 0.25, 0.75, 0.95) and
//                           write the four PPMs into output/.
//                           --output is ignored for this action.
//                           Requires CUDA.
//   --render-scene          Render a built-in multi-sphere scene
//                           via the GpuScene upload path and save
//                           it to <output>. Requires CUDA.
//   --render-triangle       Render a single uploaded triangle on
//                           the GPU and save it to <output>.
//                           Requires CUDA.
//   --render-mesh-scene     Render the built-in multi-sphere scene
//                           plus a triangle-mesh quad on the GPU,
//                           with sphere / triangle closest-hit
//                           competition. Requires CUDA.
//   --render-material-scene Render the multi-sphere + quad scene
//                           with per-object materials uploaded to
//                           the GPU; the kernel reads
//                           `materials[Hit::material_index]` for
//                           the base colour. Requires CUDA.
//   --render-direct-lighting
//                           Render the multi-sphere + quad scene
//                           with materials AND lights uploaded;
//                           the kernel evaluates direct lighting
//                           (point + directional, no shadows)
//                           plus an environment ambient. Emission
//                           and the relativistic Doppler /
//                           searchlight pipeline are applied on
//                           top. Requires CUDA.
//   --output <path>         Write the rendered image to <path>.
//                           Default for --render-gradient is
//                           "output/gpu_gradient.ppm";
//                           default for --render-rays     is
//                           "output/gpu_camera_rays.ppm";
//                           default for --render-sphere   is
//                           "output/gpu_sphere.ppm";
//                           default for --render-scene    is
//                           "output/gpu_scene_spheres.ppm";
//                           default for --render-triangle is
//                           "output/gpu_triangle.ppm";
//                           default for --render-mesh-scene is
//                           "output/gpu_mesh_scene.ppm";
//                           default for --render-material-scene is
//                           "output/gpu_material_scene.ppm";
//                           default for --render-direct-lighting is
//                           "output/gpu_direct_lighting.ppm".
//                           Ignored for --render-relativistic.
//   --width  <int>          Render width in pixels  (default 1280).
//   --height <int>          Render height in pixels (default 720).
//
// Action flags (--help / --version / --device-info / --render /
// --scene-info / --render-gradient / --render-rays / --render-sphere /
// --render-relativistic / --render-scene / --render-triangle /
// --render-mesh-scene / --render-material-scene /
// --render-direct-lighting) are mutually exclusive; combining them
// is a parse error. The remaining flags configure `Config` and are
// accepted regardless of action.

class CommandLine {
public:
    enum class Action {
        Default,        // no action flag given
        Help,
        Version,
        DeviceInfo,
        Render,
        SceneInfo,
        RenderGradient,
        RenderRays,
        RenderSphere,
        RenderRelativistic,
        RenderScene,
        RenderTriangle,
        RenderMeshScene,
        RenderMaterialScene,
        RenderDirectLighting,
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
