#include "io/SceneLoader.h"

#include "camera/Camera.h"
#include "geometry/Sphere.h"
#include "material/MaterialTypes.h"
#include "math/Vec3.h"
#include "relativity/RelativityParams.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace rr::io {

// =====================================================================
// Stage 10B.2 - JSON value tree, tokeniser, parser, schema mapper.
// =====================================================================
//
// Hand-rolled per the JSON-strategy decision recorded in Stage 10B.1
// (see docs/BUILD_PLAN.md). The pipeline is:
//
//   text -> tokeniser -> recursive-descent parser -> JsonValue tree
//        -> schema mapper -> rr::scene::Scene fields
//
// Stage 10B.2 schema scope is intentionally narrow: only the
// top-level `version` field and the `render_settings` block are
// mapped. Other top-level keys (`camera`, `relativity`, ...) are
// parsed for syntactic validity, then ignored. Follow-up sub-stages
// add the remaining mappers without touching the parser layer.

namespace {

// --- JSON value tree -------------------------------------------------

enum class JsonKind {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
};

struct JsonValue {
    JsonKind                                                  kind = JsonKind::Null;
    bool                                                      b    = false;
    double                                                    n    = 0.0;
    std::string                                               s;
    std::vector<JsonValue>                                    arr;
    // Object: insertion-ordered key/value pairs. We keep a vector
    // (not a map) so the order is stable for diagnostics and so
    // duplicate keys are observable - the schema mapper picks the
    // *last* occurrence (standard JSON convention).
    std::vector<std::pair<std::string, JsonValue>>            obj;

    [[nodiscard]] const JsonValue* find(const std::string& key) const {
        const JsonValue* hit = nullptr;
        for (const auto& kv : obj) {
            if (kv.first == key) hit = &kv.second;
        }
        return hit;
    }

    // Look up `key` and fall back to `alias` (used to accept the
    // user's shorthand `samples` for `samples_per_pixel`,
    // `output` for `output_path`, `render` for `render_settings`).
    [[nodiscard]] const JsonValue* find_or(const std::string& key,
                                           const std::string& alias) const {
        if (const auto* v = find(key))   return v;
        if (const auto* v = find(alias)) return v;
        return nullptr;
    }
};

// --- Parse error type -----------------------------------------------

struct ParseError {
    std::string message;
    int         line   = 1;  // 1-based
    int         column = 1;  // 1-based
};

// --- Tokeniser + recursive-descent parser ---------------------------
//
// Position-tracking single-pass parser. No separate token stream -
// `Parser::peek_*` / `Parser::consume_*` work directly off the
// source text. RFC-8259 JSON, with one v1 deviation: we follow the
// `RRSCENE_FORMAT.md` §15 stance that comments are not allowed.

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    // Top-level entry: parse a single JSON value followed by
    // optional whitespace + EOF.
    bool parse_document(JsonValue& out, ParseError& err) {
        skip_ws();
        if (!parse_value(out, err)) return false;
        skip_ws();
        if (!at_end()) {
            err = make_error("trailing content after top-level JSON value");
            return false;
        }
        return true;
    }

private:
    const std::string& text_;
    std::size_t        pos_  = 0;
    int                line_ = 1;
    int                col_  = 1;

    [[nodiscard]] bool at_end() const { return pos_ >= text_.size(); }

    [[nodiscard]] char peek() const {
        return at_end() ? '\0' : text_[pos_];
    }

    void advance() {
        if (at_end()) return;
        const char c = text_[pos_++];
        if (c == '\n') { ++line_; col_ = 1; }
        else           { ++col_; }
    }

    [[nodiscard]] ParseError make_error(std::string msg) const {
        return ParseError{std::move(msg), line_, col_};
    }

