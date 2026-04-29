cmake_minimum_required(VERSION 3.20)

project(RelativityRender
    VERSION     0.1.0
    DESCRIPTION "RelativityRender - GPU renderer platform (relativity-core-v1)"
    LANGUAGES   CXX
)

# C++ standard. Pinned project-wide.
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# Default build type. Kernel work needs symbols + optimisation; pick
# RelWithDebInfo when the user hasn't set anything.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# Build options. The host build always works; CUDA is optional and
# requires the CUDA Toolkit + a CUDA-capable host.
option(RR_ENABLE_CUDA "Enable CUDA backend (requires CUDA Toolkit)" OFF)
option(RR_BUILD_TESTS "Build tests"                                 ON)

# Output layout.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

message(STATUS "RelativityRender ${PROJECT_VERSION} (relativity-core-v1)")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard  : ${CMAKE_CXX_STANDARD}")
message(STATUS "  CUDA backend  : ${RR_ENABLE_CUDA}")
message(STATUS "  Build tests   : ${RR_BUILD_TESTS}")

# Optional CUDA toolchain. Only required when RR_ENABLE_CUDA is ON.
if(RR_ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD          17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        # Turing through Blackwell. Override with
        # `-DCMAKE_CUDA_ARCHITECTURES=...` for other targets.
        set(CMAKE_CUDA_ARCHITECTURES "75;80;86;89;90;100;120")
    endif()
    message(STATUS "  CUDA Toolkit  : ${CUDAToolkit_VERSION}")
    message(STATUS "  CUDA archs    : ${CMAKE_CUDA_ARCHITECTURES}")
endif()

# Centralised warning flags. Every library and executable goes through
# this so warnings can't drift per target.
function(rr_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ------------------------------------------------------------------
# Module libraries.
#
# Day-1 of relativity-core-v1: only the GPU foundation. Other modules
# (camera, geometry, scene, material, lighting, relativity, renderer,
# io, server, integrations) exist as scaffold directories and will be
# repopulated in their own slices per `docs/REWRITE_STATUS.md`.
# ------------------------------------------------------------------

# rr_core: tiny utilities (logger, version constants).
add_library(rr_core STATIC
    src/core/Logger.cpp
)
target_include_directories(rr_core PUBLIC src)
rr_apply_warnings(rr_core)

# rr_image: host-side float framebuffer + PPM writer.
add_library(rr_image STATIC
    src/image/Image.cpp
)
target_include_directories(rr_image PUBLIC src)
rr_apply_warnings(rr_image)

# rr_gpu: backend-agnostic device + memory layer. The CUDA backend
# (src/cuda/) is added to this library only when RR_ENABLE_CUDA is ON;
# otherwise the layer compiles with no backend and reports the absence
# at runtime.
add_library(rr_gpu STATIC
    src/gpu/GpuBuffer.cpp
    src/gpu/GpuDevice.cpp
)
target_include_directories(rr_gpu PUBLIC src)
target_link_libraries(rr_gpu PUBLIC rr_image)
rr_apply_warnings(rr_gpu)

if(RR_ENABLE_CUDA)
    target_sources(rr_gpu PRIVATE
        src/cuda/CudaBuffer.cpp
        src/cuda/CudaContext.cpp
        src/cuda/CudaRenderer.cu
        src/cuda/CudaGradientKernel.cu
    )
    # PUBLIC so executables linking rr_gpu (RelativityRender, gpu_tests)
    # gate CUDA-specific call sites on the same RR_HAS_CUDA macro.
    target_compile_definitions(rr_gpu PUBLIC RR_HAS_CUDA)
    target_link_libraries(rr_gpu PRIVATE CUDA::cudart)
    set_target_properties(rr_gpu PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# ------------------------------------------------------------------
# Main executable.
# ------------------------------------------------------------------
add_executable(RelativityRender src/main.cpp)
target_link_libraries(RelativityRender PRIVATE rr_core rr_gpu rr_image)
target_include_directories(RelativityRender PRIVATE src)
rr_apply_warnings(RelativityRender)

# ------------------------------------------------------------------
# Tests.
# ------------------------------------------------------------------
if(RR_BUILD_TESTS)
    enable_testing()

    function(rr_add_test name)
        cmake_parse_arguments(arg "" "" "SOURCES;LIBS" ${ARGN})
        add_executable(${name} ${arg_SOURCES})
        if(arg_LIBS)
            target_link_libraries(${name} PRIVATE ${arg_LIBS})
        endif()
        target_include_directories(${name} PRIVATE src)
        rr_apply_warnings(${name})
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    rr_add_test(math_tests  SOURCES tests/math_tests.cpp)
    rr_add_test(image_tests SOURCES tests/image_tests.cpp LIBS rr_image)
    rr_add_test(gpu_tests   SOURCES tests/gpu_tests.cpp   LIBS rr_gpu)
endif()
