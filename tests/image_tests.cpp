// Hand-rolled assertion runner. A real test framework (Catch2 / doctest)
// is on the M2 deferred list and will replace this plumbing.

#include "image/Color.h"
#include "image/Framebuffer.h"
#include "image/Image.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

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

void test_image_basic_rgba() {
    Image img(4, 3, PixelFormat::Rgba32F);

    RR_CHECK(img.width()    == 4);
    RR_CHECK(img.height()   == 3);
    RR_CHECK(img.channels() == 4);
    RR_CHECK(img.format()   == PixelFormat::Rgba32F);
    RR_CHECK(img.size_in_floats() == 4u * 3u * 4u);
    RR_CHECK(!img.empty());

    img.set_pixel(1, 2, Rgba(0.5f, 0.25f, 0.125f, 0.75f));
    RR_CHECK(img.get_pixel(1, 2) == Rgba(0.5f, 0.25f, 0.125f, 0.75f));

    // Other pixels untouched (still zero, default-initialized).
    RR_CHECK(img.get_pixel(0, 0) == Rgba(0.0f, 0.0f, 0.0f, 0.0f));
}

void test_image_basic_rgb() {
    Image img(2, 2, PixelFormat::Rgb32F);

    RR_CHECK(img.channels() == 3);
    RR_CHECK(img.size_in_floats() == 2u * 2u * 3u);

    // Setting through Rgba drops alpha; reading returns alpha=1.
    img.set_pixel(0, 0, Rgba(0.2f, 0.4f, 0.6f, 0.5f));
    RR_CHECK(img.get_pixel(0, 0) == Rgba(0.2f, 0.4f, 0.6f, 1.0f));
}

void test_image_clear() {
    Image img(3, 2, PixelFormat::Rgba32F);
    img.clear(Rgba(0.1f, 0.2f, 0.3f, 1.0f));
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            RR_CHECK(img.get_pixel(x, y) == Rgba(0.1f, 0.2f, 0.3f, 1.0f));
        }
    }
}

void test_image_resize() {
    Image img(4, 4, PixelFormat::Rgba32F);
    img.clear(Rgba(1, 1, 1, 1));

    img.resize(8, 2);
    RR_CHECK(img.width()  == 8);
    RR_CHECK(img.height() == 2);
    RR_CHECK(img.size_in_floats() == 8u * 2u * 4u);

    // Resize zeroes content.
    RR_CHECK(img.get_pixel(0, 0) == Rgba(0.0f, 0.0f, 0.0f, 0.0f));
    RR_CHECK(img.get_pixel(7, 1) == Rgba(0.0f, 0.0f, 0.0f, 0.0f));
}

void test_framebuffer_clear() {
    Framebuffer fb(3, 2);
    RR_CHECK(fb.width()  == 3);
    RR_CHECK(fb.height() == 2);
    RR_CHECK(fb.format() == PixelFormat::Rgba32F);

    fb.clear(Rgba(0.1f, 0.2f, 0.3f, 1.0f));
    RR_CHECK(fb.color().get_pixel(0, 0) == Rgba(0.1f, 0.2f, 0.3f, 1.0f));
    RR_CHECK(fb.color().get_pixel(2, 1) == Rgba(0.1f, 0.2f, 0.3f, 1.0f));

    fb.resize(5, 4);
    RR_CHECK(fb.width()  == 5);
    RR_CHECK(fb.height() == 4);
    RR_CHECK(fb.color().get_pixel(0, 0) == Rgba(0.0f, 0.0f, 0.0f, 0.0f));
}

// Image IO validation. Generates a gradient on the CPU only to exercise
// PPM writing - this is the one allowed CPU-side pixel generation in
// this module per the M4 rule.
void test_image_io_ppm_gradient() {
    constexpr int kW = 8;
    constexpr int kH = 4;

    Image img(kW, kH, PixelFormat::Rgb32F);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(kW - 1);
            const float v = static_cast<float>(y) / static_cast<float>(kH - 1);
            img.set_pixel(x, y, Rgba(u, v, 0.0f, 1.0f));
        }
    }

    const auto path =
        std::filesystem::temp_directory_path() / "rr_image_test_gradient.ppm";

    RR_CHECK(img.save_ppm(path));
    RR_CHECK(std::filesystem::exists(path));

    // Validate header and payload size.
    std::ifstream in(path, std::ios::binary);
    RR_CHECK(in.is_open());

    std::string magic;
    int         w = 0;
    int         h = 0;
    int         max_val = 0;
    in >> magic >> w >> h >> max_val;
    in.get();  // consume the single whitespace after the header.

    RR_CHECK(magic   == "P6");
    RR_CHECK(w       == kW);
    RR_CHECK(h       == kH);
    RR_CHECK(max_val == 255);

    const std::streampos data_begin = in.tellg();
    in.seekg(0, std::ios::end);
    const std::streampos file_end   = in.tellg();
    const auto           data_bytes = file_end - data_begin;
    RR_CHECK(data_bytes == static_cast<std::streamoff>(kW * kH * 3));

    in.close();
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void test_image_save_empty_returns_false() {
    Image empty;
    const auto path =
        std::filesystem::temp_directory_path() / "rr_image_test_empty.ppm";
    RR_CHECK(!empty.save_ppm(path));
}

}

int main() {
    test_image_basic_rgba();
    test_image_basic_rgb();
    test_image_clear();
    test_image_resize();
    test_framebuffer_clear();
    test_image_io_ppm_gradient();
    test_image_save_empty_returns_false();

    std::printf("image_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
