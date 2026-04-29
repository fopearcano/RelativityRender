cmake_minimum_required(VERSION 3.20)

project(RelativityRender
    VERSION     0.1.0
    DESCRIPTION "RelativityRender - GPU renderer platform (Stage 2: math library)"
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

option(RR_BUILD_TESTS "Build tests" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

message(STATUS "RelativityRender ${PROJECT_VERSION} (Stage 2: math library)")

message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build tests  : ${RR_BUILD_TESTS}")

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

# Main executable. Stage 1 surface (no math dependency yet).
add_executable(RelativityRender
    src/main.cpp
    src/core/Logger.cpp
    src/core/Config.cpp
    src/core/CommandLine.cpp
)
target_include_directories(RelativityRender PRIVATE src)
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
endif()
