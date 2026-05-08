// Stage 5 GPU buffer-layer tests.
//
// Validates `GpuBuffer<T>` and the `rr::gpu::detail::gpu_*` byte-level
// backend dispatch:
//
//   - default state (empty buffer, null pointer, size 0)
//   - zero-allocate is a successful no-op
//   - move-only ownership (deleted copy + working move)
//   - the user's "small validation path": upload an array of floats
//     to the device, download it, verify values on the CPU
//   - honest failure when no backend is compiled in (allocate(>0)
//     returns false, buffer stays empty - no nullptr dereference,
//     no crash)
//   - backend availability is consistent with backend name
//
// When CUDA is OFF the buffer dispatch returns nullptr / false from
// every call; when CUDA is ON and a device is visible the round-trip
// runs end-to-end. Both paths are exercised by the same test source.

#include "gpu/GpuBuffer.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuTexture.h"
#include "texture/ImageTexture.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>
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

using rr::gpu::GpuBuffer;

// ---------- Default state ----------

void test_default_state() {
    GpuBuffer<int> buf;
    RR_CHECK(buf.empty());
    RR_CHECK(buf.size() == 0u);
    RR_CHECK(buf.size_in_bytes() == 0u);
    RR_CHECK(buf.device_ptr() == nullptr);

    const GpuBuffer<int>& cref = buf;
    RR_CHECK(cref.device_ptr() == nullptr);
}

void test_zero_allocate_is_no_op() {
    GpuBuffer<float> buf;
    RR_CHECK(buf.allocate(0));
    RR_CHECK(buf.empty());
    RR_CHECK(buf.device_ptr() == nullptr);
}

// ---------- Move-only ----------

void test_move_only_traits() {
    static_assert(!std::is_copy_constructible_v<GpuBuffer<int>>,
                  "GpuBuffer must not be copy-constructible");
    static_assert(!std::is_copy_assignable_v<GpuBuffer<int>>,
                  "GpuBuffer must not be copy-assignable");
    static_assert(std::is_move_constructible_v<GpuBuffer<int>>,
                  "GpuBuffer must be move-constructible");
    static_assert(std::is_move_assignable_v<GpuBuffer<int>>,
                  "GpuBuffer must be move-assignable");
    static_assert(std::is_nothrow_move_constructible_v<GpuBuffer<int>>,
                  "GpuBuffer move ctor must be noexcept");
    static_assert(std::is_nothrow_move_assignable_v<GpuBuffer<int>>,
                  "GpuBuffer move assign must be noexcept");
}

void test_move_construction_and_assignment_empty() {
    GpuBuffer<int> a;
    RR_CHECK(a.allocate(0));

    GpuBuffer<int> b = std::move(a);
    RR_CHECK(a.empty());           // moved-from
    RR_CHECK(b.empty());

    GpuBuffer<int> c;
    c = std::move(b);
    RR_CHECK(c.empty());
}

// ---------- Honest failure when no backend ----------

void test_allocate_either_succeeds_or_fails_cleanly() {
    GpuBuffer<float> buf;
    const bool ok = buf.allocate(64);

    if (!ok) {
        // No backend or no visible device: documented honest failure.
        RR_CHECK(buf.empty());
        RR_CHECK(buf.device_ptr() == nullptr);
        RR_CHECK(buf.size() == 0u);
    } else {
        RR_CHECK(buf.size()         == 64u);
        RR_CHECK(buf.size_in_bytes() == 64u * sizeof(float));
        RR_CHECK(buf.device_ptr() != nullptr);

        // Reset clears the allocation cleanly.
        buf.reset();
        RR_CHECK(buf.empty());
        RR_CHECK(buf.device_ptr() == nullptr);

        // reset() is safe to call repeatedly.
        buf.reset();
        RR_CHECK(buf.empty());
    }
}

// ---------- Validation path: float-array upload / download / verify ----------

