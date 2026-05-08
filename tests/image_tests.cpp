// Stage 3 image / framebuffer tests.
//
// Two parts:
//   1. Automated assertions on the Image + Framebuffer surface
//      (resize, clear, set_pixel, get_pixel, PPM save round-trip).
//   2. A debug/manual IO validation step: build a small UV gradient
//      on the host and write it to `output/image_test.ppm` so the
//      developer can visually confirm PPM output is well-formed.
//
// IMPORTANT: the gradient produced here is a CPU-side IO test only.
// It is NOT renderer output; the renderer will write its own pixels
// from a GPU kernel in a later stage. This file exists to verify the
// image library, nothing else.

#include "image/Color.h"
#include "image/Framebuffer.h"
#include "image/Image.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

int g_total  = 0;
int g_failed = 0;

#define RR_CHECK(...)                                                         \
    do {                                                                      \
        ++g_total;                                                            \
        if (!(__VA_ARGS__)) {                                                 \
            ++g_failed;                                                       \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                        \
                         #__VA_ARGS__, __FILE__, __LINE__);                   \
        }                                                                     \
    } while (0)

using rr::image::Framebuffer;
using rr::image::Image;
using rr::image::PixelFormat;
using rr::image::Rgb;
using rr::image::Rgba;

// ---------- Image ----------

void test_image_default_state() {
    Image img;
    RR_CHECK(img.empty());
    RR_CHECK(img.width()  == 0);
    RR_CHECK(img.height() == 0);
    RR_CHECK(img.size_in_floats() == 0u);
}

void test_image_construction_and_channels() {
    Image rgba(4, 3, PixelFormat::Rgba32F);
    RR_CHECK(rgba.width()    == 4);
    RR_CHECK(rgba.height()   == 3);
    RR_CHECK(rgba.channels() == 4);
    RR_CHECK(!rgba.empty());
    RR_CHECK(rgba.size_in_floats() == 4u * 3u * 4u);

    Image rgb(2, 2, PixelFormat::Rgb32F);
    RR_CHECK(rgb.channels() == 3);
    RR_CHECK(rgb.size_in_floats() == 2u * 2u * 3u);
}

void test_image_clear_and_get_pixel() {
    Image img(3, 2, PixelFormat::Rgba32F);

    img.clear(Rgba{0.25f, 0.5f, 0.75f, 1.0f});
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const auto p = img.get_pixel(x, y);
            RR_CHECK(p.r == 0.25f);
            RR_CHECK(p.g == 0.5f);
            RR_CHECK(p.b == 0.75f);
            RR_CHECK(p.a == 1.0f);
        }
    }
}

void test_image_set_pixel() {
    Image img(2, 2, PixelFormat::Rgba32F);

    img.set_pixel(0, 0, Rgba{1.0f, 0.0f, 0.0f, 1.0f});
    img.set_pixel(1, 0, Rgba{0.0f, 1.0f, 0.0f, 1.0f});
    img.set_pixel(0, 1, Rgba{0.0f, 0.0f, 1.0f, 1.0f});
    img.set_pixel(1, 1, Rgba{1.0f, 1.0f, 1.0f, 0.5f});

    RR_CHECK(img.get_pixel(0, 0).r == 1.0f);
    RR_CHECK(img.get_pixel(1, 0).g == 1.0f);
    RR_CHECK(img.get_pixel(0, 1).b == 1.0f);
    RR_CHECK(img.get_pixel(1, 1).a == 0.5f);
}

void test_image_rgb32f_alpha_semantics() {
    // Rgb32F drops alpha on set, reports 1 on get.
    Image rgb(1, 1, PixelFormat::Rgb32F);
    rgb.set_pixel(0, 0, Rgba{0.2f, 0.4f, 0.6f, 0.0f});
    const auto p = rgb.get_pixel(0, 0);
    RR_CHECK(p.r == 0.2f);
    RR_CHECK(p.g == 0.4f);
    RR_CHECK(p.b == 0.6f);
    RR_CHECK(p.a == 1.0f);
}

void test_image_resize_resets_pixels() {
    Image img(2, 2, PixelFormat::Rgba32F);
    img.clear(Rgba{1.0f, 1.0f, 1.0f, 1.0f});

    img.resize(4, 3);
    RR_CHECK(img.width()  == 4);
    RR_CHECK(img.height() == 3);
    RR_CHECK(img.size_in_floats() == 4u * 3u * 4u);

    // After resize, all pixels are zero.
    const auto p = img.get_pixel(0, 0);
    RR_CHECK(p.r == 0.0f);
    RR_CHECK(p.g == 0.0f);
    RR_CHECK(p.b == 0.0f);
    RR_CHECK(p.a == 0.0f);
}

