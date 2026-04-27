# `tests/` — Tests

Unit and integration tests for RelativityRender modules. Empty until M2
introduces the Core Engine, which is where the first tests will live.

Per `docs/DEVELOPMENT_RULES.md`:

- Math has unit tests for every operation that affects rendering correctness.
- Image / IO tests cover EXR / PNG round-trips.
- GPU Device Layer / CUDA Backend ship a "kernel runs and writes the
  expected pattern" smoke test gated by device availability.
- Scene Graph and Scene File Format have round-trip tests.
- Path Tracer ships reference-image regression tests with documented
  tolerances.
- CI builds at minimum the host-only modules. GPU tests may be optional
  in CI but mandatory for local validation before merging.

A module landing without at least one test exercising its public surface
is considered incomplete.
