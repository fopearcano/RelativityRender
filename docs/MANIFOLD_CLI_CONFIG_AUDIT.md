# Manifold CLI Config Audit (MANI-I.2)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `1bb1fb4` ("core: MANI-I.1
— Manifold CLI Config (impl, CLI-only)").
Audit host: linux, audit-host build (no CUDA, no OptiX SDK).
Mode: documentation-only. No source code is touched by this
verdict; the result is synthesised purely from the tree's
current state, `git diff main..HEAD`, the running
`RelativityRender` executable's `--help` output, the
`cli_tests` binary's runtime output, and `ctest` exit codes.

This audit is the per-slice gate for the MANI-I.1 CLI
config slice (`1bb1fb4`). It verifies the seven items the
task brief enumerates — every CLI flag exists, defaults are
a no-op, invalid input is handled safely on both the
chart-name and strength axes, no renderer behaviour
changed, build / test green — and produces the
PASS / REPAIR / BLOCKED verdict that gates progression to
the renderer-config slice (renumbered MANI-I.3; see §4).

---

## 1. VERDICT

**PASS.**

All seven checks return PASS. No REPAIR or BLOCKED item is
found. The MANI-I.1 CLI config slice is safely exposed and
the operator may proceed to the renderer-config slice
(MANI-I.3, renumbered from the original MANI-I.2 per §4
below).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | CLI flags exist                              | **PASS** | `RelativityRender --help` reports exactly four `--manifold-*` flag entries (`--manifold-enable`, `--manifold-chart <name>`, `--manifold-strength <float>`, `--manifold-debug`). Each carries a dedicated help block citing MANI-I.1 and the future consumer slice (MANI-I.3 for the config plumb, MANI-I.4 for the debug-warp AOV per the renumbered integration plan §3). Source positions: `src/core/CommandLine.cpp` adds 88 net lines in the parse loop and 44 net lines in the usage text; `src/core/CommandLine.h` adds a 30-line header docstring block. |
| 2 | Defaults are no-op                           | **PASS** | `Config::manifold` is declared with default-initialisation, so `Config{}` carries `ManifoldMode{}` — i.e. `enabled = false`, `chart = CoordinateChartType::Euclidean`, `strength = 0.0f`, `debug_visualization = false`, `preserve_light_speed_normally = true`, `transform_coordinates_instead_of_light = true` (the documented "no output change" anchor per MANIFOLD.6 and the Foundation Audit verdict). `cli_tests` case M1 asserts every one of those six fields field-by-field. `cli_tests` case M13 re-asserts the same defaults across seven representative argv vectors that combine other modifier flags (`--firefly-clamp`, `--denoise`, `--enable-nee`, `--beta`, `--width`, `--height`) without a `--manifold-*` flag. |
| 3 | Invalid chart names handled safely           | **PASS** | The `parse_chart_type` helper in `CommandLine.cpp` performs an exact case-sensitive match against the five legal kebab-case names; on miss it returns `false`, and the calling parser arm sets `r.action = Action::Error` with the message `"invalid --manifold-chart value: <token> (expected one of: euclidean, schwarzschild-like, kruskal-like, penrose-like, kerr-like)"`. Empirically verified at runtime: `RelativityRender --manifold-chart bogus` exits with code 1 and prints the documented error followed by usage. `cli_tests` cases M4 (unknown value with full legal-name enumeration in the error), M5 (case-mismatch like `Euclidean` rejected; the config's chart field stays at the default), and M12 (missing value — the next token is another `--*` flag — rejected) all pass. |
| 4 | Invalid strength values handled safely       | **PASS** | The `--manifold-strength` parser arm uses `std::from_chars` and rejects any input whose parse fails or leaves residual characters with the message `"invalid float for --manifold-strength: <token>"`. Empirically verified: `RelativityRender --manifold-strength abc` exits with the documented error. The contract explicitly **allows** out-of-range values (negative or `> 1`) per the `ManifoldMode::strength` documentation; `cli_tests` case M7 verifies both `-0.5` and `2.0` pass through to `Config::manifold.strength` unchanged. Case M8 verifies non-parseable rejection. |
| 5 | No renderer behaviour changed                | **PASS** | `git diff main..HEAD` shows six files modified across the whole branch's MANI-I.1 commit, all inside the documented MANI-I.1 scope: `docs/BUILD_PLAN.md`, `docs/MANIFOLD_INTEGRATION_PLAN.md`, `src/core/CommandLine.cpp`, `src/core/CommandLine.h`, `src/core/Config.h`, `tests/cli_tests.cpp`. Zero files in any of `src/cuda/`, `src/optix/`, `src/pathtracer/`, `src/renderer/`, `src/gpu/`, `src/scene/`, `src/io/`, `src/server/`, `src/main.cpp`, `src/camera/`, `src/material/`, `src/lighting/`, `src/texture/`, `src/geometry/`, `src/image/`, `src/math/`, `src/relativity/`, `src/manifold/`, or `src/field/`. Every existing CLI action's dispatcher therefore ignores `Config::manifold` structurally — the field is parsed and stored, but no consumer reads it yet. |
| 6 | Build / test status                          | **PASS** | Audit-host `cmake --build build -j` succeeds cleanly with no warnings under the project's `rr_apply_warnings` settings (`-Wall -Wextra -Werror`-class). Full `ctest`: `100% tests passed, 0 tests failed out of 12` — the same twelve binaries that were green at the post-Foundation-Audit baseline (`math_tests`, `image_tests`, `gpu_tests`, `pathtracer_tests`, `pathtracer_nee_tests`, `pathtracer_bsdf_tests`, `pathtracer_mis_tests`, `cli_tests`, `relativity_tests`, `manifold_identity_tests`, `demo_tests`, `renderer_tests`). The `cli_tests` binary itself reports `cli_tests: 123/123 passed` (was `110/110` pre-MANI-I.1; the 13 new MANI-I.1 cases M1-M13 add 13 assertions of their own, with the increase of 110 → 123 reflecting the new RR_CHECK calls). `RelativityRender --version` and `RelativityRender --help` both run without error. |
| 7 | PASS / REPAIR / BLOCKED verdict              | **PASS** | All six structural checks above return PASS; no observation is REPAIR or BLOCKED. The slice's "what does NOT ship" list is exhaustive (no CUDA / OptiX / renderer / RenderSettings / scene / server / C4D / UI / coordinate-warp change), and the runtime invariants are pinned by `cli_tests` cases M1-M13 in 123 assertions. The slice is **safe to extend** into the renderer-config slice. |

---

## 3. REASONING SUMMARY

The MANI-I.1 commit (`1bb1fb4`) introduces:

- a `rr::manifold::ManifoldMode manifold` member on
  `rr::core::Config` (defaulting to the disabled /
  Euclidean / strength 0 / debug off anchor);
- four new modifier-flag parser arms in
  `src/core/CommandLine.cpp`
  (`--manifold-enable` / `--manifold-chart <name>` /
  `--manifold-strength <float>` / `--manifold-debug`)
  plus a `parse_chart_type` helper mapping the five legal
  kebab-case chart-family names to the matching
  `CoordinateChartType` enumerator;
- 44 lines of `--help` usage text documenting each new
  flag's role, defaults, accepted values, and downstream
  consumer slice;
- 30 lines of header-comment documentation in
  `CommandLine.h`;
- 13 new test cases (M1–M13) in `tests/cli_tests.cpp`
  covering default-off anchors, each-value chart dispatch,
  unknown / case-mismatch / missing-value rejection,
  in / out-of-range strength values, non-parseable
  strength rejection, debug-flag presence, four-flag
  combination, order-independence, and default-off
  byte-identity across seven representative argv vectors.

No file outside `src/core/`, `tests/cli_tests.cpp`, and
`docs/` is touched. The `rr_manifold` and `rr_field`
INTERFACE libraries remain unlinked from any renderer
target; the link-graph invariant the Foundation Audit
pinned is preserved verbatim. The four new flags populate
`Config` but no renderer code path reads `Config::manifold`
this slice — every existing CLI action's dispatcher
silently ignores the field, structurally guaranteeing the
bit-identity invariant the integration plan §2 requires.

Invalid-input handling is the audit's two most observable
risk surfaces. On the chart-name axis the parser is
strictly case-sensitive, enumerates every legal name in
the error message on miss, and refuses to swallow a
following `--*` token as the value (the existing
`take_value` contract). On the strength axis the parser
uses `std::from_chars` with a residual-character check
(rejecting tokens like `0.5abc` that partially parse) and
accepts out-of-range values per the `ManifoldMode::strength`
contract — the renderer is free to extrapolate when
consumer slices land. Both axes are empirically exercised
in `cli_tests` cases M3 / M4 / M5 / M12 (chart) and M6 /
M7 / M8 (strength).

Build / test invariants are unchanged from the pre-MANI-I.1
baseline: the audit-host build is green with no new
warnings, the existing 12 ctest binaries pass byte-
identically, and the `cli_tests` binary's internal
assertion count moves from `110/110 passed` to
`123/123 passed` with the MANI-I.1 expansion.

---

## 4. NEXT

The slice is **safe to extend**. The integration plan's
slice numbering needs a one-step shift to absorb this
audit slot:

- **MANI-I.1** — CLI config only (LANDED, audited at MANI-I.2).
- **MANI-I.2** — **THIS AUDIT** (CLI Config Audit, doc-only).
- **MANI-I.3** — pass ManifoldMode into renderer config
  (formerly MANI-I.2 in the plan; renumbered).
- **MANI-I.4** — Euclidean identity GPU path (formerly
  MANI-I.3).
- **MANI-I.5** — debug coordinate-warp AOV (formerly
  MANI-I.4).
- **MANI-I.6** — Schwarzschild-like artistic coordinate
  remap (formerly MANI-I.5).
- **MANI-I.7** — Penrose-like compactification
  visualisation (formerly MANI-I.6).
- **MANI-I.8** — final cross-host audit (formerly
  MANI-I.7); merge gate for the whole MANI-I.* programme.

The integration plan §3 chain diagram and §4–§10 slice
sections are updated as part of this MANI-I.2 commit so
the per-slice numbering stays coherent. The
`MANIFOLD_INTEGRATION_PLAN.md` §11 non-goals and §12
references sections are unchanged.

No REPAIR action is required. No BLOCKED item is
outstanding. The next concrete commit the operator may
prompt for is **MANI-I.3 — pass ManifoldMode into
renderer config** per the renumbered integration plan §5
(host-side plumb of `Config::manifold` into a
`rr::scene::RenderSettings::manifold` field; renderer
logs the value at launch but does not consume it; no GPU
touch).
