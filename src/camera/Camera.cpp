#include "camera/Camera.h"

#include "math/MathUtils.h"
#include "math/Vec3.h"

#include <cmath>

namespace rr::camera {

namespace {

constexpr float kDefaultVfovDegrees = 45.0f;
constexpr float kDefaultAspect      = 16.0f / 9.0f;
constexpr float kDefaultNear        = 0.1f;
constexpr float kDefaultFar         = 1000.0f;

}

Camera::Camera()
    : position_{0.0f, 0.0f, 0.0f},
      forward_ {0.0f, 0.0f, -1.0f},
      up_      {0.0f, 1.0f,  0.0f},
      right_   {1.0f, 0.0f,  0.0f},
      vfov_deg_(kDefaultVfovDegrees),
      aspect_  (kDefaultAspect),
      near_    (kDefaultNear),
      far_     (kDefaultFar) {}

void Camera::look_at(rr::math::Vec3 eye, rr::math::Vec3 target, rr::math::Vec3 up_hint) {
    using rr::math::length_squared;
    using rr::math::normalize;

    position_ = eye;

    const auto delta = target - eye;
    if (length_squared(delta) <= rr::math::kEpsilon * rr::math::kEpsilon) {
        // Degenerate look_at: keep the existing orientation.
        return;
    }

    forward_ = normalize(delta);
    recompute_basis(up_hint);
}

void Camera::set_vertical_fov_degrees(float deg) {
    // Clamp to a sane open interval. 0 produces a degenerate ray bundle;
    // values >= 180 invert the projection.
    vfov_deg_ = rr::math::clamp(deg, 0.01f, 179.0f);
}

void Camera::set_aspect(float aspect) {
    aspect_ = aspect > 0.0f ? aspect : kDefaultAspect;
}

void Camera::set_clip_range(float near_plane, float far_plane) {
    near_ = near_plane;
    far_  = far_plane;
}

float Camera::vertical_fov_radians() const {
    return rr::math::radians(vfov_deg_);
}

GpuCamera Camera::to_gpu() const {
    GpuCamera g{};
    g.position      = position_;
    g.forward       = forward_;
    g.up            = up_;
    g.right         = right_;
    g.tan_half_vfov = std::tan(0.5f * vertical_fov_radians());
    g.aspect        = aspect_;
    return g;
}

void Camera::recompute_basis(rr::math::Vec3 up_hint) {
    using rr::math::cross;
    using rr::math::length_squared;
    using rr::math::normalize;

    rr::math::Vec3 r = cross(forward_, up_hint);
    if (length_squared(r) <= rr::math::kEpsilon * rr::math::kEpsilon) {
        // up_hint was parallel to forward; fall back to a sensible axis.
        const rr::math::Vec3 fallback = (std::fabs(forward_.y) > 0.9f)
            ? rr::math::Vec3{0.0f, 0.0f, 1.0f}
            : rr::math::Vec3{0.0f, 1.0f, 0.0f};
        r = cross(forward_, fallback);
    }
    right_ = normalize(r);
    up_    = normalize(cross(right_, forward_));
}

}
