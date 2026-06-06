# WebRTC byte_order.h must map glibc endian helpers onto QNX qh/endian.h

- Date: 2026-06-05
- Signature: endian.h file not found in rtc_base/byte_order.h
- Stage: compile
- Category: platform-api-gap
- Scope: WebRTC endian helpers

## Symptoms

- WebRTC failed to compile because `rtc_base/byte_order.h` included glibc `<endian.h>`, which QNX does not ship.

## Root cause

- QNX exposes endian conversion helpers through `<qh/endian.h>` and `ENDIAN_*` macros rather than glibc short names such as `htobe16`.

## Fix pattern

- Keep endian shims scoped to the consuming header rather than force-including them globally, unless multiple subsystems need the same mapping.

## Applied change

- Added a QNX-specific include of `<qh/endian.h>` and mapped glibc-style helper names onto QNX `ENDIAN_*` macros with `#ifndef` guards.

## Verification

- The previously failing WebRTC object rebuilt successfully and the build moved on to the next Linux-uapi blocker.

## Files touched

- `cef/patch/patches/qnx/chromium/webrtc_qnx_byte_order_endian.patch`
- `cef/patch/patch.cfg`
- `third_party/webrtc/rtc_base/byte_order.h`

## Related notes

- `docs/qnx/build-error-index.md`
