// Hand-rolled assertion runner. The real test framework comes with
// the M2 deferred items.

#include "camera/Camera.h"
#include "geometry/Sphere.h"
#include "math/Vec3.h"
#include "relativity/RelativityParams.h"
#include "scene/Scene.h"
#include "scene/SceneObject.h"
#include "scene/Transform.h"

#include <cstdio>
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

using rr::math::Vec3;
using rr::scene::RenderSettings;
using rr::scene::Scene;
using rr::scene::SceneLight;
using rr::scene::SceneMaterial;
using rr::scene::SceneMesh;
using rr::scene::SceneObject;
using rr::scene::SceneSphere;
using rr::scene::Transform;

void test_transform_defaults() {
    Transform t;
    RR_CHECK(t.position               == Vec3(0, 0, 0));
    RR_CHECK(t.euler_rotation_radians == Vec3(0, 0, 0));
    RR_CHECK(t.scale                  == Vec3(1, 1, 1));

    const auto i = Transform::identity();
    RR_CHECK(i.position               == Vec3(0, 0, 0));
    RR_CHECK(i.scale                  == Vec3(1, 1, 1));
}

void test_scene_object_defaults() {
    SceneObject o;
    RR_CHECK(o.name.empty());
    RR_CHECK(o.visible);
    RR_CHECK(o.transform.position == Vec3(0, 0, 0));
    RR_CHECK(o.transform.scale    == Vec3(1, 1, 1));
}

void test_render_settings_defaults() {
    RenderSettings r;
    RR_CHECK(r.width  == 1280);
    RR_CHECK(r.height == 720);
    RR_CHECK(r.samples_per_pixel == 1);
    RR_CHECK(r.max_depth         == 1);
}

void test_default_scene_is_empty() {
    Scene s;
    RR_CHECK(s.spheres.empty());
    RR_CHECK(s.meshes.empty());
    RR_CHECK(s.materials.empty());
    RR_CHECK(s.lights.empty());

    // Camera, observer, params, render settings start at sensible
    // defaults inherited from the upstream modules.
    RR_CHECK(s.camera.position()    == Vec3(0, 0, 0));
    RR_CHECK(s.observer.velocity    == Vec3(0, 0, 0));
    RR_CHECK(s.relativity.enable_aberration);
    RR_CHECK(s.render_settings.width == 1280);
}

void test_population_round_trip() {
    Scene s;

    SceneMaterial mat;
    mat.id                = 7;
    mat.name              = "matte_white";
    mat.params.baseColor  = Vec3{0.9f, 0.9f, 0.9f};
    s.materials.push_back(mat);
    const int matte_index = static_cast<int>(s.materials.size()) - 1;

    SceneSphere sph;
    sph.object.name      = "test_sphere";
    sph.geometry.center  = Vec3{0, 0, -3};
    sph.geometry.radius  = 1.0f;
    sph.material_index   = matte_index;
    s.spheres.push_back(sph);

    SceneMesh mesh;
    mesh.object.name = "test_mesh";
    mesh.source_path = "assets/teapot.obj";
    mesh.data.vertices.push_back({Vec3{0, 0, 0}, Vec3{0, 0, 1}, rr::math::Vec2{0, 0}});
    mesh.data.vertices.push_back({Vec3{1, 0, 0}, Vec3{0, 0, 1}, rr::math::Vec2{1, 0}});
    mesh.data.vertices.push_back({Vec3{0, 1, 0}, Vec3{0, 0, 1}, rr::math::Vec2{0, 1}});
    mesh.data.triangles.push_back({0, 1, 2});
    mesh.data.material_id = matte_index;
    s.meshes.push_back(mesh);

    SceneLight light;
    light.object.name = "key_light";
    light.data        = rr::lighting::make_point_light(
        Vec3{5, 5, 5}, Vec3{1.0f, 0.95f, 0.9f}, 8.0f);
    s.lights.push_back(light);

    RR_CHECK(s.materials.size() == 1u);
    RR_CHECK(s.spheres.size()   == 1u);
    RR_CHECK(s.meshes.size()    == 1u);
    RR_CHECK(s.lights.size()    == 1u);

    RR_CHECK(s.spheres[0].material_index == matte_index);
    RR_CHECK(s.materials[matte_index].id   == 7);
    RR_CHECK(s.materials[matte_index].name == std::string("matte_white"));
    RR_CHECK(s.materials[matte_index].params.baseColor == Vec3(0.9f, 0.9f, 0.9f));
    RR_CHECK(s.spheres[0].geometry.center == Vec3(0, 0, -3));
    RR_CHECK(s.spheres[0].geometry.radius == 1.0f);
    RR_CHECK(s.meshes[0].data.vertex_count()   == 3u);
    RR_CHECK(s.meshes[0].data.triangle_count() == 1u);
    RR_CHECK(s.meshes[0].data.material_id      == matte_index);
    RR_CHECK(s.lights[0].data.type     == rr::lighting::LightType::Point);
    RR_CHECK(s.lights[0].data.position == Vec3(5, 5, 5));
    RR_CHECK(s.lights[0].data.intensity == 8.0f);
}

void test_clear_resets_lists_and_state() {
    Scene s;
    s.spheres.push_back({});
    s.meshes.push_back({});
    s.materials.push_back({});
    s.lights.push_back({});
    s.observer.velocity            = Vec3{0.5f, 0, 0};
    s.relativity.enable_aberration = false;
    s.render_settings.width        = 1920;

    s.clear();

    RR_CHECK(s.spheres.empty());
    RR_CHECK(s.meshes.empty());
    RR_CHECK(s.materials.empty());
    RR_CHECK(s.lights.empty());
    RR_CHECK(s.observer.velocity   == Vec3(0, 0, 0));
    RR_CHECK(s.relativity.enable_aberration);
    RR_CHECK(s.render_settings.width == 1280);
}

}

int main() {
    test_transform_defaults();
    test_scene_object_defaults();
    test_render_settings_defaults();
    test_default_scene_is_empty();
    test_population_round_trip();
    test_clear_resets_lists_and_state();

    std::printf("scene_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
