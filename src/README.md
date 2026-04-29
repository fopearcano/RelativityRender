# `src/` — RelativityRender source modules

This is the `relativity-core-v1` rewrite tree. Day-1 scope: **GPU
foundation only** — CUDA detection plus a single GPU gradient kernel.
Everything else is scaffold (an empty directory + README) and will be
repopulated in dedicated slices listed in `docs/REWRITE_STATUS.md`.

| Directory     | Day-1 status         | Notes |
|---------------|----------------------|-------|
| `core/`       | partial              | `Logger`, `Version` only |
| `math/`       | populated            | Vec2/3/4, Mat4, Transform, MathUtils (RR_HD foundation) |
| `image/`      | partial              | host `Image` + PPM writer; `Framebuffer` deferred |
| `gpu/`        | partial              | `GpuBuffer<T>`, `GpuDevice` |
| `cuda/`       | partial              | `CudaContext`, `CudaBuffer`, `CudaRenderer::render_gradient` |
| `scene/`      | scaffold (empty)     | comes back with a real renderer |
| `geometry/`   | scaffold (empty)     | comes back with intersection |
| `material/`   | scaffold (empty)     | comes back with shading |
| `lighting/`   | scaffold (empty)     | comes back with sampler support |
| `camera/`     | scaffold (empty)     | comes back with ray-gen |
| `relativity/` | scaffold (empty)     | comes back with relativistic kernel |
| `renderer/`   | scaffold (empty)     | integrator + AOVs land here |
| `io/`         | scaffold (empty)     | scene loader / writer |
| `server/`     | scaffold (empty)     | server v2 |

Hard rule (from the prototype audit, carried forward): the renderer
core MUST NOT depend on UI or Cinema 4D. See `docs/REWRITE_STATUS.md`
for the rewrite slice order.
