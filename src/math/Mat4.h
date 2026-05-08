#pragma once

#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "math/Vec4.h"

namespace rr::math {

// Row-major 4x4 matrix. m[row][col].
//
// Translations live in column 3:
//   [ 1 0 0 tx ]
//   [ 0 1 0 ty ]
//   [ 0 0 1 tz ]
//   [ 0 0 0  1 ]
//
// `transform_point` treats the input as homogeneous w=1 (so translation
// applies). `transform_vector` treats it as w=0 (translation is ignored).
struct Mat4 {
    float m[4][4]{};

    constexpr Mat4() = default;

    static RR_HD constexpr Mat4 identity() {
        Mat4 r{};
        r.m[0][0] = 1.0f;
        r.m[1][1] = 1.0f;
        r.m[2][2] = 1.0f;
        r.m[3][3] = 1.0f;
        return r;
    }

    static RR_HD constexpr Mat4 translation(Vec3 t) {
        Mat4 r = identity();
        r.m[0][3] = t.x;
        r.m[1][3] = t.y;
        r.m[2][3] = t.z;
        return r;
    }

    static RR_HD constexpr Mat4 scale(Vec3 s) {
        Mat4 r{};
        r.m[0][0] = s.x;
        r.m[1][1] = s.y;
        r.m[2][2] = s.z;
        r.m[3][3] = 1.0f;
        return r;
    }
};

RR_HD constexpr Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) {
                s += a.m[i][k] * b.m[k][j];
            }
            r.m[i][j] = s;
        }
    }
    return r;
}

// Apply to a position: includes the translation column.
RR_HD constexpr Vec3 transform_point(const Mat4& m, Vec3 p) {
    const float x = m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3];
    const float y = m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3];
    const float z = m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z + m.m[2][3];
    return {x, y, z};
}

// Apply to a direction: ignores the translation column.
RR_HD constexpr Vec3 transform_vector(const Mat4& m, Vec3 v) {
    const float x = m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z;
    const float y = m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z;
    const float z = m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z;
    return {x, y, z};
}

}
