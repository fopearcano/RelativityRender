// Hand-rolled assertion runner. The real test framework comes
// with the M2 deferred items.
//
// Material graph runtime tests. Covers the v1 compile-to-
// MaterialParams path defined in `src/material/MaterialGraph.{h,cpp}`:
// validation (duplicate ids, dangling refs, cycles, missing
// terminals, duplicate terminals), per-node bake correctness
// for the six v1 node types (ConstantColor, TextureSample,
// Add, Multiply, Diffuse, Emission), and the texture-sampler
// callback contract.

#include "material/MaterialGraph.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_total  = 0;
int g_failed = 0;

float abs_f(float a) { return a < 0.0f ? -a : a; }

bool nearly_equal(float a, float b, float eps = 1.0e-5f) {
    const float scale  = 1.0f > abs_f(a) ? 1.0f : abs_f(a);
    const float scale2 = scale > abs_f(b) ? scale : abs_f(b);
    return abs_f(a - b) <= eps * scale2;
}

bool nearly_equal(rr::math::Vec3 a, rr::math::Vec3 b, float eps = 1.0e-5f) {
    return nearly_equal(a.x, b.x, eps)
        && nearly_equal(a.y, b.y, eps)
        && nearly_equal(a.z, b.z, eps);
}

#define RR_CHECK(...)                                                         \
    do {                                                                      \
        ++g_total;                                                            \
        if (!(__VA_ARGS__)) {                                                 \
            ++g_failed;                                                       \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                        \
                         #__VA_ARGS__, __FILE__, __LINE__);                   \
        }                                                                     \
    } while (0)

using rr::material::Graph;
using rr::material::GraphNode;
using rr::material::NodeType;
using rr::material::compile_graph_to_material;
using rr::math::Vec2;
using rr::math::Vec3;

GraphNode make_constant(int id, Vec3 value) {
    GraphNode n;
    n.id          = id;
    n.type        = NodeType::ConstantColor;
    n.color_value = value;
    return n;
}

GraphNode make_texture(int id, int texture_id, Vec2 uv = {0.5f, 0.5f}) {
    GraphNode n;
    n.id         = id;
    n.type       = NodeType::TextureSample;
    n.texture_id = texture_id;
    n.default_uv = uv;
    return n;
}

GraphNode make_add(int id, int a, int b) {
    GraphNode n;
    n.id      = id;
    n.type    = NodeType::Add;
    n.input_a = a;
    n.input_b = b;
    return n;
}

GraphNode make_multiply(int id, int a, int b) {
    GraphNode n;
    n.id      = id;
    n.type    = NodeType::Multiply;
    n.input_a = a;
    n.input_b = b;
    return n;
}

GraphNode make_diffuse(int id, int albedo_src) {
    GraphNode n;
    n.id           = id;
    n.type         = NodeType::Diffuse;
    n.input_albedo = albedo_src;
    return n;
}

GraphNode make_emission(int id, int color_src, float strength = 1.0f) {
    GraphNode n;
    n.id              = id;
    n.type            = NodeType::Emission;
    n.input_color     = color_src;
    n.strength_value  = strength;
    return n;
}

// --- Naming + parsing ---------------------------------------------------

void test_node_type_names() {
    RR_CHECK(std::strcmp(rr::material::node_type_name(
        NodeType::ConstantColor), "ConstantColor") == 0);
    RR_CHECK(std::strcmp(rr::material::node_type_name(
        NodeType::TextureSample), "TextureSample") == 0);
    RR_CHECK(std::strcmp(rr::material::node_type_name(
        NodeType::Add), "Add") == 0);
    RR_CHECK(std::strcmp(rr::material::node_type_name(
        NodeType::Multiply), "Multiply") == 0);
    RR_CHECK(std::strcmp(rr::material::node_type_name(
        NodeType::Diffuse), "Diffuse") == 0);
    RR_CHECK(std::strcmp(rr::material::node_type_name(
        NodeType::Emission), "Emission") == 0);
}

