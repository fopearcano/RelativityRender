cmake_minimum_required(VERSION 3.20)

project(RelativityRender
    VERSION     0.1.0
    DESCRIPTION "RelativityRender - GPU renderer platform (Stage 11A: GPU sampling system)"
    LANGUAGES   CXX
)

# C++20, pinned project-wide.
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# Default build type. Picked when the user hasn't set anything.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

option(RR_BUILD_TESTS "Build tests"                                    ON)
option(RR_ENABLE_CUDA "Enable CUDA backend (requires CUDA Toolkit)"   OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

message(STATUS "RelativityRender ${PROJECT_VERSION} (Stage 11A: GPU sampling system)")

message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build tests  : ${RR_BUILD_TESTS}")
message(STATUS "  CUDA backend : ${RR_ENABLE_CUDA}")

# Optional CUDA toolchain. Only required when RR_ENABLE_CUDA is ON.
# Stage 6 introduces the first .cu translation units (CudaTestKernel.cu
# + CudaRenderer.cu), so the CUDA language is enabled here. Earlier
# CUDA-compiled .cpp files (CudaContext.cpp, CudaBuffer.cpp) continue
# to be compiled by the host C++ compiler; nvcc only sees the .cu files.
# When RR_ENABLE_CUDA is OFF the host build configures cleanly with no
# CUDA on the system - the GPU layer reports backend "(none)" and an
# empty device list at runtime.
if(RR_ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD          17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        # Turing through Blackwell. Override with
        # `-DCMAKE_CUDA_ARCHITECTURES=...` to target other GPUs.
        set(CMAKE_CUDA_ARCHITECTURES "75;80;86;89;90;100;120")
    endif()
    message(STATUS "  CUDA Toolkit : ${CUDAToolkit_VERSION}")
    message(STATUS "  CUDA archs   : ${CMAKE_CUDA_ARCHITECTURES}")
endif()

# Centralised warning flags. Every target goes through this so warnings
# can't drift per target.
function(rr_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# rr_math: header-only RR_HD math library.
# Vec2, Vec3, Vec4, Mat4, MathUtils.h. The RR_HD macro in MathUtils.h
# expands to `__host__ __device__` under nvcc and is empty otherwise,
# so the same headers will be usable from CUDA kernels in a later
# stage without modification.
add_library(rr_math INTERFACE)
target_include_directories(rr_math INTERFACE src)

# rr_image: host-side floating-point framebuffer + PPM writer.
# Color (Rgb / Rgba), Image (host buffer with set/get_pixel + clear +
# resize + save_ppm), Framebuffer (thin owning wrapper that pairs an
# Image with the renderer-target lifecycle). PUBLIC-links rr_math
# because Color.h includes math/MathUtils.h for the RR_HD macro.
add_library(rr_image STATIC
    src/image/Image.cpp
    src/image/Framebuffer.cpp
)
target_include_directories(rr_image PUBLIC src)
target_link_libraries(rr_image PUBLIC rr_math)
rr_apply_warnings(rr_image)

# rr_camera: host-side perspective camera + the device-friendly
# `GpuCamera` POD and the `RR_HD generate_camera_ray` helper. The
# device-side helper lives header-only in `camera/CameraRay.h` so the
# same code runs on host (tests) and device (kernels).
add_library(rr_camera STATIC
    src/camera/Camera.cpp
)
target_include_directories(rr_camera PUBLIC src)
target_link_libraries(rr_camera PUBLIC rr_math)
rr_apply_warnings(rr_camera)

# rr_geometry: geometry primitives.
#
# Stage 7A promotes this from INTERFACE to STATIC because Mesh.cpp
# now ships real implementation (clear / empty / reserve /
# local_bounds). Sphere / Triangle / Vertex / AABB stay header-only
# and travel through the public include path.
add_library(rr_geometry STATIC
    src/geometry/Mesh.cpp
)
target_include_directories(rr_geometry PUBLIC src)
target_link_libraries(rr_geometry PUBLIC rr_math)
rr_apply_warnings(rr_geometry)

# rr_material: PBR material data model (Stage 8A). Host-side
# `MaterialParams` POD + `Material` wrapper class with diffuse /
# emissive / metal presets. The BSDF interface, parameter validation,
# and texture / node-graph wiring live in their own master-order
# modules; this is the foundation those modules populate. The same
# `MaterialParams` POD will be the launch-arg form for the eventual
# GPU BSDF (`CudaMaterial.cuh` is a thin re-export today).
add_library(rr_material STATIC
    src/material/Material.cpp
)
target_include_directories(rr_material PUBLIC src)
target_link_libraries(rr_material PUBLIC rr_math)
rr_apply_warnings(rr_material)

# rr_lighting: light data model (Stage 9A). Host-side `Light` POD
# (a flat type-discriminated struct with point / directional /
# area-placeholder / environment-placeholder fields) plus four
# convenience factory functions. The kernel-side sampling /
# evaluation helpers join `cuda/CudaLight.cuh` later; today that
# header is a thin re-export. Renderer integration (GPU upload +
# kernel direct-lighting) is Stage 9B.
add_library(rr_lighting STATIC
    src/lighting/Light.cpp
)
target_include_directories(rr_lighting PUBLIC src)
target_link_libraries(rr_lighting PUBLIC rr_math)
rr_apply_warnings(rr_lighting)

# rr_relativity: header-only relativity math leaf.
#
#   RelativityParams.h - Observer (3-velocity in c-units) and
#                         RelativityParams (artist-facing toggles +
#                         max_beta cap).
#   RelativityMath.h   - RR_HD inline functions: clampBeta, gamma,
#                         lorentzContraction, dopplerFactor,
#                         searchlightFactor, aberrateDirection,
#                         applyDopplerColor. PHYSICAL vs
#                         ARTISTIC-APPROXIMATION tagging in each
#                         function header.
#   RelativityMath.cuh - Thin re-export for kernel TUs; future
#                         device-specific overrides land here.
#
# Stage 9 is math-only; no renderer or kernel integration ships in
# this stage.
add_library(rr_relativity INTERFACE)
target_include_directories(rr_relativity INTERFACE src)
target_link_libraries(rr_relativity INTERFACE rr_math)

# rr_scene: host-side authoring scene container.
#
# Owns the camera, render settings, Observer + RelativityParams, the
# real sphere list, and placeholder lists for meshes / materials /
# lights (each populated as the corresponding master-order module
# ships). The scene knows nothing about the GPU or the file format -
# the GPU upload path (Stage 6B / master module 11) and the scene
# loader (master module 15) consume it as input. PUBLIC-links every
# type Scene names by value so consumers see complete types.
add_library(rr_scene STATIC
    src/scene/Scene.cpp
)
target_include_directories(rr_scene PUBLIC src)
target_link_libraries(rr_scene PUBLIC rr_math rr_camera rr_geometry rr_relativity rr_material rr_lighting)
rr_apply_warnings(rr_scene)

# rr_pathtracer: GPU-safe sampling foundation (Stage 11A; master
# module 16). Header-only at this stage - everything in
# `pathtracer/RNG.{h,cuh}` and `pathtracer/Sampling.{h,cuh}` is
# RR_HD inline. The library exists so consumers (the Stage 11A
# RNG-test kernel today, the path tracer in later stages) get a
# stable include path + transitive math dependency. PUBLIC-links
# `rr_math` because the sampling primitives use Vec2 / Vec3 /
# kPi / kTwoPi / max by value.
add_library(rr_pathtracer INTERFACE)
target_include_directories(rr_pathtracer INTERFACE src)
target_link_libraries(rr_pathtracer INTERFACE rr_math)

# rr_io: scene-format IO (Stage 10B). Stage 10B.1 ships only the
# library scaffold + a single host-side helper (`sceneFileExists`).
# The `.rrscene` v1.0 parser + writer (`SceneLoader::load`,
# `SceneWriter::save`, etc., per `docs/RRSCENE_FORMAT.md`) arrive
# in a follow-up sub-stage. The library PUBLIC-links `rr_scene`
# because the parser API will populate `rr::scene::Scene` by value.
add_library(rr_io STATIC
    src/io/SceneLoader.cpp
    src/io/SceneWriter.cpp
)
target_include_directories(rr_io PUBLIC src)
target_link_libraries(rr_io PUBLIC rr_scene)
rr_apply_warnings(rr_io)

# rr_gpu: backend-agnostic GPU device + memory + (Stage 6) kernel
# launch layer.
#
#   GpuDevice.cpp  - device enumeration; delegates to the CUDA backend
#                    when RR_HAS_CUDA is defined, returns empty list
#                    otherwise.
#   GpuBuffer.cpp  - byte-level dispatch for `GpuBuffer<T>`. Forwards
#                    gpu_alloc / gpu_free / gpu_copy_h2d / gpu_copy_d2h
#                    to the CUDA backend when RR_HAS_CUDA is defined,
#                    or returns nullptr / false honestly otherwise.
#
# When RR_ENABLE_CUDA is ON the library also picks up:
#
#   CudaContext.cpp     - cudaGetDeviceCount + cudaGetDeviceProperties.
#                          Plain C++.
#   CudaBuffer.cpp      - cudaMalloc / cudaFree / cudaMemcpy. Plain C++.
#   CudaTestKernel.cu   - __global__ k_gradient_rgba32f + the
#                          launch_gradient_rgba32f host-callable
#                          launcher. Compiled by nvcc.
#   CudaRenderer.cu     - host-side scaffolding (allocate GpuBuffer,
#                          launch, sync, download into rr::image::Image).
#                          Compiled by nvcc because it transitively
#                          includes <cuda_runtime.h>.
#
# rr_image is part of the public ABI when CUDA is on (CudaRenderer's
# Result carries an rr::image::Image by value), so it is PUBLIC-linked
# in that mode.
add_library(rr_gpu STATIC
    src/gpu/GpuDevice.cpp
    src/gpu/GpuBuffer.cpp
    src/gpu/GpuScene.cpp
    src/gpu/GpuMesh.cpp
)
target_include_directories(rr_gpu PUBLIC src)
# rr_gpu's GpuScene exposes Camera / Observer / RelativityParams /
# Sphere / MaterialParams / Light by value; PUBLIC-link those
# modules so consumers see the complete types via gpu/GpuScene.h.
target_link_libraries(rr_gpu PUBLIC rr_camera rr_relativity rr_geometry rr_material rr_lighting)
rr_apply_warnings(rr_gpu)

if(RR_ENABLE_CUDA)
    target_sources(rr_gpu PRIVATE
        src/cuda/CudaContext.cpp
        src/cuda/CudaBuffer.cpp
        src/cuda/CudaTestKernel.cu
        src/cuda/CudaRngTestKernel.cu
        src/cuda/CudaRenderer.cu
    )
    # PUBLIC: targets linking rr_gpu (RelativityRender, gpu_tests) gate
    # CUDA-specific call sites on the same RR_HAS_CUDA macro.
    target_compile_definitions(rr_gpu PUBLIC RR_HAS_CUDA)
    # rr_image is part of rr_gpu's public ABI when CUDA is on
    # (CudaRenderer::Result carries an Image by value); rr_camera is
    # too because CudaRenderer::render_camera_rays takes a Camera by
    # const ref and CudaTestKernel.cu calls generate_camera_ray.
    # rr_geometry joins for the sphere-intersection diagnostic;
    # CudaRenderer::render_sphere takes a Sphere by const ref and
    # the kernel reads its fields. rr_relativity joins at Stage 10:
    # CudaRenderer::render_relativistic_sphere takes Observer +
    # RelativityParams by const ref, and the kernel calls
    # aberrateDirection / dopplerFactor / applyDopplerColor /
    # searchlightFactor on the device.
    target_link_libraries(rr_gpu PUBLIC  rr_image rr_camera rr_geometry rr_relativity rr_pathtracer)
    target_link_libraries(rr_gpu PRIVATE CUDA::cudart)
    set_target_properties(rr_gpu PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# Main executable. Stage 6B surface: --device-info / --render-gradient
# / --render-rays / --render-sphere / --render-relativistic /
# --render-scene. Links rr_scene now that --render-scene constructs
# a Scene + iterates its SceneSphere wrappers; the rest of the
# library list stays the same.
add_executable(RelativityRender
    src/main.cpp
    src/core/Logger.cpp
    src/core/Config.cpp
    src/core/CommandLine.cpp
)
target_include_directories(RelativityRender PRIVATE src)
target_link_libraries(RelativityRender PRIVATE
    rr_gpu rr_image rr_camera rr_geometry rr_relativity rr_material rr_lighting rr_scene rr_io)
rr_apply_warnings(RelativityRender)

# Tests. The project does not yet have a third-party test framework;
# each test binary is a small self-contained executable with hand-rolled
# assertions and an exit-code-driven pass/fail. ctest wires them up.
if(RR_BUILD_TESTS)
    enable_testing()

    add_executable(math_tests tests/math_tests.cpp)
    target_link_libraries(math_tests PRIVATE rr_math)
    rr_apply_warnings(math_tests)
    add_test(NAME math_tests COMMAND math_tests)

    # image_tests writes a debug gradient to `output/image_test.ppm`
    # alongside its automated assertions. Pinning the working directory
    # to ${CMAKE_BINARY_DIR} keeps the gradient under the build tree
    # (which is gitignored) instead of polluting the source tree when
    # ctest runs.
    add_executable(image_tests tests/image_tests.cpp)
    target_link_libraries(image_tests PRIVATE rr_image)
    rr_apply_warnings(image_tests)
    add_test(NAME image_tests COMMAND image_tests
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    # gpu_tests covers GpuBuffer<T> default state, zero-allocate,
    # move-only properties, honest failure when no backend is compiled
    # in, and (when CUDA is on and a device is visible) a float-array
    # upload / download / verify round-trip.
    add_executable(gpu_tests tests/gpu_tests.cpp)
    target_link_libraries(gpu_tests PRIVATE rr_gpu)
    rr_apply_warnings(gpu_tests)
    add_test(NAME gpu_tests COMMAND gpu_tests)

    # pathtracer_tests (Stage 11A) covers the RNG / Sampling
    # primitives. The headers are RR_HD inline so they compile
    # against the host C++ compiler too; this test exercises them
    # there to catch host-build breakage and to verify correctness
    # invariants (per-pixel decorrelation, [0, 1) range, hemisphere
    # samples are unit-length + upper hemisphere, cos-weighted mean
    # of dz is 2/3, etc.).
    add_executable(pathtracer_tests tests/pathtracer_tests.cpp)
    target_link_libraries(pathtracer_tests PRIVATE rr_pathtracer)
    rr_apply_warnings(pathtracer_tests)
    add_test(NAME pathtracer_tests COMMAND pathtracer_tests)
endif()
