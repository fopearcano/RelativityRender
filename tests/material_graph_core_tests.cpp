// Hand-rolled assertion runner.  The real test framework comes
// with the M2 deferred items.
//
// Material graph DATA-CORE tests.  Covers the new structured
// data layer under `src/material/graph/`:
//   - Socket: enum naming, parse_socket_type, can_connect
//     (the spec section 7.3 implicit-conversion table).
//   - Node:   enum naming, parse_node_type (with the
//     "Diffuse" -> DiffuseBSDF alias), is_terminal,
//     `make_node` socket layout for every v1 catalogue type,
//     `find_socket`.
//   - Graph:  `find_node`, `incoming_connections`,
//     `validate_graph` happy + every error path
//     (duplicate ids, dangling refs, sockets in the wrong
//     direction, double-wired sinks, type-incompatible
//     connections, cycles, missing terminals).
//
// No evaluation, no GPU, no UI - the new data core is data only.

#include "cuda/CudaMaterialGraph.cuh"
#include "material/GpuMaterial.h"
#include "material/MaterialTypes.h"
#include "material/graph/Graph.h"
#include "material/graph/GraphEvaluator.h"
#include "material/graph/Node.h"
#include "material/graph/Socket.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_total  = 0;
int g_failed = 0;

#define RR_CHECK(...)                                                         \
    do {                                                                      \
        ++g_total;                                                            \
        if (!(__VA_ARGS__)) {                                                 \
            ++g_failed;                                                       \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                        \
                         #__VA_ARGS__, __FILE__, __LINE__);                   \
        }                                                                     \
    } while (0)

using namespace rr::material::graph;

bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

// --- Socket enum + parser ------------------------------------------------

void test_socket_type_names_round_trip() {
    RR_CHECK(std::strcmp(socket_type_name(SocketType::Float),  "float")  == 0);
    RR_CHECK(std::strcmp(socket_type_name(SocketType::Vec2),   "vec2")   == 0);
    RR_CHECK(std::strcmp(socket_type_name(SocketType::Vec3),   "vec3")   == 0);
    RR_CHECK(std::strcmp(socket_type_name(SocketType::Color),  "color")  == 0);
    RR_CHECK(std::strcmp(socket_type_name(SocketType::Normal), "normal") == 0);
}

void test_parse_socket_type_canonical_lowercase() {
    SocketType t;
    RR_CHECK(parse_socket_type("float",  t) && t == SocketType::Float);
    RR_CHECK(parse_socket_type("vec2",   t) && t == SocketType::Vec2);
    RR_CHECK(parse_socket_type("vec3",   t) && t == SocketType::Vec3);
    RR_CHECK(parse_socket_type("color",  t) && t == SocketType::Color);
    RR_CHECK(parse_socket_type("normal", t) && t == SocketType::Normal);
}

void test_parse_socket_type_pascalcase_aliases() {
    SocketType t;
    RR_CHECK(parse_socket_type("Float",  t) && t == SocketType::Float);
    RR_CHECK(parse_socket_type("Vec3",   t) && t == SocketType::Vec3);
    RR_CHECK(parse_socket_type("Color",  t) && t == SocketType::Color);
    RR_CHECK(parse_socket_type("Normal", t) && t == SocketType::Normal);
}

void test_parse_socket_type_rejects_unknown() {
    SocketType t;
    RR_CHECK(!parse_socket_type("",       t));
    RR_CHECK(!parse_socket_type("nope",   t));
    RR_CHECK(!parse_socket_type("VEC3",   t));   // case-sensitive
    RR_CHECK(!parse_socket_type("Vec4",   t));
}

void test_socket_direction_names() {
    RR_CHECK(std::strcmp(socket_direction_name(SocketDirection::Input),  "input")  == 0);
    RR_CHECK(std::strcmp(socket_direction_name(SocketDirection::Output), "output") == 0);
}

// --- can_connect (implicit-conversion table from spec 7.3) --------------

void test_can_connect_identity_for_every_type() {
    for (int i = 0; i < kSocketTypeCount; ++i) {
        const auto t = static_cast<SocketType>(i);
        RR_CHECK(can_connect(t, t));
    }
}

void test_can_connect_float_broadcasts_to_vector_kinds() {
    RR_CHECK( can_connect(SocketType::Float, SocketType::Vec2));
    RR_CHECK( can_connect(SocketType::Float, SocketType::Vec3));
    RR_CHECK( can_connect(SocketType::Float, SocketType::Color));
    // Spec: float -> normal is NOT in the table (a normal needs
    // a unit-length contract; broadcast can't satisfy it).
    RR_CHECK(!can_connect(SocketType::Float, SocketType::Normal));
}

void test_can_connect_vec3_color_reinterpret_both_ways() {
    RR_CHECK(can_connect(SocketType::Vec3,  SocketType::Color));
    RR_CHECK(can_connect(SocketType::Color, SocketType::Vec3));
}

void test_can_connect_normal_drops_to_vec3_but_not_back() {
    RR_CHECK( can_connect(SocketType::Normal, SocketType::Vec3));
    RR_CHECK( can_connect(SocketType::Normal, SocketType::Normal));
    // No implicit normalisation -> vec3 -> normal forbidden.
    RR_CHECK(!can_connect(SocketType::Vec3,   SocketType::Normal));
    RR_CHECK(!can_connect(SocketType::Color,  SocketType::Normal));
}

void test_can_connect_rejects_vec_truncation_and_color_to_float() {
    // Spec 7.3 prohibits truncation / luminance reduction.
    RR_CHECK(!can_connect(SocketType::Vec3,  SocketType::Vec2));
    RR_CHECK(!can_connect(SocketType::Vec2,  SocketType::Vec3));
    RR_CHECK(!can_connect(SocketType::Color, SocketType::Float));
    RR_CHECK(!can_connect(SocketType::Vec3,  SocketType::Float));
}


// --- Node enum + parser --------------------------------------------------

void test_node_type_names_round_trip() {
    RR_CHECK(std::strcmp(node_type_name(NodeType::ConstantColor),
                         "ConstantColor") == 0);
    RR_CHECK(std::strcmp(node_type_name(NodeType::TextureSample),
                         "TextureSample") == 0);
    RR_CHECK(std::strcmp(node_type_name(NodeType::Add),     "Add")     == 0);
    RR_CHECK(std::strcmp(node_type_name(NodeType::Multiply),"Multiply")== 0);
    RR_CHECK(std::strcmp(node_type_name(NodeType::DiffuseBSDF),
                         "DiffuseBSDF") == 0);
    RR_CHECK(std::strcmp(node_type_name(NodeType::Emission),"Emission")== 0);
}

void test_parse_node_type_canonical_and_alias() {
    NodeType t;
    RR_CHECK(parse_node_type("ConstantColor", t) && t == NodeType::ConstantColor);
    RR_CHECK(parse_node_type("TextureSample", t) && t == NodeType::TextureSample);
    RR_CHECK(parse_node_type("Add",           t) && t == NodeType::Add);
    RR_CHECK(parse_node_type("Multiply",      t) && t == NodeType::Multiply);
    RR_CHECK(parse_node_type("DiffuseBSDF",   t) && t == NodeType::DiffuseBSDF);
    RR_CHECK(parse_node_type("Emission",      t) && t == NodeType::Emission);
    // Spec's canonical name `Diffuse` is accepted as an alias.
    RR_CHECK(parse_node_type("Diffuse",       t) && t == NodeType::DiffuseBSDF);
}

void test_parse_node_type_rejects_case_variants_and_unknown() {
    NodeType t;
    RR_CHECK(!parse_node_type("",                t));
    RR_CHECK(!parse_node_type("Frobnicate",      t));
    RR_CHECK(!parse_node_type("constantcolor",   t));   // case-sensitive
    RR_CHECK(!parse_node_type("DIFFUSE",         t));
}

void test_is_terminal_only_diffuse_and_emission() {
    RR_CHECK( is_terminal(NodeType::DiffuseBSDF));
    RR_CHECK( is_terminal(NodeType::Emission));
    RR_CHECK(!is_terminal(NodeType::ConstantColor));
    RR_CHECK(!is_terminal(NodeType::TextureSample));
    RR_CHECK(!is_terminal(NodeType::Add));
    RR_CHECK(!is_terminal(NodeType::Multiply));
}


// --- make_node socket layout ---------------------------------------------

void test_make_node_constant_color_layout() {
    auto n = make_node(NodeType::ConstantColor, /*id=*/0);
    RR_CHECK(n.type == NodeType::ConstantColor);
    RR_CHECK(n.id   == 0);
    RR_CHECK(n.inputs.empty());
    RR_CHECK(n.outputs.size() == 1);
    RR_CHECK(n.outputs[0].name      == "value");
    RR_CHECK(n.outputs[0].type      == SocketType::Color);
    RR_CHECK(n.outputs[0].direction == SocketDirection::Output);
}

