#include "material/GpuMaterial.h"

#include "material/graph/Graph.h"
#include "material/graph/Node.h"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rr::material {

const char* gpu_opcode_name(GpuOpcode op) {
    switch (op) {
    case GpuOpcode::ConstantColor: return "ConstantColor";
    case GpuOpcode::TextureSample: return "TextureSample";
    case GpuOpcode::Add:           return "Add";
    case GpuOpcode::Multiply:      return "Multiply";
    case GpuOpcode::Diffuse:       return "Diffuse";
    case GpuOpcode::Emission:      return "Emission";
    }
    return "(unknown)";
}

namespace {

using graph::Connection;
using graph::Graph;
using graph::Node;
using graph::NodeId;
using graph::NodeType;

GpuOpcode opcode_from_node_type(NodeType t) {
    switch (t) {
    case NodeType::ConstantColor: return GpuOpcode::ConstantColor;
    case NodeType::TextureSample: return GpuOpcode::TextureSample;
    case NodeType::Add:           return GpuOpcode::Add;
    case NodeType::Multiply:      return GpuOpcode::Multiply;
    case NodeType::DiffuseBSDF:   return GpuOpcode::Diffuse;
    case NodeType::Emission:      return GpuOpcode::Emission;
    }
    return GpuOpcode::ConstantColor;
}

bool is_terminal_node(NodeType t) {
    return t == NodeType::DiffuseBSDF || t == NodeType::Emission;
}

const Connection* find_connection_to(const Graph& graph,
                                     NodeId to_node,
                                     std::string_view to_socket) {
    for (const auto& c : graph.connections) {
        if (c.to_node == to_node && c.to_socket == to_socket) {
            return &c;
        }
    }
    return nullptr;
}

// Iterative DFS topological sort, terminal-driven (only
// nodes reachable from at least one terminal land in the
// result). Pure: no graph mutation. Returns false (and
// fills `error`) on any cycle - validation should already
// have caught that case, but defence-in-depth.
//
// On success `out_order` is the list of node ids in
// evaluation order. Terminal nodes ARE included in the
// order; the lowering pass picks them out into the
// terminals[] table. The iteration order over a node's
// inputs is fixed so the result is deterministic for any
// given graph.
bool topo_sort(const Graph& graph,
               std::vector<NodeId>& out_order,
               std::string& error) {
    enum Color : std::uint8_t { White = 0, Grey = 1, Black = 2 };

    // Build node-id -> node-array-index map.
    std::unordered_map<NodeId, std::size_t> idx_of;
    idx_of.reserve(graph.nodes.size());
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        idx_of[graph.nodes[i].id] = i;
    }
    std::vector<Color> colour(graph.nodes.size(), White);

    out_order.clear();
    out_order.reserve(graph.nodes.size());

    // Per-node incoming-source list (sorted by socket name)
    // so the resulting order is deterministic.
    auto sources_for = [&](const Node& n) {
        std::vector<NodeId> list;
        list.reserve(2);
        for (const auto& c : graph.connections) {
            if (c.to_node == n.id) {
                list.push_back(c.from_node);
            }
        }
        std::sort(list.begin(), list.end());
        return list;
    };

    struct Frame { std::size_t idx; std::vector<NodeId> srcs; std::size_t cursor; };
    std::vector<Frame> stack;
    stack.reserve(16);

    auto visit = [&](std::size_t root_idx) -> bool {
        if (colour[root_idx] == Black) return true;
        stack.push_back({root_idx, sources_for(graph.nodes[root_idx]), 0});

        while (!stack.empty()) {
            auto& f = stack.back();
            if (f.cursor == 0) {
                if (colour[f.idx] == Grey) {
                    std::ostringstream os;
                    os << "cycle detected through node "
                       << graph.nodes[f.idx].id;
                    error = os.str();
                    return false;
                }
                if (colour[f.idx] == Black) {
                    stack.pop_back();
                    continue;
                }
                colour[f.idx] = Grey;
            }

            if (f.cursor >= f.srcs.size()) {
                colour[f.idx] = Black;
                out_order.push_back(graph.nodes[f.idx].id);
                stack.pop_back();
                continue;
            }

            const NodeId src_id = f.srcs[f.cursor++];
            const auto it = idx_of.find(src_id);
            if (it == idx_of.end()) {
                continue;       // already validated; defensive
            }
            const std::size_t src_idx = it->second;
            if (colour[src_idx] != Black) {
                stack.push_back({src_idx,
                                 sources_for(graph.nodes[src_idx]),
                                 0});
            }
        }
        return true;
    };

    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        if (is_terminal_node(graph.nodes[i].type)) {
            if (!visit(i)) return false;
        }
    }
    return true;
}

// Resolve an input socket on `node` to the slot index of
// its source's emitted op, OR -1 if the input is unwired
// (i.e. the kernel should use the immediate fallback).
std::int16_t resolve_input_slot(
    const Graph& graph,
    NodeId node_id,
    std::string_view socket_name,
    const std::unordered_map<NodeId, std::int16_t>& node_to_slot) {
    const Connection* c = find_connection_to(graph, node_id, socket_name);
    if (c == nullptr) return -1;
    auto it = node_to_slot.find(c->from_node);
    if (it == node_to_slot.end()) {
        // Source is reachable (passed validation) but not in
        // node_to_slot - that means the source is itself a
        // terminal, which is illegal as a connection source.
        // Validation should have rejected; defensive -1.
        return -1;
    }
    return it->second;
}

}  // namespace

