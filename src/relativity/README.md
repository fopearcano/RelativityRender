# `src/relativity/` — Relativistic Camera / Perception Model

**Layer:** L4. **Milestone:** M9. **Status:** not started.

The differentiator. Lorentz boost on ray directions, relativistic
aberration, Doppler color shift, searchlight / headlight beaming,
retarded-time approximation, observer 4-velocity, the relativistic ray
transformation pipeline.

Integrated at primary-ray generation and during shading where Doppler /
aberration affect light transport — **not** as a post-process. The path
tracer uses this module; this module does not know the path tracer
exists.

See `docs/MODULE_MAP.md` for the authoritative contract.
