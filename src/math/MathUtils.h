#pragma once

// Host/device portability shim. When compiled by NVCC, math primitives
// declared `RR_HD` are usable on both host and device. On host-only
// compilers the macro disappears.
#if defined(__CUDACC__)
    #define RR_HD __host__ __device__
#else
    #define RR_HD
#endif

#include <cmath>

namespace rr::math {

inline constexpr float kPi      = 3.14159265358979323846f;
inline constexpr float kTwoPi   = 2.0f * kPi;
inline constexpr float kHalfPi  = 0.5f * kPi;
inline constexpr float kInvPi   = 1.0f / kPi;
inline constexpr float kEpsilon = 1.0e-6f;

template <typename T>
RR_HD constexpr T min(T a, T b) {
    return a < b ? a : b;
}

template <typename T>
RR_HD constexpr T max(T a, T b) {
    return a > b ? a : b;
}

template <typename T>
RR_HD constexpr T clamp(T value, T lo, T hi) {
    return value < lo ? lo : (hi < value ? hi : value);
}

template <typename T>
RR_HD constexpr T lerp(T a, T b, float t) {
    return a + (b - a) * t;
}

RR_HD constexpr float radians(float degrees) {
    return degrees * (kPi / 180.0f);
}

RR_HD constexpr float degrees(float radians) {
    return radians * (180.0f / kPi);
}

RR_HD constexpr float saturate(float v) {
    return clamp(v, 0.0f, 1.0f);
}

}
