// RelativityRender entry point.
//
// Stage 6B scope: every prior CLI action plus `--render-scene`,
// which builds a built-in multi-sphere `Scene`, uploads it via
// `GpuScene`, and runs the GPU closest-hit kernel that loops over
// the uploaded sphere array. No scene parser yet, no materials, no
// lights, no path tracer, no server, no C4D.

#include "core/CommandLine.h"
#include "core/Config.h"
#include "core/Logger.h"
#include "core/Version.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuTiming.h"  // Stage 18A.1: format_gpu_timing_line
#include "io/SceneLoader.h"
#include "server/RenderServer.h"
#include "server/SocketPlatform.h"  // shutdown(2) wakeup + portable shim

#include <atomic>
#include <csignal>
#include <cstdio>      // Stage 20H: snprintf for the beta-suffixed default output path

// Stage 12B.5: rr_optix is only linked into the executable when
// RELATIVITYRENDER_ENABLE_OPTIX=ON, so the include is gated on the
// same macro the rr_optix target PUBLIC-defines. OFF builds never
// see this header and never reference the OptixBackend class.
#ifdef RELATIVITYRENDER_ENABLE_OPTIX
    #include "optix/OptixBackend.h"
    #include "optix/OptixDenoiser.h"
    #include "optix/OptixRenderer.h"
#endif

// Stage 20M: lift host-side scene / texture / vector / cstring
// includes out of the RR_HAS_CUDA gate so the audit-host
// `--render-optix-textured-material` dispatcher can construct
// its procedural Scene + ImageTexture even when CUDA is OFF
// (the OptiX render call is still gated on
// RELATIVITYRENDER_ENABLE_OPTIX inside the dispatcher).
#include "geometry/Mesh.h"
#include "lighting/Light.h"            // Stage 20N: needed by --render-optix-aovs dispatcher (audit-host build too)
#include "material/MaterialTypes.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "scene/Scene.h"
#include "texture/ImageTexture.h"

#include <cstddef>
#include <cstring>
#include <vector>

#ifdef RR_HAS_CUDA
    #include "camera/Camera.h"
    #include "cuda/CudaAccumulation.cuh"
    #include "cuda/CudaRenderer.h"
    #include "geometry/Sphere.h"
    #include "geometry/Triangle.h"
    #include "gpu/GpuBuffer.h"
    #include "gpu/GpuScene.h"
    #include "lighting/Light.h"
    #include "material/Material.h"
    #include "pathtracer/PathTracer.h"
    #include "relativity/RelativityParams.h"
    #include "renderer/AccumulationBuffer.h"
    #include "renderer/AOV.h"
    #include "renderer/GpuAOVBuffer.h"
#endif

#include "image/Image.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

void report_device_info() {
    using rr::core::Logger;

    Logger::info(std::string("GPU backend: ") + rr::gpu::gpu_backend_name());

    const auto devices = rr::gpu::enumerate_devices();
    if (devices.empty()) {
        Logger::info("No CUDA-capable devices visible. "
                     "Rebuild with -DRR_ENABLE_CUDA=ON on a host with the "
                     "CUDA Toolkit and a CUDA-capable GPU to enable device "
                     "queries.");
    } else {
        Logger::info(std::to_string(devices.size())
                     + (devices.size() == 1 ? " device:" : " devices:"));
        for (const auto& d : devices) {
            const std::string line =
                "  [" + std::to_string(d.index) + "] " + d.name
              + " (sm_"  + d.compute_capability_string()
              + ", "     + d.total_memory_human()
              + ", "     + std::to_string(d.multiprocessor_count) + " SMs)";
            Logger::info(line);
        }
    }

    // Stage 12B.5: OptiX availability stanza. Three lines that
    // describe the scaffold's compile / SDK / runtime state. None
    // of this initialises an OptixDeviceContext or invokes any
    // OptiX runtime call - it reports compile-time facts only.
    // CUDA remains the primary renderer; this is purely informational.
#ifdef RELATIVITYRENDER_ENABLE_OPTIX
    Logger::info(std::string("OptiX build enabled: ")
                 + (rr::optix::OptixBackend::isCompiled() ? "yes" : "no"));
    Logger::info(std::string("OptiX SDK found: ")
                 + (rr::optix::OptixBackend::isSdkFound() ? "yes" : "no"));
    Logger::info("OptiX renderer status: scaffold only");
#else
    // OFF build: rr_optix is not linked and OptixBackend is not
    // visible. Report the compile-time fact and stop - SDK / status
    // lines are meaningless when the backend was never built in.
    Logger::info("OptiX build enabled: no");
#endif
}

// Stage 18A.1: emit a single console line with the renderer's
// GPU-side kernel time + a primary-rays/sec estimate. Pure
// instrumentation - the renderers populate `gpu_time_ms` via a
// `cudaEvent_t` pair around the launch region; this helper just
// formats and logs. A `gpu_time_ms <= 0` (no-CUDA build, early
// exit, audit-host OptiX fallback, etc.) silently skips the log
// line. Defined outside the `RR_HAS_CUDA` gate because the OptiX
// CLI handlers consume it on hosts that have OptiX enabled but
// not CUDA.
inline void log_gpu_timing(const char* label,
                           int width, int height,
                           float gpu_time_ms) {
    auto line = rr::gpu::format_gpu_timing_line(label, width, height,
                                                gpu_time_ms);
    if (!line.empty()) {
        rr::core::Logger::info(line);
    }
}

// Stage 19C.1: denoiser-friendly timing log. Same shape as
// `log_gpu_timing` but uses the `ms/frame` + `frames/sec`
// metric framing instead of `rays/sec` (the denoiser does
// not trace primary rays). Same gating-friendly posture:
// silently skip the log line when `gpu_time_ms <= 0`.
inline void log_denoiser_timing(const char* label,
                                int width, int height,
                                float gpu_time_ms) {
    auto line = rr::gpu::format_denoiser_timing_line(label, width, height,
                                                     gpu_time_ms);
    if (!line.empty()) {
        rr::core::Logger::info(line);
    }
}

#ifdef RR_HAS_CUDA
// Forward declarations for two save helpers used by the GPU render
// dispatches. Both are gated on `RR_HAS_CUDA` because that's the
// only context where a populated framebuffer / AOV buffer reaches
// the host.
bool save_image_or_error(const rr::image::Image& img,
                         const std::string&      out_path,
                         std::string_view        label,
                         int                     width,
                         int                     height);

bool save_aov_to_ppm(const rr::renderer::GpuAOVBuffer& buffer,
                     const std::string&                out_path,
                     int                               width,
                     int                               height,
                     std::string_view                  label);

bool save_image_or_error(const rr::image::Image& img,
                         const std::string&      out_path,
                         std::string_view        label,
                         int                     width,
                         int                     height) {
    using rr::core::Logger;
    namespace fs = std::filesystem;

    const fs::path out_fs = out_path;
    if (out_fs.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_fs.parent_path(), ec);
        if (ec) {
            Logger::error("could not create output directory '"
                        + out_fs.parent_path().string() + "': "
                        + ec.message());
            return false;
        }
    }

    if (!img.save_ppm(out_fs)) {
        Logger::error("could not write PPM: " + out_path);
        return false;
    }

    std::error_code ec;
    const fs::path  abs = fs::absolute(out_fs, ec);
    Logger::info(std::string("wrote ") + std::string(label) + ": "
               + (ec ? out_path : abs.string())
               + " (" + std::to_string(width) + "x"
               + std::to_string(height) + ", RGBA32F)");
    return true;
}

// Stage 14A.3 helper: download a per-pass `GpuAOVBuffer` and write
// it to PPM via the existing `Image::save_ppm` path. Vector AOVs
// (3 floats / pixel: Beauty / Normal / Albedo) are copied directly
// into an `Rgb32F` image. Scalar AOVs (1 float / pixel: Depth /
// DopplerFactor / SearchlightFactor) are replicated to grayscale
// RGB so the resulting PPM is viewable. The float -> uint8 clamp
// happens inside `save_ppm`, matching every other GPU-render
// action's save behaviour. No per-pixel value computation runs on
// the CPU - the floats stored in the AOV buffer are exactly what
// the kernel wrote (encoded forms for Normal / Depth, raw for the
// others; see `cuda/CudaAOV.cuh` for the encoding choices).
bool save_aov_to_ppm(const rr::renderer::GpuAOVBuffer& buffer,
                     const std::string&                out_path,
                     int                               width,
                     int                               height,
                     std::string_view                  label) {
    using rr::core::Logger;

    std::vector<float> host;
    if (!buffer.download(host)) {
        Logger::error(std::string(label)
                    + " AOV download failed (no GPU backend, "
                      "buffer not allocated, or device->host copy "
                      "errored)");
        return false;
    }

    const int components = buffer.component_count();
    rr::image::Image img(width, height, rr::image::PixelFormat::Rgb32F);

    if (components == 3) {
        // Direct copy: Image's Rgb32F layout is exactly 3 contiguous
        // floats per pixel, matching the AOV buffer's layout.
        const std::size_t expected =
            static_cast<std::size_t>(width)
          * static_cast<std::size_t>(height) * 3u;
        if (host.size() != expected || img.size_in_floats() != expected) {
            Logger::error(std::string(label)
                        + " AOV size mismatch (host="
                        + std::to_string(host.size())
                        + ", image=" + std::to_string(img.size_in_floats())
                        + ", expected=" + std::to_string(expected) + ")");
            return false;
        }
        std::memcpy(img.data(), host.data(), expected * sizeof(float));
    } else if (components == 1) {
        // Replicate scalar to RGB so the PPM has three identical
        // channels. Pure data-layout transformation; no value math.
        const std::size_t pixel_count =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        if (host.size() != pixel_count) {
            Logger::error(std::string(label)
                        + " AOV size mismatch (host="
                        + std::to_string(host.size())
                        + ", expected=" + std::to_string(pixel_count) + ")");
            return false;
        }
        float* dst = img.data();
        for (std::size_t i = 0; i < pixel_count; ++i) {
            const float v = host[i];
            dst[i * 3 + 0] = v;
            dst[i * 3 + 1] = v;
            dst[i * 3 + 2] = v;
        }
    } else {
        Logger::error(std::string(label)
                    + " AOV has unexpected component count: "
                    + std::to_string(components));
        return false;
    }

    return save_image_or_error(img, out_path, label, width, height);
}
#endif  // RR_HAS_CUDA

// Module-local state for the Stage 15A.2 `--server` action's
// signal-driven shutdown. The handler stores the listen fd
// when the server starts and clears it on stop; SIGINT /
// SIGTERM (POSIX) and SIGINT (Windows / Console-Ctrl) trigger
// an async-signal-safe `shutdown(fd, kSocketShutdownBoth)` to
// wake a blocked `accept()` and set the stop flag the serving
// loop polls between cycles.
//
// The Windows-portability slice changed the captured fd type
// from `int` to `rr::server::socket_t` (which is `int` on
// POSIX and `SOCKET = UINT_PTR` on Windows). `std::atomic<
// socket_t>` is lock-free on both platforms.
namespace server_signal {

std::atomic<rr::server::socket_t> g_listen_fd{rr::server::kInvalidSocket};
std::atomic<bool>                 g_stop_requested{false};

extern "C" void signal_handler(int /*sig*/) {
    g_stop_requested.store(true, std::memory_order_release);
    const rr::server::socket_t fd =
        g_listen_fd.load(std::memory_order_acquire);
    if (fd != rr::server::kInvalidSocket) {
        // Async-signal-safe (POSIX) / safe-from-any-thread
        // (Winsock2): `shutdown(2)` triggers the kernel-side
        // wakeup of `accept()` in the main thread, which the
        // server's serve_one() then surfaces; the loop
        // observes `g_stop_requested` and exits.
        ::shutdown(fd, rr::server::kSocketShutdownBoth);
    }
}

}  // namespace server_signal

// `--server` dispatch (Stage 15A.2; master order #20). Starts
// the renderer server on `127.0.0.1:7777`, logs startup, loops
// `serve_one()` until SIGINT / SIGTERM, logs each request +
// the final shutdown line, and returns 0 on graceful exit.
// Pure host code; works without CUDA.
int run_server(const rr::core::Config& /*cfg*/) {
    using rr::core::Logger;

    // Initialise the platform's socket subsystem (no-op on POSIX,
    // WSAStartup on Windows). Must run before `RenderServer::start`.
    if (!rr::server::initSocketSystem()) {
        Logger::error("server start failed: socket subsystem "
                      "initialisation failed");
        return 1;
    }

    rr::server::RenderServer::Config server_cfg;
    // Bind address + port default to "127.0.0.1" / 7777 per
    // the Stage 15A.1 Config contract.
    rr::server::RenderServer server(server_cfg);

    if (!server.start()) {
        Logger::error("server start failed: " + server.last_error());
        rr::server::shutdownSocketSystem();
        return 1;
    }

    // Capture the listen fd so the signal handler can wake
    // a blocked accept() via shutdown(2). Reset the stop flag
    // in case a previous --server run left it set in the same
    // process (single CLI invocation today, but defensive).
    server_signal::g_listen_fd.store(server.listen_fd(),
                                     std::memory_order_release);
    server_signal::g_stop_requested.store(false,
                                          std::memory_order_release);

    // Install signal handlers for graceful shutdown. POSIX uses
    // sigaction with no SA_RESTART so accept() returns on signal
    // delivery; Windows-only signal() handles SIGINT (Ctrl+C is
    // delivered via a separate thread under the hood). Either
    // way the wire-level `shutdown` command is the deterministic
    // alternative.
#if defined(_WIN32)
    std::signal(SIGINT, server_signal::signal_handler);
#else
    struct sigaction sa{};
    sa.sa_handler = server_signal::signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGINT,  &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
#endif

    Logger::info("renderer server started on "
               + server.bind_address() + ":"
               + std::to_string(server.port())
               + " (Ctrl-C / SIGTERM to stop)");

    long served = 0;
    while (server.is_listening()
        && !server_signal::g_stop_requested.load(
              std::memory_order_acquire)) {
        const auto result = server.serve_one();

        if (server_signal::g_stop_requested.load(
              std::memory_order_acquire)) {
            // Stop arrived during this cycle. The serve_one's
            // accept() likely returned with EINVAL after our
            // shutdown(2); ignore its error_message and exit.
            break;
        }

        if (!result.ok) {
            Logger::warning("server cycle error: "
                          + (result.error_message.empty()
                                 ? std::string("(unknown)")
                                 : result.error_message));
            continue;
        }

        ++served;
        Logger::info("served '" + result.command + "' from "
                   + result.client_address + ":"
                   + std::to_string(result.client_port)
                   + " -> '" + result.response + "'");

        // Wire-driven graceful shutdown (see
        // docs/SHELL_HANG_AUDIT.md): if the just-served command
        // was `shutdown`, the server set its
        // `shutdown_requested_` flag inside `handle_command`.
        // Break the loop cleanly; the response was already sent
        // to the client.
        if (server.shutdown_requested()) {
            Logger::info("renderer server: shutdown requested by client");
            break;
        }
    }

    server.stop();
    server_signal::g_listen_fd.store(rr::server::kInvalidSocket,
                                     std::memory_order_release);

    // Tear down the socket subsystem (no-op on POSIX, WSACleanup
    // on Windows). Symmetric with the initSocketSystem() call at
    // the top of this function.
    rr::server::shutdownSocketSystem();

    Logger::info("renderer server stopped ("
               + std::to_string(served)
               + (served == 1 ? " request served)" : " requests served)"));
    return 0;
}

// `--scene-info` dispatch. Loads `cfg.scene_path` via the Stage
// 10B.2 scene parser, prints the version + render settings, and
// exits. No render, no GPU - this works whether or not CUDA is
// compiled in. Returns 0 on parse success, non-zero otherwise.
int run_scene_info(const rr::core::Config& cfg) {
    using rr::core::Logger;

    if (cfg.scene_path.empty()) {
        Logger::error("--scene-info requires a file path");
        return 2;
    }

    const auto result = rr::io::load(cfg.scene_path);
    if (!result.ok) {
        std::string msg = "scene load failed: " + result.error_message;
        if (result.error_line > 0) {
            msg += " (line " + std::to_string(result.error_line)
                +  ", column " + std::to_string(result.error_column) + ")";
        }
        Logger::error(msg);
        return 1;
    }

    const auto& rs  = result.scene.render_settings;
    const auto& cam = result.scene.camera;

    auto fmt_vec3 = [](rr::math::Vec3 v) {
        return "[" + std::to_string(v.x) + ", "
                   + std::to_string(v.y) + ", "
                   + std::to_string(v.z) + "]";
    };

    Logger::info("scene file: " + cfg.scene_path);
    Logger::info("  version           : " + result.version);
    Logger::info("  render_settings:");
    Logger::info("    width             : " + std::to_string(rs.width));
    Logger::info("    height            : " + std::to_string(rs.height));
    Logger::info("    samples_per_pixel : "
               + std::to_string(rs.samples_per_pixel));
    Logger::info("    max_depth         : " + std::to_string(rs.max_depth));
    Logger::info("    output_path       : "
               + (rs.output_path.empty()
                    ? std::string("(none)")
                    : rs.output_path));
    Logger::info("  camera:");
    Logger::info("    position          : " + fmt_vec3(cam.position()));
    Logger::info("    forward           : " + fmt_vec3(cam.forward()));
    Logger::info("    up                : " + fmt_vec3(cam.up()));
    Logger::info("    fov_degrees       : "
               + std::to_string(cam.vertical_fov_degrees()));
    Logger::info("    aspect            : "
               + std::to_string(cam.aspect()));

    const auto& obs = result.scene.observer;
    const auto& rp  = result.scene.relativity;
    const float beta_speed = std::sqrt(obs.velocity.x * obs.velocity.x
                                     + obs.velocity.y * obs.velocity.y
                                     + obs.velocity.z * obs.velocity.z);
    auto fmt_bool = [](bool b) -> std::string {
        return b ? "true" : "false";
    };

    Logger::info("  relativity:");
    Logger::info("    observer_velocity     : " + fmt_vec3(obs.velocity));
    Logger::info("    |beta|                : " + std::to_string(beta_speed));
    Logger::info("    enable_aberration     : "
               + fmt_bool(rp.enable_aberration));
    Logger::info("    enable_doppler        : "
               + fmt_bool(rp.enable_doppler));
    Logger::info("    enable_searchlight    : "
               + fmt_bool(rp.enable_searchlight));
    Logger::info("    doppler_color_strength: "
               + std::to_string(rp.doppler_color_strength));
    Logger::info("    searchlight_strength  : "
               + std::to_string(rp.searchlight_strength));
    Logger::info("    max_beta              : "
               + std::to_string(rp.max_beta));

    const auto& mats = result.scene.materials;
    Logger::info("  materials:");
    Logger::info("    count             : " + std::to_string(mats.size()));
    if (!mats.empty()) {
        const auto& m = mats.front();
        Logger::info("    [0]:");
        Logger::info("      id              : " + std::to_string(m.id));
        Logger::info("      name            : "
                   + (m.name.empty() ? std::string("(unnamed)") : m.name));
        Logger::info("      baseColor       : " + fmt_vec3(m.params.baseColor));
        Logger::info("      emissionColor   : "
                   + fmt_vec3(m.params.emissionColor));
        Logger::info("      emissionStrength: "
                   + std::to_string(m.params.emissionStrength));
        Logger::info("      roughness       : "
                   + std::to_string(m.params.roughness));
        Logger::info("      metallic        : "
                   + std::to_string(m.params.metallic));
        Logger::info("      specular        : "
                   + std::to_string(m.params.specular));
    }

    const auto& sph = result.scene.spheres;
    Logger::info("  spheres:");
    Logger::info("    count             : " + std::to_string(sph.size()));
    if (!sph.empty()) {
        const auto& s = sph.front();
        Logger::info("    [0]:");
        Logger::info("      name            : "
                   + (s.object.name.empty() ? std::string("(unnamed)")
                                            : s.object.name));
        Logger::info("      center          : " + fmt_vec3(s.geometry.center));
        Logger::info("      radius          : "
                   + std::to_string(s.geometry.radius));
        Logger::info("      material_index  : "
                   + std::to_string(s.geometry.material_index));
    }

    const auto& lts = result.scene.lights;
    auto fmt_light_type = [](rr::lighting::LightType t) -> std::string {
        switch (t) {
            case rr::lighting::LightType::Point:       return "point";
            case rr::lighting::LightType::Directional: return "directional";
            case rr::lighting::LightType::Area:        return "area";
            case rr::lighting::LightType::Environment: return "environment";
        }
        return "unknown";
    };
    const auto& meshes = result.scene.meshes;
    Logger::info("  meshes:");
    Logger::info("    count             : " + std::to_string(meshes.size()));
    if (!meshes.empty()) {
        const auto& mesh = meshes.front();
        Logger::info("    [0]:");
        Logger::info("      name            : "
                   + (mesh.object.name.empty() ? std::string("(unnamed)")
                                               : mesh.object.name));
        Logger::info("      vertex_count    : "
                   + std::to_string(mesh.geometry.vertex_count()));
        Logger::info("      triangle_count  : "
                   + std::to_string(mesh.geometry.triangle_count()));
        Logger::info("      material_id     : "
                   + std::to_string(mesh.geometry.material_id));
    }

    Logger::info("  lights:");
    Logger::info("    count             : " + std::to_string(lts.size()));
    if (!lts.empty()) {
        const auto& l = lts.front();
        const bool has_position  =
            (l.data.type == rr::lighting::LightType::Point
          || l.data.type == rr::lighting::LightType::Area);
        const bool has_direction =
            (l.data.type == rr::lighting::LightType::Directional);
        Logger::info("    [0]:");
        Logger::info("      type            : " + fmt_light_type(l.data.type));
        Logger::info("      name            : "
                   + (l.object.name.empty() ? std::string("(unnamed)")
                                            : l.object.name));
        Logger::info("      color           : " + fmt_vec3(l.data.color));
        Logger::info("      intensity       : "
                   + std::to_string(l.data.intensity));
        if (has_position) {
            Logger::info("      position        : "
                       + fmt_vec3(l.data.position));
        }
        if (has_direction) {
            Logger::info("      direction       : "
                       + fmt_vec3(l.data.direction));
        }
    }
    return 0;
}

