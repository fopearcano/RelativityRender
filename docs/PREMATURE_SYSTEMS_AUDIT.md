# Prototype Reuse Audit — Step 7: Premature Systems

Date: 2026-04-29
Branch: `claude/create-docs-architecture-T2Dp5`
Scope: C4D plugin, renderer server, UI, node graph.
Sources: `docs/PROTOTYPE_FILE_INDEX.md`, `docs/PROTOTYPE_CLASSIFICATION.md`,
`docs/ARCHITECTURE_AUDIT.md`, `docs/C4D_NATIVE_RENDERER_PLAN.md`,
`docs/MATERIAL_GRAPH_SPEC.md`, `integrations/c4d/RelativityRenderBridge/`,
`src/server/`, `src/material/`.

This step asks one question per system: **was it introduced too early, is it
incomplete, should it be archived?** Each system gets one of:

- **ARCHIVE_NOW**  — premature, do not bring into the rewrite tree.
- **KEEP_FOR_LATER** — premature for the renderer, but design or protocol is
  worth preserving and will be picked up at a known later milestone.
- **SAFE**         — introduced at the right time, can survive into the rewrite.

No code is modified by this step.

---

## 1. Cinema 4D plugin (`integrations/c4d/RelativityRenderBridge/`)

### What exists

- `RelativityRenderBridge.pyp` — 2,075-line single-file Python plugin:
  scene-object dialog, render-settings dialog, scene export, server invocation,
  preview window.
- `rrscene_writer.py` — translation of C4D scene → `.rrscene` JSON.
- `server_client.py` — line-protocol TCP client (no `c4d` import, host-testable).
- `preview_state.py` — clamping / validation helpers (no `c4d` import).
- `image_io.py` — pure-Python PPM P6 reader + 24-bit BMP writer.
- `tests/` — host tests for the four non-`c4d` modules.

### Was it introduced too early?

Yes. The plugin was implemented in M19 against a renderer that:

- has no production OptiX path (placeholder per `GPU_RENDER_AUDIT.md`);
- has no denoiser (M22 is plan-only);
- has a hand-rolled `SceneLoader` (`ARCHITECTURE_AUDIT.md` flagged 831 lines of
  manual JSON);
- still ships demo code in `main.cpp`.

A user-facing DCC integration before the renderer it wraps is finished is the
textbook premature integration: every churn in the renderer pulls churn through
the bridge.

### Is it incomplete?

The bridge itself is feature-complete for the v1 contract — six implementation
slices, well-tested non-`c4d` modules, deliberate single-file `.pyp`. The
**system underneath it** (renderer server, scene format, AOV semantics) is what
is incomplete, which is why the bridge is fragile by association.

### Should it be archived?

Yes — and the rewrite path is already documented. `docs/C4D_NATIVE_RENDERER_PLAN.md`
(M23) replaces this Python bridge with a native C++ `VideoPostData` plugin at
renderer-replacement priority. The Python bridge will not survive into the
rewrite tree; it stays in `integrations/c4d/` only as a reference for the
scene-translation rules already worked out (object types, transform handling,
material graph mapping).

### Classification

**ARCHIVE_NOW** for inclusion in the rewrite. Reference-only after that.
Already aligns with step 2: all 11 bridge files are `ARCHIVE_ONLY`.

---

## 2. Renderer server (`src/server/`)

### What exists

- `RenderServer.h` / `RenderServer.cpp` — single-client TCP server on
  127.0.0.1:7777, line-based protocol, five commands (HELLO, RENDER, STATUS,
  QUIT, error).
- `README.md` — protocol documented.
- Used by the C4D bridge above and by `--serve` in `main.cpp`.

### Was it introduced too early?

Borderline. The server itself is small and honest about its scope (one client
at a time, line-based, 127.0.0.1 only). But it was wired up *before* the things
it streams existed in mature form:

- AOV pack is M17-young;
- Scene format is hand-rolled JSON;
- There is no binary frame channel, no EXR, no cancellation, no progress.

So the protocol is right-sized for v1 and the implementation is correct, but it
is one milestone ahead of the renderer's actual capabilities.

### Is it incomplete?

Deliberately. The server v1 covers exactly what the M19 bridge needs. Anything
the renderer cannot yet produce (multi-AOV streaming, EXR, cancellation,
progress, multi-client) is intentionally out of scope. That is honest, not
broken — but it does mean the server will need a hardening pass once the
renderer can produce richer output.

### Should it be archived?

No. The protocol shape (HELLO / RENDER / STATUS / QUIT / line framing) is the
right starting contract for a relativistic GPU renderer driven by a DCC, and it
is the only piece of the C4D stack that survives into the native-plugin rewrite
(M23 talks the same protocol locally). Do not throw it away — but do **not**
treat it as final either.

### Classification

**KEEP_FOR_LATER**. Promote to an explicit "server v2" slice (multi-client,
binary AOV stream, EXR, cancellation, progress) once the renderer produces
content worth streaming. Until then, freeze the v1 protocol.

---

## 3. UI

### What exists

Two distinct things have been called "UI" in this prototype:

1. The C4D **bridge dialog** (inside `RelativityRenderBridge.pyp`):
   scene-object editor, render-settings editor, preview window. This is C4D
   `gui` code wrapped around the bridge.
2. A planned standalone **Preview UI** (M20). This **does not exist**: there is
   no `src/ui/`, no Qt/Dear-ImGui dependency, no preview window outside C4D.

### Was it introduced too early?

For (1), yes, by association — it lives inside the premature C4D plugin and has
all the same problems. For (2), the question does not apply: it was never
introduced.

