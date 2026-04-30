#pragma once

#include "math/MathUtils.h"  // RR_HD
#include "math/Vec2.h"

#include <cstdint>

namespace rr::pathtracer {

// Per-pixel pseudo-random number generator state.
//
// Stage 11A surface: a 64-bit PCG-XSH-RR-64-32 state. PCG32 is the
// standard GPU-path-tracer RNG: 8 bytes per thread, integer
// arithmetic only (no LUTs, no branching), strong statistical
// properties, and trivially RR_HD. The state is the only field
// the consumer must persist between samples.
//
// The struct is a plain aggregate so it can be passed by value to
// kernels and stored as part of larger per-pixel POD bundles
// (`PathState`, `RayPayload`, ...) without ABI surprises.
struct Rng {
    std::uint64_t state = 0;
};

// One step of the PCG-XSH-RR-64-32 generator. Returns 32 random
// bits; consumers usually call `next_float` / `next_vec2` rather
// than this directly. Mutates `r` in place. Marked RR_HD so the
// same code runs on host (tests) and device (kernels).
RR_HD inline std::uint32_t pcg32_next(Rng& r) {
    const std::uint64_t old = r.state;
    // PCG32 LCG step. Multiplier + increment from the reference
    // implementation; using the default stream constant
    // (0xDA3E39CB94B95BDB) folded into the increment is equivalent
    // for a single-stream RNG.
    r.state = old * 6364136223846793005ULL + 1442695040888963407ULL;

    // XSH-RR output function: xorshift then random rotation. The
    // result is decorrelated from the next state.
    const std::uint32_t xorshifted =
        static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
    const std::uint32_t rot = static_cast<std::uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

// Splittable seed mixer. SplitMix64 is the standard 64-bit
// avalanche hash for spreading a small key (pixel coords, frame
// index, global seed) across the full PCG state space.
RR_HD inline std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x  = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x  = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// Build a per-pixel `Rng` whose stream is uncorrelated with the
// streams of adjacent pixels and adjacent frames. The four inputs
// are mixed through SplitMix64 to seed the PCG state, then PCG is
// stepped once so neighbouring seeds don't show the LCG's initial
// low-bit grid pattern. Stage 11A's caller is the noise-test
// kernel; the path tracer (master module 16) reuses this entry
// point for primary-ray seeding.
RR_HD inline Rng make_pixel_rng(std::uint32_t pixel_x,
                                std::uint32_t pixel_y,
                                std::uint32_t frame_index,
                                std::uint64_t global_seed) {
    // Pack the four inputs into a 64-bit key. The shifts and XORs
    // below are chosen so swapping x <-> y, x <-> frame, etc.,
    // produces distinct keys; SplitMix64 then avalanches each bit.
    const std::uint64_t key =
          global_seed
        ^ (static_cast<std::uint64_t>(pixel_x)     << 32u)
        ^ (static_cast<std::uint64_t>(pixel_y)     << 16u)
        ^ (static_cast<std::uint64_t>(frame_index)       );

    Rng r;
    r.state = splitmix64(key);
    // Burn one step. PCG's first output from an unmixed seed can
    // share low bits with neighbours; one step decorrelates them
    // while keeping the seed function deterministic + cheap.
    (void)pcg32_next(r);
    return r;
}

// Uniform random float in [0, 1). The output uses 24 random bits
// (the float significand width) so consecutive samples are
// uniformly distributed across the representable values - the top
// 8 bits are dropped because they would round-trip through
// float-to-uint scaling unevenly.
RR_HD inline float next_float(Rng& r) {
    const std::uint32_t bits = pcg32_next(r);
    // Shift to 24 bits, then divide by 2^24. Multiplying by the
    // pre-computed reciprocal avoids a div on the device.
    constexpr float kInv2to24 = 1.0f / 16777216.0f;  // 1.0 / (1 << 24)
    return static_cast<float>(bits >> 8u) * kInv2to24;
}

// Uniform random Vec2 in [0, 1)^2. Calling `next_float` twice
// keeps the per-component distribution identical to a 1D draw
// (the alternative - splitting one 32-bit draw into two 16-bit
// halves - costs precision the hemisphere samplers care about).
RR_HD inline rr::math::Vec2 next_vec2(Rng& r) {
    const float x = next_float(r);
    const float y = next_float(r);
    return rr::math::Vec2{x, y};
}

}  // namespace rr::pathtracer