// `--scene-summary` dispatch. Stage 10B.9 verification path: loads
// the file via the same parser as `--scene-info` and prints a
// compact one-section summary - resolution, material / sphere /
// mesh / light counts, and the scalar `|beta|` of the observer's
// 3-velocity. No render, no GPU; intentionally smaller than
// `--scene-info`'s exhaustive dump so a full v1 scene's load
// status fits on a single screen.
int run_scene_summary(const rr::core::Config& cfg) {
    using rr::core::Logger;

    if (cfg.scene_path.empty()) {
        Logger::error("--scene-summary requires a file path");
        return 2;
    }

    const auto result = rr::io::load(cfg.scene_path);
    if (!result.ok) {
        std::string msg = "scene load failed: " + result.error_message;
        if (result.error_line > 0) {
            msg += " (line " + std::to_string(result.error_line)
                +  ", column " + std::to_string(result.error_column) + ")";
        }
        Logger::error(msg);
        return 1;
    }

    const auto& s   = result.scene;
    const auto& v   = s.observer.velocity;
    const float beta = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);

    Logger::info("scene file: " + cfg.scene_path);
    Logger::info("  resolution     : "
               + std::to_string(s.render_settings.width) + "x"
               + std::to_string(s.render_settings.height));
    Logger::info("  materials      : " + std::to_string(s.materials.size()));
    Logger::info("  spheres        : " + std::to_string(s.spheres.size()));
    Logger::info("  meshes         : " + std::to_string(s.meshes.size()));
    Logger::info("  lights         : " + std::to_string(s.lights.size()));
    Logger::info("  |beta|         : " + std::to_string(beta));
    return 0;
}

