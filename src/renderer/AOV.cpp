#include "renderer/AOV.h"

#include <utility>

namespace rr::renderer {

int aov_component_count(AOVType type) noexcept {
    switch (type) {
        case AOVType::Beauty:            return 3;
        case AOVType::Normal:            return 3;
        case AOVType::Depth:             return 1;
        case AOVType::Albedo:            return 3;
        case AOVType::DopplerFactor:     return 1;
        case AOVType::SearchlightFactor: return 1;
    }
    // Unknown enumerator. Returning 0 keeps the eventual renderer-
    // integration sub-stage's "size a buffer of N components"
    // arithmetic safe (an N=0 allocation is a no-op success in
    // every existing GpuBuffer / Image path).
    return 0;
}

std::string_view aov_type_name(AOVType type) noexcept {
    switch (type) {
        case AOVType::Beauty:            return "beauty";
        case AOVType::Normal:            return "normal";
        case AOVType::Depth:             return "depth";
        case AOVType::Albedo:            return "albedo";
        case AOVType::DopplerFactor:     return "doppler_factor";
        case AOVType::SearchlightFactor: return "searchlight_factor";
    }
    return "unknown";
}

// Factory implementations. Each is a static member of `AOV` and
// can write to the private `type_` / `name_` fields directly. The
// id stays at `kInvalidAOVId` until the eventual scene-side AOV
// table assigns one via `set_id`.

AOV AOV::make_beauty(std::string name) {
    AOV aov;
    aov.type_ = AOVType::Beauty;
    aov.name_ = name.empty() ? std::string(aov_type_name(AOVType::Beauty))
                             : std::move(name);
    return aov;
}

AOV AOV::make_normal(std::string name) {
    AOV aov;
    aov.type_ = AOVType::Normal;
    aov.name_ = name.empty() ? std::string(aov_type_name(AOVType::Normal))
                             : std::move(name);
    return aov;
}

AOV AOV::make_depth(std::string name) {
    AOV aov;
    aov.type_ = AOVType::Depth;
    aov.name_ = name.empty() ? std::string(aov_type_name(AOVType::Depth))
                             : std::move(name);
    return aov;
}

AOV AOV::make_albedo(std::string name) {
    AOV aov;
    aov.type_ = AOVType::Albedo;
    aov.name_ = name.empty() ? std::string(aov_type_name(AOVType::Albedo))
                             : std::move(name);
    return aov;
}

AOV AOV::make_doppler_factor(std::string name) {
    AOV aov;
    aov.type_ = AOVType::DopplerFactor;
    aov.name_ = name.empty() ? std::string(aov_type_name(AOVType::DopplerFactor))
                             : std::move(name);
    return aov;
}

AOV AOV::make_searchlight_factor(std::string name) {
    AOV aov;
    aov.type_ = AOVType::SearchlightFactor;
    aov.name_ = name.empty() ? std::string(aov_type_name(AOVType::SearchlightFactor))
                             : std::move(name);
    return aov;
}

int AOV::component_count() const noexcept {
    return aov_component_count(type_);
}

void AOV::set_id(AOVId id) noexcept {
    id_ = id;
}

void AOV::set_name(std::string name) {
    name_ = std::move(name);
}

}  // namespace rr::renderer
