#pragma once

#include "math/MathUtils.h"

namespace rr::math {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    RR_HD constexpr Vec3(float xx, float yy, float zz) : x(xx), y(yy), z(zz) {}
    RR_HD constexpr explicit Vec3(float v) : x(v), y(v), z(v) {}

    RR_HD constexpr Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    RR_HD constexpr Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    RR_HD constexpr Vec3 operator-()       const { return {-x, -y, -z}; }
    RR_HD constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    RR_HD constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    // Component-wise (Hadamard) product. Useful for color * color in shading.
    RR_HD constexpr Vec3 operator*(Vec3 b)  const { return {x * b.x, y * b.y, z * b.z}; }

    RR_HD constexpr Vec3& operator+=(Vec3 b)  { x += b.x; y += b.y; z += b.z; return *this; }
    RR_HD constexpr Vec3& operator-=(Vec3 b)  { x -= b.x; y -= b.y; z -= b.z; return *this; }
    RR_HD constexpr Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    RR_HD constexpr Vec3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }

    RR_HD constexpr bool operator==(Vec3 b) const { return x == b.x && y == b.y && z == b.z; }
    RR_HD constexpr bool operator!=(Vec3 b) const { return !(*this == b); }
};

RR_HD constexpr Vec3 operator*(float s, Vec3 v) { return v * s; }

RR_HD constexpr float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

RR_HD constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

RR_HD inline float length_squared(Vec3 v) { return dot(v, v); }
RR_HD inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }

// Returns the zero vector for inputs whose length is non-positive. Avoids
// a division by zero on degenerate inputs without hiding NaNs from real
// numerical bugs upstream.
RR_HD inline Vec3 normalize(Vec3 v) {
    const float len = length(v);
    return len > 0.0f ? v * (1.0f / len) : Vec3{};
}

RR_HD constexpr Vec3 clamp(Vec3 v, Vec3 lo, Vec3 hi) {
    return {
        clamp(v.x, lo.x, hi.x),
        clamp(v.y, lo.y, hi.y),
        clamp(v.z, lo.z, hi.z)
    };
}

RR_HD constexpr Vec3 clamp(Vec3 v, float lo, float hi) {
    return {
        clamp(v.x, lo, hi),
        clamp(v.y, lo, hi),
        clamp(v.z, lo, hi)
    };
}

RR_HD constexpr Vec3 lerp(Vec3 a, Vec3 b, float t) {
    return a + (b - a) * t;
}

}
