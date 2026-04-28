// Hand-rolled assertion runner. The real test framework comes
// with the M2 deferred items.
//
// Renderer server tests. Exercise the pure command dispatcher
// directly so port binding / TCP I/O do not enter the test path
// (which would make CI flaky on hosts without :7777 available).
// The TCP loop in `RenderServer::run` is a thin shell over
// `dispatch_command`; covering the dispatcher covers v1 protocol
// semantics end-to-end.

#include "server/RenderServer.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
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

using rr::server::CommandResult;
using rr::server::ServerState;
using rr::server::dispatch_command;

bool starts_with(const std::string& s, const char* prefix) {
    const auto n = std::strlen(prefix);
    return s.size() >= n && std::strncmp(s.c_str(), prefix, n) == 0;
}

bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

// --- ping ---------------------------------------------------------------

void test_ping_replies_pong() {
    ServerState state;
    const auto r = dispatch_command("ping", state);
    RR_CHECK(r.response == "OK pong");
    RR_CHECK(!r.wants_shutdown);
}

void test_ping_is_case_insensitive() {
    ServerState state;
    RR_CHECK(dispatch_command("PING",  state).response == "OK pong");
    RR_CHECK(dispatch_command("Ping",  state).response == "OK pong");
    RR_CHECK(dispatch_command("pInG",  state).response == "OK pong");
}

void test_ping_tolerates_surrounding_whitespace() {
    ServerState state;
    RR_CHECK(dispatch_command("  ping  ", state).response == "OK pong");
    // CRLF-style trailing chars from a Windows client.
    RR_CHECK(dispatch_command("ping\r",   state).response == "OK pong");
}

// --- empty / unknown ----------------------------------------------------

void test_empty_command_errors() {
    ServerState state;
    const auto r = dispatch_command("", state);
    RR_CHECK(starts_with(r.response, "ERR "));
    RR_CHECK(contains(r.response, "empty"));
    RR_CHECK(!r.wants_shutdown);
}

void test_whitespace_only_command_errors() {
    ServerState state;
    const auto r = dispatch_command("   \t \r", state);
    RR_CHECK(starts_with(r.response, "ERR "));
    RR_CHECK(contains(r.response, "empty"));
}

void test_unknown_command_errors() {
    ServerState state;
    const auto r = dispatch_command("frobnicate 42", state);
    RR_CHECK(starts_with(r.response, "ERR "));
    RR_CHECK(contains(r.response, "unknown"));
    RR_CHECK(contains(r.response, "frobnicate"));
}

// --- shutdown -----------------------------------------------------------

void test_shutdown_sets_wants_shutdown_flag() {
    ServerState state;
    const auto r = dispatch_command("shutdown", state);
    RR_CHECK(starts_with(r.response, "OK"));
    RR_CHECK(r.wants_shutdown);
}

// --- set_beta -----------------------------------------------------------

void test_set_beta_updates_observer_velocity() {
    ServerState state;
    RR_CHECK(state.scene.observer.velocity.x == 0.0f);

    const auto r = dispatch_command("set_beta 0.5", state);
    RR_CHECK(starts_with(r.response, "OK "));
    RR_CHECK(!r.wants_shutdown);
    RR_CHECK(state.scene.observer.velocity.x == 0.5f);
    RR_CHECK(state.scene.observer.velocity.y == 0.0f);
    RR_CHECK(state.scene.observer.velocity.z == 0.0f);
}

void test_set_beta_accepts_negative_values() {
    ServerState state;
    const auto r = dispatch_command("set_beta -0.25", state);
    RR_CHECK(starts_with(r.response, "OK "));
    RR_CHECK(state.scene.observer.velocity.x == -0.25f);
}

void test_set_beta_rejects_missing_argument() {
    ServerState state;
    const auto r = dispatch_command("set_beta", state);
    RR_CHECK(starts_with(r.response, "ERR "));
    // Velocity must remain unchanged on a rejected command.
    RR_CHECK(state.scene.observer.velocity.x == 0.0f);
}

