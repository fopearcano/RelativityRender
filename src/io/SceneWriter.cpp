#include "io/SceneWriter.h"

#include "geometry/Mesh.h"
#include "geometry/Sphere.h"
#include "lighting/Light.h"
#include "material/MaterialTypes.h"
#include "math/MathUtils.h"
#include "math/Transform.h"
#include "math/Vec3.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <ostream>
#include <sstream>
#include <string>

namespace rr::io {

namespace {

// Tiny indented JSON writer. Hand-rolled so the writer matches
// the loader's footprint (no third-party header). The output
// favours readability over compactness; the writer is rarely
// hot.
class JsonWriter {
public:
    explicit JsonWriter(std::ostream& os) : os_(os) {}

    void number(float v) {
        // Print a clean float: no trailing zeros, but always at
        // least one decimal so the value reads as floating-point
        // when re-parsed.
        std::ostringstream tmp;
        tmp.imbue(std::locale::classic());
        tmp.setf(std::ios::fmtflags(0), std::ios::floatfield);
        tmp.precision(7);
        tmp << v;
        std::string s = tmp.str();
        if (s.find('.') == std::string::npos
            && s.find('e') == std::string::npos
            && s.find('E') == std::string::npos
            && s.find('n') == std::string::npos /* nan / inf */) {
            s += ".0";
        }
        os_ << s;
    }

    void number(int v)         { os_ << v; }
    void boolean(bool v)       { os_ << (v ? "true" : "false"); }
    void string(const std::string& s) {
        os_ << '"';
        for (char c : s) {
            switch (c) {
                case '"':  os_ << "\\\""; break;
                case '\\': os_ << "\\\\"; break;
                case '\n': os_ << "\\n";  break;
                case '\r': os_ << "\\r";  break;
                case '\t': os_ << "\\t";  break;
                default:   os_ << c;       break;
            }
        }
        os_ << '"';
    }

    void vec3(rr::math::Vec3 v) {
        os_ << '[';
        number(v.x); os_ << ", ";
        number(v.y); os_ << ", ";
        number(v.z);
        os_ << ']';
    }

