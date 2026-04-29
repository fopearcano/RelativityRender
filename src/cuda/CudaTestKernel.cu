// CUDA diagnostic kernels.
//
// Future kernels (path tracing, AOVs, ...) join this file - or
// sibling .cu files - in their own dedicated stages. Today the
// device runs:
//   - k_gradient_rgba32f       (Stage 6)
//   - k_camera_rays_visualize  (Stage 7)
//   - k_sphere_visualize       (Stage 8)
//   - k_sphere_relativistic    (Stage 10)

#include "cuda/CudaIntersection.cuh"
#include "cuda/CudaKernels.cuh"
#include "relativity/RelativityMath.cuh"

#include "math/Vec3.h"

namespace rr::cuda {

// ---------- Stage 6: UV gradient ----------

namespace {

// One thread = one pixel. Output is the Rgba32F layout used by
// rr::image::Image (interleaved, top-left origin, row stride
// width * 4 floats). The gradient is computed entirely on the
// device - no CPU pixel loop touches this data.
__global__ void k_gradient_rgba32f(float* pixels, int width, int height) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const float u = (width  > 1) ? float(x) / float(width  - 1) : 0.0f;
    const float v = (height > 1) ? float(y) / float(height - 1) : 0.0f;

    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = u;
    pixels[idx + 1] = v;
    pixels[idx + 2] = 0.0f;
    pixels[idx + 3] = 1.0f;
}

}  // namespace

void launch_gradient_rgba32f(float* device_pixels, int width, int height,
                             cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_gradient_rgba32f<<<grid, block, 0, stream>>>(device_pixels, width, height);
}

// ---------- Stage 7: Camera-ray visualisation ----------

namespace {

// One thread = one pixel. The GPU generates the primary pinhole ray
// via the same `RR_HD generate_camera_ray` helper that host tests
// run, then encodes the (already-normalised) ray direction as RGB by
// mapping each component from [-1, 1] to [0, 1]. Alpha is 1. The
// kernel does all per-pixel work; the host never touches per-ray
// state.
__global__ void k_camera_rays_visualize(float* pixels, int width, int height,
                                        rr::camera::GpuCamera cam) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const auto ray = rr::camera::generate_camera_ray(cam, x, y, width, height);

    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = 0.5f * ray.direction.x + 0.5f;
    pixels[idx + 1] = 0.5f * ray.direction.y + 0.5f;
    pixels[idx + 2] = 0.5f * ray.direction.z + 0.5f;
    pixels[idx + 3] = 1.0f;
}

}  // namespace

void launch_camera_rays_visualize(float* device_pixels, int width, int height,
                                  rr::camera::GpuCamera cam,
                                  cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_camera_rays_visualize<<<grid, block, 0, stream>>>(device_pixels,
                                                         width, height, cam);
}

// ---------- Stage 8: Single-sphere intersection ----------

namespace {

// One thread = one pixel. The GPU generates the primary pinhole ray,
// runs `intersect_sphere` against the supplied `sphere` POD, and
// writes the framebuffer.
//
// Hit:  shade with `0.5*n + 0.5` (the canonical "normal as color"
//       diagnostic; reveals geometry without committing to a material
//       system).
// Miss: a simple vertical sky gradient parameterised by the ray's
//       y-direction. Lerp from white at the horizon (y=0) to a soft
//       blue at zenith (y=1), so the image does not collapse to a
//       single colour outside the sphere.
//
// The host never touches per-pixel state.
__global__ void k_sphere_visualize(float* pixels, int width, int height,
                                   rr::camera::GpuCamera cam,
                                   rr::geometry::Sphere  sphere) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const auto ray = rr::camera::generate_camera_ray(cam, x, y, width, height);
    const auto hit = rr::cuda::intersect_sphere(ray, sphere,
                                                /*t_min=*/0.0f,
                                                /*t_max=*/1.0e30f);

    float r, g, b;
    if (hit.hit) {
        // Normal-as-color shade. Each component is in [-1, 1] -> map
        // to [0, 1].
        r = 0.5f * hit.normal.x + 0.5f;
        g = 0.5f * hit.normal.y + 0.5f;
        b = 0.5f * hit.normal.z + 0.5f;
    } else {
        // Sky gradient: white at horizon, soft blue overhead. `t` is
        // remapped from [-1, 1] to [0, 1] so it works regardless of
        // ray normalisation by the camera.
        const float t = 0.5f * (ray.direction.y + 1.0f);
        r = (1.0f - t) * 1.0f + t * 0.5f;
        g = (1.0f - t) * 1.0f + t * 0.7f;
        b = (1.0f - t) * 1.0f + t * 1.0f;
    }

    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = r;
    pixels[idx + 1] = g;
    pixels[idx + 2] = b;
    pixels[idx + 3] = 1.0f;
}

}  // namespace

