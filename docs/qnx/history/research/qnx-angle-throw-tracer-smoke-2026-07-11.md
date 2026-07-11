# QNX ANGLE throw-tracer LD_PRELOAD smoke (2026-07-11)

## Context

The `libangle_throw_tracer.so` (DESIGN: `qnx-angle-throw-tracer-design-2026-07-11`) was integrated into the CEF-managed bootstrap pipeline and smoke-tested against `cefsimple --use-gl=angle --use-angle=gles-egl` under QEMU virgl.

Goal: confirm the LD_PRELOAD interpose fires in the GPU child process and determine whether the `std::system_error(EDEADLK)` throw site can be identified.

## Build + Bootstrap

- Patch: `cef/patch/patches/qnx/chromium/angle_qnx_throw_tracer.patch` (26-line addition to `third_party/angle/BUILD.gn`)
- New file: `cef/patch/qnx/chromium/new_files/third_party/angle/src/libangle_throw_tracer.cc`
- Registration: `cef/patch/patch.cfg` entry `qnx/chromium/angle_qnx_throw_tracer` (after `angle_qnx_terminate_capture`)
- Bootstrap: `488 patches total (467 applied, 21 skipped, 0 failed)` → GN gen → `Done. Made 33073 targets`
- Build: `ninja libangle_throw_tracer` → `libangle_throw_tracer.so` (23792 bytes), only 1 benign warning (`__builtin_return_address(1)`)

## Smoke Results

### 1. Tracer IS loaded in GPU child processes

With `--env LD_PRELOAD=/mnt/nfs/out/qnx_release/libangle_throw_tracer.so`:
- `[ANGLE-THROW-TRACER] libangle_throw_tracer.so LOADED` appears 8+ times
- Multiple child processes (browser, GPU, renderer, utility) load the tracer
- `LD_PRELOAD` propagates through `posix_spawnp` to child processes on QNX (`--no-sandbox`)

### 2. `__cxa_throw` interpose FIRES (verified by removing EGL filter)

With the EGL filter temporarily disabled (`if (false && tls_egl_call_depth == 0) {...}`):
```
[ANGLE-THROW-TRACER] __cxa_throw (raw) 0x00000037aef0d691 0x00000037aef3646d
```
- The hook fires for each `std::system_error(EDEADLK)` throw
- Raw return addresses captured before unwinding

With the EGL filter ENABLED (production mode):
- No `__cxa_throw` output — because `tls_egl_call_depth == 0` at throw time
- The filter correctly suppresses output for throws outside EGL wrappers

### 3. Throw is on a DIFFERENT thread than EGL entry wrappers

The EGL filter finding proves that `tls_egl_call_depth == 0` when the throw occurs. This means the throw does NOT happen inside our EGL entry wrappers. Possible explanations:
- ANGLE spawns an internal thread for EGL initialization (likely: `DisplayEGL` creates a worker thread)
- The recursive `std::mutex::lock()` is called on that worker thread, not the main thread that called `eglGetPlatformDisplay`
- The `thread_local tls_egl_call_depth` is per-thread, so the worker thread sees depth=0

### 4. EGL catch block does NOT fire

The `try/catch(std::system_error&)` in our EGL entry wrappers never fires because the throw is on a different thread. The exception propagates on the worker thread, hits no catch handler, and `std::terminate()` is called → SIGABRT (exit code 134).

### 5. Throw site: `libGLESv2.so` — `std::set<string>::find` (ANGLE TLS index map)

The `angle_qnx_terminate_capture` termination handler (set via `std::set_terminate`) captures 1 post-unwind frame:
```
#00 0x15c637ef7f libGLESv2.so :: std::__2::__tree<...>::find(...)
```
This is ANGLE's TLS index map (`std::set<std::string>`) lookup, called during per-thread EGL initialization. The mutex protecting this TLS map is locked recursively → `EDEADLK`.

### 6. DIAGNOSTIC FINDING: the recursive mutex is in the TLS index map, not the global EGL mutex

Previous investigation assumed the `ScopedGlobalEGLMutexLock` inside `eglGetPlatformDisplay` was the source. The smoke evidence suggests the recursive lock is on ANGLE's TLS index map, which is accessed from a worker thread spawned inside `eglInitialize` or a similar EGL call.

## Impact on DESIGN.md

- **§4.2 (EGL context filter)**: The assumption that the throw happens inside EGL wrappers was WRONG. The throw is on a different thread where `tls_egl_call_depth` is always 0. The EGL catch blocks never fire.
- **§4.4 (Catch and `::_exit(1)`)**: The `::_exit(1)` path never executes because the exception propagates to `std::terminate()` on the worker thread.
- **The tracer's `__cxa_throw` interpose is still functional**: it can capture throw-site addresses regardless of thread, but needs the EGL filter removed to be useful for ANGLE diagnostics.

## Recommended Next Steps

1. **System EGL linkage** (proven path): `LD_PRELOAD=/usr/lib/libEGL.so.1` makes native CEF GPU producer reach `accepted=true; eglSwapBuffers reached`. The permanent fix (`cef/BUILD.gn:1099` → system Mesa EGL) is the right direction for Phase 7.

2. **ANGLE explicit runtime**: The recursive mutex is on ANGLE's TLS index map, not the global EGL mutex. A thread-local TLS slot initialization collides with the same slot's lock. Diagnosis would require:
   - Remove EGL filter from `__cxa_throw` interpose
   - Add `dladdr()`-based symbol resolution to identify exact call site
   - Add thread-ID to output to confirm thread isolation
   - Consider adding `Dl_info` resolution in the tracer (like `terminate_capture_qnx.cc`)

3. **The `angle_qnx_throw_tracer` patch is diagnostic-only** and should remain; it's useful for future ANGLE debugging with filter removed.

## Tracer Design Change (applied)

Constructor now outputs a `[ANGLE-THROW-TRACER] libangle_throw_tracer.so LOADED` message via `::write(2,...)` to confirm loading in each process. This is a permanent diagnostic feature.

EGL filter comment updated with diagnostic finding.
