#include "core/CommandLine.h"
#include "core/Config.h"
#include "core/Logger.h"
#include "core/Version.h"
#include "gpu/GpuDevice.h"

// SceneLoader runs on the host even when CUDA is absent (the
// loader is pure host code), so include it unconditionally.
#include "io/SceneLoader.h"
#include "optix/OptixBackend.h"
#include "scene/Scene.h"

#ifdef RR_HAS_CUDA
    #include "cuda/CudaRenderer.h"
    #include "gpu/GpuScene.h"
    #include "image/Color.h"
    #include "image/Image.h"
    #include "renderer/AOV.h"
    #include "texture/ImageTexture.h"
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

void log_optix_info() {
    rr::core::Logger::info(rr::optix::optix_backend_status_line());
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
        log_optix_info();
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

        // M14 deliverables: two path-trace renders of the same
        // scene at different sample counts. Same upload, same
        // camera + relativity, just bouncing per pixel for spp
        // independent paths. Output paths are fixed for the
        // milestone deliverable so the two PPMs are easy to
        // diff between commits.
        struct PathTracePass { int spp; int max_depth; const char* path; };
        const PathTracePass passes[] = {
            { 1,  4, "output/pathtrace_spp_1.ppm"  },
            { 16, 4, "output/pathtrace_spp_16.ppm" },
        };
        for (const auto& pass : passes) {
            Logger::info("path tracing: spp=" + std::to_string(pass.spp)
                         + " max_depth=" + std::to_string(pass.max_depth));
            auto pt = rr::cuda::CudaRenderer::render_pathtrace(
                gpu_scene, width, height, pass.spp, pass.max_depth,
                /*seed_offset=*/0u);
            if (!pt.ok) {
                Logger::error("path-trace render failed: " + pt.message);
                return 1;
            }
            const std::filesystem::path pt_path = pass.path;
            std::error_code pt_ec;
            if (pt_path.has_parent_path()) {
                std::filesystem::create_directories(pt_path.parent_path(), pt_ec);
            }
            if (!pt.image.save_ppm(pt_path)) {
                Logger::error("saving image failed: " + pt_path.string());
                return 1;
            }
            Logger::info("saved " + pt_path.string());
        }

        // M16 deliverable: textured-material render. Build a
        // procedural checkerboard texture, bind it to the first
        // material's `base_color_texture_id`, upload, render. The
        // existing scene's spheres get spherical UVs from
        // `intersect_sphere`; meshes (e.g. the M11 emissive quad)
        // pick UVs from per-vertex attributes. Output is fixed at
        // `output/gpu_textured_material.ppm` so the deliverable is
        // reproducible across runs.
        {
            rr::scene::Scene textured = scene;

            // 32 x 32 checkerboard. Coarse 4-pixel cells so the
            // pattern is unmistakable in the rendered output.
            constexpr int kSize = 32;
            constexpr int kCell = 4;
            rr::image::Image checker(kSize, kSize,
                                     rr::image::PixelFormat::Rgba32F);
            for (int yy = 0; yy < kSize; ++yy) {
                for (int xx = 0; xx < kSize; ++xx) {
                    const bool dark = (((xx / kCell) ^ (yy / kCell)) & 1) == 0;
                    const auto rgba = dark
                        ? rr::image::Rgba(0.05f, 0.05f, 0.05f, 1.0f)
                        : rr::image::Rgba(0.95f, 0.55f, 0.10f, 1.0f);
                    checker.set_pixel(xx, yy, rgba);
                }
            }

            rr::texture::ImageTexture tex;
            tex.set_image(std::move(checker));
            textured.textures.push_back(std::move(tex));
            const int tex_index = static_cast<int>(textured.textures.size()) - 1;

            // Bind the texture to the first material. The host
            // scene was loaded from a `.rrscene` file, so it has
            // at least one material - guard for the
            // empty-material edge case anyway.
            if (!textured.materials.empty()) {
                textured.materials[0].params.base_color_texture_id = tex_index;
            }

            rr::gpu::GpuScene tex_gpu;
            if (!tex_gpu.upload_from(textured)) {
                Logger::error("textured GPU upload failed (no CUDA "
                              "device or device allocation refused)");
                return 1;
            }
            Logger::info("uploaded textured scene: "
                         + std::to_string(tex_gpu.texture_count()) + " textures");

            auto tex_result = rr::cuda::CudaRenderer::render_scene(
                tex_gpu, width, height);
            if (!tex_result.ok) {
                Logger::error("textured render failed: " + tex_result.message);
                return 1;
            }

            const std::filesystem::path tex_out = "output/gpu_textured_material.ppm";
            std::error_code tex_ec;
            if (tex_out.has_parent_path()) {
                std::filesystem::create_directories(tex_out.parent_path(), tex_ec);
            }
            if (!tex_result.image.save_ppm(tex_out)) {
                Logger::error("saving image failed: " + tex_out.string());
                return 1;
            }
            Logger::info("saved " + tex_out.string());
        }

        // M17 deliverable: render the same scene one more time and
        // capture every AOV slot. The AOV launch reuses the M16
        // shading pipeline, so the beauty AOV matches
        // `output/from_scene.ppm` bit-for-bit; the other slots
        // expose the intermediate quantities (normal, depth,
        // albedo, Doppler factor, searchlight factor). Output paths
        // are fixed for the milestone deliverable so the six PPMs
        // are easy to diff between commits.
        {
            auto aov_result = rr::cuda::CudaRenderer::render_aovs(
                gpu_scene, width, height);
            if (!aov_result.ok) {
                Logger::error("AOV render failed: " + aov_result.message);
                return 1;
            }

            struct AOVOutput { rr::renderer::AOVKind kind; const char* path; };
            const AOVOutput aov_outputs[] = {
                { rr::renderer::AOVKind::Beauty,            "output/aov_beauty.ppm"      },
                { rr::renderer::AOVKind::Normal,            "output/aov_normal.ppm"      },
                { rr::renderer::AOVKind::Depth,             "output/aov_depth.ppm"       },
                { rr::renderer::AOVKind::Albedo,            "output/aov_albedo.ppm"      },
                { rr::renderer::AOVKind::DopplerFactor,     "output/aov_doppler.ppm"     },
                { rr::renderer::AOVKind::SearchlightFactor, "output/aov_searchlight.ppm" },
            };
            for (const auto& out : aov_outputs) {
                const auto& aov = aov_result.aovs[static_cast<int>(out.kind)];
                const std::filesystem::path aov_path = out.path;
                std::error_code aov_ec;
                if (aov_path.has_parent_path()) {
                    std::filesystem::create_directories(aov_path.parent_path(), aov_ec);
                }
                if (!aov.save_ppm(aov_path)) {
                    Logger::error(std::string("saving AOV failed: ")
                                  + rr::renderer::aov_kind_name(out.kind)
                                  + " -> " + aov_path.string());
                    return 1;
                }
                Logger::info(std::string("saved AOV ")
                             + rr::renderer::aov_kind_name(out.kind)
                             + ": " + aov_path.string());
            }
        }
#else
        Logger::info("(no CUDA backend compiled; rebuild with "
                     "-DRR_ENABLE_CUDA=ON to render the loaded scene)");
#endif
        return 0;
    }

    Logger::info("No action requested. Use --help to see options.");
    return 0;
}