### Is it incomplete?

(1) is feature-complete relative to the v1 bridge contract. (2) is non-existent.

### Should it be archived?

(1) is archived as part of archiving the C4D plugin (it is literally inside the
same `.pyp`). (2) has nothing to archive. The native C4D plugin (M23) replaces
the dialog with C4D-native scene-object descriptions and render-settings UI.

### Classification

- C4D bridge dialog: **ARCHIVE_NOW** (folded into the C4D-plugin decision).
- Standalone Preview UI: **N/A** — does not exist; not premature, just absent.

There is no separate `src/ui/` to keep or archive. The rewrite should not add
one until the renderer has a denoiser (M22) and stable AOVs.

---

## 4. Node graph (material graph)

### What exists

Two parallel implementations — already flagged in `ARCHITECTURE_AUDIT.md` as
the prototype's biggest single piece of structural debt:

| Layer | Files | Lines | Status |
|---|---|---|---|
| **Legacy** | `src/material/MaterialGraph.{h,cpp}`, `material_graph_tests.cpp` | 610 | Pre-M21, replaced |
| **New data core** | `src/material/graph/Graph.{h,cpp}`, `Node.h`, `Socket.h`, `GraphEvaluator.{h,cpp}` | ~1,110 | M21, current |
| **GPU IR + lowering** | `src/material/GpuMaterial.{h,cpp}` | 616 | M21, current |
| **Device evaluator** | `src/cuda/CudaMaterialGraph.cuh` | 213 | M21, current, RR_HD |
| **Host tests** | `material_graph_core_tests.cpp` | — | 354/354 passing |

There is no graph **editor**. The graph is authored in `.rrscene` JSON and in
the C4D bridge; there is no node UI, no drag-and-drop, no live preview of
intermediate sockets.

### Was it introduced too early?

Mixed answer:

- The **new** material-graph stack (data core + GPU IR + RR_HD evaluator) was
  introduced exactly when it should have been: it is required before any
  honest path tracer, and it lands at M21 alongside the integrator work. Not
  premature.
- The **legacy** `MaterialGraph.{h,cpp}` is dead weight — it predates the
  rewrite and is already classified `REWRITE` in step 2. It is not "premature",
  it is *superseded*.
- A graph **editor** has not been introduced and should not be until the
  renderer is stable enough that authored graphs survive longer than a week.

### Is it incomplete?

The new stack is feature-complete for the v1 spec
(`docs/MATERIAL_GRAPH_SPEC.md`, 11 sections) and tested. The legacy
implementation is complete in the trivial sense (it compiles and has tests),
but it is the wrong design and is being replaced. The editor is non-existent.

### Should it be archived?

- New data core + GPU IR + device evaluator: **keep**, this is the spine of
  the rewrite's shading.
- Legacy `MaterialGraph.{h,cpp}` + `material_graph_tests.cpp`: handled by the
  step-2 `REWRITE` classification — delete on entering the rewrite tree, do
  not port.
- Editor: nothing to archive; defer.

### Classification

**KEEP_FOR_LATER** as a system. The new graph survives into the rewrite as-is
(promote to `material/graph/` library); the legacy duplicate gets dropped as
part of the existing `REWRITE` decision; the editor is deferred to a later
slice once denoising and stable AOVs land.

---

## Summary table

| System | Premature? | Incomplete? | Decision |
|---|---|---|---|
| C4D Python bridge plugin | Yes | Underlying renderer is | **ARCHIVE_NOW** |
| Renderer server (line protocol) | Mildly — ahead of renderer capability | By design (v1 scope) | **KEEP_FOR_LATER** |
| C4D bridge dialog | Yes (inside plugin) | No (matches v1 contract) | **ARCHIVE_NOW** (folded in) |
| Standalone Preview UI | N/A — not introduced | N/A | — |
| Material graph: new core + GPU IR | No, lands with M21 | No | **KEEP_FOR_LATER** (promote) |
| Material graph: legacy `MaterialGraph.{h,cpp}` | No, but superseded | No | Already `REWRITE` (step 2) |
| Node graph editor | Not introduced | N/A | Defer |

No system in scope is classified **SAFE**: each one is either ahead of the
renderer it depends on, or replaced by a planned successor, or simply absent.

---

## Implications for the rewrite

1. **C4D integration is M23, not M19.** Start the rewrite with no Python
   plugin. Bring the bridge back only as reference for scene-translation rules
   while building the native `VideoPostData` plugin.
2. **Server protocol survives, server implementation needs a v2 pass.** Keep
   the line protocol and the five commands. Plan an explicit hardening slice
   (multi-client, binary AOV, EXR, cancellation, progress) once the renderer
   can produce content worth streaming.
3. **No UI work until M22.** Neither the C4D dialog nor a standalone preview
   UI should be ported. Add UI only after denoising and stable AOVs exist; the
   rewrite tree should not contain a `src/ui/` until then.
4. **Material graph is the keeper.** Promote the new data core + GPU IR + RR_HD
   evaluator to a first-class `material/graph/` library in the rewrite. Delete
   the legacy `MaterialGraph.{h,cpp}` per step 2. Do not write a node editor
   until shading is stable and the denoiser is in.
5. **No "premature" exception left untracked.** Every premature system in this
   audit is already covered by an existing plan: C4D → M23, server v2 → future
   server slice, UI → after M22, legacy graph → step-2 `REWRITE`. Nothing in
   step 7 unblocks new work; it confirms that the existing plans match the
   prototype's actual debt.
