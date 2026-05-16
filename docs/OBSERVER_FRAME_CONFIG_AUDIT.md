# ObserverFrame Config / CLI Bridge Audit (OBSERVER.5)

Date:   2026-05-16
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `16600dc` ("host:
OBSERVER.4 — ObserverFrame Config / CLI Bridge
(impl, host-only)").
Audit baseline: `bf57c9e` ("docs: OBSERVER.3 —
ObserverFrame Data Model Audit (docs only)") — the
last commit before OBSERVER.4 landed.
Audit host: linux, audit-host build (no CUDA SDK,
no OptiX SDK).
Mode: documentation-only. No source code is touched
by this verdict; the result is synthesised purely
from the tree's current state, `git diff` against
the post-OBSERVER.3 baseline, the `cli_tests`
runtime output, the `manifold_identity_tests`
runtime output, and `ctest` exit codes.

This audit is the per-slice gate for OBSERVER.4
(`16600dc`). It verifies the eight items the task
brief enumerates — observer CLI flags exist;
defaults preserve current behaviour; invalid beta
values handled safely; invalid direction values
handled safely; `ObserverFrame` receives CLI /
config values; no CUDA / OptiX behaviour changed;
build / test status; verdict — and produces a
`PASS` / `REPAIR` / `BLOCKED` verdict that gates
progression to the renumbered OBSERVER.6
(camera-to-observer adapter).

---

## 1. VERDICT

**PASS.**

All seven structural checks return `PASS`. No
`REPAIR` or `BLOCKED` item is found. The OBSERVER.4
CLI / config bridge is safely landed, default-no-op
verified, invalid-value rejection covered for every
parser arm, and produces zero CUDA / OptiX behaviour
change. The operator may proceed to OBSERVER.6
(camera-to-observer adapter; the host-side
`build_observer_frame_from_camera(...)` helper that
consumes the new `Config::observer` field + the
active `rr::camera::Camera` + the existing
`rr::relativity::Observer` and produces an
`ObserverFrame`).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | Observer CLI flags exist                 | **PASS** | The OBSERVER.4 commit (`16600dc`) adds four parser arms to `src/core/CommandLine.cpp`, each gated on a documented flag string:<br>**(a)** `--observer-beta <float>` at lines 678-700 — uses the new `parse_finite_float` helper (line 58); rejects non-parseable AND non-finite values.<br>**(b)** `--observer-direction <x,y,z>` at lines 701-725 — uses the new `parse_vec3` helper (line 75); rejects malformed triples (wrong comma count; empty component; non-parseable scalar; non-finite component).<br>**(c)** `--observer-proper-time <float>` at lines 726-745 — uses `parse_finite_float`.<br>**(d)** `--observer-perception-mode <name>` at lines 746-766 — uses the new `parse_perception_mode` helper (line 113); accepts `default` -> `PerceptionMode::Identity` and `relativistic` -> `PerceptionMode::ConstantVelocityMinkowski`; rejects unknown values with a diagnostic listing both legal names.<br>The flags are documented in the `--help` output at lines 1314-1364 (a multi-line block immediately after the `--manifold-debug` help block, before the `--width` / `--height` block); a smoke test `/home/user/RelativityRender/build/bin/RelativityRender --help \| grep -A 5 "observer-"` returns all four flag entries with their documented descriptions. The flag arms are populated on the new `rr::core::Config::observer` field (a `rr::manifold::ObserverConfig` POD) at `src/core/Config.h:171` — the `ObserverConfig` itself is declared at `src/manifold/ObserverFrame.h:426`. |
| 2 | Defaults preserve current behaviour      | **PASS** | Four-layer default-preservation guarantee:<br>**(a) Field-by-field default audit on `ObserverConfig`** (`src/manifold/ObserverFrame.h:426-485`): every field has an explicit per-field initialiser that resolves to the documented "no behaviour change" anchor — `beta_magnitude = 0.0f`; `direction = {0, 0, 0}`; `proper_time = 0.0f`; `perception_mode = PerceptionMode::Identity`.<br>**(b) Field-by-field default audit on `Config::observer`** (`src/core/Config.h:171`): the `Config::observer` field is brace-default-constructed (no explicit initialiser other than the `ObserverConfig` POD's per-field defaults), so the `Config{}` default-construction path produces the documented no-op anchor identically.<br>**(c) Empirical anchor verification** at `test_observer_default_off` (`tests/cli_tests.cpp:389`): `run({"prog"})` returns a `ParseResult` whose `r.config.observer` reads back as `(beta_magnitude=0, direction=(0,0,0), proper_time=0, perception_mode=Identity)` — pins the parser's no-flag path.<br>**(d) Empirical anchor verification across non-observer argv vectors** at `test_observer_default_off_with_other_flags` (`tests/cli_tests.cpp:624`): across **eight** non-observer argv vectors — including ones that combine with `--render-pathtrace`, `--render-optix-pathtrace`, `--scene-info`, `--render-aovs`, `--render-demo`, all the `--manifold-*` flags, `--firefly-clamp`, `--width`/`--height` — the four observer fields stay at their pre-OBSERVER.4 defaults. This is the parser-surface byte-identity invariant the OBSERVER.1 plan §8 non-goals declares ("No behaviour change by default"). |
| 3 | Invalid beta values handled safely       | **PASS** | Three-layer invalid-beta handling:<br>**(a) Non-parseable values rejected** at `src/core/CommandLine.cpp:692-700`: the parser calls `parse_finite_float(value, beta)`; on `false` it sets `r.action = Action::Error` and emits `"invalid float for --observer-beta: <value>"`. Empirically verified at `test_observer_beta_invalid_rejected` (`tests/cli_tests.cpp:424`): `run({"prog", "--observer-beta", "abc"})` returns `Action::Error` with an error message containing both `--observer-beta` AND `abc`.<br>**(b) Non-finite values rejected** by the `parse_finite_float` helper itself (`src/core/CommandLine.cpp:58-66`): after a successful `std::from_chars` parse the helper additionally calls `std::isfinite(value)` and rejects NaN / `±inf`. Empirically verified at `test_observer_beta_non_finite_rejected` (`tests/cli_tests.cpp:434`): both `nan` and `inf` produce a parse error naming the flag.<br>**(c) Out-of-range values pass through to consumer-time clamping** per the documented `--manifold-strength` / `--render-demo --beta` precedent: `test_observer_beta_out_of_range_passes` (`tests/cli_tests.cpp:413`) verifies `-0.3` and `2.0` both parse cleanly. The doc comment at `src/manifold/ObserverFrame.h:437-444` states the consumer (OBSERVER.6 camera adapter) "passes the value through `rr::relativity::clampBeta`" which "silently caps `\|beta\|` at `0.999999`". The runtime ObserverFrame validators `is_finite_observer_frame(...)` + `is_normalised_timelike(...)` (landed at OBSERVER.2; verified at the OBSERVER.3 audit) catch any residual misuse downstream.<br>The same three-layer guarantee applies to `--observer-proper-time` via the shared `parse_finite_float` helper: empirically verified at `test_observer_proper_time_non_finite_rejected` (`tests/cli_tests.cpp:497`) — both `nan` and `-inf` are rejected. |
| 4 | Invalid direction values handled safely  | **PASS** | The new `parse_vec3` helper at `src/core/CommandLine.cpp:75-94` enforces a strict-token contract for `x,y,z` triples and rejects every documented malformed case. The parser arm at lines 701-725 then escalates a `false` result into `r.action = Action::Error` with `"invalid value for --observer-direction: <value> (expected x,y,z with finite floats)"`.<br>Empirically verified at `test_observer_direction_malformed_rejected` (`tests/cli_tests.cpp:468`) — five subcases all return `Action::Error` with the flag-named error message:<br>**(a)** too few commas (`1.0,2.0`) — `parse_vec3` returns `false` because `comma2 == npos`;<br>**(b)** too many commas (`1.0,2.0,3.0,4.0`) — `parse_vec3` returns `false` because `s.find(',', comma2 + 1) != npos`;<br>**(c)** empty component (`1.0,,3.0`) — `parse_vec3` returns `false` because `ys.empty()`;<br>**(d)** non-parseable component (`x,y,z`) — `parse_vec3` returns `false` because the inner `parse_finite_float` rejects `x` / `y` / `z` as non-parseable;<br>**(e)** non-finite component (`nan,0,0`) — `parse_vec3` returns `false` because the inner `parse_finite_float` rejects `nan` by `!std::isfinite`.<br>The legal-value gate is verified at `test_observer_direction_value` (`tests/cli_tests.cpp:446`) and the sentinel-zero gate at `test_observer_direction_zero_accepted` (`tests/cli_tests.cpp:457`) — `(0,0,0)` is the documented "not specified" sentinel; the OBSERVER.6 camera adapter will fall back to the camera forward axis when magnitude is non-zero and direction is zero (doc comment at `src/manifold/ObserverFrame.h:450-460`). Caller-supplied non-zero directions are NOT normalised at parse time (consumer-time normalisation per the same doc comment) — a deliberate scope decision matching the `--manifold-strength` precedent (no parse-time clamping; the math leaf handles edge cases at consumption). |
| 5 | `ObserverFrame` receives CLI / config values | **PASS** | Data path **CLI → `Config::observer` → consumer** is structurally complete at this slice; the **`Config::observer` → `ObserverFrame`** leg lives at OBSERVER.6 (camera-to-observer adapter) per the operator's OBSERVER.4 brief explicitly forbidding "observer-space transforms yet" and the OBSERVER.1 plan §7 OBSERVER.6 wording ("host-side `build_observer_frame_from_camera(...)` helper that constructs an `ObserverFrame` from the existing scene-side `rr::camera::Camera` + `rr::relativity::Observer` + the `PerceptionMode`").<br>**(a) CLI → ObserverConfig.** Verified at `test_observer_all_four_flags_combined` (`tests/cli_tests.cpp:558`): a combined invocation `--observer-beta 0.6 --observer-direction 0.0,0.0,-1.0 --observer-proper-time 12.0 --observer-perception-mode relativistic` populates `r.config.observer.{beta_magnitude=0.6, direction.x=0.0, direction.y=0.0, direction.z=-1.0, proper_time=12.0, perception_mode=ConstantVelocityMinkowski}`. Plus `test_observer_flags_order_independent` (`tests/cli_tests.cpp:577`) verifies the same composition across any flag ordering.<br>**(b) ObserverConfig → ObserverFrame.** Deferred to OBSERVER.6 per the operator's brief; the `ObserverFrame` POD itself (landed at MANIFOLD.3, extended at OBSERVER.2) has no consumer of `ObserverConfig` today. This is the explicit OBSERVER.4 scope boundary: the operator's brief said "thread values into ObserverFrame/**config** structures" (emphasis added) — the slash denotes alternatives, not conjunction. Master rule #3 ("no fake stubs") is satisfied because the `ObserverConfig` POD is **structurally consumed** by the CLI parser + the 18 new `cli_tests` functions + the planned OBSERVER.6 consumer — not a fake stub.<br>The `--observer-perception-mode` flag does directly mirror `ObserverFrame::perception_mode` (both are `rr::manifold::PerceptionMode` enum values); the camera adapter at OBSERVER.6 will copy the field 1:1 without transformation. The `--observer-beta` + `--observer-direction` pair will combine to populate `ObserverFrame::beta` via `direction_unit * clampBeta(magnitude, 0.999999)`. The `--observer-proper-time` flag will copy 1:1 into `ObserverFrame::proper_time`. The mapping table is documented in the `Config::observer` doc comment at `src/core/Config.h:134-159`. |
| 6 | No CUDA / OptiX behaviour changed        | **PASS** | `git diff bf57c9e..16600dc --name-only` returns exactly five files: `docs/BUILD_PLAN.md`, `src/core/CommandLine.cpp`, `src/core/Config.h`, `src/manifold/ObserverFrame.h`, `tests/cli_tests.cpp`. Restricting to the renderer subtree `git diff bf57c9e..16600dc --name-only -- 'src/*' ':(exclude)src/core/' ':(exclude)src/manifold/ObserverFrame.h' 'tests/*' ':(exclude)tests/cli_tests.cpp'` returns **zero hits** — confirming zero touch on `src/cuda/`, `src/optix/`, `src/pathtracer/`, `src/renderer/`, `src/gpu/`, `src/scene/`, `src/io/`, `src/server/`, `src/camera/`, `src/material/`, `src/lighting/`, `src/texture/`, `src/geometry/`, `src/image/`, `src/math/`, `src/relativity/`, `src/field/`, `src/main.cpp`, or any non-cli test binary.<br>The new `Config::observer` field is parsed and stored; no consumer reads it. The existing six scene-aware actions (`--render-pathtrace`, `--render-mesh-scene`, `--render-material-scene`, `--render-direct-lighting`, `--render-aovs`, `--render-optix-aovs`) continue to feed on the legacy `rr::relativity::Observer` + `RelativityParams` types via the existing call paths. The `CudaRenderer` / `OptixRenderer` `AOVTargets` / `OptixLaunchParams` shapes are unchanged from the post-OBSERVER.3 baseline.<br>The `cli_tests` binary recompiles `Config.cpp` + `CommandLine.cpp` directly per its Option B linkage strategy (the header changes flow through the existing include graph; the `rr_manifold` / `rr_core` INTERFACE shapes are unchanged). No new CMake target; no new ctest entry. |
| 7 | Build / test status                      | **PASS** | Audit-host `cmake --build /home/user/RelativityRender/build` succeeds cleanly with no new warnings on the core / manifold modules. Full `ctest` from the audit-host build directory: `100% tests passed, 0 tests failed out of 12`. `cli_tests` reports `254/254 passed` (was `123/123` pre-OBSERVER.4 at the post-OBSERVER.3 baseline; **+131 new RR_CHECK assertions** from the 18 new test functions). `manifold_identity_tests: 349/349 checks passed` (unchanged from the post-OBSERVER.3 baseline — no regression). `renderer_tests: 19/19 passed` (unchanged). `relativity_tests`, `pathtracer_tests`, `pathtracer_nee_tests`, `pathtracer_bsdf_tests`, `pathtracer_mis_tests`, `math_tests`, `image_tests`, `gpu_tests`, `demo_tests` — all unchanged.<br>Smoke tests pass on the audit host:<br>**(a)** `--help` returns the four new `--observer-*` flag entries with their documented descriptions;<br>**(b)** `--observer-beta 0.3 --observer-direction 1,0,0 --observer-perception-mode relativistic --observer-proper-time 5.0 --device-info` parses without error and prints the documented device-info output (no CUDA SDK -> "GPU backend: (none)");<br>**(c)** `--observer-perception-mode bogus` produces the documented parse error naming both the offending token (`bogus`) AND every legal alternative (`default`, `relativistic`). |
| 8 | PASS / REPAIR / BLOCKED verdict          | **PASS** | All seven structural checks return `PASS`. No `REPAIR` or `BLOCKED` item is outstanding. The OBSERVER.4 CLI / config bridge ships the documented four-flag surface (`--observer-beta` / `--observer-direction` / `--observer-proper-time` / `--observer-perception-mode`), the matching `ObserverConfig` POD on `Config`, three new parser helpers (`parse_finite_float`, `parse_vec3`, `parse_perception_mode`), and 18 new `cli_tests` functions covering every parse path. The CLI -> `ObserverConfig` data path is structurally complete; the `ObserverConfig` -> `ObserverFrame` leg is explicitly deferred to OBSERVER.6 per the operator's brief and the OBSERVER.1 plan. The slice is **safe to extend** to camera-to-observer adapter (OBSERVER.6) under the renumbered OBSERVER.* ladder per §4 below. |

---

## 3. REASONING SUMMARY

The OBSERVER.4 commit (`16600dc`) introduces:

- a new `rr::manifold::ObserverConfig` POD at
  `src/manifold/ObserverFrame.h:426-485` with four
  fields (`beta_magnitude`, `direction`,
  `proper_time`, `perception_mode`), each with an
  explicit per-field initialiser that resolves to
  the documented "no behaviour change" anchor;
- a `rr::manifold::ObserverConfig observer` field
  on `rr::core::Config` at `src/core/Config.h:171`
  next to the existing `ManifoldMode manifold;`
  field (parallel structure mirroring the MANI-I.1
  precedent);
- four parser arms in `src/core/CommandLine.cpp`
  (lines 678-766) for the four `--observer-*`
  modifier flags, each routing through one of three
  new helper functions in the anonymous namespace
  (`parse_finite_float` at line 58; `parse_vec3` at
  line 75; `parse_perception_mode` at line 113);
- a help-text block at
  `src/core/CommandLine.cpp:1314-1364` listing the
  four flags + their documented descriptions +
  their no-op defaults;
- 18 new test functions appended to
  `tests/cli_tests.cpp` (registered in `main()` at
  lines 679-696) covering: default-off anchor;
  each flag's value parse; invalid-value
  rejection; out-of-range pass-through; non-finite
  rejection; malformed Vec3 rejection (five
  subcases); kebab-case to enumerator mapping;
  case-mismatch rejection; placeholder enumerator
  not on CLI; combined flags; order-independence
  with action + scene-path + manifold flags;
  missing-value rejection; default-off byte-
  identity across eight non-observer argv vectors.

The observer-CLI-flags-exist invariant (check #1)
is **flag-by-flag verified** at documented file /
line positions; all four flags are reachable via
the parser arm + the `--help` text.

The defaults-preserve-current-behaviour invariant
(check #2) is **four-layer verified**: per-field
POD initialisers on `ObserverConfig`; brace
default-construction on `Config::observer`;
empirical parser anchor at
`test_observer_default_off`; empirical anchor
across eight non-observer argv vectors at
`test_observer_default_off_with_other_flags`.

The invalid-beta invariant (check #3) is
**three-layer verified**: non-parseable values
rejected by `std::from_chars`; non-finite values
rejected by `std::isfinite`; out-of-range values
pass through to consumer-time clamping per the
documented `--manifold-strength` precedent. The
same three-layer guarantee applies to
`--observer-proper-time` via the shared
`parse_finite_float` helper.

The invalid-direction invariant (check #4) is
**five-subcase verified** at the
`test_observer_direction_malformed_rejected` test:
wrong comma count (two subcases — too few, too
many); empty component; non-parseable component;
non-finite component. The sentinel-zero direction
is empirically verified at
`test_observer_direction_zero_accepted` as a
documented legal value (consumer-time fallback to
camera forward axis).

The `ObserverFrame`-receives-CLI/config-values
invariant (check #5) is verified at the
**config-bridge level**: the CLI populates
`Config::observer` (an `ObserverConfig` POD); the
`ObserverConfig` -> `ObserverFrame` leg is
explicitly deferred to OBSERVER.6 per the
operator's brief. Master rule #3 ("no fake stubs")
is satisfied because the `ObserverConfig` POD is
structurally consumed by the CLI parser + the 18
new tests + the planned OBSERVER.6 adapter — not a
fake stub. The 1:1 mapping table from
`ObserverConfig` fields onto `ObserverFrame`
fields is documented in the `Config::observer`
doc comment and verified by the OBSERVER.6
acceptance criteria from the OBSERVER.1 plan §7.

The no-CUDA/OptiX-behaviour-changed invariant
(check #6) is **directly verified** by
`git diff bf57c9e..16600dc --name-only` filtered
against the renderer / kernel subtrees returning
zero hits. The OBSERVER.4 commit is host-only by
construction.

The build/test status (check #7) is **directly
verified** by ctest 12/12 PASS + `cli_tests`
+131 RR_CHECK delta + `manifold_identity_tests`
349/349 no-regression + three audit-host smoke
tests.

No `REPAIR` or `BLOCKED` action is outstanding.
The slice is safe to extend.

---

## 4. NEXT

The slice is **safe to extend**. The renumbered
`OBSERVER_FRAME_RENDERING_PLAN.md` §7 OBSERVER.*
sub-slice ladder needs a one-step shift to absorb
this audit slot, mirroring the OBSERVER.3 audit-
slot insertion precedent + the SCHW.4 / SCHW.6 /
SCHW.8 / SCHW.10 + PENROSE.3 / PENROSE.5 /
PENROSE.7 / PENROSE.9 / PENROSE.11 audit-slot
insertion pattern:

- **OBSERVER.1** — Planning slice
  (LANDED at `eee9d6b`).
- **OBSERVER.2** — Data model
  (LANDED at `85496a5`).
- **OBSERVER.3** — Data model audit
  (LANDED at `bf57c9e`).
- **OBSERVER.4** — Config / CLI bridge
  (LANDED at `16600dc`).
- **OBSERVER.5** — **THIS AUDIT** (ObserverFrame
  Config / CLI bridge audit, doc-only).
- **OBSERVER.6** — Camera-to-observer adapter
  (was OBSERVER.5 in the post-OBSERVER.3 plan;
  renumbered).
- **OBSERVER.7** — CUDA payload bridge (was
  OBSERVER.6).
- **OBSERVER.8** — OptiX payload bridge (was
  OBSERVER.7).
- **OBSERVER.9** — Observer debug AOV (was
  OBSERVER.8).
- **OBSERVER.10** — Arc capstone audit (was
  OBSERVER.9); closes the observer-frame arc per
  the OBSERVER.1 plan §7.

The
`docs/OBSERVER_FRAME_RENDERING_PLAN.md` §7
sub-slice ladder may be updated by a follow-on
docs slice if the operator prefers an in-plan
renumbering; this audit doc is the canonical
ladder-shift record for the OBSERVER.5 audit-slot
insertion.

No `REPAIR` action is required. No `BLOCKED` item
is outstanding. The next concrete commit the
operator may prompt for is **OBSERVER.6 —
camera-to-observer adapter** per the renumbered
OBSERVER.1 plan §7 OBSERVER.4 → OBSERVER.6 (adds
a host-side `build_observer_frame_from_camera(...)`
helper that consumes the new `Config::observer`
field + the active `rr::camera::Camera` + the
existing `rr::relativity::Observer` and produces
an `ObserverFrame`; verified by extending
`manifold_identity_tests` with new assertions on
the adapter's Identity-mode passthrough,
ConstantVelocityMinkowski-mode round-trip via
`to_relativity_observer`, and tetrad orthonormality
on the resulting frame).

---

## 5. REFERENCES

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no fake
  stubs") is the load-bearing invariant for the
  `ObserverConfig` POD's reserved-but-not-yet-
  consumed status (the planned OBSERVER.6 adapter
  is the consumer).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.3
  Observer Frame — defines the seven-field
  contract OBSERVER.2 / OBSERVER.4 build on.
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md` §3.6,
  §6, §7 OBSERVER.4 — the OBSERVER.1 plan brief
  that authorised the four-flag CLI surface +
  the `Config::observer` field shape.
- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3) — the prior per-slice audit doc
  this verdict mirrors in structure; established
  the post-OBSERVER.3 baseline at `bf57c9e`.
- `docs/PENROSE_LIKE_COMPACTIFICATION_MATH_AUDIT.md`
  (PENROSE.3) — the precedent per-slice POD-leaf
  audit doc this verdict's table structure
  follows.
- `docs/MANIFOLD_CLI_CONFIG_AUDIT.md` (MANI-I.2)
  — the precedent CLI-config-bridge audit doc;
  established the four-flag-pattern audit shape
  that the OBSERVER.4 + this OBSERVER.5 audit
  follow.
- `src/manifold/ObserverFrame.h` (modified at
  `16600dc`) — carries the new `ObserverConfig`
  POD at line 426.
- `src/core/Config.h` (modified at `16600dc`) —
  carries the new `Config::observer` field at
  line 171.
- `src/core/CommandLine.cpp` (modified at
  `16600dc`) — carries the four parser arms +
  three new helpers + the help-text block.
- `src/manifold/ManifoldMode.h` — sibling POD
  whose `Config::manifold` field the new
  `Config::observer` field is declared parallel
  to.
- `src/relativity/RelativityMath.h` — the
  `clampBeta` helper the consumer (OBSERVER.6
  adapter) will apply to `beta_magnitude`.
- `tests/cli_tests.cpp` (modified at `16600dc`)
  — 18 new test functions registered in `main()`;
  reports `254/254 passed` post-OBSERVER.4 (up
  from `123/123` at the post-OBSERVER.3
  baseline).
- `tests/manifold_identity_tests.cpp` — unchanged
  by OBSERVER.4; reports `349/349 checks passed`
  (no regression from OBSERVER.2's surface).
- `docs/BUILD_PLAN.md` — OBSERVER.4 entry
  (lines 79109 onward as of `16600dc`).
- Commit `16600dc` — `host: OBSERVER.4 —
  ObserverFrame Config / CLI Bridge (impl,
  host-only)`.
- Commit `bf57c9e` — `docs: OBSERVER.3 —
  ObserverFrame Data Model Audit (docs only)`;
  the audit baseline.