void test_image_save_ppm_header() {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "rr_image_test_header.ppm";

    Image img(3, 2, PixelFormat::Rgba32F);
    img.set_pixel(0, 0, Rgba{1.0f, 0.0f, 0.0f, 1.0f});
    img.set_pixel(1, 0, Rgba{0.0f, 1.0f, 0.0f, 1.0f});
    img.set_pixel(2, 0, Rgba{0.0f, 0.0f, 1.0f, 1.0f});

    RR_CHECK(img.save_ppm(tmp));
    RR_CHECK(fs::exists(tmp));
    // P6 header + one space + dims + newline + "255\n" + 18 bytes pixels.
    RR_CHECK(fs::file_size(tmp) > 0);

    std::ifstream in(tmp, std::ios::binary);
    std::string magic;
    int parsed_w = 0, parsed_h = 0, parsed_max = 0;
    in >> magic >> parsed_w >> parsed_h >> parsed_max;
    RR_CHECK(magic == "P6");
    RR_CHECK(parsed_w == 3);
    RR_CHECK(parsed_h == 2);
    RR_CHECK(parsed_max == 255);

    fs::remove(tmp);
}

void test_empty_image_save_fails() {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "rr_image_test_empty.ppm";
    Image empty;
    RR_CHECK(!empty.save_ppm(tmp));
}

// ---------- Framebuffer ----------

void test_framebuffer_basics() {
    Framebuffer fb(8, 4, PixelFormat::Rgba32F);
    RR_CHECK(fb.width()  == 8);
    RR_CHECK(fb.height() == 4);
    RR_CHECK(fb.format() == PixelFormat::Rgba32F);

    fb.clear(Rgba{0.0f, 0.5f, 1.0f, 1.0f});
    const auto p = fb.color().get_pixel(3, 2);
    RR_CHECK(p.r == 0.0f);
    RR_CHECK(p.g == 0.5f);
    RR_CHECK(p.b == 1.0f);
    RR_CHECK(p.a == 1.0f);

    fb.resize(2, 2);
    RR_CHECK(fb.width()  == 2);
    RR_CHECK(fb.height() == 2);
    // Resize zeroes the buffer (Image::resize semantics).
    const auto z = fb.color().get_pixel(1, 1);
    RR_CHECK(z.r == 0.0f && z.g == 0.0f && z.b == 0.0f && z.a == 0.0f);
}

void test_framebuffer_save_ppm() {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "rr_framebuffer_test.ppm";

    Framebuffer fb(2, 2, PixelFormat::Rgba32F);
    fb.clear(Rgba{1.0f, 0.0f, 0.0f, 1.0f});
    RR_CHECK(fb.save_ppm(tmp));
    RR_CHECK(fs::exists(tmp));
    fs::remove(tmp);
}

// ---------- Manual IO validation: gradient -> output/image_test.ppm ----------

// Builds a UV gradient on the host and writes it to disk.
//
// THIS IS NOT RENDERER OUTPUT. The renderer will produce its own
// pixels from a GPU kernel later. This is purely a debug aid for
// confirming the PPM writer + Image surface works end-to-end.
void save_debug_gradient() {
    constexpr int kW = 64;
    constexpr int kH = 64;

    Image img(kW, kH, PixelFormat::Rgba32F);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const float u = (kW > 1) ? float(x) / float(kW - 1) : 0.0f;
            const float v = (kH > 1) ? float(y) / float(kH - 1) : 0.0f;
            img.set_pixel(x, y, Rgba{u, v, 0.0f, 1.0f});
        }
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories("output", ec);
    if (ec) {
        std::fprintf(stderr,
                     "save_debug_gradient: could not create output/: %s\n",
                     ec.message().c_str());
        ++g_failed;
        ++g_total;
        return;
    }

    const fs::path out_rel = "output/image_test.ppm";
    const fs::path out_abs = fs::absolute(out_rel, ec);

    if (!img.save_ppm(out_rel)) {
        std::fprintf(stderr, "save_debug_gradient: failed to write %s\n",
                     out_rel.string().c_str());
        ++g_failed;
        ++g_total;
        return;
    }

    // Surface the path so the user knows where to look. NOT a renderer
    // output - just IO validation.
    std::printf("image IO validation: wrote %s (%dx%d, RGBA32F gradient)\n",
                out_abs.string().c_str(), kW, kH);
    ++g_total;
}

}  // namespace

int main() {
    // Automated surface coverage.
    test_image_default_state();
    test_image_construction_and_channels();
    test_image_clear_and_get_pixel();
    test_image_set_pixel();
    test_image_rgb32f_alpha_semantics();
    test_image_resize_resets_pixels();
    test_image_save_ppm_header();
    test_empty_image_save_fails();
    test_framebuffer_basics();
    test_framebuffer_save_ppm();

    // Manual debug aid.
    save_debug_gradient();

    std::printf("image_tests: %d / %d passed\n",
                g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