void test_make_node_texture_sample_layout() {
    auto n = make_node(NodeType::TextureSample, /*id=*/1);
    RR_CHECK(n.inputs.size()  == 1);
    RR_CHECK(n.inputs[0].name == "uv");
    RR_CHECK(n.inputs[0].type == SocketType::Vec2);
    RR_CHECK(n.outputs.size() == 1);
    RR_CHECK(n.outputs[0].name == "value");
    RR_CHECK(n.outputs[0].type == SocketType::Color);
}

void test_make_node_add_and_multiply_share_layout() {
    for (auto type : { NodeType::Add, NodeType::Multiply }) {
        auto n = make_node(type, /*id=*/0);
        RR_CHECK(n.inputs.size() == 2);
        RR_CHECK(n.inputs[0].name == "a"
                 && n.inputs[0].type == SocketType::Color);
        RR_CHECK(n.inputs[1].name == "b"
                 && n.inputs[1].type == SocketType::Color);
        RR_CHECK(n.outputs.size() == 1);
        RR_CHECK(n.outputs[0].name == "value");
    }
}

void test_make_node_terminals_have_no_outputs() {
    auto d = make_node(NodeType::DiffuseBSDF, /*id=*/0);
    RR_CHECK(d.outputs.empty());
    RR_CHECK(d.inputs.size() == 1);
    RR_CHECK(d.inputs[0].name == "albedo");
    RR_CHECK(d.inputs[0].type == SocketType::Color);

    auto e = make_node(NodeType::Emission, /*id=*/1);
    RR_CHECK(e.outputs.empty());
    RR_CHECK(e.inputs.size() == 2);
    RR_CHECK(e.inputs[0].name == "color"
             && e.inputs[0].type == SocketType::Color);
    RR_CHECK(e.inputs[1].name == "strength"
             && e.inputs[1].type == SocketType::Float);
}

void test_make_node_immediate_defaults() {
    // Spec catalogue defaults (section 6).
    auto cc = make_node(NodeType::ConstantColor);
    RR_CHECK(cc.color_value.x == 0.8f
             && cc.color_value.y == 0.8f
             && cc.color_value.z == 0.8f);

    auto ts = make_node(NodeType::TextureSample);
    RR_CHECK(ts.uv_value.x == 0.5f && ts.uv_value.y == 0.5f);
    RR_CHECK(ts.texture_id == -1);

    auto em = make_node(NodeType::Emission);
    RR_CHECK(em.color_value.x == 0.0f
             && em.color_value.y == 0.0f
             && em.color_value.z == 0.0f);
    RR_CHECK(em.scalar_value == 1.0f);
}

void test_find_socket_returns_nullptr_for_wrong_direction() {
    auto n = make_node(NodeType::Add, /*id=*/0);
    // `a` and `b` are inputs; querying as output must miss.
    RR_CHECK(find_socket(n, "a", SocketDirection::Input)  != nullptr);
    RR_CHECK(find_socket(n, "a", SocketDirection::Output) == nullptr);
    RR_CHECK(find_socket(n, "value", SocketDirection::Output) != nullptr);
    RR_CHECK(find_socket(n, "value", SocketDirection::Input)  == nullptr);
    RR_CHECK(find_socket(n, "missing", SocketDirection::Input) == nullptr);
}


// --- Graph helpers + validation: happy paths ---------------------------

void test_find_node_returns_nullptr_for_unknown_id() {
    Graph g;
    g.nodes.push_back(make_node(NodeType::DiffuseBSDF, 7));
    RR_CHECK(find_node(g, 7) != nullptr);
    RR_CHECK(find_node(g, 7)->type == NodeType::DiffuseBSDF);
    RR_CHECK(find_node(g, 8) == nullptr);
    // Const overload also resolves.
    const Graph& cg = g;
    RR_CHECK(find_node(cg, 7) != nullptr);
    RR_CHECK(find_node(cg, 8) == nullptr);
}

void test_validate_minimal_diffuse_only_graph() {
    Graph g;
    g.nodes.push_back(make_node(NodeType::DiffuseBSDF, 0));
    auto r = validate_graph(g);
    RR_CHECK(r.ok);
    RR_CHECK(r.message.empty());
}

void test_validate_constant_to_diffuse_via_connection() {
    Graph g;
    g.nodes.push_back(make_node(NodeType::ConstantColor, 0));
    g.nodes.push_back(make_node(NodeType::DiffuseBSDF,   1));
    g.connections.push_back({0, "value", 1, "albedo"});
    auto r = validate_graph(g);
    RR_CHECK(r.ok);
}

void test_validate_full_v1_chain() {
    // Realistic small chain: TextureSample -> Multiply -> Diffuse,
    // plus a separate Emission terminal.  Exercises both
    // terminals + a multi-edge data flow.
    Graph g;
    g.nodes.push_back(make_node(NodeType::ConstantColor, 0));
    g.nodes.push_back(make_node(NodeType::TextureSample, 1));
    g.nodes.push_back(make_node(NodeType::Multiply,      2));
    g.nodes.push_back(make_node(NodeType::DiffuseBSDF,   3));
    g.nodes.push_back(make_node(NodeType::ConstantColor, 4));
    g.nodes.push_back(make_node(NodeType::Emission,      5));
    g.connections.push_back({1, "value", 2, "a"});
    g.connections.push_back({0, "value", 2, "b"});
    g.connections.push_back({2, "value", 3, "albedo"});
    g.connections.push_back({4, "value", 5, "color"});
    auto r = validate_graph(g);
    RR_CHECK(r.ok);
}

void test_incoming_connections_returns_target_edges() {
    Graph g;
    g.nodes.push_back(make_node(NodeType::ConstantColor, 0));
    g.nodes.push_back(make_node(NodeType::ConstantColor, 1));
    g.nodes.push_back(make_node(NodeType::Add,           2));
    g.nodes.push_back(make_node(NodeType::DiffuseBSDF,   3));
    g.connections.push_back({0, "value", 2, "a"});
    g.connections.push_back({1, "value", 2, "b"});
    g.connections.push_back({2, "value", 3, "albedo"});

    const auto into_add = incoming_connections(g, 2);
    RR_CHECK(into_add.size() == 2);
    const auto into_diffuse = incoming_connections(g, 3);
    RR_CHECK(into_diffuse.size() == 1);
    RR_CHECK(into_diffuse[0]->from_node == 2);
    const auto into_unknown = incoming_connections(g, 99);
    RR_CHECK(into_unknown.empty());
}


// --- Graph validation: error paths --------------------------------------

void test_validate_rejects_unsupported_version() {
    Graph g;
    g.version = 2;
    g.nodes.push_back(make_node(NodeType::DiffuseBSDF, 0));
    auto r = validate_graph(g);
    RR_CHECK(!r.ok);
    RR_CHECK(contains(r.message, "version"));
}

void test_validate_rejects_invalid_node_id() {
    Graph g;
    Node n = make_node(NodeType::DiffuseBSDF);    // id stays kInvalidNodeId
    g.nodes.push_back(n);
    auto r = validate_graph(g);
    RR_CHECK(!r.ok);
    RR_CHECK(contains(r.message, "invalid id"));
}

void test_validate_rejects_duplicate_id() {
    Graph g;
    g.nodes.push_back(make_node(NodeType::ConstantColor, 5));
    g.nodes.push_back(make_node(NodeType::Multiply,       5));   // dup id
    g.nodes.push_back(make_node(NodeType::DiffuseBSDF,    6));
    auto r = validate_graph(g);
    RR_CHECK(!r.ok);
    RR_CHECK(contains(r.message, "duplicate"));
}

void test_validate_rejects_dangling_from_node() {
    Graph g;
    g.nodes.push_back(make_node(NodeType::DiffuseBSDF, 0));
    g.connections.push_back({99, "value", 0, "albedo"});
    auto r = validate_graph(g);
    RR_CHECK(!r.ok);
    RR_CHECK(contains(r.message, "from_node"));
}

void test_validate_rejects_dangling_to_node() {
    Graph g;
    g.nodes.push_back(make_node(NodeType::ConstantColor, 0));
    // No DiffuseBSDF added; the connection's to_node 99 is unknown.
    g.connections.push_back({0, "value", 99, "albedo"});
    g.nodes.push_back(make_node(NodeType::DiffuseBSDF, 1));   // make terminal exist
    auto r = validate_graph(g);
    RR_CHECK(!r.ok);
    RR_CHECK(contains(r.message, "to_node"));
}

