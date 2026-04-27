#pragma once

#include "math/MathUtils.h"
#include "math/Vec3.h"

namespace rr::math {

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr Vec4() = default;
    RR_HD constexpr Vec4(float xx, float yy, float zz, float ww)
        : x(xx), y(yy), z(zz), w(ww) {}
    RR_HD constexpr explicit Vec4(float v) : x(v), y(v), z(v), w(v) {}
    RR_HD constexpr Vec4(Vec3 xyz, float ww) : x(xyz.x), y(xyz.y), z(xyz.z), w(ww) {}

    RR_HD constexpr Vec4 operator+(Vec4 b) const { return {x + b.x, y + b.y, z + b.z, w + b.w}; }
    RR_HD constexpr Vec4 operator-(Vec4 b) const { return {x - b.x, y - b.y, z - b.z, w - b.w}; }
    RR_HD constexpr Vec4 operator-()       const { return {-x, -y, -z, -w}; }
    RR_HD constexpr Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    RR_HD constexpr Vec4 operator/(float s) const { return {x / s, y / s, z / s, w / s}; }

    RR_HD constexpr bool operator==(Vec4 b) const {
        return x == b.x && y == b.y && z == b.z && w == b.w;
    }
    RR_HD constexpr bool operator!=(Vec4 b) const { return !(*this == b); }

    RR_HD constexpr Vec3 xyz() const { return {x, y, z}; }
};

RR_HD constexpr Vec4 operator*(float s, Vec4 v) { return v * s; }

RR_HD constexpr float dot(Vec4 a, Vec4 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

}
