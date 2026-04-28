#pragma once

#include "material/MaterialTypes.h"
#include "math/Vec2.h"
#include "math/Vec3.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Material node graph runtime - v1 minimal implementation.
//
// The host-side authoring data model + the compile-to-material
// path described in `docs/MATERIAL_GRAPH_SPEC.md`. This slice
// ships the six v1 node types the spec / prompt name explicitly:
//
//   ConstantColor, TextureSample, Add, Multiply, Diffuse, Emission.
//
// "Compile" here means evaluating the graph at the v1 default
// shading context (per spec sections 4 + 8.4) to bake a
// `MaterialParams`-shape snapshot. Per-hit graph evaluation in
// the kernel is a future slice; the existing renderer keeps
// reading `MaterialParams` and the bake gives it the same
// shading parameters the graph would produce for the default
// context.
//
// rr_material does NOT depend on rr_texture. Texture access for
// `TextureSample` is supplied by the caller as a small callback
// (`TextureSamplerFn`); the .rrscene loader / the C4D bridge /
// the test harness each plug their own. That keeps the existing
// module layering (`MaterialTypes` is below `ImageTexture`)
// unchanged.

namespace rr::material {

// v1 node-type discriminator. Stable ordinals - the on-disk
// schema (which is a future slice) and any future scene-format
// integrations will reference these values directly.
enum class NodeType : std::uint8_t {
    ConstantColor = 0,
    TextureSample = 1,
    Add           = 2,
    Multiply      = 3,
    Diffuse       = 4,
    Emission      = 5,
};

inline constexpr int kNodeTypeCount = 6;

[[nodiscard]] const char* node_type_name(NodeType t);

// Parse a node-type name (case-sensitive) into the enum.
// Returns false on unknown names. Accepts both `Diffuse` (the
// spec's canonical name) and `DiffuseBSDF` (the prompt's
// alternative spelling) so a future scene file written with
// either form parses without translation.
[[nodiscard]] bool parse_node_type(const std::string& s, NodeType& out);

// Terminal nodes have no outputs and contribute directly to
// the baked `MaterialParams`. Per spec 6.5: `Diffuse` and
// `Emission` are terminals in v1 (the placeholder Metallic /
// Glass terminals are not in this slice's catalogue).
[[nodiscard]] bool is_terminal(NodeType t);

// Authoring-side node. One per graph entry. Inputs that are
// `< 0` are unwired; the compiler falls back to the node's
// per-type default for the relevant input.
//
// The fields below are a tagged-union union-of-fields shape:
// per `type`, only the relevant immediate / input fields are
// read. See `node_type_name` and the v1 catalogue
// (docs/MATERIAL_GRAPH_SPEC.md section 6) for which fields
// each type uses.
struct GraphNode {
    int      id   = -1;
    NodeType type = NodeType::ConstantColor;

    // Immediate values / node parameters.
    //
    //   ConstantColor   reads `color_value`.
    //   TextureSample   reads `texture_id` and `default_uv`
    //                   (the latter substitutes for an
    //                   unwired `uv` input).
    //   Emission        reads `color_value` (default for
    //                   `color` input) and `strength_value`
    //                   (default for `strength` input).
    rr::math::Vec3 color_value     = {0.8f, 0.8f, 0.8f};
    float          strength_value  = 1.0f;
    int            texture_id      = -1;
    rr::math::Vec2 default_uv      = {0.5f, 0.5f};

    // Input wiring. `-1` = unwired. The set of inputs each
    // type honours mirrors the catalogue:
    //
    //   Add / Multiply: `input_a`, `input_b`        (both colour-typed)
    //   TextureSample : `input_uv`                  (vec2)
    //   Diffuse       : `input_albedo`              (colour)
    //   Emission      : `input_color`, `input_strength`
    //                                               (colour, scalar)
    //
    // ConstantColor has no wired inputs.
    int input_a        = -1;
    int input_b        = -1;
    int input_uv       = -1;
    int input_albedo   = -1;
    int input_color    = -1;
    int input_strength = -1;
};

// Graph as it lives in memory. Versioned per the spec's
// graph-block versioning rule (10.3). v1 is the only
// supported version today.
struct Graph {
    int                    version = 1;
    std::vector<GraphNode> nodes;
};

// Texture-access callback. Plugged in by the caller (the
// .rrscene loader or the test harness) so rr_material does
// not need to know about `rr::texture::ImageTexture`.
//
// The callback receives the node's `texture_id` and the UV
// to sample at, and returns the sampled colour. A null
// callback (or a callback that returns a zero vector for an
// out-of-range id) reduces a `TextureSample` to its caller-
// chosen fallback - the v1 implementation treats the
// returned vector as the sample value, no questions asked.
using TextureSamplerFn = std::function<rr::math::Vec3(int texture_id,
                                                      rr::math::Vec2 uv)>;

struct CompileResult {
    bool           ok = false;
    std::string    message;        // populated on `ok == false`.
    MaterialParams material{};     // populated on `ok == true`.
};

// Compile a graph to a `MaterialParams` snapshot using the
// v1 "default shading context" bake (spec sections 4 + 8.4).
//
// The bake:
//   1. Validates the graph (unique ids, known types, no
//      cycles, references resolve, at least one terminal).
//   2. Topologically orders the reachable subgraph.
//   3. Evaluates each node in order, caching per-node
//      output values in a slot pool.
//   4. For each terminal node, writes its inputs into the
//      corresponding `MaterialParams` field
//      (`Diffuse.albedo` -> `baseColor`, `Emission.color`
//      / `Emission.strength` -> `emissionColor` /
//      `emissionStrength`).
//
// Failure cases (returns `ok == false` with a descriptive
// `message`):
//   - Duplicate node id.
//   - Reference to a non-existent source-node id.
//   - Cycle in the input wiring.
//   - No terminal node in the graph.
//   - More than one terminal of the same kind (v1 SHOULD
//     rule per spec 7.5; the implementation enforces it).
//
// The bake is bit-deterministic for a given graph + sampler
// callback (the texture sampler is the only external
// dependency).
CompileResult compile_graph_to_material(const Graph& graph,
                                        const TextureSamplerFn& sampler = {});

}
