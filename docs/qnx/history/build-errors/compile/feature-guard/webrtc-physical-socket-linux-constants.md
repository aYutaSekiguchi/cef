# WebRTC physical_socket_server must exclude Linux-only PMTU and TCP constants on QNX

- Date: 2026-06-05
- Signature: asm-generic/socket.h missing or IP_PMTUDISC/TCP_USER_TIMEOUT undeclared
- Stage: compile
- Category: feature-guard
- Scope: WebRTC physical socket server

## Symptoms

- `physical_socket_server.cc` failed on missing Linux socket headers and Linux-only path-MTU / TCP constants.

## Root cause

- QNX inherited the `WEBRTC_LINUX` branch through `is_linux`, but the specific Linux uapi constants and socket ioctls used there are not available in the QNX sysroot.

## Fix pattern

- Exclude QNX from Linux-only option-handling branches when the option is not supported and a warning/fallback path already exists.

## Applied change

- Guarded Linux-only include blocks and `OPT_DONTFRAGMENT` / `OPT_TCP_USER_TIMEOUT` logic on `!defined(__QNX__)`.
- Let QNX fall through to existing unsupported-option warning paths.

## Verification

- The previously failing WebRTC object rebuilt successfully.

## Files touched

- `cef/patch/patches/qnx/chromium/webrtc_qnx_physical_socket_server.patch`
- `cef/patch/patch.cfg`
- `third_party/webrtc/rtc_base/physical_socket_server.cc`

## Related notes

- `docs/qnx/build-error-index.md`
