#include "scene/Scene.h"

#include "core/Logger.h"

#include <string>

namespace rr::scene {

void Scene::clear() {
    spheres.clear();
    meshes.clear();
    materials.clear();
    lights.clear();

    camera          = rr::camera::Camera{};
    render_settings = RenderSettings{};
    observer        = rr::relativity::Observer{};
    relativity      = rr::relativity::RelativityParams{};
}

int validate_material_texture_ids(
    std::vector<SceneMaterial>& materials,
    std::size_t                  texture_count) {
    int fixed = 0;
    for (auto& m : materials) {
        if (!m.params.useBaseColorTexture) {
            continue;
        }
        const int id = m.params.baseColorTextureId;
        const bool out_of_range =
            (id < 0) ||
            (static_cast<std::size_t>(id) >= texture_count);
        if (!out_of_range) {
            continue;
        }
        rr::core::Logger::warning(
            std::string("scene: material '") + m.name +
            "' (id=" + std::to_string(m.id) +
            ") has useBaseColorTexture=true but baseColorTextureId=" +
            std::to_string(id) +
            " is out of range [0, " +
            std::to_string(texture_count) +
            "); falling back to flat baseColor.");
        m.params.useBaseColorTexture = false;
        ++fixed;
    }
    return fixed;
}

}
