// Hand-rolled assertion runner. The real test framework comes with
// the M2 deferred items.
//
// These tests exercise the public surface of `rr::gpu::` against the
// invariants that hold regardless of whether CUDA is compiled in:
//
//   - the backend name is non-empty,
//   - `gpu_backend_available()` and `gpu_backend_name()` agree,
//   - `enumerate_devices()` is empty when no backend is compiled in,
//   - `GpuDevice` formatters produce the expected strings.
//
// When `RR_HAS_CUDA` is defined and a real device is present, the
// device list is non-empty - we don't assert that, since CI machines
// without GPUs are valid environments.

#include "gpu/GpuBuffer.h"
#include "gpu/GpuDevice.h"

#include <cstdio>
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

void test_backend_name_consistency() {
    const auto name      = rr::gpu::gpu_backend_name();
    const bool available = rr::gpu::gpu_backend_available();

    RR_CHECK(!name.empty());
    if (available) {
        RR_CHECK(name != "(none)");
    } else {
        RR_CHECK(name == "(none)");
    }
}

void test_enumerate_when_unavailable_is_empty() {
    if (rr::gpu::gpu_backend_available()) return;  // not applicable here
    RR_CHECK(rr::gpu::enumerate_devices().empty());
}

void test_device_formatters() {
    rr::gpu::GpuDevice d;
    d.index                    = 0;
    d.name                     = "Test Device";
    d.compute_capability_major = 8;
    d.compute_capability_minor = 9;
    d.total_memory_bytes       = 2ull * 1024ull * 1024ull;  // 2 MiB
    d.multiprocessor_count     = 16;

    RR_CHECK(d.compute_capability_string() == "8.9");
    RR_CHECK(d.total_memory_human()        == "2 MiB");
}

// --- GpuBuffer<T> ---------------------------------------------------------

void test_buffer_default_state() {
    rr::gpu::GpuBuffer<float> buf;
    RR_CHECK(buf.empty());
    RR_CHECK(buf.size() == 0);
    RR_CHECK(buf.size_in_bytes() == 0);
    RR_CHECK(buf.device_ptr() == nullptr);
    buf.reset();  // reset on empty must be a no-op
    RR_CHECK(buf.empty());
}

void test_buffer_move_default() {
    rr::gpu::GpuBuffer<int> a;
    rr::gpu::GpuBuffer<int> b(std::move(a));
    RR_CHECK(a.empty());
    RR_CHECK(b.empty());

    rr::gpu::GpuBuffer<int> c;
    c = std::move(b);
    RR_CHECK(b.empty());
    RR_CHECK(c.empty());
}

void test_buffer_no_backend_fails_predictably() {
    if (rr::gpu::gpu_backend_available()) return;  // host-only path only

    rr::gpu::GpuBuffer<float> buf;
    RR_CHECK(!buf.allocate(16));
    RR_CHECK(buf.empty());

    const float src[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    RR_CHECK(!buf.upload(src, 4));
    RR_CHECK(buf.empty());

    float dst[4] = {};
    RR_CHECK(!buf.download(dst, 4));   // download from empty -> only true for count=0
    RR_CHECK(buf.download(dst, 0));    // empty download is a no-op success
}

// Upload a small float array, run no kernel, download into a fresh
// vector, and verify the bytes survive the round trip. This is the
// minimum test the M5/M6 buffer plumbing must support before any
// kernels exist.
void test_buffer_roundtrip_floats_with_real_device() {
    if (!rr::gpu::gpu_backend_available()) {
        std::printf("gpu_tests: skipping CUDA round-trip (no backend compiled)\n");
        return;
    }
    if (rr::gpu::enumerate_devices().empty()) {
        std::printf("gpu_tests: skipping CUDA round-trip (no devices visible)\n");
        return;
    }

    constexpr std::size_t kN = 256;
    std::vector<float> input(kN);
    for (std::size_t i = 0; i < kN; ++i) {
        input[i] = static_cast<float>(i) * 0.5f - 1.25f;
    }

    rr::gpu::GpuBuffer<float> dev;
    RR_CHECK(dev.upload(input.data(), input.size()));
    RR_CHECK(dev.size() == kN);
    RR_CHECK(dev.size_in_bytes() == kN * sizeof(float));
    RR_CHECK(dev.device_ptr() != nullptr);

    std::vector<float> output(kN, -999.0f);
    RR_CHECK(dev.download(output.data(), output.size()));

    bool round_trip_ok = true;
    for (std::size_t i = 0; i < kN; ++i) {
        if (output[i] != input[i]) { round_trip_ok = false; break; }
    }
    RR_CHECK(round_trip_ok);

    // Move ownership and re-download from the new owner.
    rr::gpu::GpuBuffer<float> moved(std::move(dev));
    RR_CHECK(dev.empty());
    RR_CHECK(moved.size() == kN);

    std::vector<float> output2(kN, -999.0f);
    RR_CHECK(moved.download(output2.data(), output2.size()));
    RR_CHECK(output2 == input);
}

}

int main() {
    test_backend_name_consistency();
    test_enumerate_when_unavailable_is_empty();
    test_device_formatters();
    test_buffer_default_state();
    test_buffer_move_default();
    test_buffer_no_backend_fails_predictably();
    test_buffer_roundtrip_floats_with_real_device();

    std::printf("gpu_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
