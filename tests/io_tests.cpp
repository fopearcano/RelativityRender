// Hand-rolled assertion runner. The real test framework comes with
// the M2 deferred items.
//
// Loads `scenes/test_minimal.rrscene` and prints what was parsed,
// then asserts a handful of expected values. The fixture path is
// passed in via the `RR_TEST_FIXTURES_DIR` compile definition so
// the test runs from any working directory `ctest` chooses.

#include "io/SceneLoader.h"
#include "io/SceneWriter.h"
#include "lighting/Light.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#ifndef RR_TEST_FIXTURES_DIR
    #error "RR_TEST_FIXTURES_DIR must be defined by the build system"
#endif

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

void print_scene(const rr::scene::Scene& s) {
    std::printf("--- loaded scene ---\n");
    std::printf("  render_settings:\n");
    std::printf("    width  = %d\n", s.render_settings.width);
    std::printf("    height = %d\n", s.render_settings.height);

    std::printf("  camera:\n");
    std::printf("    position = (%.3f, %.3f, %.3f)\n",
                s.camera.position().x, s.camera.position().y, s.camera.position().z);
    std::printf("    forward  = (%.3f, %.3f, %.3f)\n",
                s.camera.forward().x, s.camera.forward().y, s.camera.forward().z);
    std::printf("    up       = (%.3f, %.3f, %.3f)\n",
                s.camera.up().x, s.camera.up().y, s.camera.up().z);
    std::printf("    fov      = %.2f deg\n",   s.camera.vertical_fov_degrees());
    std::printf("    aspect   = %.4f\n",       s.camera.aspect());

    std::printf("  relativity:\n");
    std::printf("    velocity = (%.3f, %.3f, %.3f)  |beta| = %.4f\n",
                s.observer.velocity.x, s.observer.velocity.y, s.observer.velocity.z,
                rr::math::length(s.observer.velocity));
    std::printf("    enable_aberration       = %s\n", s.relativity.enable_aberration ? "true" : "false");
    std::printf("    enable_doppler          = %s\n", s.relativity.enable_doppler    ? "true" : "false");
    std::printf("    enable_searchlight      = %s\n", s.relativity.enable_searchlight? "true" : "false");
    std::printf("    doppler_color_strength  = %.3f\n", s.relativity.doppler_color_strength);
    std::printf("    searchlight_strength    = %.3f\n", s.relativity.searchlight_strength);
    std::printf("---------------------\n");
}

void test_load_test_minimal_scene() {
    const std::filesystem::path fixture =
        std::filesystem::path(RR_TEST_FIXTURES_DIR) / "test_minimal.rrscene";

    auto result = rr::io::load_rrscene(fixture);
    if (!result.ok) {
        std::fprintf(stderr, "load_rrscene failed: %s\n", result.message.c_str());
    }
    RR_CHECK(result.ok);
    if (!result.ok) return;

    print_scene(result.scene);

    const auto& s = result.scene;
    RR_CHECK(s.render_settings.width  == 640);
    RR_CHECK(s.render_settings.height == 480);

    RR_CHECK(s.camera.position()                 == rr::math::Vec3(0.0f, 0.0f, 0.0f));
    RR_CHECK(nearly_equal(s.camera.forward(),       rr::math::Vec3(0.0f, 0.0f, -1.0f)));
    RR_CHECK(nearly_equal(s.camera.up(),            rr::math::Vec3(0.0f, 1.0f,  0.0f)));
    RR_CHECK(nearly_equal(s.camera.vertical_fov_degrees(), 50.0f));
    RR_CHECK(nearly_equal(s.camera.aspect(),        640.0f / 480.0f));

    // Relativity: velocity = beta * normalize(direction).
    RR_CHECK(nearly_equal(s.observer.velocity,
                          rr::math::Vec3(0.0f, 0.0f, -0.3f)));
    RR_CHECK(s.relativity.enable_aberration);
    RR_CHECK(s.relativity.enable_doppler);
    RR_CHECK(s.relativity.enable_searchlight);
    RR_CHECK(nearly_equal(s.relativity.doppler_color_strength, 1.0f));
    RR_CHECK(nearly_equal(s.relativity.searchlight_strength,   1.0f));
}

void test_missing_file_fails_predictably() {
    auto result = rr::io::load_rrscene(
        std::filesystem::path(RR_TEST_FIXTURES_DIR) / "does_not_exist.rrscene");
    RR_CHECK(!result.ok);
    RR_CHECK(!result.message.empty());
}

