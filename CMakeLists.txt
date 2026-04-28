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
option(RR_ENABLE_CUDA                  "Enable CUDA backend (requires CUDA Toolkit)" OFF)
option(RELATIVITYRENDER_ENABLE_OPTIX   "Enable OptiX backend (requires OptiX SDK)"   OFF)
option(RR_BUILD_TESTS                  "Build tests"                                 ON)
option(RR_BUILD_TOOLS                  "Build standalone tools"                      OFF)
option(RR_BUILD_INTEGRATIONS           "Build DCC integrations (Cinema 4D, ...)"     OFF)

# Output layout.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(RR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

message(STATUS "RelativityRender ${PROJECT_VERSION}")
message(STATUS "  C++ standard       : ${CMAKE_CXX_STANDARD}")
message(STATUS "  CUDA backend       : ${RR_ENABLE_CUDA}")
message(STATUS "  OptiX backend      : ${RELATIVITYRENDER_ENABLE_OPTIX}")
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

# Optional OptiX SDK. Only required when RELATIVITYRENDER_ENABLE_OPTIX
# is ON. The SDK is header-only at compile time; the runtime is loaded
# from the NVIDIA driver via `optixInit()` at startup. OptiX requires
# CUDA - it sits on top of a CUDA context.
if(RELATIVITYRENDER_ENABLE_OPTIX)
    if(NOT RR_ENABLE_CUDA)
        message(FATAL_ERROR
            "RELATIVITYRENDER_ENABLE_OPTIX requires RR_ENABLE_CUDA. "
            "Configure with both options enabled.")
    endif()

    if(NOT OPTIX_INSTALL_DIR AND DEFINED ENV{OPTIX_INSTALL_DIR})
        set(OPTIX_INSTALL_DIR "$ENV{OPTIX_INSTALL_DIR}" CACHE PATH
            "Root of the OptiX SDK installation")
    endif()

    set(_optix_hints "")
    if(OPTIX_INSTALL_DIR)
        list(APPEND _optix_hints "${OPTIX_INSTALL_DIR}/include")
    endif()

    find_path(OPTIX_INCLUDE_DIR
        NAMES optix.h
        HINTS ${_optix_hints}
              "/usr/local/optix/include"
              "/opt/nvidia/optix/include"
              "/opt/optix/include"
              "$ENV{HOME}/optix/include"
    )

    if(NOT OPTIX_INCLUDE_DIR)
        message(FATAL_ERROR
            "RELATIVITYRENDER_ENABLE_OPTIX is ON but optix.h was not found.\n"
            "Set OPTIX_INSTALL_DIR (CMake -D var or environment) to the SDK root.")
    endif()

    message(STATUS "  OptiX include dir  : ${OPTIX_INCLUDE_DIR}")
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

# Material module. Host-side parameter pack + thin authoring
# wrapper. The BSDF interface, texture binding, and node-graph
# integration land in their own milestones; this is the foundation
# scene-format and the node editor will populate.
add_library(rr_material STATIC
    src/material/Material.cpp
)
target_include_directories(rr_material PUBLIC src)
if(MSVC)
    target_compile_options(rr_material PRIVATE /W4 /permissive-)
