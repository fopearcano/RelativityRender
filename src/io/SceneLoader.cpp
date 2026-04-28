#include "io/SceneLoader.h"

#include "geometry/Sphere.h"
#include "material/MaterialTypes.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// =============================================================================
// Minimal JSON parser
//
// In-house, ~250 lines. Pulling in a 25k-line third-party JSON header for
// six top-level keys is overkill at this milestone; if the format grows
// (textures, AOVs, env maps, animation curves) the JSON parser is the
// natural place to swap to nlohmann/json without changing the public
// `load_rrscene` surface.
//
// Supports: objects, arrays, strings (basic escapes), numbers (int / float
// with scientific notation), `true`, `false`, `null`. Skips standard
// JSON whitespace. Reports line/column on errors.
//
// Out of scope: \uNNNN escapes, comments (the spec forbids them), large
// streaming inputs.
// =============================================================================

namespace rr::io {

namespace {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type                                 type = Type::Null;
    bool                                 b    = false;
    double                               n    = 0.0;
    std::string                          s;
    std::vector<JsonValue>               arr;
    std::map<std::string, JsonValue>     obj;

    [[nodiscard]] bool is_null()   const { return type == Type::Null; }
    [[nodiscard]] bool is_bool()   const { return type == Type::Bool; }
    [[nodiscard]] bool is_number() const { return type == Type::Number; }
    [[nodiscard]] bool is_string() const { return type == Type::String; }
    [[nodiscard]] bool is_array()  const { return type == Type::Array; }
    [[nodiscard]] bool is_object() const { return type == Type::Object; }

    [[nodiscard]] const JsonValue* find(const std::string& key) const {
        if (!is_object()) return nullptr;
        const auto it = obj.find(key);
        return it == obj.end() ? nullptr : &it->second;
    }
};

class JsonParser {
public:
    JsonParser(const char* data, std::size_t size)
        : begin_(data), end_(data + size), p_(data) {}

    bool        parse(JsonValue& out, std::string& error);
    std::string format_error(const std::string& msg) const;

private:
    void skip_whitespace();
    bool parse_value (JsonValue& out, std::string& error);
    bool parse_object(JsonValue& out, std::string& error);
    bool parse_array (JsonValue& out, std::string& error);
    bool parse_string(JsonValue& out, std::string& error);
    bool parse_number(JsonValue& out, std::string& error);
    bool parse_keyword(const char* kw, std::size_t len);

    const char* begin_;
    const char* end_;
    const char* p_;
};

void JsonParser::skip_whitespace() {
    while (p_ < end_) {
        const char c = *p_;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++p_;
        } else {
            break;
        }
    }
}

bool JsonParser::parse(JsonValue& out, std::string& error) {
    skip_whitespace();
    if (!parse_value(out, error)) return false;
    skip_whitespace();
    if (p_ != end_) {
        error = format_error("unexpected trailing content");
        return false;
    }
    return true;
}

bool JsonParser::parse_value(JsonValue& out, std::string& error) {
    skip_whitespace();
    if (p_ >= end_) {
        error = format_error("unexpected end of input");
        return false;
    }
    const char c = *p_;
    if (c == '{') return parse_object(out, error);
    if (c == '[') return parse_array(out, error);
    if (c == '"') return parse_string(out, error);
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(out, error);
    if (parse_keyword("true",  4)) { out.type = JsonValue::Type::Bool;   out.b = true;  return true; }
    if (parse_keyword("false", 5)) { out.type = JsonValue::Type::Bool;   out.b = false; return true; }
    if (parse_keyword("null",  4)) { out.type = JsonValue::Type::Null;                  return true; }
    error = format_error("unexpected character");
    return false;
}