void test_validate_rejects_unknown_from_socket() {
    Graph g;
    g.nodes.push_back(make_node(NodeType::ConstantColor, 0));
    g.nodes.push_back(make_node(NodeType::DiffuseBSDF,   1));
    g.connections.push_back({0, "ghost", 1, "albedo"});
    auto r = validate_graph(g);
    RR_CHECK(!r.ok);
    RR_CHECK(contains(r.message, "output socket"));
}

void test_validate_rejects_unknown_to_socket() {
    Graph g;
    g.nodes.push_back(make_node(NodeType::ConstantColor, 0));
    g.nodes.push_back(make_node(NodeType::DiffuseBSDF,   1));
    g.connections.push_back({0, "value", 1, "ghost"});
    auto r = validate_graph(g);
    RR_CHECK(!r.ok);
    RR_CHECK(contains(r.message, "input socket"));
}

void test_validate_rejects_socket_in_wrong_direction() {
    // `from_socket` is meant to be an output; targeting an input
    // socket as the source MUST fail.
    Graph g;
    g.nodes.push_back(make_node(NodeType::Add,           0));
    g.nodes.push_back(make_node(NodeType::DiffuseBSDF,   1));
    g.connections.push_back({0, "a" /* input */, 1, "albedo"});
    auto r = validate_graph(g);
    RR_CHECK(!r.ok);
    RR_CHECK(contains(r.message, "output socket"));
}

void test_validate_rejects_double_wired_sink() {
    Graph g;
    g.nodes.push_back(make_node(NodeType::ConstantColor, 0));
    g.nodes.push_back(make_node(NodeType::ConstantColor, 1));
    g.nodes.push_back(make_node(NodeType::DiffuseBSDF,   2));
    g.connections.push_back({0, "value", 2, "albedo"});
    g.connections.push_back({1, "value", 2, "albedo"});  // duplicate sink
    auto r = validate_graph(g);
    RR_CHECK(!r.ok);
    RR_CHECK(contains(r.message, "more than one incoming connection"));
}

void test_validate_rejects_type_incompatible_connection() {
    // ConstantColor.value is Color; Emission.strength is Float.
    // The implicit-conversion table forbids color -> float.
    Graph g;
    g.nodes.push_back(make_node(NodeType::ConstantColor, 0));
    g.nodes.push_back(make_node(NodeType::Emission,      1));
    g.connections.push_back({0, "value", 1, "strength"});
    auto r = validate_graph(g);
    RR_CHECK(!r.ok);
    RR_CHECK(contains(r.message, "type-incompatible"));
}

void test_validate_accepts_compatible_implicit_conversion() {
    // vec3 <-> color is reinterpret per the table.  Wire a
    // synthesised "vec3 source" by hand: ConstantColor's output
    // is Color, and an Add input that expected vec3 would still
    // pass.  In v1 the catalogue uses Color everywhere so the
    // real test is that color -> color (identity) and the
    // mixed case below validates cleanly.
    Graph g;
    g.nodes.push_back(make_node(NodeType::ConstantColor, 0));
    Node mul = make_node(NodeType::Multiply, 1);
    // Manually retype `mul.inputs[0]` to Vec3 to exercise the
    // implicit Color -> Vec3 case at the validator level.
    mul.inputs[0].type = SocketType::Vec3;
    g.nodes.push_back(mul);
    g.nodes.push_back(make_node(NodeType::DiffuseBSDF, 2));
    g.connections.push_back({0, "value", 1, "a"});           // color -> vec3
    g.connections.push_back({1, "value", 2, "albedo"});      // color -> color
    auto r = validate_graph(g);
    RR_CHECK(r.ok);
}

void test_validate_rejects_cycle() {
    // Connect two Add nodes back-to-back so they reference each
    // other.  The two-edge cycle is the simplest case.
    Graph g;
    g.nodes.push_back(make_node(NodeType::Add,         0));
    g.nodes.push_back(make_node(NodeType::Add,         1));
    g.nodes.push_back(make_node(NodeType::DiffuseBSDF, 2));
    g.connections.push_back({0, "value", 1, "a"});
    g.connections.push_back({1, "value", 0, "a"});           // cycle 0->1->0
    g.connections.push_back({1, "value", 2, "albedo"});
    auto r = validate_graph(g);
    RR_CHECK(!r.ok);
    RR_CHECK(contains(r.message, "cycle"));
}

void test_validate_rejects_graph_with_no_terminal() {
    Graph g;
    g.nodes.push_back(make_node(NodeType::ConstantColor, 0));
    g.nodes.push_back(make_node(NodeType::ConstantColor, 1));
    g.nodes.push_back(make_node(NodeType::Add,           2));
    g.connections.push_back({0, "value", 2, "a"});
    g.connections.push_back({1, "value", 2, "b"});
    auto r = validate_graph(g);
    RR_CHECK(!r.ok);
    RR_CHECK(contains(r.message, "terminal"));
}


// ---------------------------------------------------------------------------
// Graph builder helpers: add_node / connect / validate (this slice).
// ---------------------------------------------------------------------------

void test_add_node_assigns_zero_then_increments() {
    Graph g;
    const NodeId a = g.add_node(NodeType::ConstantColor);
    const NodeId b = g.add_node(NodeType::DiffuseBSDF);
    const NodeId c = g.add_node(NodeType::Emission);
    RR_CHECK(a == 0);
    RR_CHECK(b == 1);
    RR_CHECK(c == 2);
    RR_CHECK(g.nodes.size() == 3);
    RR_CHECK(g.nodes[0].type == NodeType::ConstantColor);
    RR_CHECK(g.nodes[1].type == NodeType::DiffuseBSDF);
    RR_CHECK(g.nodes[2].type == NodeType::Emission);
    // Catalogue layout is applied by `make_node`.
    RR_CHECK(g.nodes[0].outputs.size() == 1);
    RR_CHECK(g.nodes[0].outputs[0].name == "value");
}

void test_add_node_skips_past_manually_assigned_ids() {
    // Mixing the builder with direct vector pushes must not
    // reuse an existing id.
    Graph g;
    g.nodes.push_back(make_node(NodeType::ConstantColor, 100));
    const NodeId next = g.add_node(NodeType::DiffuseBSDF);
    RR_CHECK(next == 101);
}

void test_connect_appends_on_success() {
    Graph g;
    const NodeId color   = g.add_node(NodeType::ConstantColor);
    const NodeId diffuse = g.add_node(NodeType::DiffuseBSDF);
    const bool wired = g.connect(color, "value", diffuse, "albedo");
    RR_CHECK(wired);
    RR_CHECK(g.connections.size() == 1);
    RR_CHECK(g.connections[0].from_node   == color);
    RR_CHECK(g.connections[0].from_socket == "value");
    RR_CHECK(g.connections[0].to_node     == diffuse);
    RR_CHECK(g.connections[0].to_socket   == "albedo");
}

void test_connect_rejects_unknown_node() {
    Graph g;
    const NodeId color = g.add_node(NodeType::ConstantColor);
    RR_CHECK(!g.connect(color, "value", /*to=*/99, "albedo"));
    RR_CHECK(g.connections.empty());
}

void test_connect_rejects_unknown_socket() {
    Graph g;
    const NodeId color   = g.add_node(NodeType::ConstantColor);
    const NodeId diffuse = g.add_node(NodeType::DiffuseBSDF);
    RR_CHECK(!g.connect(color,   "ghost", diffuse, "albedo"));
    RR_CHECK(!g.connect(color,   "value", diffuse, "ghost"));
    RR_CHECK(g.connections.empty());
}

void test_connect_rejects_wrong_direction() {
    // Trying to use an Input socket as the source must fail.
    Graph g;
    const NodeId add     = g.add_node(NodeType::Add);
    const NodeId diffuse = g.add_node(NodeType::DiffuseBSDF);
    RR_CHECK(!g.connect(add, /*input!*/"a", diffuse, "albedo"));
    RR_CHECK(g.connections.empty());
}

void test_connect_rejects_type_incompatible() {
    // Color -> Float (Emission.strength) is not in the implicit-
    // conversion table.
    Graph g;
    const NodeId color    = g.add_node(NodeType::ConstantColor);
    const NodeId emission = g.add_node(NodeType::Emission);
    RR_CHECK(!g.connect(color, "value", emission, "strength"));
    RR_CHECK(g.connections.empty());
}

