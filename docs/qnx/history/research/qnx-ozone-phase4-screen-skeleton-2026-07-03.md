# QNX Ozone Phase 4 — Browser/UI-side Screen Skeleton

**Date:** 2026-07-03
**Phase:** 4, first substep
**Status:** Complete — target compiles

## Scope

Replace the Phase 3 failing UI stub (`InitializeUI()` returned `false`) with a minimal Browser/UI-side QNX Screen skeleton that:
- Compiles with QNX cross-compiler
- Initializes QNX Screen context (`screen_context_t`)
- Allocates stable `gfx::AcceleratedWidget` numeric IDs
- Creates and destroys visible `screen_window_t` instances
- Provides a minimal `QnxScreen`

GPU producer, Mojo frame transport, and EGL display/composition surface are intentionally deferred.

---

## Files Changed

### CEF-managed new files (all under `cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/`)

| File | Lines | Responsibility |
|---|---|---|
| `BUILD.gn` | 56 | Build target with `screen`, `EGL`, `GLESv2` libs |
| `ozone_platform_qnx.cc` | 152 | OzonePlatform subclass; owns `QnxScreenContext` + `QnxWindowManager` |
| `ozone_platform_qnx.h` | 26 | Updated header with Phase 4 class responsibilities |
| `qnx_screen_context.{cc,h}` | 35+37 | Owns `screen_context_t` via `screen_create_context()` / `screen_destroy_context()` |
| `qnx_window_manager.{cc,h}` | 104+96 | Allocates widget IDs via `base::IDMap`; tracks `QnxWidgetRecord` per widget |
| `qnx_window.{cc,h}` | 347+118 | `PlatformWindow` subclass; owns `screen_window_t`; `screen_create_window()`, `screen_set_window_property_*`, `screen_create_window_buffers()`, `screen_destroy_window()` |
| `qnx_screen.{cc,h}` | 143+54 | Minimal `PlatformScreen`; provides `Display` with 1024×768 primary display |

### CEF-managed patches (unchanged since Phase 3)
- `cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch` — adds `ozone_platform_qnx` to `build/config/ozone.gni`
- `cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch` — adds `qnx` platform to `ui/ozone/BUILD.gn`

---

## Class Responsibilities Implemented

### `OzonePlatformQnx` (replaces Phase 3 stub)
- Owns `std::unique_ptr<QnxScreenContext>` and `std::unique_ptr<QnxWindowManager>`
- `InitializeUI()`: creates `QnxScreenContext`, `QnxWindowManager`, returns `true` on success
- `CreatePlatformWindow()`: creates `QnxWindow` with bounds from `PlatformWindowInitProperties`
- `CreateScreen()`: returns `std::make_unique<QnxScreen>(window_manager_.get())`
- `InitializeGPU()`: no-op (Phase 5)
- `GetSurfaceFactoryOzone()`: `nullptr` (Phase 5)

### `QnxScreenContext`
- Owns `screen_context_t`
- Constructor: `screen_create_context(&context_, SCREEN_APPLICATION_CONTEXT)` — returns nullptr + error log on failure
- Destructor: `screen_destroy_context(context_)`

### `QnxWindowManager`
- `AddWindow(QnxWindow*)` → allocates `gfx::AcceleratedWidget` via `base::IDMap`
- `RemoveWindow(widget, window)` → removes from IDMap and records
- `GetWindow(widget)` → IDMap lookup
- `GetWidgetRecord(widget)` → `std::map` lookup returning `QnxWidgetRecord*`
- `GetAcceleratedWidgetAtScreenPoint(point)` → iterate windows, hit-test bounds
- `SetGpuAttached()`, `IncrementGeneration()`, `UpdateWidgetSize()`, `SetScreenWindow()`

### `QnxWindow` (implements `PlatformWindow`)
- Constructor: creates `screen_window_t` via `CreateScreenWindow(bounds)`, registers with `QnxWindowManager` (allocates widget ID), calls `delegate->OnAcceleratedWidgetAvailable(widget_)`
- `CreateScreenWindow()`: `screen_create_window()`, `SCREEN_PROPERTY_SIZE`, `SCREEN_PROPERTY_POSITION`, `SCREEN_PROPERTY_USAGE=SCREEN_USAGE_OPENGL_ES2`, `SCREEN_PROPERTY_VISIBLE=1`, `screen_create_window_buffers(1)`
- All `PlatformWindow` methods implemented: `Show/Hide`, `SetBoundsInPixels`, `SetTitle` (via `SCREEN_PROPERTY_ID_STRING`), `SetCapture/ReleaseCapture` (via `SCREEN_PROPERTY_SENSITIVITY`), `Maximize/Minimize/Restore`, `SetFullscreen`, `Activate/Deactivate`, etc.
- Destructor: `screen_destroy_window()`, unregisters from `QnxWindowManager`