bool JsonParser::parse_object(JsonValue& out, std::string& error) {
    out.type = JsonValue::Type::Object;
    ++p_;  // consume '{'
    skip_whitespace();
    if (p_ < end_ && *p_ == '}') { ++p_; return true; }

    while (p_ < end_) {
        skip_whitespace();
        if (p_ >= end_ || *p_ != '"') {
            error = format_error("expected string key");
            return false;
        }
        JsonValue key;
        if (!parse_string(key, error)) return false;
        skip_whitespace();
        if (p_ >= end_ || *p_ != ':') {
            error = format_error("expected ':' after key");
            return false;
        }
        ++p_;  // consume ':'
        JsonValue value;
        if (!parse_value(value, error)) return false;
        out.obj[key.s] = std::move(value);
        skip_whitespace();
        if (p_ < end_ && *p_ == ',') { ++p_; continue; }
        if (p_ < end_ && *p_ == '}') { ++p_; return true; }
        error = format_error("expected ',' or '}'");
        return false;
    }
    error = format_error("unterminated object");
    return false;
}

bool JsonParser::parse_array(JsonValue& out, std::string& error) {
    out.type = JsonValue::Type::Array;
    ++p_;  // consume '['
    skip_whitespace();
    if (p_ < end_ && *p_ == ']') { ++p_; return true; }

    while (p_ < end_) {
        JsonValue v;
        if (!parse_value(v, error)) return false;
        out.arr.push_back(std::move(v));
        skip_whitespace();
        if (p_ < end_ && *p_ == ',') { ++p_; continue; }
        if (p_ < end_ && *p_ == ']') { ++p_; return true; }
        error = format_error("expected ',' or ']'");
        return false;
    }
    error = format_error("unterminated array");
    return false;
}

bool JsonParser::parse_string(JsonValue& out, std::string& error) {
    out.type = JsonValue::Type::String;
    ++p_;  // consume opening quote
    while (p_ < end_) {
        const char c = *p_++;
        if (c == '"') return true;
        if (c == '\\') {
            if (p_ >= end_) {
                error = format_error("unterminated escape");
                return false;
            }
            const char esc = *p_++;
            switch (esc) {
                case '"':  out.s.push_back('"');  break;
                case '\\': out.s.push_back('\\'); break;
                case '/':  out.s.push_back('/');  break;
                case 'b':  out.s.push_back('\b'); break;
                case 'f':  out.s.push_back('\f'); break;
                case 'n':  out.s.push_back('\n'); break;
                case 'r':  out.s.push_back('\r'); break;
                case 't':  out.s.push_back('\t'); break;
                default:
                    error = format_error("unsupported escape sequence");
                    return false;
            }
        } else {
            out.s.push_back(c);
        }
    }
    error = format_error("unterminated string");
    return false;
}

bool JsonParser::parse_number(JsonValue& out, std::string& error) {
    out.type = JsonValue::Type::Number;
    const char* start = p_;
    if (*p_ == '-') ++p_;
    while (p_ < end_) {
        const char c = *p_;
        if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E'
            || c == '+' || c == '-') {
            ++p_;
        } else {
            break;
        }
    }
    if (p_ == start) {
        error = format_error("invalid number");
        return false;
    }
    char* tail = nullptr;
    out.n = std::strtod(start, &tail);
    if (tail != p_) {
        error = format_error("malformed number");
        return false;
    }
    return true;
}

bool JsonParser::parse_keyword(const char* kw, std::size_t len) {
    if (static_cast<std::size_t>(end_ - p_) < len) return false;
    for (std::size_t i = 0; i < len; ++i) {
        if (p_[i] != kw[i]) return false;
    }
    p_ += len;
    return true;
}

std::string JsonParser::format_error(const std::string& msg) const {
    int line   = 1;
    int column = 1;
    for (const char* q = begin_; q < p_ && q < end_; ++q) {
        if (*q == '\n') { ++line; column = 1; } else { ++column; }
    }
    std::ostringstream os;
    os << msg << " (line " << line << ", column " << column << ")";
    return os.str();
}

// =============================================================================
// JsonValue -> Scene extraction helpers
// =============================================================================

bool extract_int(const JsonValue& v, int& out) {
    if (!v.is_number()) return false;
    out = static_cast<int>(v.n);
    return true;
}

bool extract_float(const JsonValue& v, float& out) {
    if (!v.is_number()) return false;
    out = static_cast<float>(v.n);
    return true;
}

