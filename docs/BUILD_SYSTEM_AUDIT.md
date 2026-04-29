# Build System Audit

Status: audit step 6. Focused only on CMake, CUDA
integration, reproducibility, file organisation,
debug/release support. Single 565-line `CMakeLists.txt`
at the project root, no per-module `CMakeLists.txt`.

## 1. Five-question summary

| #   | Question                              | Answer                                                                |
|-----|---------------------------------------|-----------------------------------------------------------------------|
| 1   | Is CMake clean?                       | **Mostly.** Explicit sources (no GLOBs); 12 per-module libraries; clean link direction. Repetitive boilerplate (warnings + per-target include dirs) is the main hygiene issue. |
| 2   | Is CUDA integration correct?          | **Yes.** `find_package(CUDAToolkit)` + `enable_language(CUDA)` gated on `RR_ENABLE_CUDA`. `CMAKE_CUDA_ARCHITECTURES` defaulted to Turing-Ada. `RR_HAS_CUDA` propagates to consumers. |
| 3   | Is the build reproducible?            | **Yes** for a given option set. Configure is deterministic; no timestamps / glob; OptiX path is env-driven but documented. |
| 4   | Is file organisation logical?         | **Mostly.** Per-module library + test executable; sources listed explicitly. Single-file CMakeLists is large; per-module files would help navigation. |
| 5   | Is debug vs release possible?         | **Partially.** Both configs work, but `CMAKE_BUILD_TYPE` is not defaulted - unset means unoptimised on single-config generators, which is misleading for kernel work. |

## 2. CMake hygiene

`cmake_minimum_required(VERSION 3.20)` and the project
declaration are at the top. C++20 + CUDA 17 are pinned
project-wide.

What's clean:

- **Explicit source lists.** Every `add_library` /
  `add_executable` enumerates files by name; no
  `file(GLOB ...)` anywhere. Adding / removing a
  source file is a deliberate edit, not an
  invisible build-graph change.
- **Per-module static libraries.** 12 `add_library`
  calls (one per module): `rr_image`, `rr_camera`,
  `rr_geometry`, `rr_material`, `rr_lighting`,
  `rr_texture`, `rr_io`, `rr_scene`, `rr_renderer`,
  `rr_gpu`, `rr_optix`, `rr_server`. Each carries the
  same `target_include_directories(... PUBLIC src)`
  + a `target_compile_options` for warnings.
- **Linear link direction.** `target_link_libraries`
  always declares upstream PUBLIC dependencies; no
  reverse links, no layer-skipping. This is what the
  architecture audit (step 5) called out as the
  build's strongest property.

What's not clean:

- **Repetitive boilerplate.** The pattern
  `if(MSVC) /W4 /permissive- else() -Wall -Wextra
  -Wpedantic endif()` appears **18 times** (12 libs +
  6 test execs), each time identically. A single
  `function(rr_apply_warnings target)` would collapse
  it.
- **No `add_subdirectory`.** Everything is in one
  565-line root file. As the project grows, navigation
  + per-module ownership would benefit from each
  module owning its own `CMakeLists.txt`.
- **`core/` is not a library.** Its three sources
  (`Logger.cpp`, `Config.cpp`, `CommandLine.cpp`) are
  added directly into the `RelativityRender`
  executable's source list. Inconsistent with the
  module-as-library pattern; if a future test wanted
  to test `Logger`, it would need a separate compile
  rather than linking `rr_core`.
- **Test target boilerplate.** Each of the 18
  `tests/*.cpp` files gets the same five-line
  pattern: `add_executable + target_link_libraries
  + target_compile_options + add_test`. A
  `function(rr_add_test ...)` would compress to one
  line per test.
- **Hand-written `target_include_directories(... PUBLIC
  src)` on every library** repeats the project's
  include root. A top-level
  `include_directories(${CMAKE_SOURCE_DIR}/src)` plus
  PRIVATE per-target dirs would be cleaner; or
  better, an INTERFACE library `rr_includes` every
  target picks up.

None of these are blocking. They are scaling concerns
the rewrite should address as the codebase grows.

## 3. CUDA integration

Correct in shape; minor polish opportunities.

What's right:

