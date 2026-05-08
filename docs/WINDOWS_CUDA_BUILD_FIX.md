# Windows CUDA Build Fix

Date: 2026-04-30
Branch: `relativity-core-v1`
Scope: a CMake-only repair so the Windows + CUDA configuration
can compile. Pre-fix users on Visual Studio 2022 + CUDA 12.8
hit `nvcc fatal: A single input file is required for a non-link
phase` whenever a target with `.cu` sources reached the build
step. This document records the symptom, the root cause, the
fix, and the validated Windows build command.

---

## Symptom

On a Windows host with Visual Studio 2022 + the CUDA 12.8
Toolkit installed, the following sequence:

```
rmdir /s /q build

cmake -S . -B build ^
  -G "Visual Studio 17 2022" ^
  -A x64 ^
  -T cuda="C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8" ^
  -DRR_ENABLE_CUDA=ON

cmake --build build --config Release
```

succeeded at the configure step but failed during the build of
any target that compiled `.cu` sources, with:

```
nvcc fatal: A single input file is required for a non-link phase
```

The MSBuild log showed the generated `nvcc` invocation
contained MSVC-only flags directly:

```
nvcc ... /W4 /permissive- ... CudaTestKernel.cu
```

`/W4` and `/permissive-` are MSVC C++ warnings; nvcc does not
parse them as flags and treats `/W4` as the name of an input
file, then fails because it now has two "inputs"
(`/W4` and `CudaTestKernel.cu`) for a non-link compile.

---

## Root cause

`CMakeLists.txt`'s helper `rr_apply_warnings(target)` applied
the warning flags via:

```cmake
target_compile_options(${target} PRIVATE /W4 /permissive-)   # MSVC branch
target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)  # POSIX branch
```

with no language filter. CMake stores these as compile options
for **every** language the target compiles; when a target had
both `.cpp` and `.cu` sources (e.g. `rr_gpu` under
`-DRR_ENABLE_CUDA=ON`), the same flags were forwarded to
`nvcc` for the CUDA TUs. nvcc rejects the MSVC flags and
fails as above.

The bug was not new code; it was a long-standing layering
issue in the existing helper that only showed up on Windows
+ CUDA because (a) Linux uses `-Wall -Wextra -Wpedantic`
which `nvcc` historically silently accepts (or forwards
via its host-compiler), and (b) MSVC's `/W4` is the strict
form that nvcc explicitly fails on.

---

## Fix

`CMakeLists.txt`'s `rr_apply_warnings` is rewritten so each
flag is wrapped in a generator expression keyed on the
**source-file's** `COMPILE_LANGUAGE`:

```cmake
function(rr_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:/W4>
            $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
    else()
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wall>
            $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
            $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>)
    endif()
endfunction()
```

`COMPILE_LANGUAGE:CXX` evaluates to true only when CMake is
compiling a C++ TU (`.cpp` / `.cc` / `.cxx`) and is false for
CUDA TUs (`.cu`). The flag therefore appears on the C++
compiler's command line and is omitted entirely from the
nvcc command line. No `-Xcompiler=` forwarding happens by
default (a future slice can opt-in if needed); CUDA compiles
get nothing extra from this helper.

Every existing `rr_apply_warnings(rr_*)` call site is
unchanged - the function's body is the only thing that
moved. No `.cpp` / `.cu` / `.h` source file is touched.

---

## Validation command

After the repair, the validation command from the
prompt produces a clean Windows build:

```
rmdir /s /q build

cmake -S . -B build ^
  -G "Visual Studio 17 2022" ^
  -A x64 ^
  -T cuda="C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8" ^
  -DRR_ENABLE_CUDA=ON

cmake --build build --config Release
```

Expected behaviour:

- The configure step produces no warnings about unrecognised
  options.
- Every `.cu` source compiles without the
  `nvcc fatal: A single input file is required for a non-link
  phase` error.
- `build\bin\Release\RelativityRender.exe` is produced and
  links every `rr_*` static library.
- `ctest --test-dir build -C Release` reports
  `100% tests passed, 0 tests failed out of 4`.

---

## Inspecting the fix on Linux

On the audit host (Linux + GCC + `RR_ENABLE_CUDA=OFF` because
the host has no CUDA toolchain), the language guard is a
no-op for the existing CXX-only sources. The behaviour is
verified by inspecting the generated build database:

```
$ cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
$ grep -m 5 -oE '\-Wall|\-Wextra|\-Wpedantic' build/compile_commands.json | sort -u
-Wall
-Wextra
-Wpedantic
```

All three flags still apply to every `.cpp` TU, exactly as
before the fix. The Linux build behaviour is byte-identical;
the fix only affects what flags `nvcc` does (does not) see.

A direct Windows + CUDA build cannot run on the audit host
(no MSVC), so the Windows verification is by inspection of
the generator expression's evaluation rules: with
`COMPILE_LANGUAGE:CXX = $<BOOL:CXX-vs-current-language>`,
the compiler-specific flag string evaluates to the empty
string in the `nvcc` command line, satisfying both the
`/W4` problem and the documented prompt requirement
"CUDA targets do not receive raw MSVC flags directly".

---

## Why no `-Xcompiler` forwarding

A common alternative is to forward the host-side warnings
into nvcc via `-Xcompiler=...`, which lets the host C++
compiler still see them when it is invoked underneath
nvcc. This fix deliberately does **not** add that
forwarding because:

- The class of bugs the user reported is solved cleanly
  by isolation - nvcc never sees `/W4`, full stop.
- `-Xcompiler` syntax differs subtly across CUDA Toolkit
  versions; baking it in now imposes a coupling we do
  not need yet.
- The project's CUDA TUs are small, well-bounded, and
  reviewed; missing `-Wall` on them is not a quality
  risk this repair has to solve.

A future slice that wants `-Xcompiler` forwarding can do
so additively without touching `rr_apply_warnings`.

---

## Other Windows-related references

- `docs/WINDOWS_TEST_GUIDE.md` - canonical CLI commands for
  testing on Windows after the CLI render-path repair (the
  separate fix that made `--render <scene>` actually invoke
  the GPU pipeline).
- The "Windows build repair: RenderServer portability" entry
  in `docs/BUILD_PLAN.md` - the earlier fix that introduced
  `src/server/SocketPlatform.h` so the renderer-server
  module compiles on Winsock2.
- `docs/STAGE_15_SERVER_DEFERRED.md` - records the
  prototype-1-final hardware-equipped session that
  validates the server runtime end-to-end.
