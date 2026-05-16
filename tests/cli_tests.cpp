// NEE.5 (test expansion) — CLI parser tests for the
// `--enable-nee` modifier flag.
//
// MANI-I.1 (test expansion) — CLI parser tests for the four
// `--manifold-*` modifier flags
// (`--manifold-enable` / `--manifold-chart` /
// `--manifold-strength` / `--manifold-debug`), appended at
// the bottom of the test registry.
//
// Source of the NEE.5 spec:
//   `docs/PATH_TRACER_ENABLE_NEE_CLI_TASK.md` §4.1 (CLI
//   parser test, five mandatory cases + an optional
//   case-mismatch case).
//
// Source of the MANI-I.1 spec:
//   `docs/MANIFOLD_INTEGRATION_PLAN.md` §4. The four flags
//   default to disabled / Euclidean / strength 0 /
//   debug off — every existing CLI invocation without any
//   `--manifold-*` flag must produce pixel-bit-identical
//   output to the pre-pivot renderer.
//
// Linkage strategy: Option B per the NEE.5 brief §4.1 —
// the test binary recompiles `src/core/CommandLine.cpp` +
// `src/core/Config.cpp` directly from source rather than
// extracting an `rr_core_cli` library. The brief explicitly
// accepts either Option A (library) or Option B (re-compile);
// Option B is the minimal-ripple choice for this slice. A
// future cleanup can extract the library if CLI-test growth
// makes the duplication painful.
//
// Constraints:
//   - No CUDA-host requirement: parser is pure host code.
//   - No server: tests do not invoke RenderServer.
//   - Hand-rolled assertions matching the existing
//     `pathtracer_tests.cpp` / `math_tests.cpp` framework
//     idiom (RR_CHECK + per-case counters + main() registry).

#include "core/CommandLine.h"
#include "core/Config.h"
#include "manifold/CoordinateChart.h"
#include "manifold/ObserverFrame.h"
#include "math/Vec3.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

using Action      = rr::core::CommandLine::Action;
using ParseResult = rr::core::CommandLine::ParseResult;

// `CommandLine::parse` takes `int argc, char** argv`. Build the
// argv backing-storage from the test's std::string list and
// invoke parse on it.
ParseResult run(std::vector<std::string> args_in) {
    std::vector<std::vector<char>> backing;
    backing.reserve(args_in.size());
    std::vector<char*> argv;
    argv.reserve(args_in.size() + 1);

    for (const auto& s : args_in) {
        backing.emplace_back(s.begin(), s.end());
        backing.back().push_back('\0');
        argv.push_back(backing.back().data());
    }
    argv.push_back(nullptr);

    return rr::core::CommandLine::parse(static_cast<int>(args_in.size()),
                                        argv.data());
}

// ---------- §4.1 mandatory cases ----------

// Case 1: default-off. parse({"prog"}) (no flag) returns
// `r.config.enable_nee == false` AND the action is NOT Error.
// Anchors the C++ default-init contract + the parser does not
// accidentally flip the field on an unrelated argv vector.
void test_default_off_no_flag() {
    const auto r = run({"prog"});
    RR_CHECK(r.config.enable_nee == false);
    RR_CHECK(r.action != Action::Error);
    RR_CHECK(r.error_message.empty());
}

// Case 2: flag presence (action then flag). The parser arm
// flips `r.config.enable_nee` to `true` and the action remains
// RenderPathtrace.
void test_flag_after_action() {
    const auto r = run({"prog", "--render-pathtrace",
                        "scene.rrscene", "--enable-nee"});
    RR_CHECK(r.config.enable_nee == true);
    RR_CHECK(r.action == Action::RenderPathtrace);
    RR_CHECK(r.error_message.empty());
    // Also confirm the scene-path token was consumed by the
    // action arm, not the modifier arm.
    RR_CHECK(r.config.scene_path == "scene.rrscene");
}

// Case 3: flag-then-action ordering. Same outcome as case 2;
// confirms the modifier flag is order-independent (the parser
// loop's else-if chain accepts the flag in either position).
void test_flag_before_action() {
    const auto r = run({"prog", "--enable-nee",
                        "--render-pathtrace", "scene.rrscene"});
    RR_CHECK(r.config.enable_nee == true);
    RR_CHECK(r.action == Action::RenderPathtrace);
    RR_CHECK(r.error_message.empty());
    RR_CHECK(r.config.scene_path == "scene.rrscene");
}

