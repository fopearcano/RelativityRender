#pragma once

#include "material/graph/Graph.h"
#include "material/graph/Node.h"
#include "math/Vec3.h"

// Material graph CPU REFERENCE evaluator.
//
// IMPORTANT: this evaluator runs entirely on the CPU and is for
// **testing / verification only**. Per the project's engineering
// rules (`docs/DEVELOPMENT_RULES.md`, "no CPU ray tracing as
// production path"), the renderer's hot path NEVER calls into
// this code; it exists so the test harness and any future
// authoring tool can pull a colour-summary out of a graph
// without spinning up the GPU.
//
// What "evaluate" means here:
//
//   - For non-terminal nodes (`ConstantColor`, `Add`, `Multiply`,
//     `TextureSample`), the result is the colour the node's
//     output socket would carry if the graph were live.
//   - For terminal nodes (`DiffuseBSDF`, `Emission`), terminals
//     have no output sockets in the data layer; for evaluator
//     purposes we project the terminal onto a single "colour
//     summary" that downstream tools can compare and print:
//       `DiffuseBSDF` -> the resolved `albedo` colour.
//       `Emission`    -> the resolved `color` * the immediate
//                        `strength` (Emission's `strength` input
//                        accepts only Float and v1 has no
//                        Float-producing node, so the strength
//                        always comes from the node's own
//                        `scalar_value`).
//
// The evaluator walks the graph recursively from `node_id`
// backwards through connections, resolving each input either
// from a connected source node or from the input's catalogue
// default. There is intentionally no memoisation across calls -
// this is a reference implementation; correctness, not
// performance, is the contract.
//
// Cycles: the evaluator assumes the input graph has been
// validated (`validate_graph`) and is a DAG. As a safety net
// against accidental misuse (and against bugs in callers that
// build cyclic graphs without validating first), the recursion
// is capped at `EvaluationContext::max_depth` levels; on
// reaching the cap the evaluator returns black and stops.
//
// Per the prompt's "constant fallback color" requirement,
// `TextureSample` returns `EvaluationContext::fallback_texture_color`
// without ever invoking a sampler. The default is opinionated
// magenta - the standard "missing texture" debug colour - so a
// TextureSample-driven branch is visually obvious in any test
// that prints the result.

namespace rr::material::graph {

struct EvaluationContext {
    // Returned by `TextureSample` regardless of `texture_id` /
    // UV. The default is magenta so a chain involving a
    // TextureSample is visible at a glance in test output.
    rr::math::Vec3 fallback_texture_color = {1.0f, 0.0f, 1.0f};

    // Recursion safety net for malformed graphs. The evaluator
    // stops descending and returns black when the recursion
    // depth exceeds this bound. v1 graphs after validation
    // never approach the cap.
    int max_depth = 256;
};


// Evaluate `node_id` in `graph` and return its colour summary.
// See file-level comment for per-node semantics.
//
// Pre-conditions:
//   - `graph` SHOULD have been validated. Behaviour on a graph
//     that contains cycles is bounded (the depth cap aborts
//     the recursion) but returns garbage; callers that care
//     about correctness validate first.
//   - `node_id` is the id of a node in `graph`. An unknown id
//     returns black.
//
// Returns: `Vec3` with non-negative components when the inputs
// are non-negative; the evaluator does NOT clamp - if a math
// node multiplies a negative slot value, the output keeps the
// sign. Tests that expect non-negative results pass
// non-negative immediates.
[[nodiscard]] rr::math::Vec3 evaluate(const Graph& graph,
                                      NodeId node_id,
                                      const EvaluationContext& ctx = {});

}
