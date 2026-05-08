// NEE.5 (test expansion) — CLI parser tests for the
// `--enable-nee` modifier flag.
//
// Source of the spec: `docs/PATH_TRACER_ENABLE_NEE_CLI_TASK.md`
// §4.1 (CLI parser test, five mandatory cases + an optional
// case-mismatch case).
//
// Linkage strategy: Option B per the brief §4.1 — the test
// binary recompiles `src/core/CommandLine.cpp` +
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

}  // namespace

int main() {
    test_default_off_no_flag();
    test_flag_after_action();
    test_flag_before_action();
    test_flag_idempotent();
    test_flag_with_firefly_clamp();
    test_case_mismatch_rejected();
    test_default_off_with_other_flags();

    std::fprintf(stderr, "cli_tests: %d/%d passed\n",
                 g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
