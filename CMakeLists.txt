cmake_minimum_required(VERSION 3.20)

project(RelativityRender
    VERSION 0.0.1
    DESCRIPTION "RelativityRender - GPU renderer platform with relativistic perception model"
    LANGUAGES CXX
)

# C++ standard. Pinned project-wide.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options. Defaults are chosen so the host-only build always works,
# even when CUDA / OptiX / Cinema 4D toolchains are not installed.
option(RR_ENABLE_CUDA        "Enable CUDA backend (requires CUDA Toolkit)" OFF)
option(RR_ENABLE_OPTIX       "Enable OptiX backend (requires OptiX SDK)"   OFF)
option(RR_BUILD_TESTS        "Build tests"                                 ON)
option(RR_BUILD_TOOLS        "Build standalone tools"                      OFF)
option(RR_BUILD_INTEGRATIONS "Build DCC integrations (Cinema 4D, ...)"     OFF)

# Output layout.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(RR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

message(STATUS "RelativityRender ${PROJECT_VERSION}")
message(STATUS "  C++ standard       : ${CMAKE_CXX_STANDARD}")
message(STATUS "  CUDA backend       : ${RR_ENABLE_CUDA}")
message(STATUS "  OptiX backend      : ${RR_ENABLE_OPTIX}")
message(STATUS "  Build tests        : ${RR_BUILD_TESTS}")
message(STATUS "  Build tools        : ${RR_BUILD_TOOLS}")
message(STATUS "  Build integrations : ${RR_BUILD_INTEGRATIONS}")

