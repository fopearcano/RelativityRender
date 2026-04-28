#pragma once

#include "math/MathUtils.h"  // RR_HD

#include <cstdint>

// Tiny per-thread pseudo-random number generator for the path
// tracer. PCG-XSH-RR (64-bit state, 32-bit output) - small, fast,
// well-distributed, and deterministic given the seed inputs. The
// kernel seeds one `RNG` per (pixel, sample) tuple so each pixel
// path is reproducible and independent.
//
// Header-only and `RR_HD inline`, so the same code runs in host
// tests and CUDA kernels. No path-tracing integration yet; this
// is the foundation M14 will consume.

namespace rr::pathtracer {

struct RNG {
    std::uint64_t state = 0;
};

// Wang-style 32-bit hash. Spreads small inputs (pixel coords,
// sample index) across the 64-bit state space so two adjacent
// pixels with identical sample indices don't share streams.
RR_HD inline std::uint32_t wang_hash(std::uint32_t v) {
    v = (v ^ 61u) ^ (v >> 16u);
    v *= 9u;
    v = v ^ (v >> 4u);
    v *= 0x27d4eb2du;
    v = v ^ (v >> 15u);
    return v;
}

// Per-pixel RNG seed. Each `(x, y, sample)` triple yields a
// distinct deterministic stream. The fixed magic constants are
// arbitrary "decorrelation salts" - their exact values are not
// load-bearing as long as `wang_hash` produces well-spread
// outputs from them.
RR_HD inline RNG make_rng(std::uint32_t x,
                          std::uint32_t y,
                          std::uint32_t sample) {
    const std::uint64_t a = wang_hash(x ^ 0x4d4e6f6cu);
    const std::uint64_t b = wang_hash(y ^ 0x9e3779b9u);
    const std::uint64_t s = wang_hash(sample ^ 0xa5af3a17u);

    RNG rng;
    rng.state = (a << 32) | b;
    rng.state ^= (s | 1ull);                 // keep state odd / non-zero
    if (rng.state == 0ull) {
        rng.state = 0xcafef00dd15ea5e5ull;   // any non-zero fallback
    }
    return rng;
}

// Advance the state and emit a 32-bit pseudo-random integer.
// Standard PCG-XSH-RR.
RR_HD inline std::uint32_t next_uint(RNG& rng) {
    const std::uint64_t old_state = rng.state;
    rng.state = old_state * 6364136223846793005ull + 1442695040888963407ull;

    const std::uint32_t xor_shifted = static_cast<std::uint32_t>(
        ((old_state >> 18u) ^ old_state) >> 27u);
    const std::uint32_t rot = static_cast<std::uint32_t>(old_state >> 59u);
    return (xor_shifted >> rot)
         | (xor_shifted << ((-static_cast<std::int32_t>(rot)) & 31));
}

// Uniform float in [0, 1). Uses the top 24 bits of the next
// integer so the value fits exactly into a single-precision
// mantissa with no rounding bias.
RR_HD inline float next_float(RNG& rng) {
    const std::uint32_t u = next_uint(rng);
    return static_cast<float>(u >> 8u) * (1.0f / 16777216.0f);  // 1 / 2^24
}

}
