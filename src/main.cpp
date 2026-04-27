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
    #include "material/MaterialTypes.h"
    #include "math/Vec2.h"
    #include "math/Vec3.h"
    #include "relativity/RelativityParams.h"
    #include "scene/Scene.h"

    #include <vector>
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
        // M11 deliverable: materials integrated end-to-end. Each
        // primitive references a material by id; the GPU uploads the
        // material array; the kernel reads the material per hit and
        // shades with diffuse baseColor + emission + a simple
        // normal-driven hemisphere term. The existing relativistic
        // pipeline (aberration -> Doppler colour -> searchlight)
        // still wraps the final shaded value, so high beta still
        // produces blueshift / beaming on the per-material output.
        //
        // Output: output/gpu_material_scene.ppm
        //
        // CPU only constructs the scene and uploads it - every per-ray
        // step (intersection, shading, relativistic effects, write)
        // runs on the GPU. The only CPU pixel iteration is in
        // `Image::save_ppm`.
        //
        // `--output` is ignored at this milestone for reproducibility.

        const float aspect = static_cast<float>(cfg.width)
                             / static_cast<float>(cfg.height);

        rr::scene::Scene scene;
        scene.camera.set_aspect(aspect);

        // Material palette. Indices are referenced from the geometry
        // below.  At rest the relativistic pipeline collapses to
        // identity, so the saved image is a classical multi-material
        // render.
        std::vector<rr::material::MaterialParams> materials;
        materials.reserve(5);

        rr::material::MaterialParams red;       // 0
        red.baseColor = rr::math::Vec3{0.9f, 0.15f, 0.15f};
        materials.push_back(red);

        rr::material::MaterialParams green;     // 1
        green.baseColor = rr::math::Vec3{0.15f, 0.85f, 0.25f};
        materials.push_back(green);

        rr::material::MaterialParams blue;      // 2
        blue.baseColor = rr::math::Vec3{0.15f, 0.35f, 0.95f};
        materials.push_back(blue);

        rr::material::MaterialParams emissive;  // 3 - quad emits warm light
        emissive.baseColor        = rr::math::Vec3{0.0f, 0.0f, 0.0f};
        emissive.emissionColor    = rr::math::Vec3{1.0f, 0.85f, 0.4f};
        emissive.emissionStrength = 2.0f;
        materials.push_back(emissive);

        rr::material::MaterialParams floor;     // 4
        floor.baseColor = rr::math::Vec3{0.55f, 0.55f, 0.6f};
        materials.push_back(floor);

        // Spheres - same arrangement as M10, now per-sphere materials.
        const auto add_sphere = [&](const rr::math::Vec3& center, float r,
                                    int material_index) {
            rr::scene::SceneSphere s;
            s.geometry.center         = center;
            s.geometry.radius         = r;
            s.geometry.material_index = material_index;
            scene.spheres.push_back(s);
        };
        add_sphere(rr::math::Vec3{ 0.0f,    0.0f, -3.0f}, 1.0f, /*red*/   0);
        add_sphere(rr::math::Vec3{-1.6f,    0.0f, -3.5f}, 0.6f, /*green*/ 1);
        add_sphere(rr::math::Vec3{ 1.6f,    0.0f, -3.5f}, 0.6f, /*blue*/  2);
        add_sphere(rr::math::Vec3{ 0.0f, -101.0f, -3.0f}, 100.0f, /*floor*/4);

        // Mesh - the M10 quad, now flagged as the emissive material.
        rr::geometry::Mesh quad;
        quad.vertices.reserve(4);
        quad.triangles.reserve(2);
        quad.vertices.push_back({rr::math::Vec3{-0.7f, 0.4f, -3.0f},
                                 rr::math::Vec3{0.0f, 0.0f,  1.0f},
                                 rr::math::Vec2{0.0f, 0.0f}});
        quad.vertices.push_back({rr::math::Vec3{ 0.7f, 0.4f, -3.0f},
                                 rr::math::Vec3{0.0f, 0.0f,  1.0f},
                                 rr::math::Vec2{1.0f, 0.0f}});
        quad.vertices.push_back({rr::math::Vec3{ 0.7f, 1.6f, -3.0f},
                                 rr::math::Vec3{0.0f, 0.0f,  1.0f},
                                 rr::math::Vec2{1.0f, 1.0f}});
        quad.vertices.push_back({rr::math::Vec3{-0.7f, 1.6f, -3.0f},
                                 rr::math::Vec3{0.0f, 0.0f,  1.0f},
                                 rr::math::Vec2{0.0f, 1.0f}});
        quad.triangles.push_back({0, 1, 2});
        quad.triangles.push_back({0, 2, 3});
        quad.material_id = 3;  // emissive

        rr::gpu::GpuScene gpu_scene;
        if (!gpu_scene.upload_from(scene)) {
            Logger::error("scene upload failed (camera/relativity/spheres)");
            return 1;
        }
        if (!gpu_scene.upload_mesh(quad)) {
            Logger::error("mesh upload failed (no GPU backend or "
                          "device allocation refused)");
            return 1;
        }
        if (!gpu_scene.upload_materials(materials.data(), materials.size())) {
            Logger::error("material upload failed (no GPU backend or "
                          "device allocation refused)");
            return 1;
        }
        Logger::info("uploaded material scene: "
                     + std::to_string(gpu_scene.sphere_count())  + " spheres, "
                     + std::to_string(gpu_scene.gpu_mesh().triangle_count())
                                                                 + " triangles, "
                     + std::to_string(gpu_scene.material_count()) + " materials");

        auto result = rr::cuda::CudaRenderer::render_scene(
            gpu_scene, cfg.width, cfg.height);
        if (!result.ok) {
            Logger::error("GPU render failed: " + result.message);
            return 1;
        }

        const std::filesystem::path out_path = "output/gpu_material_scene.ppm";
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
