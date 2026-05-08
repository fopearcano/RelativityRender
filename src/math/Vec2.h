#pragma once

#include "math/MathUtils.h"

namespace rr::math {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    RR_HD constexpr Vec2(float xx, float yy) : x(xx), y(yy) {}
    RR_HD constexpr explicit Vec2(float v) : x(v), y(v) {}

    RR_HD constexpr Vec2 operator+(Vec2 b) const { return {x + b.x, y + b.y}; }
    RR_HD constexpr Vec2 operator-(Vec2 b) const { return {x - b.x, y - b.y}; }
    RR_HD constexpr Vec2 operator-()       const { return {-x, -y}; }
    RR_HD constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    RR_HD constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }

    RR_HD constexpr Vec2& operator+=(Vec2 b) { x += b.x; y += b.y; return *this; }
    RR_HD constexpr Vec2& operator-=(Vec2 b) { x -= b.x; y -= b.y; return *this; }
    RR_HD constexpr Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    RR_HD constexpr Vec2& operator/=(float s) { x /= s; y /= s; return *this; }

    RR_HD constexpr bool operator==(Vec2 b) const { return x == b.x && y == b.y; }
    RR_HD constexpr bool operator!=(Vec2 b) const { return !(*this == b); }
};

RR_HD constexpr Vec2 operator*(float s, Vec2 v) { return v * s; }

RR_HD constexpr float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }

RR_HD inline float length_squared(Vec2 v) { return dot(v, v); }
RR_HD inline float length(Vec2 v) { return std::sqrt(dot(v, v)); }

}
