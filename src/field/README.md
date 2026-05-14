# src/field — Field Interpretation Layer

This directory is the source-level home of the **Field
Interpretation Layer** designed in
[`docs/FIELD_INTERPRETATION_LAYER.md`](../../docs/FIELD_INTERPRETATION_LAYER.md).
It is the *Phase 1* of the Manifold Core Pivot
([`docs/MANIFOLD_RENDERING_ARCHITECTURE.md`](../../docs/MANIFOLD_RENDERING_ARCHITECTURE.md)
§6): the **optional** perception-transcoding layer that maps
non-light fields into the renderer's visible chromatic /
luminous / volumetric / distortion channels.

## Status

**Skeleton stage (FIELD.1).** Header-only contract surfaces; no
renderer integration; no kernel implementations; no behavioural
change. The directory ships:

| File | Design-doc section | Role |
|------|--------------------|------|
| `FieldType.h`        | §3            | Enum naming the five field-type slots (Scalar, Vector, Tensor, Curvature, ProbabilityAmplitudePlaceholder). Only `Scalar` has a concrete sampler this slice. |
| `ScalarField.h`      | §3.1          | `ConstantScalarField` POD (uniform value + advisory `min_value`/`max_value` range) and `SampledScalarField` POD placeholder (`domain_min`/`domain_max` + `default_value` + range), plus `evaluate(field, Vec3)` / `evaluate(field, Vec4)` overloads for both types and `zero_constant_scalar_field()` / `zero_sampled_scalar_field()` factories. Promoted to its FIELD.2 shape. The sampled type's in-domain backend (texture / grid / procedural) is deferred. |
| `FieldMapping.h`     | §4            | `FieldOutputChannel` enum (six entries matching §4.1-§4.6) + `FieldMapping` POD wiring `input_type` -> `output_channel` with `strength` and `output_clamp` + `disabled_field_mapping()` factory. |
| `FieldInterpreter.h` | §6            | `FieldInterpreter` POD describing a Phase 1 module's metadata (`name`, `enabled`, `mapping`, `strength`) + `disabled_field_interpreter()` factory + `effective_strength(...)` helper. |

The default-constructed values for every shipping POD describe
the **disabled** state — the renderer emits zero Phase 1
contribution. Setting `enabled = true` on a `FieldInterpreter`
*and* a non-zero `strength` on both the interpreter and its
mapping is what wakes a module up; future implementation
slices will introduce real kernels behind that contract.

## CMake

The directory is exposed as the `rr_field` INTERFACE library,
mirroring `rr_manifold`'s skeleton-stage pattern. Links only
`rr_math` (for `Vec4` in the scalar-field sample function);
the design-doc §8 also permits a future read-only dep on
`rr_manifold` (chart / metric / observer-frame surface) and on
the existing `rr_image` / `rr_scene` libraries, but no real
kernel needs them yet so they are not linked at this slice.

## What is intentionally NOT here this slice

Per master rule #3 ("no fake stubs pretending to be complete
systems") and the non-goals enumerated in design-doc §7, the
directory deliberately does NOT contain:

- a `VectorField`, `TensorField`, `CurvatureField`, or
  amplitude-sampler header (the four reserved slots in
  `FieldType.h` are documented as "no sampler this slice");
- a texture-backed or procedural scalar-field implementation;
- an interpretation kernel for any output channel (§4 of the
  design doc names the channels; the kernels arrive with
  their own per-module slices);
- a renderer hook of any kind — `src/cuda/`, `src/optix/`,
  `src/pathtracer/`, `src/renderer/`, and the rest of the
  source tree are unaware of `rr_field`'s existence at this
  slice;
- a chart-region predicate (deferred until the Manifold Core
  ships a region descriptor);
- a Klein-Gordon / Schrödinger / Dirac evolver (design-doc
  §7 non-goal); the Phase 1 layer consumes static field
  data, it never solves field dynamics;
- an Einstein-field-equation solver (design-doc §7 non-goal);
- a CLI flag / scene-file entry for Phase 1 modules
  (deferred to FIELD.3+ per the design-doc §9 milestone
  order).

The directory exists today only so future slices have a clean
home; every type defined here is a real, complete data POD
with documented defaults, not a stub.

## Test coverage

This slice ships no dedicated ctest binary. The skeleton's
default-no-op contract is verified inline at build time via
the standalone `-Wall -Wextra -Werror` syntax checks run by
the slice's commit message; a future FIELD.x slice that adds
a real interpretation kernel will introduce a
`field_*_tests.cpp` ctest target wired into the existing
audit-host set.
