// Hand-rolled assertion runner. A real test framework lands in a
// later slice; for day-1 of relativity-core-v1 the goal is only to
// keep the kept modules covered.

#include "image/Color.h"
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

using rr::image::Image;
using rr::image::PixelFormat;
using rr::image::Rgba;

void test_image_basics() {
    Image img(4, 3, PixelFormat::Rgba32F);
    RR_CHECK(img.width()    == 4);
    RR_CHECK(img.height()   == 3);
    RR_CHECK(img.channels() == 4);
    RR_CHECK(!img.empty());
    RR_CHECK(img.size_in_floats() == 4u * 3u * 4u);

    img.clear(Rgba{0.25f, 0.5f, 0.75f, 1.0f});
    const auto px = img.get_pixel(2, 1);
    RR_CHECK(px.r == 0.25f);
    RR_CHECK(px.g == 0.5f);
    RR_CHECK(px.b == 0.75f);
    RR_CHECK(px.a == 1.0f);

    img.set_pixel(0, 0, Rgba{1.0f, 0.0f, 0.0f, 1.0f});
    const auto p00 = img.get_pixel(0, 0);
    RR_CHECK(p00.r == 1.0f);
    RR_CHECK(p00.g == 0.0f);

    Image empty;
    RR_CHECK(empty.empty());
    RR_CHECK(empty.width()  == 0);
    RR_CHECK(empty.height() == 0);

    Image rgb(2, 2, PixelFormat::Rgb32F);
    RR_CHECK(rgb.channels() == 3);
    rgb.set_pixel(1, 1, Rgba{0.5f, 0.5f, 0.5f, 0.0f});
    const auto pr = rgb.get_pixel(1, 1);
    RR_CHECK(pr.a == 1.0f);  // Rgb32F reports alpha=1 on get
}

void test_save_ppm_roundtrip() {
    namespace fs = std::filesystem;
    const fs::path out = fs::temp_directory_path() / "rr_image_test.ppm";

    Image img(2, 2, PixelFormat::Rgba32F);
    img.set_pixel(0, 0, Rgba{1.0f, 0.0f, 0.0f, 1.0f});
    img.set_pixel(1, 0, Rgba{0.0f, 1.0f, 0.0f, 1.0f});
    img.set_pixel(0, 1, Rgba{0.0f, 0.0f, 1.0f, 1.0f});
    img.set_pixel(1, 1, Rgba{1.0f, 1.0f, 1.0f, 1.0f});

    RR_CHECK(img.save_ppm(out));
    RR_CHECK(fs::exists(out));
    RR_CHECK(fs::file_size(out) > 0);

    std::ifstream in(out, std::ios::binary);
    std::string magic;
    in >> magic;
    RR_CHECK(magic == "P6");

    fs::remove(out);
}

}  // namespace

int main() {
    test_image_basics();
    test_save_ppm_roundtrip();

    std::printf("image_tests: %d / %d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