bool extract_vec3(const JsonValue& v, rr::math::Vec3& out) {
    if (!v.is_array() || v.arr.size() != 3) return false;
    if (!v.arr[0].is_number() || !v.arr[1].is_number() || !v.arr[2].is_number()) {
        return false;
    }
    out.x = static_cast<float>(v.arr[0].n);
    out.y = static_cast<float>(v.arr[1].n);
    out.z = static_cast<float>(v.arr[2].n);
    return true;
}

// Helper that wraps "field is optional - keep default if missing or
// malformed; error if present-but-wrong-type". For required fields,
// callers check the value directly.
template <typename T, typename Extract>
bool maybe_extract(const JsonValue* parent, const char* key, T& out, Extract ex,
                   std::string& error, const char* context) {
    if (!parent) return true;
    const auto* v = parent->find(key);
    if (!v) return true;
    if (!ex(*v, out)) {
        std::ostringstream os;
        os << "field '" << context << "." << key << "' has wrong type";
        error = os.str();
        return false;
    }
    return true;
}

// =============================================================================
// Section extractors
// =============================================================================

bool load_render_settings(const JsonValue* node, rr::scene::Scene& scene,
                          std::string& error) {
    if (!node) return true;
    if (!node->is_object()) {
        error = "'render_settings' must be a JSON object";
        return false;
    }
    if (!maybe_extract(node, "width",  scene.render_settings.width,  extract_int, error, "render_settings"))  return false;
    if (!maybe_extract(node, "height", scene.render_settings.height, extract_int, error, "render_settings"))  return false;
    if (scene.render_settings.width  <= 0) { error = "render_settings.width must be > 0";  return false; }
    if (scene.render_settings.height <= 0) { error = "render_settings.height must be > 0"; return false; }
    return true;
}

bool load_camera(const JsonValue* node, rr::scene::Scene& scene,
                 std::string& error) {
    if (!node) return true;
    if (!node->is_object()) {
        error = "'camera' must be a JSON object";
        return false;
    }

    rr::math::Vec3 position{0.0f, 0.0f,  0.0f};
    rr::math::Vec3 forward {0.0f, 0.0f, -1.0f};
    rr::math::Vec3 up      {0.0f, 1.0f,  0.0f};
    float          fov    = 45.0f;

    if (!maybe_extract(node, "position", position, extract_vec3, error, "camera")) return false;
    if (!maybe_extract(node, "forward",  forward,  extract_vec3, error, "camera")) return false;
    if (!maybe_extract(node, "up",       up,       extract_vec3, error, "camera")) return false;
    if (!maybe_extract(node, "fov",      fov,      extract_float, error, "camera")) return false;

    if (rr::math::length_squared(forward) <= 0.0f) {
        error = "camera.forward must have non-zero length";
        return false;
    }
    if (!(fov > 0.0f && fov < 180.0f)) {
        error = "camera.fov must be in the open interval (0, 180)";
        return false;
    }

    scene.camera.set_position(position);
    scene.camera.look_at(position, position + forward, up);
    scene.camera.set_vertical_fov_degrees(fov);
    return true;
}