else()
    target_compile_options(rr_material PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Lighting module. Host-side data model only - point /
# directional / area placeholder / environment placeholder. The
# importance-sampling helpers and kernel-side direct-lighting
# evaluation live behind the same API and arrive with the path
# tracer (M14).
add_library(rr_lighting STATIC
    src/lighting/Light.cpp
)
target_include_directories(rr_lighting PUBLIC src)
if(MSVC)
    target_compile_options(rr_lighting PRIVATE /W4 /permissive-)
else()
    target_compile_options(rr_lighting PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Texture module. Host-side foundation only at this milestone:
# constant-color textures and an image-backed `ImageTexture` that
# wraps `rr::image::Image`. The device-side launch-argument POD
# and `RR_HD inline` sampler live in `cuda/CudaTexture.cuh`. No
# kernel currently consumes a textured material slot - hooking
# textures into the shader path lands in a later M16 slice.
add_library(rr_texture STATIC
    src/texture/Texture.cpp
    src/texture/ImageTexture.cpp
)
# rr_image is part of the public ABI of rr_texture since
# `ImageTexture` carries an `rr::image::Image` by value.
target_link_libraries(rr_texture PUBLIC rr_image)
target_include_directories(rr_texture PUBLIC src)
if(MSVC)
    target_compile_options(rr_texture PRIVATE /W4 /permissive-)
else()
    target_compile_options(rr_texture PRIVATE -Wall -Wextra -Wpedantic)
endif()

# IO module. Scene file parser (and eventually writer). Host-only.
# The parser populates rr::scene::Scene, which the renderer's GPU
# upload path already consumes; rr_io itself has no GPU dep.
# Initial slice loads only render_settings / camera / relativity
# (the M13 partial parser).
add_library(rr_io STATIC
    src/io/SceneLoader.cpp
    src/io/SceneWriter.cpp
)
target_link_libraries(rr_io PUBLIC rr_scene)
target_include_directories(rr_io PUBLIC src)
if(MSVC)
    target_compile_options(rr_io PRIVATE /W4 /permissive-)
else()
    target_compile_options(rr_io PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Scene module. Host-only authoring data model: Camera + render
# settings + Observer + RelativityParams + lists of spheres /
# meshes / materials / lights. Embeds the host PODs from each of
# those modules, so the GPU upload path can publish without
# reshaping.
add_library(rr_scene STATIC
    src/scene/Scene.cpp
)
target_link_libraries(rr_scene PUBLIC
    rr_camera
    rr_geometry
    rr_lighting
    rr_texture
)
target_include_directories(rr_scene PUBLIC src)
if(MSVC)
    target_compile_options(rr_scene PRIVATE /W4 /permissive-)
else()
    target_compile_options(rr_scene PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Renderer module. Host-side render-pass / AOV foundation. The
# `AOV` class wraps an `rr::image::Image` and adds a per-AOV
# save_ppm that normalises scalar AOVs to grayscale so the v1
# viewer hook stays human-readable. The device-side AOV pack +
# write helpers live in `cuda/CudaAOV.cuh` (header-only) and are
# consumed by the CUDA renderer + tests.
add_library(rr_renderer STATIC
    src/renderer/AOV.cpp
)
target_include_directories(rr_renderer PUBLIC src)
target_link_libraries(rr_renderer PUBLIC rr_image)
if(MSVC)
    target_compile_options(rr_renderer PRIVATE /W4 /permissive-)
else()
    target_compile_options(rr_renderer PRIVATE -Wall -Wextra -Wpedantic)
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
# rr_scene + rr_geometry + rr_material + rr_lighting are part of the
# public ABI of rr_gpu since `GpuScene` consumes scene structures,
# meshes, materials, and lights. rr_renderer is part of the public
# ABI when CUDA is on (CudaRenderer::AOVResult exposes AOV by value),
# but is cheap enough to PUBLIC-link unconditionally.
target_link_libraries(rr_gpu PUBLIC rr_scene rr_geometry rr_material rr_lighting rr_renderer)
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

# OptiX backend. Compiled into the build whether or not the SDK is
# detected, so the lifecycle / status surface is reachable from
# main.cpp regardless. When RELATIVITYRENDER_ENABLE_OPTIX is ON the
# SDK headers are added to the private include path and `RR_HAS_OPTIX`
# is defined for the implementation TU; OFF leaves stub bodies that
# report "not compiled in".
add_library(rr_optix STATIC
    src/optix/OptixBackend.cpp
    src/optix/OptixRenderer.cpp
)
target_include_directories(rr_optix PUBLIC src)
# rr_optix consumes rr::image::Image (in OptixRenderer::Result) and
# the rest of the host-side scene types via rr_gpu transitively.
target_link_libraries(rr_optix PUBLIC rr_image)
if(MSVC)
    target_compile_options(rr_optix PRIVATE /W4 /permissive-)
else()
    target_compile_options(rr_optix PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(RELATIVITYRENDER_ENABLE_OPTIX)
    target_compile_definitions(rr_optix PUBLIC RR_HAS_OPTIX)
    # OptiX is header + driver-loaded runtime; no library to link.
    # CUDA Runtime is needed because optix.h transitively includes
    # cuda_runtime.h.
    target_include_directories(rr_optix PRIVATE ${OPTIX_INCLUDE_DIR})
    target_link_libraries(rr_optix PRIVATE CUDA::cudart)
endif()

# Main executable. As more modules land they will be promoted to their
# own static libraries and linked here.
add_executable(RelativityRender
    src/main.cpp
    src/core/Logger.cpp
    src/core/Config.cpp
    src/core/CommandLine.cpp
)
target_link_libraries(RelativityRender PRIVATE rr_gpu rr_io rr_optix)

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

    # Material foundation tests. Links rr_material for Material's
    # methods + presets; MaterialParams + math headers come through
    # the PUBLIC src include path.
    add_executable(material_tests tests/material_tests.cpp)
    target_link_libraries(material_tests PRIVATE rr_material)
    if(MSVC)
        target_compile_options(material_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(material_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME material_tests COMMAND material_tests)

    # Lighting foundation tests. Links rr_lighting for the factory
    # symbols; Light POD and math headers come through the PUBLIC
    # src include path.
    add_executable(lighting_tests tests/lighting_tests.cpp)
    target_link_libraries(lighting_tests PRIVATE rr_lighting)
    if(MSVC)
        target_compile_options(lighting_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(lighting_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME lighting_tests COMMAND lighting_tests)

    # Scene loader (.rrscene) tests. Loads scenes/test_minimal.rrscene
    # via an absolute path baked in at configure time, so the test
    # works from any ctest working directory.
    add_executable(io_tests tests/io_tests.cpp)
    target_link_libraries(io_tests PRIVATE rr_io)
    target_compile_definitions(io_tests PRIVATE
        RR_TEST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/scenes")
    if(MSVC)
        target_compile_options(io_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(io_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME io_tests COMMAND io_tests)

    # Path-tracer sampling foundation tests. RNG + hemisphere
    # sampling primitives are header-only `RR_HD inline` helpers;
    # linking rr_camera transitively exposes math + the src/
    # include path the headers need.
    add_executable(sampling_tests tests/sampling_tests.cpp)
    target_link_libraries(sampling_tests PRIVATE rr_camera)
    if(MSVC)
        target_compile_options(sampling_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(sampling_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME sampling_tests COMMAND sampling_tests)

    # Texture foundation tests. Links rr_texture for ConstantTexture /
    # ImageTexture symbols; includes `cuda/CudaTexture.cuh` directly to
    # exercise the same `RR_HD inline` device sampler the kernels will
    # call later.
    add_executable(texture_tests tests/texture_tests.cpp)
    target_link_libraries(texture_tests PRIVATE rr_texture)
    if(MSVC)
        target_compile_options(texture_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(texture_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME texture_tests COMMAND texture_tests)

    # Render-pass / AOV foundation tests. Verifies the AOVKind ->
    # name + colour-vs-scalar predicates, AOV constructor sizing,
    # and that `AOV::save_ppm` normalises scalar AOVs to grayscale
    # at save time without committing the renderer to a tone-mapping
    # policy. Saves test PPMs into the build dir.
    add_executable(aov_tests tests/aov_tests.cpp)
    target_link_libraries(aov_tests PRIVATE rr_renderer)
    if(MSVC)
        target_compile_options(aov_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(aov_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME aov_tests COMMAND aov_tests)
endif()