void test_load_test_geometry_scene() {
    const std::filesystem::path fixture =
        std::filesystem::path(RR_TEST_FIXTURES_DIR) / "test_geometry.rrscene";

    auto result = rr::io::load_rrscene(fixture);
    if (!result.ok) {
        std::fprintf(stderr, "load_rrscene failed: %s\n", result.message.c_str());
    }
    RR_CHECK(result.ok);
    if (!result.ok) return;

    const auto& s = result.scene;

    std::printf("--- loaded geometry scene ---\n");
    std::printf("  materials: %zu entries\n", s.materials.size());
    for (std::size_t i = 0; i < s.materials.size(); ++i) {
        const auto& m = s.materials[i];
        std::printf("    [%zu] id=%d name=%s baseColor=(%.2f, %.2f, %.2f) "
                    "emission=(%.2f, %.2f, %.2f) * %.2f rough=%.2f\n",
                    i, m.id, m.name.c_str(),
                    m.params.baseColor.x, m.params.baseColor.y, m.params.baseColor.z,
                    m.params.emissionColor.x, m.params.emissionColor.y, m.params.emissionColor.z,
                    m.params.emissionStrength,
                    m.params.roughness);
    }
    std::printf("  spheres: %zu entries\n", s.spheres.size());
    for (std::size_t i = 0; i < s.spheres.size(); ++i) {
        const auto& sp = s.spheres[i];
        std::printf("    [%zu] center=(%.2f, %.2f, %.2f) r=%.2f material_id=%d\n",
                    i,
                    sp.geometry.center.x, sp.geometry.center.y, sp.geometry.center.z,
                    sp.geometry.radius, sp.geometry.material_index);
    }
    std::printf("------------------------------\n");

    // Counts.
    RR_CHECK(s.materials.size() == 3u);
    RR_CHECK(s.spheres.size()   == 4u);

    // Sample material values.
    RR_CHECK(s.materials[0].id   == 0);
    RR_CHECK(s.materials[0].name == std::string("matte_red"));
    RR_CHECK(s.materials[0].params.baseColor == rr::math::Vec3(0.9f, 0.15f, 0.15f));
    RR_CHECK(nearly_equal(s.materials[0].params.roughness, 0.5f));

    RR_CHECK(s.materials[1].id   == 1);
    RR_CHECK(s.materials[2].id   == 3);  // sparse / non-contiguous id
    RR_CHECK(nearly_equal(s.materials[2].params.emissionStrength, 2.0f));
    RR_CHECK(s.materials[2].params.emissionColor == rr::math::Vec3(1.0f, 0.85f, 0.4f));

    // Sample sphere values - including the "no material_id" sphere
    // that should land at -1.
    RR_CHECK(s.spheres[0].geometry.center == rr::math::Vec3(0.0f, 0.0f, -3.0f));
    RR_CHECK(nearly_equal(s.spheres[0].geometry.radius, 1.0f));
    RR_CHECK(s.spheres[0].geometry.material_index == 0);

    RR_CHECK(s.spheres[1].geometry.material_index == 1);
    RR_CHECK(s.spheres[2].geometry.material_index == 3);   // sparse-id reference
    RR_CHECK(s.spheres[3].geometry.material_index == -1);  // no material_id field
}

void test_minimal_scene_still_has_no_geometry() {
    // The minimal fixture omits both sections entirely; the loader
    // should produce an empty materials / spheres list rather than
    // failing.
    auto result = rr::io::load_rrscene(
        std::filesystem::path(RR_TEST_FIXTURES_DIR) / "test_minimal.rrscene");
    RR_CHECK(result.ok);
    RR_CHECK(result.scene.materials.empty());
    RR_CHECK(result.scene.spheres.empty());
    RR_CHECK(result.scene.lights.empty());
    RR_CHECK(result.scene.meshes.empty());
}

void test_load_full_scene() {
    // The user-facing test scene exercises every v1 section.
    auto result = rr::io::load_rrscene(
        std::filesystem::path(RR_TEST_FIXTURES_DIR) / "test.rrscene");
    if (!result.ok) {
        std::fprintf(stderr, "load_rrscene failed: %s\n", result.message.c_str());
    }
    RR_CHECK(result.ok);
    if (!result.ok) return;

    const auto& s = result.scene;
    std::printf("--- loaded full scene ---\n");
    std::printf("  materials: %zu  spheres: %zu  lights: %zu  meshes: %zu\n",
                s.materials.size(), s.spheres.size(),
                s.lights.size(), s.meshes.size());
    for (std::size_t i = 0; i < s.lights.size(); ++i) {
        const auto& L = s.lights[i].data;
        const char* type = L.type == rr::lighting::LightType::Point ? "point"
                         : L.type == rr::lighting::LightType::Directional ? "directional"
                         : "other";
        std::printf("    light[%zu] %s color=(%.2f, %.2f, %.2f) intensity=%.2f\n",
                    i, type, L.color.x, L.color.y, L.color.z, L.intensity);
    }
    for (std::size_t i = 0; i < s.meshes.size(); ++i) {
        const auto& m = s.meshes[i];
        std::printf("    mesh[%zu] name=%s vertices=%zu triangles=%zu material=%d\n",
                    i, m.object.name.c_str(),
                    m.data.vertex_count(), m.data.triangle_count(),
                    m.data.material_id);
    }
    std::printf("-------------------------\n");

    RR_CHECK(s.materials.size() == 5u);
    RR_CHECK(s.spheres.size()   == 4u);
    RR_CHECK(s.lights.size()    == 2u);
    RR_CHECK(s.meshes.size()    == 1u);

    // Lights: a directional + a point, in that order.
    RR_CHECK(s.lights[0].data.type == rr::lighting::LightType::Directional);
    RR_CHECK(nearly_equal(s.lights[0].data.intensity, 0.9f));
    // Direction should be normalised (factory does this).
    RR_CHECK(nearly_equal(rr::math::length(s.lights[0].data.direction), 1.0f));
    RR_CHECK(s.lights[1].data.type == rr::lighting::LightType::Point);
    RR_CHECK(s.lights[1].data.position == rr::math::Vec3(1.5f, 2.5f, -1.5f));
    RR_CHECK(nearly_equal(s.lights[1].data.intensity, 8.0f));

    // Mesh: 4 vertices, 2 triangles, material id 3, identity transform.
    const auto& m = s.meshes[0];
    RR_CHECK(m.data.vertex_count()   == 4u);
    RR_CHECK(m.data.triangle_count() == 2u);
    RR_CHECK(m.data.material_id      == 3);
    RR_CHECK(m.data.transform.position == rr::math::Vec3(0, 0, 0));
    RR_CHECK(m.data.transform.scale    == rr::math::Vec3(1, 1, 1));
    // CCW front-facing triangle indices.
    RR_CHECK(m.data.triangles[0].v0 == 0u);
    RR_CHECK(m.data.triangles[0].v1 == 1u);
    RR_CHECK(m.data.triangles[0].v2 == 2u);
}

