# Investigation E implementation plan (draft, read-only planning)

Date: 2026-07-12  
Reference: `BRIDGE-DESIGN.md` (committed as `e9e112feb`)  
Constraint: This document is planning only. No implementation is started here.

---

## 1. Per-file edit map

This section maps concrete edit points for:

- `qnx_render_producer.h`
- `qnx_render_producer.cc`
- `qnx_gpu_service.cc`
- `qnx_frame_importer.h`
- `qnx_frame_importer.cc`

and anchors them to `BRIDGE-DESIGN.md` §3 (`L145+`) and §5 (`L204+`).

## 1.1 `qnx_render_producer.h`

### (a) New symbol signatures (planned)

Insertion area from BRIDGE-DESIGN §5: header ranges `131-174`, `204-206`, `220-249` (`BRIDGE-DESIGN.md:L208-L210`).

Current anchors:
- `CanExportDmaBuf` (`qnx_render_producer.h:139`)
- `Initialize` (`:146`)
- `CreateDRMImage` (`:151`)
- `QueryDmaBufMetadata` (`:159`)
- `ExportDmaBufImage` (`:170`)
- `CreateExportFrame` (`:187`)
- `ResolveEGL` (`:196`)
- `ProbeExtensions` (`:199`)
- Mesa pointer block (`:221-236`)
- Mesa flags (`:244-245`)

Planned additions:

```cpp
enum class ExportPath {
  kScreenBridgePrimary,
  kMesaFallback,
  kUnavailable,
};

struct ScreenBufferDescriptor {
  base::ScopedFD fd;
  uint32_t fourcc = 0;
  uint64_t modifier = 0;
  uint32_t stride = 0;
  uint64_t offset = 0;
  uint64_t size = 0;
  void* native_image = nullptr;   // from SCREEN_PROPERTY_NATIVE_IMAGE
  int egl_handle = 0;             // from SCREEN_PROPERTY_EGL_HANDLE
};

bool InitializeScreenBridge(std::string* out_error);
bool InitializeMesaFallback(std::string* out_error);
ExportPath SelectExportPath(std::string* out_reason);
const char* ExportPathName() const;

bool BuildScreenBufferDescriptor(const gfx::Size& size,
                                 ScreenBufferDescriptor* out_desc,
                                 std::string* out_error);
bool PopulateFrameFromScreenDescriptor(ScreenBufferDescriptor* desc,
                                       QnxDmaBufFrame* out_frame,
                                       std::string* out_error);
```

### (b) Insertion points (line-level)

- Replace/extend legacy API contracts around `:131-174` (BRIDGE-DESIGN §5 row for header API surface).
- Replace comment at `:204-206` (`screen` non-use assumption must be removed; BRIDGE-DESIGN §5 row 3).
- Extend Mesa pointer/flag block at `:220-249` with screen context/pixmap/buffer state (BRIDGE-DESIGN §5 row 2).

### (c) Selection logic pseudocode (header-level contract)

```text
Initialize():
  ProbeExtensions()
  active_path = SelectExportPath()
  if active_path == kScreenBridgePrimary: init screen bridge
  else if active_path == kMesaFallback: init mesa fallback
  else fail with aggregated diagnostic
```

### (c'). Render-only frame contract (post-Option A, 2026-07-13)

