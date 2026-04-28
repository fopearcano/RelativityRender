#include "core/CommandLine.h"
#include "core/Config.h"
#include "core/Logger.h"
#include "core/Version.h"
#include "gpu/GpuDevice.h"

// SceneLoader runs on the host even when CUDA is absent (the
// loader is pure host code), so include it unconditionally.
#include "io/SceneLoader.h"
#include "scene/Scene.h"

#ifdef RR_HAS_CUDA
    #include "cuda/CudaRenderer.h"
    #include "gpu/GpuScene.h"
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

        const std::filesystem::path scene_path = *cfg.render_scene_path;
        Logger::info("loading scene: " + scene_path.string());
        auto load = rr::io::load_rrscene(scene_path);
        if (!load.ok) {
            Logger::error("scene load failed: " + load.message);
            return 1;
        }
        const auto& scene = load.scene;
        Logger::info("loaded scene: "
                     + std::to_string(scene.materials.size()) + " materials, "
                     + std::to_string(scene.spheres.size())   + " spheres, "
                     + std::to_string(scene.lights.size())    + " lights, "
                     + std::to_string(scene.meshes.size())    + " meshes");

        // The file's render_settings determine the output resolution.
        // CLI --width / --height are ignored when a scene file is
        // present; the file is the source of truth.
        const int width  = scene.render_settings.width;
        const int height = scene.render_settings.height;

#ifdef RR_HAS_CUDA
        // M13 deliverable: load -> upload -> render -> save. The CPU
        // only orchestrates; every per-ray step still runs on the
        // GPU. The only CPU pixel iteration is `Image::save_ppm`.

        rr::gpu::GpuScene gpu_scene;
        if (!gpu_scene.upload_from(scene)) {
            Logger::error("GPU upload failed (no CUDA device or "
                          "device allocation refused)");
            return 1;
        }
        Logger::info("uploaded scene: "
                     + std::to_string(gpu_scene.sphere_count())   + " spheres, "
                     + std::to_string(gpu_scene.gpu_mesh().triangle_count())
                                                                  + " triangles, "
                     + std::to_string(gpu_scene.material_count()) + " materials, "
                     + std::to_string(gpu_scene.light_count())    + " lights");

        auto result = rr::cuda::CudaRenderer::render_scene(
            gpu_scene, width, height);
        if (!result.ok) {
            Logger::error("GPU render failed: " + result.message);
            return 1;
        }

        const std::filesystem::path out_path =
            cfg.output_image_path.value_or("output/from_scene.ppm");
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
                     "-DRR_ENABLE_CUDA=ON to render the loaded scene)");
#endif
        return 0;
    }

    Logger::info("No action requested. Use --help to see options.");
    return 0;
}
