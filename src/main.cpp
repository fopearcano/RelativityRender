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

#ifdef RR_HAS_CUDA
    #include "camera/Camera.h"
    #include "cuda/CudaRenderer.h"
    #include "geometry/Mesh.h"
    #include "geometry/Sphere.h"
    #include "geometry/Triangle.h"
    #include "gpu/GpuScene.h"
    #include "material/Material.h"
    #include "material/MaterialTypes.h"
    #include "math/Vec3.h"
    #include "relativity/RelativityParams.h"
    #include "scene/Scene.h"

    #include <vector>
#endif

#include "image/Image.h"

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
        return;
    }

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

#ifdef RR_HAS_CUDA
// Create the parent directory of `out_path` (if any), save the image
// there, and log the absolute path on success. Returns true on
// success. Only used by the GPU render dispatches, so it is gated on
// the same RR_HAS_CUDA macro to avoid a `defined but not used`
// warning under host-only builds.
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
#endif  // RR_HAS_CUDA

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
        if (!save_image_or_error(r.image, run.path, label,
                                 cfg.width, cfg.height)) {
            ++failures;
        }
    }

    return failures == 0 ? 0 : 1;
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

}  // namespace

int main(int argc, char** argv) {
    using rr::core::CommandLine;
    using rr::core::Logger;

    const auto result = CommandLine::parse(argc, argv);

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
            Logger::info("render command received");
            return 0;

        case CommandLine::Action::RenderGradient:
            return run_render_gradient(result.config);

        case CommandLine::Action::RenderRays:
            return run_render_camera_rays(result.config);

        case CommandLine::Action::RenderSphere:
            return run_render_sphere(result.config);

        case CommandLine::Action::RenderRelativistic:
            return run_render_relativistic(result.config);

        case CommandLine::Action::RenderScene:
            return run_render_scene(result.config);

        case CommandLine::Action::RenderTriangle:
            return run_render_triangle(result.config);

        case CommandLine::Action::RenderMeshScene:
            return run_render_mesh_scene(result.config);

        case CommandLine::Action::RenderMaterialScene:
            return run_render_material_scene(result.config);

        case CommandLine::Action::Error:
            Logger::error(result.error_message);
            std::cerr << CommandLine::usage(argv[0]);
            return 2;

        case CommandLine::Action::Default:
            Logger::info(std::string(rr::core::kProjectName) + " "
                       + rr::core::kVersionString + " starting up.");
            Logger::info("Stage 8B: GPU material shading. "
                         "Try --device-info, --render-gradient, "
                         "--render-rays, --render-sphere, "
                         "--render-relativistic, --render-scene, "
                         "--render-triangle, --render-mesh-scene, "
                         "or --render-material-scene.");
            return 0;
    }
    return 0;
}
