#include "core/CommandLine.h"
#include "core/Config.h"
#include "core/Logger.h"
#include "core/Version.h"
#include "gpu/GpuDevice.h"

#ifdef RR_HAS_CUDA
    #include "camera/Camera.h"
    #include "cuda/CudaRenderer.h"
    #include "geometry/Sphere.h"
    #include "gpu/GpuScene.h"
    #include "math/Vec3.h"
    #include "relativity/RelativityParams.h"
    #include "scene/Scene.h"
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
        // M10 deliverable: build a host `Scene`, upload to a `GpuScene`,
        // and let the kernel walk the uploaded sphere array per pixel.
        // The CPU only populates the scene structs and launches the
        // upload + kernel; every per-ray step (aberration,
        // intersection, shading, Doppler, beaming) runs on the GPU.
        // The only CPU iteration over pixels is in `Image::save_ppm`,
        // which is the permitted exception.
        //
        // `--output` is intentionally ignored: this milestone produces
        // a single fixed file (`output/gpu_scene_spheres.ppm`) so the
        // deliverable is reproducible.

        rr::scene::Scene scene;
        scene.camera.set_aspect(static_cast<float>(cfg.width)
                                / static_cast<float>(cfg.height));

        // A small test scene: a centred unit sphere flanked by two
        // smaller spheres, plus a large "ground" sphere below the
        // camera. Real scene loading lands at M13.
        const auto add_sphere = [&](const rr::math::Vec3& center, float radius) {
            rr::scene::SceneSphere s;
            s.geometry.center = center;
            s.geometry.radius = radius;
            scene.spheres.push_back(s);
        };
        add_sphere(rr::math::Vec3{ 0.0f,    0.0f, -3.0f}, 1.0f);
        add_sphere(rr::math::Vec3{-1.6f,    0.0f, -3.5f}, 0.6f);
        add_sphere(rr::math::Vec3{ 1.6f,    0.0f, -3.5f}, 0.6f);
        add_sphere(rr::math::Vec3{ 0.0f, -101.0f, -3.0f}, 100.0f);

        // Default observer (rest frame) + default params (every effect
        // enabled). At rest, every relativistic effect collapses to
        // identity, so the result is a classical multi-sphere render.
        // Non-rest configurations are exercised by the M9 sweep.

        rr::gpu::GpuScene gpu_scene;
        if (!gpu_scene.upload_from(scene)) {
            Logger::error("scene upload failed (no GPU backend or "
                          "device allocation refused)");
            return 1;
        }
        Logger::info("uploaded scene: "
                     + std::to_string(gpu_scene.sphere_count()) + " spheres");

        auto result = rr::cuda::CudaRenderer::render_scene(
            gpu_scene, cfg.width, cfg.height);
        if (!result.ok) {
            Logger::error("GPU render failed: " + result.message);
            return 1;
        }

        const std::filesystem::path out_path = "output/gpu_scene_spheres.ppm";
        std::error_code ec;
        if (out_path.has_parent_path()) {
            std::filesystem::create_directories(out_path.parent_path(), ec);
        }
        if (!result.image.save_ppm(out_path)) {
            Logger::error("saving image failed: " + out_path.string());
            return 1;
        }
        Logger::info("saved " + out_path.string());
#else
        Logger::info("(no CUDA backend compiled; rebuild with "
                     "-DRR_ENABLE_CUDA=ON to render)");
#endif
        return 0;
    }

    Logger::info("No action requested. Use --help to see options.");
    return 0;
}