`SCREEN_PROPERTY_FD` is documented in `screen.h` as set-only ("This can
only be used to provide memory to Screen"). Pixmap-owned buffers
(`screen_create_pixmap_buffer`) do not expose a gettable FD and return
`ENOTSUP` (errno 48) on `screen_get_buffer_property_iv(...)`. This is
the documented, correct behavior, not a runtime error.

`BuildScreenBufferDescriptor` therefore does NOT bail the entire
export on `ENOTSUP`. Instead:

```cpp
errno = 0;
const int fd_rc = screen_get_buffer_property_iv(screen_buffer_, SCREEN_PROPERTY_FD, &fd);
if (fd_rc != 0 && errno == ENOTSUP) {
  DLOG(INFO) << "... SCREEN_PROPERTY_FD not exposed by screen; using render-only frame";
  // fallthrough: leave out_desc->fd invalid; populate the rest of the descriptor
} else if (fd_rc != 0 || fd < 0) {
  // genuine error: bail
}
if (fd >= 0) { /* dup() */ out_desc->fd.reset(...); }
```

The corresponding importer is informed by `frame.fd.is_valid()`:

```cpp
const bool render_only = frame.planes.empty()
  || (frame.planes.size() == 1 && frame.planes[0]
      && !frame.planes[0]->fd.is_valid());
if (render_only) {
  DLOG(INFO) << "... render-only frame; deferring to local Screen buffer presentation";
  return {true, std::string()};
}
```

Resulting semantics:
- **QEMU smoke (`--virgl`)**: producer completes the render into the
  Screen buffer; importer skips EGL import; presentation uses Screen
  buffer (QEMU framebuffer back-channel); no SIGSEGV.
- **Real QNX with `SCREEN_PROPERTY_FD` exposed** (e.g. via `screen_create_buffer`
  with explicit FD): normal dma-buf sharing path; Option A branch
  inert.
- **Real QNX with pixmap + later dma-buf allocator**: producer-side
  Option B (`SCREEN_PROPERTY_NATIVE_IMAGE` + `EGL_NATIVE_PIXMAP_KHR`)
  is the next step; out of scope for this patch.

### (d) Helper structs needed

- `ScreenBufferDescriptor` (above)
- Optional `PathInitResult { ExportPath path; std::string reason; }`

---

## 1.2 `qnx_render_producer.cc`

### (a) New/changed symbol signatures (planned)

From BRIDGE-DESIGN §3 current Mesa path ranges (`L149-L164`) and §5 rows for `qnx_render_producer.cc` (`L211-L214`).

Current function anchors:
- `Initialize` (`qnx_render_producer.cc:178`)
- `ExtensionReport` (`:212`)
- `CanExportDmaBuf` (`:228`)
- `ProbeExtensions` (`:236`)
- `ResolveEGL` (`:261`)
- `CreateDRMImage` (`:275`)
- `QueryDmaBufMetadata` (`:343`)
- `ExportDmaBufImage` (`:385`)
- `CreateExportFrame` (`:449`)

Planned additions/changes:

```cpp
bool QnxRenderProducer::InitializeScreenBridge(std::string* out_error);
bool QnxRenderProducer::InitializeMesaFallback(std::string* out_error);
QnxRenderProducer::ExportPath
QnxRenderProducer::SelectExportPath(std::string* out_reason);
bool QnxRenderProducer::BuildScreenBufferDescriptor(const gfx::Size& size,
                                                    ScreenBufferDescriptor* out_desc,
                                                    std::string* out_error);
bool QnxRenderProducer::PopulateFrameFromScreenDescriptor(
    ScreenBufferDescriptor* desc,
    QnxDmaBufFrame* out_frame,
    std::string* out_error);
```

### (b) Insertion points (line-level)

- `178-209` (`Initialize`) → replace hard Mesa fail with path selection and dual init flow.
- `212-258` (`ExtensionReport`, `CanExportDmaBuf`, `ProbeExtensions`) → include path + screen-bridge readiness.
- `275-447` (Mesa export functions) → keep as fallback branch (`kMesaFallback`) and isolate behind path switch.
- `449-535` (`CreateExportFrame`) → route to active path function, not direct `CreateDRMImage` call.

### (c) Pseudocode for `kScreenBridge` primary + Mesa fallback

```text
Initialize():
  if ctor error -> fail
  ProbeExtensions()

  reason = ""
  path = SelectExportPath(&reason)    // prefer screen bridge, then mesa fallback

  switch(path):
    case kScreenBridgePrimary:
      if !InitializeScreenBridge(&err):
        // optional single fallback attempt
        if InitializeMesaFallback(&mesa_err):
          active_path = kMesaFallback; is_valid = true; log fallback(reason+err)
          return true
        fail(reason + err + mesa_err)
      active_path = kScreenBridgePrimary; is_valid = true; return true

    case kMesaFallback:
      if InitializeMesaFallback(&err):
        active_path = kMesaFallback; is_valid = true; return true
      fail(reason + err)

    default:
      fail(reason)

CreateExportFrame():
  if !is_valid -> fail
  if active_path == kScreenBridgePrimary:
    desc = BuildScreenBufferDescriptor(size)
    PopulateFrameFromScreenDescriptor(desc, &frame)
    return frame
  if active_path == kMesaFallback:
    // existing flow:
    // CreateDRMImage -> QueryDmaBufMetadata -> ExportDmaBufImage -> frame
    return frame
  fail("no export path")
```

### (d) Helper structs

- `ScreenBufferDescriptor` in producer (single-source of fd/stride/offset/fourcc/modifier/native handle)
- Optional `ScreenBridgeState` (screen context/pixmap/buffer handles and cleanup ownership)

---

## 1.3 `qnx_gpu_service.cc`

### (a) New symbol signatures (planned)

BRIDGE-DESIGN §5 row for this file: `154-160`, `377-410` (`BRIDGE-DESIGN.md:L215`).

Current anchors:
- `AttachWidget` (`qnx_gpu_service.cc:123`)
- `ResizeWidget` (`:213`)
- `NativeFrameToMojomFrame` (`:306`)
- `SubmitTestFrameForWidget` (`:346`)
- `producer->Initialize` logs (`:158`, `:242`)
- `CreateExportFrame` call (`:378`)
- `SubmitFrame` call (`:412`)

Planned signatures (internal-only if needed):

```cpp
// optional helpers (file-local static)
const char* ProducerPathTag(const QnxRenderProducer& producer);
std::string ProducerPathDiagnostic(const QnxRenderProducer& producer);
```

No Mojo interface signature change is planned in this phase.

### (b) Insertion points

- After `producer->Initialize()` result logging in:
  - `AttachWidget` (`~154-160`)
  - `ResizeWidget` (`~242-243`)
- Around frame production/submission:
  - `SubmitTestFrameForWidget` (`~377-410`)

### (c) Pseudocode

```text
AttachWidget/ResizeWidget:
  init_ok = producer->Initialize()
  log marker:
    path=<kScreenBridge|kMesaFallback|kUnavailable>
    init_ok=<0|1>
    reason=<init_error or fallback reason>

SubmitTestFrameForWidget:
  frame = producer->CreateExportFrame()
  if error -> log marker with path + error, return
  log marker with path + fourcc + modifier + plane_count
  SubmitFrame(frame)
```

### (d) Helper structs

- None required; rely on producer-provided path string/enum accessor.

---

## 1.4 `qnx_frame_importer.h`

### (a) New symbol signatures (planned)

BRIDGE-DESIGN compatibility anchor (`BRIDGE-DESIGN.md:L194-L202`, §5 row for importer at `L216`).

Current anchors:
- `ImportAndDisplayFrame` (`qnx_frame_importer.h:92`)
- `ExtensionReport` (`:98`)
- `IsEGLReady` (`:101`)
- `WindowEGLState` (`:106`)
- `InitializeEGLDisplay` (`:127`)
- `BuildDmaBufAttrs` (`:142`)
- `ImportDmaBufToTexture` (`:149`)

Planned additions:

```cpp
struct ImportCompatibilityReport {
  bool compatible = false;
  std::string reason;
};

ImportCompatibilityReport ValidateIncomingFrameForBridge(
    const qnx::QnxDmaBufFrame& frame) const;
```

No transport (`QnxDmaBufFrame`) schema change planned in this phase.

### (b) Insertion points

- Near `BuildDmaBufAttrs` declaration (`~137-149`) for compatibility validator declaration.
- Optional diagnostics additions near `ExtensionReport`/`IsEGLReady`.

### (c) Pseudocode

```text
ValidateIncomingFrameForBridge(frame):
  require planes >= 1
  require plane[0].fd valid
  require stride/offset/size sane
  require modifier policy (0 or supported ext)
  return compatibility report
```

### (d) Helper structs

- `ImportCompatibilityReport` only (header-local, no ABI surface change).

---

## 1.5 `qnx_frame_importer.cc`

### (a) New symbol signatures (planned)

Current anchors:
- `ExtensionReport` (`qnx_frame_importer.cc:194`)
- `IsEGLReady` (`:214`)
- `InitializeEGLDisplay` (`:222`)
- `BuildDmaBufAttrs` (`:642`)
- `ImportDmaBufToTexture` (`:705`)
- `ImportAndDisplayFrame` (`:830`)

Planned additions:

```cpp
QnxFrameImporter::ImportCompatibilityReport
QnxFrameImporter::ValidateIncomingFrameForBridge(
    const qnx::QnxDmaBufFrame& frame) const;
```

### (b) Insertion points

- Add validator implementation before `BuildDmaBufAttrs` (`before ~642`).
- Call validator in `ImportAndDisplayFrame` early path (`~830+`, before fd extraction loop).
- Keep `EGL_LINUX_DMA_BUF_EXT` path as default import mechanism (`~705`, `~718-723`) unless gate fails.

### (c) Pseudocode

```text
ImportAndDisplayFrame():
  rep = ValidateIncomingFrameForBridge(frame)
  if !rep.compatible:
    log marker + return false(rep.reason)

  // existing path retained
  BuildDmaBufAttrs(frame, fds, &attrs)
  egl_image = ImportDmaBufToTexture(display, attrs, texture)
  draw + eglSwapBuffers
```

### (d) Helper structs

- Uses `ImportCompatibilityReport` from header.

---

## 2. Build impact assessment

## 2.1 GN/Ninja scope

All planned files belong to:

- `source_set("qnx")` in  
  `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn:18-91`
- specific source entries:
  - `qnx_render_producer.cc/.h` (`:47-48`)
  - `qnx_frame_importer.cc/.h` (`:55-56`)
  - `qnx_gpu_service.cc/.h` (`:59-60`)

Minimal target rebuild:

```bash
./out/qnx_release/ninja_qnx.sh ui/ozone/platform/qnx:qnx -k 20
```

If downstream link validation is required, expand to product targets after qnx target compiles.

## 2.2 Expected duration (planning estimate)

| Scope | Expected wall time |
|---|---|
| Incremental compile of `ui/ozone/platform/qnx:qnx` only | 8-25 min |
| Header-induced fanout + relink in dependent targets | 20-45 min |
| Add one smoke run after build | +10-20 min |

## 2.3 Disk space impact

- Available free space observed: ~`535G` (`df -h /home/yuta/chromium/src/out/qnx_release`).
- Current output tree size observed: ~`17G` (permission warning on one subdir during `du`).
- Expected additional disk for incremental E work:
  - object/relink delta: ~`1-4G`
  - logs/artifacts during smoke/debug: ~`<2G` if cleaned

Conclusion: current free space is sufficient for Investigation E incremental work.

---

## 3. Smoke validation plan (path discrimination markers)

Target: `qnx_run.sh` logs must clearly show whether export path is `kScreenBridge` or Mesa fallback.

## 3.1 Marker lines to add (planned)

Producer side (`qnx_render_producer.cc` / `qnx_gpu_service.cc`):

- `[QNX-BRIDGE] producer.initialize path=kScreenBridgePrimary result=ok ...`
- `[QNX-BRIDGE] producer.initialize path=kMesaFallback result=ok reason=<why-fallback>`
- `[QNX-BRIDGE] producer.export path=<...> fourcc=0x... modifier=0x... planes=N fd0_valid=1`
- `[QNX-BRIDGE] gpu.submit path=<...> widget=... generation=...`

Importer side (`qnx_frame_importer.cc`):

- `[QNX-BRIDGE] importer.validate compatible=1 path_hint=<...> ...`
- `[QNX-BRIDGE] importer.validate compatible=0 reason=<...>`
- `[QNX-BRIDGE] importer.display result=ok widget=...`

## 3.2 Pass indicators in smoke log

`kScreenBridge` success signature:

1. `producer.initialize path=kScreenBridgePrimary result=ok`
2. `producer.export path=kScreenBridgePrimary ... planes>=1`
3. `importer.validate compatible=1`
4. `importer.display result=ok`
5. submit callback accepted (or expected diagnostic for scaffold stage)

Mesa fallback signature:

1. `producer.initialize path=kMesaFallback result=ok reason=<screen-bridge-init-failed...>`
2. `producer.export path=kMesaFallback ...`
3. no `path=kScreenBridgePrimary result=ok` for the same widget/generation

## 3.3 Example grep checks

```bash
rg -n "\\[QNX-BRIDGE\\].*path=kScreenBridgePrimary|\\[QNX-BRIDGE\\].*path=kMesaFallback" <smoke.log>
rg -n "\\[QNX-BRIDGE\\].*importer\\.validate compatible=1|\\[QNX-BRIDGE\\].*importer\\.display result=ok" <smoke.log>
```

---

## 4. Risk register

| Risk | Impact | Likelihood | Mitigation | Detection signal |
|---|---|---|---|---|
| Accidental ABI/schema change (`QnxDmaBufFrame` / mojom) | Browser/GPU interop break | Medium | Keep transport contract unchanged in E; only internal producer/importer path changes | Build breaks in mojom/gen or runtime deserialization failure |
| FD lifetime bug (double-close or leak) during path switch | Crash/EBADF/leak | High | Keep `base::ScopedFD` single ownership; maintain move-only handoff in `NativeFrameToMojomFrame` | Submit path logs + sanitizer/EBADF diagnostics |
| Incomplete metadata (fourcc/stride/offset/size/modifier) | Import failure or black frame | High | Validate descriptor before frame creation; importer-side compatibility check | `importer.validate compatible=0` markers |
| Screen resource lifecycle mismatch (context/pixmap/buffer) | Leak or stale handle reuse | Medium | Introduce explicit `ScreenBridgeState` ownership/cleanup order | Repeated attach/detach smoke with resource growth/failures |
| Wrong path selection (screen bridge should work but falls back) | Hidden regression/perf loss | Medium | Deterministic selection + explicit reasons in logs | Presence of fallback marker without expected bridge errors |
| Thread-affinity misuse of `screen_*` or EGL calls | Intermittent runtime failures | Medium | Keep calls on existing GPU thread where producer currently runs | Non-deterministic init/export failures under repeated smoke |
| Importer assumptions too strict (reject valid frames) | False negatives | Medium | Gate checks align with actual `EGL_LINUX_DMA_BUF_EXT` requirements | `compatible=0` with valid-looking frame metadata |
| Marker logs absent/ambiguous | Hard to prove active path | Medium | Add standardized `[QNX-BRIDGE]` prefix + path field on all critical transitions | grep cannot classify path per widget/generation |

---

## 5. Implementation gate checklist (pass/fail)

Derived from `BRIDGE-DESIGN.md` §7 (`L241+`).

## Gate 1 — Screen metadata completeness

**Pass**:
- Producer can populate `QnxDmaBufFrame` from `screen_*` metadata with valid `fd/fourcc/stride/offset/size`.
- Marker: `[QNX-BRIDGE] producer.export ... fd0_valid=1`.

**Fail**:
- Missing/invalid fd or zero/invalid metadata fields for active path.

## Gate 2 — Import compatibility (`EGL_LINUX_DMA_BUF_EXT`)

**Pass**:
- Browser importer accepts frame and reaches display path without schema change.
- Markers: `importer.validate compatible=1` and `importer.display result=ok`.

**Fail**:
- `BuildDmaBufAttrs` or `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` rejects bridge-generated frames.

## Gate 3 — ANGLE extension observability

**Pass**:
- `egl_exts`/extension reporting clearly logs active extension set and path decision reason.
- Marker shows available `EGL_ANGLE_*` and selected path.

**Fail**:
- Path selected without auditable extension/log evidence.

## Gate 4 — Safe fallback behavior

**Pass**:
- When screen bridge is unavailable, Mesa fallback remains functional and explicitly logged.
- Marker: `path=kMesaFallback result=ok reason=...`.

**Fail**:
- No valid fallback when bridge init fails, or fallback activates silently.

---

## 6. Exit condition for this planning step

- This document is complete when all five requested sections are present and no source implementation files are edited.

## 6.1 Implementation outcome (post-2026-07-13)

Stages completed against this plan:

- **Stage 1 + 2** — `qnx_render_producer.{h,cc}`, `qnx_gpu_service.cc`,
  `qnx_frame_importer.{h,cc}` modified per §1 (a)(b)(c)(d). ~750 lines added.
- **Review pass** — 4 FAILs found and fixed: destructor `screen_destroy_buffer`
  misuse, `SCREEN_PROPERTY_EGL_HANDLE` getter type (`_pv`), `SCREEN_PROPERTY_BUFFER_SIZE`
  getter type (`_iv`), and a missing render path for `kScreenBridge`.
- **Option A** — SCREEN_PROPERTY_FD ENOTSUP graceful handling in
  `BuildScreenBufferDescriptor` (errno=0 + ENOTSUP branch + `if (fd >= 0)`
  guard) plus `render_only` short-circuit in `ImportAndDisplayFrame`.
- **CEF-managed patch**: `qnx_screen_bridge_render_only_fallback.patch`
  registered in `patch/patch.cfg`.
- **Smoke verified** via `qnx_run.sh --virgl --kill-existing`. The
  `qnx_render_producer.cc:515 ... SCREEN_PROPERTY_FD Not supported (48)`
  ERROR is gone; ANGLE display init failure (`gl_display.cc:673`) remains
  as a parallel known issue.

### 6.2 Rendering not actually visible (raised 2026-07-13)

Even with Option A applied, **rendered content does not visibly appear
on the user's QEMU host window**. The producer's pixmap is floating, not
attached to a `screen_win`. Visible output requires a separate
window-attach or window-direct-render design pass; Option A only
achieves the "no SIGSEGV, export completes" robustness level. Full analysis
is in
`SCREEN-PROPERTY-FD-BLOCKER-2026-07-13.md` §7.1.
- Investigation E coding remains blocked pending explicit supervisor approval.