bool load_relativity(const JsonValue* node, rr::scene::Scene& scene,
                     std::string& error) {
    if (!node) return true;
    if (!node->is_object()) {
        error = "'relativity' must be a JSON object";
        return false;
    }

    float          beta_mag       = 0.0f;
    rr::math::Vec3 dir            = rr::math::Vec3{0.0f, 0.0f, -1.0f};
    float          aberration_str = 1.0f;
    float          doppler_str    = 1.0f;
    float          searchlight_str= 1.0f;

    if (!maybe_extract(node, "beta_velocity",        beta_mag,        extract_float, error, "relativity")) return false;
    if (!maybe_extract(node, "velocity_direction",   dir,             extract_vec3,  error, "relativity")) return false;
    if (!maybe_extract(node, "aberration_strength",  aberration_str,  extract_float, error, "relativity")) return false;
    if (!maybe_extract(node, "doppler_strength",     doppler_str,     extract_float, error, "relativity")) return false;
    if (!maybe_extract(node, "searchlight_strength", searchlight_str, extract_float, error, "relativity")) return false;

    // Per-spec clamps.
    beta_mag        = rr::math::clamp(beta_mag <= 0.0f ? 0.0f : beta_mag, 0.0f, 0.999999f);
    aberration_str  = rr::math::clamp(aberration_str,  0.0f, 1.0f);
    doppler_str     = rr::math::clamp(doppler_str,     0.0f, 1.0f);
    searchlight_str = rr::math::clamp(searchlight_str, 0.0f, 1.0f);

    // Reconstruct host velocity = beta * normalize(direction).
    if (rr::math::length_squared(dir) <= 0.0f) {
        dir = rr::math::Vec3{0.0f, 0.0f, -1.0f};
    }
    const auto normalised = rr::math::normalize(dir);
    scene.observer.velocity = normalised * beta_mag;

    // Map continuous strengths onto host params. Aberration is binary
    // on the host (no continuous knob); strength > 0 enables it.
    scene.relativity.enable_aberration       = aberration_str  > 0.0f;
    scene.relativity.enable_doppler          = doppler_str     > 0.0f;
    scene.relativity.enable_searchlight      = searchlight_str > 0.0f;
    scene.relativity.doppler_color_strength  = doppler_str;
    scene.relativity.searchlight_strength    = searchlight_str;
    return true;
}

bool load_materials(const JsonValue* node, rr::scene::Scene& scene,
                    std::string& error) {
    if (!node) return true;
    if (!node->is_array()) {
        error = "'materials' must be a JSON array";
        return false;
    }

    std::set<int> seen_ids;
    scene.materials.clear();
    scene.materials.reserve(node->arr.size());

    for (std::size_t i = 0; i < node->arr.size(); ++i) {
        const auto& entry = node->arr[i];
        if (!entry.is_object()) {
            std::ostringstream os;
            os << "materials[" << i << "] must be a JSON object";
            error = os.str();
            return false;
        }

        const auto* id_node = entry.find("id");
        if (!id_node || !id_node->is_number()) {
            std::ostringstream os;
            os << "materials[" << i << "].id is missing or non-numeric";
            error = os.str();
            return false;
        }
        const int id = static_cast<int>(id_node->n);
        if (id < 0) {
            std::ostringstream os;
            os << "materials[" << i << "].id must be non-negative";
            error = os.str();
            return false;
        }
        if (!seen_ids.insert(id).second) {
            std::ostringstream os;
            os << "materials[" << i << "].id (" << id
               << ") duplicates an earlier entry";
            error = os.str();
            return false;
        }

        rr::scene::SceneMaterial mat;
        mat.id = id;

        if (const auto* name_node = entry.find("name");
            name_node && name_node->is_string()) {
            mat.name = name_node->s;
        }

        // Per-field extract; per-spec defaults match the host
        // MaterialParams defaults that mat.params already carries.
        std::ostringstream context;
        context << "materials[" << i << "]";
        if (!maybe_extract(&entry, "base_color",
                           mat.params.baseColor, extract_vec3,
                           error, context.str().c_str())) return false;
        if (!maybe_extract(&entry, "emission_color",
                           mat.params.emissionColor, extract_vec3,
                           error, context.str().c_str())) return false;
        if (!maybe_extract(&entry, "emission_strength",
                           mat.params.emissionStrength, extract_float,
                           error, context.str().c_str())) return false;
        if (!maybe_extract(&entry, "roughness",
                           mat.params.roughness, extract_float,
                           error, context.str().c_str())) return false;

        // Per-spec clamps.
        mat.params.roughness        = rr::math::clamp(mat.params.roughness, 0.0f, 1.0f);
        if (mat.params.emissionStrength < 0.0f) mat.params.emissionStrength = 0.0f;
        const auto clamp_pos = [](rr::math::Vec3& v) {
            if (v.x < 0.0f) v.x = 0.0f;
            if (v.y < 0.0f) v.y = 0.0f;
            if (v.z < 0.0f) v.z = 0.0f;
        };
        clamp_pos(mat.params.baseColor);
        clamp_pos(mat.params.emissionColor);

        scene.materials.push_back(std::move(mat));
    }
    return true;
}

