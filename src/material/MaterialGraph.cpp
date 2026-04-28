#include "material/MaterialGraph.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace rr::material {

const char* node_type_name(NodeType t) {
    switch (t) {
    case NodeType::ConstantColor: return "ConstantColor";
    case NodeType::TextureSample: return "TextureSample";
    case NodeType::Add:           return "Add";
    case NodeType::Multiply:      return "Multiply";
    case NodeType::Diffuse:       return "Diffuse";
    case NodeType::Emission:      return "Emission";
    }
    return "(unknown)";
}

bool parse_node_type(const std::string& s, NodeType& out) {
    // Canonical names from the spec.
    if (s == "ConstantColor") { out = NodeType::ConstantColor; return true; }
    if (s == "TextureSample") { out = NodeType::TextureSample; return true; }
    if (s == "Add")           { out = NodeType::Add;           return true; }
    if (s == "Multiply")      { out = NodeType::Multiply;      return true; }
    if (s == "Diffuse")       { out = NodeType::Diffuse;       return true; }
    if (s == "Emission")      { out = NodeType::Emission;      return true; }

    // Accepted aliases.
    if (s == "DiffuseBSDF")   { out = NodeType::Diffuse;       return true; }
    return false;
}

bool is_terminal(NodeType t) {
    return t == NodeType::Diffuse || t == NodeType::Emission;
}

namespace {

// Helpers for the validation + bake passes. Kept anonymous so
// only `compile_graph_to_material` exposes anything externally.

// Per-node output value cached in topo order. Both fields are
// populated for every node; consumers read whichever the
// corresponding input expects (most v1 sockets are colour /
// `vec3` and read `vec`; `Emission.strength` is the only
// scalar consumer in this slice and reads `scalar`).
struct NodeOutput {
    rr::math::Vec3 vec    = {0.0f, 0.0f, 0.0f};
    float          scalar = 0.0f;
};

// O(1) `id` -> index lookup over the graph's node array.
struct IdMap {
    std::unordered_map<int, std::size_t> by_id;

