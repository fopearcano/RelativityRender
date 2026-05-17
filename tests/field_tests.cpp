// FIELD-I.2 + FIELD-I.4 — host-side tests for the tagged-form
// scalar-field config POD landed at `src/field/ScalarField.h`
// (FIELD-I.2) and the tagged-form mapping config POD landed at
// `src/field/FieldMapping.h` (FIELD-I.4). Verifies the
// `ScalarFieldKind` + `ScalarFieldConfig` + `evaluate(...)` +
// `disabled_scalar_field_config()` helpers (FIELD-I.2) and the
// `FieldMappingTarget` + `FieldMappingConfig` +
// `evaluate_mapping(...)` + `disabled_field_mapping_config()`
// helpers (FIELD-I.4) behave per the design doc + the
// FIELD-I.1 plan.
//
// No test framework in the project — same hand-rolled `RR_CHECK`
// idiom as `tests/relativity_tests.cpp` and
// `tests/manifold_identity_tests.cpp`. Counts assertions via
// `g_total` / `g_failed`; `main()` returns 0 iff all PASS.
//
// Linkage: this binary links `rr_field` (the INTERFACE library
// that pulls in `rr_math`). No CUDA / OptiX dependency; runs on
// the audit host.

#include "field/FieldMapping.h"
#include "field/ScalarField.h"
#include "math/Vec3.h"
#include "math/Vec4.h"

#include <cmath>
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

constexpr float kEps = 1.0e-5f;

bool approx(float a, float b, float eps = kEps) {
    const float d = a - b;
    return (d < 0.0f ? -d : d) <= eps;
}

// ---------- 1. Enum + factory + default-POD anchors ----------

void test_scalar_field_kind_enumerators_distinct() {
    using rr::field::ScalarFieldKind;
    // Defence-in-depth against an accidental enumerator
    // collision in a future edit (mirrors the OBSERVER.2
    // `test_observer_2_perception_mode_default` precedent).
    RR_CHECK(ScalarFieldKind::Constant !=
             ScalarFieldKind::Radial);
    RR_CHECK(ScalarFieldKind::Constant !=
             ScalarFieldKind::ProceduralPlaceholder);
    RR_CHECK(ScalarFieldKind::Radial !=
             ScalarFieldKind::ProceduralPlaceholder);
    // `Constant = 0` is the explicit anchor (verified by
    // cast — guarantees default-constructed `ScalarFieldConfig`
    // carries `kind = Constant`).
    RR_CHECK(static_cast<unsigned>(ScalarFieldKind::Constant) == 0u);
}

void test_disabled_scalar_field_config_factory() {
    using rr::field::ScalarFieldConfig;
    using rr::field::ScalarFieldKind;
    using rr::field::disabled_scalar_field_config;

    const ScalarFieldConfig c = disabled_scalar_field_config();
    RR_CHECK(c.enabled        == false);
    RR_CHECK(c.strength       == 0.0f);
    RR_CHECK(c.kind           == ScalarFieldKind::Constant);
    RR_CHECK(c.center.x       == 0.0f);
    RR_CHECK(c.center.y       == 0.0f);
    RR_CHECK(c.center.z       == 0.0f);
    RR_CHECK(c.min_radius     == 0.0f);
    RR_CHECK(c.max_radius     == 1.0f);
    RR_CHECK(c.falloff        == 1.0f);
    RR_CHECK(c.min_value      == 0.0f);
    RR_CHECK(c.max_value      == 1.0f);
    RR_CHECK(c.constant_value == 0.0f);
}

void test_default_scalar_field_config_default_constructed() {
    // Default-constructed `ScalarFieldConfig{}` must match
    // the factory's output byte-for-byte (the factory just
    // returns the default).
    using rr::field::ScalarFieldConfig;
    using rr::field::ScalarFieldKind;
    using rr::field::disabled_scalar_field_config;

    const ScalarFieldConfig direct{};
    const ScalarFieldConfig factory = disabled_scalar_field_config();
    RR_CHECK(direct.enabled        == factory.enabled);
    RR_CHECK(direct.strength       == factory.strength);
    RR_CHECK(direct.kind           == factory.kind);
    RR_CHECK(direct.center.x       == factory.center.x);
    RR_CHECK(direct.center.y       == factory.center.y);
    RR_CHECK(direct.center.z       == factory.center.z);
    RR_CHECK(direct.min_radius     == factory.min_radius);
    RR_CHECK(direct.max_radius     == factory.max_radius);
    RR_CHECK(direct.falloff        == factory.falloff);
    RR_CHECK(direct.min_value      == factory.min_value);
    RR_CHECK(direct.max_value      == factory.max_value);
    RR_CHECK(direct.constant_value == factory.constant_value);
}

