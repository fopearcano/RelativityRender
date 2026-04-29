#include "gpu/GpuScene.h"

#include "geometry/Mesh.h"

namespace rr::gpu {

bool GpuScene::upload_camera(const rr::camera::Camera& camera) {
    camera_     = camera.to_gpu();
    has_camera_ = true;
    return true;
}

bool GpuScene::upload_relativity(
        const rr::relativity::Observer&         observer,
        const rr::relativity::RelativityParams& params) {
    observer_       = observer;
    params_         = params;
    has_relativity_ = true;
    return true;
}

bool GpuScene::upload_spheres(const rr::geometry::Sphere* host,
                              std::size_t                 count) {
    if (count == 0) {
        spheres_.reset();
        sphere_count_ = 0;
        return true;
    }
    if (host == nullptr) {
        spheres_.reset();
        sphere_count_ = 0;
        return false;
    }
    if (!spheres_.upload(host, count)) {
        // Honest failure: backend missing or device allocation failed.
        // Drop any partial state so the kernel never sees a stale
        // pointer.
        spheres_.reset();
        sphere_count_ = 0;
        return false;
    }
    sphere_count_ = count;
    return true;
}

bool GpuScene::upload_mesh(const rr::geometry::Mesh& mesh) {
    return mesh_.upload_from(mesh);
}

bool GpuScene::upload_materials(const rr::material::MaterialParams* host,
                                std::size_t                         count) {
    if (count == 0) {
        materials_.reset();
        material_count_ = 0;
        return true;
    }
    if (host == nullptr) {
        materials_.reset();
        material_count_ = 0;
        return false;
    }
    if (!materials_.upload(host, count)) {
        materials_.reset();
        material_count_ = 0;
        return false;
    }
    material_count_ = count;
    return true;
}

void GpuScene::reset_device() noexcept {
    spheres_.reset();
    sphere_count_ = 0;
    // GpuMesh has no reset method that drops device buffers
    // separately from metadata; replacing it with a default-
    // constructed instance is the safest equivalent and avoids
    // partial state.
    mesh_ = GpuMesh{};
    materials_.reset();
    material_count_ = 0;
}

void GpuScene::clear() noexcept {
    reset_device();
    camera_         = rr::camera::GpuCamera{};
    observer_       = rr::relativity::Observer{};
    params_         = rr::relativity::RelativityParams{};
    has_camera_     = false;
    has_relativity_ = false;
}

}