void test_set_beta_rejects_non_numeric() {
    ServerState state;
    const auto r = dispatch_command("set_beta hello", state);
    RR_CHECK(starts_with(r.response, "ERR "));
    RR_CHECK(state.scene.observer.velocity.x == 0.0f);
}

void test_set_beta_rejects_at_or_above_unity() {
    ServerState state;
    {
        const auto r = dispatch_command("set_beta 1.0", state);
        RR_CHECK(starts_with(r.response, "ERR "));
    }
    {
        const auto r = dispatch_command("set_beta 1.5", state);
        RR_CHECK(starts_with(r.response, "ERR "));
    }
    {
        const auto r = dispatch_command("set_beta -1.0", state);
        RR_CHECK(starts_with(r.response, "ERR "));
    }
    RR_CHECK(state.scene.observer.velocity.x == 0.0f);
}

// --- load_scene ---------------------------------------------------------

void test_load_scene_rejects_missing_argument() {
    ServerState state;
    const auto r = dispatch_command("load_scene", state);
    RR_CHECK(starts_with(r.response, "ERR "));
    RR_CHECK(!state.scene_loaded);
}

void test_load_scene_reports_missing_file() {
    ServerState state;
    const auto r = dispatch_command(
        "load_scene scenes/__definitely_missing__.rrscene", state);
    RR_CHECK(starts_with(r.response, "ERR "));
    RR_CHECK(!state.scene_loaded);
}

void test_load_scene_succeeds_on_real_fixture() {
    ServerState state;
    const std::string path = std::string(RR_TEST_FIXTURES_DIR)
                           + "/test_minimal.rrscene";
    const auto r = dispatch_command("load_scene " + path, state);
    RR_CHECK(starts_with(r.response, "OK "));
    RR_CHECK(state.scene_loaded);
    RR_CHECK(state.last_scene_path == std::filesystem::path(path));
    // The fixture has at least one material + one sphere; the
    // exact counts are surfaced in the response so the test
    // confirms the dispatcher reports them rather than baking
    // hard-coded numbers in.
    RR_CHECK(contains(r.response, "materials"));
    RR_CHECK(contains(r.response, "spheres"));
}

// --- render -------------------------------------------------------------

void test_render_without_scene_errors() {
    ServerState state;
    const auto r = dispatch_command("render", state);
    RR_CHECK(starts_with(r.response, "ERR "));
    RR_CHECK(contains(r.response, "no scene loaded"));
    RR_CHECK(!r.wants_shutdown);
    // Failed renders do not advance the success counter or
    // populate the `last_render_*` bookkeeping.
    RR_CHECK(state.render_count       == 0);
    RR_CHECK(state.last_render_width  == 0);
    RR_CHECK(state.last_render_height == 0);
    RR_CHECK(state.last_render_path.empty());
}

#ifndef RR_HAS_CUDA
void test_render_without_cuda_reports_clearly() {
    ServerState state;
    // Load a scene first so we exercise the no-CUDA branch
    // rather than the no-scene branch.
    const std::string path = std::string(RR_TEST_FIXTURES_DIR)
                           + "/test_minimal.rrscene";
    const auto loaded = dispatch_command("load_scene " + path, state);
    RR_CHECK(starts_with(loaded.response, "OK "));

    const auto r = dispatch_command("render", state);
    RR_CHECK(starts_with(r.response, "ERR "));
    RR_CHECK(contains(r.response, "CUDA"));
    // Even with a scene loaded, a failed render must not advance
    // the success counter.
    RR_CHECK(state.render_count == 0);
}
#endif

// --- end-to-end command sequence ----------------------------------------