// ---------- 2. Default disabled / no-op anchors ----------

void test_evaluate_disabled_returns_zero() {
    using rr::field::evaluate;
    using rr::field::ScalarFieldConfig;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // Default-constructed config: enabled = false, strength
    // = 0. Evaluator returns 0 at every test position
    // regardless of kind / parameter values.
    const ScalarFieldConfig c{};
    const Vec3 positions[] = {
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{1.0f, 2.0f, 3.0f},
        Vec3{-5.0f, 7.5f, -100.0f},
        Vec3{1.0e6f, 1.0e6f, 1.0e6f},
    };
    for (const Vec3& p : positions) {
        RR_CHECK(evaluate(c, p) == 0.0f);
        // Vec4 overload routes to the Vec3 evaluator on the
        // spatial part; same result for any time component.
        RR_CHECK(evaluate(c, Vec4{0.0f, p.x, p.y, p.z}) == 0.0f);
        RR_CHECK(evaluate(c, Vec4{42.0f, p.x, p.y, p.z}) == 0.0f);
    }
}

void test_evaluate_enabled_but_zero_strength_returns_zero() {
    using rr::field::evaluate;
    using rr::field::ScalarFieldConfig;
    using rr::field::ScalarFieldKind;
    using rr::math::Vec3;

    // Even with non-trivial parameters + enabled = true, a
    // zero `strength` short-circuits to 0. This is the
    // "wired but quiet" anchor from the FIELD-I.1 plan.
    ScalarFieldConfig c{};
    c.enabled        = true;
    c.strength       = 0.0f;          // <- the load-bearing zero
    c.kind           = ScalarFieldKind::Constant;
    c.constant_value = 42.0f;
    RR_CHECK(evaluate(c, Vec3{0.0f, 0.0f, 0.0f}) == 0.0f);
    RR_CHECK(evaluate(c, Vec3{1.5f, -2.3f, 4.7f}) == 0.0f);
}

// ---------- 3. Constant kind ----------

void test_constant_kind_returns_strength_times_value() {
    using rr::field::evaluate;
    using rr::field::ScalarFieldConfig;
    using rr::field::ScalarFieldKind;
    using rr::math::Vec3;

    ScalarFieldConfig c{};
    c.enabled        = true;
    c.strength       = 1.0f;
    c.kind           = ScalarFieldKind::Constant;
    c.constant_value = 0.75f;

    // Constant returns the same value at every spatial
    // position by construction (the position is ignored).
    RR_CHECK(approx(evaluate(c, Vec3{0.0f, 0.0f, 0.0f}), 0.75f));
    RR_CHECK(approx(evaluate(c, Vec3{1.5f, -2.3f, 4.7f}), 0.75f));
    RR_CHECK(approx(evaluate(c, Vec3{-100.0f, 100.0f, 0.0f}), 0.75f));
}

void test_constant_kind_strength_scales_output() {
    using rr::field::evaluate;
    using rr::field::ScalarFieldConfig;
    using rr::field::ScalarFieldKind;
    using rr::math::Vec3;

    ScalarFieldConfig c{};
    c.enabled        = true;
    c.kind           = ScalarFieldKind::Constant;
    c.constant_value = 1.0f;
    const Vec3 p{0.0f, 0.0f, 0.0f};

    c.strength = 1.0f;
    RR_CHECK(approx(evaluate(c, p), 1.0f));
    c.strength = 0.5f;
    RR_CHECK(approx(evaluate(c, p), 0.5f));
    c.strength = 2.0f;
    RR_CHECK(approx(evaluate(c, p), 2.0f));
    c.strength = -1.0f;
    RR_CHECK(approx(evaluate(c, p), -1.0f));
}

// ---------- 4. Radial kind ----------

