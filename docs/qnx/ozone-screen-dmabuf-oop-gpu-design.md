# QNX Ozone OOP-GPU Backend Design Note

> **Document status:** Draft for user approval
> **Date:** 2026-07-03
> **Scope:** Phase 2 final backend design for native QNX Screen/EGL Ozone using DMAbuf/EGLImage over Chromium Mojo platform handles
> **Predecessor:** `docs/qnx/ozone-out-of-process-gpu-plan.md`
> **Feasibility base:** Phase 1A and Phase 1B microtask reports (QEMU virgl, x86_64)

---

## Table of Contents

1. [Summary and Goals](#1-summary-and-goals)
2. [Evidence Summary: Phase 1A/1B](#2-evidence-summary-phase-1a1b)
3. [Selected Architecture](#3-selected-architecture)
4. [Stable Widget ID Model](#4-stable-widget-id-model)
5. [Chromium/Ozone Class Responsibilities and Planned Files](#5-chromiumozone-class-responsibilities-and-planned-files)
6. [IPC/Handshake Sketch](#6-ipchandshake-sketch)
7. [Display Path Details](#7-display-path-details)
8. [GPU Producer/Export Path Details](#8-gpu-producerexport-path-details)
9. [Crash/Restart Behavior and Cleanup Ownership](#9-crashrestart-behavior-and-cleanup-ownership)
10. [GN/Ozone Wiring Strategy](#10-gnozone-wiring-strategy)
11. [Risks and Validation Plan](#11-risks-and-validation-plan)
12. [Implementation Phases After User Approval](#12-implementation-phases-after-user-approval)
13. [User Approval Required Before Implementation](#13-user-approval-required-before-implementation)

---

## 1. Summary and Goals

### 1.1 Goal

Implement a native QNX Screen/EGL Ozone backend for Chromium/CEF that:

- Runs the GPU process out-of-process (OOP) from the Browser/UI process.
- Allows the GPU process to crash and restart independently without destroying browser-visible windows.
- Uses the DMAbuf/EGLImage buffer-sharing primitive, with DMAbuf fds carried in Chromium Mojo `handle<platform>` fields. Phase 1B used raw `SCM_RIGHTS` probes to prove the underlying QNX fd-passing mechanism works under QEMU virgl.

### 1.2 Non-Goals

- `--in-process-gpu` is a debugging/fallback tool only; it is not the target architecture.
- No raw `screen_window_t` pointers are passed as `gfx::AcceleratedWidget`.
- No Wayland, Weston, DRM/GBM, GTK, or Qt as first-class dependencies.
- No EGL stream APIs (`EGL_KHR_stream*`) — confirmed absent in QEMU virgl Mesa and not the chosen path.
- No `SCREEN_PROPERTY_EGL_HANDLE` — confirmed non-fatal diagnostic in QEMU virgl; direct `screen_window_t` → `eglCreateWindowSurface` is the working path.

### 1.3 User-Approved Direction

Per `docs/qnx/ozone-out-of-process-gpu-plan.md`:

- Browser/UI process owns visible QNX Screen windows and input/window lifecycle.
- GPU process owns render producer resources and can crash/restart independently.
- Existing headless QNX test paths must remain working.
- Durable QNX changes stay under `cef/patch/patches/qnx/` and `cef/patch/qnx/chromium/new_files/`.

---

## 2. Evidence Summary: Phase 1A/1B

### 2.1 Phase 1A — Extension Inventory Probe

**Report:** `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1a-2026-07-02.md`
**Command:** `qcc -Vgcc_ntox86_64 -o qnx_egl_extension_probe tools/qnx_probes/qnx_egl_extension_probe.c -lscreen -lEGL -lGLESv2` + `./tools/qnx_run.sh --virgl -- ./qnx_egl_extension_probe`
**Result:** Exit 0, EGL 1.5 Mesa Project.

| Extension / API | Status | Relevance |
|---|---|---|
| `EGL_KHR_stream` | **ABSENT** | Eliminated as primary sharing path |
| `EGL_KHR_stream_producer_eglsurface` | **ABSENT** | Eliminated |
| `EGL_KHR_stream_cross_process_fd` | **ABSENT** | Eliminated |
| `EGL_QNX_image_native_buffer` | **PRESENT** | Available for Screen-native buffers |
| `EGL_MESA_image_dma_buf_export` | **PRESENT** | Primary export primitive |
| `EGL_EXT_image_dma_buf_import` | **PRESENT** | Primary import primitive |
| `EGL_EXT_image_dma_buf_import_modifiers` | **PRESENT** | Modifier support |
| `GL_OES_EGL_image` | **PRESENT** | GL-side binding |
| `GL_OES_EGL_image_external` | **PRESENT** | Available |
| `EGL_EXT_platform_base` | **PRESENT** | `eglGetPlatformDisplayEXT` resolved |

**Decision:** EGL streams are unavailable in QEMU virgl. DMAbuf/EGLImage is the selected sharing primitive. The Chromium implementation should transport DMAbuf fds through Mojo `handle<platform>` values rather than owning a bespoke Unix-domain socket protocol; the Phase 1B `SCM_RIGHTS` probes are feasibility evidence for Mojo's underlying POSIX fd transfer.

---

### 2.2 Phase 1B — DMAbuf/EGLImage Microtasks

All five Phase 1B microtasks completed successfully under QEMU virgl (x86_64, virtio-vga-gl, Mesa 1.5 EGL driver).

#### 2.2.1 Export-Only (Phase 1B-exportonly-runtime)

**Report:** `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-exportonly-runtime-2026-07-02.md`

```
eglCreateDRMImageMESA: created 389b83d3c0
Query OK: fourcc=0x34325241 (AR24), planes=1, modifier=0x0
eglExportDMABUFImageMESA: SUCCESS
Plane 0: fd=6 stride=256 offset=0
fstat: st_mode=0666 (DMAbuf fd pattern), O_RDWR
```

**Pass/fail:** ✅ PASS — real DMAbuf fd exported, no pbuffer fallback.

#### 2.2.2 Import/Bind (Phase 1B-dmabuf-import-runtime)

**Report:** `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-dmabuf-import-runtime-2026-07-02.md`

- Producer: `eglCreateDRMImageMESA` + `eglExportDMABUFImageMESA` → real DMAbuf fd (AR24, 1 plane, fd=7).
- Producer → parent → consumer: `sendmsg(SCM_RIGHTS)` each leg → consumer received fd.
- Consumer: `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, NULL, ..., fd)` → `EGL_NO_IMAGE` was **not** returned.
- Consumer: `glEGLImageTargetTexture2DOES` → GL texture created, `glGetError()=0x0`.

**Pass/fail:** ✅ PASS — complete chain GPU process → SCM_RIGHTS → consumer → EGLImage → GL texture.

#### 2.2.3 Display Isolation (Phase 1B-display-isolation)

**Report:** `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-display-isolation-2026-07-02.md`

- `screen_create_window` → `SCREEN_USAGE_OPENGL_ES2`, visible=1: ✅ PASS
- `SCREEN_PROPERTY_EGL_HANDLE`: screen_rc=-1 (non-fatal diagnostic) ✅
- `eglCreateWindowSurface(display, cfg, (EGLNativeWindowType)screen_win, NULL)` → `EGLSurface=0x2846535700` ✅ PASS
- `eglSwapBuffers`: OK, no EGL error ✅ PASS

**Pass/fail:** ✅ PASS — direct `screen_window_t` → `eglCreateWindowSurface` works; `SCREEN_PROPERTY_EGL_HANDLE` is **not** required.

#### 2.2.4 Display Composition (Phase 1B-display)

**Report:** `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-display-2026-07-02.md`

All five legs in one QEMU virgl run:

| Milestone | Status |
|---|---|
| Path A DMAbuf export | ✅ AR24, 1 plane, fd=7 |
| SCM_RIGHTS GPU child → parent → consumer | ✅ 1 fd received each leg |
| `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` → EGLImage | ✅ `0x4f5dcbd3b0` |
| `glEGLImageTargetTexture2DOES` → GL texture | ✅ tex=1, GL error=0 |
| Screen window (64×64, GLES2, visible) | ✅ |
| `SCREEN_PROPERTY_EGL_HANDLE` — diagnostic only | ✅ non-fatal |
| `eglCreateWindowSurface(screen_win)` → EGLSurface | ✅ `0x4f5dcbff40` |
| GLES2 shader draw (fullscreen quad) | ✅ prog=3, GL error=0 |
| `eglSwapBuffers` | ✅ OK |

**Pass/fail:** ✅ PASS — imported DMAbuf texture rendered to visible Screen window via `eglSwapBuffers`.

#### 2.2.5 Crash/Restart (Phase 1B-crash)

**Report:** `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-crash-2026-07-02.md`

```
./qnx_dmabuf_restart_producer --exit-code=37  → exit 37 (crash simulation)
./qnx_dmabuf_restart_producer --exit-code=0   → exit 0  (restart simulation)
restart exits: first=37 second=0
Consumer exit code: 0
```

| Milestone | Frame 1 | Frame 2 | Same? |
|---|---|---|---|
| `screen_window_t` pointer | `0x3f7d615130` | `0x3f7d615130` | ✅ |
| `EGLSurface` pointer | `0x3f7d63cd40` | `0x3f7d63cd40` | ✅ |
| Consumer alive after producer non-zero exit | yes | yes | ✅ |

**Pass/fail:** ✅ PASS — browser-like consumer owns one persistent Screen window across producer crash/restart.

---

## 3. Selected Architecture

### 3.1 Process Ownership

```
┌─────────────────────────────────────────────────────────┐
│  Browser / UI Process                                   │
│                                                         │
│  - Owns visible screen_window_t                         │
│  - Owns screen_context_t                                │
│  - Owns EGL display + EGLSurface + EGLContext           │
│    (compositing only, not GL producer)                  │
│  - Manages input, window lifecycle, bounds, visibility   │
│  - Allocates stable gfx::AcceleratedWidget IDs           │
│  - Owns Mojo receiver/remote for QNX GPU messages        │
│  - Receives DMAbuf metadata + Mojo platform handles      │
│  - Imports DMAbuf via eglCreateImageKHR + texture bind  │
│  - Renders imported texture to EGL window surface        │
│  - Not destroyed when GPU process crashes                │
│                                                         │
│                          ┌──────────────────────────────┐│
│                          │  GPU Process                 ││
│                          │                              ││
│                          │  - Owns EGL display + context││
│                          │    (GLES2 render producer)   ││
│                          │  - Creates DRM image via    ││
│                          │    eglCreateDRMImageMESA     ││
│                          │  - Renders to DRM image       ││
│                          │  - Exports DMAbuf fd via     ││
│                          │    eglExportDMABUFImageMESA  ││
│                          │  - Sends frame metadata +    ││
│                          │    Mojo platform fd handles   ││
│                          │  - May crash and be restarted││
│                          │    independently             ││
│                          │  - Does NOT own Screen window││
│                          └──────────────────────────────┘│
└─────────────────────────────────────────────────────────┘
```

### 3.2 Buffer Flow

```
GPU Process:
  glTexImage2D(...) or FBO render → DRM image
  eglExportDMABUFImageMESA        → DMAbuf fd(s) + metadata (fourcc, stride, plane count)

IPC (Chromium Mojo):
  QnxGpuHost.SubmitFrame(QnxDmaBufFrame)
  QnxDmaBufFrame contains: widget_id | generation | width | height | fourcc | modifier | planes[]
  Each plane carries handle<platform> fd + stride + offset + size.

Browser Process:
  Mojo deserializes platform handles → DMAbuf fd(s)
  eglCreateImageKHR(display, EGL_LINUX_DMA_BUF_EXT, NULL, ...)
  glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_image)
  Render fullscreen quad with texture
  eglSwapBuffers(egl_dpy, window_surface)
```

### 3.3 Why This Architecture

- **OOP-GPU is required:** GPU process crash must not destroy the browser-visible window. Phase 1B-crash proved this works with one persistent Screen window across producer crash/restart.
- **DMAbuf/EGLImage is proven:** Phase 1B proved the complete producer-export → IPC → consumer-import → display chain under QEMU virgl.
- **Underlying fd passing is proven:** Phase 1B-scmrights-devicefd and Phase 1B-dmabuf-import both confirmed QNX can pass character-device fds and DMAbuf fds. Chromium implementation should rely on Mojo `handle<platform>` transport, which uses platform fd passing underneath on POSIX systems.
- **`screen_window_t` is never cross-process:** Browser owns the window pointer. GPU only receives a stable numeric widget ID.
- **`SCREEN_PROPERTY_EGL_HANDLE` is not needed:** Direct `screen_window_t` → `eglCreateWindowSurface` works on Mesa; Phase 1B-display-isolation confirmed this.
- **Headless is unaffected:** Default build/test paths do not use `--ozone-platform=qnx`.

---

## 4. Stable Widget ID Model

### 4.1 `gfx::AcceleratedWidget` on QNX

Per `ui/gfx/native_ui_types.h`:

```cpp
#if BUILDFLAG(IS_OZONE)
using AcceleratedWidget = uint32_t;
inline constexpr AcceleratedWidget kNullAcceleratedWidget = 0;
#endif
```

On QNX, `gfx::AcceleratedWidget` is a `uint32_t` numeric ID — **not** a `screen_window_t` pointer. This is a critical architectural constraint: the widget ID is an opaque, stable identifier allocated and managed by the Browser/UI process.

### 4.2 Widget Record

Each widget has an associated record managed by the browser-side `QnxWindowManager`:

```
struct QnxWidgetRecord {
  gfx::AcceleratedWidget  id;          // stable numeric ID (1..N)
  uint32_t                generation;   // increments each time GPU reconnects
  gfx::Size              size;         // current pixel size (last confirmed)
  screen_window_t        screen_win;    // raw pointer (browser-only, never IPC'd)
  bool                    gpu_attached; // true if GPU process is currently connected
  int                     gpu_pid;      // GPU process PID (0 if detached)
};
```

### 4.3 Widget ID Lifecycle

1. **Allocation:** Browser creates a `screen_window_t` via `screen_create_window()` and assigns a `uint32_t` widget ID from a monotonically incrementing counter (starting at 1). `kNullAcceleratedWidget = 0` is never assigned.
2. **IPC handshake:** Widget ID + current generation + size are sent to the GPU process during attach.
3. **Resize:** When the Screen window is resized, the browser updates the size in the widget record and notifies the GPU process. The widget ID stays stable; only the size changes.
4. **GPU reconnect:** When the GPU process restarts, it receives the same widget ID + **incremented generation** in the reconnect handshake. The browser does not destroy or recreate the Screen window.
5. **Destruction:** When the browser destroys the Screen window, the widget record is removed and the widget ID is retired (never reallocated within the same browser process session).

### 4.4 Why Not Raw `screen_window_t` as Widget ID

- `screen_window_t` is a pointer (`uintptr_t`). Pointers are **not stable** across process boundaries — they have no meaning in the GPU process.
- If a raw pointer were used as the widget ID, the GPU process would hold a dangling reference after browser-side reallocation.
- The `uint32_t` ID is stable because it is managed entirely within the browser process, which controls all widget record lifetimes.

---

## 5. Chromium/Ozone Class Responsibilities and Planned Files

### 5.1 File Locations

Per project conventions, QNX-specific files are placed under `cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/` and associated QNX patches in `cef/patch/patches/qnx/chromium/`.

### 5.2 Class Responsibilities

#### Browser/UI Process Classes

| Class | File | Responsibility |
|---|---|---|
| `OzonePlatformQnx` | `ozone_platform_qnx.cc/h` | OzonePlatform subclass; owns `QnxScreenContext` and `QnxWindowManager`; initializes Screen/EGL for UI; registers QNX Mojo binders / `GpuPlatformSupportHost` hooks for Browser↔GPU communication |
| `QnxScreenContext` | `qnx_screen_context.cc/h` | Owns `screen_context_t`; configures QNX Screen session properties; manages display enumeration |
| `QnxWindowManager` | `qnx_window_manager.cc/h` | Owns `std::map<AcceleratedWidget, QnxWidgetRecord>`; allocates widget IDs; tracks GPU attachment state; owns browser-side Mojo receiver/remote state for QNX GPU messages |
| `QnxWindow` | `qnx_window.cc/h` | `PlatformWindow` subclass; wraps one `screen_window_t`; handles bounds, visibility, cursor, input forwarding; owns EGL display, EGLSurface, EGLContext for display composition |
| `QnxPlatformEventSource` | `qnx_platform_event_source.cc/h` | Reads QNX Screen events (`screen_get_event`) and dispatches to Aura/WindowTreeHost |

#### GPU Process Classes

| Class | File | Responsibility |
|---|---|---|
| `QnxSurfaceFactoryOzone` | `qnx_surface_factory.cc/h` | `SurfaceFactoryOzone` subclass; provides `GLOzone` and `CreateCanvasForWidget` for GPU process; owns GPU-side EGL display and context |
| `QnxGLOzoneEGL` | `qnx_gl_ozone_egl.cc/h` | `GLOzone` subclass; implements `InitializeStaticGLBindings`, `CreateViewGLSurface`, `ImportNativePixmap`, `CanImportNativePixmap`; wraps `eglCreateDRMImageMESA` / `eglExportDMABUFImageMESA` export path |
| `QnxRenderProducer` | `qnx_render_producer.cc/h` | Per-widget render producer; owns DRM image, EGL context, GLES2 pipeline; performs frame export and submits `QnxDmaBufFrame` over Mojo |
| `QnxGLES2Surface` | `qnx_gles2_surface.cc/h` | `GLSurface` subclass for GPU-side rendering; wraps EGL pbuffer or surfaceless context (GPU process only) |

#### Shared/Common Classes

| Class | File | Responsibility |
|---|---|---|
| `QnxDmaBufFrame` | `qnx_dmabuf_frame.h` / `mojom/qnx_gpu.mojom` | Frame metadata: widget_id, generation, width, height, fourcc, modifier, planes[]. Each plane carries Mojo `handle<platform>` fd plus stride/offset/size. |
| QNX GPU Mojo interfaces | `mojom/qnx_gpu.mojom` | Typed Browser↔GPU interface definitions such as `QnxGpuHost`, `AttachWidget`, `SubmitFrame`, `DetachWidget`, and ACK/error replies. |

### 5.3 Planned File List

```
cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/
├── BUILD.gn                              # Ozone platform build target
├── ozone_platform_qnx.cc
├── ozone_platform_qnx.h
├── qnx_screen_context.cc
├── qnx_screen_context.h
├── qnx_window_manager.cc
├── qnx_window_manager.h
├── qnx_window.cc
├── qnx_window.h
├── qnx_platform_event_source.cc
├── qnx_platform_event_source.h
├── qnx_surface_factory.cc
├── qnx_surface_factory.h
├── qnx_gl_ozone_egl.cc
├── qnx_gl_ozone_egl.h
├── qnx_render_producer.cc
├── qnx_render_producer.h
├── qnx_gles2_surface.cc
├── qnx_gles2_surface.h
├── qnx_dmabuf_frame.h
├── mojom/
│   ├── BUILD.gn
│   └── qnx_gpu.mojom
└── [CEF-managed GN patch files in cef/patch/patches/qnx/chromium/]
```

### 5.4 Ozone Interface Map

| Ozone Interface | QNX Implementation | Process |
|---|---|---|
| `OzonePlatform` | `OzonePlatformQnx` | Browser |
| `PlatformWindow` | `QnxWindow` | Browser |
| `PlatformScreen` | `QnxScreen` (minimal; screen enumeration only) | Browser |
| `SurfaceFactoryOzone` | `QnxSurfaceFactoryOzone` | GPU |
| `GLOzone` | `QnxGLOzoneEGL` | GPU |
| `GpuPlatformSupportHost` | inline in `OzonePlatformQnx` | Browser (IPC host side) |

### 5.5 Chromium/GPU IPC Integration

Production Chromium code should use Mojo, not a bespoke Unix-domain socket owned by the QNX backend. The raw socket protocol in `tools/qnx_probes/` was intentionally a feasibility harness: it proved that QNX can pass DMAbuf fds with `SCM_RIGHTS`, which is the underlying POSIX mechanism Mojo can use for `handle<platform>` transport.

The QNX backend should add a small platform-local Mojo interface under `ui/ozone/platform/qnx/mojom/` for the first implementation. This minimizes invasive changes to Chromium-wide `gfx::NativePixmapHandle` serialization while preserving a path to later standardization.

Initial QNX Mojo interfaces should cover:

- Browser → GPU: widget attach/reconnect (`widget_id`, `generation`, `size`).
- Browser → GPU: widget resize/detach.
- GPU → Browser: frame submission with DMAbuf plane handles and metadata.
- Browser → GPU: frame ACK / import-display error.

Longer term, once the QNX OOP path works, the backend can evaluate migrating the frame handle type toward Chromium's existing `gfx::NativePixmapHandle` / `gfx::GpuMemoryBufferHandle` path. That requires auditing QNX support in `NativePixmapPlane`, `native_handle_types.mojom`, and the associated mojom traits because several fd fields are currently guarded primarily for Linux/ChromeOS.

---

## 6. IPC/Handshake Sketch

### 6.1 Production IPC Choice: Mojo

Production Chromium code should use a typed Mojo interface. The standalone probes used raw Unix-domain sockets only to prove QNX Screen/EGL and fd-transfer feasibility outside Chromium.

The first QNX implementation should add a QNX-local mojom file, for example:

```mojom
module ui.ozone.qnx.mojom;

import "ui/gfx/geometry/mojom/geometry.mojom";
import "ui/gfx/mojom/accelerated_widget.mojom";

struct QnxDmaBufPlane {
  handle<platform> fd;
  uint32 stride;
  uint64 offset;
  uint64 size;
};

struct QnxDmaBufFrame {
  gfx.mojom.AcceleratedWidget widget;
  uint32 generation;
  uint32 width;
  uint32 height;
  uint32 fourcc;
  uint64 modifier;
  array<QnxDmaBufPlane> planes;
};

interface QnxGpuHost {
  SubmitFrame(QnxDmaBufFrame frame) => (bool accepted, string diagnostic);
  ReportProducerLost(gfx.mojom.AcceleratedWidget widget, uint32 generation);
};

interface QnxGpuControl {
  AttachWidget(gfx.mojom.AcceleratedWidget widget,
               uint32 generation,
               gfx.mojom.Size size);
  ResizeWidget(gfx.mojom.AcceleratedWidget widget,
               uint32 generation,
               gfx.mojom.Size size);
  DetachWidget(gfx.mojom.AcceleratedWidget widget, uint32 generation);
};
```

In C++, each DMAbuf fd is wrapped as a Mojo platform handle:

```cpp
// GPU side, after eglExportDMABUFImageMESA().
mojo::PlatformHandle fd_handle(std::move(scoped_fd));
qnx::mojom::QnxDmaBufPlanePtr plane = qnx::mojom::QnxDmaBufPlane::New(
    std::move(fd_handle), stride, offset, size);
```

On the browser side, Mojo deserialization yields the transferred platform handle, which is converted back into `base::ScopedFD` and passed to `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, ...)`.

### 6.2 Why Not Own a Backend Socket in Production

A backend-owned socket would duplicate Chromium's existing process IPC layer and create a second security boundary. Mojo already provides:

- typed messages and versionable mojom schemas;
- platform handle transport (`handle<platform>`) for fds;
- disconnect handlers for GPU process death;
- integration with the Browser/GPU process lifetime model;
- existing Ozone/GPU binding patterns, such as the DRM platform's mojom device interfaces.

The QNX probes remain valuable because they validated the OS-level mechanism Mojo depends on: QNX can pass DMAbuf-like fds between processes.

### 6.3 Handshake Sequence

#### Attach (GPU startup/restart)

```
Browser/UI                      GPU Process
  |                                  |
  | QnxGpuControl.AttachWidget       |
  |   widget_id=1                    |
  |   generation=1                   |
  |   size=1024x768                  |──→ GPU creates render producer for
  |                                  |    widget_id=1, generation=1
```

The browser owns the authoritative widget table. GPU process restart creates a fresh Mojo connection; the browser sends current widget records again with incremented generation numbers.

#### Per-Frame Render Loop

```
Browser/UI                      GPU Process
  |                                  |
  | ←── QnxGpuHost.SubmitFrame ──────|  GPU exports DMAbuf fd via
  |     QnxDmaBufFrame:              |  eglExportDMABUFImageMESA
  |       widget=1                   |  and sends Mojo handle<platform>
  |       generation=1               |
  |       width=1024 height=768      |
  |       fourcc=AR24                |
  |       modifier=0                 |
  |       planes[0].fd=<platform>    |
  |       planes[0].stride=4096      |
  |       planes[0].offset=0         |
  |                                  |
  | Browser imports:                 |
  |   fd = platform_handle.TakeFD()  |
  |   eglCreateImageKHR(             |
  |     EGL_LINUX_DMA_BUF_EXT, ...)  |
  |   glEGLImageTargetTexture2DOES    |
  |   glBindTexture + glDrawArrays   |
  |   eglSwapBuffers                 |
  |                                  |
  | callback accepted=true ─────────→|  GPU can release frame resources
```

#### Resize

```
Browser/UI                      GPU Process
  |                                  |
  | QnxGpuControl.ResizeWidget       |
  |   widget_id=1                    |
  |   generation=1                   |
  |   size=1280x800                  |──→ GPU recreates producer buffer
```

#### GPU Reconnect (after crash/restart)

```
Browser/UI                      GPU Process (restarted)
  |                                  |
  | Mojo disconnect handler fires     |
  | generation++                      |
  |                                  |
  | QnxGpuControl.AttachWidget       |
  |   widget_id=1                    |
  |   generation=2  ← incremented    |
  |   size=1280x800                  |──→ GPU creates NEW producer
  |                                  |    (browser window unchanged)
```

### 6.4 ACK / Error Handling

| Browser outcome | Mojo result / message | GPU action |
|---|---|---|
| Frame imported and displayed successfully | `SubmitFrame` callback: `accepted=true` | GPU releases frame resources, sends next frame |
| Frame import failed (EGL error) | `accepted=false`, diagnostic string | GPU logs error, retries or signals compositor |
| Frame took too long (> 2× frame interval) | Browser may drop and return `accepted=false` | GPU drops and re-renders |
| Widget is being destroyed | `DetachWidget(widget_id, generation)` | GPU destroys producer, stops sending frames |
| GPU process disconnects | Mojo disconnect handler | Browser preserves Screen window, increments generation |

### 6.5 Generation Invalidation

When the browser detects GPU process death via Mojo disconnect handling, it:

1. Marks the widget's `gpu_attached = false`.
2. Increments `generation` in the widget record.
3. Waits for Chromium to create/bind a new GPU process endpoint.
4. On reconnect, sends `AttachWidget` with the new `generation`.
5. If a stale or delayed frame arrives with the old `generation`, the browser discards it and returns `accepted=false` with `ERROR_WRONG_GENERATION`.

---

## 7. Display Path Details

### 7.1 Browser Display Architecture

The Browser/UI process owns one `screen_window_t` per visible widget, created with:

```c
screen_create_window(context, parent_win);
screen_set_window_property_iv(win, SCREEN_PROPERTY_USAGE, SCREEN_USAGE_OPENGL_ES2);
screen_set_window_property_iv(win, SCREEN_PROPERTY_SIZE, size);
screen_set_window_property_iv(win, SCREEN_PROPERTY_VISIBLE, 1);
screen_create_window_buffers(win, 1);
```

This is the same pattern proven in Phase 1B-display and Phase 1B-crash.

### 7.2 EGL Window Surface Creation

**`SCREEN_PROPERTY_EGL_HANDLE` is non-fatal and not used.**

Phase 1B-display-isolation proved:
- `screen_get_window_property_iv(win, SCREEN_PROPERTY_EGL_HANDLE, ...)` returns `-1` (fails) on Mesa virgl.
- This failure is **non-blocking** — `eglCreateWindowSurface` accepts the raw `screen_window_t` directly.

```cpp
// Diagnostic only — do NOT gate on failure
int screen_rc = screen_get_window_property_iv(
    screen_win, SCREEN_PROPERTY_EGL_HANDLE, &egl_handle);
if (screen_rc != 0) {
  // Non-fatal on Mesa virgl; proceed
  DLOG(WARNING) << "SCREEN_PROPERTY_EGL_HANDLE not available";
}

// Primary path — direct screen_window_t
EGLSurface window_surface = eglCreateWindowSurface(
    egl_display,   // EGLDisplay obtained from eglGetDisplay(EGL_DEFAULT_DISPLAY)
    egl_config,    // from eglChooseConfig (must have EGL_WINDOW_BIT)
    reinterpret_cast<EGLNativeWindowType>(screen_win),
    nullptr        // attrib_list — NULL per Phase 1B-display
);
CHECK(window_surface != EGL_NO_SURFACE);
```

### 7.3 Browser GLES2 Composition Pipeline

After receiving a DMAbuf frame and importing it as a GL texture:

```cpp
// Create GLES2 context for composition (if not cached)
EGLContext comp_ctx = eglCreateContext(egl_display, egl_config,
    EGL_NO_CONTEXT, context_attrs);
eglMakeCurrent(egl_display, window_surface, window_surface, comp_ctx);

// Fullscreen quad — pass-through vertex shader, texture sample fragment shader
glBindTexture(GL_TEXTURE_2D, imported_texture_id);
glUniform1i(u_tex_location, 0);
// DrawArrays triangle strip or quad
glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
glGetError(); // must be GL_NO_ERROR

// Present to Screen
eglSwapBuffers(egl_display, window_surface);

// Return ACK to GPU process
```

### 7.4 EGL Display Initialization

Both Browser and GPU processes initialize EGL independently using `eglGetDisplay(EGL_DEFAULT_DISPLAY)`:

- This returns the default EGL display, which maps to Mesa's virgl driver in QEMU.
- On real QNX hardware, this maps to the QNX GPU driver's EGL display.
- `EGL_EXT_platform_base` + `eglGetPlatformDisplayEXT` may be used for explicit platform dispatch if needed on real hardware, but `eglGetDisplay(EGL_DEFAULT_DISPLAY)` is sufficient for QEMU virgl and is the simplest starting point.

---

## 8. GPU Producer/Export Path Details

### 8.1 QEMU-Proven Path: `eglCreateDRMImageMESA` + `eglExportDMABUFImageMESA`

Phase 1B-exportonly-runtime and Phase 1B-dmabuf-import-runtime both used this path:

```cpp
// GPU process — create DRM image (no GL, no Screen needed)
EGLImageKHR drm_img = eglCreateDRMImageMESA(
    egl_display,
    nullptr  // attrib_list — no EGL_DRM_BUFFER_STRIDE_MESA needed for default path
);
CHECK(drm_img != EGL_NO_IMAGE_KHR);

// Render into it via FBO or glTexImage2D
// (In Chromium this would be the compositor's GPU pipeline)

// Export to DMAbuf
int fd;
int stride;
EGLBoolean ok = eglExportDMABUFImageMESA(egl_display, drm_img, &fd, &stride, nullptr);
CHECK(ok == EGL_TRUE);

// Query metadata
int fourcc;
int n_planes;
uint64_t modifier;
eglExportDMABUFImageQueryMESA(egl_display, drm_img, &fourcc, &n_planes, &modifier);

// fd is a real DMAbuf PRIME fd (st_mode=0666 on Mesa, O_RDWR)
// Send fd to browser as a Mojo handle<platform> alongside metadata
```

**QEMU virgl observed format:** fourcc `0x34325241` ("AR24"), 1 plane, modifier 0, stride 256–4096.

### 8.2 Note on Render-Target Export Primitive

The Phase 1B probes used `eglCreateDRMImageMESA` (which creates a Mesa-internal DRM buffer) as the starting point for export. As the implementation matures toward a full Chromium compositor pipeline, the render target export path may be preferred:

- The Chromium GPU pipeline renders into a GL framebuffer object (FBO) or pbuffer surface.
- The equivalent export primitive would use `EGL_MESA_image_dma_buf_export` to export a DRM image backed by the GPU-rendered buffer.
- On virgl/Mesa, `eglCreateDRMImageMESA` + render-to-FBO + `eglExportDMABUFImageMESA` is equivalent to the render-target export path.
- On real QNX GPU hardware, the exact equivalent (e.g., `EGL_QNX_image_native_buffer` + QNX buffer) may differ. The Phase 1A probe confirmed `EGL_QNX_image_native_buffer` is present.

**Implementation guidance:** Start with the proven `eglCreateDRMImageMESA` path (matching Phase 1B evidence). As the full compositor pipeline is wired, migrate to the render-target export primitive that matches the Chromium GPU pipeline's existing GBM/DMAbuf path.

### 8.3 Format and Modifier Handling

| Aspect | QEMU virgl evidence | Planned approach |
|---|---|---|
| Fourcc | AR24 (0x34325241) | Negotiate with compositor; prefer `XR24` or `RG24` for robustness |
| Planes | 1 plane | Handle multi-plane (YUV) as future extension |
| Modifier | 0 (linear) | Probe `EGL_EXT_image_dma_buf_import_modifiers` (present in Phase 1A); pass modifier=0 for linear buffers initially |
| Stride | 256–4096 | Derived from width × bytes_per_pixel; pass as IPC metadata |
| fd flags | O_RDWR (Mesa observed) | Pass as-is; browser uses `fcntl(fd, F_GETFL)` to confirm mode |

---

## 9. Crash/Restart Behavior and Cleanup Ownership

### 9.1 Ownership Table

| Resource | Owner | Survives GPU crash? | Destroyed when |
|---|---|---|---|
| `screen_context_t` | Browser/UI | ✅ Always | Browser shutdown |
| `screen_window_t` | Browser/UI | ✅ Always | Window close / browser shutdown |
| EGL display (browser) | Browser/UI | ✅ Always | Browser shutdown |
| EGLSurface | Browser/UI | ✅ Always | Window close |
| EGLContext (display composition) | Browser/UI | ✅ Always | Browser shutdown |
| Mojo QNX GPU host receiver / remotes | Browser/UI | ✅ (browser endpoint survives; GPU endpoint disconnects) | Browser shutdown |
| GPU Mojo endpoint | GPU process | ❌ Destroyed with GPU process | GPU crash |
| GPU EGL display | GPU process | ❌ Destroyed with GPU process | GPU crash |
| GPU EGLContext | GPU process | ❌ Destroyed with GPU process | GPU crash |
| DRM image / DMAbuf fd | GPU process | ❌ Destroyed with GPU process | GPU crash |
| Per-frame Mojo platform handles | GPU → Browser transfer | Fds transferred; sender closes after submit | Frame ACK / error |

### 9.2 GPU Process Death Detection

Browser-side detection should use Mojo disconnect handlers, plus existing Chromium GPU process lifecycle notifications where available:

```cpp
void QnxWindowManager::OnGpuMojoDisconnected() {
  // Mark all widgets as GPU-detached.
  for (auto& [widget_id, record] : widgets_) {
    record.gpu_attached = false;
    record.generation++;  // increment generation for next reconnect
  }
  qnx_gpu_control_remote_.reset();
  LOG(INFO) << "GPU process disconnected; browser window(s) preserved";
}
```

### 9.3 GPU Process Restart

1. GPU process is re-spawned by the Chromium process manager.
2. New GPU process obtains/binds the QNX Mojo endpoint through the normal Chromium GPU/Ozone binding path.
3. Browser sends `AttachWidget` for each widget with **incremented generation**.
4. GPU process creates new render producers, EGL display, and contexts.
5. GPU process begins sending frames with the new generation.
6. Browser discards any stale frames with old generation.

**This is exactly the pattern validated in Phase 1B-crash:** one persistent Screen window (`0x3f7d615130`) + one persistent EGLSurface (`0x3f7d63cd40`) survived producer exit code 37 and accepted a reconnect from a second producer.

### 9.4 Browser Shutdown

- Browser closes all Screen windows via `screen_destroy_window()`.
- Browser sends/drops `DetachWidget` / resets QNX Mojo remotes if connected.
- Browser destroys QNX Mojo receivers/remotes.
- Browser destroys EGL display.
- Browser destroys Screen context.

### 9.5 No Zombified State

There is no state where the GPU process is alive but the browser is gone, because:
- Chromium's process architecture ensures the GPU process is a child of the browser process.
- When the browser exits, the GPU process is terminated by the OS (SIGKILL or inherited signal).

---

## 10. GN/Ozone Wiring Strategy

### 10.1 Goal: Enable `--ozone-platform=qnx` Without Breaking Headless

The default CEF build for QNX is headless (no Screen window). The OOP-GPU Ozone backend must not affect the default build path.

### 10.2 Build Flag Wiring

In the CEF-managed `BUILD.gn` for the QNX platform:

```python
# cef/patch/patches/qnx/chromium/.../BUILD.gn (CEF-managed patch)
if (ozone_platform_qnx) {
  # QNX Ozone platform sources
  sources += [
    "ui/ozone/platform/qnx/ozone_platform_qnx.cc",
    "ui/ozone/platform/qnx/qnx_screen_context.cc",
    "ui/ozone/platform/qnx/qnx_window_manager.cc",
    "ui/ozone/platform/qnx/qnx_window.cc",
    "ui/ozone/platform/qnx/qnx_platform_event_source.cc",
    "ui/ozone/platform/qnx/qnx_surface_factory.cc",
    "ui/ozone/platform/qnx/qnx_gl_ozone_egl.cc",
    "ui/ozone/platform/qnx/qnx_render_producer.cc",
    "ui/ozone/platform/qnx/qnx_gles2_surface.cc",
  ]

  deps += [
    "//ui/ozone:ozone_base",
    "//ui/gfx",
    "//ui/platform_window",
  ]

  libs += [
    "screen",
    "EGL",
    "GLESv2",
  ]
}
```

### 10.3 Platform Selection

`--ozone-platform=qnx` is selected explicitly by the user. Chromium's base command-line processing automatically routes to `OzonePlatformQnx::GetInstance()` when the platform name matches.

Headless QNX targets (used by default CEF tests) do **not** select `--ozone-platform=qnx`; they use the existing headless / no-platform path, which is unaffected.

### 10.4 No Changes to Default Paths

- `gn gen out/qnx_release` succeeds with or without `ozone_platform_qnx=true`.
- Headless unit tests (`base_unittests`, etc.) do not link against QNX Screen/EGL libraries unless `ozone_platform_qnx=true`.
- The CEF-managed patch only adds the QNX platform; it does not modify existing headless or DRM/GBM platform code.

---

## 11. Risks and Validation Plan

### 11.1 Real Hardware / aarch64 Validation

**Risk:** All Phase 1A/1B evidence is from QEMU virgl (x86_64). Real QNX hardware (aarch64) behavior may differ.

**Mitigation:**
- Phase 1A confirmed `EGL_MESA_image_dma_buf_export`, `EGL_EXT_image_dma_buf_import`, and `EGL_QNX_image_native_buffer` are present in the QNX SDK headers.
- The aarch64 sysroot at `$QNX_HOST/aarch64le/usr/lib/` provides `libscreen.so`, `libEGL.so`, `libGLESv2.so`.
- **Before Phase 7 visual smoke**, validate on a real aarch64 board:
  1. Compile QNX Ozone backend for aarch64 (`qcc -Vgcc_ntoaarch64`).
  2. Run `egl-configs` and `gles2-gears` on the board.
  3. Run the Phase 1B probes (`qnx_dmabuf_export_only_probe`, `qnx_dmabuf_restart_*`) on the board.
  4. If export/import/composition fails, capture the EGL error code and update the design.

### 11.2 Format and Modifier Handling

**Risk:** Phase 1B virgl only exported AR24 (fourcc `0x34325241`, modifier=0, 1 plane). Real GPU hardware may require different formats (e.g., NV12 for video, BGRA8 for compositing).

**Mitigation:**
- `EGL_EXT_image_dma_buf_import_modifiers` was confirmed present in Phase 1A.
- The implementation will probe modifier support and negotiate the best format.
- Start with modifier=0 (linear) + AR24 or XR24 as the default path.
- Add multi-plane YUV support when Chromium's video pipeline is wired to QNX Ozone.

### 11.3 Synchronization / Fences

**Risk:** DMAbuf fd passing does not automatically synchronize. The GPU may still be writing to the buffer when the browser reads it. Without explicit fences, tearing or stale content may occur.

**Current evidence:** Phase 1B probes did not use explicit fences and showed no tearing artifacts in QEMU virgl (single-process virtio-gpu, implicit host-side synchronization). Real GPU hardware may require explicit fences.

**Mitigation:**
- Use `EGL_KHR_wait_sync` (present in Phase 1A) for GPU→browser synchronization.
- On the GPU side: after rendering, call `glFinish()` before `eglExportDMABUFImageMESA`.
- On the browser side: after `eglSwapBuffers`, the EGL implementation handles the present synchronization.
- Add explicit `EGL_ANDROID_native_fence_sync` or `EGL_KHR_fence_sync` if tearing is observed on real hardware.

### 11.4 Security

**Risk:** DMAbuf fds are transferred from the GPU process to the browser process. A compromised GPU process could send malformed metadata or unexpected handles.

**Mitigation:**
- Use Chromium Mojo endpoints, not an unauthenticated public socket path.
- The QNX Mojo interface is only bound through Chromium's managed Browser↔GPU process channel.
- Browser process validates all received handles: `TakeFD()` must produce a valid fd; metadata must be bounded; plane count must be within the supported maximum; `fcntl(fd, F_GETFL)` and `fstat()` diagnostics are logged.
- Browser verifies `widget_id` and `generation` before importing any fd.
- Browser closes received fds after `eglCreateImageKHR` / EGLImage lifetime rules permit; fds are not forwarded to renderer processes.
- The GPU process runs sandboxed (Chromium's GPU sandbox), limiting the blast radius of a compromised GPU process.

### 11.5 Performance

**Risk:** Per-frame Mojo platform-handle transfer + EGLImage import may add latency compared to an in-process path.

**Mitigation:**
- Phase 1B probes show the complete import+swap pipeline completes in well under one frame interval at 60fps (sub-16ms).
- `eglExportDMABUFImageMESA` is a zero-copy operation — the fd refers to the same DMAbuf backed by the GPU's memory.
- If profiling shows frame time exceeds budget, consider batching multiple frames or using a ring-buffer of pre-allocated DMAbuf images.
- The Phase 1B crash/restart probe showed reconnect overhead is minimal in the standalone socket model; production should validate equivalent Mojo reconnect overhead.

### 11.6 QNX-Specific API Stability

**Risk:** QNX Screen/EGL API surface used here (`screen_create_window`, `screen_set_window_property_iv`, `SCREEN_USAGE_OPENGL_ES2`, `screen_create_window_buffers`) must be compatible with the QNX SDP version in use.

**Mitigation:**
- All used API calls were confirmed available in the QNX 8.0 SDP sysroots (`screen_create_window`, `screen_set_property`, etc.).
- If a Screen API call fails at runtime, the Ozone backend will log the error and fall back to headless mode rather than crashing.
- The `OzonePlatform::InitializeForUI` return value (bool) signals to Chromium whether startup succeeded.

### 11.7 EGL Extension Availability on Real Hardware

**Risk:** Real QNX GPU hardware may not have the same EGL extensions as Mesa virgl.

**Mitigation:**
- The `QnxGLOzoneEGL::InitializeStaticGLBindings()` method will probe all required extensions at runtime and log which are missing.
- If `EGL_MESA_image_dma_buf_export` is absent on real hardware, the backend logs a fatal error and exits with a clear diagnostic message.
- If `EGL_QNX_image_native_buffer` is present (confirmed in Phase 1A), an alternative export path using QNX-native buffers may be explored as a future optimization.

---

## 12. Implementation Phases After User Approval

### Phase 3 — GN/Ozone Wiring Only

1. Add `BUILD.gn` for `ui/ozone/platform/qnx/` under CEF-managed patches.
2. Add `ozone_platform_qnx` to the allowed platform list in the CEF GN configuration.
3. Verify `gn gen out/qnx_release` succeeds.
4. Verify existing headless QNX targets (`base_unittests`, etc.) still build.
5. **Acceptance:** `gn gen` succeeds, no regressions to default build.

### Phase 4 — Browser/UI-Side QNX Ozone Skeleton

1. `OzonePlatformQnx` — registers platform, provides factory methods.
2. `QnxScreenContext` — owns `screen_context_t`, initializes Screen.
3. `QnxWindowManager` — allocates widget IDs, tracks widget records, owns QNX Mojo host/control state.
4. `QnxWindow` — wraps `screen_window_t`, implements `PlatformWindow`, sets up EGL display + window surface + context for display composition.
5. `QnxPlatformEventSource` — reads Screen events, dispatches to Aura.
6. Minimal Mojo plumbing: bind QNX Browser↔GPU endpoints; receive and discard `SubmitFrame` messages (no display yet).
7. **Acceptance:** `cefsimple --ozone-platform=qnx` starts without crash; `screen_window_t` created; no GPU process connected yet.

### Phase 5 — GPU-Side QNX Render Producer

1. `QnxSurfaceFactoryOzone` — `SurfaceFactoryOzone` for GPU process.
2. `QnxGLOzoneEGL` — GL bindings, `CreateViewGLSurface`, `ImportNativePixmap`.
3. `QnxRenderProducer` — per-widget export pipeline: `eglCreateDRMImageMESA` → render → `eglExportDMABUFImageMESA` → Mojo `SubmitFrame` with `handle<platform>` plane fds.
4. GPU process binds the QNX Mojo control/host endpoints and sends frames.
5. **Acceptance:** GPU process sends frames; browser receives Mojo platform handles carrying DMAbuf fds; no crash.

### Phase 6 — Browser/GPU Reconnect and Crash Recovery

1. Implement generation counter and `MSG_ATTACH` handshake with generation.
2. Implement `WIDGET_RESIZE` with size update.
3. Implement `SubmitFrame` callback / error signaling.
4. Simulate GPU crash (`kill -9`) and verify browser window survives.
5. Verify GPU reconnect with incremented generation.
6. **Acceptance:** `kill -9` of GPU process does not destroy browser window; GPU restart resumes drawing.

### Phase 7 — Chromium/CEF Visual Smoke

1. Build `cefsimple` or `chromium` with `--ozone-platform=qnx` and out-of-process GPU.
2. Run in QEMU virgl.
3. Capture screenshot to verify rendering.
4. Re-run representative headless QNX tests to prove no regression.
5. **Acceptance:** Screenshot matches expected output; headless tests pass.

---

## 13. User Approval Required Before Implementation

This design note describes the planned Chromium/Ozone implementation for the QNX Ozone OOP-GPU backend using DMAbuf/EGLImage transported by Chromium Mojo `handle<platform>` values. Phase 1A/1B QEMU virgl probes used raw `SCM_RIGHTS` to validate the underlying QNX fd-passing mechanism.

**Implementation must not begin until the user approves this design note.**

Before approving, the user should confirm:

1. The selected architecture (Browser=Screen owner + DMAbuf consumer; GPU=render producer + DMAbuf exporter) matches the intended product direction.
2. The widget ID model (stable `uint32_t`, not raw `screen_window_t`) is acceptable.
3. The Phase 1B evidence (QEMU virgl only) is sufficient to proceed to implementation, with real hardware validation deferred to Phase 11.1.
4. The implementation phases (3–7) are in the intended order and scope.
5. The GN/Ozone wiring approach (additive, no changes to headless defaults) is acceptable.

If any aspect of this design requires revision or clarification, please state the concern before implementation begins.

---

## Document History

| Date | Change |
|---|---|
| 2026-07-03 | Created from Phase 1A and Phase 1B microtask evidence; replaces preliminary architecture sketch in `ozone-out-of-process-gpu-plan.md`. |
| 2026-07-03 | Revised IPC design: production Chromium uses Mojo `handle<platform>` for DMAbuf fd transport; raw Unix socket + `SCM_RIGHTS` remains probe-only evidence. |

---

## Appendix A: Key Probe Source Locations

| Probe | File | Phase 1B evidence |
|---|---|---|
| Extension inventory | `tools/qnx_probes/qnx_egl_extension_probe.c` | Phase 1A |
| Export-only DMAbuf | `tools/qnx_probes/qnx_dmabuf_export_only_probe.c` | Phase 1B-exportonly |
| Screen+EGL window | `tools/qnx_probes/qnx_screen_egl_window_probe.c` | Phase 1B-display-isolation |
| DMAbuf producer | `tools/qnx_probes/qnx_dmabuf_export_producer.c` | Phase 1B-dmabuf-import, Phase 1B-display |
| DMAbuf consumer | `tools/qnx_probes/qnx_dmabuf_import_consumer.c` | Phase 1B-dmabuf-import, Phase 1B-display |
| Restart consumer | `tools/qnx_probes/qnx_dmabuf_restart_consumer.c` | Phase 1B-crash |
| Restart producer | `tools/qnx_probes/qnx_dmabuf_restart_producer.c` | Phase 1B-crash |

## Appendix B: Relevant Chromium Interface Files

| File | Purpose |
|---|---|
| `ui/ozone/public/ozone_platform.h` | `OzonePlatform` base class; `GetSurfaceFactoryOzone`, `CreatePlatformWindow`, `GetGpuPlatformSupportHost` factory methods |
| `ui/ozone/public/surface_factory_ozone.h` | `SurfaceFactoryOzone`; `CreateCanvasForWidget`, `GetGLOzone`, `CreateNativePixmap` |
| `ui/ozone/public/gl_ozone.h` | `GLOzone`; `InitializeStaticGLBindings`, `CreateViewGLSurface`, `ImportNativePixmap` |
| `ui/platform_window/platform_window.h` | `PlatformWindow` interface; `Show`, `SetBoundsInPixels`, `SetTitle`, `SetCapture`, etc. |
| `ui/gfx/native_ui_types.h` | `gfx::AcceleratedWidget = uint32_t` on Ozone; `kNullAcceleratedWidget = 0` |
| `gpu/ipc/common/gpu_surface_tracker.h` | `GpuSurfaceTracker`; surface handle registry for GPU↔browser surface lookup |
