#include "gpu/GpuScene.h"

#include "geometry/Mesh.h"
#include "lighting/Light.h"
#include "material/MaterialTypes.h"
#include "scene/Scene.h"

#include <map>
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

    // ---- Materials ---------------------------------------------------
    //
    // The host stores materials with a stable spec id (the spec
    // lookup key); the device side wants a flat array indexed
    // 0..N-1. We push materials in `scene.materials` order and
    // build a id->index map for the geometry remap below.
    std::vector<rr::material::MaterialParams> material_pack;
    material_pack.reserve(scene.materials.size());
    std::map<int, int> material_id_to_index;
    for (std::size_t i = 0; i < scene.materials.size(); ++i) {
        material_pack.push_back(scene.materials[i].params);
        if (scene.materials[i].id >= 0) {
            material_id_to_index[scene.materials[i].id] = static_cast<int>(i);
        }
    }
    ok = upload_materials(material_pack.data(), material_pack.size()) && ok;

    const auto remap_material = [&](int spec_id) -> int {
        if (spec_id < 0) return -1;
        const auto it = material_id_to_index.find(spec_id);
        return it == material_id_to_index.end() ? -1 : it->second;
    };

    // ---- Spheres -----------------------------------------------------
    //
    // Flatten visible spheres into a contiguous device-uploadable
    // array. Invisible spheres are dropped here so the kernel never
    // pays for a per-element visibility branch. material_index on
    // each Sphere POD is remapped from spec id to flat array index.
    std::vector<rr::geometry::Sphere> sphere_pack;
    sphere_pack.reserve(scene.spheres.size());
    for (const auto& s : scene.spheres) {
        if (!s.object.visible) continue;
        rr::geometry::Sphere copy = s.geometry;
        copy.material_index = remap_material(s.geometry.material_index);
        sphere_pack.push_back(copy);
    }
    ok = upload_spheres(sphere_pack.data(), sphere_pack.size()) && ok;

    // ---- Lights ------------------------------------------------------
    //
    // Drop invisible lights. The Light POD goes to the device by
    // value via GpuBuffer<Light>.
    std::vector<rr::lighting::Light> light_pack;
    light_pack.reserve(scene.lights.size());
    for (const auto& L : scene.lights) {
        if (!L.object.visible) continue;
        light_pack.push_back(L.data);
    }
    ok = upload_lights(light_pack.data(), light_pack.size()) && ok;

    // ---- Mesh --------------------------------------------------------
    //
    // GpuScene currently has a single mesh slot. Pick the first
    // visible mesh in the scene and remap its material_id from spec
    // id to flat array index. Multi-mesh support is a future GPU
    // upload slice; until then anything past index 0 is dropped
    // (visibly - by intent, not silently).
    bool uploaded_mesh = false;
    for (const auto& mesh : scene.meshes) {
        if (!mesh.object.visible) continue;
        rr::geometry::Mesh copy = mesh.data;
        copy.material_id = remap_material(mesh.data.material_id);
        ok = upload_mesh(copy) && ok;
        uploaded_mesh = true;
        break;
    }
    if (!uploaded_mesh) {
        // Clear any previous mesh - leaving stale data would be a
        // surprising side-effect for callers reusing a GpuScene.
        ok = upload_mesh(rr::geometry::Mesh{}) && ok;
    }

    return ok;
}

}