void test_radial_kind_min_value_at_or_inside_min_radius() {
    using rr::field::evaluate;
    using rr::field::ScalarFieldConfig;
    using rr::field::ScalarFieldKind;
    using rr::math::Vec3;

    ScalarFieldConfig c{};
    c.enabled    = true;
    c.strength   = 1.0f;
    c.kind       = ScalarFieldKind::Radial;
    c.center     = Vec3{0.0f, 0.0f, 0.0f};
    c.min_radius = 1.0f;
    c.max_radius = 5.0f;
    c.min_value  = 0.2f;
    c.max_value  = 0.9f;
    c.falloff    = 1.0f;

    // At the center: distance = 0 < min_radius. Returns min_value.
    RR_CHECK(approx(evaluate(c, Vec3{0.0f, 0.0f, 0.0f}), 0.2f));
    // On the min_radius sphere: distance = min_radius. The
    // evaluator clamps to <= min2 → min_value.
    RR_CHECK(approx(evaluate(c, Vec3{1.0f, 0.0f, 0.0f}), 0.2f));
    // Inside min_radius (e.g. distance = 0.5).
    RR_CHECK(approx(evaluate(c, Vec3{0.5f, 0.0f, 0.0f}), 0.2f));
}

void test_radial_kind_max_value_at_or_outside_max_radius() {
    using rr::field::evaluate;
    using rr::field::ScalarFieldConfig;
    using rr::field::ScalarFieldKind;
    using rr::math::Vec3;

    ScalarFieldConfig c{};
    c.enabled    = true;
    c.strength   = 1.0f;
    c.kind       = ScalarFieldKind::Radial;
    c.min_radius = 1.0f;
    c.max_radius = 5.0f;
    c.min_value  = 0.2f;
    c.max_value  = 0.9f;
    c.falloff    = 1.0f;

    // On the max_radius sphere: distance == max_radius.
    // The evaluator clamps r2 >= max2 → max_value.
    RR_CHECK(approx(evaluate(c, Vec3{5.0f, 0.0f, 0.0f}), 0.9f));
    // Outside max_radius (e.g. distance = 10).
    RR_CHECK(approx(evaluate(c, Vec3{10.0f, 0.0f, 0.0f}), 0.9f));
    // Same distance but oblique direction.
    RR_CHECK(approx(evaluate(c, Vec3{6.0f, 8.0f, 0.0f}), 0.9f));
}

void test_radial_kind_smoothstep_midway() {
    using rr::field::evaluate;
    using rr::field::ScalarFieldConfig;
    using rr::field::ScalarFieldKind;
    using rr::math::Vec3;

    // Midway radial sample with falloff=1: smoothstep at
    // t=0.5 returns 0.5 (the cubic 3*0.25 - 2*0.125 = 0.5);
    // the output should be exactly lerp(0, 1, 0.5) = 0.5
    // times strength.
    ScalarFieldConfig c{};
    c.enabled    = true;
    c.strength   = 1.0f;
    c.kind       = ScalarFieldKind::Radial;
    c.min_radius = 0.0f;
    c.max_radius = 1.0f;
    c.min_value  = 0.0f;
    c.max_value  = 1.0f;
    c.falloff    = 1.0f;

    // distance = 0.5; t_lin = 0.5; t_pow = 0.5; smoothstep(0.5) = 0.5.
    RR_CHECK(approx(evaluate(c, Vec3{0.5f, 0.0f, 0.0f}), 0.5f));
    // Same midway distance on a different axis.
    RR_CHECK(approx(evaluate(c, Vec3{0.0f, 0.0f, 0.5f}), 0.5f));
}

void test_radial_kind_strength_scales_output() {
    using rr::field::evaluate;
    using rr::field::ScalarFieldConfig;
    using rr::field::ScalarFieldKind;
    using rr::math::Vec3;

    ScalarFieldConfig c{};
    c.enabled    = true;
    c.kind       = ScalarFieldKind::Radial;
    c.min_radius = 0.0f;
    c.max_radius = 1.0f;
    c.min_value  = 0.0f;
    c.max_value  = 1.0f;
    c.falloff    = 1.0f;
    const Vec3 mid{0.5f, 0.0f, 0.0f};  // smoothstep(0.5) = 0.5

    c.strength = 1.0f;
    RR_CHECK(approx(evaluate(c, mid), 0.5f));
    c.strength = 0.5f;
    RR_CHECK(approx(evaluate(c, mid), 0.25f));
    c.strength = 2.0f;
    RR_CHECK(approx(evaluate(c, mid), 1.0f));
}