// `--render-from-scene` dispatch. Stage 10B.10 - the first action
// that drives the GPU renderer from authored data. CPU loads the
// `.rrscene` via the Stage 10B.2-10B.8 parser, uploads the
// resulting host `Scene` (camera + relativity + materials +
// spheres + lights) to a `GpuScene`, and runs the existing GPU
// closest-hit kernel through `CudaRenderer::render_scene`.
//
// Meshes are intentionally skipped: the prompt explicitly defers
// mesh rendering ("Do not render meshes yet"). The parser still
// reads the `meshes` block onto `scene.meshes` (Stage 10B.8); we
// just don't call `gpu_scene.upload_mesh` here. A follow-up
// stage threads `SceneMesh::geometry` through the upload path.
//
// Resolution comes from the parsed `render_settings`; this is
// the canonical source authored by the file. `--width` /
// `--height` are intentionally ignored - mixing CLI overrides
// with authored resolution would mean either the camera's aspect
// or the framebuffer's are stale, neither helpful. Output path
// precedence: `--output` > scene's `output_path` >
// "output/from_scene_spheres.ppm".
int run_render_from_scene(const rr::core::Config& cfg) {
    using rr::core::Logger;

    if (cfg.scene_path.empty()) {
        Logger::error("--render-from-scene requires a file path");
        return 2;
    }

    const auto loaded = rr::io::load(cfg.scene_path);
    if (!loaded.ok) {
        std::string msg = "scene load failed: " + loaded.error_message;
        if (loaded.error_line > 0) {
            msg += " (line " + std::to_string(loaded.error_line)
                +  ", column " + std::to_string(loaded.error_column) + ")";
        }
        Logger::error(msg);
        return 1;
    }

    const auto& scene  = loaded.scene;
    const int   width  = scene.render_settings.width;
    const int   height = scene.render_settings.height;

    const std::string out_path =
        !cfg.output_path.empty()                ? cfg.output_path
      : !scene.render_settings.output_path.empty()
                                                ? scene.render_settings.output_path
                                                : std::string("output/from_scene_spheres.ppm");

#ifndef RR_HAS_CUDA
    (void)scene;
    (void)width;
    (void)height;
    (void)out_path;
    Logger::error("--render-from-scene requires CUDA. Rebuild with "
                  "-DRR_ENABLE_CUDA=ON on a host with the CUDA Toolkit "
                  "and a CUDA-capable GPU.");
    return 1;
#else
    // The parser captures aspect from render_settings already
    // (via `apply_camera`); leave the camera as authored.

    // Pull `rr::geometry::Sphere` PODs out of the scene's
    // `SceneSphere` wrappers, dropping any entries marked invisible.
    std::vector<rr::geometry::Sphere> sphere_pods;
    sphere_pods.reserve(scene.spheres.size());
    for (const auto& s : scene.spheres) {
        if (s.object.visible) sphere_pods.push_back(s.geometry);
    }

    // Materials get flattened the same way `--render-material-scene`
    // does; the kernel reads `materials[Hit::material_index]`.
    std::vector<rr::material::MaterialParams> material_pods;
    material_pods.reserve(scene.materials.size());
    for (const auto& m : scene.materials) material_pods.push_back(m.params);

    // Lights flatten the same way `--render-direct-lighting` does.
    std::vector<rr::lighting::Light> light_pods;
    light_pods.reserve(scene.lights.size());
    for (const auto& l : scene.lights) {
        if (l.object.visible) light_pods.push_back(l.data);
    }

    rr::gpu::GpuScene gpu_scene;
    if (!gpu_scene.upload_camera(scene.camera)) {
        Logger::error("render-from-scene failed: upload_camera");
        return 1;
    }
    if (!gpu_scene.upload_relativity(scene.observer, scene.relativity)) {
        Logger::error("render-from-scene failed: upload_relativity");
        return 1;
    }
    if (!sphere_pods.empty()) {
        if (!gpu_scene.upload_spheres(sphere_pods.data(),
                                      sphere_pods.size())) {
            Logger::error("render-from-scene failed: upload_spheres");
            return 1;
        }
    }
    if (!material_pods.empty()) {
        if (!gpu_scene.upload_materials(material_pods.data(),
                                        material_pods.size())) {
            Logger::error("render-from-scene failed: upload_materials");
            return 1;
        }
    }
    if (!light_pods.empty()) {
        if (!gpu_scene.upload_lights(light_pods.data(), light_pods.size())) {
            Logger::error("render-from-scene failed: upload_lights");
            return 1;
        }
    }
    // Mesh upload is intentionally skipped per the Stage 10B.10
    // prompt rule "Do not render meshes yet". Threading
    // `SceneMesh::geometry` through `gpu_scene.upload_mesh` joins
    // in a follow-up sub-stage.

    auto r = rr::cuda::CudaRenderer::render_scene(gpu_scene, width, height);
    if (!r.ok) {
        Logger::error("render-from-scene failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-from-scene", width, height, r.gpu_time_ms);

    Logger::info("scene file  : " + cfg.scene_path);
    Logger::info("scene-render: "
               + std::to_string(sphere_pods.size())   + " sphere(s), "
               + std::to_string(material_pods.size()) + " material(s), "
               + std::to_string(light_pods.size())    + " light(s) "
                 "uploaded; meshes deferred");
    Logger::info("framebuffer : "
               + std::to_string(width) + "x" + std::to_string(height)
               + " (from render_settings)");

    if (!save_image_or_error(r.image, out_path, "GPU scene-from-file",
                             width, height)) {
        return 1;
    }

    // Stage 21E.2: optional denoise pass triggered by the
    // `--denoise` modifier flag (Stage 19B.4 +
    // Stage 21E.1 announce). The normal render path above
    // is unchanged: `output/render.ppm` (or whatever
    // `--output` resolved to) carries the same noisy beauty
    // it always did. The denoise pass is purely additive -
    // when it succeeds, an additional `output/denoised.ppm`
    // appears alongside; when it fails, a single warning
    // line surfaces the cause and the original render is
    // kept untouched.
    //
    // The denoiser needs Beauty / Albedo / Normal AOVs the
    // standard `CudaRenderer::render_scene` path does not
    // produce. We re-render the same scene through the
    // AOV-aware kernel (`render_scene_with_aovs`) on the
    // already-uploaded `GpuScene`, initialise the OptiX
    // backend + denoiser, and hand the buffers to the
    // existing `denoise_and_save_ppm` helper (Stage 21D.4 +
    // 21D.5). The helper carries its own noisy-Beauty
    // fallback per the Stage 21A.7 contract; this block
    // only handles the OUTSIDE-the-helper failure paths
    // (AOV resize / AOV-render / backend / denoiser init
    // failures) by warning + returning 0.
    if (cfg.denoise_enabled) {
#ifdef RELATIVITYRENDER_ENABLE_OPTIX
        rr::renderer::GpuAOVBuffer beauty_buf(
            rr::renderer::AOV::make_beauty());
        rr::renderer::GpuAOVBuffer normal_buf(
            rr::renderer::AOV::make_normal());
        rr::renderer::GpuAOVBuffer albedo_buf(
            rr::renderer::AOV::make_albedo());
        if (!beauty_buf.resize(width, height) ||
            !normal_buf.resize(width, height) ||
            !albedo_buf.resize(width, height)) {
            Logger::warning("--denoise: AOV buffer allocation "
                            "failed; keeping original render at "
                          + out_path);
            return 0;
        }

        rr::cuda::CudaRenderer::AOVTargets targets;
        targets.beauty = beauty_buf.device_ptr();
        targets.normal = normal_buf.device_ptr();
        targets.albedo = albedo_buf.device_ptr();

        auto aov_r = rr::cuda::CudaRenderer::render_scene_with_aovs(
            gpu_scene, width, height, targets);
        if (!aov_r.ok) {
            Logger::warning("--denoise: AOV render failed: "
                          + aov_r.message
                          + "; keeping original render at "
                          + out_path);
            return 0;
        }

        rr::optix::OptixBackend backend;
        if (!backend.initialize()) {
            Logger::warning("--denoise: OptixBackend init "
                            "failed: " + backend.last_error()
                          + "; keeping original render at "
                          + out_path);
            return 0;
        }
        rr::optix::OptixDenoiser denoiser;
        if (!denoiser.initialize(backend)) {
            Logger::warning("--denoise: OptixDenoiser init "
                            "failed: " + denoiser.last_error()
                          + "; keeping original render at "
                          + out_path);
            return 0;
        }

        rr::optix::OptixDenoiser::Inputs inputs;
        inputs.beauty_device     = beauty_buf.device_ptr();
        inputs.beauty_components = 3;
        inputs.albedo_device     = albedo_buf.device_ptr();
        inputs.normal_device     = normal_buf.device_ptr();
        inputs.width             = width;
        inputs.height            = height;

        if (!denoise_and_save_ppm(denoiser, inputs,
                                  "output/denoised.ppm")) {
            Logger::warning("--denoise: failed to save denoised "
                            "output; keeping original render at "
                          + out_path);
            // Helper has already populated `last_error_` /
            // logged its own warning where appropriate; we
            // still exit 0 because the standard render
            // succeeded.
        }
#else
        Logger::warning("--denoise: denoiser unavailable on this "
                        "build (requires -DRR_ENABLE_OPTIX=ON); "
                        "keeping original render at " + out_path);
#endif
    }

    return 0;
#endif
}

// `--render <scene>` dispatch (CLI render path repair). The
// pre-repair Stage 1 placeholder for this action only logged
// "render command received" and returned 0 - no scene was
// loaded, no GPU pipeline ran, no PPM was written. This handler
// wires the action up to the real GPU pipeline by delegating to
// the existing `run_render_from_scene`, which already loads via
// `rr::io::load`, uploads via `rr::gpu::GpuScene`, renders via
// `rr::cuda::CudaRenderer::render_scene`, and saves the result
// through `save_image_or_error`.
//
// The only behaviour difference from `--render-from-scene` is
// the default output path: per the CLI render-path-repair spec,
// `--render` defaults to `output/render.ppm` when `--output` is
// not supplied. The scene's authored
// `render_settings.output_path` is intentionally NOT consulted
// here (the spec hardcodes the default), but `--output` still
// overrides everything.
//
// All per-pixel / per-ray work runs on the GPU; this handler is
// pure host orchestration (parse / upload / launch / save), in
// keeping with the master rules.
int run_render(const rr::core::Config& cfg) {
    using rr::core::Logger;

    if (cfg.scene_path.empty()) {
        Logger::error("--render requires a scene file path");
        return 2;
    }

    rr::core::Config effective = cfg;
    if (effective.output_path.empty()) {
        effective.output_path = "output/render.ppm";
    }

    return run_render_from_scene(effective);
}

// `--render-full-scene` dispatch. Stage 10B.11 - the first action
// that drives the GPU renderer for a complete `.rrscene` file:
// camera + relativity + materials + spheres + meshes + lights all
// parsed by the loader and uploaded to `GpuScene` before
// `CudaRenderer::render_scene` produces the framebuffer.
//
// Compared to `--render-from-scene` (Stage 10B.10), this handler
// also calls `gpu_scene.upload_mesh(...)` for the first visible
// non-empty mesh in `scene.meshes`. The current `GpuScene` mesh
// slot holds exactly one mesh (multi-mesh upload is a future
// slice; see `GpuScene::upload_mesh`'s header comment); when a
// file authors more than one non-empty mesh the handler logs the
// constraint and uploads only the first. Empty meshes (vertex
// or triangle count zero per §9) are skipped silently.
//
// Output path precedence: `--output` >
// `scene.render_settings.output_path` >
// "output/from_scene_full.ppm" (the Stage 10B.11 default).
// Resolution comes from the scene's `render_settings`; `--width`
// / `--height` are intentionally ignored, same policy as
// `--render-from-scene`.
int run_render_full_scene(const rr::core::Config& cfg) {
    using rr::core::Logger;

    if (cfg.scene_path.empty()) {
        Logger::error("--render-full-scene requires a file path");
        return 2;
    }

    const auto loaded = rr::io::load(cfg.scene_path);
    if (!loaded.ok) {
        std::string msg = "scene load failed: " + loaded.error_message;
        if (loaded.error_line > 0) {
            msg += " (line " + std::to_string(loaded.error_line)
                +  ", column " + std::to_string(loaded.error_column) + ")";
        }
        Logger::error(msg);
        return 1;
    }

    const auto& scene  = loaded.scene;
    const int   width  = scene.render_settings.width;
    const int   height = scene.render_settings.height;

    const std::string out_path =
        !cfg.output_path.empty()                ? cfg.output_path
      : !scene.render_settings.output_path.empty()
                                                ? scene.render_settings.output_path
                                                : std::string("output/from_scene_full.ppm");

#ifndef RR_HAS_CUDA
    (void)scene;
    (void)width;
    (void)height;
    (void)out_path;
    Logger::error("--render-full-scene requires CUDA. Rebuild with "
                  "-DRR_ENABLE_CUDA=ON on a host with the CUDA Toolkit "
                  "and a CUDA-capable GPU.");
    return 1;
#else
    // Visible spheres + materials + visible lights flatten the
    // same way they do in `--render-from-scene` (Stage 10B.10);
    // see that handler's comments for the rationale.
    std::vector<rr::geometry::Sphere> sphere_pods;
    sphere_pods.reserve(scene.spheres.size());
    for (const auto& s : scene.spheres) {
        if (s.object.visible) sphere_pods.push_back(s.geometry);
    }
    std::vector<rr::material::MaterialParams> material_pods;
    material_pods.reserve(scene.materials.size());
    for (const auto& m : scene.materials) material_pods.push_back(m.params);
    std::vector<rr::lighting::Light> light_pods;
    light_pods.reserve(scene.lights.size());
    for (const auto& l : scene.lights) {
        if (l.object.visible) light_pods.push_back(l.data);
    }

    // Pick the first visible non-empty mesh for the upload slot.
    // Multi-mesh support is on `GpuScene`'s deferred list; the
    // single-slot constraint is documented in
    // `GpuScene::upload_mesh`'s header comment.
    const rr::geometry::Mesh* mesh_to_upload = nullptr;
    std::size_t mesh_total          = 0;
    std::size_t mesh_visible_filled = 0;
    for (const auto& sm : scene.meshes) {
        ++mesh_total;
        if (!sm.object.visible) continue;
        if (sm.geometry.empty()) continue;
        ++mesh_visible_filled;
        if (mesh_to_upload == nullptr) mesh_to_upload = &sm.geometry;
    }

    rr::gpu::GpuScene gpu_scene;
    if (!gpu_scene.upload_camera(scene.camera)) {
        Logger::error("render-full-scene failed: upload_camera");
        return 1;
    }
    if (!gpu_scene.upload_relativity(scene.observer, scene.relativity)) {
        Logger::error("render-full-scene failed: upload_relativity");
        return 1;
    }
    if (!sphere_pods.empty()) {
        if (!gpu_scene.upload_spheres(sphere_pods.data(),
                                      sphere_pods.size())) {
            Logger::error("render-full-scene failed: upload_spheres");
            return 1;
        }
    }
    if (!material_pods.empty()) {
        if (!gpu_scene.upload_materials(material_pods.data(),
                                        material_pods.size())) {
            Logger::error("render-full-scene failed: upload_materials");
            return 1;
        }
    }
    if (!light_pods.empty()) {
        if (!gpu_scene.upload_lights(light_pods.data(), light_pods.size())) {
            Logger::error("render-full-scene failed: upload_lights");
            return 1;
        }
    }
    if (mesh_to_upload != nullptr) {
        if (!gpu_scene.upload_mesh(*mesh_to_upload)) {
            Logger::error("render-full-scene failed: upload_mesh");
            return 1;
        }
        if (mesh_visible_filled > 1) {
            Logger::info("note: file authored "
                       + std::to_string(mesh_visible_filled)
                       + " visible non-empty mesh(es); GpuScene's "
                         "single-mesh slot uploaded only the first. "
                         "Multi-mesh support is a future slice.");
        }
    }

    auto r = rr::cuda::CudaRenderer::render_scene(gpu_scene, width, height);
    if (!r.ok) {
        Logger::error("render-full-scene failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-full-scene", width, height, r.gpu_time_ms);

    Logger::info("scene file       : " + cfg.scene_path);
    Logger::info("scene-render-full: "
               + std::to_string(sphere_pods.size())   + " sphere(s), "
               + std::to_string(material_pods.size()) + " material(s), "
               + std::to_string(light_pods.size())    + " light(s), "
               + std::to_string(mesh_to_upload != nullptr ? 1 : 0)
               + " mesh(es) uploaded "
               + "(authored "  + std::to_string(mesh_total)
               + ", visible+non-empty " + std::to_string(mesh_visible_filled)
               + ")");
    Logger::info("framebuffer      : "
               + std::to_string(width) + "x" + std::to_string(height)
               + " (from render_settings)");

    return save_image_or_error(r.image, out_path, "GPU full-scene-from-file",
                               width, height) ? 0 : 1;
#endif
}

// `--render-rng-test` dispatch. Stage 11A validation: invokes
// `CudaRenderer::render_rng_test` which writes a four-quadrant
// visualisation exercising every Stage 11A `pathtracer::*`
// primitive (white noise, 2D uniform, uniform hemisphere, cosine
// hemisphere). Width / height come from Config (`--width` /
// `--height`, defaults 1280x720); output defaults to
// "output/gpu_rng_test.ppm". The seed is fixed at 0 so re-runs are
// deterministic; future stages will surface a `--seed` flag once
// the path tracer needs frame-to-frame variation.
int run_render_rng_test(const rr::core::Config& cfg) {
    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/gpu_rng_test.ppm")
        : cfg.output_path;

#ifndef RR_HAS_CUDA
    (void)cfg;
    rr::core::Logger::error("--render-rng-test requires CUDA. Rebuild with "
                            "-DRR_ENABLE_CUDA=ON on a host with the CUDA "
                            "Toolkit and a CUDA-capable GPU.");
    return 1;
#else
    auto r = rr::cuda::CudaRenderer::render_rng_test(cfg.width, cfg.height,
                                                     /*seed=*/0u);
    if (!r.ok) {
        rr::core::Logger::error("rng-test render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-rng-test", cfg.width, cfg.height, r.gpu_time_ms);
    return save_image_or_error(r.image, out_path, "GPU RNG test",
                               cfg.width, cfg.height) ? 0 : 1;
#endif
}

// `--render-optix-triangle` dispatch (Stage 17A.4). Builds the
// closest-hit-augmented pipeline, uploads a single triangle that
// matches the CUDA `--render-triangle` fixture byte-for-byte,
// builds a single triangle GAS, optixLaunch'es the raygen which
// fires one primary ray per pixel; the closest-hit returns
// `0.5 * normal + 0.5` (normal-as-colour) and the miss program
// returns the same vertical sky gradient the CUDA path emits.
// Default output: `output/optix_triangle.ppm`.
int run_render_optix_triangle(const rr::core::Config& cfg) {
    using rr::core::Logger;

    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/optix_triangle.ppm")
        : cfg.output_path;

#ifndef RELATIVITYRENDER_ENABLE_OPTIX
    (void)cfg;
    Logger::error("--render-optix-triangle requires OptiX. Rebuild "
                  "with -DRR_ENABLE_OPTIX=ON on a "
                  "host with the CUDA Toolkit + OptiX SDK installed "
                  "(also pass -DOPTIX_ROOT=/path/to/optix-sdk).");
    return 1;
#else
    auto r = rr::optix::OptixRenderer::render_triangle(cfg.width, cfg.height);
    if (!r.ok) {
        Logger::error("optix triangle render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-optix-triangle", cfg.width, cfg.height, r.gpu_time_ms);

    namespace fs = std::filesystem;
    const fs::path out_fs = out_path;
    if (out_fs.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_fs.parent_path(), ec);
    }
    if (!r.image.save_ppm(out_fs)) {
        Logger::error("could not write PPM: " + out_path);
        return 1;
    }

    std::error_code ec;
    const fs::path  abs = fs::absolute(out_fs, ec);
    Logger::info(std::string("wrote OptiX triangle: ")
               + (ec ? out_path : abs.string())
               + " (" + std::to_string(cfg.width) + "x"
               + std::to_string(cfg.height) + ", RGBA32F)");
    return 0;
#endif
}

// `--render-optix-relativity` dispatch (Stage 17A.5). Drives
// the OptiX pipeline with the same single-triangle GAS that
// `--render-optix-triangle` uses, but with a non-zero observer
// velocity uploaded into the launch parameters. The OptiX
// raygen Lorentz-aberrates the primary ray; closest-hit /
// miss apply the Doppler colour shift + the bolometric
// searchlight scale. The relativity math leaf is shared with
// the CUDA path (`rr::relativity::*`) so behaviour matches
// pixel-for-pixel for matched inputs.
//
// Default output: `output/optix_relativity.ppm`. `--output`
// overrides. Requires `-DRR_ENABLE_OPTIX=ON` plus
// a host with the CUDA Toolkit + OptiX SDK installed; the
// audit-host fallback returns a clear "requires OptiX" error.
int run_render_optix_relativity(const rr::core::Config& cfg) {
    using rr::core::Logger;

    // Stage 20H: optional --beta modifier. Sentinel cfg.beta
    // (default -1.0f from Stage 19E.2) means "use the
    // historical 0.5 fixture + write output/optix_relativity.ppm
    // unchanged"; an explicit non-negative value picks the
    // beta magnitude AND derives the default output filename
    // `output/optix_relativity_beta{NNN}.ppm` (matching the
    // CUDA path's `--render-relativistic` 4-beta sweep
    // naming, e.g. beta=0.75 -> ..._beta075.ppm).
    const bool  user_beta_set    = (cfg.beta >= 0.0f);
    const float effective_beta   = user_beta_set ? cfg.beta : 0.5f;

    auto default_path_for_beta = [](float beta_value) -> std::string {
        // Encode |beta| as 3-digit integer (round-to-nearest).
        // Values >= 1.0 are clamped at 999 so the filename
        // stays 3-digit; the renderer's clampBeta will cap
        // the actual run at 0.999999. Negative inputs fold
        // to magnitude.
        if (beta_value < 0.0f) beta_value = -beta_value;
        int n = static_cast<int>(beta_value * 100.0f + 0.5f);
        if (n < 0)   n = 0;
        if (n > 999) n = 999;
        char buf[40];
        std::snprintf(buf, sizeof(buf),
                      "output/optix_relativity_beta%03d.ppm", n);
        return std::string(buf);
    };

    std::string out_path;
    if (!cfg.output_path.empty()) {
        out_path = cfg.output_path;
    } else if (user_beta_set) {
        out_path = default_path_for_beta(effective_beta);
    } else {
        out_path = "output/optix_relativity.ppm";
    }

#ifndef RELATIVITYRENDER_ENABLE_OPTIX
    (void)cfg;
    Logger::error("--render-optix-relativity requires OptiX. "
                  "Rebuild with -DRR_ENABLE_OPTIX="
                  "ON on a host with the CUDA Toolkit + OptiX "
                  "SDK installed (also pass -DOPTIX_ROOT=/path/"
                  "to/optix-sdk).");
    return 1;
#else
    auto r = rr::optix::OptixRenderer::render_relativistic(
        cfg.width, cfg.height, effective_beta);
    if (!r.ok) {
        Logger::error("optix relativistic render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-optix-relativity", cfg.width, cfg.height, r.gpu_time_ms);

    namespace fs = std::filesystem;
    const fs::path out_fs = out_path;
    if (out_fs.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_fs.parent_path(), ec);
    }
    if (!r.image.save_ppm(out_fs)) {
        Logger::error("could not write PPM: " + out_path);
        return 1;
    }

    std::error_code ec;
    const fs::path  abs = fs::absolute(out_fs, ec);
    Logger::info(std::string("wrote OptiX relativity: ")
               + (ec ? out_path : abs.string())
               + " (" + std::to_string(cfg.width) + "x"
               + std::to_string(cfg.height) + ", RGBA32F)");
    return 0;
#endif
}

// `--render-optix-raygen` dispatch (Stage 20C). Drives the
// raygen + miss + minimal-SBT + pipeline-creation surface
// without any visible geometry: builds a tiny triangle GAS
// placed BEHIND the camera (z = +5; default camera looks at
// -Z) so every primary ray misses and the miss program runs
// per pixel, producing the project's vertical sky-gradient
// environment colour. Closest-hit is in the SBT (since Stage
// 17A.4) but never fires for this scene shape.
//
// Default output: `output/optix_raygen.ppm`. `--output`
// overrides. Requires both `-DRR_ENABLE_OPTIX=ON` and a host
// with the CUDA Toolkit + OptiX SDK installed; the audit-
// host fallback returns a clear "requires OptiX" error.
int run_render_optix_raygen(const rr::core::Config& cfg) {
    using rr::core::Logger;

    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/optix_raygen.ppm")
        : cfg.output_path;

#ifndef RELATIVITYRENDER_ENABLE_OPTIX
    (void)cfg;
    Logger::error("--render-optix-raygen requires OptiX. "
                  "Rebuild with -DRR_ENABLE_OPTIX="
                  "ON on a host with the CUDA Toolkit + OptiX "
                  "SDK installed (also pass -DOPTIX_ROOT=/path/"
                  "to/optix-sdk).");
    return 1;
#else
    auto r = rr::optix::OptixRenderer::render_raygen(cfg.width, cfg.height);
    if (!r.ok) {
        Logger::error("optix raygen render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-optix-raygen", cfg.width, cfg.height, r.gpu_time_ms);

    namespace fs = std::filesystem;
    const fs::path out_fs = out_path;
    if (out_fs.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_fs.parent_path(), ec);
    }
    if (!r.image.save_ppm(out_fs)) {
        Logger::error("could not write PPM: " + out_path);
        return 1;
    }

    std::error_code ec;
    const fs::path  abs = fs::absolute(out_fs, ec);
    Logger::info(std::string("wrote OptiX raygen: ")
               + (ec ? out_path : abs.string())
               + " (" + std::to_string(cfg.width) + "x"
               + std::to_string(cfg.height) + ", RGBA32F)");
    return 0;
#endif
}

// `--render-optix-mesh-scene <file>` dispatch (Stage 20F).
// Loads a `.rrscene` file via the existing SceneLoader,
// hands the resulting `rr::scene::Scene` to
// `OptixRenderer::render_mesh_scene`, which builds an OptiX
// GAS from the first non-empty mesh and runs the existing
// raygen + miss + closest-hit pipeline (normal-as-color
// shading on hits + gradient sky on misses). No materials,
// no path tracing — Stage 20F rules.
//
// `cfg.scene_path` carries the file path. `cfg.width` /
// `cfg.height` populate the framebuffer dimensions and the
// camera aspect.
//
// Default output: `output/optix_mesh_scene.ppm`. `--output`
// overrides. Requires both `-DRR_ENABLE_OPTIX=ON` and a host
// with the CUDA Toolkit + OptiX SDK installed; the audit-host
// fallback returns the documented "requires OptiX" error.
int run_render_optix_mesh_scene(const rr::core::Config& cfg) {
    using rr::core::Logger;

    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/optix_mesh_scene.ppm")
        : cfg.output_path;

    if (cfg.scene_path.empty()) {
        Logger::error("--render-optix-mesh-scene requires a .rrscene "
                      "file path argument.");
        return 1;
    }

    // Scene load runs on the audit host too — `rr_io` is host-
    // only and the loader does not require CUDA / OptiX. We
    // load up front so a missing file or parse failure surfaces
    // with the same diagnostic on every host.
    if (!rr::io::sceneFileExists(cfg.scene_path)) {
        Logger::error("scene file not found: " + cfg.scene_path);
        return 1;
    }
    auto load = rr::io::load(cfg.scene_path);
    if (!load.ok) {
        Logger::error("failed to load scene '" + cfg.scene_path
                    + "': " + load.error_message);
        return 1;
    }

#ifndef RELATIVITYRENDER_ENABLE_OPTIX
    Logger::error("--render-optix-mesh-scene requires OptiX. "
                  "Rebuild with -DRR_ENABLE_OPTIX="
                  "ON on a host with the CUDA Toolkit + OptiX "
                  "SDK installed (also pass -DOPTIX_ROOT=/path/"
                  "to/optix-sdk).");
    return 1;
#else
    auto r = rr::optix::OptixRenderer::render_mesh_scene(
        load.scene, cfg.width, cfg.height);
    if (!r.ok) {
        Logger::error("optix mesh-scene render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-optix-mesh-scene", cfg.width, cfg.height,
                   r.gpu_time_ms);

    namespace fs = std::filesystem;
    const fs::path out_fs = out_path;
    if (out_fs.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_fs.parent_path(), ec);
    }
    if (!r.image.save_ppm(out_fs)) {
        Logger::error("could not write PPM: " + out_path);
        return 1;
    }

    std::error_code ec;
    const fs::path  abs = fs::absolute(out_fs, ec);
    Logger::info(std::string("wrote OptiX mesh scene: ")
               + (ec ? out_path : abs.string())
               + " (" + std::to_string(cfg.width) + "x"
               + std::to_string(cfg.height) + ", RGBA32F)");
    return 0;
#endif
}

// `--render-optix-material-scene <file>` dispatch (Stage 20G).
// Same load + dispatch shape as `--render-optix-mesh-scene`,
// but calls `OptixRenderer::render_material_scene` which
// populates the hit-group SBT record with the picked mesh's
// material params and asks the closest-hit to emit
// `baseColor + emissionColor * emissionStrength` instead of
// normal-as-color.
//
// Default output: `output/optix_material_scene.ppm`. `--output`
// overrides. Requires both `-DRR_ENABLE_OPTIX=ON` and a host
// with the CUDA Toolkit + OptiX SDK installed; the audit-host
// fallback returns the documented "requires OptiX" error.
int run_render_optix_material_scene(const rr::core::Config& cfg) {
    using rr::core::Logger;

    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/optix_material_scene.ppm")
        : cfg.output_path;

    if (cfg.scene_path.empty()) {
        Logger::error("--render-optix-material-scene requires a .rrscene "
                      "file path argument.");
        return 1;
    }

    if (!rr::io::sceneFileExists(cfg.scene_path)) {
        Logger::error("scene file not found: " + cfg.scene_path);
        return 1;
    }
    auto load = rr::io::load(cfg.scene_path);
    if (!load.ok) {
        Logger::error("failed to load scene '" + cfg.scene_path
                    + "': " + load.error_message);
        return 1;
    }

#ifndef RELATIVITYRENDER_ENABLE_OPTIX
    Logger::error("--render-optix-material-scene requires OptiX. "
                  "Rebuild with -DRR_ENABLE_OPTIX="
                  "ON on a host with the CUDA Toolkit + OptiX "
                  "SDK installed (also pass -DOPTIX_ROOT=/path/"
                  "to/optix-sdk).");
    return 1;
#else
    auto r = rr::optix::OptixRenderer::render_material_scene(
        load.scene, cfg.width, cfg.height);
    if (!r.ok) {
        Logger::error("optix material-scene render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-optix-material-scene", cfg.width, cfg.height,
                   r.gpu_time_ms);

    namespace fs = std::filesystem;
    const fs::path out_fs = out_path;
    if (out_fs.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_fs.parent_path(), ec);
    }
    if (!r.image.save_ppm(out_fs)) {
        Logger::error("could not write PPM: " + out_path);
        return 1;
    }

    std::error_code ec;
    const fs::path  abs = fs::absolute(out_fs, ec);
    Logger::info(std::string("wrote OptiX material scene: ")
               + (ec ? out_path : abs.string())
               + " (" + std::to_string(cfg.width) + "x"
               + std::to_string(cfg.height) + ", RGBA32F)");
    return 0;
#endif
}

// `--render-optix-pathtrace <file>` dispatch (Stage 20I).
// Loads a `.rrscene` file via the existing SceneLoader and
// runs the OptiX path tracer twice — spp=1 + spp=16 — writing
// two PPMs:
//   output/optix_pathtrace_spp1.ppm
//   output/optix_pathtrace_spp16.ppm
//
// Mirrors the CUDA `--render-pathtrace`'s 2-spp shape (which
// writes `output/pathtrace_spp_1.ppm` + `output/pathtrace_spp_16.ppm`)
// so the OptiX path is comparable. `--output` is ignored;
// the output paths are fixed for this action (the action
// produces two outputs, not one).
//
// max_bounces defaults to 3 (matches CUDA path tracer's
// default for `--render-pathtrace`); seed defaults to 0.
//
// Requires both `-DRR_ENABLE_OPTIX=ON` and a host with the
// CUDA Toolkit + OptiX SDK installed; the audit-host fallback
// returns the documented "requires OptiX" error after
// successfully loading the scene file (so loader-side bugs
// surface honestly on the audit host).
int run_render_optix_pathtrace(const rr::core::Config& cfg) {
    using rr::core::Logger;

    if (cfg.scene_path.empty()) {
        Logger::error("--render-optix-pathtrace requires a .rrscene "
                      "file path argument.");
        return 1;
    }

    if (!rr::io::sceneFileExists(cfg.scene_path)) {
        Logger::error("scene file not found: " + cfg.scene_path);
        return 1;
    }
    auto load = rr::io::load(cfg.scene_path);
    if (!load.ok) {
        Logger::error("failed to load scene '" + cfg.scene_path
                    + "': " + load.error_message);
        return 1;
    }

#ifndef RELATIVITYRENDER_ENABLE_OPTIX
    Logger::error("--render-optix-pathtrace requires OptiX. "
                  "Rebuild with -DRR_ENABLE_OPTIX="
                  "ON on a host with the CUDA Toolkit + OptiX "
                  "SDK installed (also pass -DOPTIX_ROOT=/path/"
                  "to/optix-sdk).");
    return 1;
#else
    // Stage 20J: progressive accumulation. One render call;
    // the renderer iterates samples internally + snaps a
    // resolved image at each requested checkpoint. Output is
    // bit-identical to the Stage 20I single-launch path
    // (raygen seed combines optixLaunchParams.sample_index +
    // the in-raygen loop counter, so spp=N with sample_index=0
    // and N x spp=1 with sample_index=0..N-1 produce the same
    // RNG sequence and the same accumulated radiance).
    constexpr int kMaxBounces = 3;
    constexpr unsigned int kSeed = 0u;
    const std::vector<int> kCheckpoints = { 1, 16 };

    auto pr = rr::optix::OptixRenderer::render_pathtrace_progressive(
        load.scene, cfg.width, cfg.height,
        kMaxBounces, kSeed, kCheckpoints);
    if (!pr.ok) {
        Logger::error("optix path-trace progressive render failed: "
                    + pr.message);
        return 1;
    }
    log_gpu_timing("render-optix-pathtrace (progressive total)",
                   cfg.width, cfg.height, pr.total_gpu_time_ms);

    int failures = 0;
    for (const auto& cp : pr.checkpoints) {
        std::string out_path;
        if (cp.sample_count == 1) {
            out_path = "output/optix_pathtrace_spp1.ppm";
        } else if (cp.sample_count == 16) {
            out_path = "output/optix_pathtrace_spp16.ppm";
        } else {
            // Future-proof: derive a path from the sample
            // count. Today only spp=1 and spp=16 are
            // requested by this action.
            out_path = "output/optix_pathtrace_spp"
                     + std::to_string(cp.sample_count) + ".ppm";
        }

        namespace fs = std::filesystem;
        const fs::path out_fs = out_path;
        if (out_fs.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(out_fs.parent_path(), ec);
        }
        if (!cp.image.save_ppm(out_fs)) {
            Logger::error(std::string("could not write PPM: ") + out_path);
            ++failures;
            continue;
        }

        std::error_code ec;
        const fs::path  abs = fs::absolute(out_fs, ec);
        Logger::info(std::string("wrote OptiX pathtrace (spp=")
                   + std::to_string(cp.sample_count) + "): "
                   + (ec ? out_path : abs.string())
                   + " (" + std::to_string(cfg.width) + "x"
                   + std::to_string(cfg.height) + ", RGBA32F)");
    }

    return failures == 0 ? 0 : 1;
#endif
}

// `--render-optix-direct-lighting <file>` dispatch (Stage 20K).
// Loads a `.rrscene` file via the existing SceneLoader and
// runs the OptiX closest-hit's direct-lighting branch (point
// + directional + emission + environment ambient) at the
// primary hit. No path tracing, no shadow rays.
//
// Default output: `output/optix_direct_lighting.ppm`.
// `--output` overrides. Requires both `-DRR_ENABLE_OPTIX=ON`
// and a host with the CUDA Toolkit + OptiX SDK installed; the
// audit-host fallback returns the documented "requires
// OptiX" error after a successful scene-load.
int run_render_optix_direct_lighting(const rr::core::Config& cfg) {
    using rr::core::Logger;

    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/optix_direct_lighting.ppm")
        : cfg.output_path;

    if (cfg.scene_path.empty()) {
        Logger::error("--render-optix-direct-lighting requires a "
                      ".rrscene file path argument.");
        return 1;
    }

    if (!rr::io::sceneFileExists(cfg.scene_path)) {
        Logger::error("scene file not found: " + cfg.scene_path);
        return 1;
    }
    auto load = rr::io::load(cfg.scene_path);
    if (!load.ok) {
        Logger::error("failed to load scene '" + cfg.scene_path
                    + "': " + load.error_message);
        return 1;
    }

#ifndef RELATIVITYRENDER_ENABLE_OPTIX
    Logger::error("--render-optix-direct-lighting requires OptiX. "
                  "Rebuild with -DRR_ENABLE_OPTIX="
                  "ON on a host with the CUDA Toolkit + OptiX "
                  "SDK installed (also pass -DOPTIX_ROOT=/path/"
                  "to/optix-sdk).");
    return 1;
#else
    auto r = rr::optix::OptixRenderer::render_direct_lighting(
        load.scene, cfg.width, cfg.height);
    if (!r.ok) {
        Logger::error("optix direct-lighting render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-optix-direct-lighting", cfg.width, cfg.height,
                   r.gpu_time_ms);

    namespace fs = std::filesystem;
    const fs::path out_fs = out_path;
    if (out_fs.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_fs.parent_path(), ec);
    }
    if (!r.image.save_ppm(out_fs)) {
        Logger::error("could not write PPM: " + out_path);
        return 1;
    }

    std::error_code ec;
    const fs::path  abs = fs::absolute(out_fs, ec);
    Logger::info(std::string("wrote OptiX direct lighting: ")
               + (ec ? out_path : abs.string())
               + " (" + std::to_string(cfg.width) + "x"
               + std::to_string(cfg.height) + ", RGBA32F)");
    return 0;
#endif
}

// `--render-optix-shadow-test <file>` dispatch (Stage 20L).
// Same scene-load + dispatch shape as `--render-optix-direct-lighting`,
// but calls `OptixRenderer::render_direct_lighting` with
// `enable_shadows = true`. Output:
// `output/optix_shadow_test.ppm` (default; `--output` overrides).
int run_render_optix_shadow_test(const rr::core::Config& cfg) {
    using rr::core::Logger;

    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/optix_shadow_test.ppm")
        : cfg.output_path;

    if (cfg.scene_path.empty()) {
        Logger::error("--render-optix-shadow-test requires a "
                      ".rrscene file path argument.");
        return 1;
    }

    if (!rr::io::sceneFileExists(cfg.scene_path)) {
        Logger::error("scene file not found: " + cfg.scene_path);
        return 1;
    }
    auto load = rr::io::load(cfg.scene_path);
    if (!load.ok) {
        Logger::error("failed to load scene '" + cfg.scene_path
                    + "': " + load.error_message);
        return 1;
    }

#ifndef RELATIVITYRENDER_ENABLE_OPTIX
    Logger::error("--render-optix-shadow-test requires OptiX. "
                  "Rebuild with -DRR_ENABLE_OPTIX="
                  "ON on a host with the CUDA Toolkit + OptiX "
                  "SDK installed (also pass -DOPTIX_ROOT=/path/"
                  "to/optix-sdk).");
    return 1;
#else
    auto r = rr::optix::OptixRenderer::render_direct_lighting(
        load.scene, cfg.width, cfg.height,
        /*enable_shadows=*/true);
    if (!r.ok) {
        Logger::error("optix shadow-test render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-optix-shadow-test", cfg.width, cfg.height,
                   r.gpu_time_ms);

    namespace fs = std::filesystem;
    const fs::path out_fs = out_path;
    if (out_fs.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_fs.parent_path(), ec);
    }
    if (!r.image.save_ppm(out_fs)) {
        Logger::error("could not write PPM: " + out_path);
        return 1;
    }

    std::error_code ec;
    const fs::path  abs = fs::absolute(out_fs, ec);
    Logger::info(std::string("wrote OptiX shadow test: ")
               + (ec ? out_path : abs.string())
               + " (" + std::to_string(cfg.width) + "x"
               + std::to_string(cfg.height) + ", RGBA32F)");
    return 0;
#endif
}

// `--render-optix-textured-material` dispatch (Stage 20M).
// Mirrors the CUDA `--render-textured-material` shape: takes
// no scene file, constructs a procedural textured-quad scene
// + 2x2 four-colour reference texture inline, then runs the
// OptiX closest-hit's material-flat branch with texture
// sampling enabled.
//
// Default output: `output/optix_textured_material.ppm`.
// `--output` overrides. Requires both `-DRR_ENABLE_OPTIX=ON`
// and a host with the CUDA Toolkit + OptiX SDK installed; the
// audit-host fallback returns the documented "requires
// OptiX" error.
int run_render_optix_textured_material(const rr::core::Config& cfg) {
    using rr::core::Logger;

    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/optix_textured_material.ppm")
        : cfg.output_path;

    // Build a scene with a textured quad + the matching
    // material. Mirrors the CUDA --render-textured-material
    // dispatcher's shape so the OptiX output is comparable.
    rr::scene::Scene scene;
    scene.render_settings.width  = cfg.width;
    scene.render_settings.height = cfg.height;
    scene.camera.set_aspect(static_cast<float>(cfg.width)
                          / static_cast<float>(cfg.height));

    rr::material::MaterialParams textured_params;
    textured_params.baseColor           = rr::math::Vec3{0.65f, 0.65f, 0.65f};
    textured_params.useBaseColorTexture = true;
    textured_params.baseColorTextureId  = 0;
    scene.materials.push_back({0, "textured", textured_params});

    rr::geometry::Mesh quad;
    quad.vertices.push_back({rr::math::Vec3{-3.0f, -3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.0f, 1.0f}});
    quad.vertices.push_back({rr::math::Vec3{ 3.0f, -3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{1.0f, 1.0f}});
    quad.vertices.push_back({rr::math::Vec3{ 3.0f,  3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{1.0f, 0.0f}});
    quad.vertices.push_back({rr::math::Vec3{-3.0f,  3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.0f, 0.0f}});
    quad.triangles.push_back({0, 1, 2});
    quad.triangles.push_back({0, 2, 3});
    quad.material_id = 0;

    rr::scene::SceneMesh smesh;
    smesh.object.name  = "textured-quad";
    smesh.geometry     = std::move(quad);
    scene.meshes.push_back(std::move(smesh));

    // 2x2 four-colour reference texture (top-left red,
    // top-right green, bottom-left blue, bottom-right yellow).
    // Same pattern as the CUDA --render-texture-sample-test +
    // --render-textured-material; uv = (0, 0) at the top-left
    // texel. Stored as Rgba8.
    std::vector<rr::texture::ImageTexture> textures;
    {
        rr::texture::ImageTexture tex0(
            2, 2,
            rr::texture::ImageTextureFormat::Rgba8,
            "textured_material_pattern");
        const unsigned char rgba_bytes[16] = {
            255,   0,   0, 255,    0, 255,   0, 255,
              0,   0, 255, 255,  255, 255,   0, 255,
        };
        tex0.pixels().resize(sizeof rgba_bytes);
        std::memcpy(tex0.pixels().data(), rgba_bytes, sizeof rgba_bytes);
        textures.push_back(std::move(tex0));
    }

#ifndef RELATIVITYRENDER_ENABLE_OPTIX
    Logger::error("--render-optix-textured-material requires OptiX. "
                  "Rebuild with -DRR_ENABLE_OPTIX="
                  "ON on a host with the CUDA Toolkit + OptiX "
                  "SDK installed (also pass -DOPTIX_ROOT=/path/"
                  "to/optix-sdk).");
    return 1;
#else
    auto r = rr::optix::OptixRenderer::render_textured_material(
        scene, textures, cfg.width, cfg.height);
    if (!r.ok) {
        Logger::error("optix textured-material render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-optix-textured-material",
                   cfg.width, cfg.height, r.gpu_time_ms);

    namespace fs = std::filesystem;
    const fs::path out_fs = out_path;
    if (out_fs.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_fs.parent_path(), ec);
    }
    if (!r.image.save_ppm(out_fs)) {
        Logger::error("could not write PPM: " + out_path);
        return 1;
    }

    std::error_code ec;
    const fs::path  abs = fs::absolute(out_fs, ec);
    Logger::info(std::string("wrote OptiX textured material: ")
               + (ec ? out_path : abs.string())
               + " (" + std::to_string(cfg.width) + "x"
               + std::to_string(cfg.height) + ", RGBA32F)");
    return 0;
#endif
}

// `--render-optix-aovs` dispatch (Stage 20N).
// Mirrors the CUDA `--render-aovs` dispatcher's surface (no
// scene argument, fixed AOV output paths). Builds a small
// procedural multi-light + textured-quad scene inline (the
// OptiX path is mesh-only, so spheres are intentionally
// omitted), then calls `OptixRenderer::render_aovs` which
// allocates the six per-pixel device buffers, threads them
// through `OptixLaunchParams`, and runs the existing direct-
// lighting closest-hit. The raygen / closest-hit / miss
// programs write Beauty / Normal / Depth / Albedo /
// DopplerFactor / SearchlightFactor.
//
// The renderer itself sets the observer velocity to
// beta = (0, 0, -0.5) so the Doppler / searchlight AOVs
// show visible variation across the framebuffer (mirrors the
// CUDA --render-aovs choice exactly).
//
// Output paths are fixed (mirrors --render-aovs):
//   output/optix_aov_beauty.ppm,
//   output/optix_aov_normal.ppm,
//   output/optix_aov_depth.ppm,
//   output/optix_aov_albedo.ppm,
//   output/optix_aov_doppler.ppm,
//   output/optix_aov_searchlight.ppm.
// `--output` is intentionally ignored (mirrors --render-aovs).
// Requires both `-DRR_ENABLE_OPTIX=ON` and a host with the CUDA
// Toolkit + OptiX SDK installed; the audit-host fallback returns
// the documented "requires OptiX" error.
int run_render_optix_aovs(const rr::core::Config& cfg) {
    using rr::core::Logger;

    rr::scene::Scene scene;
    scene.render_settings.width  = cfg.width;
    scene.render_settings.height = cfg.height;
    scene.camera.set_aspect(static_cast<float>(cfg.width)
                          / static_cast<float>(cfg.height));

    // Single neutral diffuse material for the quad. The OptiX
    // direct-lighting branch evaluates albedo * (direct +
    // ambient) + emission; a mid-grey baseColor lets every
    // light's contribution show through clearly across the
    // Beauty + Albedo AOVs.
    rr::material::MaterialParams neutral_params{};
    neutral_params.baseColor = rr::math::Vec3{0.65f, 0.65f, 0.65f};
    scene.materials.push_back({0, "neutral", neutral_params});

    // Single front-facing quad mesh (positions / normals / UVs).
    // Same shape as the CUDA --render-aovs dispatcher's quad.
    rr::geometry::Mesh quad;
    quad.vertices.push_back({rr::math::Vec3{-3.0f, -3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.0f, 0.0f}});
    quad.vertices.push_back({rr::math::Vec3{ 3.0f, -3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{1.0f, 0.0f}});
    quad.vertices.push_back({rr::math::Vec3{ 3.0f,  3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{1.0f, 1.0f}});
    quad.vertices.push_back({rr::math::Vec3{-3.0f,  3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.0f, 1.0f}});
    quad.triangles.push_back({0, 1, 2});
    quad.triangles.push_back({0, 2, 3});
    quad.material_id = 0;

    rr::scene::SceneMesh smesh;
    smesh.object.name = "aov-quad";
    smesh.geometry    = std::move(quad);
    scene.meshes.push_back(std::move(smesh));

    // Three lights matching the CUDA --render-aovs dispatcher:
    // directional key, warm point fill, cool environment ambient.
    std::vector<rr::lighting::Light> lights;
    lights.push_back(rr::lighting::make_directional_light(
        rr::math::Vec3{-0.4f, -0.7f, -0.6f},
        rr::math::Vec3{1.0f, 0.95f, 0.85f},
        /*intensity=*/0.9f));
    lights.push_back(rr::lighting::make_point_light(
        rr::math::Vec3{2.0f, 1.5f, -2.5f},
        rr::math::Vec3{1.0f, 0.85f, 0.6f},
        /*intensity=*/30.0f));
    lights.push_back(rr::lighting::make_environment_light(
        rr::math::Vec3{0.55f, 0.65f, 0.85f},
        /*intensity=*/0.25f));

#ifndef RELATIVITYRENDER_ENABLE_OPTIX
    Logger::error("--render-optix-aovs requires OptiX. Rebuild "
                  "with -DRR_ENABLE_OPTIX=ON on a "
                  "host with the CUDA Toolkit + OptiX SDK "
                  "installed (also pass -DOPTIX_ROOT=/path/to/"
                  "optix-sdk).");
    return 1;
#else
    auto r = rr::optix::OptixRenderer::render_aovs(
        scene, lights, cfg.width, cfg.height);
    if (!r.ok) {
        Logger::error("optix aovs render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-optix-aovs",
                   cfg.width, cfg.height, r.gpu_time_ms);

    namespace fs = std::filesystem;
    auto save_one = [&](const rr::image::Image& img,
                        const char* path,
                        const char* label) -> bool {
        const fs::path p = path;
        if (p.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
        }
        if (!img.save_ppm(p)) {
            Logger::error(std::string("could not write PPM: ") + path);
            return false;
        }
        std::error_code ec;
        const fs::path abs = fs::absolute(p, ec);
        Logger::info(std::string("wrote ") + label + ": "
                   + (ec ? std::string(path) : abs.string())
                   + " (" + std::to_string(cfg.width) + "x"
                   + std::to_string(cfg.height) + ", RGB32F)");
        return true;
    };

    bool all_ok = true;
    all_ok &= save_one(r.beauty,
                       "output/optix_aov_beauty.ppm",
                       "OptiX AOV beauty");
    all_ok &= save_one(r.normal,
                       "output/optix_aov_normal.ppm",
                       "OptiX AOV normal");
    all_ok &= save_one(r.depth,
                       "output/optix_aov_depth.ppm",
                       "OptiX AOV depth");
    all_ok &= save_one(r.albedo,
                       "output/optix_aov_albedo.ppm",
                       "OptiX AOV albedo");
    all_ok &= save_one(r.doppler_factor,
                       "output/optix_aov_doppler.ppm",
                       "OptiX AOV doppler");
    all_ok &= save_one(r.searchlight_factor,
                       "output/optix_aov_searchlight.ppm",
                       "OptiX AOV searchlight");
    return all_ok ? 0 : 1;
#endif
}

// `--render-optix-test` dispatch (Stage 17A.3). Drives the
// minimum-viable OptiX pipeline: initialise OptixBackend, build
// pipeline (raygen + miss; no closest-hit, no path tracer),
// allocate framebuffer, optixLaunch the raygen which writes a
// flat colour, download, save PPM. CUDA path is unaffected.
//
// Default output: `output/optix_test.ppm`. `--output` overrides.
// Requires both `-DRR_ENABLE_OPTIX=ON` and a host
// with the CUDA Toolkit + OptiX SDK installed; the audit-host
// fallback returns a clear "requires OptiX" error.
int run_render_optix_test(const rr::core::Config& cfg) {
    using rr::core::Logger;

    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/optix_test.ppm")
        : cfg.output_path;

#ifndef RELATIVITYRENDER_ENABLE_OPTIX
    (void)cfg;
    Logger::error("--render-optix-test requires OptiX. Rebuild "
                  "with -DRR_ENABLE_OPTIX=ON on a "
                  "host with the CUDA Toolkit + OptiX SDK "
                  "installed (also pass -DOPTIX_ROOT=/path/to/"
                  "optix-sdk).");
    return 1;
#else
    auto r = rr::optix::OptixRenderer::render_test(cfg.width, cfg.height);
    if (!r.ok) {
        Logger::error("optix test render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-optix-test", cfg.width, cfg.height, r.gpu_time_ms);

    namespace fs = std::filesystem;
    const fs::path out_fs = out_path;
    if (out_fs.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_fs.parent_path(), ec);
    }
    if (!r.image.save_ppm(out_fs)) {
        Logger::error("could not write PPM: " + out_path);
        return 1;
    }

    std::error_code ec;
    const fs::path  abs = fs::absolute(out_fs, ec);
    Logger::info(std::string("wrote OptiX test: ")
               + (ec ? out_path : abs.string())
               + " (" + std::to_string(cfg.width) + "x"
               + std::to_string(cfg.height) + ", RGBA32F)");
    return 0;
#endif
}

// `--render-texture-sample-test` dispatch. Stage 13B.2 validation
// path: synthesise a 2x2 RGBA8 four-colour test pattern on the
// host (CudaRenderer's job), upload it via `rr::gpu::GpuTexture`,
// launch the kernel that maps every output pixel through
// `sampleTextureNearest(view, uv)`, and save the resulting PPM.
// All per-pixel work runs on the device; the host only owns the
// upload + the final download / save.
//
// With clamp-to-edge nearest sampling on the 2x2 pattern the
// output is exactly four solid colour quadrants; any other
// pattern (banding, swapped channels, magenta = invalid view
// fallback) is a regression in upload, sampler, or UV mapping.
int run_render_texture_sample_test(const rr::core::Config& cfg) {
    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/gpu_texture_sample_test.ppm")
        : cfg.output_path;

#ifndef RR_HAS_CUDA
    (void)cfg;
    rr::core::Logger::error("--render-texture-sample-test requires CUDA. "
                            "Rebuild with -DRR_ENABLE_CUDA=ON on a host "
                            "with the CUDA Toolkit and a CUDA-capable "
                            "GPU.");
    return 1;
#else
    auto r = rr::cuda::CudaRenderer::render_texture_sample_test(
        cfg.width, cfg.height);
    if (!r.ok) {
        rr::core::Logger::error("texture-sample-test render failed: "
                              + r.message);
        return 1;
    }
    log_gpu_timing("render-texture-sample-test",
                   cfg.width, cfg.height, r.gpu_time_ms);
    return save_image_or_error(r.image, out_path,
                               "GPU texture sample test",
                               cfg.width, cfg.height) ? 0 : 1;
#endif
}

// `--render-accumulation-test` dispatch. Stage 11B validation
// path: allocate an AccumulationBuffer + a device-side sample
// buffer, loop `kSampleCount` iterations producing a fresh
// per-pixel `(next_float, next_float, next_float, 1.0)` sample
// frame each iteration, accumulate, then resolve to a host Image
// and save. Each per-pixel write happens on the device; the host
// only owns buffer lifetimes and the iteration count. Output
// converges to a visually uniform mid-gray.
//
// The orchestration lives here (rather than as a static method on
// `CudaRenderer`) so the `rr_renderer` library that owns
// `AccumulationBuffer` doesn't need to be linked from inside the
// `rr_gpu` static lib - the executable already links both, so
// the dependency direction stays one-way:
// rr_renderer -> rr_gpu only.
int run_render_accumulation_test(const rr::core::Config& cfg) {
    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/gpu_accumulation_test.ppm")
        : cfg.output_path;

#ifndef RR_HAS_CUDA
    (void)cfg;
    rr::core::Logger::error("--render-accumulation-test requires CUDA. "
                            "Rebuild with -DRR_ENABLE_CUDA=ON on a host "
                            "with the CUDA Toolkit and a CUDA-capable "
                            "GPU.");
    return 1;
#else
    constexpr int kSampleCount = 64;
    constexpr unsigned int kSeed = 0u;

    rr::renderer::AccumulationBuffer accum;
    if (!accum.resize(cfg.width, cfg.height) || !accum.valid()) {
        rr::core::Logger::error("accumulation-test: AccumulationBuffer "
                                "allocation failed");
        return 1;
    }

    const std::size_t float_count =
        static_cast<std::size_t>(cfg.width) * cfg.height * 4u;

    rr::gpu::GpuBuffer<float> sample;
    if (!sample.allocate(float_count)) {
        rr::core::Logger::error("accumulation-test: sample buffer "
                                "allocation failed");
        return 1;
    }

    for (int i = 0; i < kSampleCount; ++i) {
        if (!rr::cuda::launch_random_rgba_sample(
                sample.device_ptr(), cfg.width, cfg.height,
                kSeed, static_cast<unsigned int>(i))) {
            rr::core::Logger::error("accumulation-test: sample-source "
                                    "kernel launch failed at iteration "
                                  + std::to_string(i));
            return 1;
        }
        if (!accum.accumulate_sample(sample.device_ptr())) {
            rr::core::Logger::error("accumulation-test: "
                                    "accumulate_sample failed at "
                                    "iteration " + std::to_string(i));
            return 1;
        }
    }

    rr::image::Image img = accum.resolve_to_image();
    if (img.empty()) {
        rr::core::Logger::error("accumulation-test: resolve_to_image "
                                "returned empty");
        return 1;
    }

    rr::core::Logger::info("accumulation-test: "
                         + std::to_string(accum.samples_count())
                         + " samples accumulated, "
                         + std::to_string(cfg.width) + "x"
                         + std::to_string(cfg.height) + " framebuffer");

    return save_image_or_error(img, out_path,
                               "GPU accumulation test",
                               cfg.width, cfg.height) ? 0 : 1;
#endif
}

// `--render-pathtrace` dispatch. Stage 11C - the first action
// that runs the GPU path tracer. CPU loads the .rrscene, uploads
// camera + relativity + materials + spheres + first visible
// non-empty mesh + lights to a GpuScene, then drives the
// host-side `PathTracer` once at spp = 1 and once at spp = 16,
// writing both PPMs. The two outputs let an artist eyeball
// progressive convergence (spp_1 is noisy; spp_16 is markedly
// smoother).
//
// Resolution comes from the scene's `render_settings`; `--width`
// / `--height` are intentionally ignored, same policy as
// `--render-from-scene` / `--render-full-scene`. `--output` is
// also ignored - the two PPM paths are fixed by the prompt and
// matching `--render-relativistic`'s precedent of writing
// multiple fixed paths per launch.
//
// Lights are uploaded but the path tracer does not directly
// sample them in this slice (no MIS / NEE yet); illumination
// comes from emissive surface hits and the environment fallback
// (configured from a default sky tint inside `PathTraceConfig`).
int run_render_pathtrace(const rr::core::Config& cfg) {
    using rr::core::Logger;

    if (cfg.scene_path.empty()) {
        Logger::error("--render-pathtrace requires a file path");
        return 2;
    }

    const auto loaded = rr::io::load(cfg.scene_path);
    if (!loaded.ok) {
        std::string msg = "scene load failed: " + loaded.error_message;
        if (loaded.error_line > 0) {
            msg += " (line " + std::to_string(loaded.error_line)
                +  ", column " + std::to_string(loaded.error_column) + ")";
        }
        Logger::error(msg);
        return 1;
    }

    const auto& scene  = loaded.scene;
    const int   width  = scene.render_settings.width;
    const int   height = scene.render_settings.height;

#ifndef RR_HAS_CUDA
    (void)scene;
    (void)width;
    (void)height;
    Logger::error("--render-pathtrace requires CUDA. Rebuild with "
                  "-DRR_ENABLE_CUDA=ON on a host with the CUDA Toolkit "
                  "and a CUDA-capable GPU.");
    return 1;
#else
    // Same flatten + upload chain as --render-full-scene.
    std::vector<rr::geometry::Sphere> sphere_pods;
    sphere_pods.reserve(scene.spheres.size());
    for (const auto& s : scene.spheres) {
        if (s.object.visible) sphere_pods.push_back(s.geometry);
    }
    std::vector<rr::material::MaterialParams> material_pods;
    material_pods.reserve(scene.materials.size());
    for (const auto& m : scene.materials) material_pods.push_back(m.params);
    std::vector<rr::lighting::Light> light_pods;
    light_pods.reserve(scene.lights.size());
    for (const auto& l : scene.lights) {
        if (l.object.visible) light_pods.push_back(l.data);
    }
    const rr::geometry::Mesh* mesh_to_upload = nullptr;
    for (const auto& sm : scene.meshes) {
        if (!sm.object.visible) continue;
        if (sm.geometry.empty()) continue;
        mesh_to_upload = &sm.geometry;
        break;
    }

    rr::gpu::GpuScene gpu_scene;
    if (!gpu_scene.upload_camera(scene.camera)) {
        Logger::error("pathtrace failed: upload_camera"); return 1;
    }
    if (!gpu_scene.upload_relativity(scene.observer, scene.relativity)) {
        Logger::error("pathtrace failed: upload_relativity"); return 1;
    }
    if (!sphere_pods.empty()
     && !gpu_scene.upload_spheres(sphere_pods.data(), sphere_pods.size())) {
        Logger::error("pathtrace failed: upload_spheres"); return 1;
    }
    if (!material_pods.empty()
     && !gpu_scene.upload_materials(material_pods.data(),
                                    material_pods.size())) {
        Logger::error("pathtrace failed: upload_materials"); return 1;
    }
    if (!light_pods.empty()
     && !gpu_scene.upload_lights(light_pods.data(), light_pods.size())) {
        Logger::error("pathtrace failed: upload_lights"); return 1;
    }
    if (mesh_to_upload != nullptr
     && !gpu_scene.upload_mesh(*mesh_to_upload)) {
        Logger::error("pathtrace failed: upload_mesh"); return 1;
    }

    // Stage 11C writes two PPMs per invocation: spp = 1 (noisy)
    // and spp = 16 (markedly smoother). Keeping both call sites
    // compact + identical-shape so the convergence comparison
    // depends only on `samples_per_pixel`.
    struct SppRun { int spp; const char* path; const char* label; };
    constexpr SppRun kRuns[] = {
        { 1,  "output/pathtrace_spp_1.ppm",  "pathtrace spp=1"  },
        {16,  "output/pathtrace_spp_16.ppm", "pathtrace spp=16" },
    };

    rr::pathtracer::PathTracer pt;
    int failures = 0;
    for (const auto& run : kRuns) {
        rr::pathtracer::PathTraceConfig pcfg;
        pcfg.samples_per_pixel = run.spp;
        // Other PathTraceConfig fields (max_bounces, seed,
        // environment_color, environment_intensity) keep their
        // defaults. The defaults produce a moderate cool sky tint
        // so a scene without emissive surfaces still produces a
        // visible image.

        auto r = pt.render(gpu_scene, width, height, pcfg);
        if (!r.ok) {
            Logger::error(std::string(run.label) + " failed: "
                                + r.message);
            ++failures;
            continue;
        }
        log_gpu_timing(run.label, width, height, r.gpu_time_ms);

        Logger::info(std::string("scene file       : ") + cfg.scene_path);
        Logger::info("framebuffer      : "
                   + std::to_string(width) + "x" + std::to_string(height)
                   + " (from render_settings)");
        Logger::info(std::string("pathtrace        : ")
                   + std::to_string(run.spp) + " spp, "
                   + std::to_string(pcfg.max_bounces) + " bounces, "
                   + std::to_string(sphere_pods.size())   + " sphere(s), "
                   + std::to_string(material_pods.size()) + " material(s), "
                   + std::to_string(light_pods.size())    + " light(s), "
                   + std::to_string(mesh_to_upload != nullptr ? 1 : 0)
                   + " mesh(es)");

        if (!save_image_or_error(r.image, run.path, run.label,
                                 width, height)) {
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
#endif
}

// `--render-gradient` dispatch. Width/height come from Config; output
// path defaults to "output/gpu_gradient.ppm" when --output is unset.
int run_render_gradient(const rr::core::Config& cfg) {
    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/gpu_gradient.ppm")
        : cfg.output_path;

#ifndef RR_HAS_CUDA
    (void)cfg;
    rr::core::Logger::error("--render-gradient requires CUDA. Rebuild with "
                            "-DRR_ENABLE_CUDA=ON on a host with the CUDA "
                            "Toolkit and a CUDA-capable GPU.");
    return 1;
#else
    auto r = rr::cuda::CudaRenderer::render_gradient(cfg.width, cfg.height);
    if (!r.ok) {
        rr::core::Logger::error("gradient render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-gradient", cfg.width, cfg.height, r.gpu_time_ms);
    return save_image_or_error(r.image, out_path, "GPU gradient",
                               cfg.width, cfg.height) ? 0 : 1;
#endif
}

// `--render-rays` dispatch. Sets up a sensible default camera (origin,
// looking down -Z, aspect derived from the framebuffer size, 45 deg
// vfov), runs the GPU camera-ray-direction visualisation, and writes
// the PPM. The CPU only constructs the camera POD and snapshots it
// via Camera::to_gpu(); every per-pixel ray-gen step happens inside
// the kernel.
int run_render_camera_rays(const rr::core::Config& cfg) {
    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/gpu_camera_rays.ppm")
        : cfg.output_path;

#ifndef RR_HAS_CUDA
    (void)cfg;
    rr::core::Logger::error("--render-rays requires CUDA. Rebuild with "
                            "-DRR_ENABLE_CUDA=ON on a host with the CUDA "
                            "Toolkit and a CUDA-capable GPU.");
    return 1;
#else
    rr::camera::Camera cam;
    cam.set_aspect(static_cast<float>(cfg.width)
                 / static_cast<float>(cfg.height));

    auto r = rr::cuda::CudaRenderer::render_camera_rays(cam, cfg.width, cfg.height);
    if (!r.ok) {
        rr::core::Logger::error("camera-ray render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-rays", cfg.width, cfg.height, r.gpu_time_ms);
    return save_image_or_error(r.image, out_path, "GPU camera rays",
                               cfg.width, cfg.height) ? 0 : 1;
#endif
}

// `--render-sphere` dispatch. Sets up a default camera + a single
// sphere centred 3 units in front of the camera with radius 1, runs
// the GPU intersection kernel, and writes the PPM. The CPU only
// constructs the camera + sphere PODs as launch arguments; every
// per-pixel ray-gen + intersection + shading step runs on the GPU.
int run_render_sphere(const rr::core::Config& cfg) {
    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/gpu_sphere.ppm")
        : cfg.output_path;

#ifndef RR_HAS_CUDA
    (void)cfg;
    rr::core::Logger::error("--render-sphere requires CUDA. Rebuild with "
                            "-DRR_ENABLE_CUDA=ON on a host with the CUDA "
                            "Toolkit and a CUDA-capable GPU.");
    return 1;
#else
    rr::camera::Camera cam;
    cam.set_aspect(static_cast<float>(cfg.width)
                 / static_cast<float>(cfg.height));

    // Centre the sphere along the camera's default forward direction
    // (-Z), 3 units away, radius 1. Aggregate-init form keeps this
    // legible at a glance.
    const rr::geometry::Sphere sphere{
        rr::math::Vec3{0.0f, 0.0f, -3.0f},
        1.0f,
        /*material_index=*/-1
    };

    auto r = rr::cuda::CudaRenderer::render_sphere(cam, sphere,
                                                   cfg.width, cfg.height);
    if (!r.ok) {
        rr::core::Logger::error("sphere render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-sphere", cfg.width, cfg.height, r.gpu_time_ms);
    return save_image_or_error(r.image, out_path, "GPU sphere",
                               cfg.width, cfg.height) ? 0 : 1;
#endif
}

#ifdef RR_HAS_CUDA
// Build a single front-facing equilateral triangle in front of the
// default camera. Vertex winding is counter-clockwise from the
// camera's point of view so `intersect_triangle` returns the
// front-face normal (pointing toward +Z). One triangle is enough
// to demonstrate the triangle closest-hit path on its own.
rr::geometry::Mesh build_demo_triangle_mesh() {
    rr::geometry::Mesh mesh;
    mesh.vertices.reserve(3);
    mesh.vertices.push_back({rr::math::Vec3{ 0.0f,    1.0f, -3.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.5f, 1.0f}});
    mesh.vertices.push_back({rr::math::Vec3{-0.866f, -0.5f, -3.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.0f, 0.0f}});
    mesh.vertices.push_back({rr::math::Vec3{ 0.866f, -0.5f, -3.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{1.0f, 0.0f}});
    mesh.triangles.push_back({0, 1, 2});
    mesh.material_id = -1;  // simple normal-as-color shading; no material yet
    return mesh;
}

// Build a quad behind the multi-sphere scene so the closest-hit
// logic visibly competes between sphere and triangle primitives.
// Two CCW triangles at z = -6, large enough to cover the background.
rr::geometry::Mesh build_demo_quad_mesh() {
    rr::geometry::Mesh mesh;
    mesh.vertices.reserve(4);
    mesh.vertices.push_back({rr::math::Vec3{-3.0f, -3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.0f, 0.0f}});
    mesh.vertices.push_back({rr::math::Vec3{ 3.0f, -3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{1.0f, 0.0f}});
    mesh.vertices.push_back({rr::math::Vec3{ 3.0f,  3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{1.0f, 1.0f}});
    mesh.vertices.push_back({rr::math::Vec3{-3.0f,  3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.0f, 1.0f}});
    mesh.triangles.push_back({0, 1, 2});
    mesh.triangles.push_back({0, 2, 3});
    mesh.material_id = -1;
    return mesh;
}

// Build the built-in multi-sphere demo scene used by `--render-scene`.
// Three spheres in a row at z = -4, slight stagger in y so the
// closest-hit logic has something non-trivial to do. Camera is the
// default (origin, -Z forward). β = 0 (no relativistic perception
// effects) so the result isolates the GpuScene upload + closest-hit
// loop from the relativity pipeline.
rr::scene::Scene build_demo_scene(int width, int height) {
    rr::scene::Scene scene;
    scene.render_settings.width  = width;
    scene.render_settings.height = height;

    scene.camera.set_aspect(static_cast<float>(width)
                          / static_cast<float>(height));

    // Three foreground spheres + one larger background sphere so the
    // closest-hit loop visibly resolves overlap.
    const auto add = [&](float cx, float cy, float cz, float r,
                         const char* name) {
        rr::scene::SceneSphere s;
        s.object.name = name;
        s.geometry    = rr::geometry::Sphere{
            rr::math::Vec3{cx, cy, cz}, r, /*material_index=*/-1};
        scene.spheres.push_back(s);
    };
    add(-1.5f,  0.2f, -4.0f, 0.7f, "left");
    add( 0.0f, -0.1f, -3.5f, 0.8f, "centre");
    add( 1.5f,  0.2f, -4.0f, 0.7f, "right");
    add( 0.0f, -1.4f, -5.0f, 1.0f, "ground-bulb");

    return scene;
}
#endif  // RR_HAS_CUDA

// `--render-relativistic` dispatch. Runs the relativistic single-sphere
// pipeline at four observer speeds (beta = 0.00, 0.25, 0.75, 0.95) and
// writes four named PPMs into output/. The observer moves along the
// camera's default forward direction (-Z) so positive beta ->
// approaching the sphere -> blueshift in front + searchlight
// brightening + rays aberrated forward. `--output` is ignored; the
// four output paths are fixed.
int run_render_relativistic(const rr::core::Config& cfg) {
#ifndef RR_HAS_CUDA
    (void)cfg;
    rr::core::Logger::error("--render-relativistic requires CUDA. Rebuild "
                            "with -DRR_ENABLE_CUDA=ON on a host with the "
                            "CUDA Toolkit and a CUDA-capable GPU.");
    return 1;
#else
    rr::camera::Camera cam;
    cam.set_aspect(static_cast<float>(cfg.width)
                 / static_cast<float>(cfg.height));

    const rr::geometry::Sphere sphere{
        rr::math::Vec3{0.0f, 0.0f, -3.0f},
        1.0f,
        /*material_index=*/-1
    };

    rr::relativity::RelativityParams params;  // all effects on at strength 1

    struct BetaRun {
        float       beta;
        const char* path;
    };
    constexpr BetaRun kRuns[] = {
        {0.00f, "output/sphere_beta_000.ppm"},
        {0.25f, "output/sphere_beta_025.ppm"},
        {0.75f, "output/sphere_beta_075.ppm"},
        {0.95f, "output/sphere_beta_095.ppm"},
    };

    int failures = 0;
    for (const auto& run : kRuns) {
        // Observer moves along the camera's forward (-Z) direction at
        // |beta| of `run.beta`. Approaching the sphere produces the
        // canonical blueshift + forward-aberration + beaming response
        // when beta > 0.
        rr::relativity::Observer observer;
        observer.velocity = rr::math::Vec3{0.0f, 0.0f, -run.beta};

        auto r = rr::cuda::CudaRenderer::render_relativistic_sphere(
            cam, observer, params, sphere, cfg.width, cfg.height);
        if (!r.ok) {
            rr::core::Logger::error(
                std::string("relativistic render failed at beta=")
                + std::to_string(run.beta) + ": " + r.message);
            ++failures;
            continue;
        }

        const std::string label = std::string("GPU relativistic sphere "
                                              "(beta=") +
                                  std::to_string(run.beta) + ")";
        log_gpu_timing(label.c_str(), cfg.width, cfg.height, r.gpu_time_ms);
        if (!save_image_or_error(r.image, run.path, label,
                                 cfg.width, cfg.height)) {
            ++failures;
        }
    }

    return failures == 0 ? 0 : 1;
#endif
}

// `--render-demo` dispatch (Stage 19E.2; master order #14).
//
// Smallest meaningful relativistic-render demo. The scene is the
// minimum the prompt asks for:
//   - one sphere (centred at z = -3.0; radius 1.0; "neutral" diffuse)
//   - one diffuse material (warm grey baseColor)
//   - one environment light (cool blue ambient; produces visible
//     base shading without needing a directional / point light)
//   - one camera (pinhole; default forward = -Z; aspect set from
//     cfg.width / cfg.height)
//   - one observer; velocity = (0, 0, -|beta|) so positive beta
//     means "approaching the sphere", and the relativistic effects
//     show up forward of the camera (the visible side of the
//     sphere in this scene).
//
// Beta source:
//   - cfg.beta == -1.0f (sentinel, default): use 0.7f.
//   - cfg.beta == 0.0f: classical render (every relativity formula
//     degenerates to identity per `tests/relativity_tests.cpp`
//     test_identity_at_zero_beta), so the output looks like a
//     non-relativistic render of the same scene.
//   - any other value: clamped to <= 0.999999 by clampBeta.
//
// Output:
//   - output/demo_beauty.ppm    — Beauty AOV (RGB)
//   - output/demo_doppler.ppm   — Doppler-factor AOV
//                                  (scalar; replicated to RGB by
//                                  `save_aov_to_ppm`)
//
// All per-pixel work runs on the GPU through the existing
// `CudaRenderer::render_scene_with_aovs` path. The CPU's only
// contribution is constructing the Scene, uploading via
// GpuScene, allocating AOV buffers, and saving the two PPMs. On
// a build without CUDA (`RR_HAS_CUDA` undefined) the action
// returns the same documented "requires CUDA" error every other
// GPU-bound action does.
int run_render_demo(const rr::core::Config& cfg) {
#ifndef RR_HAS_CUDA
    (void)cfg;
    rr::core::Logger::error("--render-demo requires CUDA. Rebuild "
                            "with -DRR_ENABLE_CUDA=ON on a host with "
                            "the CUDA Toolkit and a CUDA-capable GPU.");
    return 1;
#else
    // Pick the user-supplied beta (or the documented default).
    const float beta_user = (cfg.beta < 0.0f) ? 0.7f : cfg.beta;
    const float beta_mag  = rr::relativity::clampBeta(
        beta_user, /*max_beta=*/0.999999f);

    rr::scene::Scene scene;
    scene.render_settings.width  = cfg.width;
    scene.render_settings.height = cfg.height;
    scene.camera.set_aspect(static_cast<float>(cfg.width)
                          / static_cast<float>(cfg.height));

    // One material (warm grey diffuse).
    auto neutral = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.75f, 0.70f, 0.65f});
    scene.materials.push_back({0, "neutral", neutral.params()});

    // One sphere on the camera's forward axis.
    {
        rr::scene::SceneSphere s;
        s.object.name = "demo-sphere";
        s.geometry    = rr::geometry::Sphere{
            rr::math::Vec3{0.0f, 0.0f, -3.0f}, 1.0f, /*material_index=*/0};
        scene.spheres.push_back(s);
    }

    // One environment light (cool blue ambient). Stage 9B's
    // direct-lighting kernel reads environment lights as flat
    // ambient; no shadow rays / NEE needed.
    scene.lights.push_back({{}, rr::lighting::make_environment_light(
        rr::math::Vec3{0.55f, 0.65f, 0.85f},
        /*intensity=*/0.75f)});

    // Observer along the camera's forward axis (-Z). beta_mag = 0
    // exactly matches a classical render (Identity at |beta|=0,
    // verified by tests/relativity_tests.cpp #1). Positive beta
    // means "approaching": forward-cone blueshift + searchlight
    // brightening; backward-cone redshift + dimming on the
    // edges. Default RelativityParams keeps every effect on at
    // strength 1.
    scene.observer.velocity = rr::math::Vec3{0.0f, 0.0f, -beta_mag};

    // Upload via GpuScene (camera / relativity / spheres /
    // materials / lights). No mesh, no texture.
    std::vector<rr::geometry::Sphere> sphere_pods;
    sphere_pods.reserve(scene.spheres.size());
    for (const auto& s : scene.spheres) {
        if (s.object.visible) sphere_pods.push_back(s.geometry);
    }
    std::vector<rr::material::MaterialParams> material_pods;
    material_pods.reserve(scene.materials.size());
    for (const auto& m : scene.materials) {
        material_pods.push_back(m.params);
    }
    std::vector<rr::lighting::Light> light_pods;
    light_pods.reserve(scene.lights.size());
    for (const auto& L : scene.lights) light_pods.push_back(L.data);

    rr::gpu::GpuScene gpu_scene;
    if (!gpu_scene.upload_camera(scene.camera))
        { rr::core::Logger::error("demo: upload_camera failed"); return 1; }
    if (!gpu_scene.upload_relativity(scene.observer, scene.relativity))
        { rr::core::Logger::error("demo: upload_relativity failed"); return 1; }
    if (!gpu_scene.upload_spheres(sphere_pods.data(), sphere_pods.size()))
        { rr::core::Logger::error("demo: upload_spheres failed"); return 1; }
    if (!gpu_scene.upload_materials(material_pods.data(),
                                    material_pods.size()))
        { rr::core::Logger::error("demo: upload_materials failed"); return 1; }
    if (!gpu_scene.upload_lights(light_pods.data(), light_pods.size()))
        { rr::core::Logger::error("demo: upload_lights failed"); return 1; }

    // Allocate the standard six AOV buffers. We only save Beauty
    // [0] and DopplerFactor [4] but the kernel needs the full
    // target struct populated, and the cost of allocating four
    // unused buffers is negligible (a 1280x720 framebuffer is
    // 3.5 MB / channel).
    auto aov_set = rr::renderer::make_default_aov_set();
    for (auto& b : aov_set) {
        if (!b.resize(cfg.width, cfg.height)) {
            rr::core::Logger::error("demo: AOV resize failed for "
                + std::string(rr::renderer::aov_type_name(b.type())));
            return 1;
        }
    }

    rr::cuda::CudaRenderer::AOVTargets targets;
    targets.beauty             = aov_set[0].device_ptr();
    targets.normal             = aov_set[1].device_ptr();
    targets.depth              = aov_set[2].device_ptr();
    targets.albedo             = aov_set[3].device_ptr();
    targets.doppler_factor     = aov_set[4].device_ptr();
    targets.searchlight_factor = aov_set[5].device_ptr();

    auto r = rr::cuda::CudaRenderer::render_scene_with_aovs(
        gpu_scene, cfg.width, cfg.height, targets);
    if (!r.ok) {
        rr::core::Logger::error("demo render failed: " + r.message);
        return 1;
    }

    {
        const std::string label = std::string("render-demo (beta=")
                                + std::to_string(beta_mag) + ")";
        log_gpu_timing(label.c_str(), cfg.width, cfg.height, r.gpu_time_ms);
    }

    // Save Beauty + DopplerFactor PPMs to the documented paths.
    // `save_aov_to_ppm` replicates the scalar Doppler factor
    // across RGB so the resulting PPM is viewable, exactly the
    // same handling `--render-aovs` uses.
    bool all_ok = true;
    if (!save_aov_to_ppm(aov_set[0], "output/demo_beauty.ppm",
                         cfg.width, cfg.height, "demo beauty")) {
        all_ok = false;
    }
    if (!save_aov_to_ppm(aov_set[4], "output/demo_doppler.ppm",
                         cfg.width, cfg.height, "demo doppler")) {
        all_ok = false;
    }
    return all_ok ? 0 : 1;
#endif
}

// `--render-scene` dispatch. Builds the built-in multi-sphere demo
// scene, uploads it via GpuScene, runs the GPU closest-hit kernel,
// and writes the PPM. The CPU's only contribution is constructing
// the Scene + upload calls + image saving; ray-gen, intersection,
// and shading all run on the device.
int run_render_scene(const rr::core::Config& cfg) {
    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/gpu_scene_spheres.ppm")
        : cfg.output_path;

#ifndef RR_HAS_CUDA
    (void)cfg;
    rr::core::Logger::error("--render-scene requires CUDA. Rebuild with "
                            "-DRR_ENABLE_CUDA=ON on a host with the CUDA "
                            "Toolkit and a CUDA-capable GPU.");
    return 1;
#else
    const auto scene = build_demo_scene(cfg.width, cfg.height);

    // Pull `rr::geometry::Sphere` PODs out of the scene's
    // `SceneSphere` wrappers, dropping any entries marked invisible.
    std::vector<rr::geometry::Sphere> sphere_pods;
    sphere_pods.reserve(scene.spheres.size());
    for (const auto& s : scene.spheres) {
        if (s.object.visible) sphere_pods.push_back(s.geometry);
    }

    rr::gpu::GpuScene gpu_scene;
    if (!gpu_scene.upload_camera(scene.camera)) {
        rr::core::Logger::error("scene render failed: upload_camera");
        return 1;
    }
    if (!gpu_scene.upload_relativity(scene.observer, scene.relativity)) {
        rr::core::Logger::error("scene render failed: upload_relativity");
        return 1;
    }
    if (!gpu_scene.upload_spheres(sphere_pods.data(), sphere_pods.size())) {
        rr::core::Logger::error("scene render failed: upload_spheres "
                                "(no GPU backend or device allocation "
                                "failed)");
        return 1;
    }

    auto r = rr::cuda::CudaRenderer::render_scene(gpu_scene,
                                                  cfg.width, cfg.height);
    if (!r.ok) {
        rr::core::Logger::error("scene render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-scene", cfg.width, cfg.height, r.gpu_time_ms);

    rr::core::Logger::info("scene: " + std::to_string(sphere_pods.size())
                         + " sphere(s) uploaded, "
                         + std::to_string(cfg.width) + "x"
                         + std::to_string(cfg.height) + " framebuffer");

    return save_image_or_error(r.image, out_path, "GPU scene",
                               cfg.width, cfg.height) ? 0 : 1;
#endif
}

// `--render-triangle` dispatch. Builds a Scene with no spheres + a
// single triangle mesh, uploads via GpuScene, runs the GPU
// closest-hit kernel (which falls through the empty sphere loop),
// and writes the PPM. Demonstrates the triangle-only path of
// k_render_scene's combined loop.
int run_render_triangle(const rr::core::Config& cfg) {
    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/gpu_triangle.ppm")
        : cfg.output_path;

#ifndef RR_HAS_CUDA
    (void)cfg;
    rr::core::Logger::error("--render-triangle requires CUDA. Rebuild with "
                            "-DRR_ENABLE_CUDA=ON on a host with the CUDA "
                            "Toolkit and a CUDA-capable GPU.");
    return 1;
#else
    rr::scene::Scene scene;
    scene.camera.set_aspect(static_cast<float>(cfg.width)
                          / static_cast<float>(cfg.height));
    // No spheres - the kernel's sphere loop runs zero iterations.

    const auto mesh = build_demo_triangle_mesh();

    rr::gpu::GpuScene gpu_scene;
    if (!gpu_scene.upload_camera(scene.camera)) {
        rr::core::Logger::error("triangle render failed: upload_camera");
        return 1;
    }
    if (!gpu_scene.upload_relativity(scene.observer, scene.relativity)) {
        rr::core::Logger::error("triangle render failed: upload_relativity");
        return 1;
    }
    if (!gpu_scene.upload_mesh(mesh)) {
        rr::core::Logger::error("triangle render failed: upload_mesh "
                                "(no GPU backend or device allocation "
                                "failed)");
        return 1;
    }

    auto r = rr::cuda::CudaRenderer::render_scene(gpu_scene,
                                                  cfg.width, cfg.height);
    if (!r.ok) {
        rr::core::Logger::error("triangle render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-triangle", cfg.width, cfg.height, r.gpu_time_ms);

    rr::core::Logger::info("triangle: 0 spheres + "
                         + std::to_string(mesh.triangle_count())
                         + " tri(s) uploaded, "
                         + std::to_string(cfg.width) + "x"
                         + std::to_string(cfg.height) + " framebuffer");

    return save_image_or_error(r.image, out_path, "GPU triangle",
                               cfg.width, cfg.height) ? 0 : 1;
#endif
}

// `--render-mesh-scene` dispatch. Builds the multi-sphere demo scene
// + a triangle quad behind the spheres, uploads both via GpuScene,
// runs the GPU closest-hit kernel (sphere + triangle compete on
// t_max), and writes the PPM.
int run_render_mesh_scene(const rr::core::Config& cfg) {
    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/gpu_mesh_scene.ppm")
        : cfg.output_path;

#ifndef RR_HAS_CUDA
    (void)cfg;
    rr::core::Logger::error("--render-mesh-scene requires CUDA. Rebuild "
                            "with -DRR_ENABLE_CUDA=ON on a host with the "
                            "CUDA Toolkit and a CUDA-capable GPU.");
    return 1;
#else
    const auto scene = build_demo_scene(cfg.width, cfg.height);

    std::vector<rr::geometry::Sphere> sphere_pods;
    sphere_pods.reserve(scene.spheres.size());
    for (const auto& s : scene.spheres) {
        if (s.object.visible) sphere_pods.push_back(s.geometry);
    }

    const auto quad = build_demo_quad_mesh();

    rr::gpu::GpuScene gpu_scene;
    if (!gpu_scene.upload_camera(scene.camera)) {
        rr::core::Logger::error("mesh-scene render failed: upload_camera");
        return 1;
    }
    if (!gpu_scene.upload_relativity(scene.observer, scene.relativity)) {
        rr::core::Logger::error("mesh-scene render failed: upload_relativity");
        return 1;
    }
    if (!gpu_scene.upload_spheres(sphere_pods.data(), sphere_pods.size())) {
        rr::core::Logger::error("mesh-scene render failed: upload_spheres");
        return 1;
    }
    if (!gpu_scene.upload_mesh(quad)) {
        rr::core::Logger::error("mesh-scene render failed: upload_mesh");
        return 1;
    }

    auto r = rr::cuda::CudaRenderer::render_scene(gpu_scene,
                                                  cfg.width, cfg.height);
    if (!r.ok) {
        rr::core::Logger::error("mesh-scene render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-mesh-scene", cfg.width, cfg.height, r.gpu_time_ms);

    rr::core::Logger::info("mesh-scene: "
                         + std::to_string(sphere_pods.size())
                         + " sphere(s) + "
                         + std::to_string(quad.triangle_count())
                         + " tri(s) uploaded, "
                         + std::to_string(cfg.width) + "x"
                         + std::to_string(cfg.height) + " framebuffer");

    return save_image_or_error(r.image, out_path, "GPU mesh scene",
                               cfg.width, cfg.height) ? 0 : 1;
#endif
}

// `--render-material-scene` dispatch. Builds the multi-sphere demo
// + a triangle-quad backdrop, assigns per-object materials, uploads
// everything (camera, relativity, spheres, mesh, materials), and
// renders. The kernel reads `materials[Hit::material_index]` for
// the base colour at hit time. Demonstrates the full Stage 8B
// pipeline: material upload + kernel lookup + emission contribution
// + the Doppler / searchlight pipeline still applied on top.
int run_render_material_scene(const rr::core::Config& cfg) {
    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/gpu_material_scene.ppm")
        : cfg.output_path;

#ifndef RR_HAS_CUDA
    (void)cfg;
    rr::core::Logger::error("--render-material-scene requires CUDA. Rebuild "
                            "with -DRR_ENABLE_CUDA=ON on a host with the "
                            "CUDA Toolkit and a CUDA-capable GPU.");
    return 1;
#else
    rr::scene::Scene scene;
    scene.render_settings.width  = cfg.width;
    scene.render_settings.height = cfg.height;
    scene.camera.set_aspect(static_cast<float>(cfg.width)
                          / static_cast<float>(cfg.height));

    // Five-material palette. Indices match SceneSphere /
    // SceneMesh material_index assignments below.
    auto red       = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.85f, 0.20f, 0.20f});
    auto green     = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.20f, 0.80f, 0.30f});
    auto blue      = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.20f, 0.30f, 0.90f});
    auto emissive  = rr::material::Material::make_emissive(
        rr::math::Vec3{1.0f, 0.85f, 0.35f}, /*strength=*/2.0f);
    auto neutral   = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.65f, 0.65f, 0.65f});

    scene.materials.push_back({0, "red",     red.params()});
    scene.materials.push_back({1, "green",   green.params()});
    scene.materials.push_back({2, "blue",    blue.params()});
    scene.materials.push_back({3, "emissive",emissive.params()});
    scene.materials.push_back({4, "neutral", neutral.params()});

    // Spheres - same layout as build_demo_scene's, with material
    // indices pointing into the palette above.
    const auto add_sphere = [&](float cx, float cy, float cz, float r,
                                int mat, const char* name) {
        rr::scene::SceneSphere s;
        s.object.name = name;
        s.geometry    = rr::geometry::Sphere{
            rr::math::Vec3{cx, cy, cz}, r, mat};
        scene.spheres.push_back(s);
    };
    add_sphere(-1.5f,  0.2f, -4.0f, 0.7f, 0, "left");        // red
    add_sphere( 0.0f, -0.1f, -3.5f, 0.8f, 1, "centre");      // green
    add_sphere( 1.5f,  0.2f, -4.0f, 0.7f, 2, "right");       // blue
    add_sphere( 0.0f, -1.4f, -5.0f, 1.0f, 3, "ground-bulb"); // emissive

    // Background quad with the neutral material.
    rr::geometry::Mesh quad;
    quad.vertices.push_back({rr::math::Vec3{-3.0f, -3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.0f, 0.0f}});
    quad.vertices.push_back({rr::math::Vec3{ 3.0f, -3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{1.0f, 0.0f}});
    quad.vertices.push_back({rr::math::Vec3{ 3.0f,  3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{1.0f, 1.0f}});
    quad.vertices.push_back({rr::math::Vec3{-3.0f,  3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.0f, 1.0f}});
    quad.triangles.push_back({0, 1, 2});
    quad.triangles.push_back({0, 2, 3});
    quad.material_id = 4;  // neutral

    // Pull Sphere PODs out of the SceneSphere wrappers (filtering
    // invisible) for the GPU upload.
    std::vector<rr::geometry::Sphere> sphere_pods;
    sphere_pods.reserve(scene.spheres.size());
    for (const auto& s : scene.spheres) {
        if (s.object.visible) sphere_pods.push_back(s.geometry);
    }

    // Materials: flat MaterialParams array indexed by SceneMaterial
    // position (the same convention the kernel uses).
    std::vector<rr::material::MaterialParams> material_pods;
    material_pods.reserve(scene.materials.size());
    for (const auto& m : scene.materials) {
        material_pods.push_back(m.params);
    }

    rr::gpu::GpuScene gpu_scene;
    if (!gpu_scene.upload_camera(scene.camera)) {
        rr::core::Logger::error("material-scene render failed: upload_camera");
        return 1;
    }
    if (!gpu_scene.upload_relativity(scene.observer, scene.relativity)) {
        rr::core::Logger::error("material-scene render failed: upload_relativity");
        return 1;
    }
    if (!gpu_scene.upload_spheres(sphere_pods.data(), sphere_pods.size())) {
        rr::core::Logger::error("material-scene render failed: upload_spheres");
        return 1;
    }
    if (!gpu_scene.upload_mesh(quad)) {
        rr::core::Logger::error("material-scene render failed: upload_mesh");
        return 1;
    }
    if (!gpu_scene.upload_materials(material_pods.data(),
                                    material_pods.size())) {
        rr::core::Logger::error("material-scene render failed: upload_materials");
        return 1;
    }

    auto r = rr::cuda::CudaRenderer::render_scene(gpu_scene,
                                                  cfg.width, cfg.height);
    if (!r.ok) {
        rr::core::Logger::error("material-scene render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-material-scene", cfg.width, cfg.height, r.gpu_time_ms);

    rr::core::Logger::info("material-scene: "
                         + std::to_string(sphere_pods.size())
                         + " sphere(s) + "
                         + std::to_string(quad.triangle_count())
                         + " tri(s) + "
                         + std::to_string(material_pods.size())
                         + " material(s) uploaded, "
                         + std::to_string(cfg.width) + "x"
                         + std::to_string(cfg.height) + " framebuffer");

    return save_image_or_error(r.image, out_path, "GPU material scene",
                               cfg.width, cfg.height) ? 0 : 1;
#endif
}

// `--render-textured-material` dispatch (Stage 13B.3 / master order
// #18). Builds the same multi-sphere + quad layout as
// `--render-material-scene`, but the quad's neutral material is
// replaced with one whose `useBaseColorTexture` flag is set and
// whose `baseColorTextureId` points at an uploaded 2x2 four-colour
// reference texture (red / green / blue / yellow). The kernel
// interpolates the per-vertex UVs at the triangle hit, samples the
// texture, and shows the four quadrants stretched across the quad.
// Spheres keep their flat baseColors, demonstrating the
// per-material gating: textures opt in, untextured materials
// short-circuit to the existing path.
int run_render_textured_material(const rr::core::Config& cfg) {
    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/gpu_textured_material.ppm")
        : cfg.output_path;

#ifndef RR_HAS_CUDA
    (void)cfg;
    rr::core::Logger::error("--render-textured-material requires CUDA. "
                            "Rebuild with -DRR_ENABLE_CUDA=ON on a host "
                            "with the CUDA Toolkit and a CUDA-capable "
                            "GPU.");
    return 1;
#else
    rr::scene::Scene scene;
    scene.render_settings.width  = cfg.width;
    scene.render_settings.height = cfg.height;
    scene.camera.set_aspect(static_cast<float>(cfg.width)
                          / static_cast<float>(cfg.height));

    // Five-material palette (matches --render-material-scene), but
    // the "neutral" slot is upgraded to a textured material that
    // points at texture id 0 (uploaded below). The other four
    // materials keep their flat baseColors.
    auto red       = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.85f, 0.20f, 0.20f});
    auto green     = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.20f, 0.80f, 0.30f});
    auto blue      = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.20f, 0.30f, 0.90f});
    auto emissive  = rr::material::Material::make_emissive(
        rr::math::Vec3{1.0f, 0.85f, 0.35f}, /*strength=*/2.0f);

    rr::material::MaterialParams textured_params;
    // baseColor still set to a recognisable fallback so debug
    // output (e.g. with the texture intentionally unbound) does
    // not collapse to white.
    textured_params.baseColor           = rr::math::Vec3{0.65f, 0.65f, 0.65f};
    textured_params.useBaseColorTexture = true;
    textured_params.baseColorTextureId  = 0;

    scene.materials.push_back({0, "red",      red.params()});
    scene.materials.push_back({1, "green",    green.params()});
    scene.materials.push_back({2, "blue",     blue.params()});
    scene.materials.push_back({3, "emissive", emissive.params()});
    scene.materials.push_back({4, "textured", textured_params});

    const auto add_sphere = [&](float cx, float cy, float cz, float r,
                                int mat, const char* name) {
        rr::scene::SceneSphere s;
        s.object.name = name;
        s.geometry    = rr::geometry::Sphere{
            rr::math::Vec3{cx, cy, cz}, r, mat};
        scene.spheres.push_back(s);
    };
    add_sphere(-1.5f,  0.2f, -4.0f, 0.7f, 0, "left");
    add_sphere( 0.0f, -0.1f, -3.5f, 0.8f, 1, "centre");
    add_sphere( 1.5f,  0.2f, -4.0f, 0.7f, 2, "right");
    add_sphere( 0.0f, -1.4f, -5.0f, 1.0f, 3, "ground-bulb");

    rr::geometry::Mesh quad;
    quad.vertices.push_back({rr::math::Vec3{-3.0f, -3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.0f, 1.0f}});
    quad.vertices.push_back({rr::math::Vec3{ 3.0f, -3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{1.0f, 1.0f}});
    quad.vertices.push_back({rr::math::Vec3{ 3.0f,  3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{1.0f, 0.0f}});
    quad.vertices.push_back({rr::math::Vec3{-3.0f,  3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.0f, 0.0f}});
    quad.triangles.push_back({0, 1, 2});
    quad.triangles.push_back({0, 2, 3});
    quad.material_id = 4;  // textured

    // Build the 2x2 four-colour reference texture (same pattern as
    // --render-texture-sample-test, sharing the visual contract:
    // top-left red, top-right green, bottom-left blue, bottom-right
    // yellow with origin uv = (0, 0) at the top-left texel).
    rr::texture::ImageTexture tex0(2, 2,
                                   rr::texture::ImageTextureFormat::Rgba8,
                                   "textured_material_pattern");
    {
        const unsigned char rgba_bytes[16] = {
            255,   0,   0, 255,    0, 255,   0, 255,
              0,   0, 255, 255,  255, 255,   0, 255,
        };
        tex0.pixels().resize(sizeof rgba_bytes);
        std::memcpy(tex0.pixels().data(), rgba_bytes, sizeof rgba_bytes);
    }
    const std::vector<rr::texture::ImageTexture> textures{ std::move(tex0) };

    std::vector<rr::geometry::Sphere> sphere_pods;
    sphere_pods.reserve(scene.spheres.size());
    for (const auto& s : scene.spheres) {
        if (s.object.visible) sphere_pods.push_back(s.geometry);
    }

    std::vector<rr::material::MaterialParams> material_pods;
    material_pods.reserve(scene.materials.size());
    for (const auto& m : scene.materials) {
        material_pods.push_back(m.params);
    }

    rr::gpu::GpuScene gpu_scene;
    if (!gpu_scene.upload_camera(scene.camera)) {
        rr::core::Logger::error("textured-material render failed: upload_camera");
        return 1;
    }
    if (!gpu_scene.upload_relativity(scene.observer, scene.relativity)) {
        rr::core::Logger::error("textured-material render failed: upload_relativity");
        return 1;
    }
    if (!gpu_scene.upload_spheres(sphere_pods.data(), sphere_pods.size())) {
        rr::core::Logger::error("textured-material render failed: upload_spheres");
        return 1;
    }
    if (!gpu_scene.upload_mesh(quad)) {
        rr::core::Logger::error("textured-material render failed: upload_mesh");
        return 1;
    }
    if (!gpu_scene.upload_materials(material_pods.data(),
                                    material_pods.size())) {
        rr::core::Logger::error("textured-material render failed: upload_materials");
        return 1;
    }
    if (!gpu_scene.upload_textures(textures.data(), textures.size())) {
        rr::core::Logger::error("textured-material render failed: upload_textures");
        return 1;
    }

    auto r = rr::cuda::CudaRenderer::render_scene(gpu_scene,
                                                  cfg.width, cfg.height);
    if (!r.ok) {
        rr::core::Logger::error("textured-material render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-textured-material",
                   cfg.width, cfg.height, r.gpu_time_ms);

    rr::core::Logger::info("textured-material: "
                         + std::to_string(sphere_pods.size())
                         + " sphere(s) + "
                         + std::to_string(quad.triangle_count())
                         + " tri(s) + "
                         + std::to_string(material_pods.size())
                         + " material(s) + "
                         + std::to_string(textures.size())
                         + " texture(s) uploaded, "
                         + std::to_string(cfg.width) + "x"
                         + std::to_string(cfg.height) + " framebuffer");

    return save_image_or_error(r.image, out_path, "GPU textured material",
                               cfg.width, cfg.height) ? 0 : 1;
#endif
}

// `--render-direct-lighting` dispatch. Builds the multi-sphere +
// quad scene with materials AND lights, uploads everything, and
// runs the GPU direct-lighting path in `k_render_scene`. The
// kernel evaluates point + directional contributions
// unconditionally (no shadows) and treats environment lights as
// ambient. Emission, Doppler colour, and searchlight beaming
// apply on top. Demonstrates the full Stage 9B pipeline.
int run_render_direct_lighting(const rr::core::Config& cfg) {
    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/gpu_direct_lighting.ppm")
        : cfg.output_path;

#ifndef RR_HAS_CUDA
    (void)cfg;
    rr::core::Logger::error("--render-direct-lighting requires CUDA. "
                            "Rebuild with -DRR_ENABLE_CUDA=ON on a host "
                            "with the CUDA Toolkit and a CUDA-capable "
                            "GPU.");
    return 1;
#else
    rr::scene::Scene scene;
    scene.render_settings.width  = cfg.width;
    scene.render_settings.height = cfg.height;
    scene.camera.set_aspect(static_cast<float>(cfg.width)
                          / static_cast<float>(cfg.height));

    // Materials. Same five-material palette as --render-material-scene.
    auto red       = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.85f, 0.20f, 0.20f});
    auto green     = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.20f, 0.80f, 0.30f});
    auto blue      = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.20f, 0.30f, 0.90f});
    auto emissive  = rr::material::Material::make_emissive(
        rr::math::Vec3{1.0f, 0.85f, 0.35f}, /*strength=*/2.0f);
    auto neutral   = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.65f, 0.65f, 0.65f});
    scene.materials.push_back({0, "red",      red.params()});
    scene.materials.push_back({1, "green",    green.params()});
    scene.materials.push_back({2, "blue",     blue.params()});
    scene.materials.push_back({3, "emissive", emissive.params()});
    scene.materials.push_back({4, "neutral",  neutral.params()});

    // Spheres + quad: same layout as --render-material-scene.
    const auto add_sphere = [&](float cx, float cy, float cz, float r,
                                int mat, const char* name) {
        rr::scene::SceneSphere s;
        s.object.name = name;
        s.geometry    = rr::geometry::Sphere{
            rr::math::Vec3{cx, cy, cz}, r, mat};
        scene.spheres.push_back(s);
    };
    add_sphere(-1.5f,  0.2f, -4.0f, 0.7f, 0, "left");
    add_sphere( 0.0f, -0.1f, -3.5f, 0.8f, 1, "centre");
    add_sphere( 1.5f,  0.2f, -4.0f, 0.7f, 2, "right");
    add_sphere( 0.0f, -1.4f, -5.0f, 1.0f, 3, "ground-bulb");

    rr::geometry::Mesh quad;
    quad.vertices.push_back({rr::math::Vec3{-3.0f, -3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.0f, 0.0f}});
    quad.vertices.push_back({rr::math::Vec3{ 3.0f, -3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{1.0f, 0.0f}});
    quad.vertices.push_back({rr::math::Vec3{ 3.0f,  3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{1.0f, 1.0f}});
    quad.vertices.push_back({rr::math::Vec3{-3.0f,  3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.0f, 1.0f}});
    quad.triangles.push_back({0, 1, 2});
    quad.triangles.push_back({0, 2, 3});
    quad.material_id = 4;

    // Lights. Three samples to demonstrate every code path:
    //   1. Directional - "sun" coming from upper-front-left.
    //   2. Point       - warm fill light to the upper right
    //                    (intensity > 1 to compensate for 1/d^2).
    //   3. Environment - cool-blue flat ambient tint.
    scene.lights.push_back({{}, rr::lighting::make_directional_light(
        rr::math::Vec3{-0.4f, -0.7f, -0.6f},
        rr::math::Vec3{1.0f, 0.95f, 0.85f},
        /*intensity=*/0.9f)});
    scene.lights.push_back({{}, rr::lighting::make_point_light(
        rr::math::Vec3{2.0f, 1.5f, -2.5f},
        rr::math::Vec3{1.0f, 0.85f, 0.6f},
        /*intensity=*/30.0f)});
    scene.lights.push_back({{}, rr::lighting::make_environment_light(
        rr::math::Vec3{0.30f, 0.40f, 0.55f},
        /*intensity=*/0.4f)});

    // Pull host PODs out of the scene wrappers for the GPU upload.
    std::vector<rr::geometry::Sphere> sphere_pods;
    sphere_pods.reserve(scene.spheres.size());
    for (const auto& s : scene.spheres) {
        if (s.object.visible) sphere_pods.push_back(s.geometry);
    }
    std::vector<rr::material::MaterialParams> material_pods;
    material_pods.reserve(scene.materials.size());
    for (const auto& m : scene.materials) material_pods.push_back(m.params);
    std::vector<rr::lighting::Light> light_pods;
    light_pods.reserve(scene.lights.size());
    for (const auto& l : scene.lights) {
        if (l.object.visible) light_pods.push_back(l.data);
    }

    rr::gpu::GpuScene gpu_scene;
    if (!gpu_scene.upload_camera(scene.camera)) {
        rr::core::Logger::error("direct-lighting failed: upload_camera");
        return 1;
    }
    if (!gpu_scene.upload_relativity(scene.observer, scene.relativity)) {
        rr::core::Logger::error("direct-lighting failed: upload_relativity");
        return 1;
    }
    if (!gpu_scene.upload_spheres(sphere_pods.data(), sphere_pods.size())) {
        rr::core::Logger::error("direct-lighting failed: upload_spheres");
        return 1;
    }
    if (!gpu_scene.upload_mesh(quad)) {
        rr::core::Logger::error("direct-lighting failed: upload_mesh");
        return 1;
    }
    if (!gpu_scene.upload_materials(material_pods.data(),
                                    material_pods.size())) {
        rr::core::Logger::error("direct-lighting failed: upload_materials");
        return 1;
    }
    if (!gpu_scene.upload_lights(light_pods.data(), light_pods.size())) {
        rr::core::Logger::error("direct-lighting failed: upload_lights");
        return 1;
    }

    auto r = rr::cuda::CudaRenderer::render_scene(gpu_scene,
                                                  cfg.width, cfg.height);
    if (!r.ok) {
        rr::core::Logger::error("direct-lighting render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-direct-lighting",
                   cfg.width, cfg.height, r.gpu_time_ms);

    rr::core::Logger::info("direct-lighting: "
                         + std::to_string(sphere_pods.size())
                         + " sphere(s) + "
                         + std::to_string(quad.triangle_count())
                         + " tri(s) + "
                         + std::to_string(material_pods.size())
                         + " material(s) + "
                         + std::to_string(light_pods.size())
                         + " light(s) uploaded, "
                         + std::to_string(cfg.width) + "x"
                         + std::to_string(cfg.height) + " framebuffer");

    return save_image_or_error(r.image, out_path, "GPU direct lighting",
                               cfg.width, cfg.height) ? 0 : 1;
#endif
}

// `--render-aovs` dispatch (Stage 14A.3 / master order #19).
// Builds the same multi-sphere + textured-quad + materials +
// lights scene `--render-direct-lighting` uses, but layers a
// non-zero observer velocity on top so the relativity AOVs
// (DopplerFactor / SearchlightFactor) show visible variation
// across the framebuffer rather than a flat 1.0. Allocates one
// `GpuAOVBuffer` per declared `AOVType`, plumbs each device
// pointer into `CudaRenderer::AOVTargets`, runs
// `render_scene_with_aovs`, then downloads + saves each pass
// independently. The CPU only owns buffer lifetime + the final
// download / save; every per-pixel value comes from the kernel.
int run_render_aovs(const rr::core::Config& cfg) {
#ifndef RR_HAS_CUDA
    (void)cfg;
    rr::core::Logger::error("--render-aovs requires CUDA. Rebuild with "
                            "-DRR_ENABLE_CUDA=ON on a host with the CUDA "
                            "Toolkit and a CUDA-capable GPU.");
    return 1;
#else
    rr::scene::Scene scene;
    scene.render_settings.width  = cfg.width;
    scene.render_settings.height = cfg.height;
    scene.camera.set_aspect(static_cast<float>(cfg.width)
                          / static_cast<float>(cfg.height));

    // Materials: same five-material palette as
    // --render-material-scene / --render-direct-lighting.
    auto red       = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.85f, 0.20f, 0.20f});
    auto green     = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.20f, 0.80f, 0.30f});
    auto blue      = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.20f, 0.30f, 0.90f});
    auto emissive  = rr::material::Material::make_emissive(
        rr::math::Vec3{1.0f, 0.85f, 0.35f}, /*strength=*/2.0f);
    auto neutral   = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.65f, 0.65f, 0.65f});
    scene.materials.push_back({0, "red",      red.params()});
    scene.materials.push_back({1, "green",    green.params()});
    scene.materials.push_back({2, "blue",     blue.params()});
    scene.materials.push_back({3, "emissive", emissive.params()});
    scene.materials.push_back({4, "neutral",  neutral.params()});

    const auto add_sphere = [&](float cx, float cy, float cz, float r,
                                int mat, const char* name) {
        rr::scene::SceneSphere s;
        s.object.name = name;
        s.geometry    = rr::geometry::Sphere{
            rr::math::Vec3{cx, cy, cz}, r, mat};
        scene.spheres.push_back(s);
    };
    add_sphere(-1.5f,  0.2f, -4.0f, 0.7f, 0, "left");
    add_sphere( 0.0f, -0.1f, -3.5f, 0.8f, 1, "centre");
    add_sphere( 1.5f,  0.2f, -4.0f, 0.7f, 2, "right");
    add_sphere( 0.0f, -1.4f, -5.0f, 1.0f, 3, "ground-bulb");

    rr::geometry::Mesh quad;
    quad.vertices.push_back({rr::math::Vec3{-3.0f, -3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.0f, 0.0f}});
    quad.vertices.push_back({rr::math::Vec3{ 3.0f, -3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{1.0f, 0.0f}});
    quad.vertices.push_back({rr::math::Vec3{ 3.0f,  3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{1.0f, 1.0f}});
    quad.vertices.push_back({rr::math::Vec3{-3.0f,  3.0f, -6.0f},
                             rr::math::Vec3{0.0f, 0.0f, 1.0f},
                             rr::math::Vec2{0.0f, 1.0f}});
    quad.triangles.push_back({0, 1, 2});
    quad.triangles.push_back({0, 2, 3});
    quad.material_id = 4;

    // Three lights: one directional (key), one warm point light
    // near the spheres (fill), one cool environment ambient.
    // Same shape as --render-direct-lighting; the brace-init
    // matches `SceneLight { SceneObject object; Light data; }`.
    scene.lights.push_back({{}, rr::lighting::make_directional_light(
        rr::math::Vec3{-0.4f, -0.7f, -0.6f},
        rr::math::Vec3{1.0f, 0.95f, 0.85f},
        /*intensity=*/0.9f)});
    scene.lights.push_back({{}, rr::lighting::make_point_light(
        rr::math::Vec3{2.0f, 1.5f, -2.5f},
        rr::math::Vec3{1.0f, 0.85f, 0.6f},
        /*intensity=*/30.0f)});
    scene.lights.push_back({{}, rr::lighting::make_environment_light(
        rr::math::Vec3{0.55f, 0.65f, 0.85f},
        /*intensity=*/0.25f)});

    // Stage 14A.3: pick a non-zero observer velocity so the
    // DopplerFactor / SearchlightFactor AOVs show visible
    // variation across the framebuffer. β = 0.5 along -Z (forward
    // motion into the scene) gives a clear forward-cone brightening
    // and a backward-cone dimming under the searchlight pass.
    scene.observer.velocity = rr::math::Vec3{0.0f, 0.0f, -0.5f};
    // Defaults already enable doppler / searchlight in
    // RelativityParams; the AOV writes happen regardless of those
    // flags, so the beauty pass shows the colour-shifted output
    // and the AOV passes show the underlying physical factors.

    std::vector<rr::geometry::Sphere> sphere_pods;
    sphere_pods.reserve(scene.spheres.size());
    for (const auto& s : scene.spheres) {
        if (s.object.visible) sphere_pods.push_back(s.geometry);
    }

    std::vector<rr::material::MaterialParams> material_pods;
    material_pods.reserve(scene.materials.size());
    for (const auto& m : scene.materials) {
        material_pods.push_back(m.params);
    }

    std::vector<rr::lighting::Light> light_pods;
    light_pods.reserve(scene.lights.size());
    for (const auto& L : scene.lights) light_pods.push_back(L.data);

    rr::gpu::GpuScene gpu_scene;
    if (!gpu_scene.upload_camera(scene.camera))
        { rr::core::Logger::error("aovs: upload_camera failed"); return 1; }
    if (!gpu_scene.upload_relativity(scene.observer, scene.relativity))
        { rr::core::Logger::error("aovs: upload_relativity failed"); return 1; }
    if (!gpu_scene.upload_spheres(sphere_pods.data(), sphere_pods.size()))
        { rr::core::Logger::error("aovs: upload_spheres failed"); return 1; }
    if (!gpu_scene.upload_mesh(quad))
        { rr::core::Logger::error("aovs: upload_mesh failed"); return 1; }
    if (!gpu_scene.upload_materials(material_pods.data(), material_pods.size()))
        { rr::core::Logger::error("aovs: upload_materials failed"); return 1; }
    if (!gpu_scene.upload_lights(light_pods.data(), light_pods.size()))
        { rr::core::Logger::error("aovs: upload_lights failed"); return 1; }

    // Allocate one GpuAOVBuffer per declared AOVType. The default
    // factory returns the six in stable order:
    //   [0] Beauty, [1] Normal, [2] Depth,
    //   [3] Albedo, [4] DopplerFactor, [5] SearchlightFactor.
    auto aov_set = rr::renderer::make_default_aov_set();
    for (auto& b : aov_set) {
        if (!b.resize(cfg.width, cfg.height)) {
            rr::core::Logger::error("aovs: resize failed for "
                + std::string(rr::renderer::aov_type_name(b.type())));
            return 1;
        }
    }

    rr::cuda::CudaRenderer::AOVTargets targets;
    targets.beauty             = aov_set[0].device_ptr();
    targets.normal             = aov_set[1].device_ptr();
    targets.depth              = aov_set[2].device_ptr();
    targets.albedo             = aov_set[3].device_ptr();
    targets.doppler_factor     = aov_set[4].device_ptr();
    targets.searchlight_factor = aov_set[5].device_ptr();

    auto r = rr::cuda::CudaRenderer::render_scene_with_aovs(
        gpu_scene, cfg.width, cfg.height, targets);
    if (!r.ok) {
        rr::core::Logger::error("aovs render failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-aovs", cfg.width, cfg.height, r.gpu_time_ms);

    // Save each AOV to its named PPM. Output filenames match the
    // prompt's spec; the doppler / searchlight passes use the
    // shorter "_doppler" / "_searchlight" stems rather than the
    // verbose factor-suffixed forms.
    struct OutSpec { std::size_t idx; const char* path; const char* label; };
    static constexpr OutSpec kSpecs[] = {
        {0, "output/aov_beauty.ppm",       "AOV beauty"},
        {1, "output/aov_normal.ppm",       "AOV normal"},
        {2, "output/aov_depth.ppm",        "AOV depth"},
        {3, "output/aov_albedo.ppm",       "AOV albedo"},
        {4, "output/aov_doppler.ppm",      "AOV doppler"},
        {5, "output/aov_searchlight.ppm",  "AOV searchlight"},
    };

    bool all_ok = true;
    for (const auto& s : kSpecs) {
        if (!save_aov_to_ppm(aov_set[s.idx], s.path,
                             cfg.width, cfg.height, s.label)) {
            all_ok = false;
        }
    }

    // Stage 19B.4: when --denoise is set, additionally
    // invoke the OptiX denoiser on the Beauty / Albedo /
    // Normal AOV buffers and save output/denoised.ppm
    // alongside the standard six AOV PPMs. Per DENOISER
    // _PLAN §9.2.1 (manual trigger mode) + §9.3 (fixed
    // output path); --output is ignored by --render-aovs
    // and the denoised output uses the documented default.
    // The flag is silently ignored when OptiX is not
    // compiled in (per DENOISER_PLAN §9.4); a clear error
    // is logged so the user knows the denoise pass did
    // not happen.
    if (cfg.denoise_enabled) {
#ifdef RELATIVITYRENDER_ENABLE_OPTIX
        const std::string denoised_path = "output/denoised.ppm";
        if (!denoise_aov_buffers_to_ppm(
                /*beauty=*/aov_set[0],
                /*albedo=*/aov_set[3],
                /*normal=*/aov_set[1],
                cfg.width, cfg.height,
                denoised_path)) {
            all_ok = false;
        }
#else
        rr::core::Logger::error(
            "--denoise requires OptiX. Rebuild with "
            "-DRR_ENABLE_OPTIX=ON on a host "
            "with the OptiX SDK installed.");
        all_ok = false;
#endif
    }

    rr::core::Logger::info("aovs: "
                         + std::to_string(aov_set.size())
                         + " AOV(s) rendered, "
                         + std::to_string(cfg.width) + "x"
                         + std::to_string(cfg.height) + " each");

    return all_ok ? 0 : 1;
#endif
}

#if defined(RR_HAS_CUDA) && defined(RELATIVITYRENDER_ENABLE_OPTIX)
// Stage 19B.4 helper. Drives the full OptiX-denoiser
// orchestration (backend + denoiser init -> set_inputs ->
// allocate FLOAT3 output buffer -> invoke (timed) ->
// download -> widen FLOAT3 -> RGBA32F (alpha = 1) -> save
// PPM) given three already-populated `GpuAOVBuffer`s
// (Beauty / Albedo / Normal) and an output path. Shared
// between `run_render_denoise` (Stage 19B.3 dedicated
// demo) and `run_render_aovs` (Stage 19B.4
// `--denoise`-modifier integration).
//
// Returns true on success. Logs `[GPU] denoise:invoke ...`
// and `wrote denoised: ...` lines on success; logs an
// error and returns false on any failure.
//
// Pre-condition: both `RR_HAS_CUDA` and
// `RELATIVITYRENDER_ENABLE_OPTIX` are defined. The function
// itself is gated on the same combination so callers do
// not need to repeat the macro checks.
bool denoise_aov_buffers_to_ppm(
        const rr::renderer::GpuAOVBuffer& beauty_buf,
        const rr::renderer::GpuAOVBuffer& albedo_buf,
        const rr::renderer::GpuAOVBuffer& normal_buf,
        int                                width,
        int                                height,
        const std::string&                 out_path) {
    using rr::core::Logger;

    if (!beauty_buf.valid() || !albedo_buf.valid() || !normal_buf.valid()) {
        Logger::error("denoise: one or more AOV buffers is invalid");
        return false;
    }

    // Stage 19C.1: bracket the entire denoiser pass with a
    // GpuTimer so the caller gets one "ms/frame" line for
    // the full pass (init + set_inputs + invoke + sync +
    // download). Pure CPU sections (set_inputs descriptor
    // build, host-side widen) contribute ~0 to the GPU
    // timer's elapsed time because no GPU work runs during
    // them; that's the correct measurement for "GPU-side
    // denoiser cost". The fine-grained `denoise:invoke`
    // line below isolates the optixDenoiserInvoke cost
    // alone.
    rr::gpu::GpuTimer total_timer;
    total_timer.start();

    // Stage 19C.3 noisy-fallback helper. Per the master
    // "renderer must never crash due to denoiser" rule:
    // when any denoiser-side step fails, log a Logger
    // ::warning, download the noisy Beauty AOV directly
    // (FLOAT3 -> RGBA32F widen), and save it to the
    // requested `out_path`. The user always gets a saved
    // image; the only difference is the absence of
    // denoising. This wrapper returns true on a successful
    // fallback save and false only when even the noisy
    // download / save fails (genuine catastrophe).
    //
    // The denoiser-pass GPU timing lines (denoise:invoke /
    // denoise:total from Stage 19C.1) are intentionally
    // skipped on the fallback path because the denoiser
    // did not actually run a successful pass to time.
    const auto save_noisy_fallback =
        [&](const std::string& reason) -> bool {
        Logger::warning("denoise: " + reason
                      + "; falling back to noisy Beauty AOV "
                        "(no denoising applied)");

        std::vector<float> host_rgb;
        if (!beauty_buf.download(host_rgb)) {
            Logger::error("denoise: noisy-fallback download "
                          "failed; no image saved");
            return false;
        }
        const std::size_t expected_floats =
            static_cast<std::size_t>(width)
          * static_cast<std::size_t>(height) * 3u;
        if (host_rgb.size() != expected_floats) {
            Logger::error("denoise: noisy-fallback download "
                          "size mismatch; no image saved");
            return false;
        }

        rr::image::Image img(width, height,
                             rr::image::PixelFormat::Rgba32F);
        const std::size_t pixel_count =
            static_cast<std::size_t>(width) * height;
        float* dst = img.data();
        for (std::size_t i = 0; i < pixel_count; ++i) {
            dst[i * 4 + 0] = host_rgb[i * 3 + 0];
            dst[i * 4 + 1] = host_rgb[i * 3 + 1];
            dst[i * 4 + 2] = host_rgb[i * 3 + 2];
            dst[i * 4 + 3] = 1.0f;
        }
        return save_image_or_error(img, out_path,
                                   "denoised (noisy fallback)",
                                   width, height);
    };

    rr::optix::OptixBackend backend;
    if (!backend.initialize()) {
        return save_noisy_fallback("OptixBackend init failed: "
                                 + backend.last_error());
    }
    rr::optix::OptixDenoiser denoiser;
    if (!denoiser.initialize(backend)) {
        return save_noisy_fallback("OptixDenoiser init failed: "
                                 + denoiser.last_error());
    }

    rr::optix::OptixDenoiser::Inputs inputs;
    inputs.beauty_device     = beauty_buf.device_ptr();
    inputs.beauty_components = 3;  // Stage 14A AOV is FLOAT3
    inputs.albedo_device     = albedo_buf.device_ptr();
    inputs.normal_device     = normal_buf.device_ptr();
    inputs.width             = width;
    inputs.height            = height;
    if (!denoiser.set_inputs(inputs)) {
        return save_noisy_fallback("set_inputs failed: "
                                 + denoiser.last_error());
    }

    // Allocate the denoised-output device buffer (FLOAT3 to
    // match the FLOAT3 Beauty input). Caller-side RAII;
    // OptixDenoiser does not free it on shutdown.
    const std::size_t out_float_count =
        static_cast<std::size_t>(width) * height * 3u;
    rr::gpu::GpuBuffer<float> denoised_dev;
    if (!denoised_dev.allocate(out_float_count)) {
        return save_noisy_fallback(
            "denoised output buffer allocate failed");
    }

    rr::optix::OptixDenoiser::Output output;
    output.device = denoised_dev.device_ptr();
    output.width  = width;
    output.height = height;

    // Stage 19C.1: fine-grained timer bracketing just the
    // optixDenoiserInvoke (+ its internal cudaDeviceSynchronize)
    // so the user can separate the actual denoise compute cost
    // from the surrounding setup / download overhead measured
    // by `total_timer`.
    rr::gpu::GpuTimer invoke_timer;
    invoke_timer.start();
    const bool invoke_ok = denoiser.invoke(output);
    invoke_timer.stop();
    if (!invoke_ok) {
        return save_noisy_fallback("invoke failed: "
                                 + denoiser.last_error());
    }
    log_denoiser_timing("denoise:invoke", width, height,
                        invoke_timer.elapsed_ms());

    // Download FLOAT3 device output -> host vector, then
    // widen to RGBA32F (alpha = 1) for save_ppm. Per
    // DENOISER_PLAN §9.5 + Stage 19B.3 architectural notes:
    // constant-alpha fill is per-pixel host arithmetic, not
    // shading - it does not violate the master "no per-
    // pixel CPU work" rule.
    std::vector<float> host_rgb(out_float_count);
    if (!denoised_dev.download(host_rgb.data(), out_float_count)) {
        return save_noisy_fallback(
            "denoised output download failed");
    }
    rr::image::Image img(width, height, rr::image::PixelFormat::Rgba32F);
    {
        const std::size_t pixel_count =
            static_cast<std::size_t>(width) * height;
        float* dst = img.data();
        for (std::size_t i = 0; i < pixel_count; ++i) {
            dst[i * 4 + 0] = host_rgb[i * 3 + 0];
            dst[i * 4 + 1] = host_rgb[i * 3 + 1];
            dst[i * 4 + 2] = host_rgb[i * 3 + 2];
            dst[i * 4 + 3] = 1.0f;
        }
    }

    // Stage 19C.1: stop the total-pass timer before the
    // host-side PPM save (which is image-IO cost, not
    // denoiser cost) so the `ms/frame` figure isolates the
    // GPU-touched portion of the pass.
    total_timer.stop();
    log_denoiser_timing("denoise:total", width, height,
                        total_timer.elapsed_ms());

    return save_image_or_error(img, out_path, "denoised", width, height);
}

// Stage 21D.4: end-to-end orchestration helper for the new
// high-level `OptixDenoiser::denoise(Inputs, Output)` API
// (Stage 21D.1..21D.3). Allocates a device-side output
// buffer sized to width * height * beauty_components floats,
// calls `denoiser.denoise(inputs, output)` to fill it on the
// GPU, downloads the result via `GpuBuffer::download`, and
// saves the host-side image to PPM via the established
// `save_image_or_error` helper.
//
// Master rule compliance:
// - The denoise step itself runs entirely on the GPU
//   (`optixDenoiserInvoke` + the SDK's CUDA kernels). The
//   host orchestration here only allocates the device
//   output buffer, calls `denoise()`, downloads bytes via
//   `cudaMemcpy(D->H)`, and writes the PPM. No per-pixel
//   work on the CPU.
// - Per the rules: "CPU may download/save only", "CPU must
//   not denoise or modify pixels". The download is a single
//   `cudaMemcpy` (no per-pixel transformation); the save
//   serialises the float framebuffer through `Image::save_ppm`
//   without modifying pixels.
//
// Default `out_path` matches the Stage 21A.6 contract
// (`output/denoised.ppm`); callers can override per the
// standard `--output` convention. The function does not
// wire to any CLI surface in this slice (per Stage 21D.x's
// "no CLI integration yet" rule); the existing
// `--render-denoise` and `--render-aovs --denoise` paths
// still flow through the legacy
// `denoise_aov_buffers_to_ppm` helper above.
bool denoise_and_save_ppm(
        rr::optix::OptixDenoiser&                denoiser,
        const rr::optix::OptixDenoiser::Inputs&  inputs,
        const std::string&                       out_path
            = std::string("output/denoised.ppm")) {
    using rr::core::Logger;

    if (inputs.width <= 0 || inputs.height <= 0) {
        Logger::error("denoise_and_save_ppm: invalid dimensions ("
                    + std::to_string(inputs.width) + "x"
                    + std::to_string(inputs.height) + ").");
        return false;
    }
    if (inputs.beauty_components != 3 && inputs.beauty_components != 4) {
        Logger::error("denoise_and_save_ppm: beauty_components must be "
                      "3 (FLOAT3) or 4 (FLOAT4); got "
                    + std::to_string(inputs.beauty_components));
        return false;
    }

    // Allocate the device-side output buffer the denoiser
    // will write into. `width * height * beauty_components`
    // floats per Stage 21A.6 / Stage 21C.1 contract.
    const std::size_t pixel_count =
        static_cast<std::size_t>(inputs.width)
      * static_cast<std::size_t>(inputs.height);
    const std::size_t output_floats =
        pixel_count * static_cast<std::size_t>(inputs.beauty_components);

    rr::gpu::GpuBuffer<float> output_buf;
    if (!output_buf.allocate(output_floats)) {
        Logger::error("denoise_and_save_ppm: failed to allocate "
                      "device output buffer ("
                    + std::to_string(output_floats * sizeof(float))
                    + " bytes).");
        return false;
    }

    rr::optix::OptixDenoiser::Output output{};
    output.device = output_buf.device_ptr();
    output.width  = inputs.width;
    output.height = inputs.height;

    // Stage 21D.5 noisy-Beauty fallback. When the denoiser
    // fails (init / set_inputs / invoke / sync / download
    // path inside `denoise()`), download the noisy Beauty
    // AOV instead and save it under the same `out_path` so
    // the renderer always produces a file at the documented
    // location (Stage 21A.7 contract). Logs a single warning
    // line describing the cause; never crashes; never throws.
    // The fallback download is a single byte-level
    // `gpu_copy_device_to_host` (no per-pixel host loop).
    const auto save_noisy_fallback =
        [&](const std::string& reason) -> bool {
        Logger::warning("denoise: " + reason
                      + "; falling back to noisy Beauty AOV "
                        "(no denoising applied)");

        rr::image::Image img(inputs.width, inputs.height,
                             (inputs.beauty_components == 4)
                                 ? rr::image::PixelFormat::Rgba32F
                                 : rr::image::PixelFormat::Rgb32F);

        const std::size_t bytes = output_floats * sizeof(float);
        if (!rr::gpu::detail::gpu_copy_device_to_host(
                img.data(), inputs.beauty_device, bytes)) {
            Logger::error("denoise: noisy-fallback download failed; "
                          "no image saved");
            return false;
        }

        return save_image_or_error(img, out_path,
                                   "denoised (noisy fallback)",
                                   inputs.width, inputs.height);
    };

    if (!denoiser.denoise(inputs, output)) {
        return save_noisy_fallback(denoiser.last_error());
    }

    // Download the denoised radiance into a host-side Image.
    // Layout matches `output_floats`; the format is RGB32F
    // for FLOAT3 / RGBA32F for FLOAT4 - both are direct
    // serialisations to PPM via `Image::save_ppm`'s
    // float-to-uint8 clamp.
    rr::image::Image img(inputs.width, inputs.height,
                         (inputs.beauty_components == 4)
                             ? rr::image::PixelFormat::Rgba32F
                             : rr::image::PixelFormat::Rgb32F);
    if (!output_buf.download(img.data(), output_floats)) {
        return save_noisy_fallback(
            "failed to download denoised output buffer to host");
    }

    return save_image_or_error(img, out_path, "denoised",
                               inputs.width, inputs.height);
}
#endif  // RR_HAS_CUDA && RELATIVITYRENDER_ENABLE_OPTIX

// `--render-denoise` dispatch (Stage 19B.3). Builds a small
// demo scene (4 diffuse spheres, no lights, no relativity)
// and runs it through the AOV pipeline (`render_scene_with
// _aovs`) to populate Beauty / Albedo / Normal device
// buffers. Then drives the OptixDenoiser end-to-end via
// the shared `denoise_aov_buffers_to_ppm` helper. The
// denoiser runs entirely on the GPU; the host only
// orchestrates and saves the final image, mirroring every
// other render-* CLI handler.
//
// Default output path: `output/denoised.ppm`. `--output`
// overrides per the standard convention.
//
// Requires both `RR_HAS_CUDA` (for the AOV-pipeline render
// kernel) and `RELATIVITYRENDER_ENABLE_OPTIX` (for the
// denoiser); the audit-host fallbacks return the documented
// "requires" errors.
int run_render_denoise(const rr::core::Config& cfg) {
    using rr::core::Logger;

    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/denoised.ppm")
        : cfg.output_path;

#if !defined(RR_HAS_CUDA) || !defined(RELATIVITYRENDER_ENABLE_OPTIX)
    (void)cfg; (void)out_path;
    Logger::error("--render-denoise requires both CUDA and OptiX. "
                  "Rebuild with -DRR_ENABLE_CUDA=ON "
                  "-DRR_ENABLE_OPTIX=ON on a host with "
                  "the CUDA Toolkit + OptiX SDK installed (also pass "
                  "-DOPTIX_ROOT=/path/to/optix-sdk).");
    return 1;
#else
    // Small demo scene: four diffuse spheres, no lights, no
    // textures, no observer velocity. The k_render_scene
    // kernel falls through to its facing-ratio fallback
    // when light_count == 0, so the Beauty pass holds visible
    // shaded radiance with Monte-Carlo-free single-sample
    // colour. Albedo + Normal AOVs come straight from the
    // hit's material / world-space normal.
    rr::scene::Scene scene;
    scene.render_settings.width  = cfg.width;
    scene.render_settings.height = cfg.height;
    scene.camera.set_aspect(static_cast<float>(cfg.width)
                          / static_cast<float>(cfg.height));

    auto red     = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.85f, 0.20f, 0.20f});
    auto green   = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.20f, 0.80f, 0.30f});
    auto blue    = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.20f, 0.30f, 0.90f});
    auto neutral = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.65f, 0.65f, 0.65f});
    scene.materials.push_back({0, "red",     red.params()});
    scene.materials.push_back({1, "green",   green.params()});
    scene.materials.push_back({2, "blue",    blue.params()});
    scene.materials.push_back({3, "neutral", neutral.params()});

    const auto add_sphere = [&](float cx, float cy, float cz, float r,
                                int mat, const char* name) {
        rr::scene::SceneSphere s;
        s.object.name = name;
        s.geometry    = rr::geometry::Sphere{
            rr::math::Vec3{cx, cy, cz}, r, mat};
        scene.spheres.push_back(s);
    };
    add_sphere(-1.5f,  0.2f, -4.0f, 0.7f, 0, "left");
    add_sphere( 0.0f, -0.1f, -3.5f, 0.8f, 1, "centre");
    add_sphere( 1.5f,  0.2f, -4.0f, 0.7f, 2, "right");
    add_sphere( 0.0f, -1.4f, -5.0f, 1.0f, 3, "ground-bulb");

    std::vector<rr::geometry::Sphere> sphere_pods;
    sphere_pods.reserve(scene.spheres.size());
    for (const auto& s : scene.spheres) {
        if (s.object.visible) sphere_pods.push_back(s.geometry);
    }
    std::vector<rr::material::MaterialParams> material_pods;
    material_pods.reserve(scene.materials.size());
    for (const auto& m : scene.materials) {
        material_pods.push_back(m.params);
    }

    rr::gpu::GpuScene gpu_scene;
    if (!gpu_scene.upload_camera(scene.camera))
        { Logger::error("denoise: upload_camera failed"); return 1; }
    if (!gpu_scene.upload_relativity(scene.observer, scene.relativity))
        { Logger::error("denoise: upload_relativity failed"); return 1; }
    if (!gpu_scene.upload_spheres(sphere_pods.data(), sphere_pods.size()))
        { Logger::error("denoise: upload_spheres failed"); return 1; }
    if (!gpu_scene.upload_materials(material_pods.data(),
                                    material_pods.size()))
        { Logger::error("denoise: upload_materials failed"); return 1; }

    // Allocate only the three AOV buffers the denoiser
    // consumes (DENOISER_PLAN §8.1: Beauty / Albedo /
    // Normal). The remaining AOVTargets fields stay null so
    // the kernel skips those writes.
    rr::renderer::GpuAOVBuffer beauty_buf(rr::renderer::AOV::make_beauty());
    rr::renderer::GpuAOVBuffer normal_buf(rr::renderer::AOV::make_normal());
    rr::renderer::GpuAOVBuffer albedo_buf(rr::renderer::AOV::make_albedo());
    if (!beauty_buf.resize(cfg.width, cfg.height) ||
        !normal_buf.resize(cfg.width, cfg.height) ||
        !albedo_buf.resize(cfg.width, cfg.height)) {
        Logger::error("denoise: AOV resize failed");
        return 1;
    }

    rr::cuda::CudaRenderer::AOVTargets targets;
    targets.beauty = beauty_buf.device_ptr();
    targets.normal = normal_buf.device_ptr();
    targets.albedo = albedo_buf.device_ptr();

    auto r = rr::cuda::CudaRenderer::render_scene_with_aovs(
        gpu_scene, cfg.width, cfg.height, targets);
    if (!r.ok) {
        Logger::error("denoise: render_scene_with_aovs failed: " + r.message);
        return 1;
    }
    log_gpu_timing("render-denoise:render", cfg.width, cfg.height,
                   r.gpu_time_ms);

    // Stage 19B.4: hand the three required AOV buffers to
    // the shared denoiser orchestration helper. The helper
    // owns the full OptixBackend / OptixDenoiser /
    // set_inputs / invoke / download / save sequence.
    return denoise_aov_buffers_to_ppm(
        beauty_buf, albedo_buf, normal_buf,
        cfg.width, cfg.height, out_path) ? 0 : 1;
