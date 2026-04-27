#pragma once

#include "math/MathUtils.h"  // RR_HD

#include <cstdint>

namespace rr::geometry {

// Plain index triple referencing positions / attributes inside a
// `Mesh`. Counter-clockwise winding when viewed from the front face,
// matching the convention used by `intersect_triangle` (added with
// the GPU mesh upload milestone).
//
// Stored as a struct of three `uint32_t` to keep the device-side
// triangle buffer dense (12 bytes / triangle, naturally aligned). A
// flat `uint32_t[3*N]` index array is layout-compatible with this
// struct and is the form most kernels will read.
struct Triangle {
    std::uint32_t v0 = 0;
    std::uint32_t v1 = 0;
    std::uint32_t v2 = 0;
};

// Convenience factory; usable from host and device. Kept out of the
// struct to preserve the aggregate-init form `Triangle{a, b, c}`.
RR_HD inline Triangle make_triangle(std::uint32_t a,
                                    std::uint32_t b,
                                    std::uint32_t c) {
    return Triangle{a, b, c};
}

}