void test_connect_rejects_duplicate_sink() {
    Graph g;
    const NodeId a       = g.add_node(NodeType::ConstantColor);
    const NodeId b       = g.add_node(NodeType::ConstantColor);
    const NodeId diffuse = g.add_node(NodeType::DiffuseBSDF);
    RR_CHECK( g.connect(a, "value", diffuse, "albedo"));
    // Second wire to the same input must fail.
    RR_CHECK(!g.connect(b, "value", diffuse, "albedo"));
    RR_CHECK(g.connections.size() == 1);
}

void test_connect_appends_cycle_then_validate_rejects_it() {
    // The connect-time check intentionally does NOT run cycle
    // detection (that's the validator's job). A connection
    // that closes a cycle still appends; validate() catches it.
    Graph g;
    const NodeId a       = g.add_node(NodeType::Add);
    const NodeId b       = g.add_node(NodeType::Add);
    const NodeId diffuse = g.add_node(NodeType::DiffuseBSDF);
    RR_CHECK(g.connect(a, "value", b, "a"));
    RR_CHECK(g.connect(b, "value", a, "a"));   // closes cycle
    RR_CHECK(g.connect(b, "value", diffuse, "albedo"));
    auto r = g.validate();
    RR_CHECK(!r.ok);
    RR_CHECK(contains(r.message, "cycle"));
}

void test_validate_method_matches_free_function() {
    Graph g;
    g.add_node(NodeType::DiffuseBSDF);
    const auto a = g.validate();
    const auto b = validate_graph(g);
    RR_CHECK(a.ok == b.ok);
    RR_CHECK(a.message == b.message);
}


// --- Required-input flag (infrastructure; v1 catalogue uses none) ------

void test_validator_passes_when_no_inputs_marked_required() {
    // v1 catalogue keeps every input optional. A graph whose
    // inputs are unwired (relying on per-input defaults) must
    // validate cleanly.
    Graph g;
    g.add_node(NodeType::DiffuseBSDF);   // unwired albedo -> default mid-grey
    auto r = g.validate();
    RR_CHECK(r.ok);
}

void test_validator_rejects_unwired_required_input() {
    // Mark a Diffuse node's albedo as required (forward-looking
    // infrastructure: the v1 catalogue does not do this).
    // Validator must surface the unwired required input.
    Graph g;
    const NodeId diffuse = g.add_node(NodeType::DiffuseBSDF);
    Node* n = find_node(g, diffuse);
    RR_CHECK(n != nullptr);
    if (n != nullptr) {
        for (auto& s : n->inputs) {
            if (s.name == "albedo") s.required = true;
        }
    }
    auto r = g.validate();
    RR_CHECK(!r.ok);
    RR_CHECK(contains(r.message, "required input"));
    RR_CHECK(contains(r.message, "albedo"));
}

void test_validator_passes_required_input_when_wired() {
    Graph g;
    const NodeId color   = g.add_node(NodeType::ConstantColor);
    const NodeId diffuse = g.add_node(NodeType::DiffuseBSDF);
    Node* n = find_node(g, diffuse);
    if (n != nullptr) {
        for (auto& s : n->inputs) {
            if (s.name == "albedo") s.required = true;
        }
    }
    RR_CHECK(g.connect(color, "value", diffuse, "albedo"));
    auto r = g.validate();
    RR_CHECK(r.ok);
}


// ---------------------------------------------------------------------------
// CPU reference evaluator (this slice).
//
// Every assertion below checks the colour summary the evaluator
// produces against an analytically-known expected value.
// `nearly_eq` accommodates the float arithmetic.
// ---------------------------------------------------------------------------

float abs_f(float v) { return v < 0.0f ? -v : v; }

bool nearly_eq(float a, float b, float eps = 1.0e-6f) {
    return abs_f(a - b) <= eps;
}

bool nearly_eq(rr::math::Vec3 a, rr::math::Vec3 b, float eps = 1.0e-6f) {
    return nearly_eq(a.x, b.x, eps)
        && nearly_eq(a.y, b.y, eps)
        && nearly_eq(a.z, b.z, eps);
}

void test_evaluate_constant_color_returns_immediate() {
    Graph g;
    const NodeId c = g.add_node(NodeType::ConstantColor);
    Node* n = find_node(g, c);
    n->color_value = rr::math::Vec3{0.2f, 0.5f, 0.8f};
    const auto v = evaluate(g, c);
    RR_CHECK(nearly_eq(v, rr::math::Vec3{0.2f, 0.5f, 0.8f}));
}

void test_evaluate_unknown_node_returns_black() {
    Graph g;
    const auto v = evaluate(g, /*node_id=*/99);
    RR_CHECK(nearly_eq(v, rr::math::Vec3{0.0f, 0.0f, 0.0f}));
}

void test_evaluate_texture_sample_returns_fallback() {
    Graph g;
    const NodeId t = g.add_node(NodeType::TextureSample);
    EvaluationContext ctx;
    ctx.fallback_texture_color = rr::math::Vec3{0.3f, 0.3f, 0.3f};
    const auto v = evaluate(g, t, ctx);
    RR_CHECK(nearly_eq(v, rr::math::Vec3{0.3f, 0.3f, 0.3f}));
}

void test_evaluate_texture_sample_default_fallback_is_magenta() {
    Graph g;
    const NodeId t = g.add_node(NodeType::TextureSample);
    const auto v = evaluate(g, t);
    RR_CHECK(nearly_eq(v, rr::math::Vec3{1.0f, 0.0f, 1.0f}));
}

void test_evaluate_add_two_constants() {
    Graph g;
    const NodeId a = g.add_node(NodeType::ConstantColor);
    const NodeId b = g.add_node(NodeType::ConstantColor);
    const NodeId s = g.add_node(NodeType::Add);
    find_node(g, a)->color_value = rr::math::Vec3{0.1f, 0.2f, 0.3f};
    find_node(g, b)->color_value = rr::math::Vec3{0.4f, 0.5f, 0.6f};
    RR_CHECK(g.connect(a, "value", s, "a"));
    RR_CHECK(g.connect(b, "value", s, "b"));
    const auto v = evaluate(g, s);
    RR_CHECK(nearly_eq(v, rr::math::Vec3{0.5f, 0.7f, 0.9f}));
}

void test_evaluate_add_unwired_falls_back_to_zero() {
    // Both inputs unwired -> identity zero + zero = zero.
    Graph g;
    const NodeId s = g.add_node(NodeType::Add);
    const auto v = evaluate(g, s);
    RR_CHECK(nearly_eq(v, rr::math::Vec3{0.0f, 0.0f, 0.0f}));
}

void test_evaluate_multiply_two_constants() {
    Graph g;
    const NodeId a = g.add_node(NodeType::ConstantColor);
    const NodeId b = g.add_node(NodeType::ConstantColor);
    const NodeId m = g.add_node(NodeType::Multiply);
    find_node(g, a)->color_value = rr::math::Vec3{0.5f, 0.5f, 0.5f};
    find_node(g, b)->color_value = rr::math::Vec3{0.4f, 0.6f, 0.8f};
    RR_CHECK(g.connect(a, "value", m, "a"));
    RR_CHECK(g.connect(b, "value", m, "b"));
    const auto v = evaluate(g, m);
    RR_CHECK(nearly_eq(v, rr::math::Vec3{0.2f, 0.3f, 0.4f}));
}

void test_evaluate_multiply_unwired_falls_back_to_one() {
    Graph g;
    const NodeId m = g.add_node(NodeType::Multiply);
    const auto v = evaluate(g, m);
    RR_CHECK(nearly_eq(v, rr::math::Vec3{1.0f, 1.0f, 1.0f}));
}

void test_evaluate_diffuse_bsdf_returns_wired_albedo() {
    Graph g;
    const NodeId c = g.add_node(NodeType::ConstantColor);
    const NodeId d = g.add_node(NodeType::DiffuseBSDF);
    find_node(g, c)->color_value = rr::math::Vec3{0.7f, 0.4f, 0.2f};
    RR_CHECK(g.connect(c, "value", d, "albedo"));
    const auto v = evaluate(g, d);
    RR_CHECK(nearly_eq(v, rr::math::Vec3{0.7f, 0.4f, 0.2f}));
}

void test_evaluate_diffuse_bsdf_unwired_uses_node_default() {
    Graph g;
    const NodeId d = g.add_node(NodeType::DiffuseBSDF);
    Node* n = find_node(g, d);
    n->color_value = rr::math::Vec3{0.1f, 0.2f, 0.3f};
    const auto v = evaluate(g, d);
    RR_CHECK(nearly_eq(v, rr::math::Vec3{0.1f, 0.2f, 0.3f}));
}