void test_radial_kind_offset_center() {
    using rr::field::evaluate;
    using rr::field::ScalarFieldConfig;
    using rr::field::ScalarFieldKind;
    using rr::math::Vec3;

    // Center at (3, 4, 0); test position (3, 4, 0) is the
    // center → distance 0 → min_value.
    ScalarFieldConfig c{};
    c.enabled    = true;
    c.strength   = 1.0f;
    c.kind       = ScalarFieldKind::Radial;
    c.center     = Vec3{3.0f, 4.0f, 0.0f};
    c.min_radius = 0.0f;
    c.max_radius = 1.0f;
    c.min_value  = 0.1f;
    c.max_value  = 0.9f;
    c.falloff    = 1.0f;

    RR_CHECK(approx(evaluate(c, Vec3{3.0f, 4.0f, 0.0f}), 0.1f));
    // Position (3, 4, 0) + (0.5, 0, 0) → distance 0.5 →
    // smoothstep midway → lerp(0.1, 0.9, 0.5) = 0.5.
    RR_CHECK(approx(evaluate(c, Vec3{3.5f, 4.0f, 0.0f}), 0.5f));
    // Outside max_radius from the offset center.
    RR_CHECK(approx(evaluate(c, Vec3{10.0f, 4.0f, 0.0f}), 0.9f));
}

void test_radial_kind_degenerate_envelope_returns_zero() {
    using rr::field::evaluate;
    using rr::field::ScalarFieldConfig;
    using rr::field::ScalarFieldKind;
    using rr::math::Vec3;

    // Degenerate envelope: max_radius <= min_radius. The
    // evaluator defends in depth by returning 0.
    ScalarFieldConfig c{};
    c.enabled    = true;
    c.strength   = 1.0f;
    c.kind       = ScalarFieldKind::Radial;
    c.min_radius = 5.0f;
    c.max_radius = 1.0f;       // <- inverted
    c.min_value  = 0.2f;
    c.max_value  = 0.9f;

    RR_CHECK(evaluate(c, Vec3{0.0f, 0.0f, 0.0f}) == 0.0f);
    RR_CHECK(evaluate(c, Vec3{2.0f, 0.0f, 0.0f}) == 0.0f);
    RR_CHECK(evaluate(c, Vec3{100.0f, 0.0f, 0.0f}) == 0.0f);

    // Also degenerate: max_radius == min_radius.
    c.min_radius = 1.0f;
    c.max_radius = 1.0f;
    RR_CHECK(evaluate(c, Vec3{0.0f, 0.0f, 0.0f}) == 0.0f);
    RR_CHECK(evaluate(c, Vec3{2.0f, 0.0f, 0.0f}) == 0.0f);
}

void test_radial_kind_falloff_reshapes_transition() {
    using rr::field::evaluate;
    using rr::field::ScalarFieldConfig;
    using rr::field::ScalarFieldKind;
    using rr::math::Vec3;

    // With falloff = 2, the transition curve is steeper:
    // t_pow at radial midway becomes 0.25 (not 0.5), so the
    // smoothstep output is lower than the falloff=1 midway.
    ScalarFieldConfig c{};
    c.enabled    = true;
    c.strength   = 1.0f;
    c.kind       = ScalarFieldKind::Radial;
    c.min_radius = 0.0f;
    c.max_radius = 1.0f;
    c.min_value  = 0.0f;
    c.max_value  = 1.0f;
    c.falloff    = 2.0f;
    const Vec3 mid{0.5f, 0.0f, 0.0f};

    // smoothstep(0, 1, 0.25) = 3*0.0625 - 2*0.015625
    // = 0.1875 - 0.03125 = 0.15625.
    RR_CHECK(approx(evaluate(c, mid), 0.15625f));

    // With falloff <= 0, the evaluator falls back to
    // falloff=1 (defence-in-depth per the doc comment).
    c.falloff = 0.0f;
    RR_CHECK(approx(evaluate(c, mid), 0.5f));
    c.falloff = -1.0f;
    RR_CHECK(approx(evaluate(c, mid), 0.5f));
}

// ---------- 5. ProceduralPlaceholder kind ----------

void test_procedural_placeholder_returns_zero() {
    using rr::field::evaluate;
    using rr::field::ScalarFieldConfig;
    using rr::field::ScalarFieldKind;
    using rr::math::Vec3;

    // ProceduralPlaceholder is reserved-but-inert per
    // master rule #3. Returns 0 regardless of every other
    // field's value.
    ScalarFieldConfig c{};
    c.enabled        = true;
    c.strength       = 1.0f;
    c.kind           = ScalarFieldKind::ProceduralPlaceholder;
    c.center         = Vec3{1.0f, 2.0f, 3.0f};
    c.min_radius     = 0.5f;
    c.max_radius     = 2.0f;
    c.min_value      = 0.3f;
    c.max_value      = 0.7f;
    c.falloff        = 1.5f;
    c.constant_value = 42.0f;

    RR_CHECK(evaluate(c, Vec3{0.0f, 0.0f, 0.0f}) == 0.0f);
    RR_CHECK(evaluate(c, Vec3{1.0f, 2.0f, 3.0f}) == 0.0f);
    RR_CHECK(evaluate(c, Vec3{-100.0f, 5.0f, 0.5f}) == 0.0f);
}

