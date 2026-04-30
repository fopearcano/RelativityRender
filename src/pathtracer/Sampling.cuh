#pragma once

// CUDA-side re-export of the host+device sampling header. Same
// rationale as `RNG.cuh`: kernel TUs include this `.cuh` so future
// device-only specialisations can land here without churning
// every call site.
//
// Stage 11A: every entry point is RR_HD inline in `Sampling.h`;
// this file is a single re-export.
#include "pathtracer/Sampling.h"