void test_float_array_roundtrip() {
    constexpr std::size_t kCount = 8;
    const float src[kCount] = {1.0f, 2.0f, 3.0f, 4.0f, 5.5f, 6.5f, 7.5f, 8.5f};

    GpuBuffer<float> buf;
    if (!buf.allocate(kCount)) {
        // No CUDA backend or no device visible. Validate the
        // documented honest-failure surface and bail; the round-trip
        // itself runs whenever the host has a CUDA-capable GPU.
        RR_CHECK(buf.empty());
        RR_CHECK(!buf.upload(src, kCount));
        std::printf("gpu_tests: float round-trip skipped "
                    "(no CUDA backend / no device).\n");
        return;
    }

    RR_CHECK(buf.upload(src, kCount));
    RR_CHECK(buf.size() == kCount);

    float dst[kCount] = {};
    RR_CHECK(buf.download(dst, kCount));

    for (std::size_t i = 0; i < kCount; ++i) {
        RR_CHECK(dst[i] == src[i]);
    }

    // Re-upload smaller count exercises the resize path.
    constexpr std::size_t kSmaller = 4;
    const float small_src[kSmaller] = {10.0f, 20.0f, 30.0f, 40.0f};
    RR_CHECK(buf.upload(small_src, kSmaller));
    RR_CHECK(buf.size() == kSmaller);

    float small_dst[kSmaller] = {};
    RR_CHECK(buf.download(small_dst, kSmaller));
    for (std::size_t i = 0; i < kSmaller; ++i) {
        RR_CHECK(small_dst[i] == small_src[i]);
    }

    // Asking to download more than the buffer holds must fail
    // without writing past the destination.
    float overflow[8] = {};
    RR_CHECK(!buf.download(overflow, 8));

    std::printf("gpu_tests: float round-trip OK (%zu, then %zu floats).\n",
                kCount, kSmaller);
}

// ---------- Backend consistency ----------

void test_backend_name_matches_availability() {
    const bool        avail = rr::gpu::gpu_backend_available();
    const std::string name  = rr::gpu::gpu_backend_name();

    if (avail) {
        RR_CHECK(name == "CUDA");
    } else {
        RR_CHECK(name == "(none)");
        RR_CHECK(rr::gpu::enumerate_devices().empty());
    }
}

// ---------- Stage 13B.1: GpuTexture ----------

using rr::gpu::GpuTexture;
using rr::texture::ImageTexture;
using rr::texture::ImageTextureFormat;

void test_gpu_texture_default_state() {
    GpuTexture tex;
    RR_CHECK(tex.empty());
    RR_CHECK(!tex.has_data());
    RR_CHECK(tex.width()         == 0);
    RR_CHECK(tex.height()        == 0);
    RR_CHECK(tex.size_in_bytes() == 0u);
    RR_CHECK(tex.device_pixels() == nullptr);
}

void test_gpu_texture_move_only_traits() {
    static_assert(!std::is_copy_constructible_v<GpuTexture>,
                  "GpuTexture must not be copy-constructible");
    static_assert(!std::is_copy_assignable_v<GpuTexture>,
                  "GpuTexture must not be copy-assignable");
    static_assert(std::is_move_constructible_v<GpuTexture>,
                  "GpuTexture must be move-constructible");
    static_assert(std::is_move_assignable_v<GpuTexture>,
                  "GpuTexture must be move-assignable");
    static_assert(std::is_nothrow_move_constructible_v<GpuTexture>,
                  "GpuTexture move ctor must be noexcept");
    static_assert(std::is_nothrow_move_assignable_v<GpuTexture>,
                  "GpuTexture move assign must be noexcept");
}

void test_gpu_texture_empty_upload_is_clear_success() {
    // upload_from(default-constructed ImageTexture): empty source ->
    // successful clear, mirroring GpuMesh::upload_vertices(host, 0).
    GpuTexture tex;
    ImageTexture src;
    RR_CHECK(tex.upload_from(src));
    RR_CHECK(tex.empty());
    RR_CHECK(tex.device_pixels() == nullptr);
}

void test_gpu_texture_size_mismatch_fails_cleanly() {
    // 4x2 Rgba8 = 32 bytes; deliberately pass 16. Must not crash;
    // must leave the texture empty.
    GpuTexture tex;
    std::vector<std::byte> too_small(16, std::byte{0xAB});
    RR_CHECK(!tex.upload(too_small.data(), too_small.size(), 4, 2,
                         ImageTextureFormat::Rgba8));
    RR_CHECK(tex.empty());
    RR_CHECK(tex.device_pixels() == nullptr);
}

void test_gpu_texture_null_with_nonzero_bytes_fails_cleanly() {
    GpuTexture tex;
    RR_CHECK(!tex.upload(nullptr, 16, 2, 2, ImageTextureFormat::Rgba8));
    RR_CHECK(tex.empty());
}

