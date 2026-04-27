#include "cuda/CudaIntersection.cuh"
#include "cuda/CudaKernels.cuh"
#include "math/Vec3.h"
#include "relativity/RelativityMath.cuh"

namespace rr::cuda {

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

}

void launch_gradient_rgba32f(float* device_pixels, int width, int height,
                             cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_gradient_rgba32f<<<grid, block, 0, stream>>>(device_pixels, width, height);
}

namespace {

// Generates a primary ray per pixel and encodes the (normalised)
// direction as RGB by mapping each component from [-1, 1] to [0, 1].
// Alpha is 1. The kernel does all per-pixel work; the host only
// launches and downloads.
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

}

void launch_camera_rays_visualize(float* device_pixels, int width, int height,
                                  rr::camera::GpuCamera cam,
                                  cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_camera_rays_visualize<<<grid, block, 0, stream>>>(device_pixels, width, height, cam);
}

namespace {

// Generate primary ray, intersect against the sphere, shade. The whole
// per-pixel pipeline runs on the device; the host only launches and
// downloads.
//
// Hit  -> RGB encodes the outward normal (`0.5*n + 0.5`).
// Miss -> simple vertical sky gradient parameterised by `dir.y`.
__global__ void k_sphere_visualize(float* pixels, int width, int height,
                                   rr::camera::GpuCamera cam,
                                   rr::geometry::Sphere  sphere) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const auto ray = rr::camera::generate_camera_ray(cam, x, y, width, height);

    // Generous t range; M14 will replace this with proper ray epsilons
    // and a real far plane, but for a single primitive any positive
    // distance is fine.
    const auto hit = rr::cuda::intersect_sphere(ray, sphere, /*t_min=*/0.0f,
                                                /*t_max=*/1.0e30f);

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (hit.hit) {
        r = 0.5f * hit.normal.x + 0.5f;
        g = 0.5f * hit.normal.y + 0.5f;
        b = 0.5f * hit.normal.z + 0.5f;
    } else {
        // dir.y is in [-1, 1]; map to [0, 1] for a top-light, dark-floor
        // sky reminiscent of an open studio backdrop.
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

}

void launch_sphere_visualize(float* device_pixels, int width, int height,
                             rr::camera::GpuCamera cam,
                             rr::geometry::Sphere  sphere,
                             cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_sphere_visualize<<<grid, block, 0, stream>>>(device_pixels, width, height,
                                                   cam, sphere);
}

namespace {

// Composes the full relativistic perception pipeline on the device:
// generate ray -> aberrate -> intersect -> base shade -> Doppler colour
// -> beaming. The whole per-pixel path is GPU-only.
//
// The pipeline orders aberration *before* intersection so the geometry
// is hit-tested with the ray as observed by the boosted frame (i.e.
// the apparent sphere position is what gets sampled), and Doppler /
// searchlight come *after* shading so they modify the perceived
// radiance rather than the underlying surface state.
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
    const auto hit = rr::cuda::intersect_sphere(ray, sphere, /*t_min=*/0.0f,
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

    // Doppler factor for the (possibly aberrated) photon direction in
    // the scene frame. Computed once and reused for steps 5 and 6.
    const float D = rr::relativity::dopplerFactor(observer.velocity,
                                                  ray.direction);

    // 5. Doppler colour shift (artistic approximation).
    if (params.enable_doppler) {
        color = rr::relativity::applyDopplerColor(color, D,
                                                  params.doppler_color_strength);
    }

    // 6. Searchlight / relativistic beaming.
    if (params.enable_searchlight) {
        const float D4    = rr::relativity::searchlightFactor(D);
        const float scale = 1.0f + (D4 - 1.0f) * params.searchlight_strength;
        color = color * scale;
    }

    // 7. Framebuffer write.
    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = color.x;
    pixels[idx + 1] = color.y;
    pixels[idx + 2] = color.z;
    pixels[idx + 3] = 1.0f;
}

}

void launch_sphere_relativistic(float* device_pixels, int width, int height,
                                rr::camera::GpuCamera           cam,
                                rr::relativity::Observer        observer,
                                rr::relativity::RelativityParams params,
                                rr::geometry::Sphere            sphere,
                                cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_sphere_relativistic<<<grid, block, 0, stream>>>(
        device_pixels, width, height, cam, observer, params, sphere);
}

}


