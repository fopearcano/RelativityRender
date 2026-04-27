// Hand-rolled assertion runner. The real test framework comes with
// the M2 deferred items.

#include "geometry/Mesh.h"
#include "geometry/Triangle.h"
#include "math/Transform.h"
#include "math/Vec2.h"
#include "math/Vec3.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

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

using rr::geometry::make_triangle;
using rr::geometry::Mesh;
using rr::geometry::Triangle;
using rr::geometry::Vertex;
using rr::math::Transform;
using rr::math::Vec2;
using rr::math::Vec3;

void test_triangle_aggregate_init() {
    Triangle t{1, 2, 3};
    RR_CHECK(t.v0 == 1u);
    RR_CHECK(t.v1 == 2u);
    RR_CHECK(t.v2 == 3u);

    const Triangle tf = make_triangle(7, 8, 9);
    RR_CHECK(tf.v0 == 7u);
    RR_CHECK(tf.v1 == 8u);
    RR_CHECK(tf.v2 == 9u);

    Triangle d;
    RR_CHECK(d.v0 == 0u);
    RR_CHECK(d.v1 == 0u);
    RR_CHECK(d.v2 == 0u);
}

void test_vertex_defaults() {
    Vertex v;
    RR_CHECK(v.position == Vec3(0, 0, 0));
    RR_CHECK(v.normal   == Vec3(0, 0, 0));
    RR_CHECK(v.uv       == Vec2(0, 0));

    Vertex w{Vec3{1, 2, 3}, Vec3{0, 1, 0}, Vec2{0.5f, 0.25f}};
    RR_CHECK(w.position == Vec3(1, 2, 3));
    RR_CHECK(w.normal   == Vec3(0, 1, 0));
    RR_CHECK(w.uv       == Vec2(0.5f, 0.25f));
}

void test_default_mesh_state() {
    Mesh m;
    RR_CHECK(m.vertex_count()   == 0u);
    RR_CHECK(m.triangle_count() == 0u);
    RR_CHECK(m.empty());
    RR_CHECK(m.material_id == -1);
    RR_CHECK(m.transform.position == Vec3(0, 0, 0));
    RR_CHECK(m.transform.scale    == Vec3(1, 1, 1));
}

void test_populate_quad_two_triangles() {
    // Build a unit quad in the XY plane as two triangles, indexed
    // counter-clockwise from the front.
    Mesh m;
    m.reserve(4, 2);
    RR_CHECK(m.vertex_count()   == 0u);   // reserve does not change size
    RR_CHECK(m.triangle_count() == 0u);

    m.vertices.push_back({Vec3{0, 0, 0}, Vec3{0, 0, 1}, Vec2{0, 0}});
    m.vertices.push_back({Vec3{1, 0, 0}, Vec3{0, 0, 1}, Vec2{1, 0}});
    m.vertices.push_back({Vec3{1, 1, 0}, Vec3{0, 0, 1}, Vec2{1, 1}});
    m.vertices.push_back({Vec3{0, 1, 0}, Vec3{0, 0, 1}, Vec2{0, 1}});

    m.triangles.push_back({0, 1, 2});
    m.triangles.push_back({0, 2, 3});

    m.material_id          = 7;
    m.transform.position   = Vec3{0, 0, -3};
    m.transform.scale      = Vec3{2, 2, 2};

    RR_CHECK(m.vertex_count()   == 4u);
    RR_CHECK(m.triangle_count() == 2u);
    RR_CHECK(!m.empty());
    RR_CHECK(m.material_id == 7);
    RR_CHECK(m.transform.position == Vec3(0, 0, -3));
    RR_CHECK(m.transform.scale    == Vec3(2, 2, 2));

    // Indices reference valid vertices.
    for (const auto& tri : m.triangles) {
        RR_CHECK(tri.v0 < m.vertex_count());
        RR_CHECK(tri.v1 < m.vertex_count());
        RR_CHECK(tri.v2 < m.vertex_count());
    }
}

void test_empty_means_either_list_empty() {
    Mesh m;
    RR_CHECK(m.empty());

    // Vertices but no triangles -> still empty (nothing renderable).
    m.vertices.push_back({Vec3{0, 0, 0}, Vec3{0, 0, 1}, Vec2{0, 0}});
    RR_CHECK(m.empty());

    m.triangles.push_back({0, 0, 0});  // degenerate but indexes valid
    RR_CHECK(!m.empty());

    // Triangles but no vertices -> empty (indices reference nothing).
    Mesh n;
    n.triangles.push_back({0, 1, 2});
    RR_CHECK(n.empty());
}

void test_clear_resets_to_default() {
    Mesh m;
    m.vertices.push_back({Vec3{1, 0, 0}, Vec3{0, 0, 1}, Vec2{0, 0}});
    m.triangles.push_back({0, 0, 0});
    m.material_id        = 42;
    m.transform.position = Vec3{1, 2, 3};

    m.clear();

    RR_CHECK(m.vertex_count()   == 0u);
    RR_CHECK(m.triangle_count() == 0u);
    RR_CHECK(m.empty());
    RR_CHECK(m.material_id == -1);
    RR_CHECK(m.transform.position == Vec3(0, 0, 0));
    RR_CHECK(m.transform.scale    == Vec3(1, 1, 1));
}

void test_transform_back_compat_alias() {
    // The scene-namespace alias still resolves to the same type.
    Transform t = Transform::identity();
    RR_CHECK(t.position == Vec3(0, 0, 0));
    RR_CHECK(t.scale    == Vec3(1, 1, 1));
}

}

int main() {
    test_triangle_aggregate_init();
    test_vertex_defaults();
    test_default_mesh_state();
    test_populate_quad_two_triangles();
    test_empty_means_either_list_empty();
    test_clear_resets_to_default();
    test_transform_back_compat_alias();

    std::printf("mesh_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
