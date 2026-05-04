#!/usr/bin/env python3
"""CUDA-host verification runner.

CUDA-H.2 shipped the skeleton (argparse + subprocess + structured
result collection). CUDA-H.3 extends it with:

- An optional build phase (``cmake -S . -B build ...`` plus
  ``cmake --build build -j``). Disable with ``--skip-build``.
- A binary-discovery pass that runs AFTER the build phase so the
  freshly-built binary is the one used for subsequent commands.
- A ``--device-info`` output analyzer that records two signals
  (``cuda_device_present``, ``no_critical_errors``) in memory.
- A separate ``--build-timeout`` so the long compile step can
  use a different timeout from the render-command timeout
  (master rule: every subprocess call has a timeout; the runner
  must not hang).

Future CUDA-H.x slices populate the render-command catalogue
per ``docs/CUDA_HOST_VERIFICATION_PLAN.md``.

Per the CUDA-H.x contract:

- argparse exposes ``--optix`` and ``--timeout`` plus minimal
  build-discovery flags so the runner can find / build the
  binary, plus build-phase flags (``--skip-build``,
  ``--build-timeout``, ``--source-dir``, ``--optix-root``).
- ``run_command(...)`` runs each ``Command`` via
  ``subprocess.run`` with a per-command timeout, captures
  stdout/stderr, and returns a ``CommandResult`` POD.
- The runner never calls ``--server`` (rule).
- The runner never modifies the renderer source (rule).
- The runner never hard-codes long-running RENDER commands;
  the only baked-in commands are the build phase
  (``cmake configure`` + ``cmake --build``) and the
  ``--device-info`` smoke. Render commands per the
  verification plan land in subsequent CUDA-H.x slices.

Python 3.10+ required (uses PEP-604 union types in dataclass
annotations and modern ``typing`` features).

Usage::

    python3 tools/verify_cuda_host.py
    python3 tools/verify_cuda_host.py --skip-build
    python3 tools/verify_cuda_host.py --optix \\
            --optix-root /opt/optix
    python3 tools/verify_cuda_host.py --timeout 120 \\
            --build-timeout 1200

Exit code is 0 when every command in the active set passes, 1
otherwise.
"""

from __future__ import annotations

import argparse
import dataclasses
import subprocess
import sys
import time
from collections.abc import Iterable
from pathlib import Path


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class Command:
    """A single verification command.

    ``name`` is a short, stable identifier used in the summary
    table and any future log/result file. ``argv`` is the list of
    arguments to pass to the program (the program itself is
    prepended by the runner). ``program`` is an optional override:
    when ``None`` (default), the runner uses the discovered
    RelativityRender binary; when set (e.g. ``Path("cmake")``),
    the runner uses that program directly. ``timeout_override``
    is an optional per-command timeout that wins over the
    runner-level default (used for the build phase, which
    legitimately takes longer than a single render command).
    ``expected_outputs`` is reserved for future CUDA-H.x slices
    that will verify output-file existence after the command
    exits.
    """

    name: str
    argv: list[str]
    program: Path | None = None
    timeout_override: float | None = None
    expected_outputs: list[Path] = dataclasses.field(default_factory=list)


@dataclasses.dataclass
class CommandResult:
    """Outcome of running a single ``Command``.

    ``status`` is one of:

    - ``"pass"``: process exit code 0 within the timeout AND
      every expected output file exists with size > 0.
    - ``"fail"``: non-zero exit code OR an expected output
      file is missing / empty (CUDA-H.4 file-check downgrade).
    - ``"timeout"``: process exceeded the per-command timeout.
    - ``"error"``: runner-side failure (binary missing, OS
      error).
    - ``"skipped"``: command was deliberately not executed
      (e.g. an OptiX entry when ``--optix`` is not set).
      CUDA-H.8 surfaces these in the summary so the operator
      sees what would have been verified if the relevant
      build flag had been enabled. Skipped commands do NOT
      count as failures for the runner's exit code.

    ``stdout`` / ``stderr`` are captured strings (decoded UTF-8
    with ``errors="replace"`` so malformed bytes never blow up
    the runner).

    ``missing_outputs`` (CUDA-H.4) lists any expected output
    paths that are absent or zero-byte after the command
    completes. Empty when ``cmd.expected_outputs`` is empty
    OR every expected file is present + non-empty. The runner
    populates this in ``_run_command_list`` after a "pass"
    subprocess result, then downgrades ``status`` to "fail"
    if the list is non-empty.
    """

    name: str
    argv: list[str]
    status: str
    returncode: int | None
    duration_s: float
    stdout: str
    stderr: str
    missing_outputs: list[Path] = dataclasses.field(default_factory=list)


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------


