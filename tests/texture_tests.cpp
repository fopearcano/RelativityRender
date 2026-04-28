// Hand-rolled assertion runner. The real test framework comes
// with the M2 deferred items.
//
// Texture foundation tests. Covers:
//   - ConstantTexture basics + factories.
//   - ImageTexture default state and sampling correctness against
//     a deterministic 2x2 fixture.
//   - The shared `RR_HD inline` device sampler in
//     `cuda/CudaTexture.cuh` produces the same values for both
//     constant and image cases (host runs the same code the
//     kernel will).

#include "cuda/CudaTexture.cuh"
#include "image/Color.h"
#include "image/Image.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "texture/ImageTexture.h"
#include "texture/Texture.h"

#include <cstdio>

namespace {

int g_total  = 0;
int g_failed = 0;

float abs_f(float a) { return a < 0.0f ? -a : a; }

bool nearly_equal(float a, float b, float eps = 1.0e-5f) {
    const float scale  = 1.0f > abs_f(a) ? 1.0f : abs_f(a);
    const float scale2 = scale > abs_f(b) ? scale : abs_f(b);
    return abs_f(a - b) <= eps * scale2;
}

bool nearly_equal(rr::math::Vec3 a, rr::math::Vec3 b, float eps = 1.0e-5f) {
    return nearly_equal(a.x, b.x, eps)
        && nearly_equal(a.y, b.y, eps)
        && nearly_equal(a.z, b.z, eps);
}

#define RR_CHECK(...)                                                         \
    do {                                                                      \
        ++g_total;                                                            \
        if (!(__VA_ARGS__)) {                                                 \
            ++g_failed;                                                       \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                        \
                         #__VA_ARGS__, __FILE__, __LINE__);                   \
        }                                                                     \
    } while (0)

using rr::cuda::TextureView;
using rr::cuda::sample_texture;
using rr::image::Image;
using rr::image::PixelFormat;
using rr::image::Rgba;
using rr::math::Vec2;
using rr::math::Vec3;
using rr::texture::ConstantTexture;
using rr::texture::ImageTexture;
using rr::texture::TextureType;

// --- ConstantTexture ----------------------------------------------------

void test_constant_default_is_white() {
    ConstantTexture t;
    RR_CHECK(t.color == Vec3(1, 1, 1));
    // Sample at random UV - the constant texture ignores it.
    RR_CHECK(t.sample(Vec2{0.0f, 0.0f}) == Vec3(1, 1, 1));
    RR_CHECK(t.sample(Vec2{0.5f, 0.5f}) == Vec3(1, 1, 1));
    RR_CHECK(t.sample(Vec2{1.5f, -3.0f}) == Vec3(1, 1, 1));
}

void test_constant_factories() {
    RR_CHECK(rr::texture::make_white_texture().color == Vec3(1, 1, 1));
    RR_CHECK(rr::texture::make_black_texture().color == Vec3(0, 0, 0));
    RR_CHECK(rr::texture::make_constant_texture(Vec3{0.2f, 0.7f, 0.9f}).color
                 == Vec3(0.2f, 0.7f, 0.9f));
}

void test_constant_type_tag() {
    RR_CHECK(ConstantTexture::type_tag() == TextureType::Constant);
}

// --- ImageTexture -------------------------------------------------------

void test_image_texture_default_is_empty() {
    ImageTexture t;
    RR_CHECK(t.empty());
    RR_CHECK(t.width()  == 0);
    RR_CHECK(t.height() == 0);
    // Empty samples to black so callers don't have to special-case.
    RR_CHECK(t.sample(Vec2{0.5f, 0.5f}) == Vec3(0, 0, 0));
}

void test_image_texture_type_tag() {
    RR_CHECK(ImageTexture::type_tag() == TextureType::Image);
}

// Build a 2x2 texture image:
//
//   +-------------+-------------+
//   | (0.9, 0.1, 0.1) | (0.1, 0.9, 0.1) |       <-- top    row (image y = 0)
//   +-------------+-------------+
//   | (0.1, 0.1, 0.9) | (0.9, 0.9, 0.1) |       <-- bottom row (image y = 1)
//   +-------------+-------------+
//
// UV (0, 0) maps to bottom-left  -> (0.1, 0.1, 0.9)  (image [0, 1])
// UV (1, 0) maps to bottom-right -> (0.9, 0.9, 0.1)  (image [1, 1])
// UV (0, 1) maps to top-left     -> (0.9, 0.1, 0.1)  (image [0, 0])
// UV (1, 1) maps to top-right    -> (0.1, 0.9, 0.1)  (image [1, 0])
ImageTexture make_2x2_test_texture() {
    Image img(2, 2, PixelFormat::Rgba32F);
    img.set_pixel(0, 0, Rgba(0.9f, 0.1f, 0.1f, 1.0f));   // top-left
    img.set_pixel(1, 0, Rgba(0.1f, 0.9f, 0.1f, 1.0f));   // top-right
    img.set_pixel(0, 1, Rgba(0.1f, 0.1f, 0.9f, 1.0f));   // bottom-left
    img.set_pixel(1, 1, Rgba(0.9f, 0.9f, 0.1f, 1.0f));   // bottom-right
    return ImageTexture{std::move(img)};
}

void test_image_texture_corner_samples() {
    const auto t = make_2x2_test_texture();
    // Sample slightly inside each quadrant centre to avoid the
    // boundary tie-break.
    RR_CHECK(nearly_equal(t.sample(Vec2{0.25f, 0.25f}),  // bottom-left
                          Vec3{0.1f, 0.1f, 0.9f}));
    RR_CHECK(nearly_equal(t.sample(Vec2{0.75f, 0.25f}),  // bottom-right
                          Vec3{0.9f, 0.9f, 0.1f}));
    RR_CHECK(nearly_equal(t.sample(Vec2{0.25f, 0.75f}),  // top-left
                          Vec3{0.9f, 0.1f, 0.1f}));
    RR_CHECK(nearly_equal(t.sample(Vec2{0.75f, 0.75f}),  // top-right
                          Vec3{0.1f, 0.9f, 0.1f}));
}

void test_image_texture_clamp_wraps_out_of_range_uvs() {
    const auto t = make_2x2_test_texture();
    // Both ends clamp to the nearest in-range texel.
    RR_CHECK(nearly_equal(t.sample(Vec2{-0.5f, -0.5f}),  // -> (0.0, 0.0) -> bottom-left
                          Vec3{0.1f, 0.1f, 0.9f}));
    RR_CHECK(nearly_equal(t.sample(Vec2{ 1.5f,  1.5f}),  // -> (1.0, 1.0) -> top-right
                          Vec3{0.1f, 0.9f, 0.1f}));
}

// --- Device-side sampler (host runs the same RR_HD inline code) ---------

TextureView make_constant_view(Vec3 color) {
    TextureView v;
    v.type           = TextureType::Constant;
    v.constant_color = color;
    return v;
}

TextureView make_image_view_from_host(const ImageTexture& host) {
    TextureView v;
    v.type           = TextureType::Image;
    v.constant_color = Vec3{1, 1, 1};       // ignored when image data present
    v.image_data     = host.image().data();
    v.image_width    = host.width();
    v.image_height   = host.height();
    v.image_channels = host.image().channels();
    v.wrap_u         = static_cast<int>(host.wrap_u());
    v.wrap_v         = static_cast<int>(host.wrap_v());
    v.filter         = static_cast<int>(host.filter());
    return v;
}

void test_device_view_constant_returns_color() {
    const auto v = make_constant_view(Vec3{0.4f, 0.6f, 0.8f});
    RR_CHECK(sample_texture(v, Vec2{0.5f, 0.5f}) == Vec3(0.4f, 0.6f, 0.8f));
    RR_CHECK(sample_texture(v, Vec2{1.7f, -2.0f}) == Vec3(0.4f, 0.6f, 0.8f));
}

void test_device_view_falls_back_to_constant_when_image_data_null() {
    TextureView v;
    v.type           = TextureType::Image;     // tagged image but no data
    v.constant_color = Vec3{0.2f, 0.3f, 0.4f};
    v.image_data     = nullptr;
    RR_CHECK(sample_texture(v, Vec2{0.5f, 0.5f}) == Vec3(0.2f, 0.3f, 0.4f));
}

void test_device_view_matches_host_image_texture() {
    const auto host = make_2x2_test_texture();
    const auto view = make_image_view_from_host(host);

    const Vec2 uvs[] = {
        Vec2{0.25f, 0.25f},   // bottom-left
        Vec2{0.75f, 0.25f},   // bottom-right
        Vec2{0.25f, 0.75f},   // top-left
        Vec2{0.75f, 0.75f},   // top-right
        Vec2{-0.5f, -0.5f},   // clamped to bottom-left
        Vec2{ 1.5f,  1.5f},   // clamped to top-right
    };
    for (const auto& uv : uvs) {
        const auto h = host.sample(uv);
        const auto d = sample_texture(view, uv);
        RR_CHECK(nearly_equal(h, d));
    }
}

}

int main() {
    test_constant_default_is_white();
    test_constant_factories();
    test_constant_type_tag();
    test_image_texture_default_is_empty();
    test_image_texture_type_tag();
    test_image_texture_corner_samples();
    test_image_texture_clamp_wraps_out_of_range_uvs();
    test_device_view_constant_returns_color();
    test_device_view_falls_back_to_constant_when_image_data_null();
    test_device_view_matches_host_image_texture();

    std::printf("texture_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