    void skip_ws() {
        while (!at_end()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                advance();
            } else {
                break;
            }
        }
    }

    bool match_literal(const char* lit) {
        const std::size_t n = std::char_traits<char>::length(lit);
        if (pos_ + n > text_.size()) return false;
        for (std::size_t i = 0; i < n; ++i) {
            if (text_[pos_ + i] != lit[i]) return false;
        }
        for (std::size_t i = 0; i < n; ++i) advance();
        return true;
    }

    bool parse_value(JsonValue& out, ParseError& err) {
        skip_ws();
        if (at_end()) {
            err = make_error("unexpected end of input, expected JSON value");
            return false;
        }

        const char c = peek();
        switch (c) {
            case '{': return parse_object(out, err);
            case '[': return parse_array(out, err);
            case '"': return parse_string_value(out, err);
            case 't':
            case 'f': return parse_bool(out, err);
            case 'n': return parse_null(out, err);
            default:
                if (c == '-' || (c >= '0' && c <= '9')) {
                    return parse_number(out, err);
                }
                err = make_error(std::string("unexpected character '")
                               + c + "', expected JSON value");
                return false;
        }
    }

    bool parse_object(JsonValue& out, ParseError& err) {
        out = JsonValue{};
        out.kind = JsonKind::Object;
        advance();          // consume '{'
        skip_ws();
        if (peek() == '}') { advance(); return true; }

        for (;;) {
            skip_ws();
            if (peek() != '"') {
                err = make_error("expected string key in object");
                return false;
            }
            std::string key;
            if (!parse_string_literal(key, err)) return false;

            skip_ws();
            if (peek() != ':') {
                err = make_error("expected ':' after object key");
                return false;
            }
            advance();      // consume ':'

            JsonValue value;
            if (!parse_value(value, err)) return false;
            out.obj.emplace_back(std::move(key), std::move(value));

            skip_ws();
            const char nxt = peek();
            if (nxt == ',') { advance(); continue; }
            if (nxt == '}') { advance(); return true; }

            err = make_error("expected ',' or '}' in object");
            return false;
        }
    }

    bool parse_array(JsonValue& out, ParseError& err) {
        out = JsonValue{};
        out.kind = JsonKind::Array;
        advance();          // consume '['
        skip_ws();
        if (peek() == ']') { advance(); return true; }

        for (;;) {
            JsonValue value;
            if (!parse_value(value, err)) return false;
            out.arr.push_back(std::move(value));

            skip_ws();
            const char nxt = peek();
            if (nxt == ',') { advance(); continue; }
            if (nxt == ']') { advance(); return true; }

            err = make_error("expected ',' or ']' in array");
            return false;
        }
    }

    bool parse_string_value(JsonValue& out, ParseError& err) {
        out = JsonValue{};
        out.kind = JsonKind::String;
        return parse_string_literal(out.s, err);
    }

    // Reads a JSON string literal (opening quote already not yet
    // consumed) into `dst`, handling the standard JSON escapes:
    // \" \\ \/ \b \f \n \r \t \uXXXX. Surrogate pairs are decoded
    // to UTF-8.
    bool parse_string_literal(std::string& dst, ParseError& err) {
        dst.clear();
        if (peek() != '"') {
            err = make_error("expected '\"' to start string");
            return false;
        }
        advance();          // consume opening '"'

        for (;;) {
            if (at_end()) {
                err = make_error("unterminated string");
                return false;
            }
            const char c = peek();
            if (c == '"') { advance(); return true; }
            if (c == '\\') {
                advance();
                if (at_end()) {
                    err = make_error("unterminated escape sequence");
                    return false;
                }
                const char esc = peek();
                advance();
                switch (esc) {
                    case '"':  dst.push_back('"');  break;
                    case '\\': dst.push_back('\\'); break;
                    case '/':  dst.push_back('/');  break;
                    case 'b':  dst.push_back('\b'); break;
                    case 'f':  dst.push_back('\f'); break;
                    case 'n':  dst.push_back('\n'); break;
                    case 'r':  dst.push_back('\r'); break;
                    case 't':  dst.push_back('\t'); break;
                    case 'u': {
                        std::uint32_t cp = 0;
                        if (!parse_hex4(cp, err)) return false;
                        // Surrogate-pair handling.
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            if (peek() != '\\') {
                                err = make_error("expected low surrogate "
                                                 "after high surrogate");
                                return false;
                            }
                            advance();
                            if (peek() != 'u') {
                                err = make_error("expected '\\u' for low "
                                                 "surrogate");
                                return false;
                            }
                            advance();
                            std::uint32_t low = 0;
                            if (!parse_hex4(low, err)) return false;
                            if (low < 0xDC00 || low > 0xDFFF) {
                                err = make_error("invalid low surrogate");
                                return false;
                            }
                            cp = 0x10000 + ((cp - 0xD800) << 10)
                                         + (low - 0xDC00);
                        }
                        encode_utf8(cp, dst);
                        break;
                    }
                    default:
                        err = make_error(std::string("invalid escape '\\")
                                       + esc + "'");
                        return false;
                }
            } else {
                // Reject raw control characters per RFC 8259.
                if (static_cast<unsigned char>(c) < 0x20) {
                    err = make_error("unescaped control character in string");
                    return false;
                }
                dst.push_back(c);
                advance();
            }
        }
    }

    bool parse_hex4(std::uint32_t& out, ParseError& err) {
        out = 0;
        for (int i = 0; i < 4; ++i) {
            if (at_end()) {
                err = make_error("truncated \\u escape");
                return false;
            }
            const char c = peek();
            std::uint32_t digit = 0;
            if      (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = 10 + (c - 'a');
            else if (c >= 'A' && c <= 'F') digit = 10 + (c - 'A');
            else {
                err = make_error("invalid hex digit in \\u escape");
                return false;
            }
            out = (out << 4) | digit;
            advance();
        }
        return true;
    }

    static void encode_utf8(std::uint32_t cp, std::string& dst) {
        if (cp <= 0x7F) {
            dst.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            dst.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            dst.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            dst.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            dst.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            dst.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            dst.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            dst.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            dst.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            dst.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool parse_bool(JsonValue& out, ParseError& err) {
        out = JsonValue{};
        out.kind = JsonKind::Bool;
        if (match_literal("true"))  { out.b = true;  return true; }
        if (match_literal("false")) { out.b = false; return true; }
        err = make_error("expected 'true' or 'false'");
        return false;
    }

    bool parse_null(JsonValue& out, ParseError& err) {
        out = JsonValue{};
        out.kind = JsonKind::Null;
        if (match_literal("null")) return true;
        err = make_error("expected 'null'");
        return false;
    }

    // RFC-8259 number grammar:
    //   '-'? ( '0' | [1-9][0-9]* ) ( '.' [0-9]+ )? ( [eE] [+-]? [0-9]+ )?
    bool parse_number(JsonValue& out, ParseError& err) {
        out = JsonValue{};
        out.kind = JsonKind::Number;
        const std::size_t start = pos_;

        if (peek() == '-') advance();

        if (peek() == '0') {
            advance();
        } else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9') advance();
        } else {
            err = make_error("invalid number: missing integer part");
            return false;
        }

        if (peek() == '.') {
            advance();
            if (!(peek() >= '0' && peek() <= '9')) {
                err = make_error("invalid number: missing fraction digits");
                return false;
            }
            while (peek() >= '0' && peek() <= '9') advance();
        }

        if (peek() == 'e' || peek() == 'E') {
            advance();
            if (peek() == '+' || peek() == '-') advance();
            if (!(peek() >= '0' && peek() <= '9')) {
                err = make_error("invalid number: missing exponent digits");
                return false;
            }
            while (peek() >= '0' && peek() <= '9') advance();
        }

        const std::string lit = text_.substr(start, pos_ - start);
        char*       end_ptr  = nullptr;
        const double v       = std::strtod(lit.c_str(), &end_ptr);
        if (end_ptr != lit.c_str() + lit.size()) {
            err = make_error("invalid number literal: " + lit);
            return false;
        }
        out.n = v;
        return true;
    }
};

// --- Schema mapper helpers ------------------------------------------

// Convert a JSON Number to int with range / integer-ness checks.
// Returns false (and populates `err`) if the value is not an integer
// in [INT_MIN, INT_MAX].
bool to_int(const JsonValue& v, int& out, std::string& err,
            const char* field) {
    if (v.kind != JsonKind::Number) {
        err = std::string("field '") + field + "' must be an integer";
        return false;
    }
    const double n = v.n;
    if (n < -2147483648.0 || n > 2147483647.0) {
        err = std::string("field '") + field + "' out of int range";
        return false;
    }
    const auto rounded = static_cast<int>(n);
    if (static_cast<double>(rounded) != n) {
        err = std::string("field '") + field
            + "' must be an integer (got fractional value)";
        return false;
    }
    out = rounded;
    return true;
}

bool to_string(const JsonValue& v, std::string& out, std::string& err,
               const char* field) {
    if (v.kind != JsonKind::String) {
        err = std::string("field '") + field + "' must be a string";
        return false;
    }
    out = v.s;
    return true;
}

bool to_bool(const JsonValue& v, bool& out, std::string& err,
             const char* field) {
    if (v.kind != JsonKind::Bool) {
        err = std::string("field '") + field + "' must be a boolean";
        return false;
    }
    out = v.b;
    return true;
}

// Convert a JSON Number to float. Numbers that round-trip via double
// are accepted; non-finite values (NaN / inf cannot legally appear
// in JSON, but we double-check after the cast) are rejected.
bool to_float(const JsonValue& v, float& out, std::string& err,
              const char* field) {
    if (v.kind != JsonKind::Number) {
        err = std::string("field '") + field + "' must be a number";
        return false;
    }
    const double n = v.n;
    if (!std::isfinite(n)) {
        err = std::string("field '") + field + "' must be finite";
        return false;
    }
    out = static_cast<float>(n);
    return true;
}

// Read a length-3 JSON array of finite numbers into a Vec3. The
// `field` label is used for error diagnostics ("camera.position",
// etc.) and is included in the error message verbatim.
bool to_vec3(const JsonValue& v, rr::math::Vec3& out, std::string& err,
             const char* field) {
    if (v.kind != JsonKind::Array) {
        err = std::string("field '") + field
            + "' must be a JSON array of 3 numbers";
        return false;
    }
    if (v.arr.size() != 3) {
        err = std::string("field '") + field
            + "' must have exactly 3 elements (got "
            + std::to_string(v.arr.size()) + ")";
        return false;
    }
    float xyz[3] = {0.0f, 0.0f, 0.0f};
    const char* labels[3] = { "[0]", "[1]", "[2]" };
    for (int i = 0; i < 3; ++i) {
        const std::string elem_field = std::string(field) + labels[i];
        if (!to_float(v.arr[i], xyz[i], err, elem_field.c_str())) {
            return false;
        }
    }
    out = rr::math::Vec3{xyz[0], xyz[1], xyz[2]};
    return true;
}

// Apply an entry from the `render_settings` block onto `rs`.
// Accepts the canonical spec names (`width`, `height`,
// `samples_per_pixel`, `max_depth`) and the user-shorthand aliases
// (`samples`, `output`, `output_path`). Returns false on validation
// failure and populates `err`.
bool apply_render_settings(const JsonValue& obj,
                           rr::scene::RenderSettings& rs,
                           std::string& err) {
    if (obj.kind != JsonKind::Object) {
        err = "'render_settings' must be a JSON object";
        return false;
    }

    if (const auto* v = obj.find("width")) {
        if (!to_int(*v, rs.width, err, "render_settings.width")) return false;
        if (rs.width <= 0) {
            err = "render_settings.width must be > 0";
            return false;
        }
    }
    if (const auto* v = obj.find("height")) {
        if (!to_int(*v, rs.height, err, "render_settings.height")) return false;
        if (rs.height <= 0) {
            err = "render_settings.height must be > 0";
            return false;
        }
    }
    if (const auto* v = obj.find_or("samples_per_pixel", "samples")) {
        if (!to_int(*v, rs.samples_per_pixel, err,
                    "render_settings.samples_per_pixel")) return false;
        if (rs.samples_per_pixel < 1) {
            err = "render_settings.samples_per_pixel must be >= 1";
            return false;
        }
    }
    if (const auto* v = obj.find("max_depth")) {
        if (!to_int(*v, rs.max_depth, err,
                    "render_settings.max_depth")) return false;
        if (rs.max_depth < 1) {
            err = "render_settings.max_depth must be >= 1";
            return false;
        }
    }
    if (const auto* v = obj.find_or("output_path", "output")) {
        if (!to_string(*v, rs.output_path, err,
                       "render_settings.output_path")) return false;
    }
    return true;
}

// Apply the `camera` block onto `cam`. Stage 10B.3 surface: reads
// `position`, an orientation pair (`forward` direction OR `target`
// look-at point - canonical spec form), `up`, and the vertical FOV
// in degrees (`fov_degrees` canonical, `fovDegrees` accepted as
// authoring shorthand). Aspect is set from `rs.width / rs.height`
// so the camera basis is consistent with the resolution authored
// in the same file (per RRSCENE_FORMAT.md §5).
//
// Stage 10B.3 explicitly does NOT read `near` / `far`; the Camera
// keeps its default clip range. That mapping joins in a follow-up
// sub-stage when the corresponding render features need them.
bool apply_camera(const JsonValue& obj,
                  const rr::scene::RenderSettings& rs,
                  rr::camera::Camera& cam,
                  std::string& err) {
    if (obj.kind != JsonKind::Object) {
        err = "'camera' must be a JSON object";
        return false;
    }

    rr::math::Vec3 position{0.0f, 0.0f, 0.0f};
    rr::math::Vec3 up_hint{0.0f, 1.0f, 0.0f};
    rr::math::Vec3 target{0.0f, 0.0f, -1.0f};

    bool has_position = false;
    if (const auto* v = obj.find("position")) {
        if (!to_vec3(*v, position, err, "camera.position")) return false;
        has_position = true;
    } else {
        position = cam.position();
    }

    if (const auto* v = obj.find("up")) {
        if (!to_vec3(*v, up_hint, err, "camera.up")) return false;
    } else {
        up_hint = cam.up();
    }

    // Orientation: prefer `forward` (the user-shorthand authoring
    // style; a direction vector relative to `position`) and fall
    // back to `target` (the canonical spec form; a world-space
    // look-at point). If neither is given, retain the camera's
    // existing orientation by deriving target from position +
    // current forward.
    if (const auto* v = obj.find("forward")) {
        rr::math::Vec3 fwd{0.0f, 0.0f, -1.0f};
        if (!to_vec3(*v, fwd, err, "camera.forward")) return false;
        if (fwd.x == 0.0f && fwd.y == 0.0f && fwd.z == 0.0f) {
            err = "camera.forward must be a non-zero vector";
            return false;
        }
        target = position + fwd;
    } else if (const auto* tv = obj.find("target")) {
        if (!to_vec3(*tv, target, err, "camera.target")) return false;
        if (target.x == position.x
         && target.y == position.y
         && target.z == position.z) {
            err = "camera.target must differ from camera.position";
            return false;
        }
    } else if (has_position) {
        // Position changed but neither `forward` nor `target` was
        // authored: keep the camera's existing forward direction by
        // projecting it forward from the new position.
        target = position + cam.forward();
    } else {
        target = position + cam.forward();
    }

    cam.look_at(position, target, up_hint);

    if (const auto* v = obj.find_or("fov_degrees", "fovDegrees")) {
        float fov = 0.0f;
        if (!to_float(*v, fov, err, "camera.fov_degrees")) return false;
        if (!(fov > 0.01f && fov < 180.0f)) {
            err = "camera.fov_degrees must be in (0.01, 180)";
            return false;
        }
        cam.set_vertical_fov_degrees(fov);
    }

    // Aspect derives from the resolution authored in the same file
    // (see RRSCENE_FORMAT.md §5). RenderSettings has been validated
    // before us; both width and height are > 0.
    cam.set_aspect(static_cast<float>(rs.width)
                 / static_cast<float>(rs.height));
    return true;
}

// Apply the `relativity` block onto `observer` + `params`. Stage
// 10B.4 surface accepts both the canonical spec form (§6 of
// RRSCENE_FORMAT.md) and the user-shorthand authoring style:
//
//   canonical                   shorthand
//   --------                    ---------
//   observer_velocity (Vec3)    betaVelocity (float, scalar speed)
//                             + velocityDirection (Vec3, direction)
//   enable_aberration (bool)    aberrationStrength (float; 0 == off)
//   doppler_color_strength      dopplerStrength
//   searchlight_strength        searchlightStrength
//   (no canonical equivalent)   enabled (master gate; false zeroes
//                                        all three enable_* flags)
//
// Shorthands win when both forms are present, matching the
// 10B.3 precedence policy. The cross-section validation rule from
// §12 #2 (`length(observer_velocity) < max_beta < 1`) is enforced.
//
// Note: the host `RelativityParams` has no float aberration
// strength field today (the kernel reads `enable_aberration` as a
// boolean), so `aberrationStrength` is mapped onto the boolean as
// a 0-or-non-zero gate. When a future stage adds a real
// `aberration_strength` field this mapper grows a single line; the
// shorthand contract is documented in RRSCENE_FORMAT.md §6.1.
bool apply_relativity(const JsonValue& obj,
                      rr::relativity::Observer& observer,
                      rr::relativity::RelativityParams& params,
                      std::string& err) {
    if (obj.kind != JsonKind::Object) {
        err = "'relativity' must be a JSON object";
        return false;
    }

    // ----- canonical fields first -----

    if (const auto* v = obj.find("observer_velocity")) {
        if (!to_vec3(*v, observer.velocity, err,
                     "relativity.observer_velocity")) return false;
    }
    if (const auto* v = obj.find("enable_aberration")) {
        if (!to_bool(*v, params.enable_aberration, err,
                     "relativity.enable_aberration")) return false;
    }
    if (const auto* v = obj.find("enable_doppler")) {
        if (!to_bool(*v, params.enable_doppler, err,
                     "relativity.enable_doppler")) return false;
    }
    if (const auto* v = obj.find("enable_searchlight")) {
        if (!to_bool(*v, params.enable_searchlight, err,
                     "relativity.enable_searchlight")) return false;
    }
    if (const auto* v = obj.find("doppler_color_strength")) {
        if (!to_float(*v, params.doppler_color_strength, err,
                      "relativity.doppler_color_strength")) return false;
        if (params.doppler_color_strength < 0.0f) {
            err = "relativity.doppler_color_strength must be >= 0";
            return false;
        }
    }
    if (const auto* v = obj.find("searchlight_strength")) {
        if (!to_float(*v, params.searchlight_strength, err,
                      "relativity.searchlight_strength")) return false;
        if (params.searchlight_strength < 0.0f) {
            err = "relativity.searchlight_strength must be >= 0";
            return false;
        }
    }
    if (const auto* v = obj.find("max_beta")) {
        if (!to_float(*v, params.max_beta, err,
                      "relativity.max_beta")) return false;
        if (!(params.max_beta > 0.0f && params.max_beta < 1.0f)) {
            err = "relativity.max_beta must satisfy 0 < max_beta < 1";
            return false;
        }
    }

    // ----- shorthands (override canonical when present) -----

    // betaVelocity + velocityDirection -> observer.velocity. Both
    // must be authored together; one without the other is
    // ambiguous (no implicit "default direction" - the spec is
    // silent and we'd rather reject than guess).
    const JsonValue* beta_v = obj.find("betaVelocity");
    const JsonValue* dir_v  = obj.find("velocityDirection");
    if (beta_v != nullptr || dir_v != nullptr) {
        if (beta_v == nullptr || dir_v == nullptr) {
            err = "relativity.betaVelocity and relativity."
                  "velocityDirection must be authored together";
            return false;
        }
        float beta = 0.0f;
        if (!to_float(*beta_v, beta, err,
                      "relativity.betaVelocity")) return false;
        if (beta < 0.0f) {
            err = "relativity.betaVelocity must be >= 0";
            return false;
        }
        rr::math::Vec3 dir{0.0f, 0.0f, 0.0f};
        if (!to_vec3(*dir_v, dir, err,
                     "relativity.velocityDirection")) return false;
        const float dlen2 = dir.x*dir.x + dir.y*dir.y + dir.z*dir.z;
        if (dlen2 == 0.0f) {
            err = "relativity.velocityDirection must be a non-zero vector";
            return false;
        }
        const float inv_len = 1.0f / std::sqrt(dlen2);
        observer.velocity = rr::math::Vec3{dir.x * inv_len * beta,
                                           dir.y * inv_len * beta,
                                           dir.z * inv_len * beta};
    }

    if (const auto* v = obj.find("dopplerStrength")) {
        if (!to_float(*v, params.doppler_color_strength, err,
                      "relativity.dopplerStrength")) return false;
        if (params.doppler_color_strength < 0.0f) {
            err = "relativity.dopplerStrength must be >= 0";
            return false;
        }
    }
    if (const auto* v = obj.find("searchlightStrength")) {
        if (!to_float(*v, params.searchlight_strength, err,
                      "relativity.searchlightStrength")) return false;
        if (params.searchlight_strength < 0.0f) {
            err = "relativity.searchlightStrength must be >= 0";
            return false;
        }
    }
    if (const auto* v = obj.find("aberrationStrength")) {
        // No host-side float home today; collapse to the boolean
        // gate that the kernel actually reads. Any positive value
        // enables aberration; zero disables it.
        float strength = 0.0f;
        if (!to_float(*v, strength, err,
                      "relativity.aberrationStrength")) return false;
        if (strength < 0.0f) {
            err = "relativity.aberrationStrength must be >= 0";
            return false;
        }
        params.enable_aberration = (strength > 0.0f);
    }

    // Master gate. When false, all three per-effect enable flags
    // are forced off. When true (default), no-op - per-effect
    // flags retain whatever the canonical / shorthand inputs set.
    if (const auto* v = obj.find("enabled")) {
        bool enabled = true;
        if (!to_bool(*v, enabled, err,
                     "relativity.enabled")) return false;
        if (!enabled) {
            params.enable_aberration  = false;
            params.enable_doppler     = false;
            params.enable_searchlight = false;
        }
    }

    // ----- cross-section validation (RRSCENE_FORMAT.md §12 #2) -----

    {
        const auto& v = observer.velocity;
        const float speed2 = v.x*v.x + v.y*v.y + v.z*v.z;
        if (speed2 >= 1.0f) {
            err = "relativity.observer_velocity has |v| >= 1 "
                  "(must be < 1 in c-units)";
            return false;
        }
        if (speed2 >= params.max_beta * params.max_beta) {
            err = "relativity.observer_velocity has |v| >= max_beta "
                  "(must be strictly less than max_beta)";
            return false;
        }
    }

    return true;
}

// Apply a single materials-array entry onto `out`. Validates id /
// per-channel ranges, accepts both canonical snake_case and the
// `MaterialParams` C++-camelCase aliases (`baseColor`,
// `emissionColor`, `emissionStrength`). The canonical form per
// RRSCENE_FORMAT.md §7 is snake_case; camelCase is accepted as an
// authoring shorthand because it matches the C++ field names
// directly. Stage 10B.5 explicitly skips `transmission`; it
// remains at the MaterialParams default until a follow-up stage.
bool apply_material(const JsonValue& obj,
                    rr::scene::SceneMaterial& out,
                    std::string& err,
                    const char* entry_label) {
    if (obj.kind != JsonKind::Object) {
        err = std::string(entry_label) + " must be a JSON object";
        return false;
    }

    // id (required, non-negative).
    const auto* id_v = obj.find("id");
    if (id_v == nullptr) {
        err = std::string(entry_label) + " is missing required 'id'";
        return false;
    }
    if (id_v->kind != JsonKind::Number) {
        err = std::string(entry_label) + ".id must be an integer";
        return false;
    }
    const double id_d = id_v->n;
    if (!std::isfinite(id_d) || id_d != std::floor(id_d)
     || id_d < 0.0
     || id_d > static_cast<double>(std::numeric_limits<int>::max())) {
        err = std::string(entry_label) + ".id must be a non-negative integer";
        return false;
    }
    out.id = static_cast<int>(id_d);

    // name (optional, default "").
    if (const auto* v = obj.find("name")) {
        if (!to_string(*v, out.name, err,
                       (std::string(entry_label) + ".name").c_str())) {
            return false;
        }
    }

    auto& p = out.params;

    // base_color / baseColor (Vec3, each component >= 0).
    if (const auto* v = obj.find_or("base_color", "baseColor")) {
        rr::math::Vec3 c{0.8f, 0.8f, 0.8f};
        const std::string field = std::string(entry_label) + ".base_color";
        if (!to_vec3(*v, c, err, field.c_str())) return false;
        if (c.x < 0.0f || c.y < 0.0f || c.z < 0.0f) {
            err = field + " components must be >= 0";
            return false;
        }
        p.baseColor = c;
    }

    // emission_color / emissionColor (Vec3, each component >= 0).
    if (const auto* v = obj.find_or("emission_color", "emissionColor")) {
        rr::math::Vec3 c{0.0f, 0.0f, 0.0f};
        const std::string field = std::string(entry_label) + ".emission_color";
        if (!to_vec3(*v, c, err, field.c_str())) return false;
        if (c.x < 0.0f || c.y < 0.0f || c.z < 0.0f) {
            err = field + " components must be >= 0";
            return false;
        }
        p.emissionColor = c;
    }

    // emission_strength / emissionStrength (float, >= 0).
    if (const auto* v = obj.find_or("emission_strength", "emissionStrength")) {
        const std::string field =
            std::string(entry_label) + ".emission_strength";
        if (!to_float(*v, p.emissionStrength, err, field.c_str())) return false;
        if (p.emissionStrength < 0.0f) {
            err = field + " must be >= 0";
            return false;
        }
    }

    // roughness / metallic / specular - each a float in [0, 1].
    auto read_unit_scalar = [&](const char* key, float& dst) -> bool {
        if (const auto* v = obj.find(key)) {
            const std::string field = std::string(entry_label) + "." + key;
            if (!to_float(*v, dst, err, field.c_str())) return false;
            if (!(dst >= 0.0f && dst <= 1.0f)) {
                err = field + " must be in [0, 1]";
                return false;
            }
        }
        return true;
    };
    if (!read_unit_scalar("roughness", p.roughness)) return false;
    if (!read_unit_scalar("metallic",  p.metallic))  return false;
    if (!read_unit_scalar("specular",  p.specular))  return false;

    return true;
}

// Apply the `materials` JSON array onto `scene.materials`. Stage
// 10B.5 surface: id / name / baseColor / emissionColor /
// emissionStrength / roughness / metallic / specular. Enforces the
// §12 #3 unique-id rule. `transmission` is intentionally not read
// here - that placeholder field on MaterialParams gets a mapper
// when its consuming BSDF stage lands.
bool apply_materials(const JsonValue& arr,
                     std::vector<rr::scene::SceneMaterial>& out,
                     std::string& err) {
    if (arr.kind != JsonKind::Array) {
        err = "'materials' must be a JSON array";
        return false;
    }
    out.clear();
    out.reserve(arr.arr.size());
    std::unordered_map<int, std::size_t> seen_ids;

    for (std::size_t i = 0; i < arr.arr.size(); ++i) {
        const std::string label = "materials[" + std::to_string(i) + "]";
        rr::scene::SceneMaterial m;
        if (!apply_material(arr.arr[i], m, err, label.c_str())) return false;

        const auto inserted = seen_ids.emplace(m.id, i);
        if (!inserted.second) {
            err = label + ".id (" + std::to_string(m.id)
                + ") collides with materials["
                + std::to_string(inserted.first->second) + "].id";
            return false;
        }
        out.push_back(std::move(m));
    }
    return true;
}

// Apply a single spheres-array entry onto `out`. Validates radius
// and material_index, populates `object.name` and the geometry POD
// fields. Stage 10B.6 surface: name, center, radius, and the
// material reference (`material_index` snake_case canonical or
// `materialId` camelCase shorthand). `visible` and `transform`
// are spec-§8 fields but the prompt scope explicitly excluded
// them; they remain at their `SceneObject` defaults until a
// follow-up sub-stage rounds them out.
bool apply_sphere(const JsonValue& obj,
                  std::size_t materials_count,
                  rr::scene::SceneSphere& out,
                  std::string& err,
                  const char* entry_label) {
    if (obj.kind != JsonKind::Object) {
        err = std::string(entry_label) + " must be a JSON object";
        return false;
    }

    // name (optional, default "").
    if (const auto* v = obj.find("name")) {
        if (!to_string(*v, out.object.name, err,
                       (std::string(entry_label) + ".name").c_str())) {
            return false;
        }
    }

    // center (required, finite Vec3).
    const auto* c_v = obj.find("center");
    if (c_v == nullptr) {
        err = std::string(entry_label) + " is missing required 'center'";
        return false;
    }
    if (!to_vec3(*c_v, out.geometry.center, err,
                 (std::string(entry_label) + ".center").c_str())) {
        return false;
    }

    // radius (required, finite positive float).
    const auto* r_v = obj.find("radius");
    if (r_v == nullptr) {
        err = std::string(entry_label) + " is missing required 'radius'";
        return false;
    }
    if (!to_float(*r_v, out.geometry.radius, err,
                  (std::string(entry_label) + ".radius").c_str())) {
        return false;
    }
    if (!(out.geometry.radius > 0.0f)) {        // §12 #9
        err = std::string(entry_label) + ".radius must be > 0";
        return false;
    }

    // material_index / materialId (optional integer; -1 or in
    // [0, materials_count)).
    if (const auto* v = obj.find_or("material_index", "materialId")) {
        if (v->kind != JsonKind::Number) {
            err = std::string(entry_label)
                + ".material_index must be an integer";
            return false;
        }
        const double idx_d = v->n;
        if (!std::isfinite(idx_d) || idx_d != std::floor(idx_d)) {
            err = std::string(entry_label)
                + ".material_index must be an integer";
            return false;
        }
        const long long idx_ll = static_cast<long long>(idx_d);
        if (idx_ll < -1
         || idx_ll > static_cast<long long>(std::numeric_limits<int>::max())) {
            err = std::string(entry_label)
                + ".material_index must fit in an int and be >= -1";
            return false;
        }
        const int idx = static_cast<int>(idx_ll);
        if (idx >= 0
         && static_cast<std::size_t>(idx) >= materials_count) {
            // §12 #4: per-file we treat this as "reject file" so
            // misauthored references surface immediately rather
            // than silently rendering with the neutral default.
            err = std::string(entry_label) + ".material_index ("
                + std::to_string(idx)
                + ") is out of range [0, " + std::to_string(materials_count)
                + ")";
            return false;
        }
        out.geometry.material_index = idx;
    }
    return true;
}

// Apply the `spheres` JSON array onto `scene.spheres`. Stage
// 10B.6 surface: each entry's name / center / radius /
// material_index (or materialId shorthand). Per-entry validation
// runs through `apply_sphere`; the array driver only owns the
// container reset + reserve and the labelling.
bool apply_spheres(const JsonValue& arr,
                   std::size_t materials_count,
                   std::vector<rr::scene::SceneSphere>& out,
                   std::string& err) {
    if (arr.kind != JsonKind::Array) {
        err = "'spheres' must be a JSON array";
        return false;
    }
    out.clear();
    out.reserve(arr.arr.size());
    for (std::size_t i = 0; i < arr.arr.size(); ++i) {
        const std::string label = "spheres[" + std::to_string(i) + "]";
        rr::scene::SceneSphere s;
        if (!apply_sphere(arr.arr[i], materials_count, s, err,
                          label.c_str())) {
            return false;
        }
        out.push_back(std::move(s));
    }
    return true;
}

// Slurp `path` into a string; returns false on read failure.
bool read_text_file(const std::string& path, std::string& out,
                    std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "could not open scene file: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) {
        err = "I/O error reading scene file: " + path;
        return false;
    }
    out = ss.str();
    return true;
}

}  // namespace