void test_parse_node_type_canonical_names() {
    NodeType t;
    RR_CHECK(rr::material::parse_node_type("ConstantColor", t)
             && t == NodeType::ConstantColor);
    RR_CHECK(rr::material::parse_node_type("TextureSample", t)
             && t == NodeType::TextureSample);
    RR_CHECK(rr::material::parse_node_type("Add", t)
             && t == NodeType::Add);
    RR_CHECK(rr::material::parse_node_type("Multiply", t)
             && t == NodeType::Multiply);
    RR_CHECK(rr::material::parse_node_type("Diffuse", t)
             && t == NodeType::Diffuse);
    RR_CHECK(rr::material::parse_node_type("Emission", t)
             && t == NodeType::Emission);
}

void test_parse_node_type_accepts_diffuse_bsdf_alias() {
    // The doc spec calls the node `Diffuse`; the implementation
    // also accepts `DiffuseBSDF` so a future scene file written
    // with either form parses without translation.
    NodeType t;
    RR_CHECK(rr::material::parse_node_type("DiffuseBSDF", t)
             && t == NodeType::Diffuse);
}

void test_parse_node_type_rejects_unknown_and_case_variants() {
    NodeType t;
    RR_CHECK(!rr::material::parse_node_type("",                  t));
    RR_CHECK(!rr::material::parse_node_type("Frobnicate",        t));
    // Case-sensitive: lowercase variants are rejected.
    RR_CHECK(!rr::material::parse_node_type("diffuse",           t));
    RR_CHECK(!rr::material::parse_node_type("CONSTANTCOLOR",     t));
}

void test_is_terminal_only_for_diffuse_and_emission() {
    RR_CHECK(rr::material::is_terminal(NodeType::Diffuse));
    RR_CHECK(rr::material::is_terminal(NodeType::Emission));
    RR_CHECK(!rr::material::is_terminal(NodeType::ConstantColor));
    RR_CHECK(!rr::material::is_terminal(NodeType::TextureSample));
    RR_CHECK(!rr::material::is_terminal(NodeType::Add));
    RR_CHECK(!rr::material::is_terminal(NodeType::Multiply));
}

// --- Smallest valid graph ------------------------------------------------

void test_constant_color_to_diffuse() {
    Graph g;
    g.nodes.push_back(make_constant(0, Vec3{0.7f, 0.4f, 0.2f}));
    g.nodes.push_back(make_diffuse (1, /*albedo=*/0));

    auto r = compile_graph_to_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(nearly_equal(r.material.baseColor, Vec3{0.7f, 0.4f, 0.2f}));
    // No emission terminal -> emission stays at MaterialParams default.
    RR_CHECK(nearly_equal(r.material.emissionColor, Vec3{0, 0, 0}));
    RR_CHECK(r.material.emissionStrength == 0.0f);
}

void test_unwired_diffuse_uses_node_default_color() {
    // Diffuse with no wired albedo input falls back to its own
    // `color_value` (which is the node's per-type default,
    // mid-grey from the catalogue).
    Graph g;
    GraphNode d;
    d.id          = 0;
    d.type        = NodeType::Diffuse;
    d.color_value = Vec3{0.3f, 0.4f, 0.5f};
    g.nodes.push_back(d);

    auto r = compile_graph_to_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(nearly_equal(r.material.baseColor, Vec3{0.3f, 0.4f, 0.5f}));
}

// --- Math nodes ----------------------------------------------------------

void test_add_two_constants() {
    Graph g;
    g.nodes.push_back(make_constant(0, Vec3{0.2f, 0.3f, 0.4f}));
    g.nodes.push_back(make_constant(1, Vec3{0.1f, 0.2f, 0.3f}));
    g.nodes.push_back(make_add     (2, 0, 1));
    g.nodes.push_back(make_diffuse (3, /*albedo=*/2));

    auto r = compile_graph_to_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(nearly_equal(r.material.baseColor, Vec3{0.3f, 0.5f, 0.7f}));
}

void test_multiply_two_constants() {
    Graph g;
    g.nodes.push_back(make_constant (0, Vec3{0.5f, 0.5f, 0.5f}));
    g.nodes.push_back(make_constant (1, Vec3{0.4f, 0.6f, 0.8f}));
    g.nodes.push_back(make_multiply (2, 0, 1));
    g.nodes.push_back(make_diffuse  (3, /*albedo=*/2));

    auto r = compile_graph_to_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(nearly_equal(r.material.baseColor, Vec3{0.2f, 0.3f, 0.4f}));
}