void test_load_then_set_beta_threads_state() {
    // Verify the natural client sequence (load_scene -> set_beta
    // -> render) flows scene + observer state through the
    // dispatcher's mutable state correctly. The render call's
    // GPU branch is unreachable on a host-only build, but we
    // can confirm the chain reaches it without spurious errors.
    ServerState state;
    const std::string path = std::string(RR_TEST_FIXTURES_DIR)
                           + "/test_minimal.rrscene";

    const auto loaded = dispatch_command("load_scene " + path, state);
    RR_CHECK(starts_with(loaded.response, "OK "));
    RR_CHECK(state.scene_loaded);

    // After load_scene the velocity is whatever the file declared
    // (the fixture leaves it at zero); set_beta should rewrite it.
    const auto beta = dispatch_command("set_beta 0.7", state);
    RR_CHECK(starts_with(beta.response, "OK "));
    RR_CHECK(state.scene.observer.velocity.x == 0.7f);
    RR_CHECK(state.scene.observer.velocity.y == 0.0f);
    RR_CHECK(state.scene.observer.velocity.z == 0.0f);

    // The render call should reach the no-CUDA branch (host-only
    // CI) or the GPU branch (CUDA-enabled CI) - either way it
    // must not raise the no-scene-loaded error path now.
    const auto rendered = dispatch_command("render", state);
    RR_CHECK(!contains(rendered.response, "no scene loaded"));
}

void test_load_scene_resets_after_set_beta() {
    // load_scene replaces the entire scene. Beta set BEFORE a
    // load is therefore expected to be lost on the load - this
    // pins the behaviour so a future change doesn't silently
    // start preserving it.
    ServerState state;

    const auto beta = dispatch_command("set_beta 0.4", state);
    RR_CHECK(starts_with(beta.response, "OK "));
    RR_CHECK(state.scene.observer.velocity.x == 0.4f);

    const std::string path = std::string(RR_TEST_FIXTURES_DIR)
                           + "/test_minimal.rrscene";
    const auto loaded = dispatch_command("load_scene " + path, state);
    RR_CHECK(starts_with(loaded.response, "OK "));
    // Fixture has no relativity section, so the loaded scene's
    // observer velocity is back to the default (zero).
    RR_CHECK(state.scene.observer.velocity.x == 0.0f);
}

// --- argument parsing edge cases ----------------------------------------

void test_paths_with_spaces_are_passed_through() {
    // The dispatcher splits on the first whitespace run, so
    // anything after that is the path. Spaces inside the path
    // would still be preserved if they came through; the trim
    // strips outer whitespace only. Verify the simple case here:
    // a load_scene call with an obviously bad path should still
    // hit the IO layer's "file not found" branch rather than
    // an argument parsing failure.
    ServerState state;
    const auto r = dispatch_command(
        "load_scene /tmp/__nonexistent__.rrscene", state);
    RR_CHECK(starts_with(r.response, "ERR "));
    // The error must come from the loader (file not found) and
    // therefore mention "load_scene failed", not from an
    // argument-validation error.
    RR_CHECK(contains(r.response, "load_scene failed"));
}

}

int main() {
    test_ping_replies_pong();
    test_ping_is_case_insensitive();
    test_ping_tolerates_surrounding_whitespace();
    test_empty_command_errors();
    test_whitespace_only_command_errors();
    test_unknown_command_errors();
    test_shutdown_sets_wants_shutdown_flag();
    test_set_beta_updates_observer_velocity();
    test_set_beta_accepts_negative_values();
    test_set_beta_rejects_missing_argument();
    test_set_beta_rejects_non_numeric();
    test_set_beta_rejects_at_or_above_unity();
    test_load_scene_rejects_missing_argument();
    test_load_scene_reports_missing_file();
    test_load_scene_succeeds_on_real_fixture();
    test_render_without_scene_errors();
#ifndef RR_HAS_CUDA
    test_render_without_cuda_reports_clearly();
#endif
    test_load_then_set_beta_threads_state();
    test_load_scene_resets_after_set_beta();
    test_paths_with_spaces_are_passed_through();

    std::printf("server_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
