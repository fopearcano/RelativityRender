#include "gpu/GpuScene.h"

#include "geometry/Mesh.h"
#include "scene/Scene.h"

#include <vector>

namespace rr::gpu {

bool GpuScene::upload_camera(const rr::camera::Camera& camera) {
    camera_     = camera.to_gpu();
    has_camera_ = true;
    return true;
}

bool GpuScene::upload_relativity(const rr::relativity::Observer&         observer,
                                 const rr::relativity::RelativityParams& params) {
    observer_       = observer;
    params_         = params;
    has_relativity_ = true;
    return true;
}

bool GpuScene::upload_mesh(const rr::geometry::Mesh& mesh) {
    return mesh_.upload_from(mesh);
}

bool GpuScene::upload_materials(const rr::material::MaterialParams* host,
                                std::size_t count) {
    if (count == 0) {
        materials_.reset();
        materials_count_ = 0;
        return true;
    }
    if (host == nullptr) {
        return false;
    }
    if (!materials_.upload(host, count)) {
        materials_.reset();
        materials_count_ = 0;
        return false;
    }
    materials_count_ = count;
    return true;
}

bool GpuScene::upload_lights(const rr::lighting::Light* host, std::size_t count) {
    if (count == 0) {
        lights_.reset();
        lights_count_ = 0;
        return true;
    }
    if (host == nullptr) {
        return false;
    }
    if (!lights_.upload(host, count)) {
        lights_.reset();
        lights_count_ = 0;
        return false;
    }
    lights_count_ = count;
    return true;
}

bool GpuScene::upload_spheres(const rr::geometry::Sphere* host, std::size_t count) {
    if (count == 0) {
        spheres_.reset();
        spheres_count_ = 0;
        return true;
    }
    if (host == nullptr) {
        return false;
    }
    if (!spheres_.upload(host, count)) {
        spheres_.reset();
        spheres_count_ = 0;
        return false;
    }
    spheres_count_ = count;
    return true;
}

bool GpuScene::upload_from(const rr::scene::Scene& scene) {
    bool ok = true;
    ok = upload_camera(scene.camera) && ok;
    ok = upload_relativity(scene.observer, scene.relativity) && ok;

    // Flatten visible spheres into a contiguous device-uploadable
    // array. Invisible spheres are dropped here so the kernel never
    // pays for a per-element visibility branch.
    std::vector<rr::geometry::Sphere> flat;
    flat.reserve(scene.spheres.size());
    for (const auto& s : scene.spheres) {
        if (s.object.visible) {
            flat.push_back(s.geometry);
        }
    }
    ok = upload_spheres(flat.data(), flat.size()) && ok;
    return ok;
}

}
