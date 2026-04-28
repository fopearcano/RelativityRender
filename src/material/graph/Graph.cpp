#include "material/graph/Graph.h"

#include "material/graph/Node.h"
#include "material/graph/Socket.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rr::material::graph {

// ---------------------------------------------------------------------------
// Socket layer
// ---------------------------------------------------------------------------

const char* socket_type_name(SocketType type) {
    switch (type) {
    case SocketType::Float:  return "float";
    case SocketType::Vec2:   return "vec2";
    case SocketType::Vec3:   return "vec3";
    case SocketType::Color:  return "color";
    case SocketType::Normal: return "normal";
    }
    return "(unknown)";
}

bool parse_socket_type(std::string_view name, SocketType& out) {
    // Lowercase canonical (matches spec section 7.2's table).
    if (name == "float")  { out = SocketType::Float;  return true; }
    if (name == "vec2")   { out = SocketType::Vec2;   return true; }
    if (name == "vec3")   { out = SocketType::Vec3;   return true; }
    if (name == "color")  { out = SocketType::Color;  return true; }
    if (name == "normal") { out = SocketType::Normal; return true; }
    // PascalCase aliases - tolerated so a JSON written either way
    // round-trips without translation.
    if (name == "Float")  { out = SocketType::Float;  return true; }
    if (name == "Vec2")   { out = SocketType::Vec2;   return true; }
    if (name == "Vec3")   { out = SocketType::Vec3;   return true; }
    if (name == "Color")  { out = SocketType::Color;  return true; }
    if (name == "Normal") { out = SocketType::Normal; return true; }
    return false;
}

bool can_connect(SocketType source, SocketType sink) {
    // Identity always succeeds.
    if (source == sink) return true;

    // Implicit conversions per spec section 7.3.
    switch (source) {
    case SocketType::Float:
        // float -> any (broadcast).
        return sink == SocketType::Vec2
            || sink == SocketType::Vec3
            || sink == SocketType::Color;
    case SocketType::Vec3:
        // vec3 <-> color (reinterpret).
        return sink == SocketType::Color;
    case SocketType::Color:
        // color <-> vec3 (reinterpret).
        return sink == SocketType::Vec3;
    case SocketType::Normal:
        // normal -> vec3 (drop unit-length contract).
        return sink == SocketType::Vec3;
    case SocketType::Vec2:
        // vec2 -> only vec2; spec 7.3 explicitly forbids
        // vec2 <-> vec3 implicit conversion.
        return false;
    }
    return false;
}

const char* socket_direction_name(SocketDirection direction) {
    switch (direction) {
    case SocketDirection::Input:  return "input";
    case SocketDirection::Output: return "output";
    }
    return "(unknown)";
}


// ---------------------------------------------------------------------------
// Node layer
// ---------------------------------------------------------------------------

const char* node_type_name(NodeType type) {
    switch (type) {
    case NodeType::ConstantColor: return "ConstantColor";
    case NodeType::TextureSample: return "TextureSample";
    case NodeType::Add:           return "Add";
    case NodeType::Multiply:      return "Multiply";
    case NodeType::DiffuseBSDF:   return "DiffuseBSDF";
    case NodeType::Emission:      return "Emission";
    }
    return "(unknown)";
}

bool parse_node_type(std::string_view name, NodeType& out) {
    if (name == "ConstantColor") { out = NodeType::ConstantColor; return true; }
    if (name == "TextureSample") { out = NodeType::TextureSample; return true; }
    if (name == "Add")           { out = NodeType::Add;           return true; }
    if (name == "Multiply")      { out = NodeType::Multiply;      return true; }
    if (name == "DiffuseBSDF")   { out = NodeType::DiffuseBSDF;   return true; }
    if (name == "Emission")      { out = NodeType::Emission;      return true; }

    // Spec's canonical name `Diffuse` is accepted as an alias.
    if (name == "Diffuse")       { out = NodeType::DiffuseBSDF;   return true; }
    return false;
}

