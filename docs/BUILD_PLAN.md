# Build Plan

Tracking doc per `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
("Update docs/BUILD_PLAN.md after every implementation"). Each entry
records what landed, in which stage, and the next concrete step.

## Current state

**Stage 1 — Core app.** Skeleton C++20 executable that prints its
version and a startup message. No GPU, no rendering, no scene system,
no server, no integrations.

### Files in scope

| File                       | Role                                                |
|----------------------------|-----------------------------------------------------|
| `CMakeLists.txt`           | Single executable, C++20, warnings on, no deps.     |
| `src/main.cpp`             | Entry point. Prints version + startup line.         |
| `src/core/Logger.h`        | `info` / `warning` / `error` static API.            |
| `src/core/Logger.cpp`      | Thread-safe stdio logger with timestamp + level.    |
| `src/core/Version.h`       | `kProjectName` / `kVersionMajor/Minor/Patch` / `kVersionString`. |

Build:

```sh
cmake -S . -B build
cmake --build build -j
build/bin/RelativityRender
```

Expected output:

```
[HH:MM:SS.mmm] [INFO] RelativityRender 0.1.0 starting up.
[HH:MM:SS.mmm] [INFO] Stage 1: core application skeleton. No GPU, no renderer, no scene yet.
```

## Stage history

### Stage 1 — Core app (current)

Status: implemented.

- Created `CMakeLists.txt` (~40 lines, one executable, C++20,
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

## Next stage

**Stage 2 — Math library.** Module 4 in the master order. Scope:
header-only RR_HD POD primitives — `Vec2`, `Vec3`, `Vec4`, `Mat4`,
`Transform`, `MathUtils.h` (with the `RR_HD` macro). Host tests
verify behaviour by construction so the same code works on the GPU
when the CUDA stage lands.

Deliverables (planned, NOT yet implemented):

- `src/math/Vec2.h`, `Vec3.h`, `Vec4.h`
- `src/math/Mat4.h`
- `src/math/Transform.h`
- `src/math/MathUtils.h`
- `tests/math_tests.cpp`
- CMake additions: a header-only `rr_math` interface library, a
  `math_tests` executable wired via `add_test()`.

No additional dependencies. The prototype's math headers (audited
clean, KEEP_AS_IS) are the source of the implementation.

## Constraints carried forward

From `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` — these apply
to every stage:

- Build incrementally. Keep every step compilable.
- No fake stubs. No empty scaffold dirs that pretend a system exists.
- No CPU per-pixel or per-ray work as the production path (will apply
  once rendering lands; documented up-front so it stays visible).
- Core modules never depend on Cinema 4D, UI, node editor, or any DCC.
- Update this file after every implementation.