#endif
}

// `--render-optix-denoise` dispatch (Stage 21D.6). End-to-end
// runtime verification slice for the new
// `OptixDenoiser::denoise(Inputs, Output)` API (Stage 21D.1
// shell + 21D.2 invoke + 21D.3 guided contract + 21D.4
// download/save + 21D.5 noisy-fallback).
//
// Uses the SAME demo scene + AOV-pipeline shape as
// `run_render_denoise`: four diffuse spheres, no lights,
// no textures, no observer velocity. Beauty / Albedo /
// Normal AOVs are populated by `render_scene_with_aovs`,
// then the device pointers are passed to the new
// `denoise_and_save_ppm` helper. This is the first CLI
// surface that exercises `OptixDenoiser::denoise(...)` end
// to end on a CUDA + OptiX SDK host.
//
// Default output path: `output/denoised.ppm` per the
// Stage 21A.6 contract; `--output` overrides per the
// standard convention.
//
// Audit-host fallback: when either CUDA or OptiX is
// missing at build time, the function exits 1 with the
// documented "requires" error. The user's "if no OptiX
// runtime is available, document as runtime deferred,
// not code failure" rule covers this case: the source
// builds cleanly in every mode; the actual denoise
// happens only on a real CUDA + OptiX SDK host.
int run_render_optix_denoise(const rr::core::Config& cfg) {
    using rr::core::Logger;

    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/denoised.ppm")
        : cfg.output_path;

#if !defined(RR_HAS_CUDA) || !defined(RELATIVITYRENDER_ENABLE_OPTIX)
    (void)cfg; (void)out_path;
    Logger::error("--render-optix-denoise requires both CUDA and OptiX. "
                  "Rebuild with -DRR_ENABLE_CUDA=ON "
                  "-DRR_ENABLE_OPTIX=ON on a host with "
                  "the CUDA Toolkit + OptiX SDK installed (also pass "
                  "-DOPTIX_ROOT=/path/to/optix-sdk).");
    return 1;
#else
    // Same demo scene as --render-denoise (4 diffuse spheres,
    // no lights, no textures). The k_render_scene kernel
    // falls through to its facing-ratio fallback when
    // light_count == 0; Beauty / Albedo / Normal AOVs come
    // straight from the hit's material / world-space normal.
    rr::scene::Scene scene;
    scene.render_settings.width  = cfg.width;
    scene.render_settings.height = cfg.height;
    scene.camera.set_aspect(static_cast<float>(cfg.width)
                          / static_cast<float>(cfg.height));

    auto red     = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.85f, 0.20f, 0.20f});
    auto green   = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.20f, 0.80f, 0.30f});
    auto blue    = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.20f, 0.30f, 0.90f});
    auto neutral = rr::material::Material::make_diffuse(
        rr::math::Vec3{0.65f, 0.65f, 0.65f});
    scene.materials.push_back({0, "red",     red.params()});
    scene.materials.push_back({1, "green",   green.params()});
    scene.materials.push_back({2, "blue",    blue.params()});
    scene.materials.push_back({3, "neutral", neutral.params()});

    const auto add_sphere = [&](float cx, float cy, float cz, float r,
                                int mat, const char* name) {
        rr::scene::SceneSphere s;
        s.object.name = name;
        s.geometry    = rr::geometry::Sphere{
            rr::math::Vec3{cx, cy, cz}, r, mat};
        scene.spheres.push_back(s);
    };
    add_sphere(-1.5f,  0.2f, -4.0f, 0.7f, 0, "left");
    add_sphere( 0.0f, -0.1f, -3.5f, 0.8f, 1, "centre");
    add_sphere( 1.5f,  0.2f, -4.0f, 0.7f, 2, "right");
    add_sphere( 0.0f, -1.4f, -5.0f, 1.0f, 3, "ground-bulb");

    std::vector<rr::geometry::Sphere> sphere_pods;
    sphere_pods.reserve(scene.spheres.size());
    for (const auto& s : scene.spheres) {
        if (s.object.visible) sphere_pods.push_back(s.geometry);
    }
    std::vector<rr::material::MaterialParams> material_pods;
    material_pods.reserve(scene.materials.size());
    for (const auto& m : scene.materials) {
        material_pods.push_back(m.params);
    }

    rr::gpu::GpuScene gpu_scene;
    if (!gpu_scene.upload_camera(scene.camera))
        { Logger::error("optix-denoise: upload_camera failed"); return 1; }
    if (!gpu_scene.upload_relativity(scene.observer, scene.relativity))
        { Logger::error("optix-denoise: upload_relativity failed"); return 1; }
    if (!gpu_scene.upload_spheres(sphere_pods.data(), sphere_pods.size()))
        { Logger::error("optix-denoise: upload_spheres failed"); return 1; }
    if (!gpu_scene.upload_materials(material_pods.data(),
                                    material_pods.size()))
        { Logger::error("optix-denoise: upload_materials failed"); return 1; }

    rr::renderer::GpuAOVBuffer beauty_buf(rr::renderer::AOV::make_beauty());
    rr::renderer::GpuAOVBuffer normal_buf(rr::renderer::AOV::make_normal());
    rr::renderer::GpuAOVBuffer albedo_buf(rr::renderer::AOV::make_albedo());
    if (!beauty_buf.resize(cfg.width, cfg.height) ||
        !normal_buf.resize(cfg.width, cfg.height) ||
        !albedo_buf.resize(cfg.width, cfg.height)) {
        Logger::error("optix-denoise: AOV resize failed");
        return 1;
    }

    rr::cuda::CudaRenderer::AOVTargets targets;
    targets.beauty = beauty_buf.device_ptr();
    targets.normal = normal_buf.device_ptr();
    targets.albedo = albedo_buf.device_ptr();

    auto r = rr::cuda::CudaRenderer::render_scene_with_aovs(
        gpu_scene, cfg.width, cfg.height, targets);
    if (!r.ok) {
        Logger::error("optix-denoise: render_scene_with_aovs failed: "
                    + r.message);
        return 1;
    }
    log_gpu_timing("render-optix-denoise:render",
                   cfg.width, cfg.height, r.gpu_time_ms);

    // Initialise the OptiX backend + denoiser. The new
    // `denoise_and_save_ppm` helper expects an already-
    // initialised denoiser; it does NOT take ownership of
    // the backend / denoiser handles.
    rr::optix::OptixBackend backend;
    if (!backend.initialize()) {
        Logger::error("optix-denoise: OptixBackend init failed: "
                    + backend.last_error());
        return 1;
    }
    rr::optix::OptixDenoiser denoiser;
    if (!denoiser.initialize(backend)) {
        Logger::error("optix-denoise: OptixDenoiser init failed: "
                    + denoiser.last_error());
        return 1;
    }

    // Build the Inputs POD from the AOV device pointers.
    // Layout matches the Stage 21C.1 / 21A.3 contract:
    // Beauty FLOAT3 (the AOV pipeline default), Albedo
    // FLOAT3 (linear, pre-lighting), Normal FLOAT3 (encoded
    // `0.5 n + 0.5`).
    rr::optix::OptixDenoiser::Inputs inputs;
    inputs.beauty_device     = beauty_buf.device_ptr();
    inputs.beauty_components = 3;
    inputs.albedo_device     = albedo_buf.device_ptr();
    inputs.normal_device     = normal_buf.device_ptr();
    inputs.width             = cfg.width;
    inputs.height            = cfg.height;

    // End-to-end orchestration: denoise -> download -> save.
    // The Stage 21D.5 noisy-Beauty fallback fires inside
    // the helper if the denoise step fails; the function
    // returns true whenever a file (denoised OR noisy
    // fallback) was successfully written.
    return denoise_and_save_ppm(denoiser, inputs, out_path) ? 0 : 1;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    using rr::core::CommandLine;
    using rr::core::Logger;

    const auto result = CommandLine::parse(argc, argv);

    // Stage 21E.1: announce whether the --denoise modifier
    // was requested. Logged once per invocation, only when
    // the flag is set, so the standard quiet path (no
    // `--denoise`) emits no extra log line. Per-action
    // dispatchers consume `result.config.denoise_enabled`
    // separately to decide whether to actually run the
    // denoiser; this slice only prints the request state.
    if (result.config.denoise_enabled) {
        Logger::info("denoise: requested via --denoise flag");
    }

    switch (result.action) {
        case CommandLine::Action::Help:
            std::cout << CommandLine::usage(argv[0]);
            return 0;

        case CommandLine::Action::Version:
            std::cout << CommandLine::version_string() << '\n';
            return 0;

        case CommandLine::Action::DeviceInfo:
            report_device_info();
            return 0;

        case CommandLine::Action::Render:
            return run_render(result.config);

        case CommandLine::Action::SceneInfo:
            return run_scene_info(result.config);

        case CommandLine::Action::SceneSummary:
            return run_scene_summary(result.config);

        case CommandLine::Action::RenderFromScene:
            return run_render_from_scene(result.config);

        case CommandLine::Action::RenderFullScene:
            return run_render_full_scene(result.config);

        case CommandLine::Action::RenderRngTest:
            return run_render_rng_test(result.config);

        case CommandLine::Action::RenderAccumulationTest:
            return run_render_accumulation_test(result.config);

        case CommandLine::Action::RenderPathtrace:
            return run_render_pathtrace(result.config);

        case CommandLine::Action::RenderGradient:
            return run_render_gradient(result.config);

        case CommandLine::Action::RenderRays:
            return run_render_camera_rays(result.config);

        case CommandLine::Action::RenderSphere:
            return run_render_sphere(result.config);

        case CommandLine::Action::RenderRelativistic:
            return run_render_relativistic(result.config);

        case CommandLine::Action::RenderDemo:
            return run_render_demo(result.config);

        case CommandLine::Action::RenderScene:
            return run_render_scene(result.config);

        case CommandLine::Action::RenderTriangle:
            return run_render_triangle(result.config);

        case CommandLine::Action::RenderMeshScene:
            return run_render_mesh_scene(result.config);

        case CommandLine::Action::RenderMaterialScene:
            return run_render_material_scene(result.config);

        case CommandLine::Action::RenderDirectLighting:
            return run_render_direct_lighting(result.config);

        case CommandLine::Action::RenderTextureSampleTest:
            return run_render_texture_sample_test(result.config);

        case CommandLine::Action::RenderTexturedMaterial:
            return run_render_textured_material(result.config);

        case CommandLine::Action::RenderAOVs:
            return run_render_aovs(result.config);

        case CommandLine::Action::Server:
            return run_server(result.config);

        case CommandLine::Action::RenderOptixTest:
            return run_render_optix_test(result.config);

        case CommandLine::Action::RenderOptixTriangle:
            return run_render_optix_triangle(result.config);

        case CommandLine::Action::RenderOptixRelativity:
            return run_render_optix_relativity(result.config);

        case CommandLine::Action::RenderOptixRaygen:
            return run_render_optix_raygen(result.config);

        case CommandLine::Action::RenderOptixMeshScene:
            return run_render_optix_mesh_scene(result.config);

        case CommandLine::Action::RenderOptixMaterialScene:
            return run_render_optix_material_scene(result.config);

        case CommandLine::Action::RenderOptixPathtrace:
            return run_render_optix_pathtrace(result.config);

        case CommandLine::Action::RenderOptixDirectLighting:
            return run_render_optix_direct_lighting(result.config);

        case CommandLine::Action::RenderOptixShadowTest:
            return run_render_optix_shadow_test(result.config);

        case CommandLine::Action::RenderOptixTexturedMaterial:
            return run_render_optix_textured_material(result.config);

        case CommandLine::Action::RenderOptixAovs:
            return run_render_optix_aovs(result.config);

        case CommandLine::Action::RenderOptixDenoise:
            return run_render_optix_denoise(result.config);

        case CommandLine::Action::RenderDenoise:
            return run_render_denoise(result.config);

        case CommandLine::Action::Error:
            Logger::error(result.error_message);
            std::cerr << CommandLine::usage(argv[0]);
            return 2;

        case CommandLine::Action::Default:
            Logger::info(std::string(rr::core::kProjectName) + " "
                       + rr::core::kVersionString + " starting up.");
            Logger::info("Stage 15A.2: CLI server mode. "
                         "Try --server, "
                         "--render-aovs, "
                         "--render-textured-material, "
                         "--render-texture-sample-test, "
                         "--render-pathtrace <file>, "
                         "--render-accumulation-test, "
                         "--render-rng-test, "
                         "--render-full-scene <file>, "
                         "--render-from-scene <file>, "
                         "--scene-summary <file>, "
                         "--scene-info <file>, --device-info, "
                         "--render-gradient, --render-rays, "
                         "--render-sphere, --render-relativistic, "
                         "--render-scene, --render-triangle, "
                         "--render-mesh-scene, "
                         "--render-material-scene, "
                         "or --render-direct-lighting.");
            return 0;
    }
    return 0;
}