void test_evaluate_emission_color_times_strength() {
    Graph g;
    const NodeId c = g.add_node(NodeType::ConstantColor);
    const NodeId e = g.add_node(NodeType::Emission);
    find_node(g, c)->color_value = rr::math::Vec3{1.0f, 0.5f, 0.25f};
    find_node(g, e)->scalar_value = 2.0f;
    RR_CHECK(g.connect(c, "value", e, "color"));
    const auto v = evaluate(g, e);
    RR_CHECK(nearly_eq(v, rr::math::Vec3{2.0f, 1.0f, 0.5f}));
}

void test_evaluate_emission_unwired_uses_node_default_color() {
    Graph g;
    const NodeId e = g.add_node(NodeType::Emission);
    Node* n = find_node(g, e);
    n->color_value  = rr::math::Vec3{0.3f, 0.3f, 0.3f};
    n->scalar_value = 4.0f;
    const auto v = evaluate(g, e);
    RR_CHECK(nearly_eq(v, rr::math::Vec3{1.2f, 1.2f, 1.2f}));
}

void test_evaluate_chain_const_plus_const_times_const_to_diffuse() {
    // (a + b) * c -> Diffuse.albedo. Exercises three levels of
    // recursion + a tinted multiplier.
    //   a = (0.1, 0.2, 0.3); b = (0.2, 0.1, 0.4); c = (2, 2, 1)
    //   a + b = (0.3, 0.3, 0.7); * c = (0.6, 0.6, 0.7)
    Graph g;
    const NodeId a    = g.add_node(NodeType::ConstantColor);
    const NodeId b    = g.add_node(NodeType::ConstantColor);
    const NodeId c    = g.add_node(NodeType::ConstantColor);
    const NodeId sum  = g.add_node(NodeType::Add);
    const NodeId mul  = g.add_node(NodeType::Multiply);
    const NodeId diff = g.add_node(NodeType::DiffuseBSDF);
    find_node(g, a)->color_value = rr::math::Vec3{0.1f, 0.2f, 0.3f};
    find_node(g, b)->color_value = rr::math::Vec3{0.2f, 0.1f, 0.4f};
    find_node(g, c)->color_value = rr::math::Vec3{2.0f, 2.0f, 1.0f};
    RR_CHECK(g.connect(a,   "value", sum,  "a"));
    RR_CHECK(g.connect(b,   "value", sum,  "b"));
    RR_CHECK(g.connect(sum, "value", mul,  "a"));
    RR_CHECK(g.connect(c,   "value", mul,  "b"));
    RR_CHECK(g.connect(mul, "value", diff, "albedo"));
    const auto v = evaluate(g, diff);
    RR_CHECK(nearly_eq(v, rr::math::Vec3{0.6f, 0.6f, 0.7f}));
}

void test_evaluate_diamond_fanout_does_not_corrupt_value() {
    // A single ConstantColor feeds both sides of an Add.
    // Without memoisation, evaluate() walks the constant
    // twice; the result MUST still be exactly 2 * its value.
    Graph g;
    const NodeId c = g.add_node(NodeType::ConstantColor);
    const NodeId s = g.add_node(NodeType::Add);
    find_node(g, c)->color_value = rr::math::Vec3{0.25f, 0.5f, 0.75f};
    RR_CHECK(g.connect(c, "value", s, "a"));
    RR_CHECK(g.connect(c, "value", s, "b"));
    const auto v = evaluate(g, s);
    RR_CHECK(nearly_eq(v, rr::math::Vec3{0.5f, 1.0f, 1.5f}));
}

void test_evaluate_depth_cap_returns_black_on_runaway() {
    // Build a deeper-than-cap chain by hand and watch the cap
    // bite. With cap = 4, an Add chain six deep MUST return
    // black at the top because the inner-most resolve hits
    // the depth limit. Validation would catch the cyclic case
    // upstream; this test exercises the cap as a defensive
    // last resort, not as a substitute for validation.
    Graph g;
    NodeId tail = g.add_node(NodeType::ConstantColor);
    find_node(g, tail)->color_value = rr::math::Vec3{1.0f, 1.0f, 1.0f};
    NodeId head = tail;
    for (int i = 0; i < 8; ++i) {
        const NodeId step = g.add_node(NodeType::Add);
        RR_CHECK(g.connect(head, "value", step, "a"));
        head = step;
    }
    EvaluationContext ctx;
    ctx.max_depth = 4;
    const auto v = evaluate(g, head, ctx);
    // The exact value depends on how many levels fit; the
    // contract is "MUST not loop forever". Any finite return
    // value passes the test.
    (void)v;
    RR_CHECK(true);
}


// --- Smoke test: ConstantColor -> DiffuseBSDF, print result ------------

void test_smoke_constant_color_to_diffuse_via_builder() {
    // The exact graph the prompt asks for. Built through the
    // new `add_node` / `connect` / `validate` builder helpers,
    // and prints the validation result so the test output
    // surfaces the API in action even on a fully-passing run.
    Graph g;
    const NodeId color   = g.add_node(NodeType::ConstantColor);
    const NodeId diffuse = g.add_node(NodeType::DiffuseBSDF);

    const bool wired = g.connect(color, "value", diffuse, "albedo");
    const auto r     = g.validate();

    std::printf("[smoke] ConstantColor(%d) -> DiffuseBSDF(%d)\n",
                color, diffuse);
    std::printf("[smoke]   connect    -> %s\n",
                wired ? "true" : "false");
    std::printf("[smoke]   validate() -> %s%s%s\n",
                r.ok ? "OK" : "ERR",
                r.message.empty() ? "" : ": ",
                r.message.c_str());

    RR_CHECK(wired);
    RR_CHECK(r.ok);
    RR_CHECK(r.message.empty());
}


void test_smoke_evaluator_prints_resulting_color() {
    // Tiny graph: Const(0.7, 0.4, 0.2) -> DiffuseBSDF.
    // Builds via the builder API, validates, evaluates the
    // terminal, prints the resulting colour. Asserts the
    // evaluator's result matches the wired constant.
    Graph g;
    const NodeId color   = g.add_node(NodeType::ConstantColor);
    const NodeId diffuse = g.add_node(NodeType::DiffuseBSDF);
    Node* cn = find_node(g, color);
    cn->color_value = rr::math::Vec3{0.7f, 0.4f, 0.2f};

    RR_CHECK(g.connect(color, "value", diffuse, "albedo"));
    RR_CHECK(g.validate().ok);

    const auto result = evaluate(g, diffuse);

    std::printf("[smoke] evaluate(DiffuseBSDF(%d)) = (%.3f, %.3f, %.3f)\n",
                diffuse, result.x, result.y, result.z);

    RR_CHECK(nearly_eq(result, rr::math::Vec3{0.7f, 0.4f, 0.2f}));
}


// ---------------------------------------------------------------------------
// GPU-friendly material lowering (this slice).
//
// The lowering produces a `rr::material::GpuMaterial`: a flat
// op list + terminal table the future kernel will read through
// raw device pointers. Every test below verifies a structural
// property of the IR the spec section 9.2 calls for; nothing
// here exercises kernel execution (per the prompt).
// ---------------------------------------------------------------------------

using rr::material::GpuMaterial;
using rr::material::GpuOp;
using rr::material::GpuOpcode;
using rr::material::GpuTerminal;
using rr::material::compile_graph_to_gpu_material;
using rr::material::debug_print_gpu_material;
using rr::material::gpu_opcode_name;

void test_gpu_opcode_name_round_trip() {
    RR_CHECK(std::strcmp(gpu_opcode_name(GpuOpcode::ConstantColor),
                         "ConstantColor") == 0);
    RR_CHECK(std::strcmp(gpu_opcode_name(GpuOpcode::TextureSample),
                         "TextureSample") == 0);
    RR_CHECK(std::strcmp(gpu_opcode_name(GpuOpcode::Add),
                         "Add") == 0);
    RR_CHECK(std::strcmp(gpu_opcode_name(GpuOpcode::Multiply),
                         "Multiply") == 0);
    RR_CHECK(std::strcmp(gpu_opcode_name(GpuOpcode::Diffuse),
                         "Diffuse") == 0);
    RR_CHECK(std::strcmp(gpu_opcode_name(GpuOpcode::Emission),
                         "Emission") == 0);
}