// Case 4: repeated flag is idempotent. Second occurrence
// re-assigns `true` to an already-`true` field (mirrors
// `--denoise`'s contract).
void test_flag_idempotent() {
    const auto r = run({"prog", "--render-pathtrace",
                        "scene.rrscene", "--enable-nee",
                        "--enable-nee"});
    RR_CHECK(r.config.enable_nee == true);
    RR_CHECK(r.action == Action::RenderPathtrace);
    RR_CHECK(r.error_message.empty());
}

// Case 5: combined with another modifier flag
// (--firefly-clamp). Both fields land on Config; no
// cross-flag interference.
void test_flag_with_firefly_clamp() {
    const auto r = run({"prog", "--render-pathtrace",
                        "scene.rrscene", "--enable-nee",
                        "--firefly-clamp", "8.0"});
    RR_CHECK(r.config.enable_nee == true);
    RR_CHECK(r.config.firefly_clamp == 8.0f);
    RR_CHECK(r.action == Action::RenderPathtrace);
    RR_CHECK(r.error_message.empty());
}

// ---------- §4.1 optional 6th case ----------

// Case 6: case-mismatch rejected. The parser uses
// case-sensitive flag matching; `--enable-Nee` (capital N)
// must NOT be silently accepted as `--enable-nee`. Falls
// through to the parser's "unknown argument" handler.
void test_case_mismatch_rejected() {
    const auto r = run({"prog", "--render-pathtrace",
                        "scene.rrscene", "--enable-Nee"});
    RR_CHECK(r.action == Action::Error);
    RR_CHECK(r.config.enable_nee == false);
    // Error message references the offending token verbatim
    // so the operator can spot the typo.
    RR_CHECK(r.error_message.find("--enable-Nee") != std::string::npos);
}

// ---------- additional default-off byte-identity anchors ----------
// These directly exercise the user's "Default OFF behavior must
// remain unchanged" rule on the parser surface: across multiple
// argv vectors that do NOT include `--enable-nee`,
// `r.config.enable_nee` stays bit-false. The CUDA path's actual
// per-pixel byte-identity is structurally guaranteed by the
// PATH_TRACER_NEE_AUDIT.md §1.2 IEEE-754 + RNG-stream argument;
// these parser-surface checks anchor that no downstream caller
// ever sees a non-false default from the CLI.

void test_default_off_with_other_flags() {
    // Various argv vectors that do NOT pass --enable-nee.
    // Each must produce `r.config.enable_nee == false`.
    const std::vector<std::vector<std::string>> argv_vectors = {
        {"prog", "--render-pathtrace", "scene.rrscene"},
        {"prog", "--render-pathtrace", "scene.rrscene",
         "--firefly-clamp", "8.0"},
        {"prog", "--render-pathtrace", "scene.rrscene",
         "--width", "320", "--height", "240"},
        {"prog", "--render-optix-pathtrace", "scene.rrscene"},
        {"prog", "--scene-info", "scene.rrscene"},
    };
    for (const auto& argv : argv_vectors) {
        const auto r = run(argv);
        RR_CHECK(r.config.enable_nee == false);
        RR_CHECK(r.action != Action::Error);
    }
}

// ---------- MANI-I.1: `--manifold-*` parser tests ----------

using ChartType = rr::manifold::CoordinateChartType;

// Case M1: defaults. No `--manifold-*` flag => the `manifold`
// field on Config is the documented "disabled, Euclidean,
// strength 0, no debug overlay" anchor (matches
// `ManifoldMode{}`).
void test_manifold_default_disabled() {
    const auto r = run({"prog"});
    RR_CHECK(r.action != Action::Error);
    RR_CHECK(r.config.manifold.enabled                                == false);
    RR_CHECK(r.config.manifold.chart                                  == ChartType::Euclidean);
    RR_CHECK(r.config.manifold.strength                               == 0.0f);
    RR_CHECK(r.config.manifold.debug_visualization                    == false);
    RR_CHECK(r.config.manifold.preserve_light_speed_normally          == true);
    RR_CHECK(r.config.manifold.transform_coordinates_instead_of_light == true);
}

