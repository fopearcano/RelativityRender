#include "texture/Texture.h"

#include <utility>

namespace rr::texture {

Texture Texture::make_constant(rr::math::Vec3 base_color, std::string name) {
    Texture t;
    t.kind_       = TextureKind::Constant;
    t.base_color_ = base_color;
    t.name_       = std::move(name);
    return t;
}

Texture Texture::make_image(int image_index, std::string name) {
    Texture t;
    t.kind_        = TextureKind::Image;
    t.image_index_ = image_index;
    t.name_        = std::move(name);
    return t;
}

void Texture::set_id(TextureId id) noexcept {
    id_ = id;
}

void Texture::set_name(std::string name) {
    name_ = std::move(name);
}

void Texture::set_base_color(rr::math::Vec3 c) noexcept {
    base_color_ = c;
}

void Texture::set_image_index(int image_index) noexcept {
    image_index_ = image_index;
}

}
