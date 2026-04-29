cmake_minimum_required(VERSION 3.20)

project(RelativityRender
    VERSION     0.1.0
    DESCRIPTION "RelativityRender - GPU renderer platform (Stage 1: core app)"
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

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

message(STATUS "RelativityRender ${PROJECT_VERSION} (Stage 1: core app)")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard : ${CMAKE_CXX_STANDARD}")

# Stage 1 has exactly one executable. As later stages land (math
# library, image module, GPU layer, ...) they get promoted to their own
# static libraries and linked here. This file stays small until then.
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
