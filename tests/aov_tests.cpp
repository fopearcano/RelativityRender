// Hand-rolled assertion runner. The real test framework comes
// with the M2 deferred items.
//
// Render-pass / AOV foundation tests. Covers:
//   - AOVKind -> name mapping for the v1 six.
//   - aov_is_color predicate (Beauty / Normal / Albedo are colour;
//     Depth / DopplerFactor / SearchlightFactor are scalar).
//   - AOV constructor sizes the underlying Image to the requested
//     dimensions in Rgba32F, regardless of kind.
//   - save_ppm path: empty AOVs fail; colour AOVs go through
//     `Image::save_ppm` directly; scalar AOVs are normalised at
//     save time so the brightest pixel in the R channel maps to
//     1.0 in the saved 8-bit grayscale PPM.

#include "image/Color.h"
#include "image/Image.h"
#include "renderer/AOV.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
using rr::renderer::AOV;
using rr::renderer::AOVKind;

// --- Naming + colour-vs-scalar predicates --------------------------------

void test_aov_kind_name_covers_v1_set() {
    RR_CHECK(std::strcmp(rr::renderer::aov_kind_name(AOVKind::Beauty),            "beauty")            == 0);
    RR_CHECK(std::strcmp(rr::renderer::aov_kind_name(AOVKind::Normal),            "normal")            == 0);
    RR_CHECK(std::strcmp(rr::renderer::aov_kind_name(AOVKind::Depth),             "depth")             == 0);
    RR_CHECK(std::strcmp(rr::renderer::aov_kind_name(AOVKind::Albedo),            "albedo")            == 0);
    RR_CHECK(std::strcmp(rr::renderer::aov_kind_name(AOVKind::DopplerFactor),     "dopplerFactor")     == 0);
    RR_CHECK(std::strcmp(rr::renderer::aov_kind_name(AOVKind::SearchlightFactor), "searchlightFactor") == 0);
}

void test_aov_kind_count_constant() {
    RR_CHECK(rr::renderer::kAOVCount == 6);
}

void test_aov_is_color_predicate() {
    // Colour AOVs: Beauty / Normal / Albedo carry a Vec3 per pixel.
    RR_CHECK(rr::renderer::aov_is_color(AOVKind::Beauty));
    RR_CHECK(rr::renderer::aov_is_color(AOVKind::Normal));
    RR_CHECK(rr::renderer::aov_is_color(AOVKind::Albedo));

    // Scalar AOVs: pack a single value in the R channel.
    RR_CHECK(!rr::renderer::aov_is_color(AOVKind::Depth));
    RR_CHECK(!rr::renderer::aov_is_color(AOVKind::DopplerFactor));
    RR_CHECK(!rr::renderer::aov_is_color(AOVKind::SearchlightFactor));
}

// --- Constructor sizing --------------------------------------------------

void test_default_aov_is_empty() {
    AOV a;
    RR_CHECK(a.empty());
    RR_CHECK(a.width()  == 0);
    RR_CHECK(a.height() == 0);
    RR_CHECK(a.kind()   == AOVKind::Beauty);
}

void test_aov_constructor_sizes_image_uniformly() {
    for (int i = 0; i < rr::renderer::kAOVCount; ++i) {
        const auto kind = static_cast<AOVKind>(i);
        AOV a(kind, 7, 5);
        RR_CHECK(a.kind()           == kind);
        RR_CHECK(a.width()          == 7);
        RR_CHECK(a.height()         == 5);
        RR_CHECK(!a.empty());
        // Storage is uniformly Rgba32F so the upload / download
        // path is the same for every AOV.
        RR_CHECK(a.image().format() == PixelFormat::Rgba32F);
        RR_CHECK(a.image().channels() == 4);
    }
}

// --- save_ppm ------------------------------------------------------------

std::filesystem::path make_temp_path(const char* stem) {
    auto p = std::filesystem::temp_directory_path()
           / (std::string("rr_aov_test_") + stem + ".ppm");
    std::error_code ec;
    std::filesystem::remove(p, ec);
    return p;
}

bool read_file_bytes(const std::filesystem::path& path, std::vector<unsigned char>& out) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;
    is.seekg(0, std::ios::end);
    const auto size = static_cast<std::streamsize>(is.tellg());
    is.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (size > 0) {
        is.read(reinterpret_cast<char*>(out.data()), size);
    }
    return static_cast<bool>(is);
}

// Returns the byte index in `data` immediately after the third
// whitespace-separated token in the PPM header (P6 width height
// maxval). PPM bodies start at that offset.
std::size_t ppm_pixel_offset(const std::vector<unsigned char>& data) {
    int tokens = 0;
    std::size_t i = 0;
    while (i < data.size() && tokens < 4) {
        // Skip whitespace.
        while (i < data.size()
               && (data[i] == ' ' || data[i] == '\n'
                   || data[i] == '\r' || data[i] == '\t')) ++i;
        // Skip comments (lines starting with '#').
        if (i < data.size() && data[i] == '#') {
            while (i < data.size() && data[i] != '\n') ++i;
            continue;
        }
        // Token body.
        while (i < data.size()
               && data[i] != ' ' && data[i] != '\n'
               && data[i] != '\r' && data[i] != '\t') ++i;
        ++tokens;
        // After the maxval (4th token), one whitespace byte separates
        // header from binary body.
        if (tokens == 4 && i < data.size()) {
            ++i;
            return i;
        }
    }
    return i;
}