// Case M2: --manifold-enable flips the master switch; the
// other fields stay at their defaults.
void test_manifold_enable_flag() {
    const auto r = run({"prog", "--manifold-enable"});
    RR_CHECK(r.action != Action::Error);
    RR_CHECK(r.config.manifold.enabled                  == true);
    RR_CHECK(r.config.manifold.chart                    == ChartType::Euclidean);
    RR_CHECK(r.config.manifold.strength                 == 0.0f);
    RR_CHECK(r.config.manifold.debug_visualization      == false);
}

// Case M3: every legal `--manifold-chart` value parses to
// the matching `CoordinateChartType` enumerator. Verifies
// the five-name dispatch table end-to-end.
void test_manifold_chart_each_value() {
    struct Case { const char* name; ChartType want; };
    const Case cases[] = {
        {"euclidean",           ChartType::Euclidean},
        {"schwarzschild-like",  ChartType::SchwarzschildLike},
        {"kruskal-like",        ChartType::KruskalLikePlaceholder},
        {"penrose-like",        ChartType::PenroseLike},
        {"kerr-like",           ChartType::KerrLikePlaceholder},
    };
    for (const auto& c : cases) {
        const auto r = run({"prog", "--manifold-chart", c.name});
        RR_CHECK(r.action != Action::Error);
        RR_CHECK(r.config.manifold.chart == c.want);
    }
}

// Case M4: unknown `--manifold-chart` value is rejected with
// a parse error that names the offending token AND lists
// every legal alternative (the operator-facing diagnostic
// contract from MANIFOLD_INTEGRATION_PLAN.md §4 Risks).
void test_manifold_chart_unknown_rejected() {
    const auto r = run({"prog", "--manifold-chart", "bogus"});
    RR_CHECK(r.action == Action::Error);
    RR_CHECK(r.error_message.find("--manifold-chart") != std::string::npos);
    RR_CHECK(r.error_message.find("bogus")            != std::string::npos);
    RR_CHECK(r.error_message.find("euclidean")        != std::string::npos);
    RR_CHECK(r.error_message.find("schwarzschild-like") != std::string::npos);
    RR_CHECK(r.error_message.find("kerr-like")        != std::string::npos);
}

// Case M5: case-mismatch on the value is rejected (parser is
// case-sensitive). `Euclidean` (capital E) must NOT alias to
// `euclidean`. Mirrors NEE.5's case-mismatch case.
void test_manifold_chart_case_mismatch_rejected() {
    const auto r = run({"prog", "--manifold-chart", "Euclidean"});
    RR_CHECK(r.action == Action::Error);
    RR_CHECK(r.error_message.find("Euclidean") != std::string::npos);
    // The config's chart field stays at the default.
    RR_CHECK(r.config.manifold.chart == ChartType::Euclidean);
}

// Case M6: `--manifold-strength` parses a finite float into
// the `manifold.strength` field; in-range values pass through.
void test_manifold_strength_value() {
    const auto r = run({"prog", "--manifold-strength", "0.75"});
    RR_CHECK(r.action != Action::Error);
    RR_CHECK(r.config.manifold.strength == 0.75f);
}

// Case M7: out-of-range strength values are NOT clamped at
// parse time. Both negative and >1 values pass through to the
// renderer per the ManifoldMode::strength contract.
void test_manifold_strength_out_of_range_passes() {
    const auto r_neg = run({"prog", "--manifold-strength", "-0.5"});
    RR_CHECK(r_neg.action != Action::Error);
    RR_CHECK(r_neg.config.manifold.strength == -0.5f);

    const auto r_big = run({"prog", "--manifold-strength", "2.0"});
    RR_CHECK(r_big.action != Action::Error);
    RR_CHECK(r_big.config.manifold.strength == 2.0f);
}

// Case M8: non-parseable `--manifold-strength` value is
// rejected at parse time.
void test_manifold_strength_invalid_rejected() {
    const auto r = run({"prog", "--manifold-strength", "abc"});
    RR_CHECK(r.action == Action::Error);
    RR_CHECK(r.error_message.find("--manifold-strength") != std::string::npos);
    RR_CHECK(r.error_message.find("abc")                 != std::string::npos);
}