// ---------- 6. Vec4 overload routes to spatial Vec3 ----------

void test_vec4_overload_consumes_spatial_part() {
    using rr::field::evaluate;
    using rr::field::ScalarFieldConfig;
    using rr::field::ScalarFieldKind;
    using rr::math::Vec3;
    using rr::math::Vec4;

    ScalarFieldConfig c{};
    c.enabled        = true;
    c.strength       = 1.0f;
    c.kind           = ScalarFieldKind::Constant;
    c.constant_value = 0.42f;

    // Vec4 = (time, x, y, z) per the project's spacetime
    // convention. The evaluator extracts (y, z, w) =
    // (x, y, z) and dispatches to the Vec3 overload.
    const float expected = evaluate(c, Vec3{1.5f, -2.3f, 4.7f});
    RR_CHECK(approx(expected, 0.42f));
    RR_CHECK(approx(evaluate(c, Vec4{0.0f, 1.5f, -2.3f, 4.7f}),
                    expected));
    // Time component is ignored for these spatial-only
    // kinds.
    RR_CHECK(approx(evaluate(c, Vec4{42.0f, 1.5f, -2.3f, 4.7f}),
                    expected));
    RR_CHECK(approx(evaluate(c, Vec4{-7.0f, 1.5f, -2.3f, 4.7f}),
                    expected));
}

void test_vec4_overload_radial_routes_through_spatial_part() {
    using rr::field::evaluate;
    using rr::field::ScalarFieldConfig;
    using rr::field::ScalarFieldKind;
    using rr::math::Vec3;
    using rr::math::Vec4;

    ScalarFieldConfig c{};
    c.enabled    = true;
    c.strength   = 1.0f;
    c.kind       = ScalarFieldKind::Radial;
    c.min_radius = 0.0f;
    c.max_radius = 1.0f;
    c.min_value  = 0.0f;
    c.max_value  = 1.0f;
    c.falloff    = 1.0f;
    // Vec4 (time, x=0.5, y=0, z=0) → spatial midway distance.
    RR_CHECK(approx(evaluate(c, Vec4{0.0f, 0.5f, 0.0f, 0.0f}),
                    0.5f));
    // Time variation does not affect the spatial result.
    RR_CHECK(approx(evaluate(c, Vec4{99.0f, 0.5f, 0.0f, 0.0f}),
                    0.5f));
}

// ---------- 7. FieldMappingTarget + FieldMappingConfig (FIELD-I.4) ----------

void test_field_mapping_target_enumerators_distinct() {
    using rr::field::FieldMappingTarget;
    // Defence-in-depth against an accidental enumerator
    // collision in a future edit (mirrors the
    // `test_scalar_field_kind_enumerators_distinct`
    // precedent above).
    RR_CHECK(FieldMappingTarget::None != FieldMappingTarget::ColorMultiplier);
    RR_CHECK(FieldMappingTarget::None != FieldMappingTarget::Emission);
    RR_CHECK(FieldMappingTarget::None != FieldMappingTarget::DiagnosticAOV);
    RR_CHECK(FieldMappingTarget::ColorMultiplier !=
             FieldMappingTarget::Emission);
    RR_CHECK(FieldMappingTarget::ColorMultiplier !=
             FieldMappingTarget::DiagnosticAOV);
    RR_CHECK(FieldMappingTarget::Emission !=
             FieldMappingTarget::DiagnosticAOV);
    // `None = 0` is the explicit anchor — guarantees the
    // default-constructed `FieldMappingConfig` carries
    // `target = None` so default authoring is a guaranteed
    // no-op.
    RR_CHECK(static_cast<unsigned>(FieldMappingTarget::None) == 0u);
}

