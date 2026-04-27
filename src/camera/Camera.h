#pragma once

#include "camera/CameraRay.h"
#include "math/Vec3.h"

namespace rr::camera {

// Host-side perspective camera. Owns the position / orientation /
// projection parameters. Per the milestone roadmap this is a
// pinhole-only camera; lens, motion blur, and the relativistic
// observer model are added in their own modules and consume the same
// `GpuCamera` POD this class hands out.
//
// The basis is stored explicitly (forward / up / right) and is
// re-orthogonalized whenever the orientation changes. `look_at`
// sets the entire basis from an eye / target / up-hint triple.
//
// Near/far are stored only as metadata at this milestone - they are
// not used for clipping. They are part of the camera surface so later
// passes (depth AOV, frustum culling) can read them from a single
// place.
class Camera {
public:
    Camera();

    // Place the camera at `eye` looking at `target`, with `up_hint`
    // disambiguating roll. The basis is re-orthogonalized; if
    // `eye == target` the orientation is left unchanged.
    void look_at(rr::math::Vec3 eye,
                 rr::math::Vec3 target,
                 rr::math::Vec3 up_hint = rr::math::Vec3{0.0f, 1.0f, 0.0f});

    void set_position(rr::math::Vec3 p) { position_ = p; }

    void set_vertical_fov_degrees(float deg);
    void set_aspect(float aspect);
    void set_clip_range(float near_plane, float far_plane);

    rr::math::Vec3 position() const { return position_; }
    rr::math::Vec3 forward()  const { return forward_; }
    rr::math::Vec3 up()       const { return up_; }
    rr::math::Vec3 right()    const { return right_; }

    float vertical_fov_degrees() const { return vfov_deg_; }
    float vertical_fov_radians() const;
    float aspect()               const { return aspect_; }
    float near_plane()           const { return near_; }
    float far_plane()            const { return far_; }

    // Snapshot the camera into the device-friendly POD. The result is
    // self-contained and may be copied into kernel arguments or
    // device constant memory by the GPU backend.
    GpuCamera to_gpu() const;

private:
    void recompute_basis(rr::math::Vec3 up_hint);

    rr::math::Vec3 position_;
    rr::math::Vec3 forward_;
    rr::math::Vec3 up_;
    rr::math::Vec3 right_;
    float          vfov_deg_;
    float          aspect_;
    float          near_;
    float          far_;
};

}
