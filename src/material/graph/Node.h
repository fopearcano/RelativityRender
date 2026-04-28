#pragma once

#include "material/graph/Socket.h"
#include "math/Vec2.h"
#include "math/Vec3.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Material graph data core - node layer.
//
// One v1 node-type discriminator + a plain-data `Node` carrying
// its id, type, declared input / output sockets, and any per-type
// immediate values. No evaluation; no GPU; no UI.
//
// The node-type set is the v1 catalogue from
// `docs/MATERIAL_GRAPH_SPEC.md` section 6:
//
//   ConstantColor, TextureSample (placeholder), Add, Multiply,
//   DiffuseBSDF, Emission.
//
// Naming note: the spec's canonical name for the diffuse terminal
// is `Diffuse`; this slice uses the prompt's preferred
// `DiffuseBSDF` as the canonical enum name. `parse_node_type`
// accepts BOTH spellings so JSON written either way parses
// without translation.

namespace rr::material::graph {

using NodeId = std::int32_t;

inline constexpr NodeId kInvalidNodeId = -1;


enum class NodeType : std::uint8_t {
    ConstantColor = 0,
    TextureSample = 1,
    Add           = 2,
    Multiply      = 3,
    DiffuseBSDF   = 4,
    Emission      = 5,
};

inline constexpr int kNodeTypeCount = 6;

[[nodiscard]] const char* node_type_name(NodeType type);

// Parse a node-type name into the enum. Case-sensitive. Accepts
// the prompt's `DiffuseBSDF` and the spec's `Diffuse` as
// equivalent; everything else rejects.
[[nodiscard]] bool parse_node_type(std::string_view name, NodeType& out);

// True for terminal nodes: per spec 7.5, terminals have no
// outgoing connections (no output sockets) and contribute
// directly to the final shading result. v1 terminals are
// `DiffuseBSDF` and `Emission`.
[[nodiscard]] bool is_terminal(NodeType type);


// Plain-data node. Carries:
//   - `id`     : unique within the containing graph.
//   - `type`   : discriminator (per the v1 catalogue).
//   - `name`   : optional human-readable label (round-trips
//                through serialisation; not used by validation).
//   - `inputs` / `outputs`: the declared sockets, one entry per
//                catalogue-defined slot. Constructed via
//                `make_node` to match the v1 catalogue exactly;
//                callers that synthesise nodes by hand are
//                responsible for the same shape.
//   - per-type immediates (`color_value`, `scalar_value`,
//                `uv_value`, `texture_id`): used by the eventual
//                evaluator, ignored by the data-only layer.
//
// All fields are trivially serialisable; no virtuals, no smart
// pointers, no shared ownership.
struct Node {
    NodeId   id   = kInvalidNodeId;
    NodeType type = NodeType::ConstantColor;
    std::string name;

    std::vector<Socket> inputs;
    std::vector<Socket> outputs;

    // Per-type immediate values. Each NodeType reads only the
    // fields the v1 catalogue declares for it:
    //   ConstantColor:  color_value
    //   TextureSample:  texture_id, uv_value (default UV when
    //                   the `uv` input is unwired)
    //   Emission:       color_value (default `color` input),
    //                   scalar_value (default `strength` input)
    rr::math::Vec3 color_value  = {0.8f, 0.8f, 0.8f};
    float          scalar_value = 1.0f;
    rr::math::Vec2 uv_value     = {0.5f, 0.5f};
    int            texture_id   = -1;
};

// Build a `Node` of the requested type with the catalogue's
// canonical socket layout already populated. The returned node
// has its `id` and `type` set; immediates default to the per-
// catalogue defaults the spec lists. Callers fill in the `id`
// (or get the default `kInvalidNodeId` and assign later) and
// any non-default immediates.
//
// Socket layout produced (per spec section 6):
//   ConstantColor  : in []                         out [value: Color]
//   TextureSample  : in [uv: Vec2]                 out [value: Color]
//   Add            : in [a: Color, b: Color]       out [value: Color]
//   Multiply       : in [a: Color, b: Color]       out [value: Color]
//   DiffuseBSDF    : in [albedo: Color]            out []
//   Emission       : in [color: Color, strength: Float]  out []
//
// v1 binds Add / Multiply to Color in / Color out. The spec
// allows polymorphism over Float / Vec3 / Color for these math
// nodes; a future slice can introduce typed variants.
[[nodiscard]] Node make_node(NodeType type, NodeId id = kInvalidNodeId);

// Look up a socket by name on `node`. Returns nullptr when no
// socket of that name exists in the requested direction.
// Pointer is invalidated when the node's socket vectors are
// resized; safe for snapshot-style use within the validator.
[[nodiscard]] const Socket* find_socket(const Node& node,
                                        std::string_view name,
                                        SocketDirection direction);

}