// =====================================================================
// Public API
// =====================================================================

bool sceneFileExists(const std::string& path) {
    std::error_code ec;
    const std::filesystem::path p(path);

    // `exists` follows symlinks. We require a regular file underneath,
    // not a directory or special file. Any filesystem error reported
    // through `ec` collapses to "does not exist as a usable scene
    // file" - the caller can re-attempt the open and surface a more
    // specific error from the OS at that point if needed.
    if (!std::filesystem::exists(p, ec) || ec) return false;
    if (!std::filesystem::is_regular_file(p, ec) || ec) return false;
    return true;
}

LoadResult parse(const std::string& text) {
    LoadResult result;

    JsonValue  root;
    ParseError pe;
    Parser     parser(text);
    if (!parser.parse_document(root, pe)) {
        result.error_message = "JSON parse error: " + pe.message;
        result.error_line    = pe.line;
        result.error_column  = pe.column;
        return result;
    }

    if (root.kind != JsonKind::Object) {
        result.error_message =
            "top-level value must be a JSON object (got " +
            std::string(root.kind == JsonKind::Array  ? "array"  :
                        root.kind == JsonKind::String ? "string" :
                        root.kind == JsonKind::Number ? "number" :
                        root.kind == JsonKind::Bool   ? "bool"   :
                                                        "null")
            + ")";
        return result;
    }

    // version (required)
    const JsonValue* version_v = root.find("version");
    if (version_v == nullptr) {
        result.error_message = "missing required top-level 'version' field";
        return result;
    }
    if (version_v->kind != JsonKind::String) {
        result.error_message = "'version' must be a string";
        return result;
    }
    result.version = version_v->s;

    // Major-version gate. v1 parser accepts only "1.x.y". We compare
    // by the leading numeric run before the first '.' rather than
    // doing full SemVer parsing - the scene format is closed and
    // tracks one major version at a time.
    {
        const std::string& vs = result.version;
        std::string major_str;
        std::size_t i = 0;
        while (i < vs.size() && vs[i] >= '0' && vs[i] <= '9') {
            major_str.push_back(vs[i]);
            ++i;
        }
        if (major_str.empty() || major_str != "1") {
            result.error_message =
                "unsupported scene version '" + vs +
                "' (this build accepts major version 1)";
            return result;
        }
    }

    // render_settings (optional). Accept `render` as an alias for
    // the user-shorthand authoring style.
    if (const JsonValue* rs_v = root.find_or("render_settings", "render")) {
        std::string apply_err;
        if (!apply_render_settings(*rs_v, result.scene.render_settings,
                                   apply_err)) {
            result.error_message = apply_err;
            return result;
        }
    }

    // camera (optional). Stage 10B.3 surface: position / forward
    // (or target) / up / fov. Aspect derives from the
    // already-validated render_settings.
    if (const JsonValue* cam_v = root.find("camera")) {
        std::string apply_err;
        if (!apply_camera(*cam_v, result.scene.render_settings,
                          result.scene.camera, apply_err)) {
            result.error_message = apply_err;
            return result;
        }
    }

    // relativity (optional). Stage 10B.4 surface: observer
    // velocity (canonical Vec3 OR betaVelocity + velocityDirection
    // shorthand) + per-effect enables/strengths + master `enabled`
    // gate + max_beta with the §12 #2 cross-section validation.
    if (const JsonValue* rel_v = root.find("relativity")) {
        std::string apply_err;
        if (!apply_relativity(*rel_v, result.scene.observer,
                              result.scene.relativity, apply_err)) {
            result.error_message = apply_err;
            return result;
        }
    }

    // materials (optional). Stage 10B.5 surface: id, name,
    // base_color / emission_color / emission_strength (snake_case
    // canonical or camelCase shorthand) plus roughness / metallic /
    // specular. Enforces §12 #3 unique-id. `transmission` is
    // skipped until its consuming BSDF stage lands.
    if (const JsonValue* mats_v = root.find("materials")) {
        std::string apply_err;
        if (!apply_materials(*mats_v, result.scene.materials, apply_err)) {
            result.error_message = apply_err;
            return result;
        }
    }

    // spheres (optional). Stage 10B.6 surface: name, center,
    // radius, material_index (or materialId shorthand). The
    // `materials_count` argument lets the per-entry validator
    // enforce §12 #4 (material_index in range or -1) without
    // re-reaching into the scene container.
    if (const JsonValue* sph_v = root.find("spheres")) {
        std::string apply_err;
        if (!apply_spheres(*sph_v,
                           result.scene.materials.size(),
                           result.scene.spheres, apply_err)) {
            result.error_message = apply_err;
            return result;
        }
    }

    // Stage 10B.6 schema scope ends here. Other top-level keys
    // (`meshes`, `lights`) are intentionally ignored - they were
    // syntax-checked by the JSON parser pass and will be mapped
    // onto `scene` in follow-up sub-stages.

    result.ok = true;
    return result;
}

LoadResult load(const std::string& path) {
    LoadResult result;

    if (!sceneFileExists(path)) {
        result.error_message = "scene file does not exist: " + path;
        return result;
    }

    std::string text;
    std::string read_err;
    if (!read_text_file(path, text, read_err)) {
        result.error_message = read_err;
        return result;
    }

    return parse(text);
}

}  // namespace rr::io