void launch_sphere_visualize(float* device_pixels, int width, int height,
                             rr::camera::GpuCamera   cam,
                             rr::geometry::Sphere    sphere,
                             cudaStream_t            stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_sphere_visualize<<<grid, block, 0, stream>>>(device_pixels,
                                                    width, height,
                                                    cam, sphere);
}

// ---------- Stage 10: Relativistic single-sphere render ----------

namespace {

// One thread = one pixel. Runs the full relativistic perception
// pipeline on the device:
//   1. Generate the primary camera ray.
//   2. (If enabled) Lorentz-aberrate the ray direction in the
//      observer's frame.
//   3. Intersect the (possibly aberrated) ray against `sphere`.
//   4. Base shade: 0.5 * normal + 0.5 on hit; vertical sky gradient
//      on miss.
//   5. Compute the Doppler factor D for the (possibly aberrated)
//      ray direction once and reuse it.
//   6. (If enabled) apply the artistic Doppler colour shift.
//   7. (If enabled) scale by `1 + (D^4 - 1) * searchlight_strength`
//      to model relativistic beaming. The lerp form keeps the
//      strength knob a true [0, 1] dial: 0 -> no beaming,
//      1 -> full D^4.
//   8. Write framebuffer.
__global__ void k_sphere_relativistic(float* pixels, int width, int height,
                                      rr::camera::GpuCamera           cam,
                                      rr::relativity::Observer        observer,
                                      rr::relativity::RelativityParams params,
                                      rr::geometry::Sphere            sphere) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    using rr::math::Vec3;

    // 1. Camera ray.
    auto ray = rr::camera::generate_camera_ray(cam, x, y, width, height);

    // 2. Aberration.
    if (params.enable_aberration) {
        ray.direction = rr::relativity::aberrateDirection(observer.velocity,
                                                          ray.direction);
    }

    // 3. Sphere intersection.
    const auto hit = rr::cuda::intersect_sphere(ray, sphere,
                                                /*t_min=*/0.0f,
                                                /*t_max=*/1.0e30f);

    // 4. Base shade.
    Vec3 color;
    if (hit.hit) {
        color = Vec3{0.5f * hit.normal.x + 0.5f,
                     0.5f * hit.normal.y + 0.5f,
                     0.5f * hit.normal.z + 0.5f};
    } else {
        const float t = 0.5f * (ray.direction.y + 1.0f);
        color = Vec3{(1.0f - t) * 1.0f + t * 0.5f,
                     (1.0f - t) * 1.0f + t * 0.7f,
                     (1.0f - t) * 1.0f + t * 1.0f};
    }

    // 5. Doppler factor for the (possibly aberrated) photon
    //    direction in the scene frame. Computed once and reused.
    const float D = rr::relativity::dopplerFactor(observer.velocity,
                                                  ray.direction);

    // 6. Doppler colour shift (artistic approximation).
    if (params.enable_doppler) {
        color = rr::relativity::applyDopplerColor(color, D,
                                                  params.doppler_color_strength);
    }

    // 7. Searchlight / relativistic beaming.
    if (params.enable_searchlight) {
        const float D4    = rr::relativity::searchlightFactor(D);
        const float scale = 1.0f + (D4 - 1.0f) * params.searchlight_strength;
        color = color * scale;
    }

    // 8. Framebuffer write.
    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = color.x;
    pixels[idx + 1] = color.y;
    pixels[idx + 2] = color.z;
    pixels[idx + 3] = 1.0f;
}

}  // namespace

void launch_sphere_relativistic(float* device_pixels, int width, int height,
                                rr::camera::GpuCamera           cam,
                                rr::relativity::Observer        observer,
                                rr::relativity::RelativityParams params,
                                rr::geometry::Sphere            sphere,
                                cudaStream_t                    stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_sphere_relativistic<<<grid, block, 0, stream>>>(
        device_pixels, width, height, cam, observer, params, sphere);
}

}  // namespace rr::cuda
