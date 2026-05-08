#pragma once

#include "camera/Camera.h"
#include "camera/CameraRay.h"
#include "geometry/Sphere.h"
#include "gpu/GpuBuffer.h"
#include "gpu/GpuMesh.h"
#include "gpu/GpuTexture.h"
#include "lighting/Light.h"
#include "material/MaterialTypes.h"
#include "relativity/RelativityParams.h"
#include "texture/ImageTexture.h"

#include <cstddef>
#include <vector>

namespace rr::geometry { struct Mesh; }

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
// the device buffer as needed. There is no compile-time max-sphere
// cap.
//
// When `RR_HAS_CUDA` is not defined the camera / relativity uploads
// still succeed (they are pure host snapshots); sphere uploads
// succeed only when `count == 0` because the underlying
// `GpuBuffer<T>::allocate` returns `false` without a backend. This
// matches the rest of the GPU layer's "honest absence" behaviour.
//
// Future stages add mesh / material / light / texture upload paths
// to this class without changing the existing methods' contracts.
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
    [[nodiscard]] bool upload_camera(const rr::camera::Camera& camera);

    // Snapshot observer + relativity params. Always succeeds.
    [[nodiscard]] bool upload_relativity(
        const rr::relativity::Observer&         observer,
        const rr::relativity::RelativityParams& params);

    // Upload `count` spheres from a contiguous host array.
    // Reallocates the device buffer to fit. Returns `false` if the
    // GPU backend is not available or the underlying allocation
    // failed. On failure the sphere count is reset to zero.
    //
    // `host == nullptr` with `count == 0` clears the buffer and is
    // always a success.
    [[nodiscard]] bool upload_spheres(const rr::geometry::Sphere* host,
                                      std::size_t                 count);

    // Upload a single triangle mesh into the scene's mesh slot.
    // Calling with an empty mesh clears the slot. Multi-mesh support
    // is a future slice. Forwards to `GpuMesh::upload_from`, so
    // partial-failure semantics match: vertex/triangle uploads are
    // attempted independently and the metadata (material id,
    // transform) is written even when an upload fails - useful for
    // diagnostics.
    [[nodiscard]] bool upload_mesh(const rr::geometry::Mesh& mesh);

    // Upload `count` `MaterialParams` PODs from a contiguous host
    // array. Sphere `material_index` and Mesh `material_id` index
    // into this array on the device. `count == 0` clears the buffer
    // and is always a success; non-empty uploads require a working
    // GPU backend. On failure the count is reset to zero so the
    // kernel never sees a stale pointer.
    [[nodiscard]] bool upload_materials(const rr::material::MaterialParams* host,
                                        std::size_t                         count);

    // Upload `count` `Light` PODs from a contiguous host array. The
    // kernel iterates the array per hit. Same backend-honest
    // semantics as `upload_materials`.
    [[nodiscard]] bool upload_lights(const rr::lighting::Light* host,
                                     std::size_t                count);

    // Upload `count` `ImageTexture` entries (Stage 13B.3; master
    // order #18). Each entry becomes a separate `GpuTexture` whose
    // device pixel buffer is owned by this scene; the kernel-side
    // view array (one `DeviceTextureView` per upload) is built
    // by the renderer at launch time from the accessors below.
    // `MaterialParams::baseColorTextureId` indexes into this array.
    //
    // `count == 0` (or `host == nullptr` with `count == 0`) clears
    // the texture set and is always a success. Non-empty uploads
    // require a working GPU backend; any per-texture upload
    // failure aborts the whole batch and resets the texture set
    // to empty (no partial state).
    [[nodiscard]] bool upload_textures(const rr::texture::ImageTexture* host,
                                       std::size_t                      count);

    // Free every device allocation owned by this scene. Does NOT
    // touch the host snapshots (camera / observer / params) - call
    // `clear()` for the full reset.
    void reset_device() noexcept;

    // Reset every field to its default-constructed state, including
    // the host snapshots. Frees device memory.
    void clear() noexcept;

    // Accessors used by the renderer + diagnostic / test code. The
    // device pointer is non-owning and remains valid for the
    // lifetime of this `GpuScene` (or until the next
    // `upload_spheres` call).
    [[nodiscard]] const rr::camera::GpuCamera&            gpu_camera() const noexcept { return camera_; }
    [[nodiscard]] const rr::relativity::Observer&         observer()   const noexcept { return observer_; }
    [[nodiscard]] const rr::relativity::RelativityParams& params()     const noexcept { return params_; }

    [[nodiscard]] const rr::geometry::Sphere* device_spheres() const noexcept { return spheres_.device_ptr(); }
    [[nodiscard]] std::size_t                 sphere_count()   const noexcept { return sphere_count_; }

    // The mesh slot owned by this scene. Renderer reads device
    // pointers + counts off this object to populate a
    // `CudaMeshView`.
    [[nodiscard]] const GpuMesh& mesh() const noexcept { return mesh_; }

    // Device pointer + count for the uploaded materials array.
    // Both are nullptr / 0 when no upload has happened.
    [[nodiscard]] const rr::material::MaterialParams* device_materials() const noexcept {
        return materials_.device_ptr();
    }
    [[nodiscard]] std::size_t material_count() const noexcept { return material_count_; }

    // Device pointer + count for the uploaded lights array.
    [[nodiscard]] const rr::lighting::Light* device_lights() const noexcept {
        return lights_.device_ptr();
    }
    [[nodiscard]] std::size_t light_count() const noexcept { return light_count_; }

    // Per-texture accessors for the renderer. Each `GpuTexture`
    // exposes its device pixel pointer + dimensions + format; the
    // renderer (CudaRenderer.cu) walks this array to build a
    // device-side `DeviceTextureView` table the kernel reads via
    // `MaterialParams::baseColorTextureId`. Both accessors return
    // empty / 0 when no texture upload has happened.
    [[nodiscard]] const std::vector<GpuTexture>& textures() const noexcept {
        return textures_;
    }
    [[nodiscard]] std::size_t texture_count() const noexcept {
        return textures_.size();
    }

    [[nodiscard]] bool has_camera()     const noexcept { return has_camera_; }
    [[nodiscard]] bool has_relativity() const noexcept { return has_relativity_; }

private:
    rr::camera::GpuCamera             camera_{};
    rr::relativity::Observer          observer_{};
    rr::relativity::RelativityParams  params_{};

    GpuBuffer<rr::geometry::Sphere>   spheres_{};
    std::size_t                       sphere_count_   = 0;

    GpuMesh                           mesh_{};

    GpuBuffer<rr::material::MaterialParams> materials_{};
    std::size_t                             material_count_ = 0;

    GpuBuffer<rr::lighting::Light>    lights_{};
    std::size_t                       light_count_    = 0;

    // Per-texture device storage. `GpuTexture` is move-only and
    // owns its `GpuBuffer<std::byte>`; storing them in a
    // `std::vector` is fine because the vector only ever moves
    // them. The kernel-side flat `DeviceTextureView` table is
    // NOT held here - the renderer builds it at launch time from
    // these entries' device pointers + metadata, keeping rr_gpu
    // free of CUDA-specific types.
    std::vector<GpuTexture>           textures_{};

    bool                              has_camera_     = false;
    bool                              has_relativity_ = false;
};

}
