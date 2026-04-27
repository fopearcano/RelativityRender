cmake_minimum_required(VERSION 3.20)

project(RelativityRender
    VERSION 0.0.1
    DESCRIPTION "RelativityRender - GPU renderer platform with relativistic perception model"
    LANGUAGES CXX
)

# C++ standard. Pinned project-wide per docs/DEVELOPMENT_RULES.md sec.8.
set(CMAKE_CXX_STANDARD 17)
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

# Project root for module CMake files to reference.
set(RR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

message(STATUS "RelativityRender ${PROJECT_VERSION}")
message(STATUS "  C++ standard       : ${CMAKE_CXX_STANDARD}")
message(STATUS "  CUDA backend       : ${RR_ENABLE_CUDA}")
message(STATUS "  OptiX backend      : ${RR_ENABLE_OPTIX}")
message(STATUS "  Build tests        : ${RR_BUILD_TESTS}")
message(STATUS "  Build tools        : ${RR_BUILD_TOOLS}")
message(STATUS "  Build integrations : ${RR_BUILD_INTEGRATIONS}")
message(STATUS "")
message(STATUS "  Source tree is currently a skeleton (M1).")
message(STATUS "  Module CMakeLists.txt files are added as each module lands.")
message(STATUS "  See docs/BUILD_PLAN.md and docs/MILESTONE_ROADMAP.md.")

# Module subdirectories are added here as they land. Order tracks
# docs/MILESTONE_ROADMAP.md. They are commented out until the corresponding
# milestone is implemented; uncommenting without a CMakeLists.txt in the
# subdir would break the host build, which violates the engineering rules.
#
# add_subdirectory(src/core)        # M2  Core Engine
# add_subdirectory(src/math)        # M3  Math Library
# add_subdirectory(src/image)       # M4  Image / Framebuffer
# add_subdirectory(src/gpu)         # M5  GPU Device Layer
# add_subdirectory(src/cuda)        # M5/M6 CUDA Backend
# add_subdirectory(src/camera)      # M7  Camera System
# add_subdirectory(src/relativity)  # M9  Relativistic Camera Model
# add_subdirectory(src/scene)       # M10 Scene Graph
# add_subdirectory(src/geometry)    # M10 Geometry System
# add_subdirectory(src/material)    # M11 Material / Shading
# add_subdirectory(src/lighting)    # M12 Lighting System
# add_subdirectory(src/io)          # M13 Scene File / Image IO
# add_subdirectory(src/pathtracer)  # M14 Path Tracer
# add_subdirectory(src/renderer)    # M14+ Renderer (progressive, AOVs, denoise)
# add_subdirectory(src/optix)       # M15 OptiX Backend
# add_subdirectory(src/texture)     # M16 Texture System
# add_subdirectory(src/server)      # M18 Renderer Server

if(RR_BUILD_TESTS)
    enable_testing()
    # add_subdirectory(tests)
endif()

if(RR_BUILD_TOOLS)
    # add_subdirectory(tools)
endif()

if(RR_BUILD_INTEGRATIONS)
    # add_subdirectory(integrations/c4d)
endif()