void test_unwired_add_falls_back_to_zero_default() {
    // Add with both inputs unwired: per-component zero + zero =
    // zero. The bake should reach the diffuse terminal with a
    // black baseColor.
    Graph g;
    GraphNode add;
    add.id   = 0;
    add.type = NodeType::Add;
    g.nodes.push_back(add);
    g.nodes.push_back(make_diffuse(1, 0));

    auto r = compile_graph_to_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(nearly_equal(r.material.baseColor, Vec3{0, 0, 0}));
}

void test_unwired_multiply_falls_back_to_one_default() {
    // Multiply with both inputs unwired: identity. Diffuse should
    // therefore see white.
    Graph g;
    GraphNode mul;
    mul.id   = 0;
    mul.type = NodeType::Multiply;
    g.nodes.push_back(mul);
    g.nodes.push_back(make_diffuse(1, 0));

    auto r = compile_graph_to_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(nearly_equal(r.material.baseColor, Vec3{1, 1, 1}));
}

// --- Emission ------------------------------------------------------------

void test_emission_terminal_basic() {
    Graph g;
    g.nodes.push_back(make_constant(0, Vec3{1.0f, 0.5f, 0.25f}));
    g.nodes.push_back(make_emission(1, /*color_src=*/0, /*strength=*/2.5f));

    auto r = compile_graph_to_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(nearly_equal(r.material.emissionColor, Vec3{1.0f, 0.5f, 0.25f}));
    RR_CHECK(nearly_equal(r.material.emissionStrength, 2.5f));
    // No diffuse terminal -> baseColor stays at MaterialParams default.
    RR_CHECK(nearly_equal(r.material.baseColor, Vec3{0.8f, 0.8f, 0.8f}));
}

void test_emission_uses_node_color_default_when_unwired() {
    Graph g;
    GraphNode em;
    em.id              = 0;
    em.type            = NodeType::Emission;
    em.color_value     = Vec3{0.2f, 0.3f, 0.4f};
    em.strength_value  = 5.0f;
    g.nodes.push_back(em);

    auto r = compile_graph_to_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(nearly_equal(r.material.emissionColor, Vec3{0.2f, 0.3f, 0.4f}));
    RR_CHECK(nearly_equal(r.material.emissionStrength, 5.0f));
}

void test_emission_strength_clamped_to_non_negative() {
    Graph g;
    GraphNode em = make_emission(0, /*color_src=*/-1, /*strength=*/-3.0f);
    g.nodes.push_back(em);

    auto r = compile_graph_to_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(r.material.emissionStrength == 0.0f);
}

// --- Both terminals coexist ---------------------------------------------

void test_diffuse_and_emission_coexist() {
    Graph g;
    g.nodes.push_back(make_constant(0, Vec3{0.8f, 0.2f, 0.1f}));
    g.nodes.push_back(make_constant(1, Vec3{0.0f, 0.5f, 1.0f}));
    g.nodes.push_back(make_diffuse (2, /*albedo=*/0));
    g.nodes.push_back(make_emission(3, /*color_src=*/1, /*strength=*/0.7f));

    auto r = compile_graph_to_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(nearly_equal(r.material.baseColor,     Vec3{0.8f, 0.2f, 0.1f}));
    RR_CHECK(nearly_equal(r.material.emissionColor, Vec3{0.0f, 0.5f, 1.0f}));
    RR_CHECK(nearly_equal(r.material.emissionStrength, 0.7f));
}

// --- TextureSample ------------------------------------------------------

