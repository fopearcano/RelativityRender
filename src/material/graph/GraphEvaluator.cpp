#include "material/graph/GraphEvaluator.h"

#include "material/graph/Graph.h"
#include "material/graph/Node.h"
#include "math/Vec3.h"

#include <string>
#include <string_view>

namespace rr::material::graph {

namespace {

// Per-component vec3 helpers. The math module's operators
// already cover these but a local alias keeps the per-node
// switch terse.
inline rr::math::Vec3 cadd(rr::math::Vec3 a, rr::math::Vec3 b) {
    return rr::math::Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

inline rr::math::Vec3 cmul(rr::math::Vec3 a, rr::math::Vec3 b) {
    return rr::math::Vec3{a.x * b.x, a.y * b.y, a.z * b.z};
}

inline rr::math::Vec3 cscale(rr::math::Vec3 v, float s) {
    return rr::math::Vec3{v.x * s, v.y * s, v.z * s};
}

// Recursive worker. Carries the current depth so the depth cap
// in EvaluationContext is honoured without threading a stateful
// context down the stack.
rr::math::Vec3 evaluate_at(const Graph& graph,
                           NodeId node_id,
                           const EvaluationContext& ctx,
                           int depth) {
    if (depth >= ctx.max_depth) {
        return rr::math::Vec3{0.0f, 0.0f, 0.0f};
    }

    const Node* n = find_node(graph, node_id);
    if (n == nullptr) {
        return rr::math::Vec3{0.0f, 0.0f, 0.0f};
    }

    // Walk the graph's connection list to find the source that
    // drives `socket_name` on this node. Returns
    // `default_value` when the input is unwired.
    auto resolve = [&](std::string_view socket_name,
                       rr::math::Vec3 default_value) -> rr::math::Vec3 {
        for (const auto& c : graph.connections) {
            if (c.to_node == node_id && c.to_socket == socket_name) {
                return evaluate_at(graph, c.from_node, ctx, depth + 1);
            }
        }
        return default_value;
    };

    switch (n->type) {
    case NodeType::ConstantColor:
        return n->color_value;

    case NodeType::TextureSample:
        // Reference evaluator: no actual sampling. The fallback
        // colour is the contract; the kernel-side path tracer
        // calls into the real M16 sampler instead.
        return ctx.fallback_texture_color;

    case NodeType::Add: {
        // Per spec section 6.3: unwired inputs default to
        // additive identity (zero). cadd of two zeros is zero,
        // which keeps the chain a no-op when nothing is wired.
        const auto a = resolve("a", rr::math::Vec3{0.0f, 0.0f, 0.0f});
        const auto b = resolve("b", rr::math::Vec3{0.0f, 0.0f, 0.0f});
        return cadd(a, b);
    }

    case NodeType::Multiply: {
        // Multiplicative identity is one; an unwired chain
        // collapses to white.
        const auto a = resolve("a", rr::math::Vec3{1.0f, 1.0f, 1.0f});
        const auto b = resolve("b", rr::math::Vec3{1.0f, 1.0f, 1.0f});
        return cmul(a, b);
    }

    case NodeType::DiffuseBSDF:
        // Terminal: the colour summary is the resolved albedo.
        // Unwired falls back to the node's own `color_value`
        // (the catalogue's mid-grey default by construction in
        // make_node).
        return resolve("albedo", n->color_value);

    case NodeType::Emission: {
        // Terminal: colour summary is `color * strength`. The
        // `strength` input is Float-typed; v1 has no Float-
        // producing node, so the input is always unwired and
        // the immediate `scalar_value` carries the strength.
        // Wiring `color` to a non-Float source resolves
        // normally; otherwise the node's own `color_value`
        // applies.
        const auto col = resolve("color", n->color_value);
        return cscale(col, n->scalar_value);
    }
    }

    return rr::math::Vec3{0.0f, 0.0f, 0.0f};
}

}  // namespace

rr::math::Vec3 evaluate(const Graph& graph,
                        NodeId node_id,
                        const EvaluationContext& ctx) {
    return evaluate_at(graph, node_id, ctx, /*depth=*/0);
}

}