void test_save_ppm_fails_on_empty_aov() {
    AOV a;  // default-constructed -> empty
    const auto path = make_temp_path("empty");
    RR_CHECK(!a.save_ppm(path));
    // Nothing should have been written.
    RR_CHECK(!std::filesystem::exists(path));
}

void test_save_ppm_color_aov_writes_image_directly() {
    // A 2x1 colour AOV. Pixel 0 = pure red, pixel 1 = pure green.
    AOV a(AOVKind::Beauty, 2, 1);
    a.image().set_pixel(0, 0, Rgba(1.0f, 0.0f, 0.0f, 1.0f));
    a.image().set_pixel(1, 0, Rgba(0.0f, 1.0f, 0.0f, 1.0f));

    const auto path = make_temp_path("color");
    RR_CHECK(a.save_ppm(path));
    RR_CHECK(std::filesystem::exists(path));

    std::vector<unsigned char> bytes;
    RR_CHECK(read_file_bytes(path, bytes));
    const auto off = ppm_pixel_offset(bytes);
    // 2 pixels * 3 bytes/pixel = 6 body bytes after the header.
    RR_CHECK(bytes.size() >= off + 6);
    if (bytes.size() >= off + 6) {
        // Pixel 0 = red.
        RR_CHECK(bytes[off + 0] == 255);
        RR_CHECK(bytes[off + 1] == 0);
        RR_CHECK(bytes[off + 2] == 0);
        // Pixel 1 = green.
        RR_CHECK(bytes[off + 3] == 0);
        RR_CHECK(bytes[off + 4] == 255);
        RR_CHECK(bytes[off + 5] == 0);
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void test_save_ppm_scalar_aov_normalises_to_grayscale() {
    // 3x1 scalar AOV. R channel values: 0.0, 0.5, 2.0.
    // The brightest pixel (2.0) should normalise to 1.0 / 255 in
    // the saved PPM, the 0.5 pixel to 0.25 / 64, and 0.0 to 0.
    AOV a(AOVKind::Depth, 3, 1);
    a.image().set_pixel(0, 0, Rgba(0.0f, 0.0f, 0.0f, 1.0f));
    a.image().set_pixel(1, 0, Rgba(0.5f, 0.0f, 0.0f, 1.0f));
    a.image().set_pixel(2, 0, Rgba(2.0f, 0.0f, 0.0f, 1.0f));

    const auto path = make_temp_path("scalar");
    RR_CHECK(a.save_ppm(path));
    RR_CHECK(std::filesystem::exists(path));

    std::vector<unsigned char> bytes;
    RR_CHECK(read_file_bytes(path, bytes));
    const auto off = ppm_pixel_offset(bytes);
    RR_CHECK(bytes.size() >= off + 9);
    if (bytes.size() >= off + 9) {
        // Pixel 0 -> 0/2 = 0.0  -> 0
        RR_CHECK(bytes[off + 0] == 0);
        RR_CHECK(bytes[off + 1] == 0);
        RR_CHECK(bytes[off + 2] == 0);
        // Pixel 1 -> 0.5/2 = 0.25 -> round(0.25 * 255) = 64.
        // Image::save_ppm uses int(v * 255 + 0.5) which gives 64.
        RR_CHECK(bytes[off + 3] >= 63 && bytes[off + 3] <= 65);
        RR_CHECK(bytes[off + 4] == bytes[off + 3]);
        RR_CHECK(bytes[off + 5] == bytes[off + 3]);
        // Pixel 2 -> 2/2 = 1.0 -> 255 (the brightest scalar
        // is normalised to white).
        RR_CHECK(bytes[off + 6] == 255);
        RR_CHECK(bytes[off + 7] == 255);
        RR_CHECK(bytes[off + 8] == 255);
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void test_save_ppm_scalar_aov_handles_all_zero_input() {
    // All-zero scalar input: the normaliser must not divide by
    // zero. The whole grayscale image saves as black.
    AOV a(AOVKind::DopplerFactor, 2, 1);
    a.image().set_pixel(0, 0, Rgba(0.0f, 0.0f, 0.0f, 1.0f));
    a.image().set_pixel(1, 0, Rgba(0.0f, 0.0f, 0.0f, 1.0f));

    const auto path = make_temp_path("scalar_zero");
    RR_CHECK(a.save_ppm(path));

    std::vector<unsigned char> bytes;
    RR_CHECK(read_file_bytes(path, bytes));
    const auto off = ppm_pixel_offset(bytes);
    RR_CHECK(bytes.size() >= off + 6);
    if (bytes.size() >= off + 6) {
        for (std::size_t i = 0; i < 6; ++i) {
            RR_CHECK(bytes[off + i] == 0);
        }
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

}

int main() {
    test_aov_kind_name_covers_v1_set();
    test_aov_kind_count_constant();
    test_aov_is_color_predicate();
    test_default_aov_is_empty();
    test_aov_constructor_sizes_image_uniformly();
    test_save_ppm_fails_on_empty_aov();
    test_save_ppm_color_aov_writes_image_directly();
    test_save_ppm_scalar_aov_normalises_to_grayscale();
    test_save_ppm_scalar_aov_handles_all_zero_input();

    std::printf("aov_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
