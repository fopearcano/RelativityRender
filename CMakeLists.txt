cmake_minimum_required(VERSION 3.20)

project(RelativityRender
    VERSION     0.1.0
    DESCRIPTION "RelativityRender - GPU renderer platform (Stage 5: GPU buffer layer)"
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

message(STATUS "RelativityRender ${PROJECT_VERSION} (Stage 5: GPU buffer layer)")

message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build tests  : ${RR_BUILD_TESTS}")
message(STATUS "  CUDA backend : ${RR_ENABLE_CUDA}")

# Optional CUDA toolchain. Only required when RR_ENABLE_CUDA is ON.
# Stage 4 only needs the CUDA Runtime headers + libcudart for device
# enumeration; no .cu files are compiled here, so we deliberately do
# NOT call enable_language(CUDA). That comes back when Stage 5 (CUDA
# framebuffer / kernel infrastructure) introduces the first kernel.
# When RR_ENABLE_CUDA is OFF the host build configures cleanly with
# no CUDA on the system - the GPU layer reports backend "(none)"
# and an empty device list at runtime.
if(RR_ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
    message(STATUS "  CUDA Toolkit : ${CUDAToolkit_VERSION}")
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

# rr_gpu: backend-agnostic GPU device + memory layer.
#
#   GpuDevice.cpp  - device enumeration; delegates to the CUDA backend
#                    when RR_HAS_CUDA is defined, returns empty list
#                    otherwise.
#   GpuBuffer.cpp  - byte-level dispatch for `GpuBuffer<T>`. Forwards
#                    gpu_alloc / gpu_free / gpu_copy_h2d / gpu_copy_d2h
#                    to the CUDA backend when RR_HAS_CUDA is defined,
#                    or returns nullptr / false honestly otherwise.
#
# The CUDA backend (CudaContext.cpp + CudaBuffer.cpp) is added to
# rr_gpu only when RR_ENABLE_CUDA is ON. Both .cpp files are plain
# C++ - they include <cuda_runtime.h> and call cudaMalloc / cudaFree
# / cudaMemcpy / cudaGetDeviceProperties; no __global__ kernels and
# no <<<...>>> launch syntax, so this stage still does NOT need
# enable_language(CUDA). That comes back when the first .cu kernel
# lands in a later stage.
add_library(rr_gpu STATIC
    src/gpu/GpuDevice.cpp
    src/gpu/GpuBuffer.cpp
)
target_include_directories(rr_gpu PUBLIC src)
rr_apply_warnings(rr_gpu)

if(RR_ENABLE_CUDA)
    target_sources(rr_gpu PRIVATE
        src/cuda/CudaContext.cpp
        src/cuda/CudaBuffer.cpp
    )
    # PUBLIC: targets linking rr_gpu (RelativityRender, gpu_tests) gate
    # CUDA-specific call sites on the same RR_HAS_CUDA macro.
    target_compile_definitions(rr_gpu PUBLIC RR_HAS_CUDA)
    target_link_libraries(rr_gpu PRIVATE CUDA::cudart)
endif()

# Main executable. Stage 4 surface: now links rr_gpu so --device-info
# can enumerate visible CUDA devices.
add_executable(RelativityRender
    src/main.cpp
    src/core/Logger.cpp
    src/core/Config.cpp
    src/core/CommandLine.cpp
)
target_include_directories(RelativityRender PRIVATE src)
target_link_libraries(RelativityRender PRIVATE rr_gpu)
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
endif()
