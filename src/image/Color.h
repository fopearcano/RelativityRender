#pragma once

#include "math/MathUtils.h"  // for RR_HD; Color types stay device-friendly.

namespace rr::image {

struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    constexpr Rgb() = default;
    RR_HD constexpr Rgb(float rr, float gg, float bb) : r(rr), g(gg), b(bb) {}
    RR_HD constexpr explicit Rgb(float v) : r(v), g(v), b(v) {}

    RR_HD constexpr bool operator==(Rgb o) const { return r == o.r && g == o.g && b == o.b; }
    RR_HD constexpr bool operator!=(Rgb o) const { return !(*this == o); }
};

struct Rgba {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr Rgba() = default;
    RR_HD constexpr Rgba(float rr, float gg, float bb, float aa)
        : r(rr), g(gg), b(bb), a(aa) {}
    RR_HD constexpr Rgba(Rgb rgb, float aa) : r(rgb.r), g(rgb.g), b(rgb.b), a(aa) {}
    RR_HD constexpr explicit Rgba(Rgb rgb) : r(rgb.r), g(rgb.g), b(rgb.b), a(1.0f) {}

    RR_HD constexpr Rgb rgb() const { return {r, g, b}; }

    RR_HD constexpr bool operator==(Rgba o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    RR_HD constexpr bool operator!=(Rgba o) const { return !(*this == o); }
};

}
