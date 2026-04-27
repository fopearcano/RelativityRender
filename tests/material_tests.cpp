// Hand-rolled assertion runner. The real test framework comes with
// the M2 deferred items.

#include "material/Material.h"
#include "material/MaterialTypes.h"
#include "math/Vec3.h"

#include <cstdio>
#include <string>
#include <utility>

namespace {

int g_total  = 0;
int g_failed = 0;

float abs_f(float a) { return a < 0.0f ? -a : a; }

bool nearly_equal(float a, float b, float eps = 1.0e-5f) {
    const float scale  = 1.0f > abs_f(a) ? 1.0f : abs_f(a);
    const float scale2 = scale > abs_f(b) ? scale : abs_f(b);
    return abs_f(a - b) <= eps * scale2;
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

using rr::material::Material;
using rr::material::MaterialParams;
using rr::math::Vec3;

void test_material_params_defaults() {
    MaterialParams p;
    RR_CHECK(p.baseColor        == Vec3(0.8f, 0.8f, 0.8f));
    RR_CHECK(p.emissionColor    == Vec3(0.0f, 0.0f, 0.0f));
    RR_CHECK(nearly_equal(p.emissionStrength, 0.0f));
    RR_CHECK(nearly_equal(p.roughness,        0.5f));
    RR_CHECK(nearly_equal(p.metallic,         0.0f));
    RR_CHECK(nearly_equal(p.specular,         0.5f));
    RR_CHECK(nearly_equal(p.transmission,     0.0f));
}

void test_material_default_construct() {
    Material m;
    RR_CHECK(m.name().empty());
    RR_CHECK(m.params().baseColor == Vec3(0.8f, 0.8f, 0.8f));
}

void test_material_construct_with_params() {
    MaterialParams p;
    p.baseColor = Vec3{0.1f, 0.2f, 0.3f};
    p.metallic  = 1.0f;
    Material m(p);
    RR_CHECK(m.name().empty());
    RR_CHECK(m.params().baseColor == Vec3(0.1f, 0.2f, 0.3f));
    RR_CHECK(nearly_equal(m.params().metallic, 1.0f));
}

void test_material_construct_with_name_and_params() {
    MaterialParams p;
    p.roughness = 0.25f;
    Material m("brushed_steel", p);
    RR_CHECK(m.name() == std::string("brushed_steel"));
    RR_CHECK(nearly_equal(m.params().roughness, 0.25f));
}

void test_material_setters_replace_state() {
    Material m;
    m.set_name("red_plastic");
    MaterialParams p;
    p.baseColor = Vec3{0.9f, 0.1f, 0.1f};
    p.roughness = 0.7f;
    m.set_params(p);
    RR_CHECK(m.name() == std::string("red_plastic"));
    RR_CHECK(m.params().baseColor == Vec3(0.9f, 0.1f, 0.1f));
    RR_CHECK(nearly_equal(m.params().roughness, 0.7f));
}

void test_material_mutable_params_accessor() {
    Material m;
    m.params().emissionColor    = Vec3{1.0f, 0.5f, 0.2f};
    m.params().emissionStrength = 4.0f;
    RR_CHECK(m.params().emissionColor == Vec3(1.0f, 0.5f, 0.2f));
    RR_CHECK(nearly_equal(m.params().emissionStrength, 4.0f));
    // Other fields untouched.
    RR_CHECK(m.params().baseColor == Vec3(0.8f, 0.8f, 0.8f));
}

void test_make_diffuse_preset() {
    const auto m = Material::make_diffuse(Vec3{0.2f, 0.6f, 0.9f});
    RR_CHECK(m.params().baseColor == Vec3(0.2f, 0.6f, 0.9f));
    RR_CHECK(nearly_equal(m.params().metallic,  0.0f));
    RR_CHECK(nearly_equal(m.params().roughness, 1.0f));
    RR_CHECK(nearly_equal(m.params().emissionStrength, 0.0f));
}

void test_make_emissive_preset() {
    const auto m = Material::make_emissive(Vec3{1.0f, 0.7f, 0.4f}, 5.0f);
    RR_CHECK(m.params().emissionColor == Vec3(1.0f, 0.7f, 0.4f));
    RR_CHECK(nearly_equal(m.params().emissionStrength, 5.0f));
    RR_CHECK(m.params().baseColor == Vec3(0.0f, 0.0f, 0.0f));
}

void test_make_metal_preset() {
    const auto m = Material::make_metal(Vec3{0.95f, 0.93f, 0.88f}, 0.1f);
    RR_CHECK(m.params().baseColor == Vec3(0.95f, 0.93f, 0.88f));
    RR_CHECK(nearly_equal(m.params().metallic,  1.0f));
    RR_CHECK(nearly_equal(m.params().roughness, 0.1f));
    RR_CHECK(nearly_equal(m.params().specular,  1.0f));
}

void test_transmission_placeholder_round_trips() {
    // The transmission slot is reserved (no shader behaviour yet) but
    // the scene file format and node editor will read it; verify it
    // round-trips through the params struct as a plain float.
    MaterialParams p;
    p.transmission = 0.6f;
    Material m(p);
    RR_CHECK(nearly_equal(m.params().transmission, 0.6f));

    m.params().transmission = 0.25f;
    RR_CHECK(nearly_equal(m.params().transmission, 0.25f));
}

}

int main() {
    test_material_params_defaults();
    test_material_default_construct();
    test_material_construct_with_params();
    test_material_construct_with_name_and_params();
    test_material_setters_replace_state();
    test_material_mutable_params_accessor();
    test_make_diffuse_preset();
    test_make_emissive_preset();
    test_make_metal_preset();
    test_transmission_placeholder_round_trips();

    std::printf("material_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
