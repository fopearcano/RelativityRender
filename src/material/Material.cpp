#include "material/Material.h"

#include <utility>

namespace rr::material {

Material::Material(MaterialParams params)
    : params_(params) {}

Material::Material(std::string name, MaterialParams params)
    : name_(std::move(name)), params_(params) {}

void Material::set_name(std::string name) {
    name_ = std::move(name);
}

void Material::set_params(MaterialParams params) {
    params_ = params;
}

Material Material::make_diffuse(rr::math::Vec3 base_color) {
    MaterialParams p;
    p.baseColor = base_color;
    p.metallic  = 0.0f;
    p.roughness = 1.0f;
    return Material(p);
}

Material Material::make_emissive(rr::math::Vec3 emission_color, float strength) {
    MaterialParams p;
    // Black-body diffuse so the surface is not double-lit when an
    // emissive light source is also reflective; the shader can
    // change this later.
    p.baseColor        = rr::math::Vec3{0.0f, 0.0f, 0.0f};
    p.emissionColor    = emission_color;
    p.emissionStrength = strength;
    return Material(p);
}

Material Material::make_metal(rr::math::Vec3 base_color, float roughness) {
    MaterialParams p;
    p.baseColor = base_color;
    p.metallic  = 1.0f;
    p.roughness = roughness;
    p.specular  = 1.0f;
    return Material(p);
}

}
