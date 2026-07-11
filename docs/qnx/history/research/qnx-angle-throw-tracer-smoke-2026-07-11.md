# QNX ANGLE throw-tracer LD_PRELOAD smoke (2026-07-11)

> **SUPERSEDED — corrections applied 2026-07-11.**
>
> Sections marked **[RETIRED]** below contain conclusions that have been
> retracted.  The currently accepted FACT and the corrected interpretation
> are in:
>
> - `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/RESULT.md`
>   — in-process GlobalMutex diagnostic smoke (44 [GMD] events, 2 GPU
>   children), same-thread same-GlobalMutex re-entry CONFIRMED 2/2.
> - `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/FACT-CORRECTION.md`
>   — explicit retraction of the [RETIRED] conclusions below.
>
> The `libangle_throw_tracer.so` LD_PRELOAD design itself is **NOT**
> retracted: G1–G9 gates pass on mini tests, `__cxa_throw` interpose
> fires, and the tool is useful for ANGLE/libGLESv2 frames within the
> limitations documented in RESULT.md.

## Context (historical, pre-correction)

The `libangle_throw_tracer.so` (DESIGN: `qnx-angle-throw-tracer-design-2026-07-11`)
was integrated into the CEF-managed bootstrap pipeline and smoke-tested
against `cefsimple --use-gl=angle --use-angle=gles-egl` under QEMU virgl.

Goal: confirm the LD_PRELOAD interpose fires in the GPU child process
and determine whether the `std::system_error(EDEADLK)` throw site can
be identified.

## Build + Bootstrap (FACT)

- Patch: `cef/patch/patches/qnx/chromium/angle_qnx_throw_tracer.patch` (26-line addition to `third_party/angle/BUILD.gn`)
- New file: `cef/patch/qnx/chromium/new_files/third_party/angle/src/libangle_throw_tracer.cc`
- Registration: `cef/patch/patch.cfg` entry `qnx/chromium/angle_qnx_throw_tracer` (after `angle_qnx_terminate_capture`)
- Bootstrap: `488 patches total (467 applied, 21 skipped, 0 failed)` → GN gen → `Done. Made 33073 targets`
- Build: `ninja libangle_throw_tracer` → `libangle_throw_tracer.so` (23792 bytes), only 1 benign warning (`__builtin_return_address(1)`)

## Smoke Results

### 1. Tracer IS loaded in GPU child processes (FACT)

With `--env LD_PRELOAD=/mnt/nfs/out/qnx_release/libangle_throw_tracer.so`:
- `[ANGLE-THROW-TRACER] libangle_throw_tracer.so LOADED` appears 8+ times
- Multiple child processes (browser, GPU, renderer, utility) load the tracer
- `LD_PRELOAD` propagates through `posix_spawnp` to child processes on QNX (`--no-sandbox`)

### 2. `__cxa_throw` interpose FIRES (FACT, with EGL filter removed)

With the EGL filter temporarily disabled (`if (false && tls_egl_call_depth == 0) {...}`):
```
[ANGLE-THROW-TRACER] __cxa_throw (raw) 0x00000037aef0d691 0x00000037aef3646d
```
The hook fires for each `std::system_error(EDEADLK)` throw and captures
raw return addresses before unwinding.

**Caveat — libc++ caller frames are unreliable on QNX:**
`addr2line` on offset `0x58691` (ra[1]) resolves to
`std::__throw_future_error(future_errc)` at `future:501`, but
disassembly shows the real `__cxa_throw` call site is at offset
`0x586bc` (return `0x586c1`); control flow cannot pass through
`0x58691`.  Similarly, `0x8146d` (ra[2]) does not match the unwind
sequence.  Therefore, throw-site identification from `__cxa_throw`
hook frames for libc++ internal callers is not reliable on QNX
(this is an unwinder/library limitation, not a tracer design defect).
ANGLE/libGLESv2 caller frames are adoptable individually when
`nm`, `objdump` disassembly, and `addr2line` independently agree
(three-point confirmation), as done for `libGLESv2.so+0x9eb85`
(`EGL_GetDisplay` size 128, `entry_points_egl_autogen.cpp:498`,
instruction at `0x9eb80` calls `ScopedGlobalMutexLock<0>::ScopedGlobalMutexLock()`).

With the EGL filter ENABLED (production mode): no `__cxa_throw`
output — because `tls_egl_call_depth == 0` at throw time.  The filter
correctly suppresses output for throws outside EGL wrappers, BUT
this same observation is consistent with either (a) a throw on a
different thread, (b) a throw before `tls_push_egl_call`, or (c) a
throw via ANGLE's `eglGetProcAddress` + function-pointer call (which
does NOT pass through our wrappers).  These alternatives are NOT
distinguishable by the EGL filter alone.

### 3. ~~Throw is on a DIFFERENT thread than EGL entry wrappers~~ [RETIRED]