void test_texture_sample_invokes_callback_with_id_and_uv() {
    Graph g;
    g.nodes.push_back(make_texture(0, /*texture_id=*/7,
                                   /*uv=*/Vec2{0.25f, 0.75f}));
    g.nodes.push_back(make_diffuse(1, /*albedo=*/0));

    int  seen_id = -1;
    Vec2 seen_uv{0, 0};
    auto sampler = [&](int id, Vec2 uv) -> Vec3 {
        seen_id = id;
        seen_uv = uv;
        return Vec3{0.1f, 0.2f, 0.3f};
    };

    auto r = compile_graph_to_material(g, sampler);
    RR_CHECK(r.ok);
    RR_CHECK(seen_id == 7);
    RR_CHECK(nearly_equal(seen_uv.x, 0.25f));
    RR_CHECK(nearly_equal(seen_uv.y, 0.75f));
    RR_CHECK(nearly_equal(r.material.baseColor, Vec3{0.1f, 0.2f, 0.3f}));
}

void test_texture_sample_falls_back_to_white_without_sampler() {
    // No sampler -> placeholder / debug colour. Spec says
    // TextureSample is a placeholder; v1's fallback is white
    // so a missing-texture material stays visible.
    Graph g;
    g.nodes.push_back(make_texture(0, /*texture_id=*/0));
    g.nodes.push_back(make_diffuse(1, /*albedo=*/0));

    auto r = compile_graph_to_material(g);  // no sampler
    RR_CHECK(r.ok);
    RR_CHECK(nearly_equal(r.material.baseColor, Vec3{1, 1, 1}));
}

void test_texture_sample_falls_back_when_id_negative() {
    // Negative texture id -> falls back to white even when a
    // sampler is provided (the sampler is not called).
    Graph g;
    g.nodes.push_back(make_texture(0, /*texture_id=*/-1));
    g.nodes.push_back(make_diffuse(1, /*albedo=*/0));

    int call_count = 0;
    auto sampler = [&](int, Vec2) -> Vec3 {
        ++call_count;
        return Vec3{0.5f, 0.5f, 0.5f};
    };
    auto r = compile_graph_to_material(g, sampler);
    RR_CHECK(r.ok);
    RR_CHECK(call_count == 0);
    RR_CHECK(nearly_equal(r.material.baseColor, Vec3{1, 1, 1}));
}

// --- Composition: textured + tinted -------------------------------------

void test_textured_diffuse_tinted_via_multiply() {
    // Realistic chain: TextureSample -> Multiply (other side =
    // ConstantColor tint) -> Diffuse. Verifies fan-in across
    // two source nodes and that the math node's output flows
    // into the terminal.
    Graph g;
    g.nodes.push_back(make_texture (0, /*texture_id=*/3));         // returns sampled colour
    g.nodes.push_back(make_constant(1, Vec3{0.5f, 0.5f, 1.0f}));  // tint
    g.nodes.push_back(make_multiply(2, 0, 1));
    g.nodes.push_back(make_diffuse (3, /*albedo=*/2));

    auto sampler = [&](int id, Vec2) -> Vec3 {
        return id == 3 ? Vec3{0.4f, 0.4f, 0.4f}
                       : Vec3{1.0f, 0.0f, 1.0f};  // sentinel: should not appear
    };
    auto r = compile_graph_to_material(g, sampler);
    RR_CHECK(r.ok);
    RR_CHECK(nearly_equal(r.material.baseColor,
                          Vec3{0.4f * 0.5f, 0.4f * 0.5f, 0.4f * 1.0f}));
}

// --- Validation: errors -------------------------------------------------

void test_empty_graph_rejected() {
    Graph g;  // no nodes
    auto r = compile_graph_to_material(g);
    RR_CHECK(!r.ok);
    RR_CHECK(r.message.find("empty") != std::string::npos);
}

void test_unsupported_version_rejected() {
    Graph g;
    g.version = 2;
    g.nodes.push_back(make_diffuse(0, -1));
    auto r = compile_graph_to_material(g);
    RR_CHECK(!r.ok);
    RR_CHECK(r.message.find("version") != std::string::npos);
}

void test_duplicate_id_rejected() {
    Graph g;
    g.nodes.push_back(make_constant(5, Vec3{0, 0, 0}));
    g.nodes.push_back(make_constant(5, Vec3{1, 1, 1}));   // duplicate id
    g.nodes.push_back(make_diffuse (6, /*albedo=*/5));
    auto r = compile_graph_to_material(g);
    RR_CHECK(!r.ok);
    RR_CHECK(r.message.find("duplicate") != std::string::npos);
}