    bool build(const Graph& graph, std::string& error) {
        by_id.clear();
        by_id.reserve(graph.nodes.size());
        for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
            const int id = graph.nodes[i].id;
            if (!by_id.emplace(id, i).second) {
                std::ostringstream os;
                os << "duplicate node id " << id;
                error = os.str();
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool find(int id, std::size_t& out) const {
        auto it = by_id.find(id);
        if (it == by_id.end()) return false;
        out = it->second;
        return true;
    }
};

// Iterate the inputs an `id`-indexed node CAN reference. The
// caller passes a callable that receives each non-`-1` source
// node id; this keeps the per-type input fanout in one place.
template <typename F>
void each_wired_input(const GraphNode& n, F&& visit) {
    auto v = [&](int src) { if (src >= 0) visit(src); };
    switch (n.type) {
    case NodeType::ConstantColor:
        break;  // no wired inputs.
    case NodeType::TextureSample:
        v(n.input_uv);
        break;
    case NodeType::Add:
    case NodeType::Multiply:
        v(n.input_a);
        v(n.input_b);
        break;
    case NodeType::Diffuse:
        v(n.input_albedo);
        break;
    case NodeType::Emission:
        v(n.input_color);
        v(n.input_strength);
        break;
    }
}

// Validate id references on every node. Pass 1 of the
// validation suite. Returns false + populates `error` on the
// first dangling id; otherwise returns true.
bool check_references(const Graph& graph,
                      const IdMap& ids,
                      std::string& error) {
    for (const auto& n : graph.nodes) {
        bool ok = true;
        std::ostringstream os;
        each_wired_input(n, [&](int src) {
            if (!ok) return;
            std::size_t idx;
            if (!ids.find(src, idx)) {
                os << "node " << n.id
                   << " references unknown source-node id " << src;
                error = os.str();
                ok = false;
            }
        });
        if (!ok) return false;
    }
    return true;
}

// Topological sort by depth-first traversal from each terminal,
// dropping unreachable nodes (per spec 7.4 / 8.3). Records the
// order in `out_order` (vector of node array indices) so the
// bake can iterate linearly.
//
// Cycle detection uses the standard 3-state colouring (white /
// grey / black). Returns false + populates `error` on a cycle.
bool topo_sort(const Graph& graph,
               const IdMap& ids,
               std::vector<std::size_t>& out_order,
               std::string& error) {
    enum Color : std::uint8_t { White = 0, Grey = 1, Black = 2 };
    std::vector<Color> colour(graph.nodes.size(), White);
    out_order.clear();
    out_order.reserve(graph.nodes.size());

    // Iterative DFS keyed by node array index.
    struct Frame { std::size_t idx; int next_input; };
    std::vector<Frame> stack;
    stack.reserve(16);

    auto visit = [&](std::size_t root) -> bool {
        if (colour[root] == Black) return true;
        stack.push_back({root, 0});
        while (!stack.empty()) {
            auto& f = stack.back();
            const auto& n = graph.nodes[f.idx];

            if (f.next_input == 0) {
                if (colour[f.idx] == Grey) {
                    std::ostringstream os;
                    os << "cycle detected through node " << n.id;
                    error = os.str();
                    return false;
                }
                if (colour[f.idx] == Black) {
                    stack.pop_back();
                    continue;
                }
                colour[f.idx] = Grey;
            }

            // Walk the wired inputs in the same fixed per-type
            // order so the resulting topological order is
            // deterministic for a given graph.
            int sources[2 + 2] = {-1, -1, -1, -1};  // worst case 2 inputs in v1
            int n_src = 0;
            switch (n.type) {
            case NodeType::ConstantColor: break;
            case NodeType::TextureSample:
                if (n.input_uv >= 0)        sources[n_src++] = n.input_uv;
                break;
            case NodeType::Add:
            case NodeType::Multiply:
                if (n.input_a >= 0)         sources[n_src++] = n.input_a;
                if (n.input_b >= 0)         sources[n_src++] = n.input_b;
                break;
            case NodeType::Diffuse:
                if (n.input_albedo >= 0)    sources[n_src++] = n.input_albedo;
                break;
            case NodeType::Emission:
                if (n.input_color >= 0)     sources[n_src++] = n.input_color;
                if (n.input_strength >= 0)  sources[n_src++] = n.input_strength;
                break;
            }

            if (f.next_input >= n_src) {
                colour[f.idx] = Black;
                out_order.push_back(f.idx);
                stack.pop_back();
                continue;
            }

            const int src_id = sources[f.next_input++];
            std::size_t src_idx;
            // `check_references` already verified every wired
            // source resolves; the [[nodiscard]] result is
            // intentionally swallowed.
            (void)ids.find(src_id, src_idx);
            if (colour[src_idx] != Black) {
                stack.push_back({src_idx, 0});
            }
        }
        return true;
    };

    // Seed the traversal from every terminal node so unreached
    // subgraphs are dropped.
    bool any_terminal = false;
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        if (is_terminal(graph.nodes[i].type)) {
            any_terminal = true;
            if (!visit(i)) return false;
        }
    }
    if (!any_terminal) {
        error = "graph has no terminal node "
                "(at least one Diffuse or Emission is required)";
        return false;
    }
    return true;
}

// Per-component multiply / add of `Vec3`s. The math module's
// operators already cover this; helpers below just keep the
// per-node cases readable.
inline rr::math::Vec3 cmul(rr::math::Vec3 a, rr::math::Vec3 b) {
    return rr::math::Vec3{a.x * b.x, a.y * b.y, a.z * b.z};
}

inline rr::math::Vec3 cadd(rr::math::Vec3 a, rr::math::Vec3 b) {
    return rr::math::Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

// Read the colour value driving an input. Falls back to
// `default_color` when the input is unwired.
rr::math::Vec3 read_color_input(int source_id,
                                rr::math::Vec3 default_color,
                                const IdMap& ids,
                                const std::vector<NodeOutput>& outputs) {
    if (source_id < 0) return default_color;
    std::size_t idx;
    if (!ids.find(source_id, idx)) return default_color;  // defensive
    return outputs[idx].vec;
}

// Read the scalar value driving an input. v1 has no
// scalar-producing nodes; the only consumer of a scalar
// input today is `Emission.strength`, and the only legal
// source for it is "unwired" (so the default applies).
// Connection from a colour-producing node falls through to
// the default, matching spec 7.3's prohibition on
// `vec3 -> float` implicit conversion.
float read_scalar_input(int source_id,
                        float default_scalar,
                        const Graph& graph,
                        const IdMap& ids,
                        const std::vector<NodeOutput>& outputs) {
    if (source_id < 0) return default_scalar;
    std::size_t idx;
    if (!ids.find(source_id, idx)) return default_scalar;
    const auto& src = graph.nodes[idx];
    // Only nodes whose output is naturally scalar should drive
    // a scalar input. v1 has no such producer. Defensive
    // fallback: keep the default. A future ConstantFloat node
    // would land here.
    (void)src;
    return outputs[idx].scalar != 0.0f
         ? outputs[idx].scalar
         : default_scalar;
}

void evaluate_node(const Graph& graph,
                   std::size_t  node_idx,
                   const IdMap& ids,
                   const TextureSamplerFn& sampler,
                   std::vector<NodeOutput>& outputs) {
    const auto& n = graph.nodes[node_idx];
    NodeOutput  out;

    switch (n.type) {
    case NodeType::ConstantColor: {
        out.vec = n.color_value;
        break;
    }
    case NodeType::TextureSample: {
        // UV input falls back to the node's `default_uv`. v1
        // has no vec2-producing node so wired inputs are
        // unreachable; the field stays as a forward-compatible
        // hook for the next slice that adds UV / UVTransform
        // outputs.
        const rr::math::Vec2 uv = n.default_uv;
        if (sampler && n.texture_id >= 0) {
            out.vec = sampler(n.texture_id, uv);
        } else {
            // Caller supplied no sampler, or the texture id is
            // out of range. Spec section 6.2 / 9.3 calls
            // TextureSample a placeholder; the v1 fallback is
            // white so a graph that references a missing
            // texture stays visible (vs. silently going black).
            out.vec = rr::math::Vec3{1.0f, 1.0f, 1.0f};
        }
        break;
    }
    case NodeType::Add: {
        const auto a = read_color_input(n.input_a,
                                        rr::math::Vec3{0, 0, 0},
                                        ids, outputs);
        const auto b = read_color_input(n.input_b,
                                        rr::math::Vec3{0, 0, 0},
                                        ids, outputs);
        out.vec = cadd(a, b);
        break;
    }
    case NodeType::Multiply: {
        const auto a = read_color_input(n.input_a,
                                        rr::math::Vec3{1, 1, 1},
                                        ids, outputs);
        const auto b = read_color_input(n.input_b,
                                        rr::math::Vec3{1, 1, 1},
                                        ids, outputs);
        out.vec = cmul(a, b);
        break;
    }
    case NodeType::Diffuse:
    case NodeType::Emission: {
        // Terminals: nothing to produce. The caller writes
        // their inputs into MaterialParams directly. Leave the
        // slot as zeros so any erroneous downstream read is
        // harmless.
        break;
    }
    }

    outputs[node_idx] = out;
}

// Apply the result of evaluating a terminal to `params`. v1
// allows at most one terminal of each kind (per spec 7.5);
// `terminal_seen[type]` tracks which kinds have been written
// so the caller can flag duplicates as a validation error.
bool apply_terminal(const Graph& graph,
                    std::size_t node_idx,
                    const IdMap& ids,
                    const std::vector<NodeOutput>& outputs,
                    MaterialParams& params,
                    bool terminal_seen[kNodeTypeCount],
                    std::string& error) {
    const auto& n = graph.nodes[node_idx];
    if (terminal_seen[static_cast<int>(n.type)]) {
        std::ostringstream os;
        os << "graph has more than one " << node_type_name(n.type)
           << " terminal (v1 allows at most one of each terminal type)";
        error = os.str();
        return false;
    }
    terminal_seen[static_cast<int>(n.type)] = true;

    switch (n.type) {
    case NodeType::Diffuse: {
        params.baseColor = read_color_input(n.input_albedo,
                                            n.color_value,
                                            ids, outputs);
        break;
    }
    case NodeType::Emission: {
        params.emissionColor    = read_color_input(n.input_color,
                                                   n.color_value,
                                                   ids, outputs);
        params.emissionStrength = read_scalar_input(n.input_strength,
                                                    n.strength_value,
                                                    graph, ids, outputs);
        if (params.emissionStrength < 0.0f) params.emissionStrength = 0.0f;
        break;
    }
    default:
        break;  // not a terminal
    }
    return true;
}

}  // namespace

CompileResult compile_graph_to_material(const Graph& graph,
                                        const TextureSamplerFn& sampler) {
    CompileResult result;

    if (graph.version != 1) {
        std::ostringstream os;
        os << "graph version " << graph.version
           << " is not supported (this build implements v1 only)";
        result.message = os.str();
        return result;
    }
    if (graph.nodes.empty()) {
        result.message = "graph is empty";
        return result;
    }

    IdMap ids;
    if (!ids.build(graph, result.message)) return result;
    if (!check_references(graph, ids, result.message)) return result;

    std::vector<std::size_t> order;
    if (!topo_sort(graph, ids, order, result.message)) return result;

    std::vector<NodeOutput> outputs(graph.nodes.size());
    for (std::size_t idx : order) {
        evaluate_node(graph, idx, ids, sampler, outputs);
    }

    // Apply terminals to MaterialParams, tracking duplicates.
    bool terminal_seen[kNodeTypeCount] = {false};
    MaterialParams params;
    for (std::size_t idx : order) {
        const auto& n = graph.nodes[idx];
        if (!is_terminal(n.type)) continue;
        if (!apply_terminal(graph, idx, ids, outputs,
                            params, terminal_seen, result.message)) {
            return result;
        }
    }

    result.material = params;
    result.ok       = true;
    return result;
}

}