### `QnxScreen` (implements `PlatformScreen`)
- Provides one `display::Display` (ID=1, 1024×768, DPR=1.0)
- `GetDisplayForAcceleratedWidget(widget)` → look up window bounds → `GetDisplayMatching()`
- `GetAcceleratedWidgetAtScreenPoint(point)` → `QnxWindowManager::GetAcceleratedWidgetAtScreenPoint()`
- `IsHeadless()` → `false` (QNX Screen is a real display backend)

---

## Key API Decisions

### QNX Screen properties used (all confirmed in `screen.h`)
- `SCREEN_PROPERTY_SIZE`, `SCREEN_PROPERTY_POSITION`, `SCREEN_PROPERTY_USAGE`, `SCREEN_PROPERTY_VISIBLE`, `SCREEN_PROPERTY_ID_STRING`, `SCREEN_PROPERTY_SENSITIVITY`
- `SCREEN_USAGE_OPENGL_ES2` usage flag
- `SCREEN_SENSITIVITY_ALWAYS = 1` for input capture, `SCREEN_SENSITIVITY_NO_FOCUS = 3` for release

### Properties confirmed NOT in QNX Screen SDK
- `SCREEN_PROPERTY_FULLSCREEN` — absent; fullscreen simulated as state change
- `SCREEN_PROPERTY_CAPTURE` — absent; use `SCREEN_PROPERTY_SENSITIVITY` instead
- `screen_set_window_property_char` — absent; use `screen_set_window_property_cv(len, str)` for string titles

---

## Validation

### `git diff --check`
Exit code 0, no whitespace errors across all 13 changed/new files.

### Patch dry-runs
Both GN patches (`--dry-run --forward`) succeeded (Hunk #1 at expected offsets).

### Build
```
source /home/yuta/qnx800/qnxsdp-env.sh
gn gen out/qnx_phase4 --args="$(cat out/qnx_release/args.gn) ozone_platform_qnx=true"
ninja -C out/qnx_phase4 ui/ozone/platform/qnx:qnx
```
Result: **798/798 targets built**, including:
- `obj/ui/ozone/platform/qnx/qnx/qnx_screen_context.o` ✅
- `obj/ui/ozone/platform/qnx/qnx/qnx_window_manager.o` ✅
- `obj/ui/ozone/platform/qnx/qnx/qnx_screen.o` ✅
- `obj/ui/ozone/platform/qnx/qnx/qnx_window.o` ✅
- `obj/ui/ozone/platform/qnx/qnx/ozone_platform_qnx.o` ✅
- `AR obj/ui/ozone/libozone_base.a` ✅

### Temporary root-source files/patches cleaned up
Root tree `ui/ozone/platform/qnx/` and `out/qnx_phase4/` removed after validation.

---

## What Remains for Phase 4 (Subsequent Substeps)

### `QnxPlatformEventSource` (next substep)
- Reads QNX Screen events via `screen_get_event()`
- Dispatches to `PlatformEventDispatcher` / `WindowTreeHost`
- Not needed for compile, but needed for runtime input. Left for a dedicated substep.

### EGL display/composition surface
- `QnxWindow` creates `screen_window_t` but does not yet create `EGLDisplay`, `EGLConfig`, `EGLSurface`, or `EGLContext`
- Phase 5 adds `eglGetDisplay(EGL_DEFAULT_DISPLAY)`, `eglChooseConfig()`, `eglCreateWindowSurface((EGLNativeWindowType)screen_win, ...)`, and the GLES2 composition pipeline

### Mojo frame transport
- Browser↔GPU Mojo endpoints (`QnxGpuHost`, `QnxGpuControl`, `qnx_gpu.mojom`) deferred to Phase 5/6
- Phase 4 skeleton uses `CreateStubGpuPlatformSupportHost()` as placeholder

---

## Blockers / Requested Plan Updates

**None for Phase 4, first substep.** The skeleton compiles and `InitializeUI()` returns `true`.

### Deferred decisions for parent consideration
1. **QnxPlatformEventSource** — minimal compile-safe event source is deferred to next Phase 4 substep. Confirm this sequencing is acceptable before delegating the event source microtask.
2. **EGL display initialization** — should Phase 5 add `EGLDisplay`/`EGLSurface`/`EGLContext` in `QnxWindow` (browser-side display composition) or defer even further?
3. **Display enumeration** — `QnxScreen` currently provides a 1024×768 hardcoded display. When should real QNX Screen display enumeration be wired?

---

## Summary

Phase 4 first substep is complete. The QNX Ozone Browser/UI skeleton compiles and provides:
- `screen_context_t` initialization/destruction
- `screen_window_t` create/destroy with correct properties
- Stable `gfx::AcceleratedWidget` numeric IDs
- Minimal `QnxScreen` for display management
- All `PlatformWindow` methods implemented
- No GPU/Mojo logic (deferred)

**Target: compiles ✅**