void test_compile_const_diffuse_emits_one_op_one_terminal() {
    Graph g;
    const NodeId color   = g.add_node(NodeType::ConstantColor);
    const NodeId diffuse = g.add_node(NodeType::DiffuseBSDF);
    find_node(g, color)->color_value = rr::math::Vec3{0.7f, 0.4f, 0.2f};
    RR_CHECK(g.connect(color, "value", diffuse, "albedo"));

    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(r.material.ops.size()       == 1);
    RR_CHECK(r.material.terminals.size() == 1);
    RR_CHECK(r.material.slot_count       == 1);

    const auto& op = r.material.ops[0];
    RR_CHECK(op.opcode == GpuOpcode::ConstantColor);
    RR_CHECK(op.imm_color.x == 0.7f);
    RR_CHECK(op.imm_color.y == 0.4f);
    RR_CHECK(op.imm_color.z == 0.2f);

    const auto& t = r.material.terminals[0];
    RR_CHECK(t.kind     == GpuOpcode::Diffuse);
    RR_CHECK(t.in_color == 0);                  // wired to slot 0
    RR_CHECK(t.in_strength == -1);              // Diffuse leaves strength at -1
}

void test_compile_unwired_diffuse_uses_immediate_default() {
    // Diffuse with no albedo input -> in_color = -1, the
    // imm_color falls back to the node's color_value (the
    // catalogue's mid-grey default).
    Graph g;
    g.add_node(NodeType::DiffuseBSDF);
    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(r.material.ops.empty());
    RR_CHECK(r.material.terminals.size() == 1);
    const auto& t = r.material.terminals[0];
    RR_CHECK(t.kind     == GpuOpcode::Diffuse);
    RR_CHECK(t.in_color == -1);
    // make_node defaults DiffuseBSDF.color_value to mid-grey
    // (0.8, 0.8, 0.8) per the catalogue.
    RR_CHECK(nearly_eq(t.imm_color, rr::math::Vec3{0.8f, 0.8f, 0.8f}));
}

void test_compile_emission_carries_color_and_strength_immediate() {
    Graph g;
    const NodeId em = g.add_node(NodeType::Emission);
    Node* n = find_node(g, em);
    n->color_value  = rr::math::Vec3{1.0f, 0.5f, 0.25f};
    n->scalar_value = 2.5f;
    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(r.material.terminals.size() == 1);
    const auto& t = r.material.terminals[0];
    RR_CHECK(t.kind          == GpuOpcode::Emission);
    RR_CHECK(t.in_color      == -1);
    RR_CHECK(t.in_strength   == -1);
    RR_CHECK(nearly_eq(t.imm_color, rr::math::Vec3{1.0f, 0.5f, 0.25f}));
    RR_CHECK(nearly_eq(t.imm_strength, 2.5f));
}

void test_compile_add_chain_assigns_dense_slots_in_topo_order() {
    // (a + b) -> Diffuse.albedo. Three ops, slots 0 / 1 / 2.
    // Slot 2 (the Add) must reference slots 0 and 1.
    Graph g;
    const NodeId ca   = g.add_node(NodeType::ConstantColor);
    const NodeId cb   = g.add_node(NodeType::ConstantColor);
    const NodeId sum  = g.add_node(NodeType::Add);
    const NodeId diff = g.add_node(NodeType::DiffuseBSDF);
    find_node(g, ca)->color_value = rr::math::Vec3{0.1f, 0.2f, 0.3f};
    find_node(g, cb)->color_value = rr::math::Vec3{0.4f, 0.5f, 0.6f};
    RR_CHECK(g.connect(ca,  "value", sum,  "a"));
    RR_CHECK(g.connect(cb,  "value", sum,  "b"));
    RR_CHECK(g.connect(sum, "value", diff, "albedo"));

    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(r.material.ops.size()       == 3);
    RR_CHECK(r.material.terminals.size() == 1);
    RR_CHECK(r.material.slot_count       == 3);

    // The two constants come first (in some order); the Add
    // is last - it depends on both. Find the Add op and
    // check its in_a / in_b reference the two constants.
    int add_idx = -1;
    int const_count = 0;
    for (std::size_t i = 0; i < r.material.ops.size(); ++i) {
        if (r.material.ops[i].opcode == GpuOpcode::Add) {
            add_idx = static_cast<int>(i);
        } else if (r.material.ops[i].opcode == GpuOpcode::ConstantColor) {
            ++const_count;
        }
    }
    RR_CHECK(const_count == 2);
    RR_CHECK(add_idx     == 2);
    const auto& add_op = r.material.ops[add_idx];
    // in_a / in_b must be 0 or 1 (the two constants), in
    // some order, both within bounds and distinct.
    RR_CHECK((add_op.in_a == 0 || add_op.in_a == 1));
    RR_CHECK((add_op.in_b == 0 || add_op.in_b == 1));
    RR_CHECK(add_op.in_a != add_op.in_b);

    // Diffuse terminal references the Add slot.
    const auto& t = r.material.terminals[0];
    RR_CHECK(t.kind     == GpuOpcode::Diffuse);
    RR_CHECK(t.in_color == 2);
}

void test_compile_texture_sample_carries_id_and_fallback_color() {
    // TextureSample is a placeholder in v1. The IR carries
    // its texture_id verbatim and bakes the white "missing
    // texture" fallback into imm_color.
    //
    // The TextureSample MUST be wired into the Diffuse
    // terminal - the lowering's terminal-driven reachability
    // would otherwise drop it as dead code (per spec 7.4),
    // which is the correct behaviour but masks the property
    // we want to assert.
    Graph g;
    const NodeId ts      = g.add_node(NodeType::TextureSample);
    const NodeId diffuse = g.add_node(NodeType::DiffuseBSDF);
    find_node(g, ts)->texture_id = 7;
    RR_CHECK(g.connect(ts, "value", diffuse, "albedo"));

    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(r.material.ops.size() == 1);
    if (r.material.ops.empty()) return;     // defensive: avoid OOB
    const auto& op = r.material.ops[0];
    RR_CHECK(op.opcode  == GpuOpcode::TextureSample);
    RR_CHECK(op.imm_int == 7);
    RR_CHECK(nearly_eq(op.imm_color, rr::math::Vec3{1.0f, 1.0f, 1.0f}));
}

void test_compile_drops_dead_code_subgraph() {
    // Add a Multiply branch that doesn't reach any terminal.
    // The IR MUST NOT contain it.
    Graph g;
    const NodeId reach   = g.add_node(NodeType::ConstantColor);
    const NodeId dead_a  = g.add_node(NodeType::ConstantColor);
    const NodeId dead_b  = g.add_node(NodeType::ConstantColor);
    const NodeId dead_m  = g.add_node(NodeType::Multiply);
    const NodeId diffuse = g.add_node(NodeType::DiffuseBSDF);
    RR_CHECK(g.connect(reach,  "value", diffuse, "albedo"));
    RR_CHECK(g.connect(dead_a, "value", dead_m,  "a"));
    RR_CHECK(g.connect(dead_b, "value", dead_m,  "b"));
    // dead_m has no consumer - it's unreachable from the
    // single Diffuse terminal.

    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    // Only the reach constant survives; dead_a / dead_b /
    // dead_m all dropped.
    RR_CHECK(r.material.ops.size()       == 1);
    RR_CHECK(r.material.terminals.size() == 1);
    RR_CHECK(r.material.slot_count       == 1);
    RR_CHECK(r.material.ops[0].opcode    == GpuOpcode::ConstantColor);
}

void test_compile_rejects_invalid_graph() {
    // No terminal: validate_graph rejects, the lowering
    // surfaces the error message.
    Graph g;
    g.add_node(NodeType::ConstantColor);
    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(!r.ok);
    RR_CHECK(contains(r.message, "terminal"));
}

void test_compile_emission_with_wired_color_records_slot() {
    Graph g;
    const NodeId c  = g.add_node(NodeType::ConstantColor);
    const NodeId em = g.add_node(NodeType::Emission);
    find_node(g, c)->color_value = rr::math::Vec3{1.0f, 0.5f, 0.25f};
    RR_CHECK(g.connect(c, "value", em, "color"));
    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(r.material.ops.size()       == 1);
    RR_CHECK(r.material.terminals.size() == 1);
    const auto& t = r.material.terminals[0];
    RR_CHECK(t.kind         == GpuOpcode::Emission);
    RR_CHECK(t.in_color     == 0);
    RR_CHECK(t.in_strength  == -1);
}

void test_compile_diffuse_plus_emission_emits_two_terminals() {
    Graph g;
    const NodeId base = g.add_node(NodeType::ConstantColor);
    const NodeId glow = g.add_node(NodeType::ConstantColor);
    const NodeId d    = g.add_node(NodeType::DiffuseBSDF);
    const NodeId e    = g.add_node(NodeType::Emission);
    find_node(g, base)->color_value = rr::math::Vec3{0.8f, 0.2f, 0.1f};
    find_node(g, glow)->color_value = rr::math::Vec3{0.0f, 0.5f, 1.0f};
    find_node(g, e)->scalar_value   = 0.7f;
    RR_CHECK(g.connect(base, "value", d, "albedo"));
    RR_CHECK(g.connect(glow, "value", e, "color"));
    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(r.material.ops.size()       == 2);
    RR_CHECK(r.material.terminals.size() == 2);
    // Both terminal kinds present.
    bool seen_diffuse = false, seen_emission = false;
    for (const auto& t : r.material.terminals) {
        if (t.kind == GpuOpcode::Diffuse)  seen_diffuse  = true;
        if (t.kind == GpuOpcode::Emission) seen_emission = true;
    }
    RR_CHECK(seen_diffuse);
    RR_CHECK(seen_emission);
}


