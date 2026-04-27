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
        // M8 test path: the GPU generates a primary ray per pixel,
        // intersects against a single sphere, and shades. The CPU
        // only constructs the camera + sphere structs, launches the
        // kernel, downloads the framebuffer, and saves. No CPU pixel
        // loop runs in this code path (save_ppm internals are the
        // one permitted exception per the engineering rules).
        const std::filesystem::path out_path =
            cfg.output_image_path.value_or("output/gpu_sphere.ppm");

        rr::camera::Camera camera;  // origin, looking down -Z, +Y up
        camera.set_aspect(static_cast<float>(cfg.width)
                          / static_cast<float>(cfg.height));

        // Hard-coded test scene: one sphere centred 3 units in front of
        // the camera with unit radius. Real scene loading lands at M13.
        const rr::geometry::Sphere sphere{
            rr::math::Vec3{0.0f, 0.0f, -3.0f}, 1.0f};

        Logger::info("rendering sphere on GPU: "
                     + std::to_string(cfg.width) + "x"
                     + std::to_string(cfg.height));

        auto result = rr::cuda::CudaRenderer::render_sphere(
            camera, sphere, cfg.width, cfg.height);
        if (!result.ok) {
            Logger::error("GPU render failed: " + result.message);
            return 1;
        }

        std::error_code ec;
        if (out_path.has_parent_path()) {
            std::filesystem::create_directories(out_path.parent_path(), ec);
            // ec from create_directories is non-fatal: save_ppm will fail
            // with a clearer message if the parent is genuinely unwritable.
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
