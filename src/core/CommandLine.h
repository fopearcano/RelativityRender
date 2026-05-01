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
//   --scene-summary <file>  Load a `.rrscene` file and print a
//                           compact summary (resolution, material
//                           count, sphere count, mesh count, light
//                           count, |beta|). Stage 10B.9 verifies a
//                           full v1 scene loads end-to-end. No
//                           render. Pure host code; works without
//                           CUDA.
//   --render-from-scene <file>
//                           Load a `.rrscene` file and render its
//                           sphere scene on the GPU. Stage 10B.10
//                           is the first action that actually
//                           renders from authored data: CPU
//                           parses + uploads camera / relativity /
//                           materials / spheres / lights to
//                           `GpuScene`; the kernel produces every
//                           pixel. Meshes are intentionally skipped
//                           in this slice (the prompt rules them
//                           out). Output path precedence:
//                           `--output` > scene's
//                           `render_settings.output_path` >
//                           "output/from_scene_spheres.ppm".
//                           Resolution comes from the scene's
//                           `render_settings`; `--width` /
//                           `--height` are ignored. Requires CUDA.
//   --render-full-scene <file>
//                           Like `--render-from-scene`, but also
//                           uploads the first visible non-empty
//                           mesh from the file (single-mesh GpuScene
//                           slot today; multi-mesh support is a
//                           future slice). Stage 10B.11 surface:
//                           the parser fully drives the GPU
//                           renderer for camera / relativity /
//                           materials / spheres / meshes / lights.
//                           Output path precedence: `--output` >
//                           scene's `render_settings.output_path` >
//                           "output/from_scene_full.ppm". Requires
//                           CUDA.
//   --render-rng-test       Run the Stage 11A GPU RNG / sampling
//                           validation kernel. The framebuffer is
//                           split into four quadrants exercising
//                           the four `pathtracer::*` primitives
//                           (white noise, 2D uniform, uniform
//                           hemisphere, cosine hemisphere). Default
//                           output "output/gpu_rng_test.ppm".
//                           Requires CUDA.
//   --render-accumulation-test
//                           Stage 11B progressive-accumulation
//                           validation. Allocates an
//                           AccumulationBuffer, loops 64 sample
//                           frames of per-pixel
//                           `(next_float, next_float, next_float,
//                            1.0)` through `accumulate_sample`,
//                           resolves to a display Image, and
//                           saves PPM. Result converges to a
//                           uniform mid-gray. Default output
//                           "output/gpu_accumulation_test.ppm".
//                           Requires CUDA.
//   --render-pathtrace <file>
//                           Stage 11C minimal diffuse GPU path
//                           tracer. Loads the `.rrscene` file,
//                           uploads the scene to GpuScene, and
//                           runs the path tracer twice (spp = 1
//                           and spp = 16) writing
//                           "output/pathtrace_spp_1.ppm" and
//                           "output/pathtrace_spp_16.ppm".
//                           --output is ignored. Resolution
//                           comes from the scene's
//                           render_settings; --width / --height
//                           are ignored. Requires CUDA.
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
//   --render-texture-sample-test
//                           Stage 13B.2 GPU texture-sampling
//                           validation. Synthesises a 2x2 RGBA8
//                           four-colour pattern (red / green /
//                           blue / yellow) on the host, uploads
//                           it via `rr::gpu::GpuTexture`, and
//                           launches the GPU kernel that maps
//                           every output pixel to UV ->
//                           `sampleTextureNearest(view, uv)` ->
//                           framebuffer. With clamp-to-edge
//                           nearest sampling the result is four
//                           solid colour quadrants; any other
//                           pattern indicates a bug. Default
//                           output
//                           "output/gpu_texture_sample_test.ppm".
//                           Requires CUDA.
//   --render-textured-material
//                           Stage 13B.3 material-texture
//                           integration. Builds the multi-sphere
//                           + quad scene with the same five-
//                           material palette as
//                           `--render-material-scene`, but the
//                           quad's "neutral" material is
//                           replaced with one whose
//                           `useBaseColorTexture` flag is set
//                           and `baseColorTextureId` points at
//                           an uploaded 2x2 four-colour
//                           reference texture. The kernel
//                           samples the texture at the hit
//                           point's interpolated UV; the spheres
//                           keep their flat baseColors. Default
//                           output
//                           "output/gpu_textured_material.ppm".
//                           Requires CUDA.
//   --render-aovs           Stage 14A.3 AOV / render-pass
//                           validation. Builds the multi-sphere
//                           + quad + lit scene from
//                           `--render-direct-lighting` plus a
//                           non-zero observer velocity, allocates
//                           one `GpuAOVBuffer` per declared AOV,
//                           and runs the GPU render kernel that
//                           additionally writes per-pixel values
//                           for Beauty / Normal / Depth / Albedo /
//                           DopplerFactor / SearchlightFactor.
//                           Each pass is downloaded and saved
//                           separately:
//                             output/aov_beauty.ppm
//                             output/aov_normal.ppm
//                             output/aov_depth.ppm
//                             output/aov_albedo.ppm
//                             output/aov_doppler.ppm
//                             output/aov_searchlight.ppm
//                           `--output` is ignored; the path-set
//                           above is fixed. Requires CUDA.
//   --server                Start the renderer server (Stage
//                           15A.2; master order #20). Binds a
//                           TCP listen socket to localhost:7777
//                           and accepts one client at a time;
//                           supported commands: `ping` -> `pong`.
//                           Logs startup and per-request lines.
//                           Press Ctrl-C (SIGINT) or send
//                           SIGTERM to stop; the server logs a
//                           shutdown line and exits with code 0.
//                           Pure host code; runs without CUDA.
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
// --scene-info / --scene-summary / --render-from-scene /
// --render-full-scene / --render-rng-test /
// --render-accumulation-test / --render-pathtrace /
// --render-gradient / --render-rays / --render-sphere /
// --render-relativistic / --render-scene / --render-triangle /
// --render-mesh-scene / --render-material-scene /
// --render-direct-lighting / --render-texture-sample-test /
// --render-textured-material / --render-aovs / --server) are
// mutually exclusive; combining them is a parse error. The
// remaining flags configure `Config` and are accepted regardless
// of action.

class CommandLine {
public:
    enum class Action {
        Default,        // no action flag given
        Help,
        Version,
        DeviceInfo,
        Render,
        SceneInfo,
        SceneSummary,
        RenderFromScene,
        RenderFullScene,
        RenderRngTest,
        RenderAccumulationTest,
        RenderPathtrace,
        RenderGradient,
        RenderRays,
        RenderSphere,
        RenderRelativistic,
        RenderScene,
        RenderTriangle,
        RenderMeshScene,
        RenderMaterialScene,
        RenderDirectLighting,
        RenderTextureSampleTest,
        RenderTexturedMaterial,
        RenderAOVs,
        Server,
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
