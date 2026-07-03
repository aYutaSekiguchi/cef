# QNX Ozone Phase 4 — Browser/UI-side Screen Event Source

**Date:** 2026-07-03
**Phase:** 4, second substep
**Status:** Complete — target compiles

## Scope

Add a minimal, compile-safe `QnxPlatformEventSource` that polls QNX Screen events via `screen_get_event()` and integrates with the Phase 4 Browser/UI skeleton from the first substep.

Not implemented (Phase 4 scope guardrails):
- GPU producer input routing
- DMAbuf/Mojo frame transport
- Full IME
- Dedicated event-dispatch thread (uses timer-based polling on main thread instead)

---

## Files Changed

### CEF-managed new files (all under `cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/`)

| File | Lines | Responsibility |
|---|---|---|
| `qnx_platform_event_source.h` | 71 | `QnxPlatformEventSource` class declaration |
| `qnx_platform_event_source.cc` | 305 | Implementation: polling loop, Screen event translation |
| `BUILD.gn` | +8 | Added the two new source files |
| `ozone_platform_qnx.cc` | +18 | Own/start the event source in `InitializeUI()` |

### CEF-managed patches (unchanged)
- `patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch` — still requires application before build
- `patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch` — still requires application before build

---

## Class Design: `QnxPlatformEventSource`

### Inheritance
```
PlatformEventSource (ui/events/platform/platform_event_source.h)
    └── QnxPlatformEventSource
```

### Construction / Initialization
- Constructor takes `screen_context_t` (from `QnxScreenContext`) and `QnxWindowManager*`
- Creates one `screen_event_t` handle via `screen_create_event()` — reused for all polling
- Registers itself as the thread-local `PlatformEventSource` singleton via `PlatformEventSource` base class constructor (`thread_local` + `AutoReset`)

### Polling loop
- Timer-based polling using `SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask()`
- Poll interval: 16 ms (~60 Hz), matching the event timeout
- Each tick calls `screen_get_event(context, event, 16_000_000ns)` (non-blocking timeout)
- If `event_type != SCREEN_EVENT_NONE`: call `TranslateScreenEvent()`
- Always re-posts the next tick if `weak_factory_.HasWeakPtrs()` is true
- `Stop()` calls `weak_factory_.InvalidateWeakPtrs()` to break the loop

### Event translation
Phase 4 handles a conservative subset:
- **SCREEN_EVENT_CLOSE**: logged (close propagation deferred)
- **SCREEN_EVENT_KEYBOARD**: logged with sym/flags (full translation deferred)
- **SCREEN_EVENT_POINTER**: logged with position/buttons (full translation deferred)
- **SCREEN_EVENT_MTOUCH_TOUCH/MOVE/RELEASE**: logged (full translation deferred)
- **SCREEN_EVENT_DISPLAY**: logged (display re-enumeration deferred)
- **SCREEN_EVENT_IDLE**: logged (idle tracking deferred)
- All other event types: logged at `VLOG(1)` level with type name, window handle, name, and timestamp

### `OzonePlatformQnxImpl` wiring
```cc
// In InitializeUI(), after screen_context_ and window_manager_:
event_source_ = std::make_unique<QnxPlatformEventSource>(
    screen_context_->context(), window_manager_.get());
if (!QnxPlatformEventSource::GetInstance())
  return false;
event_source_->Start();  // posts first OnTimerTick via task runner
```

### Lifetime safety
- `OzonePlatformQnxImpl` owns `event_source_` via `std::unique_ptr`
- `~QnxPlatformEventSource()` calls `Stop()` then `screen_destroy_event()`
- `weak_factory_` invalidation in `Stop()` prevents pending tasks from running after destruction

### Key API decisions

**QNX Screen APIs used:**
- `screen_create_event(screen_event_t*)` — creates reusable event handle
- `screen_get_event(screen_context_t, screen_event_t, uint64_t timeout_ns)` — polls with short non-blocking timeout
- `screen_get_event_property_iv(screen_event_t, SCREEN_PROPERTY_TYPE, int*)` — gets event type
- `screen_get_event_property_pv(screen_event_t, SCREEN_PROPERTY_WINDOW, void**)` — gets source window
- `screen_get_event_property_cv(screen_event_t, SCREEN_PROPERTY_NAME, int, char*)` — gets window name
- `screen_get_event_property_llv(screen_event_t, SCREEN_PROPERTY_TIMESTAMP, long long*)` — gets timestamp
- `screen_destroy_event(screen_event_t)` — destroys event handle

**Event constants confirmed:**
```
SCREEN_EVENT_NONE = 0
SCREEN_EVENT_CLOSE = 3
SCREEN_EVENT_POINTER = 6
SCREEN_EVENT_KEYBOARD = 7
SCREEN_EVENT_DISPLAY = 11
SCREEN_EVENT_IDLE = 12
SCREEN_EVENT_MTOUCH_TOUCH = 100
SCREEN_EVENT_MTOUCH_MOVE = 101
SCREEN_EVENT_MTOUCH_RELEASE = 102
```

**Task runner:** `SequencedTaskRunner::GetCurrentDefault()` — available on the main UI thread after `InitializeUI()` is called. Does not require `SingleThreadTaskExecutor::GetCurrentUI()` which is not available in this Chromium version.

**`screen_context_t` storage:** stored as plain `screen_context_t` (not `raw_ptr<screen_context_t>`) because `raw_ptr<T>` requires complete type for the pointee, and `screen_context_t` is an opaque forward-declared type in the Chromium build.

