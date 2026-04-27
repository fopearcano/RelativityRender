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

#include "gpu/GpuDevice.h"

#include <cstdio>
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

}

int main() {
    test_backend_name_consistency();
    test_enumerate_when_unavailable_is_empty();
    test_device_formatters();

    std::printf("gpu_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