bool is_terminal(NodeType type) {
    return type == NodeType::DiffuseBSDF || type == NodeType::Emission;
}

namespace {

// Local helper: build a socket with an explicit direction so the
// per-type catalogue layouts below stay readable.
Socket in(std::string name, SocketType type) {
    Socket s;
    s.name      = std::move(name);
    s.type      = type;
    s.direction = SocketDirection::Input;
    return s;
}

Socket out(std::string name, SocketType type) {
    Socket s;
    s.name      = std::move(name);
    s.type      = type;
    s.direction = SocketDirection::Output;
    return s;
}

}  // namespace

Node make_node(NodeType type, NodeId id) {
    Node n;
    n.id   = id;
    n.type = type;

    switch (type) {
    case NodeType::ConstantColor:
        n.outputs = { out("value", SocketType::Color) };
        break;

    case NodeType::TextureSample:
        n.inputs  = { in ("uv",    SocketType::Vec2) };
        n.outputs = { out("value", SocketType::Color) };
        break;

    case NodeType::Add:
    case NodeType::Multiply:
        n.inputs  = { in ("a",     SocketType::Color),
                      in ("b",     SocketType::Color) };
        n.outputs = { out("value", SocketType::Color) };
        break;

    case NodeType::DiffuseBSDF:
        n.inputs  = { in("albedo", SocketType::Color) };
        // No outputs (terminal).
        break;

    case NodeType::Emission:
        n.inputs  = { in("color",    SocketType::Color),
                      in("strength", SocketType::Float) };
        // No outputs (terminal).
        break;
    }

    // Per-type immediate defaults from the spec catalogue.
    switch (type) {
    case NodeType::ConstantColor:
        n.color_value = rr::math::Vec3{0.8f, 0.8f, 0.8f};
        break;
    case NodeType::TextureSample:
        n.uv_value = rr::math::Vec2{0.5f, 0.5f};
        break;
    case NodeType::Emission:
        n.color_value  = rr::math::Vec3{0.0f, 0.0f, 0.0f};
        n.scalar_value = 1.0f;
        break;
    case NodeType::DiffuseBSDF:
        n.color_value = rr::math::Vec3{0.8f, 0.8f, 0.8f};
        break;
    case NodeType::Add:
    case NodeType::Multiply:
        // No immediates.
        break;
    }

    return n;
}

const Socket* find_socket(const Node& node,
                          std::string_view name,
                          SocketDirection direction) {
    const auto& bag = (direction == SocketDirection::Input)
                    ? node.inputs : node.outputs;
    for (const auto& s : bag) {
        if (s.name == name) return &s;
    }
    return nullptr;
}


// ---------------------------------------------------------------------------
// Graph layer
// ---------------------------------------------------------------------------

