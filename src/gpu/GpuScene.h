#pragma once

#include "camera/Camera.h"
#include "camera/CameraRay.h"
#include "geometry/Sphere.h"
#include "gpu/GpuBuffer.h"
#include "gpu/GpuMesh.h"
#include "relativity/RelativityParams.h"

#include <cstddef>

namespace rr::geometry { struct Mesh; }
namespace rr::scene    { struct Scene; }

namespace rr::gpu {

// Backend-agnostic GPU scene container.
//
// Holds the small per-frame state every kernel reads (a `GpuCamera`
// POD, observer state, relativity knobs) plus the device-side array
// of spheres. Camera / observer / params are host PODs that the
// kernel takes by value as launch arguments; the sphere array is
// owned here by `GpuBuffer<Sphere>` and passed to the kernel as a
// device pointer + count.
//
// Allocation is dynamic: `upload_spheres(host, count)` reallocates
// the device buffer as needed. There is no compile-time max-spheres
// cap.
//
// When `RR_HAS_CUDA` is not defined the camera / observer / params
// uploads still succeed (they are pure host snapshots); sphere
// uploads succeed only when `count == 0` because the underlying
// `GpuBuffer<T>::allocate` returns false without a backend. This
// matches the rest of the GPU layer's "honest absence" behaviour.
class GpuScene {
public:
    GpuScene() = default;
    ~GpuScene() = default;

    GpuScene(const GpuScene&)            = delete;
    GpuScene& operator=(const GpuScene&) = delete;
    GpuScene(GpuScene&&) noexcept            = default;
    GpuScene& operator=(GpuScene&&) noexcept = default;

    // Snapshot the camera into the device-friendly POD. Always
    // succeeds; the resulting state is read directly from
    // `gpu_camera()` by the renderer.
    bool upload_camera(const rr::camera::Camera& camera);

    // Snapshot observer + relativity params. Always succeeds.
    bool upload_relativity(const rr::relativity::Observer&         observer,
                           const rr::relativity::RelativityParams& params);

    // Upload `count` spheres from a contiguous host array.
    // Reallocates the device buffer to fit. Returns false if the
    // GPU backend is not available (or the underlying allocation
    // failed). On failure the sphere count is reset to zero.
    bool upload_spheres(const rr::geometry::Sphere* host, std::size_t count);

    // Upload a single mesh's vertex / triangle / metadata into the
    // scene's mesh slot. Calling this with an empty mesh clears the
    // slot. Multi-mesh support is the next M11 slice.
    bool upload_mesh(const rr::geometry::Mesh& mesh);

    // Convenience: pull camera + relativity + visible spheres from a
    // host `rr::scene::Scene`. Invisible spheres are filtered out on
    // the host before upload. Returns the AND of every individual
    // upload step. Mesh upload is deliberately not part of
    // `upload_from` until the scene-side mesh wrappers carry real
    // data; today they are placeholders.
    bool upload_from(const rr::scene::Scene& scene);

    // --- Queries -----------------------------------------------------

    bool        has_camera()      const noexcept { return has_camera_; }
    bool        has_relativity()  const noexcept { return has_relativity_; }
    std::size_t sphere_count()    const noexcept { return spheres_count_; }
    bool        has_mesh()        const noexcept { return mesh_.has_data(); }

    // --- Backend accessors used by the CUDA renderer -----------------

    const rr::camera::GpuCamera&            gpu_camera()  const noexcept { return camera_; }
    const rr::relativity::Observer&         observer()    const noexcept { return observer_; }
    const rr::relativity::RelativityParams& relativity()  const noexcept { return params_; }

    // Device pointer to the sphere array. Returns nullptr when no
    // spheres have been successfully uploaded.
    const rr::geometry::Sphere* device_spheres() const noexcept {
        return spheres_.device_ptr();
    }

    const GpuMesh& gpu_mesh() const noexcept { return mesh_; }

private:
    rr::camera::GpuCamera             camera_{};
    rr::relativity::Observer          observer_{};
    rr::relativity::RelativityParams  params_{};
    bool                              has_camera_     = false;
    bool                              has_relativity_ = false;

    rr::gpu::GpuBuffer<rr::geometry::Sphere> spheres_;
    std::size_t                              spheres_count_ = 0;

    GpuMesh                                  mesh_;
};

}