void test_writer_round_trip() {
    // Load test.rrscene, save it back to a temp file, reload, and
    // verify the round-trip preserves the v1 fields.
    const std::filesystem::path src =
        std::filesystem::path(RR_TEST_FIXTURES_DIR) / "test.rrscene";
    const std::filesystem::path dst =
        std::filesystem::temp_directory_path() / "rrscene_round_trip.rrscene";

    auto loaded = rr::io::load_rrscene(src);
    RR_CHECK(loaded.ok);
    if (!loaded.ok) return;

    auto write = rr::io::save_rrscene(loaded.scene, dst);
    if (!write.ok) {
        std::fprintf(stderr, "save_rrscene failed: %s\n", write.message.c_str());
    }
    RR_CHECK(write.ok);
    if (!write.ok) return;

    auto reloaded = rr::io::load_rrscene(dst);
    if (!reloaded.ok) {
        std::fprintf(stderr, "reload after write failed: %s\n",
                     reloaded.message.c_str());
    }
    RR_CHECK(reloaded.ok);
    if (!reloaded.ok) return;

    const auto& a = loaded.scene;
    const auto& b = reloaded.scene;

    RR_CHECK(a.render_settings.width  == b.render_settings.width);
    RR_CHECK(a.render_settings.height == b.render_settings.height);

    RR_CHECK(nearly_equal(a.camera.vertical_fov_degrees(),
                          b.camera.vertical_fov_degrees()));
    RR_CHECK(nearly_equal(a.camera.position(), b.camera.position()));
    RR_CHECK(nearly_equal(a.camera.forward(),  b.camera.forward()));

    RR_CHECK(nearly_equal(a.observer.velocity, b.observer.velocity));
    RR_CHECK(a.relativity.enable_aberration       == b.relativity.enable_aberration);
    RR_CHECK(nearly_equal(a.relativity.doppler_color_strength,
                          b.relativity.doppler_color_strength));

    RR_CHECK(a.materials.size() == b.materials.size());
    RR_CHECK(a.spheres.size()   == b.spheres.size());
    RR_CHECK(a.lights.size()    == b.lights.size());
    RR_CHECK(a.meshes.size()    == b.meshes.size());

    if (a.materials.size() == b.materials.size() && !a.materials.empty()) {
        RR_CHECK(a.materials.front().id == b.materials.front().id);
        RR_CHECK(a.materials.front().name == b.materials.front().name);
        RR_CHECK(nearly_equal(a.materials.front().params.baseColor,
                              b.materials.front().params.baseColor));
    }
    if (a.spheres.size() == b.spheres.size() && !a.spheres.empty()) {
        RR_CHECK(nearly_equal(a.spheres.front().geometry.center,
                              b.spheres.front().geometry.center));
        RR_CHECK(nearly_equal(a.spheres.front().geometry.radius,
                              b.spheres.front().geometry.radius));
        RR_CHECK(a.spheres.front().geometry.material_index
                 == b.spheres.front().geometry.material_index);
    }
    if (a.meshes.size() == b.meshes.size() && !a.meshes.empty()) {
        RR_CHECK(a.meshes.front().data.vertex_count()
                 == b.meshes.front().data.vertex_count());
        RR_CHECK(a.meshes.front().data.triangle_count()
                 == b.meshes.front().data.triangle_count());
        RR_CHECK(a.meshes.front().data.material_id
                 == b.meshes.front().data.material_id);
    }

    std::error_code ec;
    std::filesystem::remove(dst, ec);
}

}

int main() {
    test_load_test_minimal_scene();
    test_missing_file_fails_predictably();
    test_load_test_geometry_scene();
    test_minimal_scene_still_has_no_geometry();
    test_load_full_scene();
    test_writer_round_trip();

    std::printf("io_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