**[RETIRED 2026-07-11]:** the original conclusion "different thread"
was based on `tls_egl_call_depth == 0` at throw time and is not a
proof of thread identity.  The current in-process GlobalMutex
diagnostic (see RESULT.md) shows the same-thread same-GlobalMutex
re-entry pattern with **same tid=1**, contradicting the "different
thread" hypothesis.  The EGL filter observation was consistent with
multiple mutually-exclusive alternatives; treating it as proof of
thread identity was a category error.

### 4. EGL catch block does NOT fire [RETIRED]

**[RETIRED]:** the original explanation ("throw on different thread
so catch never fires") was based on the retired §3.  The EGL catch
does not fire in the LD_PRELOAD smoke because **ANGLE calls EGL via
`eglGetProcAddress` + function-pointer indirection** (verified by
`nm -u libGLESv2.so | grep egl` returning no undefined EGL
symbols).  LD_PRELOAD only interposes EGL functions reached through
dynamic symbols; indirect calls bypass our wrappers entirely.
This is the actual reason, not the thread story.

### 5. Throw site: `libGLESv2.so` — `std::set<string>::find` ~~(ANGLE TLS index map)~~ [RETIRED]

**[RETIRED]:** the original conclusion "ANGLE TLS index map" was
based on:
- a single post-unwind frame from `terminate_capture_qnx.cc`
  (`std::__tree::find`),
- nm lookup showing `std::__tree::find` is a WEAK template
  instantiation in `libGLESv2.so`,
- and disassembly reverse-calculation from the dladdr `+0x4d9f`
  offset.

**The disassembly reverse-calculation is unreliable** (supervisor
audit, 2026-07-11): `+0x4d9f` is the offset from `dladdr`'s
"nearest preceding symbol" report, not a verified intra-function
offset; the WEAK template instantiation may not be the actual
call site.  The actual `std::__tree::find` function is a generic
C++ template instantiation that can be called from anywhere that
uses `std::set<std::string>::find()` — including GL extension-set
processing (`DispatchTableGL::initProcsSharedExtensions(const
std::set<std::string>&)`).  Calling this the "ANGLE TLS index map"
was unsupported speculation.

### 6. ~~DIAGNOSTIC FINDING: the recursive mutex is in the TLS index map, not the global EGL mutex~~ [RETIRED]

**[RETIRED]:** based on §3 and §5.  Superseded by the in-process
GlobalMutex diagnostic (RESULT.md), which shows same-thread
same-GlobalMutex re-entry on `egl::priv::GlobalMutex` (non-recursive
default variant, which is exactly the configuration that aborts on
re-entry).

## Impact on DESIGN.md (corrected)

- **§4.2 (EGL context filter)**: the filter is correct *as
  designed* but assumes the throw occurs inside our EGL wrappers.
  This assumption fails for ANGLE's `eglGetProcAddress` indirection
  path.  The filter is therefore not a useful gate for ANGLE
  diagnostics.
- **§4.4 (Catch and `::_exit(1)`)**: the `::_exit(1)` path never
  executes because ANGLE's call path bypasses our wrappers.
- **The tracer's `__cxa_throw` interpose is functional**: it can
  capture throw-site addresses regardless of thread, but
  libc++ internal caller frames are not reliably unwound on QNX
  (see §2 caveat).  ANGLE/libGLESv2 caller frames require
  three-point confirmation (nm + objdump + addr2line) for adoption.

## Status of `angle_qnx_throw_tracer` patch (LD_PRELOAD tracer)

- The tracer itself is **retained**: G1–G9 gates pass on mini tests;
  `__cxa_throw` interpose fires; useful for ANGLE/libGLESv2 frames
  with three-point symbol confirmation.
- The conclusion in this smoke that the throw site was at the
  ANGLE TLS index map is retracted; the in-process GlobalMutex
  diagnostic provides the corroborated evidence instead
  (RESULT.md).

## Corrected interpretation (FACT)

- Same-thread same-GlobalMutex re-entry on ANGLE `egl::priv::GlobalMutex`
  (default, non-recursive variant) is **CONFIRMED** for two GPU
  child processes in the in-process GlobalMutex diagnostic smoke.
  See RESULT.md for evidence.
- The recursive lock fix would require `ANGLE_ENABLE_GLOBAL_MUTEX_RECURSION`
  or an equivalent code change to `GlobalMutex::lock()` itself,
  not the unrelated `angle::priv::MutexOnStd` class.

## See also

- `docs/qnx/history/research/qnx-angle-throw-tracer-design-2026-07-11/DESIGN.md`
  — original design (LD_PRELOAD), not retracted but its assumptions
  refined.
- `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/RESULT.md`
  — current accepted FACT.
- `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/FACT-CORRECTION.md`
  — explicit retraction list.