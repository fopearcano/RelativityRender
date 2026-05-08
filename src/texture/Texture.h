#pragma once

#include "math/Vec3.h"

#include <cstdint>
#include <string>

namespace rr::texture {

// Stable integer handle for a texture entry in a scene-side texture
// table. -1 means "no texture" (the renderer falls back to its
// default - e.g. the parent material's flat baseColor). Textures are
// referenced from materials and other shading data by id, not by
// pointer, because pointer references would force lifetime coupling
// that complicates GPU upload.
using TextureId = std::int32_t;

inline constexpr TextureId kInvalidTextureId = -1;

// Tag for which payload a `Texture` carries.
//
// - Constant: a single RGB colour. No UV lookup, no image data.
//   Wherever a sampler reads the texture it gets the same value.
// - Image:    a reference (by index) to an `ImageTexture` entry
//   in a scene-side image-texture table. The sampler eventually
//   does a UV-keyed lookup into that pixel data; today the
//   reference is a forward declaration only - no sampling.
enum class TextureKind : std::uint32_t {
    Constant = 0,
    Image    = 1,
};

// Host-side texture descriptor. Plain data, copy-friendly.
//
// Stage 13A scope: data model only. There is no `sample(uv)`
// method, no GPU-side `cudaTextureObject_t`, no mipmap build,
// no UV transform. Materials are not yet wired to reference
// textures either; this slice lays the groundwork for the next
// sub-stage to add a texture table to the scene + a
// `texture_id` field on `MaterialParams` without further
// data-shape churn.
//
// Field semantics:
// - `id` is the stable handle used elsewhere (materials, shading
//   data) to refer to this texture. -1 means uninitialised.
// - `kind` selects which of `base_color` / `image_index` carries
//   meaning. The other field is ignored when sampling.
// - `base_color` is the constant payload (Constant kind only).
//   The default is opaque white so a constant-texture-with-no-
//   value-set produces a recognisable albedo.
// - `image_index` is an index into the scene's image-texture
//   table (Image kind only). -1 means "no image bound yet".
// - `name` is for authoring / debugging only; the GPU side
//   never sees it.
class Texture {
public:
    Texture() = default;

    [[nodiscard]] static Texture make_constant(rr::math::Vec3 base_color,
                                               std::string    name = {});
    [[nodiscard]] static Texture make_image(int         image_index,
                                            std::string name = {});

    [[nodiscard]] TextureId           id()          const noexcept { return id_; }
    [[nodiscard]] TextureKind         kind()        const noexcept { return kind_; }
    [[nodiscard]] rr::math::Vec3      base_color()  const noexcept { return base_color_; }
    [[nodiscard]] int                 image_index() const noexcept { return image_index_; }
    [[nodiscard]] const std::string&  name()        const noexcept { return name_; }

    void set_id(TextureId id) noexcept;
    void set_name(std::string name);

    // Constant-kind payload. Setting this does NOT switch `kind` to
    // Constant; the caller is responsible for matching the payload
    // setter to the texture's kind. Use the factory functions when
    // the kind is the source of truth.
    void set_base_color(rr::math::Vec3 c) noexcept;

    // Image-kind payload. Same semantics as `set_base_color`: this
    // does not change `kind`. -1 means "no image bound".
    void set_image_index(int image_index) noexcept;

private:
    TextureId      id_           = kInvalidTextureId;
    TextureKind    kind_         = TextureKind::Constant;
    rr::math::Vec3 base_color_   = rr::math::Vec3{1.0f, 1.0f, 1.0f};
    int            image_index_  = -1;
    std::string    name_;
};

}
