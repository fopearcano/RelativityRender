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

# Main executable. As modules land they will be promoted to their own
# static libraries linked here; for now the foundation is small enough
# that listing source files at the top level is the simplest honest setup.
add_executable(RelativityRender
    src/main.cpp
    src/core/Logger.cpp
)

target_include_directories(RelativityRender PRIVATE src)

if(MSVC)
    target_compile_options(RelativityRender PRIVATE /W4 /permissive-)
else()
    target_compile_options(RelativityRender PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(RR_BUILD_TESTS)
    enable_testing()
endif()