const Node* find_node(const Graph& graph, NodeId id) {
    for (const auto& n : graph.nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

Node* find_node(Graph& graph, NodeId id) {
    for (auto& n : graph.nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

std::vector<const Connection*>
incoming_connections(const Graph& graph, NodeId id) {
    std::vector<const Connection*> out;
    out.reserve(4);
    for (const auto& c : graph.connections) {
        if (c.to_node == id) out.push_back(&c);
    }
    return out;
}


// ---------------------------------------------------------------------------
// Graph builder helpers (this slice).
// ---------------------------------------------------------------------------

NodeId Graph::add_node(NodeType type) {
    NodeId next = 0;
    for (const auto& n : nodes) {
        if (n.id >= next) next = n.id + 1;
    }
    Node n = make_node(type, next);
    nodes.push_back(std::move(n));
    return next;
}

bool Graph::connect(NodeId           from_node_id,
                    std::string_view from_socket,
                    NodeId           to_node_id,
                    std::string_view to_socket) {
    // 1. Both nodes resolve.
    const Node* from = find_node(*this, from_node_id);
    if (from == nullptr) return false;
    const Node* to   = find_node(*this, to_node_id);
    if (to == nullptr)   return false;

    // 2. Sockets resolve in the right direction.
    const Socket* src = find_socket(*from, from_socket,
                                    SocketDirection::Output);
    if (src == nullptr) return false;
    const Socket* dst = find_socket(*to, to_socket,
                                    SocketDirection::Input);
    if (dst == nullptr) return false;

    // 3. Sink not already wired.
    for (const auto& c : connections) {
        if (c.to_node == to_node_id && c.to_socket == to_socket) {
            return false;
        }
    }

    // 4. Type compatibility (per spec 7.3 implicit conversion table).
    if (!can_connect(src->type, dst->type)) return false;

    Connection c;
    c.from_node   = from_node_id;
    c.from_socket = std::string(from_socket);
    c.to_node     = to_node_id;
    c.to_socket   = std::string(to_socket);
    connections.push_back(std::move(c));
    return true;
}

ValidationResult Graph::validate() const {
    return validate_graph(*this);
}


namespace {

// Connection-key for the "input has at most one incoming" rule.
// Two connections targeting the same (to_node, to_socket) pair
// collide; one of them MUST be removed by the author.
struct InputSinkKey {
    NodeId      to_node;
    std::string to_socket;

    bool operator==(const InputSinkKey& o) const {
        return to_node == o.to_node && to_socket == o.to_socket;
    }
};

struct InputSinkHasher {
    std::size_t operator()(const InputSinkKey& k) const {
        return std::hash<int>()(k.to_node)
             ^ (std::hash<std::string>()(k.to_socket) << 1);
    }
};

// Cycle detection by iterative DFS with 3-state colouring.
// Returns true on cycle and reports the offending node id.
bool has_cycle(const Graph& graph, NodeId& cycle_node_id) {
    // Build node-id -> array index lookup.
    std::unordered_map<NodeId, std::size_t> idx_of;
    idx_of.reserve(graph.nodes.size());
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        idx_of.emplace(graph.nodes[i].id, i);
    }

    // Edge map: per node, the list of connected destination
    // node indices (only edges that resolve cleanly).
    std::vector<std::vector<std::size_t>> adj(graph.nodes.size());
    for (const auto& c : graph.connections) {
        auto src = idx_of.find(c.from_node);
        auto dst = idx_of.find(c.to_node);
        if (src == idx_of.end() || dst == idx_of.end()) continue;
        adj[src->second].push_back(dst->second);
    }

    enum Color : std::uint8_t { White = 0, Grey = 1, Black = 2 };
    std::vector<Color> colour(graph.nodes.size(), White);

    struct Frame { std::size_t node_idx; std::size_t next_edge; };
    std::vector<Frame> stack;
    stack.reserve(16);

    for (std::size_t root = 0; root < graph.nodes.size(); ++root) {
        if (colour[root] != White) continue;
        stack.push_back({root, 0});
        while (!stack.empty()) {
            auto& f = stack.back();
            if (f.next_edge == 0) {
                colour[f.node_idx] = Grey;
            }
            const auto& edges = adj[f.node_idx];
            if (f.next_edge >= edges.size()) {
                colour[f.node_idx] = Black;
                stack.pop_back();
                continue;
            }
            const auto next = edges[f.next_edge++];
            if (colour[next] == Grey) {
                cycle_node_id = graph.nodes[next].id;
                return true;
            }
            if (colour[next] == White) {
                stack.push_back({next, 0});
            }
        }
    }
    return false;
}

}  // namespace

ValidationResult validate_graph(const Graph& graph) {
    ValidationResult result;

    // Version.
    if (graph.version != 1) {
        std::ostringstream os;
        os << "graph version " << graph.version
           << " is not supported (this build implements v1 only)";
        result.message = os.str();
        return result;
    }

    // 1. Every node id is unique.
    {
        std::unordered_set<NodeId> seen;
        seen.reserve(graph.nodes.size());
        for (const auto& n : graph.nodes) {
            if (n.id == kInvalidNodeId) {
                std::ostringstream os;
                os << "node has invalid id (" << kInvalidNodeId << ")";
                result.message = os.str();
                return result;
            }
            if (!seen.insert(n.id).second) {
                std::ostringstream os;
                os << "duplicate node id " << n.id;
                result.message = os.str();
                return result;
            }
        }
    }

    // 2-5. Every connection: nodes resolve, sockets resolve in
    //      the right direction, sink not already wired,
    //      type-compatible.
    std::unordered_set<InputSinkKey, InputSinkHasher> sink_seen;
    sink_seen.reserve(graph.connections.size());

    for (const auto& c : graph.connections) {
        const Node* from = find_node(graph, c.from_node);
        if (from == nullptr) {
            std::ostringstream os;
            os << "connection references unknown from_node " << c.from_node;
            result.message = os.str();
            return result;
        }
        const Node* to = find_node(graph, c.to_node);
        if (to == nullptr) {
            std::ostringstream os;
            os << "connection references unknown to_node " << c.to_node;
            result.message = os.str();
            return result;
        }
        const Socket* src = find_socket(*from, c.from_socket,
                                        SocketDirection::Output);
        if (src == nullptr) {
            std::ostringstream os;
            os << "connection references unknown output socket '"
               << c.from_socket << "' on node " << c.from_node;
            result.message = os.str();
            return result;
        }
        const Socket* dst = find_socket(*to, c.to_socket,
                                        SocketDirection::Input);
        if (dst == nullptr) {
            std::ostringstream os;
            os << "connection references unknown input socket '"
               << c.to_socket << "' on node " << c.to_node;
            result.message = os.str();
            return result;
        }

        // Sink uniqueness.
        InputSinkKey key{c.to_node, c.to_socket};
        if (!sink_seen.insert(key).second) {
            std::ostringstream os;
            os << "input socket '" << c.to_socket << "' on node "
               << c.to_node << " has more than one incoming connection";
            result.message = os.str();
            return result;
        }

        // Type compatibility.
        if (!can_connect(src->type, dst->type)) {
            std::ostringstream os;
            os << "connection from " << c.from_node << "."
               << c.from_socket << " (" << socket_type_name(src->type)
               << ") to " << c.to_node << "." << c.to_socket
               << " (" << socket_type_name(dst->type)
               << ") is type-incompatible";
            result.message = os.str();
            return result;
        }
    }

    // 6. DAG.
    NodeId cycle_id = kInvalidNodeId;
    if (has_cycle(graph, cycle_id)) {
        std::ostringstream os;
        os << "cycle detected through node " << cycle_id;
        result.message = os.str();
        return result;
    }

    // 7. At least one terminal.
    bool any_terminal = false;
    for (const auto& n : graph.nodes) {
        if (is_terminal(n.type)) {
            any_terminal = true;
            break;
        }
    }
    if (!any_terminal) {
        result.message = "graph has no terminal node "
                         "(at least one DiffuseBSDF or Emission "
                         "is required)";
        return result;
    }

    // 8. Every input socket marked `required` is wired by some
    //    connection. v1 catalogue marks NO sockets required, so
    //    this loop is a no-op for any v1 graph; the
    //    infrastructure exists so a future node type with a
    //    no-default input can opt in.
    for (const auto& n : graph.nodes) {
        for (const auto& s : n.inputs) {
            if (!s.required) continue;
            bool wired = false;
            for (const auto& c : graph.connections) {
                if (c.to_node == n.id && c.to_socket == s.name) {
                    wired = true;
                    break;
                }
            }
            if (!wired) {
                std::ostringstream os;
                os << "required input socket '" << s.name
                   << "' on node " << n.id
                   << " (" << node_type_name(n.type)
                   << ") is not wired";
                result.message = os.str();
                return result;
            }
        }
    }

    result.ok = true;
    return result;
}

}