    std::ostream& raw() { return os_; }

private:
    std::ostream& os_;
};

void write_indent(std::ostream& os, int depth) {
    for (int i = 0; i < depth; ++i) os << "    ";
}

void write_render_settings(JsonWriter& w, const rr::scene::Scene& s, int depth) {
    auto& os = w.raw();
    os << '\n';
    write_indent(os, depth); os << "\"render_settings\": {\n";
    write_indent(os, depth + 1); os << "\"width\":  ";  w.number(s.render_settings.width);  os << ",\n";
    write_indent(os, depth + 1); os << "\"height\": ";  w.number(s.render_settings.height); os << '\n';
    write_indent(os, depth); os << "}";
}

void write_camera(JsonWriter& w, const rr::scene::Scene& s, int depth) {
    auto& os = w.raw();
    os << '\n';
    write_indent(os, depth); os << "\"camera\": {\n";
    write_indent(os, depth + 1); os << "\"position\": "; w.vec3(s.camera.position()); os << ",\n";
    write_indent(os, depth + 1); os << "\"forward\":  "; w.vec3(s.camera.forward());  os << ",\n";
    write_indent(os, depth + 1); os << "\"up\":       "; w.vec3(s.camera.up());       os << ",\n";
    write_indent(os, depth + 1); os << "\"fov\":      "; w.number(s.camera.vertical_fov_degrees()); os << '\n';
    write_indent(os, depth); os << "}";
}

void write_relativity(JsonWriter& w, const rr::scene::Scene& s, int depth) {
    auto& os = w.raw();
    const float beta_mag = rr::math::length(s.observer.velocity);
    auto dir = beta_mag > 0.0f
        ? s.observer.velocity * (1.0f / beta_mag)
        : rr::math::Vec3{0.0f, 0.0f, -1.0f};

    const float doppler_str     = s.relativity.doppler_color_strength;
    const float searchlight_str = s.relativity.searchlight_strength;
    const float aberration_str  = s.relativity.enable_aberration ? 1.0f : 0.0f;

    os << '\n';
    write_indent(os, depth); os << "\"relativity\": {\n";
    write_indent(os, depth + 1); os << "\"beta_velocity\":        "; w.number(beta_mag);        os << ",\n";
    write_indent(os, depth + 1); os << "\"velocity_direction\":   "; w.vec3(dir);               os << ",\n";
    write_indent(os, depth + 1); os << "\"aberration_strength\":  "; w.number(aberration_str);  os << ",\n";
    write_indent(os, depth + 1); os << "\"doppler_strength\":     "; w.number(doppler_str);     os << ",\n";
    write_indent(os, depth + 1); os << "\"searchlight_strength\": "; w.number(searchlight_str); os << '\n';
    write_indent(os, depth); os << "}";
}

void write_materials(JsonWriter& w, const rr::scene::Scene& s, int depth) {
    auto& os = w.raw();
    os << '\n';
    write_indent(os, depth); os << "\"materials\": [";
    if (s.materials.empty()) { os << "]"; return; }
    os << '\n';
    for (std::size_t i = 0; i < s.materials.size(); ++i) {
        const auto& m = s.materials[i];
        write_indent(os, depth + 1); os << "{\n";
        write_indent(os, depth + 2); os << "\"id\":                "; w.number(m.id); os << ",\n";
        write_indent(os, depth + 2); os << "\"name\":              "; w.string(m.name); os << ",\n";
        write_indent(os, depth + 2); os << "\"base_color\":        "; w.vec3(m.params.baseColor);     os << ",\n";
        write_indent(os, depth + 2); os << "\"emission_color\":    "; w.vec3(m.params.emissionColor); os << ",\n";
        write_indent(os, depth + 2); os << "\"emission_strength\": "; w.number(m.params.emissionStrength); os << ",\n";
        write_indent(os, depth + 2); os << "\"roughness\":         "; w.number(m.params.roughness); os << '\n';
        write_indent(os, depth + 1); os << "}";
        if (i + 1 < s.materials.size()) os << ',';
        os << '\n';
    }
    write_indent(os, depth); os << "]";
}

void write_spheres(JsonWriter& w, const rr::scene::Scene& s, int depth) {
    auto& os = w.raw();
    os << '\n';
    write_indent(os, depth); os << "\"spheres\": [";
    if (s.spheres.empty()) { os << "]"; return; }
    os << '\n';
    for (std::size_t i = 0; i < s.spheres.size(); ++i) {
        const auto& sp = s.spheres[i];
        write_indent(os, depth + 1); os << "{\n";
        write_indent(os, depth + 2); os << "\"position\":    "; w.vec3(sp.geometry.center); os << ",\n";
        write_indent(os, depth + 2); os << "\"radius\":      "; w.number(sp.geometry.radius); os << ",\n";
        write_indent(os, depth + 2); os << "\"material_id\": "; w.number(sp.geometry.material_index); os << '\n';
        write_indent(os, depth + 1); os << "}";
        if (i + 1 < s.spheres.size()) os << ',';
        os << '\n';
    }
    write_indent(os, depth); os << "]";
}

bool write_lights(JsonWriter& w, const rr::scene::Scene& s, int depth,
                  std::string& error) {
    auto& os = w.raw();
    os << '\n';
    write_indent(os, depth); os << "\"lights\": [";
    if (s.lights.empty()) { os << "]"; return true; }
    os << '\n';
    for (std::size_t i = 0; i < s.lights.size(); ++i) {
        const auto& L = s.lights[i].data;
        const char* type_str = nullptr;
        switch (L.type) {
            case rr::lighting::LightType::Point:       type_str = "point";       break;
            case rr::lighting::LightType::Directional: type_str = "directional"; break;
            case rr::lighting::LightType::Area:
            case rr::lighting::LightType::Environment:
                // v1 doesn't surface these; skip the entry rather
                // than emit something the loader would reject.
                continue;
            default:
                error = "lights[i].type holds an unrecognised LightType";
                return false;
        }

        write_indent(os, depth + 1); os << "{\n";
        write_indent(os, depth + 2); os << "\"type\":      "; w.string(type_str); os << ",\n";
        if (L.type == rr::lighting::LightType::Point) {
            write_indent(os, depth + 2); os << "\"position\":  "; w.vec3(L.position); os << ",\n";
        } else {
            write_indent(os, depth + 2); os << "\"direction\": "; w.vec3(L.direction); os << ",\n";
        }
        write_indent(os, depth + 2); os << "\"color\":     "; w.vec3(L.color); os << ",\n";
        write_indent(os, depth + 2); os << "\"intensity\": "; w.number(L.intensity); os << '\n';
        write_indent(os, depth + 1); os << "}";
        if (i + 1 < s.lights.size()) os << ',';
        os << '\n';
    }
    write_indent(os, depth); os << "]";
    return true;
}

void write_transform(JsonWriter& w, const rr::math::Transform& t, int depth) {
    auto& os = w.raw();
    os << "{\n";
    write_indent(os, depth + 1); os << "\"position\": "; w.vec3(t.position);                os << ",\n";
    write_indent(os, depth + 1); os << "\"rotation\": "; w.vec3(t.euler_rotation_radians); os << ",\n";
    write_indent(os, depth + 1); os << "\"scale\":    "; w.vec3(t.scale);                  os << '\n';
    write_indent(os, depth); os << "}";
}

void write_meshes(JsonWriter& w, const rr::scene::Scene& s, int depth) {
    auto& os = w.raw();
    os << '\n';
    write_indent(os, depth); os << "\"meshes\": [";
    if (s.meshes.empty()) { os << "]"; return; }
    os << '\n';
    for (std::size_t i = 0; i < s.meshes.size(); ++i) {
        const auto& m = s.meshes[i];
        write_indent(os, depth + 1); os << "{\n";
        write_indent(os, depth + 2); os << "\"name\": "; w.string(m.object.name); os << ",\n";

        write_indent(os, depth + 2); os << "\"vertices\": [";
        if (!m.data.vertices.empty()) {
            os << '\n';
            for (std::size_t v = 0; v < m.data.vertices.size(); ++v) {
                write_indent(os, depth + 3); w.vec3(m.data.vertices[v].position);
                if (v + 1 < m.data.vertices.size()) os << ',';
                os << '\n';
            }
            write_indent(os, depth + 2);
        }
        os << "],\n";

        write_indent(os, depth + 2); os << "\"triangles\": [";
        if (!m.data.triangles.empty()) {
            os << '\n';
            for (std::size_t t = 0; t < m.data.triangles.size(); ++t) {
                const auto& tri = m.data.triangles[t];
                write_indent(os, depth + 3);
                os << "[" << tri.v0 << ", " << tri.v1 << ", " << tri.v2 << "]";
                if (t + 1 < m.data.triangles.size()) os << ',';
                os << '\n';
            }
            write_indent(os, depth + 2);
        }
        os << "],\n";

        write_indent(os, depth + 2); os << "\"material_id\": "; w.number(m.data.material_id); os << ",\n";
        write_indent(os, depth + 2); os << "\"transform\": ";
        write_transform(w, m.data.transform, depth + 2);
        os << '\n';

        write_indent(os, depth + 1); os << "}";
        if (i + 1 < s.meshes.size()) os << ',';
        os << '\n';
    }
    write_indent(os, depth); os << "]";
}

}  // anonymous namespace

WriteResult save_rrscene(const rr::scene::Scene& scene,
                         const std::filesystem::path& path) {
    WriteResult result;

    // Create the parent directory if needed; failure is non-fatal
    // here because the open-for-write below will produce a clearer
    // diagnostic if the destination is genuinely unwritable.
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        result.message = "could not open file for write: " + path.string();
        return result;
    }

    JsonWriter w(out);
    auto&      os = w.raw();
    os << "{\n";
    constexpr int kDepth = 1;

    write_indent(os, kDepth); os << "\"version\": 1,";
    write_render_settings(w, scene, kDepth); os << ",";
    write_camera(w, scene, kDepth);          os << ",";
    write_relativity(w, scene, kDepth);      os << ",";
    write_materials(w, scene, kDepth);       os << ",";
    write_spheres(w, scene, kDepth);         os << ",";
    std::string error;
    if (!write_lights(w, scene, kDepth, error)) {
        result.message = error;
        return result;
    }
    os << ",";
    write_meshes(w, scene, kDepth);
    os << '\n' << "}\n";

    if (!out.good()) {
        result.message = "stream write failed: " + path.string();
        return result;
    }

    result.ok = true;
    return result;
}

}