void test_dangling_reference_rejected() {
    Graph g;
    g.nodes.push_back(make_diffuse(0, /*albedo=*/42));     // 42 does not exist
    auto r = compile_graph_to_material(g);
    RR_CHECK(!r.ok);
    RR_CHECK(r.message.find("unknown") != std::string::npos);
}

void test_cycle_rejected() {
    // Add(0) feeds Multiply(1).a; Multiply(1) feeds Add(0).a -> cycle.
    Graph g;
    g.nodes.push_back(make_add     (0, /*a=*/1, /*b=*/-1));
    g.nodes.push_back(make_multiply(1, /*a=*/0, /*b=*/-1));
    g.nodes.push_back(make_diffuse (2, /*albedo=*/0));
    auto r = compile_graph_to_material(g);
    RR_CHECK(!r.ok);
    RR_CHECK(r.message.find("cycle") != std::string::npos);
}

void test_no_terminal_rejected() {
    Graph g;
    g.nodes.push_back(make_constant(0, Vec3{1, 1, 1}));
    g.nodes.push_back(make_constant(1, Vec3{0.5f, 0.5f, 0.5f}));
    g.nodes.push_back(make_multiply(2, 0, 1));
    auto r = compile_graph_to_material(g);
    RR_CHECK(!r.ok);
    RR_CHECK(r.message.find("terminal") != std::string::npos);
}

void test_duplicate_terminal_rejected() {
    Graph g;
    g.nodes.push_back(make_constant(0, Vec3{1, 0, 0}));
    g.nodes.push_back(make_diffuse (1, 0));
    g.nodes.push_back(make_diffuse (2, 0));   // second Diffuse
    auto r = compile_graph_to_material(g);
    RR_CHECK(!r.ok);
    // Message names the offending terminal kind.
    RR_CHECK(r.message.find("Diffuse") != std::string::npos);
}

void test_dead_code_subgraph_does_not_break_compile() {
    // A dangling subgraph that doesn't reach any terminal must
    // be dropped by topo-sort; its evaluation MUST NOT affect
    // the bake. Here, a Multiply branch is unreachable; the
    // Diffuse terminal sees only the ConstantColor it directly
    // wires.
    Graph g;
    g.nodes.push_back(make_constant (0, Vec3{0.9f, 0.1f, 0.1f}));
    g.nodes.push_back(make_constant (1, Vec3{0.0f, 0.0f, 0.0f}));
    g.nodes.push_back(make_multiply (2, 0, 1));   // unreachable
    g.nodes.push_back(make_diffuse  (3, /*albedo=*/0));

    auto r = compile_graph_to_material(g);
    RR_CHECK(r.ok);
    RR_CHECK(nearly_equal(r.material.baseColor, Vec3{0.9f, 0.1f, 0.1f}));
}

}

int main() {
    test_node_type_names();
    test_parse_node_type_canonical_names();
    test_parse_node_type_accepts_diffuse_bsdf_alias();
    test_parse_node_type_rejects_unknown_and_case_variants();
    test_is_terminal_only_for_diffuse_and_emission();

    test_constant_color_to_diffuse();
    test_unwired_diffuse_uses_node_default_color();

    test_add_two_constants();
    test_multiply_two_constants();
    test_unwired_add_falls_back_to_zero_default();
    test_unwired_multiply_falls_back_to_one_default();

    test_emission_terminal_basic();
    test_emission_uses_node_color_default_when_unwired();
    test_emission_strength_clamped_to_non_negative();

    test_diffuse_and_emission_coexist();

    test_texture_sample_invokes_callback_with_id_and_uv();
    test_texture_sample_falls_back_to_white_without_sampler();
    test_texture_sample_falls_back_when_id_negative();
    test_textured_diffuse_tinted_via_multiply();

    test_empty_graph_rejected();
    test_unsupported_version_rejected();
    test_duplicate_id_rejected();
    test_dangling_reference_rejected();
    test_cycle_rejected();
    test_no_terminal_rejected();
    test_duplicate_terminal_rejected();
    test_dead_code_subgraph_does_not_break_compile();

    std::printf("material_graph_tests: %d/%d passed\n",
                g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
