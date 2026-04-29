# `integrations/c4d/` — Cinema 4D integration

**Status (relativity-core-v1, day-1): not started.**

This directory exists as scaffold only. No Cinema 4D code lives here in
the current branch. The prototype's M19 Python bridge is preserved on
the frozen `prototype_v0` tag; the rewrite skips the Python bridge and
goes directly to a native C++ `VideoPostData` plugin in a much later
slice (after the GPU renderer, OptiX path, and denoiser are in).

This is the only place in the repository that will be allowed to link
the Cinema 4D SDK. Forbidden imports: anything else under `src/`,
anything under `tools/`.
