#include "core/CommandLine.h"
#include "core/Config.h"
#include "core/Logger.h"
#include "core/Version.h"
#include "gpu/GpuDevice.h"

#ifdef RR_HAS_CUDA
    #include "camera/Camera.h"
    #include "cuda/CudaRenderer.h"
    #include "geometry/Sphere.h"
    #include "math/Vec3.h"
    #include "relativity/RelativityParams.h"
#endif

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void log_device_info() {
    using rr::core::Logger;

    Logger::info(std::string("GPU backend: ") + rr::gpu::gpu_backend_name());

    if (!rr::gpu::gpu_backend_available()) {
        Logger::info("No GPU backend compiled in. "
                     "Reconfigure with -DRR_ENABLE_CUDA=ON to enable CUDA.");
        return;
    }

    const auto devices = rr::gpu::enumerate_devices();
    if (devices.empty()) {
        Logger::warning("No CUDA-capable devices visible.");
        return;
    }

    Logger::info(std::to_string(devices.size()) + " device(s) visible:");
    for (const auto& d : devices) {
        std::ostringstream os;
        os << "  [" << d.index << "] " << d.name
           << "  (cc "  << d.compute_capability_string()
           << ", "      << d.total_memory_human()
           << ", "      << d.multiprocessor_count << " SMs)";
        Logger::info(os.str());
    }
}

}

int main(int argc, char** argv) {
    using rr::core::CommandLine;
    using rr::core::Config;
    using rr::core::Logger;

    Config cfg;
    const auto parse = CommandLine::parse(argc, argv, cfg);

    switch (parse.status) {
    case CommandLine::Status::Help:
        std::cout << CommandLine::usage();
        return 0;
    case CommandLine::Status::Version:
        std::cout << rr::core::kProjectName << ' ' << rr::core::kVersionString << '\n';
        return 0;
    case CommandLine::Status::Error:
        Logger::error(parse.message);
        std::cerr << CommandLine::usage();
        return 2;
    case CommandLine::Status::Ok:
        break;
    }

    Logger::info(std::string(rr::core::kProjectName) + " " + rr::core::kVersionString + " starting");

    if (cfg.show_device_info) {
        log_device_info();
        return 0;
    }

    if (cfg.wants_render()) {
        Logger::info("render command received");

#ifdef RR_HAS_CUDA
        // M9 deliverable: the GPU runs the full relativistic perception
        // pipeline per pixel (ray-gen -> aberration -> intersection ->
        // Doppler colour -> beaming -> write). The CPU only configures
        // the camera, observer, params, and sphere, then launches once
        // per beta value and saves the four PPMs. No CPU pixel loop
        // runs in this code path (save_ppm internals are the one
        // permitted exception per the engineering rules).
        //
        // `--output` is intentionally ignored here: this milestone
        // produces a fixed sweep of four files so the M9 deliverable
        // is reproducible. Single-image renders remain available
        // through `CudaRenderer::render_sphere` for tests / future CLI
        // additions.

        rr::camera::Camera camera;  // origin, looking down -Z, +Y up
        camera.set_aspect(static_cast<float>(cfg.width)
                          / static_cast<float>(cfg.height));

        // Hard-coded test scene: one sphere centred 3 units in front of
        // the camera with unit radius. Real scene loading lands at M13.
        const rr::geometry::Sphere sphere{
            rr::math::Vec3{0.0f, 0.0f, -3.0f}, 1.0f};

        // Defaults: every effect enabled, every strength = 1. The
        // physics is honest at high beta - searchlight scaling is
        // ~D^4 so forward pixels saturate at beta ~= 0.95, which
        // is the iconic relativistic-flight headlight effect.
        const rr::relativity::RelativityParams params;

        struct Step {
            float       beta;
            const char* path;
        };
        const Step steps[] = {
            {0.00f, "output/sphere_beta_000.ppm"},
            {0.25f, "output/sphere_beta_025.ppm"},
            {0.75f, "output/sphere_beta_075.ppm"},
            {0.95f, "output/sphere_beta_095.ppm"},
        };

        Logger::info("relativistic sphere sweep: "
                     + std::to_string(cfg.width) + "x"
                     + std::to_string(cfg.height)
                     + ", " + std::to_string(sizeof(steps) / sizeof(steps[0]))
                     + " beta values");

        for (const auto& step : steps) {
            // Forward motion along the camera's view direction (-Z).
            // beta . dir(forward photon) > 0  -> blueshift along axis.
            rr::relativity::Observer observer;
            observer.velocity = rr::math::Vec3{0.0f, 0.0f, -step.beta};

            auto result = rr::cuda::CudaRenderer::render_relativistic_sphere(
                camera, observer, params, sphere, cfg.width, cfg.height);
            if (!result.ok) {
                Logger::error("GPU render failed at beta="
                              + std::to_string(step.beta) + ": "
                              + result.message);
                return 1;
            }

            const std::filesystem::path out_path = step.path;
            std::error_code             ec;
            if (out_path.has_parent_path()) {
                std::filesystem::create_directories(out_path.parent_path(), ec);
            }
            if (!result.image.save_ppm(out_path)) {
                Logger::error("saving image failed: " + out_path.string());
                return 1;
            }
            Logger::info("saved " + out_path.string()
                         + " (beta=" + std::to_string(step.beta) + ")");
        }
#else
        Logger::info("(no CUDA backend compiled; rebuild with "
                     "-DRR_ENABLE_CUDA=ON to render)");
#endif
        return 0;
    }

    Logger::info("No action requested. Use --help to see options.");
    return 0;
}
