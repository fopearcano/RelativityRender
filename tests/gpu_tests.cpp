// GPU foundation tests. Day-1 covers only GpuBuffer<T> + GpuDevice;
// scene / mesh / camera-related GPU tests come back in their own
// slices.

#include "gpu/GpuBuffer.h"
#include "gpu/GpuDevice.h"

#include <cstdio>
#include <cstdint>
#include <string>
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

void test_buffer_default_state() {
    GpuBuffer<int> buf;
    RR_CHECK(buf.empty());
    RR_CHECK(buf.size() == 0u);
    RR_CHECK(buf.size_in_bytes() == 0u);
    RR_CHECK(buf.device_ptr() == nullptr);
}

void test_buffer_zero_alloc_is_ok() {
    GpuBuffer<float> buf;
    RR_CHECK(buf.allocate(0));
    RR_CHECK(buf.empty());
}

void test_buffer_allocate_no_backend_fails_honestly() {
    // When CUDA is not compiled in, gpu_alloc returns nullptr and
    // allocate(>0) returns false. When CUDA IS compiled in but no
    // device is visible the same path also reports failure honestly.
    // Either way the buffer stays in a clean post-failure state.
    GpuBuffer<float> buf;
    const bool ok = buf.allocate(64);
    if (!ok) {
        RR_CHECK(buf.empty());
        RR_CHECK(buf.device_ptr() == nullptr);
    } else {
        RR_CHECK(buf.size() == 64u);
        RR_CHECK(buf.device_ptr() != nullptr);
        buf.reset();
        RR_CHECK(buf.empty());
    }
}

void test_buffer_move_only() {
    static_assert(!std::is_copy_constructible_v<GpuBuffer<int>>);
    static_assert(!std::is_copy_assignable_v<GpuBuffer<int>>);
    static_assert(std::is_move_constructible_v<GpuBuffer<int>>);
    static_assert(std::is_move_assignable_v<GpuBuffer<int>>);

    GpuBuffer<int> a;
    (void)a.allocate(0);
    GpuBuffer<int> b = std::move(a);
    RR_CHECK(b.empty());

    GpuBuffer<int> c;
    c = std::move(b);
    RR_CHECK(c.empty());
}

void test_buffer_upload_download_roundtrip_when_backend_works() {
    GpuBuffer<int> buf;
    if (!buf.allocate(4)) {
        // No backend available - skip the round-trip silently. The
        // honest-failure path is exercised by test_buffer_allocate_*.
        return;
    }
    const int src[4] = {1, 2, 3, 4};
    RR_CHECK(buf.upload(src, 4));
    int dst[4] = {0, 0, 0, 0};
    RR_CHECK(buf.download(dst, 4));
    RR_CHECK(dst[0] == 1);
    RR_CHECK(dst[1] == 2);
    RR_CHECK(dst[2] == 3);
    RR_CHECK(dst[3] == 4);
}

void test_device_query_is_consistent_with_backend_flag() {
    const bool        avail   = rr::gpu::gpu_backend_available();
    const std::string name    = rr::gpu::gpu_backend_name();
    const auto        devices = rr::gpu::enumerate_devices();

    if (avail) {
        // CUDA compiled in. Backend name is "CUDA". Device list may be
        // empty (no GPU on this host) but the call must succeed.
        RR_CHECK(name == "CUDA");
    } else {
        RR_CHECK(name == "(none)");
        RR_CHECK(devices.empty());
    }

    for (const auto& d : devices) {
        RR_CHECK(d.index >= 0);
        RR_CHECK(!d.name.empty());
        RR_CHECK(d.compute_capability_major >= 0);
    }
}

}  // namespace

int main() {
    test_buffer_default_state();
    test_buffer_zero_alloc_is_ok();
    test_buffer_allocate_no_backend_fails_honestly();
    test_buffer_move_only();
    test_buffer_upload_download_roundtrip_when_backend_works();
    test_device_query_is_consistent_with_backend_flag();

    std::printf("gpu_tests: %d / %d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