// Case M9: `--manifold-debug` flips the debug_visualization
// toggle. Presence-only flag (no value consumed).
void test_manifold_debug_flag() {
    const auto r = run({"prog", "--manifold-debug"});
    RR_CHECK(r.action != Action::Error);
    RR_CHECK(r.config.manifold.debug_visualization == true);
}

// Case M10: all four flags compose; the parser populates the
// matching fields without cross-flag interference.
void test_manifold_all_four_flags_combined() {
    const auto r = run({"prog",
                        "--manifold-enable",
                        "--manifold-chart", "schwarzschild-like",
                        "--manifold-strength", "0.5",
                        "--manifold-debug"});
    RR_CHECK(r.action != Action::Error);
    RR_CHECK(r.config.manifold.enabled              == true);
    RR_CHECK(r.config.manifold.chart                == ChartType::SchwarzschildLike);
    RR_CHECK(r.config.manifold.strength             == 0.5f);
    RR_CHECK(r.config.manifold.debug_visualization  == true);
}

// Case M11: order-independence. The four flags work in any
// order and compose with action + scene-path tokens cleanly.
void test_manifold_flags_order_independent() {
    const auto r = run({"prog",
                        "--manifold-strength", "0.25",
                        "--render-pathtrace", "scene.rrscene",
                        "--manifold-enable",
                        "--manifold-chart", "penrose-like",
                        "--enable-nee",
                        "--manifold-debug"});
    RR_CHECK(r.action == Action::RenderPathtrace);
    RR_CHECK(r.error_message.empty());
    RR_CHECK(r.config.scene_path == "scene.rrscene");
    RR_CHECK(r.config.enable_nee == true);
    RR_CHECK(r.config.manifold.enabled              == true);
    RR_CHECK(r.config.manifold.chart                == ChartType::PenroseLike);
    RR_CHECK(r.config.manifold.strength             == 0.25f);
    RR_CHECK(r.config.manifold.debug_visualization  == true);
}

// Case M12: `--manifold-chart` without a following value is a
// parse error (take_value contract: refuses to swallow another
// `--*` token).
void test_manifold_chart_missing_value_rejected() {
    const auto r = run({"prog", "--manifold-chart", "--manifold-debug"});
    RR_CHECK(r.action == Action::Error);
    RR_CHECK(r.error_message.find("--manifold-chart") != std::string::npos);
}

// Case M13: default-off byte-identity anchor. Across various
// non-manifold argv vectors, the four manifold fields stay at
// their pre-pivot defaults. This is the parser-surface
// equivalent of the bit-identity invariant the integration
// plan §2 declares.
void test_manifold_default_off_with_other_flags() {
    const std::vector<std::vector<std::string>> argv_vectors = {
        {"prog", "--render-pathtrace", "scene.rrscene"},
        {"prog", "--render-pathtrace", "scene.rrscene",
         "--firefly-clamp", "8.0"},
        {"prog", "--render-pathtrace", "scene.rrscene",
         "--width", "320", "--height", "240"},
        {"prog", "--render-optix-pathtrace", "scene.rrscene"},
        {"prog", "--scene-info", "scene.rrscene"},
        {"prog", "--render-aovs", "--denoise"},
        {"prog", "--render-demo", "--beta", "0.7"},
    };
    for (const auto& argv : argv_vectors) {
        const auto r = run(argv);
        RR_CHECK(r.action != Action::Error);
        RR_CHECK(r.config.manifold.enabled                  == false);
        RR_CHECK(r.config.manifold.chart                    == ChartType::Euclidean);
        RR_CHECK(r.config.manifold.strength                 == 0.0f);
        RR_CHECK(r.config.manifold.debug_visualization      == false);
    }
}

// ---------- OBSERVER.4: --observer-* parser tests ----------

using PerceptionMode = rr::manifold::PerceptionMode;

// Case O1: ObserverConfig defaults match the documented
// no-op anchor (per the OBSERVER.1 plan §3.6 + the
// `rr::manifold::ObserverConfig` POD's per-field initialisers).
// Parser surface byte-identity is enforced via the existing
// MANI-CONSUME.* tests; this case anchors the C++ default-init.
void test_observer_default_off() {
    const auto r = run({"prog"});
    RR_CHECK(r.action != Action::Error);
    RR_CHECK(r.config.observer.beta_magnitude == 0.0f);
    RR_CHECK(r.config.observer.direction.x    == 0.0f);
    RR_CHECK(r.config.observer.direction.y    == 0.0f);
    RR_CHECK(r.config.observer.direction.z    == 0.0f);
    RR_CHECK(r.config.observer.proper_time    == 0.0f);
    RR_CHECK(r.config.observer.perception_mode == PerceptionMode::Identity);
}