void test_disabled_field_mapping_config_factory() {
    using rr::field::FieldMappingConfig;
    using rr::field::FieldMappingTarget;
    using rr::field::disabled_field_mapping_config;

    const FieldMappingConfig c = disabled_field_mapping_config();
    RR_CHECK(c.target       == FieldMappingTarget::None);
    RR_CHECK(c.strength     == 0.0f);
    RR_CHECK(c.bias         == 0.0f);
    RR_CHECK(c.min_value    == 0.0f);
    RR_CHECK(c.max_value    == 1.0f);
    RR_CHECK(c.clamp_output == false);
}

void test_default_field_mapping_config_default_constructed() {
    using rr::field::FieldMappingConfig;
    using rr::field::disabled_field_mapping_config;

    const FieldMappingConfig a = disabled_field_mapping_config();
    const FieldMappingConfig b{};
    // Factory output is byte-identical to the default-
    // constructed POD.
    RR_CHECK(a.target       == b.target);
    RR_CHECK(a.strength     == b.strength);
    RR_CHECK(a.bias         == b.bias);
    RR_CHECK(a.min_value    == b.min_value);
    RR_CHECK(a.max_value    == b.max_value);
    RR_CHECK(a.clamp_output == b.clamp_output);
}

void test_evaluate_mapping_default_target_none_returns_zero() {
    using rr::field::evaluate_mapping;
    using rr::field::FieldMappingConfig;
    // Default config (target = None) must short-circuit to 0
    // regardless of the sample value.
    const FieldMappingConfig c{};
    RR_CHECK(approx(evaluate_mapping(c, 0.0f),   0.0f));
    RR_CHECK(approx(evaluate_mapping(c, 1.0f),   0.0f));
    RR_CHECK(approx(evaluate_mapping(c, -1.0f),  0.0f));
    RR_CHECK(approx(evaluate_mapping(c, 1e6f),   0.0f));
}

void test_evaluate_mapping_target_none_short_circuits() {
    using rr::field::evaluate_mapping;
    using rr::field::FieldMappingConfig;
    using rr::field::FieldMappingTarget;
    // Even when every other parameter is dialled to non-
    // default values, `target = None` must short-circuit
    // (the no-op anchor is load-bearing).
    FieldMappingConfig c{};
    c.target       = FieldMappingTarget::None;
    c.strength     = 5.0f;
    c.bias         = 3.0f;
    c.min_value    = -10.0f;
    c.max_value    = 10.0f;
    c.clamp_output = true;
    RR_CHECK(approx(evaluate_mapping(c, 0.5f), 0.0f));
    RR_CHECK(approx(evaluate_mapping(c, 100.0f), 0.0f));
}

void test_evaluate_mapping_color_multiplier_no_clamp() {
    using rr::field::evaluate_mapping;
    using rr::field::FieldMappingConfig;
    using rr::field::FieldMappingTarget;
    FieldMappingConfig c{};
    c.target       = FieldMappingTarget::ColorMultiplier;
    c.strength     = 2.0f;
    c.bias         = 1.0f;
    c.clamp_output = false;
    // mapped = strength * sample + bias = 2 * sample + 1.
    RR_CHECK(approx(evaluate_mapping(c, 0.0f),   1.0f));
    RR_CHECK(approx(evaluate_mapping(c, 1.0f),   3.0f));
    RR_CHECK(approx(evaluate_mapping(c, 3.0f),   7.0f));
    RR_CHECK(approx(evaluate_mapping(c, -1.0f), -1.0f));
}

void test_evaluate_mapping_emission_target_routes() {
    using rr::field::evaluate_mapping;
    using rr::field::FieldMappingConfig;
    using rr::field::FieldMappingTarget;
    FieldMappingConfig c{};
    c.target   = FieldMappingTarget::Emission;
    c.strength = 1.0f;
    c.bias     = 0.0f;
    // Same evaluator path as ColorMultiplier — the target
    // selector determines which channel the renderer
    // writes to, not the math.
    RR_CHECK(approx(evaluate_mapping(c, 0.42f),  0.42f));
    RR_CHECK(approx(evaluate_mapping(c, -1.5f), -1.5f));
}

void test_evaluate_mapping_diagnostic_aov_target_routes() {
    using rr::field::evaluate_mapping;
    using rr::field::FieldMappingConfig;
    using rr::field::FieldMappingTarget;
    FieldMappingConfig c{};
    c.target   = FieldMappingTarget::DiagnosticAOV;
    c.strength = 0.5f;
    c.bias     = 0.25f;
    RR_CHECK(approx(evaluate_mapping(c, 0.0f),  0.25f));
    RR_CHECK(approx(evaluate_mapping(c, 1.0f),  0.75f));
    RR_CHECK(approx(evaluate_mapping(c, 2.0f),  1.25f));
}