- **Option-gated.** `RR_ENABLE_CUDA` defaults `OFF`.
  When OFF, no CUDA toolchain is required;
  `find_package(CUDAToolkit)` and
  `enable_language(CUDA)` are skipped. Host-only
  builds work on machines with no CUDA installed.
- **`RR_HAS_CUDA` propagates correctly.** Defined as
  a `target_compile_definitions(rr_gpu PUBLIC ...)`
  - so every consumer of `rr_gpu` (the executable,
  `gpu_tests`) sees it. Source code uses
  `#ifdef RR_HAS_CUDA` to gate CUDA-runtime calls.
- **CUDA sources added conditionally.** When
  `RR_ENABLE_CUDA` is ON, `target_sources(rr_gpu
  PRIVATE ...)` adds the four `.cu` / `.cpp` files
  for the CUDA backend. Same library name, different
  contents per option - reproducible per option.
- **Architecture default reasonable.**
  `CMAKE_CUDA_ARCHITECTURES "75;80;86;89"` covers
  Turing through Ada. Override via
  `-DCMAKE_CUDA_ARCHITECTURES=...` per the inline
  comment.
- **`CUDA::cudart` linked PRIVATE** to `rr_gpu`. The
  CUDA Runtime is an implementation detail; consumers
  don't see it.
- **`POSITION_INDEPENDENT_CODE ON`** on `rr_gpu` when
  CUDA is on. Necessary for the eventual shared-
  library / plugin path (M23 native renderer).

What's missing:

- **No Hopper / Blackwell** in the default
  architectures list. Pre-90 (Hopper) hardware would
  fall back to JIT-compile from PTX or fail. Cheap
  fix: add 90 (Hopper) and 100 (Blackwell) when
  shipping.
- **No `--use_fast_math` / `-lineinfo` / kernel
  optimisation flags.** Every kernel compiles with
  defaults. Production renderers usually pin
  `-lineinfo` (for profiler integration) and
  consider `-O3 --use_fast_math` (with the
  determinism trade-off explicit). v1 leaves this
  to the toolchain default.
- **Stream argument hardcoded to `nullptr`.** Not a
  CMake issue per se, but the CMake doesn't gate
  the existing stream parameter on a config flag.
  Future progressive workflow needs a stream-pool
  option.

## 4. Reproducibility

For a given `(option set, compiler, OS, CUDA Toolkit
version)` tuple, the configure step is deterministic.

What's right:

- **No timestamps in build artifacts.** No
  `string(TIMESTAMP ...)` calls; no
  `__DATE__` / `__TIME__` in the source.
- **No globbed source lists.** A new file under
  `src/foo/` only enters the build when explicitly
  added.
- **No `find_package` against unspecified versions.**
  Only `CUDAToolkit REQUIRED` (when CUDA is on),
  which the toolchain version pins.
- **OptiX path is env-driven but documented.**
  `$ENV{OPTIX_INSTALL_DIR}` is checked; the
  `find_path` HINTS list adds standard install
  locations. A user-supplied `-DOPTIX_INSTALL_DIR=...`
  is the canonical override. When the SDK isn't
  found, configure errors with a clear message:
  > `"RELATIVITYRENDER_ENABLE_OPTIX is ON but optix.h was not found."`

What could drift:

- **`CMAKE_CUDA_ARCHITECTURES`** is a shared variable
  that doesn't get re-set if the user's environment
  has it. Today's `if(NOT DEFINED ...)` check is
  correct. Verified.
- **Compiler version.** No `CMAKE_CXX_COMPILER_VERSION`
  guard. A C++20-incompatible compiler would fail
  later in the build with confusing errors; an
  early `if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS
  ...)` check + `message(FATAL_ERROR ...)` would
  be friendlier.
- **No vendor lockfile / CMake-fetch pinning.** The
  project has no third-party deps today
  (`third_party/` is an empty placeholder), so
  there's nothing to lock. Once nlohmann's JSON or
  another dep enters (per the architecture audit's
  recommendation for `io/`), `FetchContent` with a
  pinned commit hash is the way.

## 5. File organisation