def run_command(
    binary: Path | None,
    cmd: Command,
    timeout_s: float,
    cwd: Path,
) -> CommandResult:
    """Run a single ``Command`` via ``subprocess.run``.

    Captures stdout + stderr; enforces ``cmd.timeout_override``
    when set, otherwise ``timeout_s``. Never raises on a non-zero
    exit; reflects every failure mode in the returned
    ``CommandResult.status``.

    ``binary`` is used as argv[0] when the command does not
    override it via ``cmd.program``. When ``cmd.program`` is set
    (e.g., for ``cmake`` build commands) the binary parameter
    can be ``None``.
    """

    program = cmd.program if cmd.program is not None else binary
    if program is None:
        return CommandResult(
            name=cmd.name,
            argv=list(cmd.argv),
            status="error",
            returncode=None,
            duration_s=0.0,
            stdout="",
            stderr=(
                "runner error: no program (binary not discovered "
                "and no Command.program override)"
            ),
        )

    effective_timeout = (
        cmd.timeout_override if cmd.timeout_override is not None
        else timeout_s
    )
    argv = [str(program), *cmd.argv]
    start = time.monotonic()
    try:
        completed = subprocess.run(  # noqa: S603 (caller-provided argv)
            argv,
            cwd=str(cwd),
            capture_output=True,
            text=True,
            timeout=effective_timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        return CommandResult(
            name=cmd.name,
            argv=argv,
            status="timeout",
            returncode=None,
            duration_s=time.monotonic() - start,
            stdout=(exc.stdout or "") if isinstance(exc.stdout, str) else "",
            stderr=(exc.stderr or "") if isinstance(exc.stderr, str) else "",
        )
    except (FileNotFoundError, PermissionError, OSError) as exc:
        return CommandResult(
            name=cmd.name,
            argv=argv,
            status="error",
            returncode=None,
            duration_s=time.monotonic() - start,
            stdout="",
            stderr=f"runner error: {exc}",
        )

    return CommandResult(
        name=cmd.name,
        argv=argv,
        status="pass" if completed.returncode == 0 else "fail",
        returncode=completed.returncode,
        duration_s=time.monotonic() - start,
        stdout=completed.stdout,
        stderr=completed.stderr,
    )


# ---------------------------------------------------------------------------
# Command catalogues
# ---------------------------------------------------------------------------


def make_build_commands(
    args: argparse.Namespace,
) -> list[Command]:
    """The build phase: ``cmake configure`` + ``cmake --build``.

    Mirrors ``docs/CUDA_HOST_VERIFICATION_PLAN.md`` §1.

    The configure step always passes
    ``-DCMAKE_BUILD_TYPE=Release``, ``-DRR_ENABLE_CUDA=ON``,
    ``-DRR_BUILD_TESTS=ON``. When ``args.optix`` is set, also
    passes ``-DRR_ENABLE_OPTIX=ON`` (and ``-DOPTIX_ROOT=...``
    when ``args.optix_root`` is set).

    Both build commands carry ``timeout_override =
    args.build_timeout`` so they don't share the per-render
    ``args.timeout`` (build legitimately takes longer than a
    single render command).
    """

    configure_argv = [
        "-S", str(args.source_dir),
        "-B", str(args.build_dir),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DRR_ENABLE_CUDA=ON",
        "-DRR_BUILD_TESTS=ON",
    ]
    if args.optix:
        configure_argv.append("-DRR_ENABLE_OPTIX=ON")
        if args.optix_root is not None:
            configure_argv.append(
                f"-DOPTIX_ROOT={args.optix_root}"
            )

    return [
        Command(
            name="cmake-configure",
            argv=configure_argv,
            program=Path("cmake"),
            timeout_override=args.build_timeout,
        ),
        Command(
            name="cmake-build",
            argv=["--build", str(args.build_dir), "-j"],
            program=Path("cmake"),
            timeout_override=args.build_timeout,
        ),
    ]


def base_commands() -> list[Command]:
    """The default command set.

    CUDA-H.2 shipped just ``--device-info`` (the fast smoke).
    CUDA-H.3 added the build phase + device-info analyzer
    around it. CUDA-H.4 added the four core CUDA render
    commands per ``docs/CUDA_HOST_VERIFICATION_PLAN.md`` §3.1
    -- §3.4. CUDA-H.5 added the scene-render + texture
    commands per §3.5 -- §3.6. CUDA-H.6 added the AOV pass
    per §3.7. CUDA-H.7 adds the CUDA path tracer per §3.8:

    - ``--render-gradient``               -> ``output/gpu_gradient.ppm``
    - ``--render-rays``                   -> ``output/gpu_camera_rays.ppm``
    - ``--render-sphere``                 -> ``output/gpu_sphere.ppm``
    - ``--render-relativistic``           -> four PPMs at fixed beta
      values (``output/sphere_beta_{000,025,075,095}.ppm``).
    - ``--render scenes/test_spheres.rrscene
       --output output/render.ppm``      -> ``output/render.ppm``
      (per the verification plan's literal CLI shape; the
      `--output` argument is redundant with `run_render`'s
      "output/render.ppm" default but kept explicit for
      clarity).
    - ``--render-texture-sample-test``    -> ``output/gpu_texture_sample_test.ppm``
    - ``--render-textured-material``      -> ``output/gpu_textured_material.ppm``
    - ``--render-aovs``                   -> six PPMs in one
      invocation (``output/aov_{beauty,normal,depth,albedo,
      doppler,searchlight}.ppm``).
    - ``--render-pathtrace``              -> two PPMs in one
      invocation (``output/pathtrace_spp_1.ppm`` +
      ``output/pathtrace_spp_16.ppm``). CUDA path tracer; no
      OptiX requirement; no denoiser.

    Each entry carries the expected output paths in
    ``Command.expected_outputs``; the runner verifies file
    existence + ``size > 0`` after the command completes
    (CUDA-H.4 contract). Future CUDA-H.x slices add the
    OptiX commands.
    """

    return [
        Command(name="device-info", argv=["--device-info"]),
        Command(
            name="render-gradient",
            argv=["--render-gradient"],
            expected_outputs=[Path("output/gpu_gradient.ppm")],
        ),
        Command(
            name="render-camera-rays",
            argv=["--render-rays"],
            expected_outputs=[Path("output/gpu_camera_rays.ppm")],
        ),
        Command(
            name="render-sphere",
            argv=["--render-sphere"],
            expected_outputs=[Path("output/gpu_sphere.ppm")],
        ),
        Command(
            name="render-relativistic",
            argv=["--render-relativistic"],
            expected_outputs=[
                Path("output/sphere_beta_000.ppm"),
                Path("output/sphere_beta_025.ppm"),
                Path("output/sphere_beta_075.ppm"),
                Path("output/sphere_beta_095.ppm"),
            ],
        ),
        Command(
            name="render-scene-spheres",
            argv=[
                "--render",
                "scenes/test_spheres.rrscene",
                "--output",
                "output/render.ppm",
            ],
            expected_outputs=[Path("output/render.ppm")],
        ),
        Command(
            name="render-texture-sample-test",
            argv=["--render-texture-sample-test"],
            expected_outputs=[
                Path("output/gpu_texture_sample_test.ppm"),
            ],
        ),
        Command(
            name="render-textured-material",
            argv=["--render-textured-material"],
            expected_outputs=[
                Path("output/gpu_textured_material.ppm"),
            ],
        ),
        Command(
            name="render-aovs",
            argv=["--render-aovs"],
            expected_outputs=[
                Path("output/aov_beauty.ppm"),
                Path("output/aov_normal.ppm"),
                Path("output/aov_depth.ppm"),
                Path("output/aov_albedo.ppm"),
                Path("output/aov_doppler.ppm"),
                Path("output/aov_searchlight.ppm"),
            ],
        ),
        Command(
            name="render-pathtrace",
            # `--render-pathtrace` requires a scene-file
            # argument (per src/core/CommandLine.cpp); pass the
            # standard multi-light fixture scene the CUDA path
            # tracer was authored against.
            argv=[
                "--render-pathtrace",
                "scenes/test_full_scene.rrscene",
            ],
            expected_outputs=[
                Path("output/pathtrace_spp_1.ppm"),
                Path("output/pathtrace_spp_16.ppm"),
            ],
        ),
    ]


def optix_commands() -> list[Command]:
    """OptiX command set per CUDA-H.8.

    CUDA-H.2 left this as an empty placeholder. CUDA-H.8
    populates it with the three OptiX entries the user
    specifically called out (raygen, triangle, pathtrace).
    Other OptiX commands listed in the verification plan §4
    (test, relativity, mesh / material / lighting / shadow,
    textured, AOVs, denoise) are deferred to future slices
    if the operator wants them.

    The runner only RUNS these when ``--optix`` is set; when
    not set, ``main()`` substitutes a SKIPPED ``CommandResult``
    for each via ``make_skipped_results(...)`` so the
    operator still sees what would have been verified.
    """

    return [
        Command(
            name="render-optix-raygen",
            argv=["--render-optix-raygen"],
            expected_outputs=[Path("output/optix_raygen.ppm")],
        ),
        Command(
            name="render-optix-triangle",
            argv=["--render-optix-triangle"],
            expected_outputs=[Path("output/optix_triangle.ppm")],
        ),
        Command(
            name="render-optix-pathtrace",
            # `--render-optix-pathtrace` requires a scene-file
            # argument (per src/core/CommandLine.cpp:321; same
            # contract as the CUDA-side `--render-pathtrace`).
            argv=[
                "--render-optix-pathtrace",
                "scenes/test_full_scene.rrscene",
            ],
            expected_outputs=[
                Path("output/optix_pathtrace_spp1.ppm"),
                Path("output/optix_pathtrace_spp16.ppm"),
            ],
        ),
    ]


def make_skipped_results(
    commands: list[Command],
    reason: str,
) -> list[CommandResult]:
    """Synthesize a SKIPPED CommandResult per command.

    Used by main() when ``--optix`` is not set: the OptiX
    commands are still surfaced in the summary so the
    operator sees them as SKIPPED, with the reason recorded
    in ``stderr`` for clarity. Skipped results do NOT count
    as failures (per CUDA-H.8 "Must not fail when OptiX is
    OFF").
    """

    return [
        CommandResult(
            name=cmd.name,
            argv=list(cmd.argv),
            status="skipped",
            returncode=None,
            duration_s=0.0,
            stdout="",
            stderr=f"[runner] skipped: {reason}",
        )
        for cmd in commands
    ]


# ---------------------------------------------------------------------------
# Output analysis
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class DeviceInfoSignals:
    """In-memory record of the ``--device-info`` output analysis.

    Two boolean signals per the CUDA-H.3 prompt:

    - ``cuda_device_present``: stdout/stderr contains a CUDA
      device row (``"[N] <name> (sm_..."``) AND the negative
      "No CUDA-capable devices visible" line is absent.
    - ``no_critical_errors``: stdout/stderr does not contain
      any ``"[ERROR]"`` log line AND the underlying command's
      return code is 0.

    The dataclass is recorded in memory only (no file write
    yet per the CUDA-H.3 contract); future CUDA-H.x slices
    can persist the signals to a result file.
    """

    cuda_device_present: bool
    no_critical_errors: bool


def analyze_device_info(result: CommandResult) -> DeviceInfoSignals:
    """Parse a ``--device-info`` ``CommandResult`` for two signals.

    Detection rules:

    - A device row is recognised by the ``"[<index>] <name>
      (sm_<cc>, ..."`` pattern emitted by
      ``run_render_device_info`` in ``src/main.cpp``.
    - The negative "No CUDA-capable devices visible" line is
      the documented "no GPU detected" sentinel emitted by
      the same function on a CUDA-disabled or empty-driver
      host.
    - Critical errors are matched against the project's
      ``[ERROR]`` Logger prefix (used by every dispatcher's
      hard-failure path).

    Returns the two boolean signals as a ``DeviceInfoSignals``
    POD; callers decide how to act on them (a CUDA-host
    verification PASS requires both true).
    """

    text = (result.stdout or "") + "\n" + (result.stderr or "")

    has_no_cuda_line = "No CUDA-capable devices visible" in text
    # Match a "[0] <name> (sm_..." line emitted per device.
    has_device_row = bool(_DEVICE_ROW_RE.search(text))
    cuda_device_present = has_device_row and not has_no_cuda_line

    has_error_line = "[ERROR]" in text
    no_critical_errors = (
        not has_error_line and result.returncode == 0
    )

    return DeviceInfoSignals(
        cuda_device_present=cuda_device_present,
        no_critical_errors=no_critical_errors,
    )


# Pattern: a left-bracketed integer index, a name, and "(sm_<cc>"
# anywhere in the same line. Matches the
# `run_render_device_info` output:
#     "[0] NVIDIA GeForce RTX 4090 (sm_89, 24 GB, 128 SMs)"
import re  # noqa: E402  (kept near use site for readability)
_DEVICE_ROW_RE = re.compile(
    r"\[\s*\d+\s*\][^\n]*\(sm_\d+",
    re.MULTILINE,
)


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------


def _format_status(status: str) -> str:
    return status.upper().rjust(8)


def print_summary(results: Iterable[CommandResult]) -> None:
    """Print a tabular summary of the result list to stdout."""

    results_list = list(results)
    if not results_list:
        print("no commands ran")
        return

    print()
    print("=" * 72)
    print(f"{'name':<32} {'status':>8} {'rc':>4} {'time':>8}")
    print("-" * 72)
    for r in results_list:
        rc = "-" if r.returncode is None else str(r.returncode)
        time_str = f"{r.duration_s:.2f}s"
        print(
            f"{r.name:<32} {_format_status(r.status):>8} "
            f"{rc:>4} {time_str:>8}"
        )
    print("=" * 72)

    by_status: dict[str, int] = {}
    for r in results_list:
        by_status[r.status] = by_status.get(r.status, 0) + 1
    summary_parts = [f"{count} {name}" for name, count in
                     sorted(by_status.items())]
    print("totals: " + ", ".join(summary_parts))


def dump_failure(result: CommandResult) -> None:
    """Print the captured stdout/stderr for a failed command."""

    print(f"\n--- {result.name} ({result.status}) ---")
    print(f"argv: {result.argv}")
    if result.returncode is not None:
        print(f"returncode: {result.returncode}")
    if result.stdout:
        print("stdout:")
        print(result.stdout.rstrip())
    if result.stderr:
        print("stderr:")
        print(result.stderr.rstrip())


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "CUDA-host verification runner skeleton (CUDA-H.2). "
            "Runs RelativityRender against the canonical CLI smoke set "
            "and reports a structured pass/fail summary."
        ),
    )
    parser.add_argument(
        "--optix",
        action="store_true",
        help=(
            "Additionally run OptiX commands (requires the build to "
            "have RR_ENABLE_OPTIX=ON and the OptiX SDK installed at "
            "runtime). The OptiX command list is empty in the "
            "CUDA-H.2 skeleton; future slices populate it."
        ),
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=60.0,
        help=(
            "Per-command timeout in seconds (default: 60). The "
            "runner enforces this via subprocess.run(timeout=...)."
        ),
    )
    parser.add_argument(
        "--bin",
        type=Path,
        default=None,
        help=(
            "Path to the RelativityRender binary. Default: "
            "auto-discover under build/bin/RelativityRender."
        ),
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=Path("build"),
        help=(
            "Build directory used for binary auto-discovery "
            "(default: build)."
        ),
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help=(
            "Repository root used as the cwd when running each "
            "command (default: parent of this script's directory)."
        ),
    )
    parser.add_argument(
        "--show-stdout",
        action="store_true",
        help=(
            "Always print captured stdout/stderr, not just on "
            "failure."
        ),
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help=(
            "Skip the build phase. Default: run "
            "'cmake configure' + 'cmake --build' before "
            "running the command set."
        ),
    )
    parser.add_argument(
        "--build-timeout",
        type=float,
        default=600.0,
        help=(
            "Per-command timeout for the build phase in "
            "seconds (default: 600). Separate from --timeout "
            "because compile times legitimately exceed render "
            "timeouts."
        ),
    )
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help=(
            "Source directory passed as 'cmake -S <source-dir>' "
            "(default: parent of this script's directory)."
        ),
    )
    parser.add_argument(
        "--optix-root",
        type=Path,
        default=None,
        help=(
            "Path to the OptiX SDK install (passed as "
            "'-DOPTIX_ROOT=<path>'). Only used when --optix is "
            "set. Default: leave unset and let cmake auto-"
            "detect / fall through to the audit-host fallback."
        ),
    )
    return parser.parse_args(argv)