void test_evaluate_mapping_zero_strength_returns_bias() {
    using rr::field::evaluate_mapping;
    using rr::field::FieldMappingConfig;
    using rr::field::FieldMappingTarget;
    // "Wired but quiet" anchor: target != None + strength = 0
    // collapses to just the bias term regardless of sample.
    FieldMappingConfig c{};
    c.target   = FieldMappingTarget::ColorMultiplier;
    c.strength = 0.0f;
    c.bias     = 0.7f;
    RR_CHECK(approx(evaluate_mapping(c, 0.0f),   0.7f));
    RR_CHECK(approx(evaluate_mapping(c, 1e6f),   0.7f));
    RR_CHECK(approx(evaluate_mapping(c, -1e6f),  0.7f));
}

void test_evaluate_mapping_bias_additive() {
    using rr::field::evaluate_mapping;
    using rr::field::FieldMappingConfig;
    using rr::field::FieldMappingTarget;
    // Bias is purely additive; no interaction with strength.
    FieldMappingConfig c{};
    c.target   = FieldMappingTarget::ColorMultiplier;
    c.strength = 1.0f;
    c.bias     = -0.5f;
    RR_CHECK(approx(evaluate_mapping(c, 0.5f),  0.0f));
    RR_CHECK(approx(evaluate_mapping(c, 1.5f),  1.0f));
    RR_CHECK(approx(evaluate_mapping(c, 0.0f), -0.5f));
}

void test_evaluate_mapping_clamp_output_disabled_passes_through() {
    using rr::field::evaluate_mapping;
    using rr::field::FieldMappingConfig;
    using rr::field::FieldMappingTarget;
    // clamp_output = false → strength + bias output passes
    // through unchanged, even when it lies outside
    // [min_value, max_value].
    FieldMappingConfig c{};
    c.target       = FieldMappingTarget::ColorMultiplier;
    c.strength     = 1.0f;
    c.bias         = 0.0f;
    c.min_value    = 0.0f;
    c.max_value    = 1.0f;
    c.clamp_output = false;
    RR_CHECK(approx(evaluate_mapping(c, 5.0f),  5.0f));   // above max_value
    RR_CHECK(approx(evaluate_mapping(c, -3.0f), -3.0f));  // below min_value
}

void test_evaluate_mapping_clamp_output_enabled_clamps_high() {
    using rr::field::evaluate_mapping;
    using rr::field::FieldMappingConfig;
    using rr::field::FieldMappingTarget;
    FieldMappingConfig c{};
    c.target       = FieldMappingTarget::Emission;
    c.strength     = 2.0f;
    c.bias         = 1.0f;
    c.min_value    = 0.0f;
    c.max_value    = 1.0f;
    c.clamp_output = true;
    // sample = 3 → mapped = 2*3+1 = 7 → clamped to max = 1.
    RR_CHECK(approx(evaluate_mapping(c, 3.0f), 1.0f));
    // Boundary: exactly at max stays at max.
    RR_CHECK(approx(evaluate_mapping(c, 0.0f), 1.0f));  // 2*0+1 = 1
}

void test_evaluate_mapping_clamp_output_enabled_clamps_low() {
    using rr::field::evaluate_mapping;
    using rr::field::FieldMappingConfig;
    using rr::field::FieldMappingTarget;
    FieldMappingConfig c{};
    c.target       = FieldMappingTarget::Emission;
    c.strength     = 1.0f;
    c.bias         = -2.0f;
    c.min_value    = 0.0f;
    c.max_value    = 1.0f;
    c.clamp_output = true;
    // sample = 0 → mapped = -2 → clamped to min = 0.
    RR_CHECK(approx(evaluate_mapping(c, 0.0f), 0.0f));
    // sample = 1 → mapped = -1 → clamped to min = 0.
    RR_CHECK(approx(evaluate_mapping(c, 1.0f), 0.0f));
}

