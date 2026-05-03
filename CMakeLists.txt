cmake_minimum_required(VERSION 3.20)

project(RelativityRender
    VERSION     0.1.0
    DESCRIPTION "RelativityRender - GPU renderer platform (Stage 20E: OptiX closest-hit verified)"
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

# Stage 12B.1: option-flag-only slice. When OFF (default) the build is
# byte-identical to before; when ON we print a status note that OptiX
# support has been requested. Sources, headers, and find_package(OptiX)
# wiring land in subsequent Stage 12B sub-stages - this slice
# deliberately does nothing else so the build stays clean on hosts
# without the OptiX SDK.
#
# Naming: the canonical user-facing flag is `RR_ENABLE_OPTIX`,
# matching the `RR_*` prefix every other project option uses
# (RR_BUILD_TESTS, RR_ENABLE_CUDA). The previous flag name
# `RELATIVITYRENDER_ENABLE_OPTIX` is kept as a deprecated alias
# (see the forwarding block below) so existing CI scripts /
# docstring instructions / commit messages do not silently
# stop opting in to the OptiX backend.
option(RR_ENABLE_OPTIX
       "Request OptiX backend (Stage 12B.1: flag-only; no sources / headers wired yet)"
       OFF)

# Deprecated alias: forward `-DRELATIVITYRENDER_ENABLE_OPTIX=ON`
# (the pre-Stage-19 flag spelling) to the canonical
# `RR_ENABLE_OPTIX` and emit a one-line deprecation warning at
# configure time. The forward only fires when the user explicitly
# set the deprecated alias and did NOT set the canonical option,
# so the new spelling always wins when both are passed.
if(DEFINED RELATIVITYRENDER_ENABLE_OPTIX AND RELATIVITYRENDER_ENABLE_OPTIX
   AND NOT RR_ENABLE_OPTIX)
    message(WARNING
        "RELATIVITYRENDER_ENABLE_OPTIX is deprecated; use RR_ENABLE_OPTIX "
        "instead. Forwarding -DRELATIVITYRENDER_ENABLE_OPTIX=ON to "
        "-DRR_ENABLE_OPTIX=ON for backwards compatibility. The "
        "deprecated alias will continue to work but new docs / CI / "
        "commit messages should adopt the canonical RR_ENABLE_OPTIX "
        "name.")
    set(RR_ENABLE_OPTIX ON CACHE BOOL "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

message(STATUS "RelativityRender ${PROJECT_VERSION} (Stage 20E: OptiX closest-hit verified)")

message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build tests  : ${RR_BUILD_TESTS}")
message(STATUS "  CUDA backend : ${RR_ENABLE_CUDA}")

# Stage 12B.1: when the user has requested OptiX support, print a
# status note acknowledging the request. This sub-stage does NOT wire
# OptiX sources, find_package(OptiX), or any include paths - those
# land in subsequent 12B.x slices. The flag's only effect today is
# this message.
if(RR_ENABLE_OPTIX)
    message(STATUS "  OptiX backend: requested "
                   "(-DRR_ENABLE_OPTIX=ON; Stage 12B.2 file "
                   "skeleton compiles, SDK / programs / SBT / AS wiring "
                   "lands in subsequent 12B sub-stages)")
endif()

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
#
# Each flag is wrapped in `$<$<COMPILE_LANGUAGE:CXX>:...>` so only C++
# translation units pick it up. CUDA targets compile their `.cu` files
# through `nvcc`, which does not accept MSVC's `/W4` / `/permissive-`
# (it would interpret them as input files and fail with
# "nvcc fatal: A single input file is required for a non-link phase"
# - see `docs/WINDOWS_CUDA_BUILD_FIX.md`). Filtering by language keeps
# the host warnings on `.cpp` and leaves `.cu` compiles untouched.
# If a future slice wants to forward host-side warnings into nvcc, it
# can do so explicitly via `-Xcompiler=...` rather than letting them
# leak in by default.
function(rr_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:/W4>
            $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
    else()
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wall>
            $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
            $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>)
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

# rr_texture: texture data model (Stage 13A; master order #18).
# Host-side `Texture` POD (constant-colour or image reference) +
# `ImageTexture` (width/height/format + a placeholder pixel
# buffer). No sampler, no GPU upload, no UV lookup; the BSDF
# wiring (a future `MaterialParams.base_color_texture_id` etc.)
# and the device-side `cudaTextureObject_t` lifecycle land in
# subsequent 13B+ sub-stages. The same `Texture` POD will be the
# launch-argument form for the eventual sampler;
# `cuda/CudaTexture.cuh` is a thin re-export today, mirroring
# `CudaMaterial.cuh` / `CudaLight.cuh`.
add_library(rr_texture STATIC
    src/texture/Texture.cpp
    src/texture/ImageTexture.cpp
)
target_include_directories(rr_texture PUBLIC src)
target_link_libraries(rr_texture PUBLIC rr_math)
rr_apply_warnings(rr_texture)

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

# rr_renderer: host-side renderer pieces that don't fit cleanly
# inside `rr_gpu` (CUDA backend) or `rr_image` (host buffers).
# Stage 11B introduces this library for the progressive-
# accumulation buffer (`AccumulationBuffer`); Stage 11C adds the
# host-side `PathTracer` that drives the spp loop + accumulation
# + resolve. Stage 14A.1 adds the AOV / render-pass data model
# (`AOV.cpp`; master order #19) - data model only, no renderer
# integration yet. Stage 14A.2 adds the per-pass GPU buffer
# owner (`GpuAOVBuffer.cpp`) that allocates / manages /
# downloads device-side AOV storage; still no kernel hook.
# Each .cpp ends up here (instead of inside the header-only
# `rr_pathtracer` INTERFACE library) so the static-lib
# dependency direction stays one-way:
# rr_renderer -> rr_gpu, rr_pathtracer, rr_image, with no cycle.
add_library(rr_renderer STATIC
    src/renderer/AccumulationBuffer.cpp
    src/renderer/AOV.cpp
    src/renderer/GpuAOVBuffer.cpp
    src/pathtracer/PathTracer.cpp
)
target_include_directories(rr_renderer PUBLIC src)
target_link_libraries(rr_renderer PUBLIC rr_image rr_gpu rr_pathtracer)
rr_apply_warnings(rr_renderer)

# rr_server: renderer-server skeleton (Stage 15A.1; master order
# #20). Pure-POSIX TCP server bound to 127.0.0.1 with a single
# command (`ping` -> `pong`). No rendering integration yet. The
# library compiles standalone (only depends on libc / POSIX
# sockets) and is linked into the RelativityRender executable so
# the build target exercises it; an actual `--serve` CLI handler
# joins in a subsequent 15A+ sub-stage.
#
# Linux-only at this stage: `socket` / `bind` / `listen` /
# `accept` come from <sys/socket.h>. Cross-platform support
# (winsock backend) is a future slice when the server actually
# needs to ship to non-Linux hosts.
add_library(rr_server STATIC
    src/server/RenderServer.cpp
)
target_include_directories(rr_server PUBLIC src)
# Stage 15B.1: rr_server's `load_scene` command forwards to the
# host-side `.rrscene` parser via `rr::io::load`, and the loaded
# scene is held as `std::optional<rr::scene::Scene>` in the
# server's state. PUBLIC-link rr_io (which already pulls
# rr_scene transitively) so the server's header surface is
# self-contained for downstream consumers.
#
# Stage-15-render: the wire `render` command also needs the GPU
# upload + render path. Linking rr_gpu (for `GpuScene` +
# `CudaRenderer` declarations) and rr_image (for `Image::save_ppm`)
# is unconditional - the actual render code in RenderServer.cpp
# is gated on `RR_HAS_CUDA`, so a no-CUDA build links the same
# libraries but never calls into the CUDA-only symbols.
target_link_libraries(rr_server PUBLIC rr_io rr_gpu rr_image)
# Windows-portability slice: link Ws2_32 for the Winsock2 ABI
# (socket / bind / listen / accept / closesocket / shutdown /
# inet_pton / WSAStartup / WSACleanup). PUBLIC-link so the
# downstream `RelativityRender` executable picks the dependency
# up transitively. POSIX builds don't need a separate library
# (the symbols live in libc).
if(WIN32)
    target_link_libraries(rr_server PUBLIC ws2_32)
endif()
rr_apply_warnings(rr_server)

# rr_optix: OptiX backend file skeleton. Stage 12B.3 makes the
# entire library conditional on RR_ENABLE_OPTIX so the OFF build
# never compiles any `src/optix/` source - byte-identical to the
# pre-12B state. When ON, the library aggregates the placeholder
# Stage 12B.2 sources (OptixBackend.cpp + OptixRenderer.cpp) and
# exports the `RELATIVITYRENDER_ENABLE_OPTIX` compile definition
# PUBLICly so consumers reading `OptixBackend::isCompiled()` see
# the right answer.
#
# Naming: the canonical CMake option is `RR_ENABLE_OPTIX`; the
# C++ compile-time macro is intentionally still spelled
# `RELATIVITYRENDER_ENABLE_OPTIX` for source-code stability
# (every `#ifdef RELATIVITYRENDER_ENABLE_OPTIX` in `src/optix/`,
# `src/main.cpp`, etc. is unaffected by the rename). The CMake
# option controls whether the C++ macro is defined; the two
# spellings serve different layers and need not match
# character-for-character.
#
# No OptiX SDK dependency, no `<optix.h>` include, no
# `find_package(OptiX)` - the placeholder sources contain only
# preprocessor macro checks. Subsequent 12B sub-stages add the
# remaining `src/optix/` files (OptixPrograms.cu / OptixSBT.h /
# OptixAccel.h / OptixLaunchParams.h per OPTIX_BACKEND_PLAN.md
# §20-§23) and the real OptiX SDK wiring.
if(RR_ENABLE_OPTIX)
    # Stage 12B.4: detect OptiX SDK path. The user passes
    # `-DOPTIX_ROOT=/path/to/optix-sdk` or
    # `-DOPTIX_SDK_DIR=/path/to/optix-sdk` at configure time; we
    # also fall back to the `OPTIX_ROOT` environment variable
    # (the canonical NVIDIA convention). Probe for
    # `include/optix.h` under each candidate; set
    # `RELATIVITYRENDER_OPTIX_SDK_FOUND` accordingly. This is
    # detection only - no `find_package(OptiX)`, no compile-time
    # propagation of the include path into `rr_optix`, no
    # OptixDeviceContext init. Subsequent 12B sub-stages consume
    # the detected path; today the placeholder rr_optix sources
    # don't include `<optix.h>` so a "not found" warning is
    # non-blocking.
    set(_optix_candidates "")
    if(OPTIX_ROOT)
        list(APPEND _optix_candidates "${OPTIX_ROOT}")
    endif()
    if(OPTIX_SDK_DIR)
        list(APPEND _optix_candidates "${OPTIX_SDK_DIR}")
    endif()
    if(NOT _optix_candidates AND DEFINED ENV{OPTIX_ROOT})
        list(APPEND _optix_candidates "$ENV{OPTIX_ROOT}")
    endif()

    set(RELATIVITYRENDER_OPTIX_SDK_FOUND        FALSE)
    set(RELATIVITYRENDER_OPTIX_SDK_INCLUDE_DIR  "")
    foreach(_path IN LISTS _optix_candidates)
        if(EXISTS "${_path}/include/optix.h")
            set(RELATIVITYRENDER_OPTIX_SDK_FOUND       TRUE)
            set(RELATIVITYRENDER_OPTIX_SDK_INCLUDE_DIR "${_path}/include")
            message(STATUS
                "  OptiX SDK    : ${_path} (include/optix.h located)")
            break()
        endif()
    endforeach()

    if(NOT RELATIVITYRENDER_OPTIX_SDK_FOUND)
        message(WARNING
            "RR_ENABLE_OPTIX=ON but no OptiX SDK could be located. "
            "Pass -DOPTIX_ROOT=/path/to/optix-sdk or "
            "-DOPTIX_SDK_DIR=/path/to/optix-sdk (or set the "
            "OPTIX_ROOT environment variable) so cmake can find "
            "'include/optix.h' under the SDK install. The current "
            "configure / build continues - Stage 12B.4 detection is "
            "non-blocking, and the rr_optix file skeleton compiles "
            "without the real SDK. Subsequent 12B sub-stages that "
            "need <optix.h>, optixInit(), etc. will fail to "
            "configure until this is resolved.")
    endif()

    unset(_optix_candidates)

    # Stage 17A.1: when the SDK was located, the OptiX context-init
    # path in `src/optix/OptixBackend.cpp` includes `<optix.h>`,
    # `<optix_function_table_definition.h>`, `<optix_stubs.h>`, and
    # `<cuda_runtime.h>`. Both the SDK include directory and the
    # CUDA Toolkit (for `cudaFree` / `cudaGetErrorString` and the
    # `CUDA::cudart` import library) need to be available.
    # `find_package(CUDAToolkit)` may already have run inside the
    # `RR_ENABLE_CUDA` branch above; if not, run it here.
    if(RELATIVITYRENDER_OPTIX_SDK_FOUND)
        if(NOT CUDAToolkit_FOUND)
            find_package(CUDAToolkit REQUIRED)
        endif()
    endif()

    add_library(rr_optix STATIC
        src/optix/OptixBackend.cpp
        src/optix/OptixAccel.cpp
        src/optix/OptixPipeline.cpp
        src/optix/OptixRenderer.cpp
        src/optix/OptixDenoiser.cpp
    )
    target_include_directories(rr_optix PUBLIC src)
    # Stage 18A.1: rr_optix consumes `rr::gpu::GpuTimer` +
    # `rr::gpu::format_gpu_timing_line` from rr_gpu when CUDA is
    # enabled (audit-host build keeps the link too; the no-op
    # fallback satisfies the symbol). PRIVATE - the timer is used
    # only inside OptixRenderer.cpp; the rr_optix public header
    # surface is unchanged.
    target_link_libraries(rr_optix PRIVATE rr_gpu)
    rr_apply_warnings(rr_optix)
    # PUBLIC so consumers of rr_optix see the same macro state
    # the library was compiled against. Today only OptixBackend.cpp
    # itself reads the macro; future sub-stages add CLI handlers
    # and other consumers that gate on it.
    target_compile_definitions(rr_optix PUBLIC RELATIVITYRENDER_ENABLE_OPTIX)
    # Stage 12B.5: propagate the SDK-found result from the
    # detection block above as a PUBLIC compile definition so
    # `OptixBackend::isSdkFound()` (and any future SDK-gated
    # call site) can answer at runtime. The macro is defined
    # only when `include/optix.h` was located; otherwise it is
    # undefined and `isSdkFound()` returns false.
    if(RELATIVITYRENDER_OPTIX_SDK_FOUND)
        target_compile_definitions(rr_optix PUBLIC RELATIVITYRENDER_OPTIX_SDK_FOUND)
        # Stage 17A.1: propagate the SDK include dir as PRIVATE
        # (only OptixBackend.cpp consumes `<optix.h>`; the public
        # rr_optix header surface stays free of SDK includes) and
        # link CUDA::cudart so the runtime symbols
        # `cudaFree` / `cudaGetErrorString` resolve.
        target_include_directories(rr_optix PRIVATE
            ${RELATIVITYRENDER_OPTIX_SDK_INCLUDE_DIR})
        target_link_libraries(rr_optix PRIVATE CUDA::cudart)

        # Stage 17A.3: compile src/optix/OptixPrograms.cu -> PTX
        # via nvcc, then run cmake/EmbedPtxAsHeader.cmake to wrap
        # the PTX text in a `static const char[]` symbol that
        # OptixPipeline.cpp #includes. The generated header lives
        # in the build tree (alongside other build-generated
        # artifacts); rr_optix's PRIVATE include dir picks it up.
        set(_optix_programs_cu
            ${CMAKE_CURRENT_SOURCE_DIR}/src/optix/OptixPrograms.cu)
        set(_optix_programs_ptx
            ${CMAKE_CURRENT_BINARY_DIR}/OptixPrograms.ptx)
        set(_optix_programs_h
            ${CMAKE_CURRENT_BINARY_DIR}/OptixPrograms_embedded_ptx.h)

        add_custom_command(
            OUTPUT  ${_optix_programs_ptx}
            COMMAND ${CUDAToolkit_NVCC_EXECUTABLE}
                    --ptx
                    -std=c++17
                    -O2
                    --use_fast_math
                    -I ${CMAKE_CURRENT_SOURCE_DIR}/src
                    -I ${RELATIVITYRENDER_OPTIX_SDK_INCLUDE_DIR}
                    -o ${_optix_programs_ptx}
                    ${_optix_programs_cu}
            DEPENDS ${_optix_programs_cu}
            COMMENT "[OptiX] Compiling OptixPrograms.cu -> PTX")

        add_custom_command(
            OUTPUT  ${_optix_programs_h}
            COMMAND ${CMAKE_COMMAND}
                    -DPTX_FILE=${_optix_programs_ptx}
                    -DOUTPUT_HEADER=${_optix_programs_h}
                    -DSYMBOL_NAME=g_optix_programs_ptx
                    -P ${CMAKE_SOURCE_DIR}/cmake/EmbedPtxAsHeader.cmake
            DEPENDS ${_optix_programs_ptx}
                    ${CMAKE_SOURCE_DIR}/cmake/EmbedPtxAsHeader.cmake
            COMMENT "[OptiX] Embedding PTX as C header")

        # Attach the generated header to rr_optix so the PTX
        # build runs whenever any rr_optix TU is rebuilt that
        # references the symbol.
        target_sources(rr_optix PRIVATE ${_optix_programs_h})
        target_include_directories(rr_optix PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endif()

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
    src/gpu/GpuTexture.cpp
    src/gpu/GpuTiming.cpp
)
target_include_directories(rr_gpu PUBLIC src)
# rr_gpu's GpuScene exposes Camera / Observer / RelativityParams /
# Sphere / MaterialParams / Light by value; PUBLIC-link those
# modules so consumers see the complete types via gpu/GpuScene.h.
# rr_texture is PUBLIC because GpuTexture's upload_from(ImageTexture&)
# / metadata accessors expose `rr::texture::ImageTexture` and
# `ImageTextureFormat` in its header.
target_link_libraries(rr_gpu PUBLIC rr_camera rr_relativity rr_geometry rr_material rr_lighting rr_texture)
rr_apply_warnings(rr_gpu)

if(RR_ENABLE_CUDA)
    target_sources(rr_gpu PRIVATE
        src/cuda/CudaContext.cpp
        src/cuda/CudaBuffer.cpp
        src/cuda/CudaTiming.cpp
        src/cuda/CudaTestKernel.cu
        src/cuda/CudaRngTestKernel.cu
        src/cuda/CudaAccumulation.cu
        src/cuda/CudaPathTracer.cu
        src/cuda/CudaTextureSampleTestKernel.cu
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
    rr_gpu rr_image rr_camera rr_geometry rr_relativity rr_material rr_lighting rr_texture rr_scene rr_io rr_renderer rr_server)
# Stage 12B.3: rr_optix is only created when OptiX support is
# requested (see the rr_optix block above), so the link must be
# conditional too. When OFF, the executable's link line is
# byte-identical to pre-12B builds.
if(RR_ENABLE_OPTIX)
    target_link_libraries(RelativityRender PRIVATE rr_optix)
endif()
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

    # relativity_tests (Stage 19E.1) covers the relativity math
    # leaf in `src/relativity/RelativityMath.h`: identity at
    # |beta| = 0; forward (parallel-to-beta) blueshift /
    # backward redshift against the closed-form longitudinal
    # Doppler factor; aberration angle against the analytic
    # cos(theta') = (cos(theta) - beta) / (1 - beta cos(theta))
    # relation; D finite + positive across |beta| < 1; clampBeta
    # behaviour; numerical stability at |beta| = 0.99 (incl. the
    # Stage 18A.3 precomputed-launch overload). RR_HD inlines
    # compile against the host compiler so the leaf is fully
    # exercisable on the audit host.
    add_executable(relativity_tests tests/relativity_tests.cpp)
    target_link_libraries(relativity_tests PRIVATE rr_relativity)
    rr_apply_warnings(relativity_tests)
    add_test(NAME relativity_tests COMMAND relativity_tests)

    # demo_tests (Stage 19E.2) covers the `--render-demo` action's
    # relativistic-perception layer end-to-end on the host: the
    # demo's exact camera / observer composition fed through
    # `generate_camera_ray` (RR_HD inline) and `dopplerFactor`
    # (RR_HD inline) reproduces the closed-form longitudinal
    # blueshift at the central pixel; beta = 0 produces D == 1
    # everywhere; beta = 0.7 produces D > 1 at center with
    # forward beaming (corner D < center D); the same input
    # twice produces bit-identical output (smoke-test
    # determinism); out-of-range |beta| is silently clamped to
    # <= 0.999999. Runs without CUDA.
    add_executable(demo_tests tests/demo_tests.cpp)
    target_link_libraries(demo_tests PRIVATE rr_camera rr_relativity)
    rr_apply_warnings(demo_tests)
    add_test(NAME demo_tests COMMAND demo_tests)

    # optix_tests (Stage 20D) covers the OptiX backend's host-
    # side surface: OptixBackend lifecycle (initialize / shutdown
    # / device_context / last_error), OptixGas default + move +
    # reset semantics, build_mesh_gas audit-host fallback (returns
    # ok=false with a documented error when the SDK is not
    # located OR the backend is not initialised), and the
    # OptixLaunchParams POD defaults (every Stage 17A.3-17A.5 +
    # 20B field defaults to a value that keeps existing OptiX
    # rendering programs producing byte-identical output). The
    # final test asserts the GAS-handle -> launch-params plumbing
    # matches what OptixRenderer::render_triangle does at runtime.
    #
    # Gated on RR_ENABLE_OPTIX (rr_optix only exists when the
    # option is ON). The test compiles + runs on the audit host
    # because every code path it exercises is host-side.
    if(RR_ENABLE_OPTIX)
        add_executable(optix_tests tests/optix_tests.cpp)
        target_link_libraries(optix_tests PRIVATE rr_optix)
        rr_apply_warnings(optix_tests)
        add_test(NAME optix_tests COMMAND optix_tests)
    endif()
endif()
