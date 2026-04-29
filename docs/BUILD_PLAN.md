# Build Plan

Tracking doc per `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
("Update docs/BUILD_PLAN.md after every implementation"). Each entry
records what landed, in which stage, and the next concrete step.

## Current state

**Stages 1–2 — Core app + math library.** Skeleton C++20 executable
with logger, version constants, application config, command-line
handling, and a header-only RR_HD math library covering Vec2 / Vec3 /
Vec4 / Mat4 plus 60 host-side test assertions wired to ctest. No GPU,
no rendering, no scene system, no server, no integrations.

### Files in scope

| File                       | Role                                                |
|----------------------------|-----------------------------------------------------|
| `CMakeLists.txt`           | Executable + interface library + ctest wiring.      |
| `src/main.cpp`             | Entry point. Parses CLI, dispatches per action.     |
| `src/core/Logger.h`        | `info` / `warning` / `error` static API.            |
| `src/core/Logger.cpp`      | Thread-safe stdio logger with timestamp + level.    |
| `src/core/Version.h`       | `kProjectName` / `kVersionMajor/Minor/Patch` / `kVersionString`. |
| `src/core/Config.h`        | `Config` POD: `width` / `height` / `scene_path` / `output_path`, plus `validate()`. |
| `src/core/Config.cpp`      | `Config::validate()` returns first problem (positive dims) as a string. |
| `src/core/CommandLine.h`   | `CommandLine::parse(argc, argv) -> ParseResult { Action, Config, error_message }`, `usage(...)`, `version_string()`. |
| `src/core/CommandLine.cpp` | Hand-rolled flag parser. Action flags mutually exclusive; numeric / value validation; clean exit codes. |
| `src/math/MathUtils.h`     | `RR_HD` host/device portability macro; `min`/`max`/`clamp`/`lerp`/`saturate`/`radians`/`degrees`; `kPi`/`kTwoPi`/`kHalfPi`/`kEpsilon`. |
| `src/math/Vec2.h`          | RR_HD POD; `+`/`-`/scalar `*`/`/`, `dot`, `length`. |
| `src/math/Vec3.h`          | RR_HD POD; `+`/`-`/scalar `*`/`/`, Hadamard `*`, `dot`, `cross`, `length`, `length_squared`, `normalize` (zero on degenerate input), `clamp` (Vec3 + scalar bounds), `lerp`. |
| `src/math/Vec4.h`          | RR_HD POD; `+`/`-`/scalar `*`/`/`, `dot`, `xyz()` accessor, `(Vec3, w)` constructor. |
| `src/math/Mat4.h`          | Row-major 4x4; `identity` / `translation` / `scale` (snake_case to match codebase convention); `operator*`; `transform_point` / `transform_vector`. |
| `tests/math_tests.cpp`     | Stand-alone executable, hand-rolled `RR_CHECK` macro, 60 assertions covering every Vec3 + Mat4 capability the prompt called out. |

Build:

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
build/bin/RelativityRender                          # default startup banner
build/bin/RelativityRender --help
build/bin/RelativityRender --version
build/bin/RelativityRender --device-info
build/bin/RelativityRender --render scene.rrscene --output out.png --width 1920 --height 1080
```

### CLI behavior (Stage 1)

| Invocation                                                                    | Output / exit code |
|-------------------------------------------------------------------------------|--------------------|
| (no args)                                                                     | startup banner via `Logger::info`, exit 0. |
| `--help`                                                                      | usage text on stdout, exit 0. |
| `--version`                                                                   | `RelativityRender 0.1.0` on stdout, exit 0. |
| `--device-info`                                                               | `[INFO] GPU device info not implemented yet`, exit 0. |
| `--render <scene>` (with optional `--output / --width / --height`)            | `[INFO] render command received`, exit 0. (No actual rendering.) |
| Unknown flag, missing value, non-numeric `--width / --height`, non-positive dimensions, two action flags | `[ERROR] <reason>` then usage on stderr, exit 2. |

## Stage history

### Stage 1.1 — Core app skeleton

Status: implemented.

- Created `CMakeLists.txt` (one executable, C++20,
  `-Wall -Wextra -Wpedantic` / `/W4 /permissive-`).
- Created `src/main.cpp` (no GPU; logs project name + version + a
  Stage-1 status line, exits 0).
- Reused `src/core/Logger.{h,cpp}` and `src/core/Version.h` from the
  prototype — both clean per the prior audit, both already supply the
  exact API Stage 1 requires.

The branch was scoped back from an earlier "GPU foundation" attempt
that landed Stage 6 work (CUDA device layer) before Stages 4 (math
library) and 5 (image / framebuffer system). Per the master
instructions' module order and the rule "Do not overbuild a later
system before the current layer works", the GPU layer was removed
from the branch and will be reintroduced in its proper stage.

### Stage 1.2 — Config + command-line handling

Status: implemented.

