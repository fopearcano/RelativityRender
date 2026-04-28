#include "gpu/GpuScene.h"

#include "geometry/Mesh.h"
#include "image/Image.h"
#include "lighting/Light.h"
#include "material/MaterialTypes.h"
#include "scene/Scene.h"
#include "texture/ImageTexture.h"

#include <map>
#include <utility>
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

bool GpuScene::upload_material_graphs(const rr::material::MaterialParams* host,
                                      std::size_t count) {
    // Reset to a clean state so a partial failure below
    // never leaves the kernel reading stale buffers.
    graph_ops_.reset();
    graph_terminals_.reset();
    material_graph_views_.reset();
    material_graph_view_count_ = 0;

    if (count == 0) return true;
    if (host == nullptr) return false;

    // Build per-material IRs host-side, concatenate ops +
    // terminals into single host arrays, and remember each
    // material's offset so we can reconstruct the per-material
    // device pointers after the buffers are uploaded.
    std::vector<rr::material::GpuOp>       all_ops;
    std::vector<rr::material::GpuTerminal> all_terminals;
    std::vector<std::size_t>               op_offset(count + 1, 0);
    std::vector<std::size_t>               term_offset(count + 1, 0);

    for (std::size_t i = 0; i < count; ++i) {
        const auto mat = rr::material::synthesise_gpu_material_from_params(
            &host[i]);
        op_offset[i + 1]   = op_offset[i]   + mat.ops.size();
        term_offset[i + 1] = term_offset[i] + mat.terminals.size();
        all_ops.insert(all_ops.end(),
                       mat.ops.begin(),       mat.ops.end());
        all_terminals.insert(all_terminals.end(),
                             mat.terminals.begin(), mat.terminals.end());
    }

    // Upload the ops + terminals first; then build the
    // per-material views with device pointers + counts and
    // upload that array. Empty arrays - all materials with
    // zero ops or zero terminals - skip the empty upload to
    // avoid an empty-buffer rejection from the GPU layer.
    const rr::material::GpuOp*       d_ops_base = nullptr;
    const rr::material::GpuTerminal* d_term_base = nullptr;

    if (!all_ops.empty()) {
        if (!graph_ops_.upload(all_ops.data(), all_ops.size())) {
            graph_ops_.reset();
            return false;
        }
        d_ops_base = graph_ops_.device_ptr();
    }
    if (!all_terminals.empty()) {
        if (!graph_terminals_.upload(all_terminals.data(),
                                     all_terminals.size())) {
            graph_ops_.reset();
            graph_terminals_.reset();
            return false;
        }
        d_term_base = graph_terminals_.device_ptr();
    }

    // Per-material views. Each view's `ops` / `terminals`
    // pointer is `device_base + this_material's_offset`. The
    // count is the difference between consecutive offsets.
    std::vector<rr::cuda::CudaMaterialGraphView> views;
    views.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        rr::cuda::CudaMaterialGraphView v;
        const std::size_t op_n   = op_offset[i + 1]   - op_offset[i];
        const std::size_t term_n = term_offset[i + 1] - term_offset[i];
        v.ops             = (d_ops_base != nullptr  && op_n   > 0)
                            ? d_ops_base  + op_offset[i]  : nullptr;
        v.op_count        = static_cast<std::int32_t>(op_n);
        v.terminals       = (d_term_base != nullptr && term_n > 0)
                            ? d_term_base + term_offset[i] : nullptr;
        v.terminal_count  = static_cast<std::int32_t>(term_n);
        views.push_back(v);
    }

    if (!material_graph_views_.upload(views.data(), views.size())) {
        graph_ops_.reset();
        graph_terminals_.reset();
        material_graph_views_.reset();
        return false;
    }
    material_graph_view_count_ = count;
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

bool GpuScene::upload_textures(const rr::texture::ImageTexture* host,
                               std::size_t count) {
    // Always reset first - either we'll repopulate from `host` or
    // we're explicitly clearing.
    texture_pixels_.clear();
    texture_views_.reset();
    texture_count_ = 0;

    if (count == 0) {
        return true;
    }
    if (host == nullptr) {
        return false;
    }

    texture_pixels_.reserve(count);

    std::vector<rr::cuda::TextureView> views;
    views.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const auto& tex = host[i];
        rr::cuda::TextureView v;

        if (tex.empty()) {
            // No image data -> Constant fallback. Keep an empty
            // GpuBuffer<float> in the parallel slot so the
            // index-by-position invariant holds for the views.
            v.type           = rr::texture::TextureType::Constant;
            v.constant_color = rr::math::Vec3{1.0f, 1.0f, 1.0f};
            texture_pixels_.emplace_back();
            views.push_back(v);
            continue;
        }

        // Upload pixel data into its own device buffer. The buffer
        // outlives the views array (it's owned by `*this`), so the
        // device pointer baked into the view stays valid.
        rr::gpu::GpuBuffer<float> pixels;
        const auto floats = tex.image().size_in_floats();
        if (!pixels.upload(tex.image().data(), floats)) {
            // Clean up any partial state - reuploading from
            // scratch is cheaper than reasoning about half-built
            // arrays.
            texture_pixels_.clear();
            texture_views_.reset();
            texture_count_ = 0;
            return false;
        }

        v.type           = rr::texture::TextureType::Image;
        v.constant_color = rr::math::Vec3{1.0f, 1.0f, 1.0f};  // unused fallback
        v.image_data     = pixels.device_ptr();
        v.image_width    = tex.width();
        v.image_height   = tex.height();
        v.image_channels = tex.image().channels();
        v.wrap_u         = static_cast<int>(tex.wrap_u());
        v.wrap_v         = static_cast<int>(tex.wrap_v());
        v.filter         = static_cast<int>(tex.filter());

        texture_pixels_.push_back(std::move(pixels));
        views.push_back(v);
    }

    if (!texture_views_.upload(views.data(), views.size())) {
        texture_pixels_.clear();
        texture_views_.reset();
        texture_count_ = 0;
        return false;
    }
    texture_count_ = views.size();
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
    // Auto-synthesise + upload the per-material graph IRs
    // alongside. The kernel reads either the flat material
    // (legacy fields like `metallic` / `roughness`) or the
    // graph view (baseColor / emission), depending on its
    // shading needs. Same per-material indexing applies.
    ok = upload_material_graphs(material_pack.data(),
                                material_pack.size()) && ok;

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

    // ---- Textures ----------------------------------------------------
    //
    // Materials reference textures by index into this list. Empty
    // texture list is fine - the kernel falls back to the
    // material's `baseColor` when `base_color_texture_id` is -1
    // or out-of-range.
    ok = upload_textures(scene.textures.data(), scene.textures.size()) && ok;

    return ok;
}

}