// Case O2: `--observer-beta` parses a finite float into the
// `observer.beta_magnitude` field.
void test_observer_beta_value() {
    const auto r = run({"prog", "--observer-beta", "0.5"});
    RR_CHECK(r.action != Action::Error);
    RR_CHECK(r.config.observer.beta_magnitude == 0.5f);
}

// Case O3: out-of-range beta passes through (the consumer
// clamps via clampBeta at OBSERVER.5; matching the existing
// `--manifold-strength` precedent). Negative values pass
// through too (a negative magnitude is a sign-flip on the
// direction; the consumer resolves it).
void test_observer_beta_out_of_range_passes() {
    const auto r_neg = run({"prog", "--observer-beta", "-0.3"});
    RR_CHECK(r_neg.action != Action::Error);
    RR_CHECK(r_neg.config.observer.beta_magnitude == -0.3f);

    const auto r_big = run({"prog", "--observer-beta", "2.0"});
    RR_CHECK(r_big.action != Action::Error);
    RR_CHECK(r_big.config.observer.beta_magnitude == 2.0f);
}

// Case O4: non-parseable `--observer-beta` value is rejected.
void test_observer_beta_invalid_rejected() {
    const auto r = run({"prog", "--observer-beta", "abc"});
    RR_CHECK(r.action == Action::Error);
    RR_CHECK(r.error_message.find("--observer-beta") != std::string::npos);
    RR_CHECK(r.error_message.find("abc")             != std::string::npos);
}

// Case O5: non-finite `--observer-beta` value is rejected
// (parse-time finite-floats gate so the consumer never sees
// NaN / +-inf on the magnitude).
void test_observer_beta_non_finite_rejected() {
    const auto r_nan = run({"prog", "--observer-beta", "nan"});
    RR_CHECK(r_nan.action == Action::Error);
    RR_CHECK(r_nan.error_message.find("--observer-beta") != std::string::npos);

    const auto r_inf = run({"prog", "--observer-beta", "inf"});
    RR_CHECK(r_inf.action == Action::Error);
    RR_CHECK(r_inf.error_message.find("--observer-beta") != std::string::npos);
}

// Case O6: `--observer-direction` parses a comma-separated
// triple into the `observer.direction` Vec3 field.
void test_observer_direction_value() {
    const auto r = run({"prog", "--observer-direction", "0.6,-0.8,0.0"});
    RR_CHECK(r.action != Action::Error);
    RR_CHECK(r.config.observer.direction.x ==  0.6f);
    RR_CHECK(r.config.observer.direction.y == -0.8f);
    RR_CHECK(r.config.observer.direction.z ==  0.0f);
}

// Case O7: zero direction is a legal value (the sentinel
// "not specified" default; the consumer at OBSERVER.5 falls
// back to the camera forward axis if magnitude is non-zero).
void test_observer_direction_zero_accepted() {
    const auto r = run({"prog", "--observer-direction", "0,0,0"});
    RR_CHECK(r.action != Action::Error);
    RR_CHECK(r.config.observer.direction.x == 0.0f);
    RR_CHECK(r.config.observer.direction.y == 0.0f);
    RR_CHECK(r.config.observer.direction.z == 0.0f);
}

// Case O8: malformed `--observer-direction` token is rejected.
// Covers: too few commas; too many commas; empty component;
// non-parseable component; non-finite component.
void test_observer_direction_malformed_rejected() {
    const auto r_few = run({"prog", "--observer-direction", "1.0,2.0"});
    RR_CHECK(r_few.action == Action::Error);
    RR_CHECK(r_few.error_message.find("--observer-direction") != std::string::npos);

    const auto r_many = run({"prog", "--observer-direction", "1.0,2.0,3.0,4.0"});
    RR_CHECK(r_many.action == Action::Error);

    const auto r_empty = run({"prog", "--observer-direction", "1.0,,3.0"});
    RR_CHECK(r_empty.action == Action::Error);

    const auto r_word = run({"prog", "--observer-direction", "x,y,z"});
    RR_CHECK(r_word.action == Action::Error);

    const auto r_nan = run({"prog", "--observer-direction", "nan,0,0"});
    RR_CHECK(r_nan.action == Action::Error);
}