def discover_binary(args: argparse.Namespace) -> Path | None:
    if args.bin is not None:
        return args.bin if args.bin.is_file() else None
    candidate = args.build_dir / "bin" / "RelativityRender"
    return candidate if candidate.is_file() else None


def check_output_files(
    cmd: Command,
    cwd: Path,
) -> list[Path]:
    """Return the subset of ``cmd.expected_outputs`` that are
    missing or empty.

    Each expected path is resolved relative to ``cwd`` (the
    runner's working directory; typically the repo root so
    paths like ``output/gpu_gradient.ppm`` resolve as
    expected).

    A path is "missing" when ``Path.is_file()`` is False; it
    is "empty" when ``Path.stat().st_size == 0``. Both cases
    end up in the returned list; callers decide how to act
    on each. Per the CUDA-H.4 contract: file size > 0 is
    sufficient (no PPM-magic / format check yet; that is a
    future slice).
    """

    missing: list[Path] = []
    for rel in cmd.expected_outputs:
        absolute = (cwd / rel) if not rel.is_absolute() else rel
        try:
            if not absolute.is_file() or absolute.stat().st_size == 0:
                missing.append(rel)
        except OSError:
            # stat() failure (race with deletion, permissions,
            # etc.) treats the path as missing.
            missing.append(rel)
    return missing


