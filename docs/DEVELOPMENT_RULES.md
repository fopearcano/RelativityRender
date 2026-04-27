# RelativityRender — DEVELOPMENT RULES

These rules are normative. Every contribution must comply. They are derived
from `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` and refined for daily
work.

---

## 1. Identity Rules

1. RelativityRender is a serious GPU renderer platform. Treat it like one.
2. It is **not** a toy, a demo, a Cinema 4D plugin, or an Octane clone.
3. The relativistic camera/perception model is the differentiator. Do not let
   it become a side feature.

## 2. Engineering Rules

1. **Build incrementally.** One layer at a time, in the order defined in
   `docs/MILESTONE_ROADMAP.md`.
2. **Keep every step compilable.** A merged change must build cleanly. If a
   change cannot land in one step, split it.
3. **No fake stubs that pretend to be complete systems.** A stub that always
   returns `(1, 0, 1)` and is called "the material system" is forbidden. A
   minimal, honest implementation with a documented scope is fine.
4. **Do not jump ahead.** Do not start UI, the Cinema 4D bridge, the native
   C4D renderer, or the node editor before the prerequisites in the roadmap
   are met.
5. **No CPU ray tracing as a production path.** CPU may orchestrate, parse,
   load, upload, launch, receive, save, and serve. CPU may not produce final
   per-ray/per-pixel results.
6. **All per-pixel/per-ray work runs on GPU** (CUDA / OptiX).
7. **Update `docs/BUILD_PLAN.md` after every implementation step.** This is
   mandatory and tracked.
8. **Keep module boundaries clean.** See `docs/MODULE_MAP.md`. A change that
   violates layering is rejected, no matter how convenient.
9. **No monolithic files.** Split by responsibility. Header / source pairs
   per logical unit. Big files are a smell.
10. **Prefer explicit, testable interfaces.** Avoid hidden globals. Inject
    dependencies. Make things mockable at module boundaries.
11. **Do not overbuild a later system before the current layer works.** "We'll
    need this for the node editor someday" is not justification.

## 3. Dependency Rules

1. **Renderer core never depends on UI.** Modules 1–19 in `MODULE_MAP.md` must
   not `#include` or link anything from `tools/` or `bridges/`.
2. **Renderer core never depends on Cinema 4D.** Only `bridges/c4d_bridge/`
   and `bridges/c4d_native/` may link the C4D SDK.
3. **The Cinema 4D Bridge does not link renderer internals.** It talks to the
   renderer through the Scene File Format and the Renderer Server protocol.
4. **GPU backends are below the renderer algorithms**, not the other way
   around. Path tracer uses GPU backends; backends do not call into the path
   tracer.
5. **Math depends on nothing.** It is the leaf. Do not pull anything into it.
6. **No upward `#include`s.** If a lower layer needs to notify a higher layer,
   define an interface (callback / functor / virtual) owned by the lower
   layer. The higher layer implements it.
7. **CUDA Backend is below OptiX Backend.** OptiX may use CUDA. CUDA never
   uses OptiX.

## 4. Build / Repository Rules

1. Top-level directories make forbidden dependencies visible. CMake and
   `#include` paths reflect the layering.
2. Each module owns one top-level subdirectory under `src/`, `bridges/`, or
   `tools/`. Cross-module utilities go where they belong, not in a "common"
   dumping ground.
3. Generated files are out-of-source.
4. Third-party code lives under `third_party/` and is fetched or vendored
   deliberately. No drive-by additions.
5. CUDA / OptiX code is identified clearly (file extensions, separate
   compilation rules). Host-only code does not include CUDA-only headers.

## 5. GPU Code Rules

1. Math types must be `__host__ __device__`-callable where they make sense.
2. Kernels go in the backend or algorithm module that owns them, never in
   Math, Image, or Scene Graph.
3. Device memory is owned by typed wrappers (`gpu::Buffer<T>` /
   `cuda::DeviceBuffer<T>`), not raw `void*` outside the GPU layer.
4. Allocation churn during a render is avoided; allocate once per session.
5. Errors from CUDA / OptiX are converted into engine-level errors at the
   backend boundary. Higher layers do not handle raw `cudaError_t`.

## 6. Relativistic Model Rules

1. The relativistic camera is integrated into ray generation and shading. It
   is not a post-process.
2. Doppler shift and aberration must be derivable from a single observer
   4-velocity per camera. Don't fork the math across modules.
3. Relativistic AOVs (Doppler factor, observed direction, retarded time) are
   first-class and exposed through the AOV system.

## 7. Process Rules

1. **Read `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` at the start of
   every session.** Before any change.
2. **Identify which module/stage the prompt belongs to.** Do only that scope.
   Do not silently add future systems.
3. If a requested change violates the order in `MILESTONE_ROADMAP.md`,
   document the issue and implement only the safe prerequisite work.
4. **Always update `docs/BUILD_PLAN.md`** when a step lands.
5. **Tests live alongside the module they cover** under `tests/`. New
   functionality without a smoke test is incomplete.
6. **No silent rewrites.** If you discover the design is wrong, stop and
   raise it; do not refactor adjacent modules under the cover of a small task.
7. **Commits are scoped.** One module / one purpose per commit when possible.
8. **Branch discipline:** Develop on the branch named in the session
   instructions. Never push to a different branch without explicit permission.

## 8. Code Style Rules

1. C++ language level: pinned in the build (decided at L0 milestone). Do not
   mix standards within a module.
2. Public headers are minimal; implementation details live in the `.cpp` /
   `.cu` files.
3. Prefer composition over inheritance. Use polymorphism for genuine
   extension points (BSDFs, lights, cameras, integrators).
4. No exceptions across the GPU boundary; CUDA backend exposes
   `Result<T, Error>`-style returns or engine-level error types.
5. Comments explain *why*, not *what*. Names explain *what*.
6. Identifiers use the module's namespace (`math::`, `scene::`, `pt::`, ...).

## 9. Testing Rules

1. Math has unit tests for every operation that affects rendering correctness.
2. Image has tests for IO round-trips (EXR/PNG).
3. GPU Device Layer / CUDA Backend have at least one "kernel runs and writes
   the expected pattern" smoke test on a real device, gated by device
   availability.
4. Scene Graph and Scene File Format have round-trip tests.
5. Path Tracer has reference-image regression tests (small, fixed scenes,
   tolerance documented).
6. CI must build at minimum the host-only modules. GPU tests may be optional
   in CI but mandatory for local validation before merging.

## 10. What "Done" Means

A module step is **done** when:

1. It builds in isolation and as part of the whole project.
2. It has at least one test exercising its public surface.
3. It does not pull in any forbidden dependency (verified by inspection of
   includes / link list).
4. `docs/BUILD_PLAN.md` is updated with what landed and what's next.
5. Public headers and the relevant module entry in `docs/MODULE_MAP.md` are
   consistent (status flipped to `landed` or `in progress`).
