#!/usr/bin/env python3
"""CUDA-host verification skeleton runner.

This is the CUDA-H.2 skeleton: argparse + subprocess + structured
result collection. The full command list from
``docs/CUDA_HOST_VERIFICATION_PLAN.md`` is intentionally NOT baked
into this skeleton; the only command run by default is the
fast, safe ``--device-info`` smoke. Subsequent CUDA-H.x slices
expand the runner with the actual render-command catalogue,
output-file existence checks, and reference-image comparisons.

Per the CUDA-H.2 contract:

- argparse exposes ``--optix`` and ``--timeout`` (plus minimal
  build-discovery flags so the runner can find the binary).
- ``run_command(...)`` runs each ``Command`` via ``subprocess.run``
  with a per-command timeout, captures stdout/stderr, and returns
  a ``CommandResult`` POD.
- The runner never calls ``--server`` (rule).
- The runner never modifies the renderer source (rule).
- The runner never hard-codes long-running render commands; only
  ``--device-info`` ships as a built-in smoke check.

Python 3.10+ required (uses PEP-604 union types in dataclass
annotations and modern ``typing`` features).

Usage::

    python3 tools/verify_cuda_host.py
    python3 tools/verify_cuda_host.py --optix
    python3 tools/verify_cuda_host.py --timeout 120
    python3 tools/verify_cuda_host.py --bin build/bin/RelativityRender

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
    arguments to pass to the RelativityRender binary (the binary
    path itself is prepended by the runner). ``expected_outputs``
    is an optional list of file paths (relative to the repo root)
    the command is expected to produce; future CUDA-H.x slices
    will use this list to verify file existence + non-emptiness
    after the command exits.
    """

    name: str
    argv: list[str]
    expected_outputs: list[Path] = dataclasses.field(default_factory=list)


@dataclasses.dataclass
class CommandResult:
    """Outcome of running a single ``Command``.

    ``status`` is one of:

    - ``"pass"``: process exit code 0 within the timeout.
    - ``"fail"``: non-zero exit code.
    - ``"timeout"``: process exceeded the per-command timeout.
    - ``"error"``: runner-side failure (binary missing, OS error).

    ``stdout`` / ``stderr`` are captured strings (decoded UTF-8
    with ``errors="replace"`` so malformed bytes never blow up
    the runner).
    """

    name: str
    argv: list[str]
    status: str
    returncode: int | None
    duration_s: float
    stdout: str
    stderr: str


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------


def run_command(
    binary: Path,
    cmd: Command,
    timeout_s: float,
    cwd: Path,
) -> CommandResult:
    """Run a single ``Command`` via ``subprocess.run``.

    Captures stdout + stderr; enforces ``timeout_s`` per command.
    Never raises on a non-zero exit; reflects every failure mode
    in the returned ``CommandResult.status``.
    """

    argv = [str(binary), *cmd.argv]
    start = time.monotonic()
    try:
        completed = subprocess.run(  # noqa: S603 (caller-provided argv)
            argv,
            cwd=str(cwd),
            capture_output=True,
            text=True,
            timeout=timeout_s,
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


def base_commands() -> list[Command]:
    """The default command set: just ``--device-info``.

    Per the CUDA-H.2 "do not hardcode long-running commands"
    rule, the skeleton only ships the fast, safe smoke. Future
    CUDA-H.x slices add the actual render commands per
    ``docs/CUDA_HOST_VERIFICATION_PLAN.md`` once the operator
    has confirmed the runner shape.
    """

    return [
        Command(name="device-info", argv=["--device-info"]),
    ]


def optix_commands() -> list[Command]:
    """OptiX command set — empty in the CUDA-H.2 skeleton.

    Future slices add commands per
    ``docs/CUDA_HOST_VERIFICATION_PLAN.md`` §4. Each entry
    will list its expected output PPMs in
    ``Command.expected_outputs`` so the runner can verify file
    existence + non-emptiness after the command exits.
    """

    return []


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
    return parser.parse_args(argv)


def discover_binary(args: argparse.Namespace) -> Path | None:
    if args.bin is not None:
        return args.bin if args.bin.is_file() else None
    candidate = args.build_dir / "bin" / "RelativityRender"
    return candidate if candidate.is_file() else None


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    binary = discover_binary(args)
    if binary is None:
        print(
            "error: RelativityRender binary not found. "
            "Pass --bin <path> or build first.",
            file=sys.stderr,
        )
        return 1

    commands = base_commands()
    if args.optix:
        commands += optix_commands()

    if not commands:
        print("no commands to run; exiting", file=sys.stderr)
        return 0

    print(f"binary  : {binary}")
    print(f"cwd     : {args.repo_root}")
    print(f"timeout : {args.timeout:.1f}s per command")
    print(f"commands: {len(commands)}")

    results: list[CommandResult] = []
    for cmd in commands:
        result = run_command(
            binary=binary,
            cmd=cmd,
            timeout_s=args.timeout,
            cwd=args.repo_root,
        )
        results.append(result)
        marker = (
            "OK" if result.status == "pass"
            else result.status.upper()
        )
        print(f"  [{marker}] {cmd.name} ({result.duration_s:.2f}s)")
        if args.show_stdout or result.status != "pass":
            dump_failure(result)

    print_summary(results)
    return 0 if all(r.status == "pass" for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
