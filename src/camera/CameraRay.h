#pragma once

#include "math/MathUtils.h"  // RR_HD
#include "math/Vec3.h"

namespace rr::camera {

// A ray emitted from the camera. Used both as the return type of the
// host-side debug helper and as the per-thread state inside CUDA
// kernels, so this whole header must compile on both host and device.
struct CameraRay {
    rr::math::Vec3 origin;
    rr::math::Vec3 direction;
};

// Plain-old-data camera struct uploadable to the GPU. The host
// `rr::camera::Camera` produces this via `Camera::to_gpu()`; kernels
// take it by value as a launch argument or read it from device
// memory. The struct is intentionally minimal - extensions
// (lens / motion blur / relativistic boost) come in their own
// milestones and live alongside this type without modifying it.
struct GpuCamera {
    rr::math::Vec3 position;
    rr::math::Vec3 forward;
    rr::math::Vec3 up;
    rr::math::Vec3 right;
    float          tan_half_vfov;  // pre-computed tan(vfov_radians / 2)
    float          aspect;         // width / height
};

// Generate the primary pinhole ray for pixel (x, y) of a (width x height)
// image. The pixel centre is sampled at (x + 0.5, y + 0.5). Image space
// has +x to the right and +y downward (top-left origin) so row 0 is the
// top of the image, matching `rr::image::Image`'s storage order.
//
// `RR_HD` keeps this callable from device kernels and from host-side
// tests; the same code path runs in both worlds.
RR_HD inline CameraRay generate_camera_ray(const GpuCamera& cam,
                                           int x, int y,
                                           int width, int height) {
    using rr::math::Vec3;
    using rr::math::normalize;

    const float fx = static_cast<float>(x) + 0.5f;
    const float fy = static_cast<float>(y) + 0.5f;
    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);

    // Image-plane offsets in camera space: u in [-aspect*tan, +aspect*tan],
    // v in [-tan, +tan], with v flipped so screen +y is image-up.
    const float u = (2.0f * fx / fw - 1.0f) * cam.aspect * cam.tan_half_vfov;
    const float v = (1.0f - 2.0f * fy / fh) * cam.tan_half_vfov;

    const Vec3 dir = normalize(cam.forward + cam.right * u + cam.up * v);
    return CameraRay{cam.position, dir};
}

}