- Added `src/core/Config.{h,cpp}`. Plain aggregate carrying the four
  Stage-1 knobs (`width`, `height`, `scene_path`, `output_path`) plus
  a `validate()` method that returns the first problem string (empty
  on success).
- Added `src/core/CommandLine.{h,cpp}`. Pure host parser; depends only
  on `core/Config.h` and `core/Version.h`. `parse(argc, argv)` returns
  a `ParseResult { Action, Config, error_message }`. Action flags
  (`--help`, `--version`, `--device-info`, `--render <scene>`) are
  mutually exclusive; configuration flags (`--output <path>`,
  `--width <int>`, `--height <int>`) are accepted alongside any
  action.
- Rewrote `src/main.cpp` to dispatch on `result.action`. Each action
  produces exactly the Stage-1 behaviour: usage on stdout for
  `--help`, version on stdout for `--version`, the placeholder
  `"GPU device info not implemented yet"` for `--device-info`, the
  literal `"render command received"` for `--render`, and the
  startup banner for the no-args default.
- Added the two new `.cpp` files to `CMakeLists.txt`.
- All five behaviours plus five error paths (unknown flag, missing
  value, non-numeric integer, non-positive dimension, combined
  actions) verified manually; error paths return exit code 2.

No new dependencies. No GPU, no rendering, no scene parser, no
server, no C4D — same as Stage 1.1.

### Stage 2 — Math library

Status: implemented.

- Recovered the five math headers from the `prototype_v0` tag (each
  was classified KEEP_AS_IS by the prior audit and supplies exactly
  the surface the prompt requires):
  `src/math/MathUtils.h`, `Vec2.h`, `Vec3.h`, `Vec4.h`, `Mat4.h`.
- The `RR_HD` macro in `MathUtils.h` expands to `__host__ __device__`
  under nvcc and is empty otherwise, so the same headers will be
  usable from CUDA kernels in a later stage without modification.
- `Mat4` keeps the codebase's snake_case naming for member operations
  (`Mat4::identity` / `translation` / `scale` static factories,
  `transform_point` / `transform_vector` free functions). The Stage-2
  prompt named these `transformPoint` / `transformVector` informally;
  using snake_case keeps the math library consistent with the rest of
  the public surface (`Logger::info`, `Config::validate`,
  `CommandLine::parse`, ...) and with the audit's naming-consistency
  finding.
- Added `tests/math_tests.cpp`. The project does not yet have a
  third-party test framework, so this is a small self-contained
  executable: hand-rolled `RR_CHECK` macro, 60 assertions covering
  every Vec3 + Mat4 capability the prompt called out (construction,
  arithmetic, dot, cross, length / length_squared, normalize incl.
  degenerate input, clamp incl. both overloads, lerp, identity,
  translation / scale point-vs-vector semantics, multiplication +
  composition order). main() returns 0 on full pass, 1 otherwise.
- CMake additions: a header-only `rr_math` INTERFACE library
  (publishes `src/` as include path), an `RR_BUILD_TESTS` option (ON
  by default), an `rr_apply_warnings()` helper to keep warning flags
  centralised, an `enable_testing()` block, and a `math_tests`
  executable wired through `add_test(NAME math_tests COMMAND
  math_tests)`.

Verified: `cmake --build build -j` clean (no warnings under
`-Wall -Wextra -Wpedantic`); `ctest` reports `1/1 passed`; running
`math_tests` directly prints `math_tests: 60 / 60 passed`. The main
`RelativityRender` binary is unchanged — math is not yet consumed by
core code.

No new dependencies. No GPU, no rendering, no scene parser, no
server, no C4D — same as Stages 1.1 and 1.2.

## Next stage

**Stage 3 — Image / framebuffer system.** Module 5 in the master
order. Scope: a host-side floating-point framebuffer + simple PPM
writer so later stages have somewhere to download GPU pixels into.

Deliverables (planned, NOT yet implemented):

- `src/image/Color.h`     (Rgb / Rgba PODs)
- `src/image/Image.h`     (host-side float framebuffer)
- `src/image/Image.cpp`   (pixel write + PPM IO)
- `tests/image_tests.cpp` (assertions on basic ops + PPM round-trip)
- CMake additions: a real `rr_image` static library + an
  `image_tests` test target, plus `rr_apply_warnings()` applied to
  both.

Allowed CPU work (per the master engineering rule that bans CPU
ray-tracing): pixel writes for clearing / debug fills / IO
validation. No per-ray work.

## Constraints carried forward

From `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` — these apply
to every stage:

- Build incrementally. Keep every step compilable.
- No fake stubs. No empty scaffold dirs that pretend a system exists.
- No CPU per-pixel or per-ray work as the production path (will apply
  once rendering lands; documented up-front so it stays visible).
- Core modules never depend on Cinema 4D, UI, node editor, or any DCC.
- Update this file after every implementation.