def _run_command_list(
    binary: Path | None,
    commands: list[Command],
    args: argparse.Namespace,
    halt_on_failure: bool,
) -> list[CommandResult]:
    """Run a list of commands sequentially, collecting results.

    When ``halt_on_failure`` is true, the loop stops at the
    first non-pass result so subsequent steps (e.g. render
    commands after a failed build) don't fan out into a flood
    of meaningless follow-up failures.

    After each subprocess returns "pass", the runner verifies
    every entry in ``cmd.expected_outputs`` exists with
    ``size > 0`` (CUDA-H.4); if any is missing/empty the
    result's ``status`` is downgraded to "fail" and the
    missing paths are recorded in
    ``CommandResult.missing_outputs``.
    """

    results: list[CommandResult] = []
    for cmd in commands:
        result = run_command(
            binary=binary,
            cmd=cmd,
            timeout_s=args.timeout,
            cwd=args.repo_root,
        )

        # CUDA-H.4: post-run output-file verification. Only
        # check when the subprocess itself reported "pass"
        # (a failed subprocess hasn't had a chance to write
        # files; reporting "missing outputs" on top of the
        # subprocess error would be noisy).
        if result.status == "pass" and cmd.expected_outputs:
            missing = check_output_files(cmd, args.repo_root)
            if missing:
                result.missing_outputs = missing
                result.status = "fail"
                paths_str = ", ".join(str(p) for p in missing)
                # Append to stderr so dump_failure shows it.
                addendum = (
                    f"\n[runner] expected output file(s) "
                    f"missing or empty: {paths_str}"
                )
                result.stderr = (result.stderr or "") + addendum

        results.append(result)
        marker = (
            "OK" if result.status == "pass"
            else result.status.upper()
        )
        print(f"  [{marker}] {cmd.name} ({result.duration_s:.2f}s)")
        if args.show_stdout or result.status != "pass":
            dump_failure(result)
        if halt_on_failure and result.status != "pass":
            break
    return results


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    print(f"cwd     : {args.repo_root}")
    print(f"timeout : {args.timeout:.1f}s per command "
          f"(build: {args.build_timeout:.1f}s)")

    all_results: list[CommandResult] = []

    # ---- Build phase --------------------------------------
    # Halt on failure: a broken build means subsequent render
    # commands would all fail with "binary not found" or stale
    # binary errors. The runner reports the build failure +
    # exits non-zero so the operator can fix the build first.
    if not args.skip_build:
        build_cmds = make_build_commands(args)
        print(f"build   : {len(build_cmds)} step(s)")
        build_results = _run_command_list(
            binary=None,
            commands=build_cmds,
            args=args,
            halt_on_failure=True,
        )
        all_results.extend(build_results)
        if any(r.status != "pass" for r in build_results):
            print_summary(all_results)
            print("\nbuild failed; skipping subsequent commands.")
            return 1
    else:
        print("build   : skipped (--skip-build)")

    # ---- Binary discovery (post-build) --------------------
    # Run AFTER the build phase so the freshly-built binary is
    # the one used for subsequent commands.
    binary = discover_binary(args)
    if binary is None:
        print(
            "error: RelativityRender binary not found after "
            "build. Pass --bin <path> or check the build "
            "output.",
            file=sys.stderr,
        )
        print_summary(all_results)
        return 1

    # ---- Render command catalogue -------------------------
    commands = base_commands()
    optix_cmds = optix_commands()
    skipped_optix: list[CommandResult] = []
    if args.optix:
        commands += optix_cmds
    elif optix_cmds:
        # CUDA-H.8: when --optix is not set, surface the OptiX
        # commands in the summary as SKIPPED with a reason
        # recorded. They do NOT count as failures (per the
        # "Must not fail when OptiX is OFF" rule).
        skipped_optix = make_skipped_results(
            optix_cmds,
            reason="--optix flag not set",
        )

    if not commands:
        print("no render commands configured; exiting after build",
              file=sys.stderr)
        all_results.extend(skipped_optix)
        print_summary(all_results)
        return 0 if all(
            r.status in ("pass", "skipped") for r in all_results
        ) else 1

    print(f"binary  : {binary}")
    print(f"commands: {len(commands)} (skipping {len(skipped_optix)} "
          f"OptiX command(s) — pass --optix to include)"
          if skipped_optix
          else f"commands: {len(commands)}")
    cmd_results = _run_command_list(
        binary=binary,
        commands=commands,
        args=args,
        halt_on_failure=False,
    )
    all_results.extend(cmd_results)
    # Append the skipped OptiX entries AFTER the run results so
    # the summary table has the executed commands first, then
    # the deliberately-skipped ones at the bottom.
    if skipped_optix:
        for sr in skipped_optix:
            print(f"  [SKIP] {sr.name} (skipped: --optix not set)")
        all_results.extend(skipped_optix)

    # ---- Device-info output analysis ----------------------
    # CUDA-H.3 records two boolean signals in memory; no file
    # write yet (next slice's concern).
    device_info = next(
        (r for r in cmd_results if r.name == "device-info"), None
    )
    signals: DeviceInfoSignals | None = None
    if device_info is not None:
        signals = analyze_device_info(device_info)

    print_summary(all_results)

    if signals is not None:
        print()
        print("device-info analysis:")
        print(f"  cuda_device_present : {signals.cuda_device_present}")
        print(f"  no_critical_errors  : {signals.no_critical_errors}")

    # Per CUDA-H.8: "skipped" is NOT a failure. Exit 0 when
    # every result is either "pass" or "skipped".
    overall_pass = all(
        r.status in ("pass", "skipped") for r in all_results
    )
    return 0 if overall_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