---

## Validation

### Build
```sh
source /home/yuta/qnx800/qnxsdp-env.sh
git apply cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch
git apply cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch
cp cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn \
   cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/*.cc \
   cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/*.h \
   ui/ozone/platform/qnx/
gn gen out/qnx_phase4_event \
  --args="$(cat out/qnx_release/args.gn; echo 'ozone_platform_qnx=true')"
ninja -C out/qnx_phase4_event ui/ozone/platform/qnx:qnx
```

Result: **797/797 targets built**, including:
- `obj/ui/ozone/platform/qnx/qnx/qnx_platform_event_source.o` ✅
- `obj/ui/ozone/platform/qnx/qnx/ozone_platform_qnx.o` ✅
- `AR obj/ui/ozone/libozone_base.a` ✅

### Temporary root-source files/patches cleaned up
- `build/config/ozone.gni` — reverted with `git checkout`
- `ui/ozone/BUILD.gn` — reverted with `git checkout`
- `ui/ozone/platform/qnx/` directory — removed (contained BUILD.gn + 13 source files)
- `out/qnx_phase4_event/` object files — deleted (intermediate artifacts)

### `git diff --check`
Exit code 0, no whitespace errors across changed files.

---

## Compile Fixes Applied During Validation

1. **`raw_ptr<screen_context_t>` → `screen_context_t`**: `raw_ptr<T>` requires a complete type for the pointee. `screen_context_t` is a forward-declared opaque pointer in Chromium's QNX build, so wrapping it in `raw_ptr` caused a build error. Changed to plain `screen_context_t` (which is already a pointer type `struct _screen_context*`).

2. **`SingleThreadTaskExecutor::GetCurrentUI()` → `SequencedTaskRunner::GetCurrentDefault()`**: The `GetCurrentUI()` static method does not exist in this Chromium version. Replaced with `SequencedTaskRunner::GetCurrentDefault()` which is available on the main UI thread after `InitializeUI()` is called.

3. **`base::TimeDelta::FromMilliseconds()` → `base::Milliseconds()`**: `TimeDelta::FromMilliseconds()` is not a static method on `TimeDelta`. Used the free function `base::Milliseconds(16)` instead.

4. **Missing closing brace**: The `if (task_runner)` block in `OnTimerTick()` was missing its closing `}`. Added the missing brace.

5. **Missing `#include <base/memory/scoped_refptr.h>`**: Added for `scoped_refptr<base::SequencedTaskRunner>` in the .cc file.

---

## What Remains for Phase 4 Subsequent Substeps

### Mojo attach hooks
- Browser↔GPU Mojo endpoints (`QnxGpuHost`, `QnxGpuControl`, `qnx_gpu.mojom`) are deferred to Phase 5/6
- `QnxPlatformEventSource` does not yet route events to the GPU process

### EGL display initialization
- `QnxWindow` creates `screen_window_t` but does not yet create `EGLDisplay`, `EGLConfig`, `EGLSurface`, or `EGLContext`
- Phase 5 adds browser-side display composition via `eglCreateWindowSurface((EGLNativeWindowType)screen_win, ...)`

### Full event translation
- Keyboard, pointer, and touch events are logged but not dispatched as `ui::KeyEvent`/`ui::MouseEvent`/`ui::TouchEvent`
- Requires `KeyboardLayoutEngine` and `EventConverter` wiring similar to `EventFactoryEvdev` on Linux DRM

### Display enumeration
- `SCREEN_EVENT_DISPLAY` events are logged but not used to update `QnxScreen`'s display list
- Real QNX Screen display enumeration via `screen_get_display_property_*()` deferred

### Window-to-event dispatch integration
- `SCREEN_EVENT_CLOSE` is logged but not propagated to the correct `PlatformWindowDelegate`
- Requires mapping `screen_window_t` → `QnxWindow*` → widget → `PlatformEventDispatcher` (similar to cast platform pattern)

---

## Blockers / Requested Plan Updates

**None for this substep.** The target compiles and the event source skeleton is wired.

### Deferred decisions for parent consideration

1. **Event translation pipeline**: Should Phase 5 add `EventConverterQnx` (similar to `EventFactoryEvdev`) for keyboard/pointer/touch translation, or should this be deferred to Phase 6?

2. **Mojo frame transport**: When should the Phase 2 design's QNX-local Mojo interfaces (`qnx_gpu.mojom`) be introduced? Phase 5 (GPU producer) or Phase 6 (crash recovery)?

3. **Display enumeration**: When should `QnxScreen` be updated to use `SCREEN_EVENT_DISPLAY` and `screen_get_display_property_*()` for real display enumeration instead of the 1024×768 hardcoded display?

4. **Event source thread model**: The current timer-based polling on the main thread is a Phase 4 compromise. When should a dedicated `base::Thread` be introduced for the event pump? This would require `base/threading/thread.h` and would better match the `DrmThread` pattern in the Linux DRM platform.

---

## Summary

Phase 4 second substep is complete. `QnxPlatformEventSource` compiles and provides:
- `screen_event_t` lifecycle (create/destroy)
- Timer-based polling loop at ~60 Hz with 16ms timeout
- Conservative event translation (logs + partial structure for close/keyboard/pointer/touch/display/idle)
- Thread-local `PlatformEventSource` singleton registration
- Safe shutdown via `weak_factory_.InvalidateWeakPtrs()`

**Target: compiles ✅**
