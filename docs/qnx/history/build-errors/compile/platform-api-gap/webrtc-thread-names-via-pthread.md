# WebRTC on QNX must use pthread thread naming instead of Linux prctl

- Date: 2026-06-05
- Signature: linux/prctl.h file not found in platform_thread_types.cc
- Stage: compile
- Category: platform-api-gap
- Scope: WebRTC platform thread types

## Symptoms

- WebRTC compilation failed on missing Linux `prctl` headers and `gettid` assumptions.

## Root cause

- The Linux branch was selected for QNX through `is_linux`, but QNX provides `pthread_setname_np()` rather than `prctl(PR_SET_NAME, ...)`.

## Fix pattern

- When Linux thread-naming and thread-id APIs are missing on QNX, map the implementation to pthread-native equivalents if callers only need opaque identity and name setting.

## Applied change

- Skipped Linux `prctl`/syscall includes for QNX.
- Routed `CurrentThreadId()` to `pthread_self()`.
- Routed `SetCurrentThreadName()` to `pthread_setname_np(pthread_self(), name)`.

## Verification

- The build advanced beyond `platform_thread_types.cc` to the next WebRTC blocker.

## Files touched

- `cef/patch/patches/qnx/chromium/webrtc_qnx_platform_thread_names.patch`
- `cef/patch/patch.cfg`
- `third_party/webrtc/rtc_base/platform_thread_types.cc`

## Related notes

- `docs/qnx/build-error-index.md`
