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
        // Three cases (TEX-P.5; mirrors `docs/TEXTURE_SYSTEM.md`
        // §2 and the kernel-side gate in `CudaTestKernel.cu` /
        // `OptixPrograms.cu`):
        //
        //   Case 1: useBaseColorTexture == false
        //           -> kernel ignores baseColorTextureId
        //              entirely; flat baseColor is used. If
        //              the id is set anyway (>= 0) emit an
        //              info log so the operator notices the
        //              dangling assignment, but do NOT mutate
        //              state - the artist may intend to
        //              toggle the flag back on later.
        //   Case 2: useBaseColorTexture == true AND
        //           baseColorTextureId in [0, texture_count)
        //           -> kernel will sample. No-op for the
        //              validator.
        //   Case 3: useBaseColorTexture == true AND
        //           baseColorTextureId is out of range
        //           -> emit a warning naming the offending
        //              material + bad id, then clear the flag
        //              so the kernel-side gate falls back to
        //              flat baseColor on every subsequent
        //              frame. Counted as a fixup.
        if (!m.params.useBaseColorTexture) {
            // Case 1.
            if (m.params.baseColorTextureId >= 0) {
                rr::core::Logger::info(
                    std::string("scene: material '") + m.name +
                    "' (id=" + std::to_string(m.id) +
                    ") has baseColorTextureId=" +
                    std::to_string(m.params.baseColorTextureId) +
                    " but useBaseColorTexture is false; the "
                    "texture binding is ignored (kernel uses "
                    "flat baseColor).");
            }
            continue;
        }
        const int id = m.params.baseColorTextureId;
        const bool out_of_range =
            (id < 0) ||
            (static_cast<std::size_t>(id) >= texture_count);
        if (!out_of_range) {
            // Case 2.
            continue;
        }
        // Case 3.
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
