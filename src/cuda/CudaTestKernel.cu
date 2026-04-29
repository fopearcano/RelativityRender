// CUDA diagnostic kernels.
//
// Future kernels (path tracing, relativistic perception, AOVs, ...)
// join this file - or sibling .cu files - in their own dedicated
// stages. Today the device runs:
//   - k_gradient_rgba32f      (Stage 6)
//   - k_camera_rays_visualize (Stage 7)
//   - k_sphere_visualize      (Stage 8)

#include "cuda/CudaIntersection.cuh"
#include "cuda/CudaKernels.cuh"

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

}  // namespace rr::cuda