# Optional CUDA toolchain. Only required when RR_ENABLE_CUDA is ON.
# When OFF, the host build still configures cleanly with no CUDA on the
# system - the GPU layer reports backend "(none)" and an empty device
# list.
if(RR_ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        # Turing through Ada by default. Override with
        # `-DCMAKE_CUDA_ARCHITECTURES=...` for other targets.
        set(CMAKE_CUDA_ARCHITECTURES "75;80;86;89")
    endif()
    message(STATUS "  CUDA Toolkit       : ${CUDAToolkit_VERSION}")
    message(STATUS "  CUDA architectures : ${CMAKE_CUDA_ARCHITECTURES}")
endif()

# Module libraries. As each module grows it gets promoted from "sources
# listed directly in the executable" to its own static library so the
# dependency direction stays visible in the link list. rr_image is the
# first such module.
add_library(rr_image STATIC
    src/image/Image.cpp
    src/image/Framebuffer.cpp
)
target_include_directories(rr_image PUBLIC src)
if(MSVC)
    target_compile_options(rr_image PRIVATE /W4 /permissive-)
else()
    target_compile_options(rr_image PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Camera module. Host-only; depends on math (header-only). The
# device-side `GpuCamera` POD and the host/device `generate_camera_ray`
# helper live in `camera/CameraRay.h`, included from `.cu` files in the
# CUDA backend.
add_library(rr_camera STATIC
    src/camera/Camera.cpp
)
target_include_directories(rr_camera PUBLIC src)
if(MSVC)
    target_compile_options(rr_camera PRIVATE /W4 /permissive-)
else()
    target_compile_options(rr_camera PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Geometry module. Host-side primitive structures.
# `Sphere`, `Triangle`, and the intersection routines remain
# header-only; `Mesh` carries non-trivial vector storage so it has a
# small `.cpp` for `clear` / `reserve` / `empty`. GPU-uploadable
# variants and acceleration structures arrive alongside the CUDA
# scene-upload path (M10) and OptiX backend (M15).
add_library(rr_geometry STATIC
    src/geometry/Mesh.cpp
)
target_include_directories(rr_geometry PUBLIC src)
if(MSVC)
    target_compile_options(rr_geometry PRIVATE /W4 /permissive-)
else()
    target_compile_options(rr_geometry PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Scene module. Host-only authoring data model: Camera + render
# settings + Observer + RelativityParams + lists of spheres /
# meshes (placeholder) / materials (placeholder) / lights
# (placeholder). No GPU upload, no parser - those land in M10 / M11
# / M12 / M13 alongside the systems that consume the data.
add_library(rr_scene STATIC
    src/scene/Scene.cpp
)
target_link_libraries(rr_scene PUBLIC rr_camera)
target_include_directories(rr_scene PUBLIC src)
if(MSVC)
    target_compile_options(rr_scene PRIVATE /W4 /permissive-)
else()
    target_compile_options(rr_scene PRIVATE -Wall -Wextra -Wpedantic)
endif()

# GPU device layer. Backend-agnostic surface in src/gpu/. The CUDA
# backend (src/cuda/CudaContext.cpp) is only added to the build when
# RR_ENABLE_CUDA is ON; otherwise the gpu layer compiles with no
# backend and reports the absence at runtime.
add_library(rr_gpu STATIC
    src/gpu/GpuDevice.cpp
    src/gpu/GpuBuffer.cpp
    src/gpu/GpuScene.cpp
    src/gpu/GpuMesh.cpp
)
target_include_directories(rr_gpu PUBLIC src)
# rr_scene + rr_geometry are part of the public ABI of rr_gpu since
# `GpuScene` consumes `rr::scene::Scene` and `GpuMesh` consumes
# `rr::geometry::Mesh`.
target_link_libraries(rr_gpu PUBLIC rr_scene rr_geometry)
if(MSVC)
    target_compile_options(rr_gpu PRIVATE /W4 /permissive-)
else()
    target_compile_options(rr_gpu PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(RR_ENABLE_CUDA)
    target_sources(rr_gpu PRIVATE
        src/cuda/CudaContext.cpp
        src/cuda/CudaBuffer.cpp
        src/cuda/CudaTestKernel.cu
        src/cuda/CudaRenderer.cu
    )
    # PUBLIC: targets linking rr_gpu (RelativityRender, gpu_tests) gate
    # CUDA-specific call sites on the same RR_HAS_CUDA macro.
    target_compile_definitions(rr_gpu PUBLIC RR_HAS_CUDA)
    # CudaRenderer downloads into rr::image::Image and consumes
    # rr::camera::Camera, so both modules are part of the public ABI
    # of rr_gpu when CUDA is on.
    target_link_libraries(rr_gpu PUBLIC  rr_image rr_camera)
    target_link_libraries(rr_gpu PRIVATE CUDA::cudart)
    set_target_properties(rr_gpu PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# Main executable. As more modules land they will be promoted to their
# own static libraries and linked here.
add_executable(RelativityRender
    src/main.cpp
    src/core/Logger.cpp
    src/core/Config.cpp
    src/core/CommandLine.cpp
)
target_link_libraries(RelativityRender PRIVATE rr_gpu)

target_include_directories(RelativityRender PRIVATE src)

if(MSVC)
    target_compile_options(RelativityRender PRIVATE /W4 /permissive-)
else()
    target_compile_options(RelativityRender PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(RR_BUILD_TESTS)
    enable_testing()

    add_executable(math_tests tests/math_tests.cpp)
    target_include_directories(math_tests PRIVATE src)
    if(MSVC)
        target_compile_options(math_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(math_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME math_tests COMMAND math_tests)

    add_executable(image_tests tests/image_tests.cpp)
    target_link_libraries(image_tests PRIVATE rr_image)
    if(MSVC)
        target_compile_options(image_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(image_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME image_tests COMMAND image_tests)

    add_executable(gpu_tests tests/gpu_tests.cpp)
    target_link_libraries(gpu_tests PRIVATE rr_gpu)
    if(MSVC)
        target_compile_options(gpu_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(gpu_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gpu_tests COMMAND gpu_tests)

    add_executable(camera_tests tests/camera_tests.cpp)
    target_link_libraries(camera_tests PRIVATE rr_camera)
    if(MSVC)
        target_compile_options(camera_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(camera_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME camera_tests COMMAND camera_tests)

    # Geometry / intersection tests. Includes the RR_HD inline routines
    # in cuda/CudaIntersection.cuh directly - no CUDA toolchain needed
    # since the header has no runtime calls. Linking rr_camera also
    # supplies math + camera headers transitively.
    add_executable(geometry_tests tests/geometry_tests.cpp)
    target_link_libraries(geometry_tests PRIVATE rr_camera)
    if(MSVC)
        target_compile_options(geometry_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(geometry_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME geometry_tests COMMAND geometry_tests)

    # Relativity math tests. Header-only module; just needs `src/` on the
    # include path. The same RR_HD inline functions run in the GPU
    # kernels later, so the host coverage validates the device path.
    add_executable(relativity_tests tests/relativity_tests.cpp)
    target_include_directories(relativity_tests PRIVATE src)
    if(MSVC)
        target_compile_options(relativity_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(relativity_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME relativity_tests COMMAND relativity_tests)

    # Scene structure tests. Links rr_scene which transitively brings
    # camera + math + relativity + geometry headers via PUBLIC src
    # includes.
    add_executable(scene_tests tests/scene_tests.cpp)
    target_link_libraries(scene_tests PRIVATE rr_scene)
    if(MSVC)
        target_compile_options(scene_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(scene_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME scene_tests COMMAND scene_tests)

    # Mesh / triangle tests. Links rr_geometry for the Mesh symbols;
    # math and the Transform alias come along through the PUBLIC src
    # include path.
    add_executable(mesh_tests tests/mesh_tests.cpp)
    target_link_libraries(mesh_tests PRIVATE rr_geometry)
    if(MSVC)
        target_compile_options(mesh_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(mesh_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME mesh_tests COMMAND mesh_tests)
endif()