// Case O9: `--observer-proper-time` parses a finite float
// into the `observer.proper_time` field.
void test_observer_proper_time_value() {
    const auto r = run({"prog", "--observer-proper-time", "42.5"});
    RR_CHECK(r.action != Action::Error);
    RR_CHECK(r.config.observer.proper_time == 42.5f);
}

// Case O10: non-finite `--observer-proper-time` value is
// rejected (parse-time finite-floats gate, matching the
// `--observer-beta` precedent).
void test_observer_proper_time_non_finite_rejected() {
    const auto r_nan = run({"prog", "--observer-proper-time", "nan"});
    RR_CHECK(r_nan.action == Action::Error);
    RR_CHECK(r_nan.error_message.find("--observer-proper-time") != std::string::npos);

    const auto r_inf = run({"prog", "--observer-proper-time", "-inf"});
    RR_CHECK(r_inf.action == Action::Error);
}

// Case O11: `--observer-perception-mode` accepts both
// `default` and `relativistic` and maps each to the matching
// PerceptionMode enumerator.
void test_observer_perception_mode_each_value() {
    struct Case { const char* name; PerceptionMode want; };
    const Case cases[] = {
        {"default",      PerceptionMode::Identity},
        {"relativistic", PerceptionMode::ConstantVelocityMinkowski},
    };
    for (const auto& c : cases) {
        const auto r = run({"prog", "--observer-perception-mode", c.name});
        RR_CHECK(r.action != Action::Error);
        RR_CHECK(r.config.observer.perception_mode == c.want);
    }
}

// Case O12: unknown `--observer-perception-mode` value is
// rejected with a parse error that names the offending token
// AND lists every legal alternative.
void test_observer_perception_mode_unknown_rejected() {
    const auto r = run({"prog", "--observer-perception-mode", "bogus"});
    RR_CHECK(r.action == Action::Error);
    RR_CHECK(r.error_message.find("--observer-perception-mode") != std::string::npos);
    RR_CHECK(r.error_message.find("bogus")        != std::string::npos);
    RR_CHECK(r.error_message.find("default")      != std::string::npos);
    RR_CHECK(r.error_message.find("relativistic") != std::string::npos);
}

// Case O13: case-mismatch on the value is rejected (parser is
// case-sensitive; mirrors the `--manifold-chart` case-mismatch
// precedent).
void test_observer_perception_mode_case_mismatch_rejected() {
    const auto r = run({"prog", "--observer-perception-mode", "Default"});
    RR_CHECK(r.action == Action::Error);
    RR_CHECK(r.error_message.find("Default") != std::string::npos);
    // The config's perception mode stays at the default.
    RR_CHECK(r.config.observer.perception_mode == PerceptionMode::Identity);
}

// Case O14: the CurvedChartGeodesicPlaceholder enumerator is
// reserved-but-inert and NOT exposed on the CLI surface this
// slice (per the OBSERVER.1 plan §3.6 / §8 non-goals). Any
// attempt to select it by name must be rejected.
void test_observer_perception_mode_placeholder_not_exposed() {
    const auto r = run({"prog", "--observer-perception-mode",
                        "curved-chart-geodesic"});
    RR_CHECK(r.action == Action::Error);
}

// Case O15: all four observer flags compose; the parser
// populates the matching fields without cross-flag
// interference.
void test_observer_all_four_flags_combined() {
    const auto r = run({"prog",
                        "--observer-beta",          "0.6",
                        "--observer-direction",     "0.0,0.0,-1.0",
                        "--observer-proper-time",   "12.0",
                        "--observer-perception-mode", "relativistic"});
    RR_CHECK(r.action != Action::Error);
    RR_CHECK(r.config.observer.beta_magnitude == 0.6f);
    RR_CHECK(r.config.observer.direction.x    ==  0.0f);
    RR_CHECK(r.config.observer.direction.y    ==  0.0f);
    RR_CHECK(r.config.observer.direction.z    == -1.0f);
    RR_CHECK(r.config.observer.proper_time    == 12.0f);
    RR_CHECK(r.config.observer.perception_mode ==
             PerceptionMode::ConstantVelocityMinkowski);
}