GpuMaterialResult compile_graph_to_gpu_material(const graph::Graph& graph) {
    GpuMaterialResult result;

    // Stage 1: validation. The data-core's `validate_graph`
    // checks every structural rule the lowering needs.
    const auto vr = graph::validate_graph(graph);
    if (!vr.ok) {
        result.message = vr.message;
        return result;
    }

    // Stage 2: topo sort.
    std::vector<NodeId> order;
    if (!topo_sort(graph, order, result.message)) return result;

    // Stage 3: assign slots to non-terminal nodes; terminals
    // do NOT get slots (they have no output socket).
    std::unordered_map<NodeId, std::int16_t> node_to_slot;
    node_to_slot.reserve(order.size());
    int next_slot = 0;
    for (NodeId id : order) {
        const Node* n = graph::find_node(graph, id);
        if (n == nullptr) continue;          // defensive
        if (is_terminal_node(n->type))   continue;
        if (next_slot > 0x7fff) {
            result.message = "graph too large: more than 32k "
                             "non-terminal nodes (slot index "
                             "would not fit in int16)";
            return result;
        }
        node_to_slot.emplace(id, static_cast<std::int16_t>(next_slot++));
    }

    GpuMaterial mat;
    mat.ops.reserve(static_cast<std::size_t>(next_slot));
    mat.terminals.reserve(2);
    mat.slot_count = next_slot;

    // Stage 4 + 5: emit ops and terminals in topo order.
    for (NodeId id : order) {
        const Node* n = graph::find_node(graph, id);
        if (n == nullptr) continue;          // defensive

        if (is_terminal_node(n->type)) {
            GpuTerminal t;
            t.kind        = opcode_from_node_type(n->type);
            t.imm_color   = n->color_value;
            t.imm_strength = n->scalar_value;
            if (n->type == NodeType::DiffuseBSDF) {
                t.in_color   = resolve_input_slot(graph, id, "albedo",
                                                  node_to_slot);
                t.in_strength = -1;
            } else {
                t.in_color    = resolve_input_slot(graph, id, "color",
                                                   node_to_slot);
                t.in_strength = resolve_input_slot(graph, id, "strength",
                                                   node_to_slot);
            }
            mat.terminals.push_back(t);
            continue;
        }

        GpuOp op;
        op.opcode = opcode_from_node_type(n->type);
        switch (n->type) {
        case NodeType::ConstantColor:
            op.imm_color = n->color_value;
            break;
        case NodeType::TextureSample:
            op.imm_int   = n->texture_id;
            // The placeholder fallback the future kernel
            // returns when the texture is missing. White
            // by convention - matches the M16 sampler's
            // null-data fallback in `cuda/CudaTexture.cuh`.
            op.imm_color = rr::math::Vec3{1.0f, 1.0f, 1.0f};
            break;
        case NodeType::Add:
        case NodeType::Multiply:
            op.in_a = resolve_input_slot(graph, id, "a", node_to_slot);
            op.in_b = resolve_input_slot(graph, id, "b", node_to_slot);
            break;
        default:
            break;          // terminals already handled
        }
        mat.ops.push_back(op);
    }

    result.material = std::move(mat);
    result.ok       = true;
    return result;
}


// ---------------------------------------------------------------------------
// Debug print.
// ---------------------------------------------------------------------------

void debug_print_gpu_material(const GpuMaterial& mat, std::FILE* out) {
    if (out == nullptr) out = stdout;

    std::fprintf(out, "GpuMaterial: ops=%zu terminals=%zu slot_count=%d\n",
                 mat.ops.size(), mat.terminals.size(), mat.slot_count);

    std::fprintf(out, "  ops:\n");
    for (std::size_t i = 0; i < mat.ops.size(); ++i) {
        const auto& op = mat.ops[i];
        std::fprintf(out, "    [%2zu] %-14s",
                     i, gpu_opcode_name(op.opcode));
        switch (op.opcode) {
        case GpuOpcode::ConstantColor:
            std::fprintf(out, " imm_color=(%.3f, %.3f, %.3f)",
                         op.imm_color.x, op.imm_color.y, op.imm_color.z);
            break;
        case GpuOpcode::TextureSample:
            std::fprintf(out,
                         " texture_id=%d imm_color=(%.3f, %.3f, %.3f)",
                         op.imm_int,
                         op.imm_color.x, op.imm_color.y, op.imm_color.z);
            break;
        case GpuOpcode::Add:
        case GpuOpcode::Multiply:
            std::fprintf(out, " in_a=%d in_b=%d", op.in_a, op.in_b);
            break;
        default:
            break;
        }
        std::fputc('\n', out);
    }

    std::fprintf(out, "  terminals:\n");
    for (std::size_t i = 0; i < mat.terminals.size(); ++i) {
        const auto& t = mat.terminals[i];
        std::fprintf(out, "    [%2zu] %-14s",
                     i, gpu_opcode_name(t.kind));
        if (t.kind == GpuOpcode::Diffuse) {
            std::fprintf(out,
                         " in_color=%d imm_color=(%.3f, %.3f, %.3f)",
                         t.in_color,
                         t.imm_color.x, t.imm_color.y, t.imm_color.z);
        } else {
            std::fprintf(out,
                         " in_color=%d in_strength=%d "
                         "imm_color=(%.3f, %.3f, %.3f) imm_strength=%.3f",
                         t.in_color, t.in_strength,
                         t.imm_color.x, t.imm_color.y, t.imm_color.z,
                         t.imm_strength);
        }
        std::fputc('\n', out);
    }
}

}
