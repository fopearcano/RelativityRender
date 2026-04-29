#pragma once

#include "material/MaterialTypes.h"
#include "math/Vec3.h"

#include <string>

namespace rr::material {

// Host-side material wrapper.
//
// Carries an optional name (for authoring / debugging) and the
// device-friendly `MaterialParams` parameter pack. This is the
// Stage 8 foundation - the BSDF interface (`eval` / `sample` /
// `pdf`), parameter validation, and texture / node-graph wiring
// land in their own master-order modules (path tracer at module
// 16, texture at 18, node graph at 23). `Material` is the first
// scaffold the scene file format (module 15) and the future node
// editor will populate.
class Material {
public:
    Material() = default;
    explicit Material(MaterialParams params);
    Material(std::string name, MaterialParams params);

    // Read access to the data. The mutable `params()` overload lets
    // authoring code tweak fields in place without an extra copy.
    [[nodiscard]] const std::string&    name()   const noexcept { return name_; }
    [[nodiscard]] const MaterialParams& params() const noexcept { return params_; }
    [[nodiscard]] MaterialParams&       params()       noexcept { return params_; }

    void set_name(std::string name);
    void set_params(MaterialParams params);

    // Convenience presets. The renderer has sensible material values
    // even before the scene file format lands. Each preset returns a
    // `Material` with no name; callers can `set_name` after.
    [[nodiscard]] static Material make_diffuse(rr::math::Vec3 base_color);
    [[nodiscard]] static Material make_emissive(rr::math::Vec3 emission_color,
                                                float strength);
    [[nodiscard]] static Material make_metal(rr::math::Vec3 base_color,
                                             float roughness);

private:
    std::string    name_;
    MaterialParams params_;
};

}
