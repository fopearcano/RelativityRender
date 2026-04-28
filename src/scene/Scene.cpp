#include "scene/Scene.h"

namespace rr::scene {

void Scene::clear() {
    spheres.clear();
    meshes.clear();
    materials.clear();
    lights.clear();
    textures.clear();

    camera          = rr::camera::Camera{};
    render_settings = RenderSettings{};
    observer        = rr::relativity::Observer{};
    relativity      = rr::relativity::RelativityParams{};
}

}