void test_gpu_texture_negative_dims_fail_cleanly() {
    GpuTexture tex;
    std::vector<std::byte> bytes(16, std::byte{0});
    RR_CHECK(!tex.upload(bytes.data(), bytes.size(), -1, 2,
                         ImageTextureFormat::Rgba8));
    RR_CHECK(tex.empty());
}

void test_gpu_texture_upload_either_succeeds_or_fails_cleanly() {
    // 4x4 Rgba8 = 64 bytes filled with a recognisable pattern.
    constexpr int kW = 4;
    constexpr int kH = 4;
    constexpr std::size_t kBytes = kW * kH * 4;
    std::vector<std::byte> pixels(kBytes);
    for (std::size_t i = 0; i < kBytes; ++i) {
        pixels[i] = static_cast<std::byte>(i & 0xFF);
    }

    GpuTexture tex;
    const bool ok = tex.upload(pixels.data(), pixels.size(),
                               kW, kH, ImageTextureFormat::Rgba8);
    if (!ok) {
        // No CUDA backend / no visible device: must leave texture empty.
        RR_CHECK(tex.empty());
        RR_CHECK(tex.device_pixels() == nullptr);
        RR_CHECK(tex.width()  == 0);
        RR_CHECK(tex.height() == 0);
        std::printf("gpu_tests: GpuTexture upload skipped "
                    "(no CUDA backend / no device).\n");
        return;
    }

    RR_CHECK(!tex.empty());
    RR_CHECK(tex.has_data());
    RR_CHECK(tex.width()         == kW);
    RR_CHECK(tex.height()        == kH);
    RR_CHECK(tex.size_in_bytes() == kBytes);
    RR_CHECK(tex.format()        == ImageTextureFormat::Rgba8);
    RR_CHECK(tex.device_pixels() != nullptr);

    // Reset clears the allocation cleanly + zeroes metadata.
    tex.reset();
    RR_CHECK(tex.empty());
    RR_CHECK(tex.width()         == 0);
    RR_CHECK(tex.height()        == 0);
    RR_CHECK(tex.size_in_bytes() == 0u);
    RR_CHECK(tex.device_pixels() == nullptr);

    // Reset is safe to call repeatedly.
    tex.reset();
    RR_CHECK(tex.empty());

    std::printf("gpu_tests: GpuTexture upload OK (%dx%d, %zu bytes).\n",
                kW, kH, kBytes);
}

void test_gpu_texture_upload_from_image_texture_roundtrip() {
    // Same shape as the raw upload but going through upload_from with
    // a populated ImageTexture. Validates the typical caller path.
    constexpr int kW = 2;
    constexpr int kH = 3;
    ImageTexture src(kW, kH, ImageTextureFormat::Rgba32F, "ramp");
    src.pixels().resize(static_cast<std::size_t>(kW) * kH * 16);
    for (std::size_t i = 0; i < src.pixels().size(); ++i) {
        src.pixels()[i] = static_cast<std::byte>((i * 7) & 0xFF);
    }

    GpuTexture tex;
    const bool ok = tex.upload_from(src);
    if (!ok) {
        RR_CHECK(tex.empty());
        return;
    }

    RR_CHECK(tex.width()  == kW);
    RR_CHECK(tex.height() == kH);
    RR_CHECK(tex.format() == ImageTextureFormat::Rgba32F);
    RR_CHECK(tex.size_in_bytes() == src.pixels().size());
}

}  // namespace

int main() {
    test_default_state();
    test_zero_allocate_is_no_op();
    test_move_only_traits();
    test_move_construction_and_assignment_empty();
    test_allocate_either_succeeds_or_fails_cleanly();
    test_float_array_roundtrip();
    test_backend_name_matches_availability();

    test_gpu_texture_default_state();
    test_gpu_texture_move_only_traits();
    test_gpu_texture_empty_upload_is_clear_success();
    test_gpu_texture_size_mismatch_fails_cleanly();
    test_gpu_texture_null_with_nonzero_bytes_fails_cleanly();
    test_gpu_texture_negative_dims_fail_cleanly();
    test_gpu_texture_upload_either_succeeds_or_fails_cleanly();
    test_gpu_texture_upload_from_image_texture_roundtrip();

    std::printf("gpu_tests: %d / %d passed\n",
                g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
