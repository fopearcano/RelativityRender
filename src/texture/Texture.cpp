#include "texture/Texture.h"

namespace rr::texture {

ConstantTexture make_white_texture() {
    return ConstantTexture{rr::math::Vec3{1.0f, 1.0f, 1.0f}};
}

ConstantTexture make_black_texture() {
    return ConstantTexture{rr::math::Vec3{0.0f, 0.0f, 0.0f}};
}

ConstantTexture make_constant_texture(rr::math::Vec3 color) {
    return ConstantTexture{color};
}

}