Layout maps cleanly to the 12-module decomposition.
The single CMakeLists.txt's structure mirrors the
linker dependency order: leaf libraries first, then
upward. A reader can scan top-to-bottom and see
exactly what depends on what.

What works:

- **Sources alongside their module.** No `src/all/`
  or `src/everything/` dump.
- **Tests track modules.** `tests/<module>_tests.cpp`
  for every module, plus the bridge / server / AOV /
  graph variants.
- **CUDA `.cu` files live alongside their module's
  C++ files.** Production kernels in
  `src/cuda/CudaTestKernel.cu` (plus the four other
  `.cu` / `.cuh` files). The CUDA-side mirrors of
  host PODs follow a `Cuda<Type>.cuh` naming
  convention.

What doesn't:

- **The single root CMakeLists.txt is 565 lines.**
  Per-module `CMakeLists.txt` files invoked via
  `add_subdirectory(src/foo)` would let each module
  own its own build description. The root file would
  collapse to ~100 lines (project setup +
  add_subdirectory list + executable / tests).
- **`tools/` and `third_party/` are empty
  placeholders** with only README.md inside. They
  show up in `find` but contribute no build targets.
  A first-shipping rewrite would either populate
  them (with the demo binaries the architecture
  audit calls for) or remove the placeholders until
  they have content.

## 6. Debug vs release

Both configurations build correctly; the gap is in
defaulting + visibility.

What works:

- **Release build supported.** A
  `cmake -B build -DCMAKE_BUILD_TYPE=Release` configure
  + `cmake --build build` succeeds; produces optimised
  binaries.
- **Debug build supported.**
  `cmake -B build -DCMAKE_BUILD_TYPE=Debug` succeeds;
  produces debuggable binaries with assertions on.
- **Multi-config generators (Visual Studio, Xcode).**
  CMake handles per-target debug / release /
  RelWithDebInfo / MinSizeRel via the generator's
  native config switch. Nothing in the project's
  CMakeLists overrides per-config flags.

What's missing:

- **No `CMAKE_BUILD_TYPE` default.** A user running
  `cmake -B build` (no `-DCMAKE_BUILD_TYPE`) on
  Ninja / Unix Makefiles gets an empty build type:
  no `-O2`, no `-DNDEBUG`, no debug symbols. The
  resulting binary is an unoptimised
  no-asserts-defined no-symbols hybrid. This
  matters for kernel work: an unoptimised
  `__global__` is **substantially** slower than a
  `-O3` one.
  Cheap fix: top-of-file `if(NOT CMAKE_BUILD_TYPE
  AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING ... ) endif()`.
- **No `RelWithDebInfo` documented.** Most kernel
  development happens against `RelWithDebInfo`
  (optimised + symbols + lineinfo for profiler).
  Project's docs don't mention it; defaulting to
  `Release` would still be the safer choice for
  shipping.
- **No CUDA-side debug switch.** `nvcc -G` (device
  debug symbols) is not gated by a CMake option.
  Today every CUDA build uses release device code
  even if the host build is Debug. v1 doesn't need
  device debugging; the rewrite should add a
  `RR_CUDA_DEVICE_DEBUG` option that emits
  `-G -lineinfo` when ON.

## 7. Implications for the rewrite

Short:

- **Keep the build's shape.** Per-module static
  libraries with explicit source lists, option-
  gated CUDA / OptiX, `RR_HAS_CUDA` macro
  propagation, leaf-first link order. All of this
  is right.
- **Set `CMAKE_BUILD_TYPE` default to `Release`** so
  unspecified configures produce optimised builds.
- **Split the CMakeLists** across per-module files
  invoked from a small root. 565 lines becomes
  ~100 + 12 small ones.
- **Collapse the warning + test boilerplate** into
  two helper functions
  (`rr_apply_warnings(target)`,
  `rr_add_test(name, ...)`).
- **Promote `core/` to a library** (`rr_core`) for
  consistency with every other module.
- **Add Hopper + Blackwell** to the default CUDA
  architectures.
- **Add a CUDA device-debug option** for kernel
  work; pin `-lineinfo` for profiler integration.
- **Add a compiler-version guard** for the C++20
  requirement.

None blocking. The build is **functional and
reproducible today**; the rewrite tightens it.