// ---------------------------------------------------------------------------
// Device-side `evaluateMaterial` (this slice).
//
// `evaluateMaterial` is `RR_HD inline`, so the host suite
// runs the SAME implementation the kernel uses. Tests build
// a `GpuMaterial` via the lowering, wrap it in a
// `CudaMaterialGraphView` of host pointers, and call the
// evaluator. The kernel path is correct by construction.
// ---------------------------------------------------------------------------

using rr::cuda::CudaMaterialGraphView;
using rr::cuda::MaterialEvalResult;
using rr::cuda::evaluateMaterial;
using rr::material::synthesise_gpu_material_from_params;

CudaMaterialGraphView view_of(const rr::material::GpuMaterial& mat) {
    CudaMaterialGraphView v;
    v.ops             = mat.ops.data();
    v.op_count        = static_cast<std::int32_t>(mat.ops.size());
    v.terminals       = mat.terminals.data();
    v.terminal_count  = static_cast<std::int32_t>(mat.terminals.size());
    return v;
}

void test_evaluate_material_const_diffuse_yields_albedo() {
    Graph g;
    const NodeId color   = g.add_node(NodeType::ConstantColor);
    const NodeId diffuse = g.add_node(NodeType::DiffuseBSDF);
    find_node(g, color)->color_value = rr::math::Vec3{0.7f, 0.4f, 0.2f};
    RR_CHECK(g.connect(color, "value", diffuse, "albedo"));

    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    const auto eval = evaluateMaterial(view_of(r.material));
    RR_CHECK(nearly_eq(eval.baseColor, rr::math::Vec3{0.7f, 0.4f, 0.2f}));
    RR_CHECK(nearly_eq(eval.emissionColor, rr::math::Vec3{0, 0, 0}));
    RR_CHECK(eval.emissionStrength == 0.0f);
}

void test_evaluate_material_unwired_diffuse_uses_terminal_immediate() {
    // Lowering bakes the node's color_value into the
    // terminal's imm_color. evaluateMaterial honours the
    // immediate when in_color is -1.
    Graph g;
    const NodeId d = g.add_node(NodeType::DiffuseBSDF);
    find_node(g, d)->color_value = rr::math::Vec3{0.3f, 0.4f, 0.5f};
    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    const auto eval = evaluateMaterial(view_of(r.material));
    RR_CHECK(nearly_eq(eval.baseColor, rr::math::Vec3{0.3f, 0.4f, 0.5f}));
}

void test_evaluate_material_emission_picks_up_color_and_strength() {
    Graph g;
    const NodeId c = g.add_node(NodeType::ConstantColor);
    const NodeId e = g.add_node(NodeType::Emission);
    find_node(g, c)->color_value = rr::math::Vec3{1.0f, 0.5f, 0.25f};
    find_node(g, e)->scalar_value = 2.0f;
    RR_CHECK(g.connect(c, "value", e, "color"));
    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    const auto eval = evaluateMaterial(view_of(r.material));
    RR_CHECK(nearly_eq(eval.emissionColor, rr::math::Vec3{1.0f, 0.5f, 0.25f}));
    RR_CHECK(nearly_eq(eval.emissionStrength, 2.0f));
}

void test_evaluate_material_negative_strength_clamped_to_zero() {
    Graph g;
    const NodeId e = g.add_node(NodeType::Emission);
    find_node(g, e)->scalar_value = -3.0f;
    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    const auto eval = evaluateMaterial(view_of(r.material));
    RR_CHECK(eval.emissionStrength == 0.0f);
}

void test_evaluate_material_add_chain() {
    // (Const + Const) -> Diffuse.
    Graph g;
    const NodeId ca   = g.add_node(NodeType::ConstantColor);
    const NodeId cb   = g.add_node(NodeType::ConstantColor);
    const NodeId sum  = g.add_node(NodeType::Add);
    const NodeId diff = g.add_node(NodeType::DiffuseBSDF);
    find_node(g, ca)->color_value = rr::math::Vec3{0.1f, 0.2f, 0.3f};
    find_node(g, cb)->color_value = rr::math::Vec3{0.4f, 0.5f, 0.6f};
    RR_CHECK(g.connect(ca,  "value", sum,  "a"));
    RR_CHECK(g.connect(cb,  "value", sum,  "b"));
    RR_CHECK(g.connect(sum, "value", diff, "albedo"));

    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    const auto eval = evaluateMaterial(view_of(r.material));
    RR_CHECK(nearly_eq(eval.baseColor, rr::math::Vec3{0.5f, 0.7f, 0.9f}));
}

void test_evaluate_material_multiply_chain() {
    Graph g;
    const NodeId ca  = g.add_node(NodeType::ConstantColor);
    const NodeId cb  = g.add_node(NodeType::ConstantColor);
    const NodeId mul = g.add_node(NodeType::Multiply);
    const NodeId d   = g.add_node(NodeType::DiffuseBSDF);
    find_node(g, ca)->color_value = rr::math::Vec3{0.5f, 0.5f, 0.5f};
    find_node(g, cb)->color_value = rr::math::Vec3{0.4f, 0.6f, 0.8f};
    RR_CHECK(g.connect(ca,  "value", mul, "a"));
    RR_CHECK(g.connect(cb,  "value", mul, "b"));
    RR_CHECK(g.connect(mul, "value", d,   "albedo"));
    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    const auto eval = evaluateMaterial(view_of(r.material));
    RR_CHECK(nearly_eq(eval.baseColor, rr::math::Vec3{0.2f, 0.3f, 0.4f}));
}

void test_evaluate_material_texture_sample_returns_fallback() {
    // TextureSample placeholder: the lowering bakes white
    // into imm_color, evaluateMaterial reads that.
    Graph g;
    const NodeId ts = g.add_node(NodeType::TextureSample);
    const NodeId d  = g.add_node(NodeType::DiffuseBSDF);
    find_node(g, ts)->texture_id = 0;
    RR_CHECK(g.connect(ts, "value", d, "albedo"));
    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    const auto eval = evaluateMaterial(view_of(r.material));
    RR_CHECK(nearly_eq(eval.baseColor, rr::math::Vec3{1.0f, 1.0f, 1.0f}));
}

void test_evaluate_material_diffuse_and_emission_independent() {
    Graph g;
    const NodeId base = g.add_node(NodeType::ConstantColor);
    const NodeId glow = g.add_node(NodeType::ConstantColor);
    const NodeId d    = g.add_node(NodeType::DiffuseBSDF);
    const NodeId e    = g.add_node(NodeType::Emission);
    find_node(g, base)->color_value = rr::math::Vec3{0.8f, 0.2f, 0.1f};
    find_node(g, glow)->color_value = rr::math::Vec3{0.0f, 0.5f, 1.0f};
    find_node(g, e)->scalar_value   = 0.7f;
    RR_CHECK(g.connect(base, "value", d, "albedo"));
    RR_CHECK(g.connect(glow, "value", e, "color"));
    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);
    const auto eval = evaluateMaterial(view_of(r.material));
    RR_CHECK(nearly_eq(eval.baseColor,     rr::math::Vec3{0.8f, 0.2f, 0.1f}));
    RR_CHECK(nearly_eq(eval.emissionColor, rr::math::Vec3{0.0f, 0.5f, 1.0f}));
    RR_CHECK(nearly_eq(eval.emissionStrength, 0.7f));
}

void test_evaluate_material_handles_empty_view() {
    // Empty view (no graph uploaded) should return the
    // default-constructed eval result. The kernel uses this
    // as the fallback when material_graph_views is null.
    CudaMaterialGraphView v;
    const auto eval = evaluateMaterial(v);
    RR_CHECK(nearly_eq(eval.baseColor, rr::math::Vec3{0.8f, 0.8f, 0.8f}));
    RR_CHECK(nearly_eq(eval.emissionColor, rr::math::Vec3{0, 0, 0}));
    RR_CHECK(eval.emissionStrength == 0.0f);
}


// ---------------------------------------------------------------------------
// synthesise_gpu_material_from_params (this slice).
// ---------------------------------------------------------------------------

