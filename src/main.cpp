#include "core/CommandLine.h"
#include "core/Config.h"
#include "core/Logger.h"
#include "core/Version.h"
#include "gpu/GpuDevice.h"

#ifdef RR_HAS_CUDA
    #include "camera/Camera.h"
    #include "cuda/CudaRenderer.h"
    #include "geometry/Mesh.h"
    #include "geometry/Sphere.h"
    #include "geometry/Triangle.h"
    #include "gpu/GpuScene.h"
    #include "math/Vec2.h"
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
        // M10 final deliverable: GPU triangle rendering, naive loop.
        // Two outputs:
        //   - output/gpu_triangle.ppm    (mesh-only scene)
        //   - output/gpu_mesh_scene.ppm  (mixed scene: spheres + mesh)
        //
        // Per the engineering rules: CPU populates Scene structs and
        // calls upload + render. Every per-ray step (aberration,
        // sphere intersection, triangle intersection, shading,
        // Doppler, beaming, framebuffer write) runs on the GPU. The
        // only CPU iteration over pixels is `Image::save_ppm`.
        //
        // `--output` is ignored at this milestone for reproducibility.

        const float aspect = static_cast<float>(cfg.width)
                             / static_cast<float>(cfg.height);

        // A unit quad facing the camera at z = -3 (two CCW triangles).
        // Used by both deliverables - triangle-only on its own; mixed
        // scene against a backdrop of spheres.
        const auto build_quad = []() {
            rr::geometry::Mesh m;
            m.vertices.reserve(4);
            m.triangles.reserve(2);
            m.vertices.push_back({rr::math::Vec3{-0.7f,  0.4f, -3.0f},
                                  rr::math::Vec3{ 0.0f,  0.0f,  1.0f},
                                  rr::math::Vec2{0.0f, 0.0f}});
            m.vertices.push_back({rr::math::Vec3{ 0.7f,  0.4f, -3.0f},
                                  rr::math::Vec3{ 0.0f,  0.0f,  1.0f},
                                  rr::math::Vec2{1.0f, 0.0f}});
            m.vertices.push_back({rr::math::Vec3{ 0.7f,  1.6f, -3.0f},
                                  rr::math::Vec3{ 0.0f,  0.0f,  1.0f},
                                  rr::math::Vec2{1.0f, 1.0f}});
            m.vertices.push_back({rr::math::Vec3{-0.7f,  1.6f, -3.0f},
                                  rr::math::Vec3{ 0.0f,  0.0f,  1.0f},
                                  rr::math::Vec2{0.0f, 1.0f}});
            m.triangles.push_back({0, 1, 2});
            m.triangles.push_back({0, 2, 3});
            m.material_id = 0;
            return m;
        };

        const auto save = [&](const rr::image::Image& img,
                              const std::filesystem::path& out_path) -> bool {
            std::error_code ec;
            if (out_path.has_parent_path()) {
                std::filesystem::create_directories(out_path.parent_path(), ec);
            }
            if (!img.save_ppm(out_path)) {
                Logger::error("saving image failed: " + out_path.string());
                return false;
            }
            Logger::info("saved " + out_path.string());
            return true;
        };

        // --- Output 1: triangle-only ----------------------------------
        {
            rr::scene::Scene scene;
            scene.camera.set_aspect(aspect);

            rr::gpu::GpuScene gpu_scene;
            if (!gpu_scene.upload_from(scene)) {
                Logger::error("scene upload failed (camera/relativity)");
                return 1;
            }
            const auto mesh = build_quad();
            if (!gpu_scene.upload_mesh(mesh)) {
                Logger::error("mesh upload failed (no GPU backend or "
                              "device allocation refused)");
                return 1;
            }
            Logger::info("uploaded triangle-only scene: "
                         + std::to_string(gpu_scene.gpu_mesh().triangle_count())
                         + " triangles");

            auto result = rr::cuda::CudaRenderer::render_scene(
                gpu_scene, cfg.width, cfg.height);
            if (!result.ok) {
                Logger::error("GPU render failed: " + result.message);
                return 1;
            }
            if (!save(result.image, "output/gpu_triangle.ppm")) return 1;
        }

        // --- Output 2: mixed scene (spheres + mesh) -------------------
        {
            rr::scene::Scene scene;
            scene.camera.set_aspect(aspect);

            const auto add_sphere = [&](const rr::math::Vec3& center, float r) {
                rr::scene::SceneSphere s;
                s.geometry.center = center;
                s.geometry.radius = r;
                scene.spheres.push_back(s);
            };
            add_sphere(rr::math::Vec3{ 0.0f,    0.0f, -3.0f}, 1.0f);
            add_sphere(rr::math::Vec3{-1.6f,    0.0f, -3.5f}, 0.6f);
            add_sphere(rr::math::Vec3{ 1.6f,    0.0f, -3.5f}, 0.6f);
            add_sphere(rr::math::Vec3{ 0.0f, -101.0f, -3.0f}, 100.0f);

            rr::gpu::GpuScene gpu_scene;
            if (!gpu_scene.upload_from(scene)) {
                Logger::error("scene upload failed (no GPU backend or "
                              "device allocation refused)");
                return 1;
            }
            const auto mesh = build_quad();
            if (!gpu_scene.upload_mesh(mesh)) {
                Logger::error("mesh upload failed (no GPU backend or "
                              "device allocation refused)");
                return 1;
            }
            Logger::info("uploaded mixed scene: "
                         + std::to_string(gpu_scene.sphere_count())
                         + " spheres, "
                         + std::to_string(gpu_scene.gpu_mesh().triangle_count())
                         + " triangles");

            auto result = rr::cuda::CudaRenderer::render_scene(
                gpu_scene, cfg.width, cfg.height);
            if (!result.ok) {
                Logger::error("GPU render failed: " + result.message);
                return 1;
            }
            if (!save(result.image, "output/gpu_mesh_scene.ppm")) return 1;
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