// Case O16: order-independence with action + scene-path +
// other flags. The four observer flags compose with the
// manifold flags cleanly.
void test_observer_flags_order_independent() {
    const auto r = run({"prog",
                        "--observer-proper-time", "1.5",
                        "--render-pathtrace", "scene.rrscene",
                        "--observer-direction", "1.0,0.0,0.0",
                        "--manifold-enable",
                        "--observer-beta", "0.25",
                        "--observer-perception-mode", "relativistic",
                        "--manifold-chart", "schwarzschild-like"});
    RR_CHECK(r.action == Action::RenderPathtrace);
    RR_CHECK(r.error_message.empty());
    RR_CHECK(r.config.scene_path == "scene.rrscene");
    RR_CHECK(r.config.manifold.enabled == true);
    RR_CHECK(r.config.manifold.chart   == ChartType::SchwarzschildLike);
    RR_CHECK(r.config.observer.beta_magnitude == 0.25f);
    RR_CHECK(r.config.observer.direction.x    == 1.0f);
    RR_CHECK(r.config.observer.direction.y    == 0.0f);
    RR_CHECK(r.config.observer.direction.z    == 0.0f);
    RR_CHECK(r.config.observer.proper_time    == 1.5f);
    RR_CHECK(r.config.observer.perception_mode ==
             PerceptionMode::ConstantVelocityMinkowski);
}

// Case O17: missing-value cases. Each `--observer-*` flag
// that takes a value rejects an immediately-following `--*`
// token (take_value contract; mirrors the M12 precedent).
void test_observer_flags_missing_value_rejected() {
    const auto r_beta = run({"prog", "--observer-beta", "--manifold-debug"});
    RR_CHECK(r_beta.action == Action::Error);
    RR_CHECK(r_beta.error_message.find("--observer-beta") != std::string::npos);

    const auto r_dir = run({"prog", "--observer-direction", "--manifold-debug"});
    RR_CHECK(r_dir.action == Action::Error);

    const auto r_tau = run({"prog", "--observer-proper-time", "--manifold-debug"});
    RR_CHECK(r_tau.action == Action::Error);

    const auto r_mode = run({"prog", "--observer-perception-mode",
                             "--manifold-debug"});
    RR_CHECK(r_mode.action == Action::Error);
}

// Case O18b: `--observer-debug` flips
// `observer.debug_visualization` to `true`. Presence-only
// flag (no value consumed), mirroring the
// `--manifold-debug` (M9) precedent.
void test_observer_debug_flag() {
    const auto r = run({"prog", "--observer-debug"});
    RR_CHECK(r.action != Action::Error);
    RR_CHECK(r.config.observer.debug_visualization == true);
}

// Case O18c: `--observer-debug` defaults to `false` when not
// passed (anchors the C++ default-init contract).
void test_observer_debug_default_off() {
    const auto r = run({"prog"});
    RR_CHECK(r.action != Action::Error);
    RR_CHECK(r.config.observer.debug_visualization == false);
}

// Case O18d: `--observer-debug` composes cleanly with the
// existing `--render-aovs` / `--render-optix-aovs` actions
// + the other `--observer-*` flags + the `--manifold-*`
// flags (the two debug-AOV gates are orthogonal per
// OBSERVER.12 task brief §8.5).
void test_observer_debug_combined_with_other_flags() {
    const auto r = run({"prog",
                        "--render-aovs",
                        "--observer-debug",
                        "--observer-beta", "0.5",
                        "--observer-direction", "1,0,0",
                        "--observer-perception-mode", "relativistic",
                        "--manifold-debug",
                        "--manifold-enable",
                        "--manifold-chart", "schwarzschild-like",
                        "--manifold-strength", "0.4"});
    RR_CHECK(r.action != Action::Error);
    RR_CHECK(r.config.observer.debug_visualization == true);
    RR_CHECK(r.config.observer.beta_magnitude      == 0.5f);
    RR_CHECK(r.config.observer.perception_mode ==
             PerceptionMode::ConstantVelocityMinkowski);
    RR_CHECK(r.config.manifold.debug_visualization == true);
    RR_CHECK(r.config.manifold.enabled             == true);
    RR_CHECK(r.config.manifold.chart   == ChartType::SchwarzschildLike);
    RR_CHECK(r.config.manifold.strength            == 0.4f);
}