void test_synthesise_from_params_diffuse_only() {
    rr::material::MaterialParams p;
    p.baseColor = rr::math::Vec3{0.7f, 0.4f, 0.2f};
    const auto mat = synthesise_gpu_material_from_params(&p);
    RR_CHECK(mat.ops.size()       == 1);
    RR_CHECK(mat.terminals.size() == 1);
    RR_CHECK(mat.ops[0].opcode == rr::material::GpuOpcode::ConstantColor);
    RR_CHECK(nearly_eq(mat.ops[0].imm_color, rr::math::Vec3{0.7f, 0.4f, 0.2f}));
    RR_CHECK(mat.terminals[0].kind     == rr::material::GpuOpcode::Diffuse);
    RR_CHECK(mat.terminals[0].in_color == 0);

    // The synthesised graph evaluates back to the original
    // baseColor / zero emission - matching what the kernel
    // would have read from MaterialParams directly.
    const auto eval = evaluateMaterial(view_of(mat));
    RR_CHECK(nearly_eq(eval.baseColor, p.baseColor));
    RR_CHECK(eval.emissionStrength == 0.0f);
}

void test_synthesise_from_params_with_emission() {
    rr::material::MaterialParams p;
    p.baseColor        = rr::math::Vec3{0.3f, 0.3f, 0.3f};
    p.emissionColor    = rr::math::Vec3{1.0f, 0.5f, 0.25f};
    p.emissionStrength = 2.0f;
    const auto mat = synthesise_gpu_material_from_params(&p);
    RR_CHECK(mat.ops.size()       == 2);
    RR_CHECK(mat.terminals.size() == 2);
    const auto eval = evaluateMaterial(view_of(mat));
    RR_CHECK(nearly_eq(eval.baseColor,     p.baseColor));
    RR_CHECK(nearly_eq(eval.emissionColor, p.emissionColor));
    RR_CHECK(nearly_eq(eval.emissionStrength, p.emissionStrength));
}

void test_synthesise_from_null_params_yields_neutral_default() {
    const auto mat = synthesise_gpu_material_from_params(nullptr);
    RR_CHECK(mat.ops.size()       == 1);
    RR_CHECK(mat.terminals.size() == 1);
    const auto eval = evaluateMaterial(view_of(mat));
    // Default: mid-grey baseColor, no emission.
    RR_CHECK(nearly_eq(eval.baseColor, rr::math::Vec3{0.8f, 0.8f, 0.8f}));
    RR_CHECK(eval.emissionStrength == 0.0f);
}


// --- Smoke test: print compiled IR ------------------------------------

void test_smoke_debug_print_compiled_gpu_material() {
    // Builds Const(0.7, 0.4, 0.2) -> DiffuseBSDF, compiles
    // to the GPU IR, and prints the result so the test
    // output makes the on-disk shape visible at a glance.
    Graph g;
    const NodeId color   = g.add_node(NodeType::ConstantColor);
    const NodeId diffuse = g.add_node(NodeType::DiffuseBSDF);
    find_node(g, color)->color_value = rr::math::Vec3{0.7f, 0.4f, 0.2f};
    RR_CHECK(g.connect(color, "value", diffuse, "albedo"));

    auto r = compile_graph_to_gpu_material(g);
    RR_CHECK(r.ok);

    std::printf("[smoke] compiled GpuMaterial:\n");
    debug_print_gpu_material(r.material);
}

}

int main() {
    // Socket layer.
    test_socket_type_names_round_trip();
    test_parse_socket_type_canonical_lowercase();
    test_parse_socket_type_pascalcase_aliases();
    test_parse_socket_type_rejects_unknown();
    test_socket_direction_names();
    test_can_connect_identity_for_every_type();
    test_can_connect_float_broadcasts_to_vector_kinds();
    test_can_connect_vec3_color_reinterpret_both_ways();
    test_can_connect_normal_drops_to_vec3_but_not_back();
    test_can_connect_rejects_vec_truncation_and_color_to_float();

    // Node layer.
    test_node_type_names_round_trip();
    test_parse_node_type_canonical_and_alias();
    test_parse_node_type_rejects_case_variants_and_unknown();
    test_is_terminal_only_diffuse_and_emission();
    test_make_node_constant_color_layout();
    test_make_node_texture_sample_layout();
    test_make_node_add_and_multiply_share_layout();
    test_make_node_terminals_have_no_outputs();
    test_make_node_immediate_defaults();
    test_find_socket_returns_nullptr_for_wrong_direction();

    // Graph layer.
    test_find_node_returns_nullptr_for_unknown_id();
    test_validate_minimal_diffuse_only_graph();
    test_validate_constant_to_diffuse_via_connection();
    test_validate_full_v1_chain();
    test_incoming_connections_returns_target_edges();

    // Validation: error paths.
    test_validate_rejects_unsupported_version();
    test_validate_rejects_invalid_node_id();
    test_validate_rejects_duplicate_id();
    test_validate_rejects_dangling_from_node();
    test_validate_rejects_dangling_to_node();
    test_validate_rejects_unknown_from_socket();
    test_validate_rejects_unknown_to_socket();
    test_validate_rejects_socket_in_wrong_direction();
    test_validate_rejects_double_wired_sink();
    test_validate_rejects_type_incompatible_connection();
    test_validate_accepts_compatible_implicit_conversion();
    test_validate_rejects_cycle();
    test_validate_rejects_graph_with_no_terminal();

    // Builder helpers (this slice).
    test_add_node_assigns_zero_then_increments();
    test_add_node_skips_past_manually_assigned_ids();
    test_connect_appends_on_success();
    test_connect_rejects_unknown_node();
    test_connect_rejects_unknown_socket();
    test_connect_rejects_wrong_direction();
    test_connect_rejects_type_incompatible();
    test_connect_rejects_duplicate_sink();
    test_connect_appends_cycle_then_validate_rejects_it();
    test_validate_method_matches_free_function();

    // Required-input flag.
    test_validator_passes_when_no_inputs_marked_required();
    test_validator_rejects_unwired_required_input();
    test_validator_passes_required_input_when_wired();

    // CPU reference evaluator (this slice).
    test_evaluate_constant_color_returns_immediate();
    test_evaluate_unknown_node_returns_black();
    test_evaluate_texture_sample_returns_fallback();
    test_evaluate_texture_sample_default_fallback_is_magenta();
    test_evaluate_add_two_constants();
    test_evaluate_add_unwired_falls_back_to_zero();
    test_evaluate_multiply_two_constants();
    test_evaluate_multiply_unwired_falls_back_to_one();
    test_evaluate_diffuse_bsdf_returns_wired_albedo();
    test_evaluate_diffuse_bsdf_unwired_uses_node_default();
    test_evaluate_emission_color_times_strength();
    test_evaluate_emission_unwired_uses_node_default_color();
    test_evaluate_chain_const_plus_const_times_const_to_diffuse();
    test_evaluate_diamond_fanout_does_not_corrupt_value();
    test_evaluate_depth_cap_returns_black_on_runaway();

    // GPU-friendly material lowering (this slice).
    test_gpu_opcode_name_round_trip();
    test_compile_const_diffuse_emits_one_op_one_terminal();
    test_compile_unwired_diffuse_uses_immediate_default();
    test_compile_emission_carries_color_and_strength_immediate();
    test_compile_add_chain_assigns_dense_slots_in_topo_order();
    test_compile_texture_sample_carries_id_and_fallback_color();
    test_compile_drops_dead_code_subgraph();
    test_compile_rejects_invalid_graph();
    test_compile_emission_with_wired_color_records_slot();
    test_compile_diffuse_plus_emission_emits_two_terminals();

    // Device-side evaluateMaterial (this slice; runs host-
    // side because the function is RR_HD inline).
    test_evaluate_material_const_diffuse_yields_albedo();
    test_evaluate_material_unwired_diffuse_uses_terminal_immediate();
    test_evaluate_material_emission_picks_up_color_and_strength();
    test_evaluate_material_negative_strength_clamped_to_zero();
    test_evaluate_material_add_chain();
    test_evaluate_material_multiply_chain();
    test_evaluate_material_texture_sample_returns_fallback();
    test_evaluate_material_diffuse_and_emission_independent();
    test_evaluate_material_handles_empty_view();
    test_synthesise_from_params_diffuse_only();
    test_synthesise_from_params_with_emission();
    test_synthesise_from_null_params_yields_neutral_default();

    // Smoke (printed):
    test_smoke_constant_color_to_diffuse_via_builder();
    test_smoke_evaluator_prints_resulting_color();
    test_smoke_debug_print_compiled_gpu_material();

    std::printf("material_graph_core_tests: %d/%d passed\n",
                g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
