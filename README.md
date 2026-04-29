# RelativityRender

A serious GPU renderer platform with a unique relativistic camera /
perception model. CUDA / OptiX-first, designed around GPU path tracing
and ray-level relativistic perception (aberration, Doppler color
shift, searchlight beaming).

## Status

**Stage 1 — Core app.** The repository currently contains only the
skeleton C++20 application: an executable that prints its version and
a startup line. No GPU, no renderer, no scene system, no server.
Subsequent stages add capability in the order documented in
[`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`](RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt).

The current and next concrete steps live in
[`docs/BUILD_PLAN.md`](docs/BUILD_PLAN.md).

## Build

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

## Documentation

1. [`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`](RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt) — top-level rules and stage order.
2. [`docs/BUILD_PLAN.md`](docs/BUILD_PLAN.md) — current build state and next concrete step.
3. [`docs/MASTER_ARCHITECTURE.md`](docs/MASTER_ARCHITECTURE.md) — long-term layered architecture.
4. [`docs/MILESTONE_ROADMAP.md`](docs/MILESTONE_ROADMAP.md) — long-term milestones.
5. [`docs/DEVELOPMENT_RULES.md`](docs/DEVELOPMENT_RULES.md) — engineering, dependency, GPU, process rules.

## Layout

```
RelativityRender/
  CMakeLists.txt                           # ~40 lines, one executable
  README.md                                # this file
  RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt
  docs/                                    # rules, roadmap, architecture, build plan
  src/
    main.cpp
    core/
      Logger.h
      Logger.cpp
      Version.h
```
