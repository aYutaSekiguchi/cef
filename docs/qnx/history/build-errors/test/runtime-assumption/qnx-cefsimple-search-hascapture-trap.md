# QNX cefsimple search activation traps in HasCapture

- Date: 2026-07-17
- Signature: `trace trap (core dumped)` / `ui::QnxWindow::HasCapture() const`
- Stage: `test`
- Category: `platform-api-gap`
- Scope: QNX Ozone `PlatformWindow` capture state and cefsimple GUI input

## Symptoms

With a GUI-enabled QEMU session, Google loaded in `cefsimple`, but activating
the search form caused the browser window to disappear and return to the QNX
desktop. The browser process exited while renderer/GPU children remained in
`CONDVAR`. The sequence `click search field -> type -> Enter` reproduced this
in 2/2 trials. `Tab` did not reproduce it; `Space` also reproduced it.

## Root cause

`QnxWindow::SetCapture()` and `ReleaseCapture()` configured
`SCREEN_PROPERTY_SENSITIVITY`, but `QnxWindow::HasCapture()` was still:

```cpp
NOTREACHED();
return false;
```

Chromium Views activation/capture checks reach `PlatformWindow::HasCapture()`.
The crash IP resolved to the `HasCapture()` symbol's `libcef.so` offset, and
the function contained the `NOTREACHED()` trap sequence. QNX Screen documents
`SCREEN_PROPERTY_SENSITIVITY` as readable, but a local state mirror is safer
than querying Screen on every `HasCapture()` call.

## Fix pattern

Maintain a `capture_state_` boolean. Set it only after a successful
`screen_set_window_property_iv()` call in `SetCapture()`, clear it after a
successful release, clear it when destroying the Screen window, and return it
from `HasCapture()`.

## Applied change

The fix was applied to:

- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window.h`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window.cc`

The change was intentionally limited to capture state; no Chromium source
patch outside the managed CEF source-of-truth was retained.

## Verification

- Incremental `cefsimple`/`libcef.so` build succeeded.
- `nm`/instruction inspection showed `HasCapture()` loading `capture_state_`
  and returning instead of containing the trap sequence.
- Four GUI trials completed without browser disappearance, SIGTRAP, or new
  dumper core: Enter twice, Space once, and Tab once.
- Process count stayed stable at approximately 90-91; before the fix it fell
  sharply and left child processes hung.
- A separate `browser_info_manager` timeout and QNX DNS/NSSWITCH failures
  remained; these are follow-up issues and were not conflated with this crash.

## Files touched

- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window.h`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window.cc`

## Related notes

- `docs/qnx/history/research/qnx-cefsimple-input-implementation-plan-2026-07-13.md`
- `docs/qnx/gui-testing.md`