bool load_spheres(const JsonValue* node, rr::scene::Scene& scene,
                  std::string& error) {
    if (!node) return true;
    if (!node->is_array()) {
        error = "'spheres' must be a JSON array";
        return false;
    }

    scene.spheres.clear();
    scene.spheres.reserve(node->arr.size());

    for (std::size_t i = 0; i < node->arr.size(); ++i) {
        const auto& entry = node->arr[i];
        if (!entry.is_object()) {
            std::ostringstream os;
            os << "spheres[" << i << "] must be a JSON object";
            error = os.str();
            return false;
        }

        rr::math::Vec3 position;
        const auto* pos_node = entry.find("position");
        if (!pos_node || !extract_vec3(*pos_node, position)) {
            std::ostringstream os;
            os << "spheres[" << i << "].position is missing or not a Vec3";
            error = os.str();
            return false;
        }

        float radius = 0.0f;
        const auto* rad_node = entry.find("radius");
        if (!rad_node || !extract_float(*rad_node, radius)) {
            std::ostringstream os;
            os << "spheres[" << i << "].radius is missing or non-numeric";
            error = os.str();
            return false;
        }
        if (!(radius > 0.0f)) {
            std::ostringstream os;
            os << "spheres[" << i << "].radius must be > 0";
            error = os.str();
            return false;
        }

        int material_id = -1;
        std::ostringstream context;
        context << "spheres[" << i << "]";
        if (!maybe_extract(&entry, "material_id",
                           material_id, extract_int,
                           error, context.str().c_str())) return false;

        rr::scene::SceneSphere s;
        s.geometry.center         = position;
        s.geometry.radius         = radius;
        // The spec stores `material_id` as the lookup key (the entry's
        // `id`, not the array index). The renderer's GpuScene::upload_from
        // path translates this into the device-side material array index
        // - that wiring lives in the next M13 slice.
        s.geometry.material_index = material_id;
        s.material_index          = material_id;
        scene.spheres.push_back(s);
    }
    return true;
}

}  // anonymous namespace

LoadResult load_rrscene(const std::filesystem::path& path) {
    LoadResult result;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        result.message = "could not open file: " + path.string();
        return result;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string contents = buf.str();

    JsonParser parser(contents.data(), contents.size());
    JsonValue  root;
    std::string parse_error;
    if (!parser.parse(root, parse_error)) {
        result.message = "JSON parse error: " + parse_error;
        return result;
    }
    if (!root.is_object()) {
        result.message = "top-level value must be a JSON object";
        return result;
    }

    // Mandatory: version == 1.
    const auto* version_node = root.find("version");
    if (!version_node || !version_node->is_number()) {
        result.message = "missing or non-numeric 'version' field";
        return result;
    }
    const int version = static_cast<int>(version_node->n);
    if (version != 1) {
        std::ostringstream os;
        os << "unsupported schema version " << version
           << " (this loader implements v1 only)";
        result.message = os.str();
        return result;
    }

    // Optional sections - only the three this slice loads.
    std::string error;
    if (!load_render_settings(root.find("render_settings"), result.scene, error)) {
        result.message = error;
        return result;
    }
    if (!load_camera(root.find("camera"), result.scene, error)) {
        result.message = error;
        return result;
    }
    if (!load_relativity(root.find("relativity"), result.scene, error)) {
        result.message = error;
        return result;
    }
    if (!load_materials(root.find("materials"), result.scene, error)) {
        result.message = error;
        return result;
    }
    if (!load_spheres(root.find("spheres"), result.scene, error)) {
        result.message = error;
        return result;
    }

    // Aspect ratio is derived from render_settings.
    if (result.scene.render_settings.height > 0) {
        result.scene.camera.set_aspect(
            static_cast<float>(result.scene.render_settings.width)
            / static_cast<float>(result.scene.render_settings.height));
    }

    // `materials`, `spheres`, `lights`, `meshes` are deliberately
    // ignored at this milestone. Per spec they warn-and-continue;
    // logging happens in the caller, which has access to Logger.

    result.ok = true;
    return result;
}

}