// Case O18: default-off byte-identity anchor. Across various
// non-observer argv vectors, the four observer fields stay at
// their pre-OBSERVER.4 defaults. This is the parser-surface
// equivalent of the bit-identity invariant the OBSERVER.1
// plan §8 non-goals declares.
void test_observer_default_off_with_other_flags() {
    const std::vector<std::vector<std::string>> argv_vectors = {
        {"prog", "--render-pathtrace", "scene.rrscene"},
        {"prog", "--render-pathtrace", "scene.rrscene",
         "--firefly-clamp", "8.0"},
        {"prog", "--render-pathtrace", "scene.rrscene",
         "--width", "320", "--height", "240"},
        {"prog", "--render-optix-pathtrace", "scene.rrscene"},
        {"prog", "--scene-info", "scene.rrscene"},
        {"prog", "--render-aovs", "--denoise"},
        {"prog", "--render-demo", "--beta", "0.7"},
        {"prog", "--render-aovs", "--manifold-enable",
         "--manifold-chart", "schwarzschild-like",
         "--manifold-strength", "0.5"},
    };
    for (const auto& argv : argv_vectors) {
        const auto r = run(argv);
        RR_CHECK(r.action != Action::Error);
        RR_CHECK(r.config.observer.beta_magnitude == 0.0f);
        RR_CHECK(r.config.observer.direction.x    == 0.0f);
        RR_CHECK(r.config.observer.direction.y    == 0.0f);
        RR_CHECK(r.config.observer.direction.z    == 0.0f);
        RR_CHECK(r.config.observer.proper_time    == 0.0f);
        RR_CHECK(r.config.observer.perception_mode ==
                 PerceptionMode::Identity);
        // OBSERVER.13: debug_visualization defaults to false
        // across every non-observer argv vector — the
        // pre-OBSERVER.13 byte-identity anchor.
        RR_CHECK(r.config.observer.debug_visualization == false);
    }
}

}  // namespace

int main() {
    test_default_off_no_flag();
    test_flag_after_action();
    test_flag_before_action();
    test_flag_idempotent();
    test_flag_with_firefly_clamp();
    test_case_mismatch_rejected();
    test_default_off_with_other_flags();

    // MANI-I.1: --manifold-* parser tests.
    test_manifold_default_disabled();
    test_manifold_enable_flag();
    test_manifold_chart_each_value();
    test_manifold_chart_unknown_rejected();
    test_manifold_chart_case_mismatch_rejected();
    test_manifold_strength_value();
    test_manifold_strength_out_of_range_passes();
    test_manifold_strength_invalid_rejected();
    test_manifold_debug_flag();
    test_manifold_all_four_flags_combined();
    test_manifold_flags_order_independent();
    test_manifold_chart_missing_value_rejected();
    test_manifold_default_off_with_other_flags();

    // OBSERVER.4: --observer-* parser tests.
    test_observer_default_off();
    test_observer_beta_value();
    test_observer_beta_out_of_range_passes();
    test_observer_beta_invalid_rejected();
    test_observer_beta_non_finite_rejected();
    test_observer_direction_value();
    test_observer_direction_zero_accepted();
    test_observer_direction_malformed_rejected();
    test_observer_proper_time_value();
    test_observer_proper_time_non_finite_rejected();
    test_observer_perception_mode_each_value();
    test_observer_perception_mode_unknown_rejected();
    test_observer_perception_mode_case_mismatch_rejected();
    test_observer_perception_mode_placeholder_not_exposed();
    test_observer_all_four_flags_combined();
    test_observer_flags_order_independent();
    test_observer_flags_missing_value_rejected();
    test_observer_default_off_with_other_flags();
    // OBSERVER.13: --observer-debug modifier flag.
    test_observer_debug_flag();
    test_observer_debug_default_off();
    test_observer_debug_combined_with_other_flags();

    std::fprintf(stderr, "cli_tests: %d/%d passed\n",
                 g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
