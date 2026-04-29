#include "gpu/GpuScene.h"

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

void GpuScene::reset_device() noexcept {
    spheres_.reset();
    sphere_count_ = 0;
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