void test_evaluate_mapping_clamp_output_enabled_passes_through_in_range() {
    using rr::field::evaluate_mapping;
    using rr::field::FieldMappingConfig;
    using rr::field::FieldMappingTarget;
    FieldMappingConfig c{};
    c.target       = FieldMappingTarget::DiagnosticAOV;
    c.strength     = 1.0f;
    c.bias         = 0.0f;
    c.min_value    = 0.0f;
    c.max_value    = 1.0f;
    c.clamp_output = true;
    // In-range sample passes through unchanged.
    RR_CHECK(approx(evaluate_mapping(c, 0.0f),  0.0f));
    RR_CHECK(approx(evaluate_mapping(c, 0.5f),  0.5f));
    RR_CHECK(approx(evaluate_mapping(c, 1.0f),  1.0f));
}

void test_evaluate_mapping_clamp_degenerate_range_returns_min() {
    using rr::field::evaluate_mapping;
    using rr::field::FieldMappingConfig;
    using rr::field::FieldMappingTarget;
    // Defence-in-depth: max_value < min_value is artist-
    // authoring nonsense. The evaluator collapses to
    // returning `min_value` (well-defined fallback).
    FieldMappingConfig c{};
    c.target       = FieldMappingTarget::Emission;
    c.strength     = 1.0f;
    c.bias         = 0.0f;
    c.min_value    = 1.0f;
    c.max_value    = 0.0f;  // degenerate: max < min
    c.clamp_output = true;
    RR_CHECK(approx(evaluate_mapping(c, 0.5f), 1.0f));
    RR_CHECK(approx(evaluate_mapping(c, 5.0f), 1.0f));
    RR_CHECK(approx(evaluate_mapping(c, -5.0f), 1.0f));
}

void test_evaluate_mapping_negative_strength() {
    using rr::field::evaluate_mapping;
    using rr::field::FieldMappingConfig;
    using rr::field::FieldMappingTarget;
    // Negative strength inverts the field's contribution
    // (artist may want a "bright field → dark output" map).
    FieldMappingConfig c{};
    c.target   = FieldMappingTarget::ColorMultiplier;
    c.strength = -1.0f;
    c.bias     = 1.0f;
    RR_CHECK(approx(evaluate_mapping(c, 0.0f),  1.0f));
    RR_CHECK(approx(evaluate_mapping(c, 1.0f),  0.0f));
    RR_CHECK(approx(evaluate_mapping(c, 0.25f), 0.75f));
}

}  // namespace

int main() {
    // §1 — Enum + factory + default-POD anchors.
    test_scalar_field_kind_enumerators_distinct();
    test_disabled_scalar_field_config_factory();
    test_default_scalar_field_config_default_constructed();

    // §2 — Default disabled / no-op anchors.
    test_evaluate_disabled_returns_zero();
    test_evaluate_enabled_but_zero_strength_returns_zero();

    // §3 — Constant kind.
    test_constant_kind_returns_strength_times_value();
    test_constant_kind_strength_scales_output();

    // §4 — Radial kind.
    test_radial_kind_min_value_at_or_inside_min_radius();
    test_radial_kind_max_value_at_or_outside_max_radius();
    test_radial_kind_smoothstep_midway();
    test_radial_kind_strength_scales_output();
    test_radial_kind_offset_center();
    test_radial_kind_degenerate_envelope_returns_zero();
    test_radial_kind_falloff_reshapes_transition();

    // §5 — ProceduralPlaceholder kind.
    test_procedural_placeholder_returns_zero();

    // §6 — Vec4 overload routes to spatial Vec3.
    test_vec4_overload_consumes_spatial_part();
    test_vec4_overload_radial_routes_through_spatial_part();

    // §7 — FieldMappingTarget + FieldMappingConfig (FIELD-I.4).
    test_field_mapping_target_enumerators_distinct();
    test_disabled_field_mapping_config_factory();
    test_default_field_mapping_config_default_constructed();
    test_evaluate_mapping_default_target_none_returns_zero();
    test_evaluate_mapping_target_none_short_circuits();
    test_evaluate_mapping_color_multiplier_no_clamp();
    test_evaluate_mapping_emission_target_routes();
    test_evaluate_mapping_diagnostic_aov_target_routes();
    test_evaluate_mapping_zero_strength_returns_bias();
    test_evaluate_mapping_bias_additive();
    test_evaluate_mapping_clamp_output_disabled_passes_through();
    test_evaluate_mapping_clamp_output_enabled_clamps_high();
    test_evaluate_mapping_clamp_output_enabled_clamps_low();
    test_evaluate_mapping_clamp_output_enabled_passes_through_in_range();
    test_evaluate_mapping_clamp_degenerate_range_returns_min();
    test_evaluate_mapping_negative_strength();

    std::printf("field_tests: %d / %d passed\n",
                g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
